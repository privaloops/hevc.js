# Phase 9 — Performance & Optimisation

## Objectif
Atteindre la lecture temps réel dans le navigateur. Cible : 1080p@30 minimum, 4K@60 en stretch goal.

## Prérequis
Phase 8 complétée (WASM fonctionnel). Phase 7 pour les profils 10-bit.

## Budget de temps par frame

| Résolution | FPS | Budget/frame |
|------------|-----|-------------|
| 720p@30 | 30 | 33ms |
| 1080p@30 | 30 | 33ms |
| 1080p@60 | 60 | 16.6ms |
| 4K@30 | 30 | 33ms |
| 4K@60 | 60 | 16.6ms |

Référence : un décodeur C optimisé (libde265, ffmpeg) décode du 1080p@30 en ~5ms sur desktop. WASM est ~2-3x plus lent, donc ~10-15ms — faisable.

## Tâches

### 9.1 — Profiling
- [ ] Profiler le build natif (perf, Instruments)
- [ ] Profiler le build WASM (Chrome DevTools Performance)
- [ ] Identifier les top 5 hotspots (probablement : interpolation, CABAC, transform, deblocking, SAO)
- [ ] Mesurer le ratio temps WASM/natif

### 9.2 — SIMD WASM
WASM supporte les instructions SIMD 128-bit (depuis 2021, supporté par tous les navigateurs modernes).

- [ ] Interpolation luma 8-tap : vectoriser le filtre horizontal/vertical
- [ ] Interpolation chroma 4-tap : idem
- [ ] Transform inverse : partial butterfly SIMD
- [ ] Deblocking : vectoriser le filtre sur les lignes de samples
- [ ] SAO : vectoriser les comparaisons edge offset
- [ ] Reconstruction : addition + clipping SIMD

```cpp
// Exemple : interpolation luma horizontale SIMD
#include <wasm_simd128.h>

void luma_filter_h_simd(const uint8_t* src, int16_t* dst, int width, int frac) {
    v128_t coeffs = wasm_i16x8_make(
        luma_filter[frac][0], luma_filter[frac][1],
        luma_filter[frac][2], luma_filter[frac][3],
        luma_filter[frac][4], luma_filter[frac][5],
        luma_filter[frac][6], luma_filter[frac][7]
    );
    // Traiter 8 pixels à la fois...
}
```

Compiler avec : `emcc -msimd128`

### 9.3 — Threading (Web Workers)
- [ ] Pipeline de décodage : Worker 1 parse CABAC, Worker 2 fait la reconstruction
- [ ] Parallélisme par tile : chaque tile décodée dans un Worker séparé
- [ ] WPP : parallélisme par ligne de CTU (dépendance diagonale)
- [ ] SharedArrayBuffer pour partager le DPB entre workers
- [ ] Nécessite les headers COOP/COEP :
  ```
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
  ```

### 9.4 — Optimisations algorithmiques
- [ ] Lookup tables pour CABAC (rangeTabLps pré-calculé en shifts)
- [ ] Fast path pour les CU skip (pas de résidu = pas de transform)
- [ ] Fast path pour QP constant (scaling simplifié)
- [ ] Cache-friendly memory layout pour les pictures
- [ ] Éviter les divisions (remplacer par shifts)

### 9.5 — Memory
- [ ] Pool allocator pour les CU/PU/TU (éviter malloc par bloc)
- [ ] Réutiliser les buffers de frame (ring buffer dans le DPB)
- [ ] Minimiser les copies (zero-copy entre CABAC et reconstruction)
- [ ] Alignement mémoire pour SIMD (16-byte aligned)

### 9.6 — Benchmarks
- [ ] Suite de benchmarks reproductibles :
  - 720p I-only @QP22
  - 1080p IPB @QP22
  - 4K IPB @QP22
- [ ] Mesure : FPS, ms/frame, mémoire peak
- [ ] Tracking CI : alerter si régression > 10%
- [ ] Comparer avec libde265 WASM (si disponible) et ffmpeg WASM

## Critère de sortie

| Cible | Critère | Statut |
|-------|---------|--------|
| Minimum | 720p@30 temps réel dans Chrome | Requis |
| Objectif | 1080p@30 temps réel dans Chrome | Souhaité |
| Stretch | 1080p@60 temps réel | Nice to have |
| Stretch++ | 4K@30 temps réel | Ambitieux |
| Stretch+++ | 4K@60 temps réel | Très ambitieux |

## Estimation de complexité
Élevée. L'optimisation SIMD est technique et fastidieuse. Le threading WASM ajoute de la complexité (synchronisation, headers COOP/COEP).
