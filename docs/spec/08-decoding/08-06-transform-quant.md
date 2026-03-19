# 8.6 — Transform and Quantization

Spec ref : 8.6.1 (scaling/transform general), 8.6.2 (scaling), 8.6.3 (transform), 8.6.4 (cross-component prediction)

## Vue d'ensemble

```
Coefficients CABAC -> Dequant (scaling) -> Transform inverse -> Residu
```

## 8.6.2 — Scaling (Dequantisation)

### Processus

```cpp
// Pour chaque coefficient (x, y) du TU :
// d[x][y] = coefficient decode par CABAC (TransCoeffLevel)

// 8.6.2 — Scaling process
int scale_coefficient(int coeff, int qp, int log2TrSize, int cIdx,
                       const ScalingList* scaling_list) {
    // 1. Determiner le QP effectif
    int qpPrime;
    if (cIdx == 0)
        qpPrime = qp;  // Qp'Y derive du slice/CU QP delta
    else
        qpPrime = qpC;  // Qp'Cb ou Qp'Cr (table de mapping 8.6.1)

    int qpPer = qpPrime / 6;
    int qpRem = qpPrime % 6;

    // 2. Scaling factor
    int scale;
    if (scaling_list) {
        // Utiliser la matrice de scaling list
        scale = scaling_list->get(log2TrSize, cIdx, x, y);
    } else {
        // Matrice par defaut : flat 16
        scale = 16;
    }

    // 3. Level scale (Table 8-250 dans la spec)
    const int levelScale[6] = { 40, 45, 51, 57, 64, 72 };

    // 4. Calcul
    int shift = std::max(0, 14 - log2TrSize) + qpPer;  // shift depend de la taille
    // En realite la formule exacte est :
    // d'[x][y] = Clip3(coeffMin, coeffMax,
    //   ((d[x][y] * scale * levelScale[qpRem]) << qpPer) + (1 << (shift-1))) >> shift
    // ... la formule exacte varie avec bdShift, voir spec

    return result;
}
```

### QP Chroma Mapping (Table 8-10)

```cpp
// qPi -> qPc mapping (non-lineaire pour qPi > 30)
const int8_t qp_chroma_table[58] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    29, 30, 31, 32, 33, 33, 34, 34, 35, 35,
    36, 36, 37, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 48, 49, 50, 51
};
// qPi = Clip3(0, 57, QpY + pps_cb_qp_offset + slice_cb_qp_offset)
// qPc = qp_chroma_table[qPi]
// Qp'C = qPc + QpBdOffsetC
```

## 8.6.3 — Transform Inverse

### Matrices de transform

HEVC utilise des approximations entieres de DCT-II (et DST pour luma 4x4 intra) :

```cpp
// DST-VII 4x4 (luma intra seulement) — Table 8-11
const int16_t dst_4x4[4][4] = {
    { 29, 55, 74, 84 },
    { 74, 74,  0,-74 },
    { 84,-29,-74, 55 },
    { 55,-84, 74,-29 }
};

// DCT-II 4x4 — Table 8-12
const int16_t dct_4x4[4][4] = {
    { 64, 64, 64, 64 },
    { 83, 36,-36,-83 },
    { 64,-64,-64, 64 },
    { 36,-83, 83,-36 }
};

// DCT 8x8 — Table 8-13
const int16_t dct_8x8[8][8] = { /* ... 8x8 matrice */ };

// DCT 16x16 — Table 8-14
const int16_t dct_16x16[16][16] = { /* ... */ };

// DCT 32x32 — Table 8-15
const int16_t dct_32x32[32][32] = { /* ... */ };
```

### Processus de transform inverse

```cpp
// 8.6.4.2 — Transform process (2D = vertical puis horizontal)
void transform_inverse(int16_t* coeffs, int16_t* residual, int log2TrSize,
                        int cIdx, bool intra) {
    int nTbS = 1 << log2TrSize;
    int16_t temp[32][32];

    // Choix de la matrice
    const int16_t* matrix;
    if (nTbS == 4 && cIdx == 0 && intra)
        matrix = dst_4x4;  // DST pour luma 4x4 intra
    else
        matrix = get_dct_matrix(nTbS);

    // Transform skip check
    if (transform_skip_flag) {
        // Pas de transform, juste un shift
        // residual = coeffs << shift
        return;
    }

    // Etape 1 : Transform verticale (colonnes)
    int shift1 = 7;
    for (int col = 0; col < nTbS; col++) {
        partial_butterfly_inverse(coeffs, temp, col, nTbS, matrix, shift1);
    }

    // Etape 2 : Transform horizontale (lignes)
    int shift2 = 20 - bitDepth;
    for (int row = 0; row < nTbS; row++) {
        partial_butterfly_inverse(temp, residual, row, nTbS, matrix, shift2);
    }
}
```

### Partial Butterfly

La spec utilise une "partial butterfly" pour optimiser la multiplication matrice x vecteur :

```cpp
// Pour 4x4 DCT inverse :
void idct4(const int16_t* src, int16_t* dst, int shift) {
    int add = 1 << (shift - 1);
    for (int i = 0; i < 4; i++) {
        // Even part
        int E0 = 64*src[0*4+i] + 64*src[2*4+i];
        int E1 = 64*src[0*4+i] - 64*src[2*4+i];
        // Odd part
        int O0 = 83*src[1*4+i] + 36*src[3*4+i];
        int O1 = 36*src[1*4+i] - 83*src[3*4+i];

        dst[0*4+i] = Clip3(min, max, (E0 + O0 + add) >> shift);
        dst[1*4+i] = Clip3(min, max, (E1 + O1 + add) >> shift);
        dst[2*4+i] = Clip3(min, max, (E1 - O1 + add) >> shift);
        dst[3*4+i] = Clip3(min, max, (E0 - O0 + add) >> shift);
    }
}
// Structures similaires pour 8x8, 16x16, 32x32 avec plus d'etages even/odd
```

## 8.6.1 — QP Derivation

Le QP peut varier par CU (cu_qp_delta). Le processus :

```
SliceQpY = 26 + pps.init_qp_minus26 + slice_qp_delta
QpY = ((QpY_prev + CuQpDeltaVal + 52 + 2*QpBdOffsetY) % (52 + QpBdOffsetY)) - QpBdOffsetY
Qp'Y = QpY + QpBdOffsetY
```

## Sign Data Hiding (8.6.1)

Si `sign_data_hiding_enabled_flag` et que le premier et dernier coefficient significatifs sont separes de plus de 3 positions :
- Le signe du premier coefficient est infere de la parite de la somme des niveaux

## Checklist

- [ ] QP derivation (luma + chroma avec table de mapping)
- [ ] CU QP delta handling
- [ ] Scaling process (dequant) avec scaling lists
- [ ] Scaling process sans scaling lists (flat 16)
- [ ] DST 4x4 inverse (luma intra)
- [ ] DCT 4x4, 8x8, 16x16, 32x32 inverse
- [ ] Transform skip
- [ ] Sign data hiding
- [ ] Reconstruction : Clip(pred + residual)
- [ ] Tests : coefficients connus -> verifier residu exact
