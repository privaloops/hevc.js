# 5.4 -- TMVP (Temporal Motion Vector Predictor)

## Objectif

Verifier que le candidat temporel est correctement derive depuis la picture co-localisee.

## Spec refs

- S8.5.3.2.7 : Derivation of temporal luma MV predictor (collocated MV)
- S8.5.3.2.12 : Derivation of collocated MVs
- S8.3.5 : Collocated picture derivation

## Code existant

`src/decoding/inter_prediction.cpp` : partie TMVP
`src/decoding/dpb.cpp` : `derive_colpic()`

## Points critiques a verifier

### Picture co-localisee (S8.3.5)
- `colPic` = `RefPicList[collocated_from_l0_flag ? 0 : 1][collocated_ref_idx]`
- `slice_temporal_mvp_enabled_flag` doit etre true

### Position co-localisee (S8.5.3.2.7)
- Bottom-right (+1, +1) en priorite : `(xPb + nPbW, yPb + nPbH)`
- Clip au CTB boundary : si hors du CTB courant, utiliser le centre
- Fallback au centre : `(xPb + nPbW/2, yPb + nPbH/2)` si bottom-right indisponible

### MV scaling (S8.5.3.2.12)
```cpp
// Distance POC
int td = Clip3(-128, 127, poc_current - poc_collocated);
int tb = Clip3(-128, 127, poc_current - poc_ref);

// Factor de scale
int tx = (16384 + (abs(td) >> 1)) / td;
int distScaleFactor = Clip3(-4096, 4095, (tb * tx + 32) >> 6);

// MV scale
mv_scaled.x = Clip3(-32768, 32767, Sign(distScaleFactor * mv_col.x) *
              ((abs(distScaleFactor * mv_col.x) + 127) >> 8));
mv_scaled.y = idem pour y;
```

## Pieges identifies

1. **Bottom-right clip au CTB** : ne pas acceder au CTB suivant (pas encore decode)
2. **MV scaling** : la formule est complexe, les clippings sont critiques
3. **collocated list selection** : `collocated_from_l0_flag` et `collocated_ref_idx`
4. **PU motion storage dans la picture co-localisee** : doit etre au bon granularity (4x4)
5. **td == 0** : cas degenere, eviter division par zero

## Audit a faire

1. Lire S8.5.3.2.7 et S8.5.3.2.12 du PDF (pages 180-185)
2. Verifier la formule de MV scaling bit-exact
3. Verifier le clip au CTB boundary pour bottom-right
4. Comparer le candidat temporel vs HM sur le premier P-frame

## Test de validation

```cpp
// Test unitaire MV scaling :
// - td=1, tb=1 : scale factor = 256, MV inchange
// - td=2, tb=1 : scale factor = 128, MV divise par 2
// - td=1, tb=2 : scale factor = 512, MV double
// - td=-1, tb=1 : scale factor negatif, MV inverse
// - Grands MV : verifier clipping
```

## Critere de sortie

- [ ] MV temporel identique a HM pour le premier merge PU du premier P-frame
- [ ] Test unitaire MV scaling (5 cas)
- [ ] Bottom-right clip au CTB verifie
