# MASTER-PLAN — Décodeur HEVC/H.265

## Vision

Implémenter un décodeur HEVC conforme à la spec ITU-T H.265 (v8, 2021) en C++17, compilé en WebAssembly pour intégration dans un player web. Le décodeur sera validé frame-par-frame contre ffmpeg comme oracle de référence.

## Profils cibles

| Priorité | Profil | Bit depth | Chroma | Résolution max | Phase |
|----------|--------|-----------|--------|----------------|-------|
| 1 | Main | 8-bit | 4:2:0 | 4K | 4-6 |
| 2 | Main 10 | 10-bit | 4:2:0 | 4K | 7 |
| 3 | Main 4:2:2 10 | 10-bit | 4:2:2 | 4K | 7 |
| 4 | Main 4:4:4 | 8/10-bit | 4:4:4 | 4K | 7 |

Niveaux : jusqu'à 5.1 (4K@60fps, 120Mbps).

## Phases

### Phase 1 — Infrastructure (`docs/phases/phase-01-infra.md`)

**Objectif** : Setup du projet, toolchain, CI, oracle.

- CMake + Emscripten toolchain
- Google Test setup
- Script oracle : decode avec libde265, decode avec hevc-torture, compare YUV pixel-perfect
- CI GitHub Actions (build natif + WASM)
- Structures de données fondamentales (bitstream reader, types)

**Critère de sortie** : Le projet compile en natif et WASM, les tests passent, l'oracle fonctionne (même s'il n'y a rien à comparer encore).

### Phase 2 — Bitstream & NAL (`docs/phases/phase-02-bitstream.md`)

**Objectif** : Lire un flux HEVC brut, isoler les NAL units.

Spec refs : §7.3.1 (NAL unit syntax), §7.4.2 (NAL unit semantics), Annexe B (byte stream format).

- Byte stream format (start code detection, emulation prevention)
- NAL unit header parsing (nal_unit_type, nuh_layer_id, nuh_temporal_id_plus1)
- RBSP extraction (emulation prevention byte removal)
- Exp-Golomb coding (ue(v), se(v))
- Bitstream reader (read_bits, read_u, read_ue, read_se, read_flag, byte_aligned)
- Access Unit boundary detection (§7.4.2.4.4) — frame delimitation
- `more_rbsp_data()` implementation (§7.2) — stop bit detection

**Critère de sortie** : Parser un fichier .265 et lister correctement tous les NAL units avec leur type. Délimiter les Access Units (frames). Validé contre libde265 --dump-headers.

### Phase 3 — Parameter Sets & Slice Header (`docs/phases/phase-03-parsing.md`)

**Objectif** : Parser VPS, SPS, PPS, slice headers complets.

Spec refs : §7.3.2 (VPS), §7.3.3 (SPS), §7.3.4 (PPS), §7.3.6 (slice header), §7.4 (semantics correspondantes).

- VPS parsing (video_parameter_set_rbsp)
- SPS parsing (seq_parameter_set_rbsp) — profiles, levels, dimensions, CTU size, transforms, etc.
- PPS parsing (pic_parameter_set_rbsp) — tiles, entropy coding sync, deblocking control, etc.
- Slice header parsing (slice_segment_header) — slice type, POC, reference picture lists, pred weight table, etc.
- Gestion des IDs et activation des parameter sets

**Critère de sortie** : Tous les champs des parameter sets parsés correctement. Comparaison champ-par-champ avec libde265 --dump-headers sur une suite de bitstreams variés.

### Phase 4 — Intra Prediction (`docs/phases/phase-04-intra.md`)

**Objectif** : Décoder des frames I-only (intra prediction complète).

Spec refs : §7.3.8 (coding_tree_unit), §7.3.9 (coding_unit), §7.3.10 (prediction_unit), §7.3.11 (transform_unit), §8.4 (quantization), §8.5.4 (intra prediction), §8.6 (transform + scaling), §9 (CABAC).

Subdivisée en **6 sous-phases** avec validation indépendante à chaque niveau :

| Sous-phase | Doc | Validation |
|------------|-----|------------|
| **4A** — CABAC Engine + Contexts | `phase-04a-cabac.md` | Tests unitaires (7 tests) |
| **4B** — Coding Tree Structure | `phase-04b-coding-tree.md` | Trace syntax elements vs HM |
| **4C** — Residual Context Derivation | `phase-04c-residual-contexts.md` | Tests unitaires derive_*_ctx vs HM |
| **4D** — Coefficient Parsing | `phase-04d-coefficient-parsing.md` | Trace coefficients TU vs HM |
| **4E** — Transform + Dequant | `phase-04e-transform-dequant.md` | Tests unitaires vecteurs connus |
| **4F** — Prediction + Reconstruction | `phase-04f-prediction-recon.md` | Oracle pixel-perfect |

Principe : valider chaque couche en isolation AVANT l'intégration. Un bug de contexte CABAC corrompt l'état arithmétique et fait diverger tous les bins suivants — debugger au niveau oracle est alors très lent.

**Critère de sortie** : `oracle_i_64x64_qp22` passe (MD5 match). Tous les tests unitaires des sous-phases passent.

### Phase 5 — Inter Prediction (`docs/phases/phase-05-inter.md`)

**Objectif** : Décoder les frames P et B (motion compensation complète).

Spec refs : §8.3 (reference picture management), §8.5.3 (inter prediction), §8.5.3.2 (luma interpolation), §8.5.3.3 (chroma interpolation), §8.5.3.4 (weighted prediction).

Subdivisée en **4 sous-phases** avec validation indépendante :

| Sous-phase | Doc | Validation |
|------------|-----|------------|
| **5A** — DPB + Ref Lists | `phase-05a-dpb.md` | POC et ref lists vs HM |
| **5B** — MV Derivation | `phase-05b-mv-derivation.md` | MV par PU vs HM |
| **5C** — Interpolation | `phase-05c-interpolation.md` | Tests unitaires filtres 8/4-tap |
| **5D** — Integration | `phase-05d-integration.md` | Oracle pixel-perfect P+B |

5A et 5C sont indépendants (parallélisables). 5B dépend de 5A. 5D dépend de tout.

**Critère de sortie** : `oracle_p_qcif_10f` et `oracle_b_qcif_10f` pixel-perfect.

### Phase 6 — Loop Filters (`docs/phases/phase-06-loop-filters.md`)

**Objectif** : Implémenter deblocking filter et SAO. **Jalon majeur : Main profile complet.**

Spec refs : §8.7.2 (deblocking), §8.7.3 (SAO).

Subdivisée en **4 sous-phases** séquentielles (SAO dépend du deblocking) :

| Sous-phase | Validation |
|------------|------------|
| **6A** — Deblocking Luma | Oracle I-frame + deblock only |
| **6B** — Deblocking Chroma | Idem, plans Cb/Cr |
| **6C** — SAO | Oracle I-frame + SAO only |
| **6D** — Integration Full | Oracle `full_qcif_10f` = Main profile complet |

Plus simple que Phases 4/5 : traitement d'image pur, pas de CABAC, pas de propagation d'erreur.

**Critère de sortie** : `oracle_full_qcif_10f` pixel-perfect. Bitstreams real-world (Big Buck Bunny) pixel-perfect.

### Phase 7 — Profils supérieurs (`docs/phases/phase-07-high-profiles.md`)

**Objectif** : Étendre au-delà de Main profile pour supporter 4K60.

Sous-étapes :
1. **Main 10** : Étendre les pipelines de 8-bit à 10-bit, adapter les buffers
2. **Main 4:2:2 10** : Support chroma 4:2:2, adapter interpolation chroma
3. **Main 4:4:4** : Support 4:4:4, adapter toutes les opérations chroma
4. **Niveaux élevés** : Level 5.0 (4K@30), Level 5.1 (4K@60) — DPB sizing, bitrate limits
5. **Tiles & WPP** : Support multi-tile, wavefront parallel processing (prep pour WASM threads)

**Critère de sortie** : Pixel-perfect sur bitstreams de conformité pour chaque profil ajouté.

### Phase 8 — Intégration WASM (`docs/phases/phase-08-wasm.md`)

**Objectif** : Faire tourner le décodeur dans un Web Worker avec interface JS propre.

Sous-étapes :
1. **API C** : Interface C pour l'embedding (init, feed_data, get_frame, get_info)
2. **Bindings Emscripten** : Embind ou cwrap pour exposer l'API en JS
3. **Web Worker** : Décodage dans un worker, transfert des frames via SharedArrayBuffer/transferable
4. **Frame output** : YUV → RGB conversion (ou output YUV brut vers WebGL)
5. **Memory management** : Pool de buffers, gestion de la mémoire WASM (heap growth, limits)
6. **Streaming** : Support du feeding incrémental de données (pas besoin d'avoir tout le fichier)

**Critère de sortie** : Démo HTML qui lit un fichier .265 et affiche les frames décodées dans un canvas. Fonctionnel sur Chrome/Firefox.

### Phase 9 — Performance (`docs/phases/phase-09-perf.md`)

**Objectif** : Atteindre des perfs suffisantes pour la lecture temps réel (4K@60).

Sous-étapes :
1. **Profiling** : Identifier les hotspots (transform, interpolation, CABAC, filters)
2. **SIMD WASM** : Utiliser les intrinsics SIMD 128-bit de WASM pour les kernels critiques
3. **Threading** : Web Workers + SharedArrayBuffer pour le parallélisme (tiles, WPP, pipeline)
4. **Optimisations algorithmiques** : Lookup tables, fast paths, cache-friendly layouts
5. **Memory** : Réduire les allocations, pool allocators, zero-copy où possible
6. **Benchmarks** : Suite de benchmarks reproductibles, tracking des régressions

**Critère de sortie** : Décoder du 1080p@30 en temps réel dans un navigateur. 4K@60 comme objectif stretch.

## Dépendances entre phases

```
Phase 1 (Infra) ──→ Phase 2 (Bitstream) ──→ Phase 3 (Parsing)
                                                    │
                                              ┌─────┴─────┐
                                              ▼           │
                                        Phase 4 (Intra)   │
                                              │           │
                                              ▼           │
                                        Phase 5 (Inter) ◄─┘
                                              │
                                              ▼
                                        Phase 6 (Filters)
                                              │
                                    ┌─────────┼──────────┐
                                    ▼         ▼          ▼
                              Phase 7    Phase 8    Phase 9
                           (Profiles)   (WASM)    (Perf)
```

Phases 7, 8, 9 sont partiellement parallélisables.

## Stratégie de validation

Chaque phase est validée par :
1. **Tests unitaires** : Chaque fonction/module testé isolément
2. **Tests d'intégration** : Bitstreams synthétiques simples
3. **Oracle libde265** : Comparaison pixel-perfect frame-by-frame sur bitstreams de conformité
4. **Bitstreams de conformité HEVC** : Suite officielle ITU-T (disponible sur le site HEVC)

Voir `docs/oracle/oracle-strategy.md` pour les détails.

## Conventions de code

- Nommage des fonctions miroir de la spec : `decode_xxx` correspond à la section spec
- Chaque fichier source référence les sections spec qu'il implémente
- Pseudo-code de la spec traduit ligne par ligne en C++, puis optimisé
- Un commit = une sous-section spec implémentée (traçabilité)

## Pièges de conformité identifiés

Points critiques pour la conformité stricte à la spec, souvent source de mismatch :

| Piège | Section spec | Phase | Impact |
|-------|-------------|-------|--------|
| PCM mode absent | §7.3.10.2 | 4 | Crash sur certains bitstreams |
| `more_rbsp_data()` incorrect | §7.2 | 2 | Parsing incorrect des parameter sets |
| `NoRaslOutputFlag` non dérivé | §8.1 | 5 | POC incorrect aux points d'accès |
| MV scaling manquant | §8.5.3.2.12 | 5 | MV temporels incorrects |
| Bs bi-prediction simplifié | §8.7.2.4.5 | 6 | Deblocking incorrect en B-frames |
| `sig_coeff_flag` context erroné | §9.3.4.2.8 | 4 | Divergence CABAC totale |
| `cRiceParam` non adaptatif | §9.3.3.11 | 4 | Grands coefficients mal décodés |
| Clipping inter-passe transform | §8.6.3 | 4 | Résidu incorrect (bit-inexact) |
| Planar indexation incorrecte | §8.4.4.2.4 | 4 | Intra prediction fausse |
| Transposition angular absente | §8.4.4.2.6 | 4 | Modes 2-17 incorrects |
| TMVP incomplet | §8.5.3.2.9 | 5 | Candidat temporel absent/faux |
| Dependent slice CABAC | §9.2 | 4 | Divergence CABAC sur slices dépendants |
| `constrained_intra_pred_flag` | §8.4.4.2.2 | 4 | Mauvais samples de référence intra |
| Shifts interpolation inter | §8.5.3.3.3 | 5 | Mismatch pixel sur toutes les frames inter |
| QP delta derivation incomplet | §8.6.1 | 4 | QP incorrect par CU, résidu faux |
| Transform skip shift incorrect | §8.6.4.2 | 4 | Résidu transform-skip bit-inexact |
| `end_of_slice_segment_flag` absent | §7.3.8.1 | 4 | Décodeur ne sait pas quand arrêter un slice |
| Scaling list fallback non-flat | §7.4.5 | 4 | Dequant incorrect si scaling lists actives |
| `HandleCraAsBlaFlag` non géré | §8.1 | 5 | Crash/artifacts en random access |
| Long-term ref pics incomplet | §8.3.2 | 5 | Échec sur bitstreams avec LT refs |
| `cabac_init_flag` permutation | §9.2.1.1 | 4 | CABAC init tables inversées P/B |
| AU boundary suffix SEI | §7.4.2.4.4 | 2 | Découpage incorrect des frames |
| PCM byte alignment | §7.3.10.2 | 4 | Lecture décalée des samples PCM |

## Goulots de performance identifiés

Points à traiter au moment de la phase indiquée, pas avant :

| Point | Phase | Description | Action |
|-------|-------|-------------|--------|
| Picture stride non aligné | 7-9 | `stride = width` empêche le SIMD 128-bit (WASM) qui requiert un alignement 16 ou 32 samples. | Phase 7 ou 9 : arrondir le stride à un multiple de 32 dans `Picture::allocate()`. Ne pas changer avant — les tests oracle comparent du YUV brut, un stride padded changerait l'output. |
| Stack WASM trop petite | 8 | La stack WASM par défaut Emscripten est ~64KB (pas 1MB). Avec les buffers 64x64 sur la stack (AD-006) et la récursion quad-tree CTU→CU→TU, risque de stack overflow. | Phase 8 : ajouter `-sSTACK_SIZE=1048576` aux flags Emscripten, ou passer les buffers temporaires au niveau CTU et propager des pointeurs. |
| `extract_rbsp()` copie tout | 9 | Chaque NAL unit est copiée pour retirer les emulation prevention bytes. Sur de gros slice data NALs (4K), c'est une allocation + copie de centaines de KB. | Phase 9 : considérer un BitstreamReader qui skip les 0x03 à la volée sans copie préalable. |
