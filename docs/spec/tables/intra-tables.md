# Intra Prediction Tables

Tables pour la prediction intra (spec ITU-T H.265, section 8.4.4.2).

## intraPredAngle[35] — Angles de prediction (Table 8-4)

Index par mode intra (0-34). Les modes 0 (Planar) et 1 (DC) n'utilisent pas d'angle.

```cpp
const int8_t intraPredAngle[35] = {
     0,   0,  // modes 0-1 : Planar, DC (pas d'angle)
    32,  26,  21,  17,  13,   9,   5,   2,  // modes 2-9
     0,  -2,  -5,  -9, -13, -17, -21, -26,  // modes 10-17
   -32, -26, -21, -17, -13,  -9,  -5,  -2,  // modes 18-25
     0,   2,   5,   9,  13,  17,  21,  26,  // modes 26-33
    32                                        // mode 34
};
```

## invAngle[35] — Angles inverses pour projection (Table 8-5)

Utilise uniquement pour les modes avec angle negatif (modes 11-25 sauf 18 et 26).
`invAngle = round(256 * 32 / abs(intraPredAngle))`.

```cpp
const int16_t invAngle[35] = {
       0,    0,  // modes 0-1 (pas utilise)
     256,  315,  390,  482,  630,  910, 1638, 4096,  // modes 2-9
       0, 4096, 1638,  910,  630,  482,  390,  315,  // modes 10-17
     256,  315,  390,  482,  630,  910, 1638, 4096,  // modes 18-25
       0, 4096, 1638,  910,  630,  482,  390,  315,  // modes 26-33
     256                                              // mode 34
};
```

Note : seules les valeurs pour les angles negatifs sont utilisees. Les 0 pour les modes 0, 1, 10, 26 ne sont jamais accedes car ces modes n'ont pas d'angle negatif.

## Filtrage des echantillons de reference (Table 8-3)

Condition d'application du filtre [1,2,1]/4 sur les echantillons de reference, en fonction du mode et de la taille du bloc :

```cpp
// Retourne true si le filtre doit etre applique
bool intraFilterRequired(int mode, int log2BlkSize) {
    // Pas de filtrage pour les blocs 4x4
    if (log2BlkSize == 2) return false;

    // Strong intra smoothing pour 32x32 (traite separement)
    // Le filtre [1,2,1] est applique pour les modes suivants :

    // Table 8-3 : modes filtres par taille de bloc
    // 8x8  : modes 2-9, 11-17, 19-25, 27-34 (tous sauf 0, 1, 10, 18, 26)
    // 16x16 : modes 2-9, 11-17, 19-25, 27-34 (idem)
    // 32x32 : strong intra smoothing OU filtre [1,2,1] selon la condition

    // Simplifie : filtrer si le mode n'est pas DC, Planar, horizontal exact (10),
    // vertical exact (26), diagonal exacte (2, 18, 34)
    static const bool filterFlag[35] = {
    //   0     1     2     3     4     5     6     7     8     9
        true, false,false, true, true, true, true, true, true, true,
    //  10    11    12    13    14    15    16    17    18    19
        false,true, true, true, true, true, true, true, false,true,
    //  20    21    22    23    24    25    26    27    28    29
        true, true, true, true, true, true, false,true, true, true,
    //  30    31    32    33    34
        true, true, true, true, false,
    };

    // La spec utilise une condition plus precise basee sur minDistVerHor
    // qui depend de abs(intraPredAngle[mode])
    // Pour 8x8 : filtrer si minDistVerHor > intraHorVerDistThres[log2BlkSize-2]
    // intraHorVerDistThres = { 7, 1, 0 } pour 8x8, 16x16, 32x32

    int absAngle = abs(intraPredAngle[mode]);
    int thresholds[3] = { 7, 1, 0 };  // pour log2BlkSize = 3, 4, 5
    return absAngle <= thresholds[log2BlkSize - 3];
}
```

## Strong Intra Smoothing (32x32 seulement)

Condition d'application :

```cpp
bool useStrongSmoothing(int log2BlkSize, int BitDepth,
                         const int16_t* refSamples) {
    if (log2BlkSize != 5) return false;  // 32x32 seulement
    if (!sps.strong_intra_smoothing_enabled_flag) return false;

    int topLeft  = refSamples[0];             // p[-1][-1]
    int topRight = refSamples[64];            // p[63][-1]  (2*nTbS)
    int botLeft  = refSamples[64 + 64];       // p[-1][63]

    int threshold = 1 << (BitDepth - 5);

    // Bilinear condition : les echantillons intermediaires doivent etre
    // proches de l'interpolation bilineaire entre les coins
    bool bilinearTop = abs(topLeft + topRight - 2 * refSamples[32]) < threshold;
    bool bilinearLeft = abs(topLeft + botLeft - 2 * refSamples[64 + 32]) < threshold;

    return bilinearTop && bilinearLeft;
}
```

## MPM (Most Probable Modes) Derivation

```cpp
void deriveMPM(int candModeA, int candModeB, int candModeList[3]) {
    if (candModeA == candModeB) {
        if (candModeA < 2) {
            // Planar ou DC
            candModeList[0] = 0;  // Planar
            candModeList[1] = 1;  // DC
            candModeList[2] = 26; // Vertical
        } else {
            candModeList[0] = candModeA;
            candModeList[1] = 2 + ((candModeA - 2 + 29) % 32);  // -3 modulo
            candModeList[2] = 2 + ((candModeA - 2 +  1) % 32);  // +1 modulo
        }
    } else {
        candModeList[0] = candModeA;
        candModeList[1] = candModeB;

        if (candModeA != 0 && candModeB != 0)
            candModeList[2] = 0;  // Planar
        else if (candModeA != 1 && candModeB != 1)
            candModeList[2] = 1;  // DC
        else
            candModeList[2] = 26; // Vertical
    }
}
```
