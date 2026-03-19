# Phase 6 — Loop Filters (Deblocking + SAO)

## Objectif
Implémenter les filtres in-loop pour obtenir un décodeur Main profile complet.

## Prérequis
Phase 5 complétée (inter prediction fonctionnelle, sans filtres).

## Spec refs
- §8.7.2 : Deblocking filter
- §8.7.3 : Sample Adaptive Offset

## Tâches

### 6.1 — Deblocking : Boundary Derivation
- [ ] Identifier les edges à filtrer (frontières CU/TU/PU)
- [ ] Alignement sur grille 8 pixels (luma)
- [ ] Exclusion des bords de picture/tile/slice
- [ ] `loop_filter_across_tiles_enabled_flag`
- [ ] `slice_loop_filter_across_slices_enabled_flag`

### 6.2 — Deblocking : Boundary Strength
- [ ] Bs = 2 si intra
- [ ] Bs = 1 si coefficients non-nuls ou MV/refPic différents
- [ ] Bs = 0 sinon
- [ ] Stockage de Bs pour chaque edge

### 6.3 — Deblocking : beta et tC
- [ ] Tables beta et tC (Tables 8-16, 8-17)
- [ ] QP moyen entre les deux côtés de l'edge
- [ ] slice_beta_offset_div2, slice_tc_offset_div2

### 6.4 — Deblocking : Luma Filtering
- [ ] Decision de filtrage (dp, dq, beta threshold)
- [ ] Strong filter (p0, p1, p2, q0, q1, q2)
- [ ] Weak filter (p0, q0 + optionnel p1, q1)
- [ ] Clipping correct

### 6.5 — Deblocking : Chroma Filtering
- [ ] Filtrage seulement si Bs == 2
- [ ] Filtre simple (p0, q0 modifiés)

### 6.6 — Deblocking : Ordre
- [ ] Toutes les edges verticales d'abord (toute la picture)
- [ ] Puis toutes les edges horizontales
- [ ] Vérifier que l'ordre est correct (source commune de bugs)

### 6.7 — SAO : Edge Offset
- [ ] 4 classes EO (H, V, diag135, diag45)
- [ ] Catégorisation (valley, concave, flat, convex, peak)
- [ ] Application des offsets (signes fixes par catégorie)

### 6.8 — SAO : Band Offset
- [ ] 32 bandes, 4 offsets consécutifs
- [ ] band_position
- [ ] Application des offsets

### 6.9 — SAO : Merge
- [ ] sao_merge_left_flag
- [ ] sao_merge_up_flag
- [ ] Propagation des paramètres

### 6.10 — Intégration
- [ ] Ordre correct : reconstruction -> deblocking -> SAO
- [ ] SAO opère sur les samples post-deblocking
- [ ] Gestion des bords de CTU pour les comparaisons EO

## Critère de sortie

**C'est le jalon majeur du projet** : décodeur Main profile complet.

- Pixel-perfect vs libde265 sur toute la suite de conformité Main profile
- Tests avec deblocking seul, SAO seul, les deux

## Validation oracle

### Étape 1 : Deblocking seul
```bash
x265 --input raw.yuv --input-res 1920x1080 --fps 30 --frames 30 \
     --preset medium --qp 22 --no-sao -o test_deblock.265
```

### Étape 2 : SAO seul
```bash
x265 --input raw.yuv --input-res 1920x1080 --fps 30 --frames 30 \
     --preset medium --qp 22 --no-deblock -o test_sao.265
```

### Étape 3 : Full Main profile
```bash
x265 --input raw.yuv --input-res 1920x1080 --fps 30 --frames 30 \
     --preset medium --qp 22 -o test_full.265

# + bitstreams de conformité officiels
```

## Estimation de complexité
Modérée à élevée. Le deblocking est la partie la plus délicate (edge cases aux bords). SAO est plus simple algorithmiquement.
