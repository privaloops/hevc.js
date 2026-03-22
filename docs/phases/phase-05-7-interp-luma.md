# 5.7 -- Interpolation Luma (8-tap)

## Objectif

Verifier que le filtre d'interpolation luma 8-tap est bit-exact pour les 4 cas
de shift/clip (H-only, V-only, 2D passe H, 2D passe V).

## Spec refs

- S8.5.3.2.2 : Luma sample interpolation process
- Table 8-1 : Luma interpolation filter coefficients

## Code existant

`src/decoding/interpolation.cpp` (303 lignes) : `perform_inter_prediction()`

## Coefficients du filtre (Table 8-1)

```cpp
const int16_t luma_filter[4][8] = {
    {  0,  0,  0, 64,  0,  0,  0,  0 },  // frac=0 (entier)
    { -1,  4,-10, 58, 17, -5,  1,  0 },  // frac=1 (1/4 pel)
    { -1,  4,-11, 40, 40,-11,  4, -1 },  // frac=2 (1/2 pel)
    {  0,  1, -5, 17, 58,-10,  4, -1 },  // frac=3 (3/4 pel)
};
```

## 4 cas de shift/clip (SOURCE #1 DE MISMATCH)

### Cas 1 : H-only (xFrac != 0, yFrac == 0)
```
shift1 = BitDepth - 8     (= 0 pour 8-bit)
offset1 = 0 (si shift1 == 0) ou 1 << (shift1 - 1)
result = Clip3(0, maxVal, (sum + offset1) >> shift1)
```

### Cas 2 : V-only (xFrac == 0, yFrac != 0)
```
shift1 = BitDepth - 8
offset1 = 0 ou 1 << (shift1 - 1)
result = Clip3(0, maxVal, (sum + offset1) >> shift1)
```
Note: meme shift que H-only pour uni-pred.

### Cas 3 : 2D passe H (xFrac != 0, yFrac != 0, premiere passe)
```
shift1 = BitDepth - 8
offset1 = 0 ou 1 << (shift1 - 1)
temp = (sum + offset1) >> shift1     ← PAS de clip ! Resultat int16_t
```

### Cas 4 : 2D passe V (deuxieme passe sur le buffer temporaire)
```
shift2 = 6
offset2 = 1 << (shift2 - 1) = 32
result = Clip3(0, maxVal, (sum + offset2) >> shift2)
```

### Bi-prediction : precision etendue
Pour bi-pred, les passes H et V utilisent des shifts DIFFERENTS :
```
shift1_bipred = 14 - BitDepth    (= 6 pour 8-bit)
Pas de clip intermediaire, resultat en precision etendue (int16_t)
Averaging final : (predL0 + predL1 + offset) >> (15 - BitDepth + 1)
                  avec offset = 1 << (15 - BitDepth)
```

## Pieges identifies

1. **shift1 pour 8-bit = 0** : pas de shift en passe H pour uni-pred 8-bit
2. **2D passe H : PAS de clip** entre les deux passes
3. **Bi-pred shifts different de uni-pred** : shift1=6 au lieu de 0 pour 8-bit
4. **Buffer temporaire** : doit etre int16_t, pas uint8_t (valeurs peuvent etre negatives)
5. **Marge du buffer temp** : pour la passe V, il faut 3 lignes au-dessus et 4 en-dessous

## Audit a faire

1. Lire S8.5.3.2.2 du PDF (pages 163-168) -- LIRE LE PDF, PAS LES NOTES
2. Verifier chaque cas de shift/offset dans le code
3. Verifier le clip ou non-clip entre les passes

## Test de validation

```cpp
// Test unitaire par cas :
// 1. H-only frac=2 sur 8 samples : verifier le resultat exact
// 2. V-only frac=1 sur 8 samples
// 3. 2D frac=(1,2) sur 4x4 bloc : verifier buffer temp + resultat final
// 4. Bi-pred : verifier precision etendue + averaging

// Vecteurs de test : utiliser des valeurs simples (128, 129, 130...)
// pour pouvoir calculer le resultat a la main
```

## Critere de sortie

- [ ] Test unitaire H-only : 3 positions fractionnaires (1, 2, 3)
- [ ] Test unitaire V-only : 3 positions fractionnaires
- [ ] Test unitaire 2D : au moins 1 position (xFrac, yFrac) != (0,0)
- [ ] Verification shifts : tous les shifts correspondent a la spec pour 8-bit
