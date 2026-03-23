# Phase 10 -- Multi-Slice Support

## Objectif

Decoder correctement les pictures contenant plusieurs slice segments (independants et dependants).
Les 4 tests en echec (`conf_i_multislice_256`, `conf_b_xslice_256`, `oracle_bbb1080_50f`, `oracle_bbb4k_25f`) doivent passer.

## Prerequis

Phase 6 completee (loop filters fonctionnels, single-slice pixel-perfect).
Phase 7.5 completee (tiles fonctionnels — la logique de boundary exclusion est partagee).

## Spec refs

- S7.3.6.1 : Slice segment header — `first_slice_segment_in_pic_flag`, `dependent_slice_segment_flag`, `slice_segment_address`
- S7.4.7.1 : Slice segment header semantics — derivation de `SliceAddrRs`, heritage des champs pour dependent slices
- S7.3.8.1 : Slice segment data — boucle CTU `do { coding_tree_unit(); end_of_slice_segment_flag; ... } while(!end_of_slice_segment_flag)`
- S7.3.8.3 : SAO syntax — `leftCtbInSliceSeg = CtbAddrInRs > SliceAddrRs`, `upCtbInSliceSeg = (CtbAddrInRs - PicWidthInCtbsY) >= SliceAddrRs`
- S8.7.2.1 : Deblocking filter — `filterLeftCbEdgeFlag` / `filterTopCbEdgeFlag` mis a 0 quand le bord est une frontiere de slice et `slice_loop_filter_across_slices_enabled_flag == 0`
- S8.7.3 : SAO — `slice_sao_luma_flag` / `slice_sao_chroma_flag` par slice
- S9.3.2.1 : CABAC init — reinitialisation par slice independant, synchronisation contextes pour dependent slices (`TableStateIdxDs`, `TableMpsValDs`)

## Pourquoi cette phase est distincte

Le support multi-slice touche **4 couches du decodeur** :

1. **Boucle de decodage** (`decoder.cpp`) : iterer sur tous les VCL NALs d'une picture
2. **Parsing** (`slice_header.cpp`) : heritage des champs pour dependent slices
3. **Deblocking** (`deblocking.cpp`) : exclusion des edges aux frontieres inter-slice
4. **SAO** (`coding_tree.cpp`, `sao.cpp`) : merge et application respectant les frontieres inter-slice

La structure per-slice CTU est deja correcte (`decode_slice_segment_data` respecte `slice_segment_address` et `end_of_slice_segment_flag`). Le probleme est que cette fonction n'est appelee qu'une seule fois par picture (premier slice uniquement), et que les filtres ne connaissent que le dernier slice boundary.

## Decoupe en sous-etapes (5 etapes)

| Etape | Sujet | Validation | Dependances |
|-------|-------|------------|-------------|
| **10.1** | Slice index map (per-CTU) | Tests existants ne regressent pas (tous les CTU ont `slice_idx = 0`) | -- |
| **10.2** | Boucle multi-slice dans `decode_picture()` | `conf_i_multislice_256` pixel-perfect | 10.1 |
| **10.3** | Heritage dependent slices | Conformite spec (dependent slices heritent QP, type, ref lists, SAO flags, etc.) | 10.2 |
| **10.4** | Cross-slice deblocking | `conf_b_xslice_256` pixel-perfect | 10.1, 10.2 |
| **10.5** | Cross-slice SAO | `oracle_bbb1080_50f` + `oracle_bbb4k_25f` pixel-perfect | 10.1, 10.2, 10.4 |

## Graphe de dependances

```
10.1 (Slice index map) ──> 10.2 (Boucle multi-slice) ──> 10.3 (Dependent slices)
         │                          │
         │                          v
         └──────────────> 10.4 (Cross-slice deblocking)
                                    │
                                    v
                           10.5 (Cross-slice SAO)
```

10.3 est independant de 10.4/10.5 (parallelisable apres 10.2).

---

### 10.1 -- Slice Index Map

**Spec ref** : S7.4.7.1 — `SliceAddrRs` derivation, S7.3.8.3 — `leftCtbInSliceSeg`, `upCtbInSliceSeg`.

**Contexte** : Les filtres (deblocking, SAO) ont besoin de savoir si deux CTUs adjacents appartiennent a des slices differents. Actuellement, seul `slice_segment_address` du dernier slice parse est compare — insuffisant pour 3+ slices. La spec utilise `SliceAddrRs` qui est derive per-slice, mais les filtres operent apres le decodage de TOUS les slices. Il faut donc stocker l'appartenance slice de chaque CTU.

**Taches** :
- [ ] Ajouter `std::vector<uint8_t> slice_idx_buf_` dans `Decoder` (taille = `PicSizeInCtbsY`)
- [ ] Ajouter `uint8_t* slice_idx` + `int slice_idx_stride` dans `DecodingContext`
- [ ] Initialiser a 0 en debut de picture (dans `decode_picture()`)
- [ ] Dans `decode_slice_segment_data()`, ecrire l'index du slice courant pour chaque CTU decode : `ctx.slice_idx[CtbAddrInRs] = current_slice_idx`

**Pieges identifies :**
- Le stride est `PicWidthInCtbsY`, pas `PicWidthInMinCbsY` — la granularite est CTB, pas min-CB
- L'index doit etre passe a `decode_slice_segment_data()` comme parametre (ou via `DecodingContext`)
- Les pictures single-slice auront tous les CTU a 0 — pas de regression possible

**Critere de sortie** :
- [ ] Build OK, tous les tests existants passent (0 regression)

---

### 10.2 -- Boucle Multi-Slice dans `decode_picture()`

**Spec ref** : S7.3.8.1 — slice_segment_data (boucle CTU), S8.1.3 — decoding process per coded picture.

**Contexte** : `decode_picture()` ne traite actuellement que `nals[first_vcl_idx]`. Les NAL VCL suivants (slices 2, 3, ...) sont ignores dans la boucle `decode()` (lignes 34-43 de `decoder.cpp`). Il faut :
1. Collecter tous les VCL NALs de la meme picture
2. Boucler sur chaque NAL : parser le slice header, avancer le bitstream, appeler `decode_slice_segment_data()`
3. Appliquer les filtres une seule fois a la fin

**Taches** :
- [ ] Modifier `decode()` pour collecter le range `[first_vcl_idx, last_vcl_idx)` des NAL VCL d'une meme picture (en verifiant `first_slice_segment_in_pic_flag == 0` pour les NAL suivants)
- [ ] Passer ce range a `decode_picture()`
- [ ] Dans `decode_picture()`, boucler sur chaque NAL VCL du range :
  - Parser le slice header (avec heritage si dependent — voir 10.3)
  - Mettre a jour `ctx.sh` pour pointer vers le header du slice courant
  - Creer un `BitstreamReader` sur le RBSP du NAL courant
  - Avancer le bitstream apres le slice header (re-parse pour skip)
  - Byte-align
  - Appeler `decode_slice_segment_data(ctx, bs)` avec le `slice_idx` courant
- [ ] Construire les ref pic lists a partir du **premier** slice header independant (S8.3.4 — les ref lists sont per-picture, pas per-slice)
- [ ] POC derive depuis le premier slice uniquement (S8.3.1)
- [ ] Filtres (deblocking + SAO) appliques **une seule fois** apres tous les slices

**Pieges identifies :**
- `SliceQpY` peut varier entre slices independants — le `ctx.sh` doit pointer vers le bon header pendant le decodage de chaque slice
- Entry point offsets (WPP, tiles) sont per-slice — `decode_slice_segment_data` les utilise deja correctement
- Le CABAC engine doit etre reinitialise pour chaque slice independant (deja gere dans `decode_slice_segment_data` via le check `!sh.dependent_slice_segment_flag`)
- Les SAO params sont parses dans la boucle CTU de chaque slice — ils s'ecrivent dans `sao_params_buf_` aux bonnes positions grace a `slice_segment_address`

**Critere de sortie** :
- [ ] `conf_i_multislice_256` pixel-perfect (I-frame, 4 slices independants)
- [ ] Non-regression : tous les tests single-slice passent toujours

---

### 10.3 -- Heritage Dependent Slices

**Spec ref** : S7.4.7.1 — "dependent_slice_segment_flag equal to 1 specifies that the value of each slice segment header syntax element that is not present is inferred to be equal to the value of the corresponding slice segment header syntax element in the slice header."

**Contexte** : Quand `dependent_slice_segment_flag == 1`, le slice header ne contient que `slice_segment_address`. Tous les autres champs (slice_type, SliceQpY, SAO flags, ref lists, deblocking params, weighted pred, etc.) doivent etre herites du slice independant precedent. Actuellement, `slice_header.cpp` (ligne 141-143) retourne `true` sans copier aucun champ — les valeurs restent a leur initialisation par defaut (0).

**Spec ref pour CABAC** : S9.3.2.1 — quand `CtbAddrInRs == slice_segment_address && dependent_slice_segment_flag == 1`, les contextes CABAC sont synchronises depuis `TableStateIdxDs` / `TableMpsValDs` (sauvegardes a la fin du slice precedent). Ce mecanisme est deja implemente dans `decode_slice_segment_data()`.

**Taches** :
- [ ] Modifier `SliceHeader::parse()` pour accepter un parametre optionnel `const SliceHeader* prev_independent_sh`
- [ ] Quand `dependent_slice_segment_flag == 1` : copier tous les champs du `prev_independent_sh` AVANT le `return true`, en preservant `first_slice_segment_in_pic_flag`, `slice_segment_address`, et `dependent_slice_segment_flag` (qui viennent d'etre parses)
- [ ] Dans `decode_picture()` (boucle 10.2), maintenir un pointeur vers le dernier `SliceHeader` independant parse, et le passer a `parse()` pour les dependent slices
- [ ] Verifier : `SliceAddrRs` derivation — pour un dependent slice, `SliceAddrRs` est herite du slice independant contenant le CTB precedant `slice_segment_address` (S7.4.7.1). Cela affecte les checks SAO `leftCtbInSliceSeg` / `upCtbInSliceSeg`

**Pieges identifies :**
- Ne PAS copier `slice_segment_address` — c'est le seul champ propre au dependent slice
- `no_output_of_prior_pics_flag` n'est pas present pour les dependent slices (uniquement IRAP + first_slice)
- `SliceAddrRs` pour dependent slice ≠ `slice_segment_address` — c'est le `SliceAddrRs` du slice independant parent. Cela elargit la zone "in slice" pour les checks SAO merge

**Critere de sortie** :
- [ ] Un bitstream avec dependent slices decode correctement (si bitstream de test disponible)
- [ ] Non-regression : les 4 bitstreams multi-slice (qui utilisent des slices independants) passent toujours

---

### 10.4 -- Cross-Slice Deblocking

**Spec ref** : S8.7.2.1 — derivation de `filterLeftCbEdgeFlag` et `filterTopCbEdgeFlag` :
> "The left boundary of the current luma coding block is the left boundary of the slice and `slice_loop_filter_across_slices_enabled_flag` is equal to 0" → `filterLeftCbEdgeFlag = 0`
> (idem pour `filterTopCbEdgeFlag` avec "top boundary")

**Contexte** : La spec utilise "boundary of the slice" — c'est-a-dire la frontiere entre deux slices quelconques, pas seulement la frontiere du dernier slice. L'implementation actuelle (`deblocking.cpp:96-113`) compare `addr < sh.slice_segment_address` — ne fonctionne que pour la frontiere entre le premier et le dernier slice. Avec la slice index map (10.1), on peut tester `slice_idx[addr_p] != slice_idx[addr_q]`.

**Taches** :
- [ ] Remplacer la logique de `is_boundary_excluded()` pour le check slice :
  - Ancien : `addr_p < sh.slice_segment_address && addr_q >= sh.slice_segment_address`
  - Nouveau : `ctx.slice_idx[ts_p] != ctx.slice_idx[ts_q]` (note: utiliser l'adresse RS, pas TS — la slice index map est en raster scan)
- [ ] Gerer le flag per-slice : `slice_loop_filter_across_slices_enabled_flag` peut varier entre slices. La spec dit "for the slice that contains sample q0,0" — utiliser le flag du slice qui contient le cote Q de l'edge
- [ ] Pour ce faire, stocker `slice_loop_filter_across_slices_enabled_flag` per-CTU (ou per-slice-idx) en plus du slice index

**Spec precision** : S8.7.2.5.3 (eq 8-348, 8-350) — `slice_beta_offset_div2` et `slice_tc_offset_div2` sont ceux "for the slice that contains sample q0,0". Il faut utiliser les parametres du bon slice pour chaque edge. Avec multi-slice, les offsets deblocking peuvent varier entre slices.

**Pieges identifies :**
- L'adresse dans `slice_idx` est en raster scan (pas tile scan) — coherent avec `CtbAddrInRs`
- `slice_deblocking_filter_disabled_flag` est per-slice — un slice peut avoir le deblocking desactive alors que le voisin l'a active. La spec filtre uniquement "When `slice_deblocking_filter_disabled_flag` of the current slice is equal to 0" — le "current slice" est celui du CTU contenant le CU courant
- Ne pas confondre `slice_loop_filter_across_slices_enabled_flag` (controle les edges inter-slice) et `slice_deblocking_filter_disabled_flag` (controle le deblocking intra-slice)

**Critere de sortie** :
- [ ] `conf_b_xslice_256` pixel-perfect (B-frames, 4 slices, cross-slice deblocking + SAO)
- [ ] Non-regression : tests single-slice et `conf_i_multislice_256`

---

### 10.5 -- Cross-Slice SAO

**Spec ref** : S7.3.8.3 — SAO syntax :
```
leftCtbInSliceSeg = CtbAddrInRs > SliceAddrRs
upCtbInSliceSeg = ( CtbAddrInRs - PicWidthInCtbsY ) >= SliceAddrRs
```

**Spec ref** : S8.7.3 — SAO application :
> "When `slice_sao_luma_flag` of the current slice is equal to 1, the CTB modification process [...] is invoked"

**Contexte** : Le parsing SAO (`sao()` dans la boucle CTU) utilise `SliceAddrRs` pour determiner si le CTU voisin gauche/haut est dans le meme slice segment. Comme `decode_slice_segment_data` decode les CTU dans l'ordre de chaque slice, et que `SliceAddrRs` est derive correctement par slice, le **parsing** SAO est deja correct avec la boucle 10.2.

Le probleme est dans l'**application** SAO (`sao.cpp`) : les samples aux frontieres inter-slice ne doivent pas etre lus/ecrits cross-slice si `slice_loop_filter_across_slices_enabled_flag == 0`. L'implementation actuelle n'effectue aucun check cross-slice pour l'application SAO.

**Taches** :
- [ ] Verifier que le parsing SAO (merge left/up) fonctionne correctement avec la boucle multi-slice de 10.2 — `SliceAddrRs` est derive per-slice dans le header, les checks `leftCtbInSliceSeg` / `upCtbInSliceSeg` doivent utiliser le `SliceAddrRs` du slice courant (pas `slice_segment_address`)
- [ ] Dans `apply_sao()` (`sao.cpp`), pour le Edge Offset : quand un sample voisin est dans un CTU d'un slice different (via `slice_idx` map) et que `slice_loop_filter_across_slices_enabled_flag == 0` pour le slice courant, ne pas utiliser ce voisin pour la classification EO
- [ ] Stocker `slice_sao_luma_flag` et `slice_sao_chroma_flag` per-CTU (ou per-slice-idx) pour appliquer SAO uniquement sur les CTU dont le slice a le flag actif

**Pieges identifies :**
- `SliceAddrRs` pour un dependent slice n'est PAS `slice_segment_address` — c'est le `SliceAddrRs` du parent independant (voir 10.3). Si on parse avec un `SliceAddrRs` incorrect, le SAO merge sera faux
- SAO EO samples cross-CTU : quand deux CTU sont a une frontiere de slice, les samples EO a ±1 pixel du bord ne doivent pas traverser si le flag est 0. Cela concerne les 4 classes EO (H, V, diag135, diag45)
- SAO Band Offset n'est pas affecte (pas de dependance aux voisins)

**Critere de sortie** :
- [ ] `oracle_bbb1080_50f` pixel-perfect (1080p real-world, multi-slice)
- [ ] `oracle_bbb4k_25f` pixel-perfect (4K real-world, multi-slice)
- [ ] Non-regression : tous les tests existants passent (126/126)
- [ ] **Jalon** : 126/126 tests pixel-perfect — decodeur complet multi-slice

## Checklist spec par etape

Avant de coder chaque etape, lire EN ENTIER ces sections du PDF :

| Etape | Sections spec (lire dans le PDF, pas les resumes) |
|-------|---------------------------------------------------|
| **10.1** | S7.4.7.1 (derivation de `SliceAddrRs`, semantiques `slice_segment_address`) |
| **10.2** | S7.3.8.1 (slice_segment_data — boucle CTU complete), S8.1.3 (decoding process per coded picture — ordre des operations), S7.3.6.1 (slice segment header — tous les champs) |
| **10.3** | S7.4.7.1 (`dependent_slice_segment_flag` semantiques — heritage des champs), S9.3.2.1 (CABAC init — synchronisation contextes dependent slices) |
| **10.4** | S8.7.2.1 (deblocking — `filterLeftCbEdgeFlag`, `filterTopCbEdgeFlag`, conditions slice boundary), S8.7.2.5.3 (filtrage decision — `slice_beta_offset_div2`, `slice_tc_offset_div2` "for the slice that contains sample q0,0") |
| **10.5** | S7.3.8.3 (SAO syntax — `leftCtbInSliceSeg`, `upCtbInSliceSeg`), S8.7.3 (SAO application — `slice_sao_luma_flag` per-slice), S8.7.3.2 (CTB modification — samples aux frontieres) |

## Donnees de structure necessaires

Resume des structures a ajouter/modifier :

| Structure | Ajout | Granularite | Taille |
|-----------|-------|-------------|--------|
| `slice_idx_buf_` | `Decoder` | per-CTB | `PicSizeInCtbsY` bytes |
| `slice_idx` | `DecodingContext` | pointeur vers `slice_idx_buf_` | -- |
| `slice_flags_buf_` | `Decoder` | per-slice (indexe par `slice_idx`) | max 256 entries |
| `slice_flags[i].loop_filter_across_slices` | -- | per-slice | 1 bit |
| `slice_flags[i].sao_luma_flag` | -- | per-slice | 1 bit |
| `slice_flags[i].sao_chroma_flag` | -- | per-slice | 1 bit |
| `slice_flags[i].deblocking_disabled` | -- | per-slice | 1 bit |
| `slice_flags[i].beta_offset` | -- | per-slice | int8_t |
| `slice_flags[i].tc_offset` | -- | per-slice | int8_t |

Alternative : stocker ces flags per-CTU (plus simple, legere duplication memoire). Choix a faire en debut d'implementation.

## Estimation de complexite

Moderee. Les sous-phases 10.1 et 10.2 sont le coeur du travail — modifier la boucle de decodage et s'assurer que les grilles partagees (CU info, motion, intra modes) sont correctement remplies par tous les slices. Les sous-phases 10.4 et 10.5 sont des corrections chirurgicales dans les filtres avec la slice index map. 10.3 (dependent slices) est un copier-coller de champs avec un seul piege (`SliceAddrRs`).

Risque principal : les bitstreams de test utilisent `--slices 4 --wpp` (x265 impose WPP avec multi-slice). Le WPP P/B est partiellement bugge (7.6 dans BACKLOG). Si les bitstreams BBB sont encodes avec WPP + multi-slice, il faudra peut-etre fixer WPP P/B en parallele.
