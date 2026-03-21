# 5.8 -- Interpolation Chroma + Bi-prediction

## Objectif

Verifier le filtre chroma 4-tap et le pipeline bi-prediction complet.

## Spec refs

- S8.5.3.3 : Chroma sample interpolation process
- Table 8-2 : Chroma interpolation filter coefficients
- S8.5.3.2.1 eq 8-166..8-175 : Weighted sample prediction (default + explicit)

## Filtre chroma 4-tap (Table 8-2)

```cpp
const int16_t chroma_filter[8][4] = {
    {  0, 64,  0,  0 },  // frac=0
    { -2, 58, 10, -2 },  // frac=1
    { -4, 54, 16, -2 },  // frac=2
    { -6, 46, 28, -4 },  // frac=3
    { -4, 36, 36, -4 },  // frac=4
    { -4, 28, 46, -6 },  // frac=5
    { -2, 16, 54, -4 },  // frac=6
    { -2, 10, 58, -2 },  // frac=7
};
```

## MV chroma derivation

```cpp
// MV luma est en 1/4 pel, MV chroma en 1/8 pel (pour 4:2:0)
mvC.x = mv.x;  // deja en 1/4 pel luma = 1/8 pel chroma (car SubWidthC=2)
mvC.y = mv.y;
// Fractional part
xFracC = mvC.x & 7;  // 3 bits pour 8 positions
yFracC = mvC.y & 7;
// Integer part
xIntC = mvC.x >> 3;
yIntC = mvC.y >> 3;
```

**ATTENTION** : pour 4:2:0, les coordonnees chroma sont divisees par 2
(SubWidthC=2, SubHeightC=2). Le MV chroma a PLUS de precision fractionnaire.

## Bi-prediction averaging

### Default weighted prediction (S8.5.3.2.1)
```cpp
// Uni-pred : clip to [0, maxVal]
pred = Clip3(0, maxVal, predSamples);

// Bi-pred : averaging avec precision etendue
shift = 15 - BitDepth;  // = 7 pour 8-bit
offset = 1 << (shift - 1);  // = 64
pred = Clip3(0, maxVal, (predL0 + predL1 + offset) >> shift);
```

### Explicit weighted prediction (S8.5.3.4)
```cpp
// w0, w1 : poids, o0, o1 : offsets, logWDC : denominateur
shift = logWDC + 1;  // pour bi-pred
offset = ((o0 + o1 + 1) << logWDC);
pred = Clip3(0, maxVal, (w0 * predL0 + w1 * predL1 + offset) >> shift);
```

## Pieges identifies

1. **Chroma MV** : derivation depuis le MV luma, PAS un MV independant
2. **Precision chroma 1/8 pel** : 8 positions fractionnaires, pas 4
3. **Bi-pred sans clip intermediaire** : les deux predictions restent en precision
   etendue (int16_t) jusqu'au averaging final
4. **Weighted pred** : les poids sont dans le slice header (pred_weight_table)
5. **logWDC** : `luma_log2_weight_denom` (luma) ou `+ delta_chroma_log2_weight_denom` (chroma)

## Test de validation

```cpp
// Tests unitaires :
// 1. Chroma H-only frac=4 (demi-pixel) : verifier coefficients
// 2. Chroma 2D frac=(2,6) : verifier buffer temp + resultat
// 3. Bi-pred averaging : predL0=100, predL1=200, verifier le resultat exact
// 4. Weighted pred : w0=64, w1=64, o0=0, o1=0 (equivalent a default)
```

## Critere de sortie

- [ ] Test unitaire chroma interpolation (au moins 2 positions fractionnaires)
- [ ] Test unitaire bi-pred default averaging
- [ ] Test unitaire weighted prediction (si utilise dans les test streams)
- [ ] Verification MV chroma derivation
