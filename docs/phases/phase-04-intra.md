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
- [ ] cabac_init_flag handling

### 4.3 — Binarization
- [ ] Fixed Length (FL)
- [ ] Truncated Unary (TU)
- [ ] Truncated Rice (TR)
- [ ] Exp-Golomb kth order (EGk)
- [ ] Binarization spécifique de chaque syntax element

### 4.4 — Coding Tree (Quad-tree)
- [ ] `coding_tree_unit()` : dispatch vers coding_quadtree
- [ ] `coding_quadtree()` : split récursif, gestion des bords d'image
- [ ] `split_cu_flag` avec contexte spatial (voisins)
- [ ] `coding_unit()` : pred_mode, part_mode (2Nx2N, NxN)

### Note : Dependent Slice Segments

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
  - coeff_abs_level_remaining (Golomb-Rice)
  - coeff_sign_flag
  - Sign data hiding

### 4.7 — Dequantization (Scaling)
- [ ] QP derivation (slice QP + CU QP delta)
- [ ] QP chroma mapping (table non-linéaire)
- [ ] Level scale table [40, 45, 51, 57, 64, 72]
- [ ] Scaling avec/sans scaling lists
- [ ] Flat scaling (matrice 16 par défaut)

### 4.8 — Transform Inverse
- [ ] DST 4x4 inverse (luma intra seulement)
- [ ] DCT 4x4, 8x8, 16x16, 32x32 inverse
- [ ] Partial butterfly implementation
- [ ] Transform skip
- [ ] Shift/rounding corrects (bit-exact)

### 4.9 — Intra Prediction
- [ ] Reference sample availability check
- [ ] Reference sample substitution (propagation)
- [ ] Reference sample filtering ([1,2,1])
- [ ] Strong intra smoothing (32x32)
- [ ] Planar mode (mode 0)
- [ ] DC mode (mode 1) + DC filtering
- [ ] Angular modes (modes 2-34)
- [ ] Post-filtering (modes 10, 26)
- [ ] `constrained_intra_pred_flag` : si actif, les samples de reference intra ne peuvent venir QUE de blocs intra (les voisins inter sont marques "non disponibles")

### 4.10 — Reconstruction
- [ ] `recSamples = Clip(predSamples + resSamples)`
- [ ] Clipping à [0, (1 << BitDepth) - 1]
- [ ] Stockage dans le Picture buffer

### 4.10b — PCM Mode (§7.3.10.2)
Le mode PCM est requis par le profil Main. Les samples sont codes directement sans prediction ni transform.

- [ ] Parsing de `pcm_sample()` : lecture directe des samples (BitDepthY bits par sample luma, BitDepthC pour chroma)
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
