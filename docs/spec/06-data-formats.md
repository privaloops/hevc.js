# Chapitre 6 — Source, Coded, Decoded and Output Data Formats

Spec ref : ITU-T H.265 §6

## 6.1 — Formats video source et decodee

### Espace colorimetrique

HEVC travaille en YCbCr (Y = luminance, Cb/Cr = chrominance). La spec definit les formats suivants :

| chroma_format_idc | Format | SubWidthC | SubHeightC | Description |
|-------------------|--------|-----------|------------|-------------|
| 0 | Monochrome | - | - | Luminance seule |
| 1 | 4:2:0 | 2 | 2 | Chroma sous-echantillonne H+V |
| 2 | 4:2:2 | 2 | 1 | Chroma sous-echantillonne H |
| 3 | 4:4:4 | 1 | 1 | Plein resolution chroma |

### Bit depth

```cpp
// Spec §7.4.3.2.1
BitDepthY = 8 + bit_depth_luma_minus8       // [8..16]
BitDepthC = 8 + bit_depth_chroma_minus8     // [8..16]
QpBdOffsetY = 6 * bit_depth_luma_minus8
QpBdOffsetC = 6 * bit_depth_chroma_minus8
```

### Dimensions

```cpp
// Dimensions en luma samples
pic_width_in_luma_samples   // doit etre multiple de MinCbSizeY
pic_height_in_luma_samples  // doit etre multiple de MinCbSizeY

// Dimensions en CTU
PicWidthInCtbsY  = Ceil(pic_width_in_luma_samples / CtbSizeY)
PicHeightInCtbsY = Ceil(pic_height_in_luma_samples / CtbSizeY)
PicSizeInCtbsY   = PicWidthInCtbsY * PicHeightInCtbsY
```

### Conformance window

La conformance window (§7.4.3.2.1) permet de cropper l'image de sortie :

```cpp
// Les offsets sont en unites de SubWidthC/SubHeightC pour chroma
CropLeft   = conf_win_left_offset * SubWidthC
CropRight  = conf_win_right_offset * SubWidthC
CropTop    = conf_win_top_offset * SubHeightC
CropBottom = conf_win_bottom_offset * SubHeightC

// Dimensions de sortie
OutputWidth  = pic_width_in_luma_samples - CropLeft - CropRight
OutputHeight = pic_height_in_luma_samples - CropTop - CropBottom
```

## 6.2 — Ordre de scan

### Raster scan vs Z-scan

La spec utilise deux ordres de scan principaux :

**Raster scan** : gauche->droite, haut->bas (pour les CTU dans un slice/tile).

**Z-scan (Morton order)** : utilise pour les CU/TU dans un CTU.

```
Z-scan order pour un bloc 4x4 divise en 2x2 :
┌───┬───┐
│ 0 │ 1 │
├───┼───┤
│ 2 │ 3 │
└───┴───┘
Recursivement applique a chaque sous-bloc.
```

### Tables de conversion

```cpp
// Spec §6.5.1 - Z-scan to raster scan
// MinTbAddrZS[x][y] : coordonnees (x,y) en unites MinTB → adresse Z-scan
// Inverse : RasterToZScan et ZScanToRaster

// Implementation : precalculer ces tables a l'activation du SPS
void init_scan_tables(const SPS& sps) {
    // Calculer MinTbAddrZS selon §6.5.1
    // Calculer les tables de conversion tile/slice
}
```

### Scan order des coefficients

Les coefficients de transform sont scannes en diagonale (§6.5.3) :

```
Diagonal scan 4x4 :
┌────┬────┬────┬────┐
│  0 │  2 │  5 │  9 │
├────┼────┼────┼────┤
│  1 │  4 │  8 │ 12 │
├────┼────┼────┼────┤
│  3 │  7 │ 11 │ 14 │
├────┼────┼────┼────┤
│  6 │ 10 │ 13 │ 15 │
└────┴────┴────┴────┘
```

Les tables de scan sont precalculees pour les tailles 4x4, 8x8, 16x16, 32x32.

## 6.3 — Implementation C++

### Structures cles

```cpp
struct ChromaFormat {
    uint8_t chroma_format_idc;  // 0-3
    uint8_t SubWidthC;          // derive de chroma_format_idc
    uint8_t SubHeightC;         // derive de chroma_format_idc
};

struct PictureFormat {
    uint32_t width;             // pic_width_in_luma_samples
    uint32_t height;            // pic_height_in_luma_samples
    uint8_t  bit_depth_y;       // BitDepthY
    uint8_t  bit_depth_c;       // BitDepthC
    ChromaFormat chroma;

    // Conformance window
    uint32_t crop_left, crop_right, crop_top, crop_bottom;

    // Derives
    uint32_t width_in_ctbs;
    uint32_t height_in_ctbs;
    uint32_t ctb_size;
};
```

### Validation oracle

Pour cette section, la validation se fait indirectement : si les dimensions, bit depth et chroma format sont correctement parses, le SPS dumping correspondra a libde265.

## Checklist

- [ ] ChromaFormat avec derivation SubWidthC/SubHeightC
- [ ] PictureFormat avec toutes les dimensions derivees
- [ ] Conformance window calculation
- [ ] Tables de scan Z-scan precalculees
- [ ] Tables de scan coefficients (diagonal, horizontal, vertical) pour 4x4->32x32
- [ ] Tests unitaires pour chaque derivation
