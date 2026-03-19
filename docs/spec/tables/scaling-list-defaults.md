# Scaling List Default Matrices

Matrices par defaut pour la dequantization (spec ITU-T H.265, Tables 7-3 a 7-6).

Utilisees quand `scaling_list_enabled_flag == 1` mais les scaling lists ne sont pas presentes dans le SPS/PPS.

## 4x4 — Matrice par defaut (identique intra et inter)

Flat 16 pour toutes les tailles 4x4 (Table 7-3) :

```cpp
const uint8_t default_scaling_list_4x4[16] = {
    16, 16, 16, 16,
    16, 16, 16, 16,
    16, 16, 16, 16,
    16, 16, 16, 16,
};
```

## 8x8 Intra (Table 7-4)

```cpp
const uint8_t default_scaling_list_8x8_intra[64] = {
    16, 16, 16, 16, 17, 18, 21, 24,
    16, 16, 16, 16, 17, 19, 22, 25,
    16, 16, 17, 18, 20, 22, 25, 29,
    16, 16, 18, 21, 24, 27, 31, 36,
    17, 17, 20, 24, 30, 35, 41, 47,
    18, 19, 22, 27, 35, 44, 54, 65,
    21, 22, 25, 31, 41, 54, 70, 88,
    24, 25, 29, 36, 47, 65, 88, 115,
};
```

## 8x8 Inter (Table 7-5)

```cpp
const uint8_t default_scaling_list_8x8_inter[64] = {
    16, 16, 16, 16, 17, 18, 20, 24,
    16, 16, 16, 17, 18, 20, 24, 25,
    16, 16, 17, 18, 20, 24, 25, 28,
    16, 17, 18, 20, 24, 25, 28, 33,
    17, 18, 20, 24, 25, 28, 33, 41,
    18, 20, 24, 25, 28, 33, 41, 54,
    20, 24, 25, 28, 33, 41, 54, 71,
    24, 25, 28, 33, 41, 54, 71, 91,
};
```

## 16x16 et 32x32

Les matrices 16x16 et 32x32 reutilisent les matrices 8x8 par upscaling :
- La matrice 16x16 est la matrice 8x8 upsamplee 2x en chaque dimension
- La matrice 32x32 est la matrice 8x8 upsamplee 4x, avec `dc_coef` (coin [0][0]) specifie separement

```cpp
// Upsample une matrice 8x8 vers NxN (N = 16 ou 32)
void upscale_scaling_list(const uint8_t* src_8x8, uint8_t* dst, int dstSize) {
    int ratio = dstSize / 8;
    for (int y = 0; y < dstSize; y++)
        for (int x = 0; x < dstSize; x++)
            dst[y * dstSize + x] = src_8x8[(y / ratio) * 8 + (x / ratio)];
}
```

## DC coefficient pour 16x16 et 32x32

Quand `scaling_list_dc_coef_minus8` n'est pas present, la valeur par defaut du DC coefficient est **16** (identique a la matrice 4x4).

```cpp
// Pour les matrices 16x16 et 32x32, le coefficient [0][0] est remplace par dc_coef
int dc_coef_16x16 = scaling_list_dc_coef_minus8 + 8;  // si present
// default : dc_coef = 16
```

## Mapping des scaling lists par sizeId et matrixId

```
sizeId 0 (4x4)   : matrixId 0-5  → 6 matrices (3 intra Y/Cb/Cr + 3 inter Y/Cb/Cr)
sizeId 1 (8x8)   : matrixId 0-5  → 6 matrices (3 intra + 3 inter)
sizeId 2 (16x16) : matrixId 0-5  → 6 matrices (3 intra + 3 inter)
sizeId 3 (32x32) : matrixId 0-1  → 2 matrices (1 intra Y + 1 inter Y)
```

Les matrices par defaut :
- `sizeId 0` : toutes flat 16
- `sizeId 1, matrixId 0-2` (intra) : `default_scaling_list_8x8_intra`
- `sizeId 1, matrixId 3-5` (inter) : `default_scaling_list_8x8_inter`
- `sizeId 2` : upscale de sizeId 1 (memes defaults intra/inter)
- `sizeId 3, matrixId 0` (intra) : upscale de `default_scaling_list_8x8_intra`
- `sizeId 3, matrixId 1` (inter) : upscale de `default_scaling_list_8x8_inter`

## Fallback mechanism (§7.4.5)

Quand `scaling_list_pred_mode_flag[sizeId][matrixId] == 0` et `scaling_list_pred_matrix_id_delta == 0`, la matrice par defaut s'applique.

Quand `scaling_list_pred_matrix_id_delta > 0`, la matrice est copiee depuis `matrixId - delta` (meme sizeId).
