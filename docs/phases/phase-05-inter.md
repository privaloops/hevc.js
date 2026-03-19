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
- [ ] POC derivation (§8.3.1) avec wrap-around :
  - `PicOrderCntMsb` : wrap-around quand `|poc_lsb - prevPocLsb| >= MaxPicOrderCntLsb / 2`
  - IDR/BLA : `PicOrderCntMsb = 0` (reset)
- [ ] `NoRaslOutputFlag` derivation (§8.1) :
  - Vaut 1 pour la premiere picture IRAP apres le debut du bitstream
  - Vaut 1 pour chaque IDR, BLA_W_LP, BLA_W_RADL, BLA_N_LP
  - Vaut 1 pour un CRA si `HandleCraAsBlaFlag == 1`
  - Quand `NoRaslOutputFlag == 1`, les RASL pictures associees ne sont PAS output
  - **Sans ce flag, les RASL pictures referencent des pictures absentes du DPB → crash ou artifacts**
- [ ] `HandleCraAsBlaFlag` (§8.1) :
  - Flag externe au bitstream, positionne par l'application (ex: lors d'un seek/random access)
  - Quand on commence le decodage a un CRA (pas depuis le debut), les RASL pictures referencent des pictures non decodees
  - `HandleCraAsBlaFlag = 1` force le CRA a etre traite comme un BLA → les RASL sont marquees non-output et ne crashent pas
  - Tests : simuler un random access a un CRA avec RASL pictures
- [ ] Reference Picture Set derivation (§8.3.2)
- [ ] Long-term reference picture handling :
  - Parsing de `num_long_term_sps`, `num_long_term_pics` dans le slice header
  - `delta_poc_msb_present_flag[i]` et `delta_poc_msb_cycle_lt[i]` pour resoudre l'ambiguite quand le POC LSB wrappe (deux pictures differentes avec le meme LSB)
  - Derivation de `PocLtCurr[]` et `PocLtFoll[]`
  - Matching dans le DPB par **POC LSB** (pas POC complet) sauf si `delta_poc_msb_present_flag == 1`
  - Marquage "used for long-term reference" dans le DPB — un LT ref ne peut PAS etre supprime par le mecanisme short-term
  - Tests : bitstreams de conformite avec long-term ref pics
- [ ] DPB (Decoded Picture Buffer) management :
  - Add, bump, remove
  - Sizing selon le level (MaxDpbSize derive de MaxLumaPs et pic dimensions)
  - Bumping process (§C.5.2.4) :
    - Se declenche quand le DPB est plein ET une nouvelle picture doit y entrer
    - Output la picture avec le plus petit POC parmi celles marquees "needed for output"
    - Une picture peut etre simultanement "needed for output" ET "used for reference"
    - Supprimee du DPB seulement quand elle n'est plus ni l'un ni l'autre
    - L'ordre de bumping est l'ordre POC, pas l'ordre de decodage
- [ ] Tests : vérifier l'ordre POC sur des séquences avec B-frames

### 5.2 — Reference Picture List Construction
- [ ] RefPicList0 construction (§8.3.3)
- [ ] RefPicList1 construction
- [ ] Cyclic repetition
- [ ] ref_pic_list_modification()
- [ ] Tests : vérifier les listes pour différentes structures de GOP

### 5.3 — Merge Mode
- [ ] 5 candidats spatiaux dans l'ordre spec (§8.5.3.2.2, Figure 8-5) :
  - **A1** (left) : voisin gauche, echantillonne a `(xPb-1, yPb+nPbH-1)` — bas du cote gauche du PU
  - **B1** (above) : voisin dessus, echantillonne a `(xPb+nPbW-1, yPb-1)` — droite du cote superieur du PU
  - **B0** (above-right) : `(xPb+nPbW, yPb-1)` — coin superieur droit du PU
  - **A0** (below-left) : `(xPb-1, yPb+nPbH)` — coin inferieur gauche du PU
  - **B2** (above-left) : `(xPb-1, yPb-1)` — coin superieur gauche du PU, seulement si < 4 candidats disponibles
- [ ] Candidat temporel TMVP (§8.5.3.2.9) :
  - Selection de la picture co-localisee (`collocated_from_l0_flag`, `collocated_ref_idx`)
  - Selection du bloc : bottom-right du PU (+1,+1) en priorite, puis centre du PU
  - Verification de disponibilite (pas hors-image, pas intra, dans le meme slice/tile)
  - MV scaling selon la distance POC (§8.5.3.2.12)
  - `slice_temporal_mvp_enabled_flag` doit etre actif
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

### 5.4b — MV Scaling (§8.5.3.2.12)
- [ ] Scaling des MVs quand les distances POC source/cible different
- [ ] Formule : `distScaleFactor = Clip3(-4096, 4095, (tb * tx + 32) >> 6)` avec `tx = (16384 + (abs(td) >> 1)) / td`
- [ ] Utilise par le candidat temporel merge et AMVP
- [ ] Tests : sequences avec B-frames hierarchiques (distances POC asymetriques)

### 5.5 — Luma Interpolation
- [ ] Filtre 8-tap (Table 8-1)
- [ ] Interpolation horizontale (xFrac != 0, yFrac == 0)
- [ ] Interpolation verticale (xFrac == 0, yFrac != 0)
- [ ] Interpolation 2D (xFrac != 0, yFrac != 0) — H puis V
- [ ] **Shifts et precision intermediaire** (§8.5.3.3.3 — source #1 de mismatch apres CABAC) :
  - **H-only** : `shift = BitDepth - 8`, `offset = shift > 0 ? (1 << (shift-1)) : 0`, resultat clippe a `[0, (1<<BitDepth)-1]`
  - **V-only** : `shift = 14 - BitDepth`, `offset = 1 << (shift-1)`, resultat clippe a `[0, (1<<BitDepth)-1]`
  - **2D — passe H** : `shift = BitDepth - 8`, `offset = 0` (si BitDepth == 8, pas de shift). **PAS de clipping** apres cette passe
  - **2D — passe V** : `shift = 6`, `offset = 1 << 5 = 32`, resultat clippe a `[0, (1<<BitDepth)-1]`
  - **Uni-pred** : resultat direct apres interpolation
  - **Bi-pred** : les deux predictions sont en precision etendue (pas de clip intermediaire), puis `(predL0 + predL1 + offset) >> shift` avec `shift = 15 - BitDepth` (et l'interpolation ne clippe pas le resultat)
  - Buffer intermediaire : `int16_t` pour 8-bit, `int32_t` pour 10-bit+ (les valeurs apres passe H 2D sont sur ~15 bits pour 8-bit, ~17 bits pour 10-bit)
- [ ] MV clipping aux bords de la frame
- [ ] Tests : verifier des positions fractionnaires specifiques, en 1D et 2D, uni et bi-pred

### 5.6 — Chroma Interpolation
- [ ] Filtre 4-tap (Table 8-2)
- [ ] Dérivation du MV chroma depuis le MV luma
- [ ] Position fractionnaire chroma (1/8 pel)
- [ ] H, V, HV comme pour luma

### 5.7 — Bi-prediction
- [ ] Averaging des prédictions L0 et L1 :
  - Sans weighted pred : `pred = (predL0 + predL1 + 1) >> 1` (apres shift depuis precision etendue)
  - Les predictions L0 et L1 sont conservees en precision etendue (pas clippees individuellement)
- [ ] Shift et rounding corrects (voir shifts detailles en 5.5)
- [ ] Weighted prediction (explicit) :
  - Parsing de `pred_weight_table()` dans le slice header (voir Phase 3.5)
  - Uni-pred : `((pred * weight + (1 << (log2Wd-1))) >> log2Wd) + offset`
  - Bi-pred : `((predL0 * w0 + predL1 * w1 + ((o0+o1+1) << log2Wd)) >> (log2Wd+1))`
  - Default weights quand `luma_weight_l0_flag[i] == 0` : weight = `1 << luma_log2_weight_denom`, offset = 0

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
