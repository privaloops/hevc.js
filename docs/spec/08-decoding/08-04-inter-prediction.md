# 8.5.3 — Inter Prediction

Spec ref : 8.5.3.1 (general), 8.5.3.2 (luma interpolation), 8.5.3.3 (chroma interpolation), 8.5.3.4 (weighted pred)

## Vue d'ensemble

L'inter prediction reconstruit un bloc a partir d'une frame de reference + un motion vector. Le MV a une precision au 1/4 de pixel (luma), necessitant une interpolation.

## 8.5.3.1 — Processus general

```
Inputs:
- (xPb, yPb) : position du PU
- (nPbW, nPbH) : dimensions du PU
- mvL0, mvL1 : motion vectors (en 1/4 pel)
- refIdxL0, refIdxL1 : indices dans les listes de reference
- predFlagL0, predFlagL1 : flags de prediction

Process:
if (predFlagL0)
    predSamplesL0 = motion_compensation(refPicL0, mvL0, nPbW, nPbH)
if (predFlagL1)
    predSamplesL1 = motion_compensation(refPicL1, mvL1, nPbW, nPbH)

if (predFlagL0 && predFlagL1)
    // Bi-prediction : moyenne ponderee
    predSamples = (predSamplesL0 + predSamplesL1 + offset) >> shift
else if (predFlagL0)
    predSamples = predSamplesL0
else
    predSamples = predSamplesL1
```

## 8.5.3.2 — Luma Sample Interpolation

Filtre d'interpolation 8-tap pour les positions fractionnaires :

### Coefficients du filtre luma (8.5.3.2.2, Table 8-1)

```cpp
// Positions fractionnaires (1/4 pel) : 0, 1, 2, 3
// Position 0 = pixel entier, pas d'interpolation
const int16_t luma_filter[4][8] = {
    {  0,  0,  0, 64,  0,  0,  0,  0 },  // position 0 (entier)
    { -1,  4,-10, 58, 17, -5,  1,  0 },  // position 1 (1/4)
    { -1,  4,-11, 40, 40,-11,  4, -1 },  // position 2 (1/2)
    {  0,  1, -5, 17, 58,-10,  4, -1 },  // position 3 (3/4)
};
```

### Processus d'interpolation

```cpp
// Pour chaque sample (x, y) du bloc de prediction :
void luma_interpolation(
    const int16_t* ref,        // frame de reference
    int ref_stride,
    int16_t* pred,             // bloc de sortie
    int pred_stride,
    int xFrac, int yFrac,      // position fractionnaire (0-3)
    int width, int height
) {
    if (xFrac == 0 && yFrac == 0) {
        // Copie directe (pixel entier)
    } else if (yFrac == 0) {
        // Filtre horizontal seulement
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                pred[y*stride+x] = filter_h(ref, x, y, xFrac);
    } else if (xFrac == 0) {
        // Filtre vertical seulement
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                pred[y*stride+x] = filter_v(ref, x, y, yFrac);
    } else {
        // Filtre 2D : horizontal d'abord, puis vertical sur le resultat
        // Etape 1 : filtre horizontal -> buffer temporaire (avec marge verticale)
        // Etape 2 : filtre vertical sur le buffer temporaire
    }
}

// Filtre horizontal 8-tap
int filter_h(const int16_t* ref, int x, int y, int frac) {
    const int16_t* f = luma_filter[frac];
    return f[0]*ref[x-3] + f[1]*ref[x-2] + f[2]*ref[x-1] + f[3]*ref[x]
         + f[4]*ref[x+1] + f[5]*ref[x+2] + f[6]*ref[x+3] + f[7]*ref[x+4];
}
```

### Precision intermediaire pour le filtre 2D

Pour l'interpolation 2D (xFrac != 0 ET yFrac != 0), le filtre horizontal produit des valeurs intermediaires en precision etendue :
- **Pas de clipping** apres le filtre horizontal
- Les valeurs intermediaires sont sur ~22 bits (8-bit input * 8-tap filter avec coefficients jusqu'a 64)
- Le filtre vertical applique ensuite sur ces valeurs etendues
- Le clipping final est applique apres le filtre vertical : Clip3(0, (1 << BitDepth) - 1, (result + offset) >> shift)

### Shifts d'interpolation — Reference definitive (§8.5.3.3.3)

Les shifts dependent de la combinaison (H/V/HV) et du mode (uni/bi pred). Valeurs exactes :

```
Cas 1 — H-only (xFrac != 0, yFrac == 0) :
  shift    = BitDepth - 8
  offset   = shift > 0 ? (1 << (shift - 1)) : 0
  resultat = Clip3(0, (1 << BitDepth) - 1, (sum + offset) >> shift)

Cas 2 — V-only (xFrac == 0, yFrac != 0) :
  shift    = 14 - BitDepth
  offset   = 1 << (shift - 1)
  resultat = Clip3(0, (1 << BitDepth) - 1, (sum + offset) >> shift)

Cas 3 — 2D / passe H (xFrac != 0, yFrac != 0, premiere passe) :
  shift    = BitDepth - 8
  offset   = 0
  resultat = (sum + offset) >> shift    // PAS de clipping
  → valeurs intermediaires sur ~15 bits (8-bit) ou ~17 bits (10-bit)
  → buffer int16_t pour 8-bit, int32_t pour 10-bit

Cas 4 — 2D / passe V (deuxieme passe sur le buffer intermediaire) :
  shift    = 6
  offset   = 1 << 5 = 32
  resultat = Clip3(0, (1 << BitDepth) - 1, (sum + offset) >> shift)
```

Pour la **bi-prediction**, l'interpolation produit des valeurs en precision etendue (pas de clip final dans les cas 1-4), puis :

```
Bi-pred averaging :
  shift    = 15 - BitDepth
  offset   = 1 << (shift - 1)
  resultat = Clip3(0, (1 << BitDepth) - 1, (predL0 + predL1 + offset) >> shift)
```

Dans ce cas, les interpolations H-only et V-only utilisent des shifts differents pour conserver la precision etendue. Se referer a §8.5.3.3.3.1 pour les details.

## 8.5.3.3 — Chroma Sample Interpolation

Filtre 4-tap pour la chroma (precision 1/8 pel) :

```cpp
// Table 8-2
const int16_t chroma_filter[8][4] = {
    {  0, 64,  0,  0 },  // 0
    { -2, 58, 10, -2 },  // 1/8
    { -4, 54, 16, -2 },  // 2/8
    { -6, 46, 28, -4 },  // 3/8
    { -4, 36, 36, -4 },  // 4/8
    { -4, 28, 46, -6 },  // 5/8
    { -2, 16, 54, -4 },  // 6/8
    { -2, 10, 58, -2 },  // 7/8
};
```

Le MV chroma est derive du MV luma en divisant par SubWidthC/SubHeightC (avec ajustement de la position fractionnaire).

## 8.5.3.4 — Weighted Prediction

```cpp
// Si weighted_pred_flag (P slice) ou weighted_bipred_flag (B slice) :
// Uni-prediction :
predSamples[x][y] = Clip3(0, maxVal,
    ((predSamplesLX[x][y] * w0 + (1 << (log2Wd - 1))) >> log2Wd) + o0)

// Bi-prediction :
predSamples[x][y] = Clip3(0, maxVal,
    ((predSamplesL0[x][y] * w0 + predSamplesL1[x][y] * w1
      + ((o0 + o1 + 1) << log2Wd)) >> (log2Wd + 1)))
```

## Motion Vector Derivation — 8.5.3.1

### Merge Mode (8.5.3.2.2)

```
Candidats merge (dans l'ordre de priorite, Figure 8-5) :
1. A1 (left)       : (xPb-1, yPb+nPbH-1)  — bas du cote gauche du PU
2. B1 (above)      : (xPb+nPbW-1, yPb-1)  — droite du cote superieur du PU
3. B0 (above-right) : (xPb+nPbW, yPb-1)    — coin superieur droit du PU
4. A0 (below-left)  : (xPb-1, yPb+nPbH)    — coin inferieur gauche du PU
5. B2 (above-left)  : (xPb-1, yPb-1)       — coin superieur gauche, seulement si < 4 candidats
6. Temporal candidate (co-located block dans la ref pic)
7. Zero MV (padding si < MaxNumMergeCand)
```

### Combined Bi-predictive Merge Candidates (§8.5.3.2.4)

Si le nombre de candidats merge est inferieur a `MaxNumMergeCand` et que le slice est de type B, des candidats bi-predictifs combines sont generes :

```cpp
// Combiner des paires de candidats L0-only en candidats bi-predictifs
// Pour chaque paire (l0Idx, l1Idx) dans un ordre defini :
// combIdx: 0 -> (0,1), 1 -> (1,0), 2 -> (0,2), ...
for (int combIdx = 0; combIdx < numCombinations && numMergeCand < MaxNumMergeCand; combIdx++) {
    int l0CandIdx = l0CandIdxTable[combIdx];
    int l1CandIdx = l1CandIdxTable[combIdx];

    MergeCand& l0Cand = mergeCandList[l0CandIdx];
    MergeCand& l1Cand = mergeCandList[l1CandIdx];

    // Combiner seulement si les ref pics sont differentes
    // (sinon c'est redondant avec le candidat original)
    if (l0Cand.refPicL0 != l1Cand.refPicL1) {
        MergeCand combined;
        combined.predFlagL0 = 1;
        combined.predFlagL1 = 1;
        combined.mvL0 = l0Cand.mvL0;
        combined.refIdxL0 = l0Cand.refIdxL0;
        combined.mvL1 = l1Cand.mvL1;
        combined.refIdxL1 = l1Cand.refIdxL1;
        mergeCandList[numMergeCand++] = combined;
    }
}
```

### Ordre des paires (Table 8-8 de la spec)

| combIdx | l0CandIdx | l1CandIdx |
|---------|-----------|-----------|
| 0 | 0 | 1 |
| 1 | 1 | 0 |
| 2 | 0 | 2 |
| 3 | 2 | 0 |
| 4 | 1 | 2 |
| ... | ... | ... |

### AMVP (8.5.3.2.6)

```
Candidats AMVP (2 candidats) :
1. Spatial : A0/A1 (gauche), puis B0/B1/B2 (haut)
2. Temporal : co-located block
3. Zero MV (padding)

MV final = mvp_candidate[mvp_flag] + mvd
```

## Pieges connus

1. **Clipping MV** : Le MV peut pointer hors de la frame -> clamping necessaire + padding des bords
2. **Interpolation 2D** : Le filtre horizontal produit des valeurs intermediaires sur plus de bits -> attention au buffer temporaire (besoin de 16 bits min)
3. **Bi-prediction shift/round** : Les erreurs d'arrondi sont la source #1 de mismatch avec l'oracle
4. **Chroma MV derivation** : Diviser le MV luma par SubWidthC/SubHeightC, la position fractionnaire change

## Checklist

- [ ] Motion vector merge mode (5 spatial + 1 temporal + zero padding)
- [ ] AMVP mode (2 candidates + mvd)
- [ ] Luma interpolation 8-tap (H, V, HV)
- [ ] Chroma interpolation 4-tap
- [ ] Bi-prediction averaging
- [ ] Weighted prediction (explicit)
- [ ] MV clipping aux bords de la frame
- [ ] Tests : bitstreams P-only puis B, pixel-perfect vs libde265
