# 8.5.4 — Intra Prediction

Spec ref : 8.5.4.1 (general), 8.5.4.2 (reference sample availability + substitution), 8.5.4.3 (filtering), 8.5.4.4 (Planar), 8.5.4.5 (DC), 8.5.4.6 (Angular)

## Vue d'ensemble

35 modes intra pour luma (0-34), 5 modes pour chroma :

| Mode | Nom | Description |
|------|-----|-------------|
| 0 | Planar | Interpolation bilineaire |
| 1 | DC | Moyenne des samples de reference |
| 2-34 | Angular | Directionnels (26 = vertical, 10 = horizontal) |

## 8.5.4.2 — Reference Sample Availability

Avant la prediction, il faut construire le vecteur de samples de reference `p[x][y]` :

```
Samples de reference (pour un bloc NxN) :

        p[-1][-1]  p[0][-1] ... p[2N-1][-1]
        p[-1][0]
        p[-1][1]
        ...
        p[-1][2N-1]

-> 4N+1 samples au total (coin + bord haut + bord gauche)
```

### Processus de disponibilite

```cpp
// Pour chaque position de sample de reference :
// 1. Verifier si le bloc voisin est disponible (dans le meme slice/tile, deja decode)
// 2. Si non disponible : substitution
//    - Premier sample disponible -> propager aux positions non disponibles
//    - Si aucun disponible : valeur par defaut = 1 << (BitDepth - 1) (mi-echelle)
```

## 8.5.4.3 — Reference Sample Filtering

```cpp
// Le filtrage depend du mode et de la taille du bloc
bool filter_flag;
if (intraMode == DC || nTbS == 4)
    filter_flag = false;  // Pas de filtrage pour DC ou 4x4
else {
    // Table 8-3 : minDistVerHor = min(abs(mode-26), abs(mode-10))
    int minDist = std::min(std::abs(mode - 26), std::abs(mode - 10));
    // Threshold depend de la taille du bloc
    int thresh = (nTbS == 8) ? 7 : (nTbS == 16) ? 1 : 0;
    filter_flag = (minDist > thresh);
}

if (filter_flag) {
    // Filtre [1,2,1]/4 sur les samples de reference
    p'[-1][-1] = (p[-1][0] + 2*p[-1][-1] + p[0][-1] + 2) >> 2;
    for (i = 0; i < 2*nTbS - 1; i++) {
        p'[-1][i] = (p[-1][i-1] + 2*p[-1][i] + p[-1][i+1] + 2) >> 2;
        p'[i][-1] = (p[i-1][-1] + 2*p[i][-1] + p[i+1][-1] + 2) >> 2;
    }
    // Dernier sample non filtre
}

// Strong intra smoothing (8.5.4.2.3) pour 32x32
if (strong_intra_smoothing_enabled_flag && nTbS == 32 && filter_flag) {
    bool bilinear = (abs(p[-1][63] + p[63][-1] - 2*p[-1][-1]) < threshold);
    if (bilinear) {
        // Interpolation bilineaire entre les coins
        for (i = 0; i < 63; i++) {
            p'[-1][i] = ((63-i)*p[-1][-1] + (i+1)*p[-1][63] + 32) >> 6;
            p'[i][-1] = ((63-i)*p[-1][-1] + (i+1)*p[63][-1] + 32) >> 6;
        }
    }
}
```

## 8.5.4.4 — Planar Mode (Mode 0)

```cpp
// 8.5.4.2.4 — Interpolation bilineaire
void intra_pred_planar(int16_t* pred, int stride, const int16_t* ref, int nTbS) {
    int log2 = log2(nTbS);
    for (int y = 0; y < nTbS; y++) {
        for (int x = 0; x < nTbS; x++) {
            pred[y*stride + x] = (
                (nTbS - 1 - x) * ref[-1 + y+1]  +    // left ref, weighted
                (x + 1)        * ref[nTbS + 1 + (-1)] + // top-right corner
                (nTbS - 1 - y) * ref[x + 1]      +    // top ref, weighted
                (y + 1)        * ref[-1 + nTbS+1]  +  // bottom-left corner
                nTbS  // rounding
            ) >> (log2 + 1);
        }
    }
}
```

*Note : l'indexation ci-dessus est schematique. L'implementation reelle doit respecter exactement le layout du vecteur de reference.*

## 8.5.4.5 — DC Mode (Mode 1)

```cpp
void intra_pred_dc(int16_t* pred, int stride, const int16_t* ref, int nTbS, int cIdx) {
    // Moyenne des samples top + left
    int sum = 0;
    for (int i = 0; i < nTbS; i++) {
        sum += ref[i + 1];       // top (p[i][-1])
        sum += ref[-1 - i - 1];  // left (p[-1][i]) — depend du layout
    }
    int dcVal = (sum + nTbS) >> (log2(nTbS) + 1);

    // Remplir le bloc
    for (int y = 0; y < nTbS; y++)
        for (int x = 0; x < nTbS; x++)
            pred[y*stride + x] = dcVal;

    // DC filtering (luma seulement, cIdx == 0)
    if (cIdx == 0) {
        pred[0] = (ref[1] + 2*dcVal + ref[-1-1] + 2) >> 2;  // coin
        for (int x = 1; x < nTbS; x++)
            pred[x] = (ref[x+1] + 3*dcVal + 2) >> 2;        // bord haut
        for (int y = 1; y < nTbS; y++)
            pred[y*stride] = (ref[-1-y-1] + 3*dcVal + 2) >> 2; // bord gauche
    }
}
```

## 8.5.4.6 — Angular Modes (Modes 2-34)

### Angles de prediction

```cpp
// Table 8-4 : angles en 1/32 de pixel
const int8_t intraPredAngle[35] = {
    0,   0,  32, 26, 21, 17, 13,  9,  5,  2,  // modes 0-9
    0,  -2, -5, -9,-13,-17,-21,-26,-32,-26,    // modes 10-19
   -21,-17,-13, -9, -5, -2,  0,  2,  5,  9,    // modes 20-29
    13, 17, 21, 26, 32                           // modes 30-34
};

// invAngle pour modes avec angle negatif (modes 11-25 sauf 10, 26)
const int16_t invAngle[35] = { /* ... */ };
```

### Processus

```cpp
void intra_pred_angular(int16_t* pred, int stride, const int16_t* ref,
                         int nTbS, int mode, int cIdx) {
    int angle = intraPredAngle[mode];
    bool is_horizontal = (mode >= 2 && mode <= 17);  // modes 2-17

    // Construire le vecteur de reference etendu
    int16_t refMain[2*64+1];  // taille max pour 32x32
    int16_t refSide[2*64+1];

    if (is_horizontal) {
        // Main = left, Side = top
        // Copier left dans refMain, top dans refSide
    } else {
        // Main = top, Side = left
    }

    // Extension pour angles negatifs
    if (angle < 0) {
        int invAng = invAngle[mode];
        for (int i = -1; i >= -(nTbS * angle >> 5) - 1; i--) {
            refMain[i] = refSide[-1 + ((-1 - i) * invAng + 128) >> 8];
        }
    }

    // Prediction
    for (int y = 0; y < nTbS; y++) {
        for (int x = 0; x < nTbS; x++) {
            int idx, fact;
            if (is_horizontal) {
                idx = ((y + 1) * angle) >> 5;
                fact = ((y + 1) * angle) & 31;
            } else {
                idx = ((x + 1) * angle) >> 5;  // Note: simplified, actual depends on mode
                fact = ((x + 1) * angle) & 31;
            }

            if (fact != 0)
                pred[y*stride+x] = ((32-fact)*refMain[idx+1+x] + fact*refMain[idx+2+x] + 16) >> 5;
            else
                pred[y*stride+x] = refMain[idx+1+x];
        }
    }

    // Post-filter pour modes purement vertical (26) ou horizontal (10)
    // sur la premiere row/column, seulement pour luma
}
```

*Note : le pseudo-code ci-dessus est simplifie. La spec distingue soigneusement les cas horizontal/vertical et l'indexation change. Il faut suivre la spec exactement.*

## Intra Mode Derivation — 8.5.4.1

### Most Probable Modes (MPM)

```cpp
// 3 MPM derives des voisins A (gauche) et B (haut)
void derive_mpm(int candModeList[3], int modeA, int modeB) {
    if (modeA == modeB) {
        if (modeA < 2) {
            candModeList[0] = 0;  // Planar
            candModeList[1] = 1;  // DC
            candModeList[2] = 26; // Vertical
        } else {
            candModeList[0] = modeA;
            candModeList[1] = 2 + ((modeA - 2 + 29) % 32);  // voisin -1
            candModeList[2] = 2 + ((modeA - 2 +  1) % 32);  // voisin +1  (spec: inverse?)
        }
    } else {
        candModeList[0] = modeA;
        candModeList[1] = modeB;
        if (modeA != 0 && modeB != 0)
            candModeList[2] = 0;   // Planar
        else if (modeA != 1 && modeB != 1)
            candModeList[2] = 1;   // DC
        else
            candModeList[2] = 26;  // Vertical
    }
}

// Si prev_intra_luma_pred_flag :
//    IntraPredModeY = candModeList[mpm_idx]
// Sinon :
//    Trier les 3 MPM, puis indexer rem_intra_luma_pred_mode dans les 32 modes restants
```

### Chroma mode derivation

```cpp
// 5 modes chroma possibles :
// intra_chroma_pred_mode  -> mode
// 0                       -> Planar (0)
// 1                       -> Vertical (26)
// 2                       -> Horizontal (10)
// 3                       -> DC (1)
// 4                       -> Derived from luma (IntraPredModeY)
// Si le mode chroma == luma mode, remplacer par mode 34
```

## Checklist

- [ ] Reference sample availability check
- [ ] Reference sample substitution
- [ ] Reference sample filtering ([1,2,1] + strong intra smoothing)
- [ ] Planar mode prediction
- [ ] DC mode prediction + DC filtering
- [ ] Angular prediction (modes 2-34) avec extension des refs
- [ ] Post-filtering pour vertical/horizontal purs
- [ ] MPM derivation
- [ ] Chroma mode derivation
- [ ] Tests : bitstreams I-only, chaque mode isole si possible
