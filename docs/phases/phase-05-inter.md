# Phase 5 — Inter Prediction (P/B frames)

## Objectif
Décoder les frames P et B : motion compensation, interpolation, bi-prediction.

## Prérequis
Phase 4 complétée (I-frames décodées correctement).

## Spec refs
- §8.3 : Reference picture management (DPB, RPS, ref lists)
- §8.5.3 : Inter prediction (motion compensation)
- §7.3.11 : prediction_unit syntax (merge, AMVP)

## Tâches

### 5.1 — Reference Picture Management
- [ ] POC derivation (§8.3.1) avec wrap-around
- [ ] Reference Picture Set derivation (§8.3.2)
- [ ] DPB (Decoded Picture Buffer) management
  - Add, bump, remove
  - Sizing selon le level
- [ ] Tests : vérifier l'ordre POC sur des séquences avec B-frames

### 5.2 — Reference Picture List Construction
- [ ] RefPicList0 construction (§8.3.3)
- [ ] RefPicList1 construction
- [ ] Cyclic repetition
- [ ] ref_pic_list_modification()
- [ ] Tests : vérifier les listes pour différentes structures de GOP

### 5.3 — Merge Mode
- [ ] 5 candidats spatiaux (A1, B1, B0, A0, B2)
- [ ] Candidat temporel (co-located block)
- [ ] Zero MV padding
- [ ] Combined bi-predictive candidates
- [ ] Merge index parsing
- [ ] Tests : vérifier les candidats merge vs libde265 (si dump disponible)

### 5.4 — AMVP Mode
- [ ] 2 candidats spatiaux
- [ ] Candidat temporel
- [ ] Zero MV padding
- [ ] MVP index parsing
- [ ] MVD parsing + application
- [ ] Tests : vérifier le MV final

### 5.5 — Luma Interpolation
- [ ] Filtre 8-tap (Table 8-1)
- [ ] Interpolation horizontale (xFrac != 0, yFrac == 0)
- [ ] Interpolation verticale (xFrac == 0, yFrac != 0)
- [ ] Interpolation 2D (xFrac != 0, yFrac != 0) — H puis V
- [ ] Buffer temporaire pour le filtre 2D (précision étendue)
- [ ] MV clipping aux bords de la frame
- [ ] Tests : vérifier des positions fractionnaires spécifiques

### 5.6 — Chroma Interpolation
- [ ] Filtre 4-tap (Table 8-2)
- [ ] Dérivation du MV chroma depuis le MV luma
- [ ] Position fractionnaire chroma (1/8 pel)
- [ ] H, V, HV comme pour luma

### 5.7 — Bi-prediction
- [ ] Averaging des prédictions L0 et L1
- [ ] Shift et rounding corrects
- [ ] Weighted prediction (explicit, Table E.1)

### 5.8 — Inter Partition Modes
- [ ] 2Nx2N (1 PU)
- [ ] 2NxN, Nx2N (2 PU)
- [ ] 2NxnU, 2NxnD, nLx2N, nRx2N (AMP — 2 PU asymétriques)
- [ ] CU skip mode

### 5.9 — CU Skip Mode
- [ ] Pas de résidu (rqt_root_cbf implicitement 0)
- [ ] Merge mode uniquement
- [ ] Tests spécifiques

## Critère de sortie

- Décoder des séquences avec P-frames (IPPP...)
- Décoder des séquences avec B-frames (IBP, hiérarchique)
- Pixel-perfect vs libde265 (sans deblocking/SAO)

## Validation oracle

### Étape 1 : P-frames only
```bash
x265 --input raw.yuv --input-res 1920x1080 --fps 30 --frames 30 \
     --preset medium --qp 22 --no-deblock --no-sao \
     --bframes 0 -o test_p.265
```

### Étape 2 : B-frames
```bash
x265 --input raw.yuv --input-res 1920x1080 --fps 30 --frames 30 \
     --preset medium --qp 22 --no-deblock --no-sao \
     --bframes 4 -o test_b.265
```

## Estimation de complexité
Élevée. La dérivation des MV (merge, AMVP) est complexe. L'interpolation est volumineuse mais bien définie.
