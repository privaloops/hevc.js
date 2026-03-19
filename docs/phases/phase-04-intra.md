# Phase 4 — Intra Prediction (I-frames)

## Objectif
Décoder des frames I-only complètes : CABAC + quad-tree + intra prediction + transform + reconstruction.

## Prérequis
Phase 3 complétée (tous les parameter sets et slice headers parsés).

## Spec refs
- §9 : CABAC (tout le chapitre)
- §7.3.8-13 : Slice data syntax (CTU, CU, PU, TU, residual_coding)
- §8.5.4 : Intra prediction (35 modes)
- §8.6 : Transform inverse + scaling (dequant)

## Tâches

### 4.1 — CABAC Engine
- [ ] Arithmetic decoder : `decode_decision()`, `decode_bypass()`, `decode_terminate()`
- [ ] Renormalization
- [ ] Tables `rangeTabLps[64][4]`, `transIdxMps[64]`, `transIdxLps[64]` (recopier de la spec)
- [ ] Tests unitaires avec séquences de bins connues

### 4.2 — CABAC Context Initialization
- [ ] Fonction `init_context()` avec slope/offset
- [ ] Toutes les tables d'initValues (Tables 9-5 à 9-42)
- [ ] Contextes pour chaque syntax element ae(v)
- [ ] `cabac_init_flag` handling — permutation des tables d'initialisation :
  - Quand `cabac_init_flag == 1` dans un P-slice : utiliser les initValues de B-slice
  - Quand `cabac_init_flag == 1` dans un B-slice : utiliser les initValues de P-slice
  - Sans cette permutation, tous les contextes decodent avec les mauvaises probabilites → divergence CABAC totale
  - `cabac_init_present_flag` dans le PPS doit etre 1 pour que le slice puisse utiliser `cabac_init_flag`

### 4.3 — Binarization
- [ ] Fixed Length (FL)
- [ ] Truncated Unary (TU)
- [ ] Truncated Rice (TR)
- [ ] Exp-Golomb kth order (EGk)
- [ ] Binarization spécifique de chaque syntax element

### 4.4 — Slice Segment Data & Coding Tree (Quad-tree)

#### 4.4a — Boucle `slice_segment_data()` (§7.3.8.1)
La boucle principale qui itere sur les CTU d'un slice :

- [ ] Boucle `do { coding_tree_unit(); end_of_slice_segment_flag = decode_terminate(); } while (!end_of_slice_segment_flag)`
- [ ] `end_of_slice_segment_flag` est decode via `decode_terminate()` (CABAC terminate mode) apres **chaque** CTU
- [ ] `decode_terminate()` utilise un range fixe de 2 (different de `decode_decision`) — voir §9.3.4.5
- [ ] Quand le flag vaut 1, la boucle s'arrete et le slice est complet
- [ ] Gestion du `CtbAddrInTs` (scan order tiles) et `CtbAddrInRs` (raster scan)
- [ ] Tests : verifier que le nombre de CTU decodes correspond a la taille du slice

#### 4.4b — Coding Tree (Quad-tree)
- [ ] `coding_tree_unit()` : dispatch vers coding_quadtree
- [ ] `coding_quadtree()` : split récursif, gestion des bords d'image
- [ ] `split_cu_flag` avec contexte spatial (voisins)
- [ ] `coding_unit()` : pred_mode, part_mode (2Nx2N, NxN)
- [ ] `cu_transquant_bypass_flag` : si `transquant_bypass_enabled_flag` dans le PPS est actif, chaque CU peut bypasser la quantization et le transform. Quand actif :
  - Le residu est code/decode directement (pas de dequant ni transform inverse)
  - Le deblocking est desactive pour ce CU (comme PCM)
  - Utilise par le mode lossless (rare en Main profile mais present dans les bitstreams de conformite)

#### Note : Dependent Slice Segments

Les dependent slice segments (`dependent_slice_segment_flag = 1`) sont des continuations du slice precedent :

- [ ] L'etat CABAC n'est **PAS** reinitialise : les contextes sont herites du slice precedent
- [ ] Les parametres du slice (type, QP, refs, etc.) sont herites du slice independant precedent
- [ ] `slice_segment_address` determine le CTU de depart dans la picture
- [ ] Le coding tree continue exactement la ou le slice precedent s'est arrete
- [ ] Tests : bitstreams avec dependent slices (courant dans les streams tiles/WPP)

### 4.5 — Intra Mode Parsing
- [ ] `prev_intra_luma_pred_flag`
- [ ] `mpm_idx` (Most Probable Mode)
- [ ] `rem_intra_luma_pred_mode`
- [ ] MPM derivation (3 candidats depuis voisins)
- [ ] `intra_chroma_pred_mode` derivation

### 4.6 — Transform Tree
- [ ] `transform_tree()` : split récursif du TU
- [ ] `transform_unit()` : cbf_luma, cbf_cb, cbf_cr
- [ ] `residual_coding()` : le parsing CABAC le plus complexe
  - last_sig_coeff position
  - Sub-block scanning
  - sig_coeff_flag (context derivation §9.3.4.2.8 — le plus complexe, depend de coded_sub_block_flag voisins, position dans le sub-block, cIdx, log2TrafoSize)
  - coeff_abs_level_greater1/2 (context sets: 4 sets luma, 2 sets chroma, rotation par sub-block)
  - coeff_abs_level_remaining (Golomb-Rice avec `cRiceParam` adaptatif) :
    - `cRiceParam` commence a 0 pour chaque sub-block
    - Apres chaque coefficient, si `baseLevel > 3 * (1 << cRiceParam)` alors `cRiceParam = min(cRiceParam + 1, 4)`
    - Sans cette adaptation, les grands coefficients (ex: QP bas, details fins) sont mal decodes
  - coeff_sign_flag
  - Sign data hiding

### 4.7 — Dequantization (Scaling)
- [ ] QP derivation complete :
  - `SliceQpY = 26 + pps.init_qp_minus26 + slice_qp_delta`
  - CU QP delta : `QpY = ((QpY_prev + CuQpDeltaVal + 52 + 2*QpBdOffsetY) % (52 + QpBdOffsetY)) - QpBdOffsetY`
  - Le QP predicteur (`QpY_prev`) est la **moyenne** des QP des CU voisins (gauche et dessus), pas le QP du CU precedent dans l'ordre de scan
  - Le QP varie par "QP group" de taille `1 << Log2MinCuQpDeltaSize`, pas par CU individuel
  - `IsCuQpDeltaCoded` est remis a 0 au debut de chaque QP group (dans `coding_quadtree`)
  - `Qp'Y = QpY + QpBdOffsetY`
- [ ] QP chroma mapping (table non-lineaire, Table 8-10) :
  - `qPi_Cb = Clip3(-QpBdOffsetC, 57, QpY + pps_cb_qp_offset + slice_cb_qp_offset)`
  - `qPi_Cr = Clip3(-QpBdOffsetC, 57, QpY + pps_cr_qp_offset + slice_cr_qp_offset)`
  - `QpC = qp_chroma_table[qPi]` — non-lineaire pour qPi dans [30, 43]
  - Cb et Cr ont des QP independants
- [ ] Level scale table [40, 45, 51, 57, 64, 72]
- [ ] Scaling avec/sans scaling lists :
  - Si `scaling_list_enabled_flag == 0` : flat 16 pour toutes les tailles
  - Si actif : utiliser la matrice appropriee (intra/inter, taille, composante) avec le mecanisme de fallback (voir Phase 3.3)
  - Les matrices 8x8+ par defaut ne sont **PAS** flat 16

### 4.8 — Transform Inverse
- [ ] DST 4x4 inverse (luma intra seulement)
- [ ] DCT 4x4, 8x8, 16x16, 32x32 inverse
- [ ] Partial butterfly implementation
- [ ] **CRITIQUE — Clipping inter-passe** (§8.6.3) :
  - Le transform 2D est : passe verticale (colonnes) → clipping → passe horizontale (lignes)
  - Apres la passe verticale, chaque valeur intermediaire **DOIT** etre clippee a `[-32768, 32767]`
  - Sans ce clipping, des overflows dans la passe horizontale produisent des residus faux
  - C'est la cause #1 de mismatch bit-exact sur les transforms
  - Shifts : `shift1 = 7` (apres V), `shift2 = 20 - BitDepth` (apres H)
- [ ] Transform skip — quand `transform_skip_flag == 1` :
  - Pas de transform inverse (ni DCT ni DST)
  - Le residu apres dequant est directement le residu spatial, mais avec un shift specifique
  - Shift total : `(15 - BitDepth)` — appliquer `(coeffs + (1 << (shift-1))) >> shift`
  - Le scan order change : utiliser le scan **horizontal** au lieu du scan diagonal
  - Tests : bitstreams avec transform skip (courant en lossless ou near-lossless)
- [ ] Shift/rounding corrects (bit-exact)

### 4.9 — Intra Prediction
- [ ] Reference sample availability check
- [ ] Reference sample substitution (propagation)
- [ ] Reference sample filtering ([1,2,1])
- [ ] Strong intra smoothing (32x32)
- [ ] Planar mode (mode 0) :
  - Interpolation bilineaire utilisant les samples de reference haut et gauche
  - **Piege d'indexation** : les indices dans la formule spec utilisent `nTbS` (taille du bloc), pas `nTbS-1`. Verifier que `bottomLeft = ref[2*nTbS]` et `topRight = ref[2*nTbS+1]` (dans le vecteur de reference etendu)
  - Formule : `pred[y][x] = ((nTbS-1-x)*refLeft[y+1] + (x+1)*topRight + (nTbS-1-y)*refTop[x+1] + (y+1)*bottomLeft + nTbS) >> (log2(nTbS) + 1)`
- [ ] DC mode (mode 1) + DC filtering
- [ ] Angular modes (modes 2-34) — 33 modes angulaires :
  - Table `intraPredAngle[]` avec 33 valeurs (modes 2-34)
  - Table `invAngle[]` pour les angles negatifs (modes 2-10 et 11-17 selon l'axe)
  - Vecteur de reference etendu pour les angles negatifs : projeter les samples du cote oppose
  - Interpolation lineaire entre deux samples de reference (precision 1/32 de pixel)
  - **CRITIQUE — Transposition** pour les modes horizontaux (2-17) :
    1. Echanger les axes x et y dans les samples de reference
    2. Utiliser le mode "miroir" (`mode' = 34 - mode + 2`) qui a le meme angle mais en vertical
    3. Appliquer la prediction angulaire verticale standard
    4. Transposer le resultat (echanger lignes et colonnes)
    - Sans cette transposition, les modes 2-17 produisent une prediction incorrecte
- [ ] Post-filtering pour les modes purement horizontal (mode 10) et vertical (mode 26)
- [ ] `constrained_intra_pred_flag` : si actif, les samples de reference intra ne peuvent venir QUE de blocs intra (les voisins inter sont marques "non disponibles")

### 4.10 — Reconstruction
- [ ] `recSamples = Clip(predSamples + resSamples)`
- [ ] Clipping à [0, (1 << BitDepth) - 1]
- [ ] Stockage dans le Picture buffer

### 4.10b — PCM Mode (§7.3.10.2)
Le mode PCM est requis par le profil Main. Les samples sont codes directement sans prediction ni transform.

- [ ] **Byte alignment avant lecture** : le bitstream reader doit etre aligne sur une frontiere d'octet avant de lire les samples PCM. Appeler `byte_alignment()` (lire et ignorer les bits jusqu'au prochain octet). Sans cela, les samples sont lus decales et la reconstruction est fausse.
- [ ] Parsing de `pcm_sample()` : lecture directe des samples (`pcm_sample_bit_depth_luma_minus1 + 1` bits par sample luma, `pcm_sample_bit_depth_chroma_minus1 + 1` pour chroma — ces valeurs viennent du SPS, pas de BitDepthY/C)
- [ ] Stockage direct dans le buffer de reconstruction (bypass du pipeline intra/transform)
- [ ] Reset des contextes CABAC apres un bloc PCM (§9.3.1.2 — appeler `init_context()` pour tous les contextes)
- [ ] `pcm_loop_filter_disabled_flag` : si actif, le deblocking est desactive pour les edges des blocs PCM
- [ ] Tests : bitstream avec des blocs PCM (generer avec x265 `--pcm` ou trouver dans les bitstreams de conformite)

**Attention** : Le PCM est rare mais les bitstreams de conformite en contiennent. Un decodeur qui crash sur PCM echoue la conformite.

### 4.11 — SAO Parsing (stub)
- [ ] Parser les paramètres SAO par CTU (stocker mais ne pas appliquer)
- [ ] Sera implémenté en Phase 6

## Critère de sortie

- Décoder correctement des I-frames simples (QP fixe, petit, pas de filtres)
- Puis des I-frames complexes (QP variable, toutes tailles, tous modes)
- Pixel-perfect vs libde265 sur des bitstreams I-only (sans deblocking ni SAO)

## Validation oracle

### Étape 1 : I-frame minimale
```bash
# Encoder un I-frame simple avec x265
x265 --input raw.yuv --input-res 64x64 --fps 1 --frames 1 \
     --preset ultrafast --qp 30 --no-deblock --no-sao \
     --keyint 1 -o test_intra.265

# Comparer
dec265 test_intra.265 -o ref.yuv
./hevc-torture test_intra.265 -o test.yuv
python3 tools/oracle_compare.py ref.yuv test.yuv 64 64
```

### Étape 2 : I-only séquence
```bash
# Toutes les tailles de bloc, tous les modes
x265 --input raw.yuv --input-res 1920x1080 --fps 30 --frames 10 \
     --preset slow --qp 22 --no-deblock --no-sao \
     --keyint 1 -o test_intra_full.265
```

## Estimation de complexité
Très élevée. C'est la phase la plus complexe du projet. CABAC seul est un sous-projet majeur.

## Sous-phases recommandées

1. CABAC engine + tests (1 semaine)
2. Coding tree + intra mode parsing (1 semaine)
3. residual_coding (1 semaine)
4. Transform + dequant (3-4 jours)
5. Intra prediction (35 modes) (1 semaine)
6. Intégration + debug oracle (1-2 semaines)
