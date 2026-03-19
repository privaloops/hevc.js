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
Candidats merge (dans l'ordre de priorite) :
1. A1 (gauche-bas)
2. B1 (haut-droite)
3. B0 (haut-droite du bloc)
4. A0 (bas-gauche du bloc)
5. B2 (haut-gauche) — seulement si < 4 candidats
6. Temporal candidate (co-located block dans la ref pic)
7. Zero MV (padding si < MaxNumMergeCand)
```

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
