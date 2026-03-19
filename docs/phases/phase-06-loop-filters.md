# Phase 6 — Loop Filters (Deblocking + SAO)

## Objectif
Implémenter les filtres in-loop pour obtenir un décodeur Main profile complet.

## Prérequis
Phase 5 complétée (inter prediction fonctionnelle, sans filtres).

## Spec refs
- §8.7.2 : Deblocking filter
- §8.7.3 : Sample Adaptive Offset

## Tâches

### 6.1 — Deblocking : Pre-checks et Boundary Derivation
- [ ] Verification `slice_deblocking_filter_disabled_flag` : si actif, **aucun** filtrage pour ce slice
- [ ] Verification `pps_deblocking_filter_disabled_flag` : si actif et pas d'override slice, aucun filtrage
- [ ] Identifier les edges à filtrer (frontières CU/TU/PU)
- [ ] Alignement sur grille 8 pixels (luma)
- [ ] Exclusion des bords de picture/tile/slice
- [ ] `loop_filter_across_tiles_enabled_flag`
- [ ] `slice_loop_filter_across_slices_enabled_flag`

### 6.2 — Deblocking : Boundary Strength (§8.7.2.4)
- [ ] Bs = 2 si l'un des blocs est intra ou PCM (avec `pcm_loop_filter_disabled_flag == 0`)
- [ ] Bs = 2 si l'un des blocs a `cu_transquant_bypass_flag == 1` — pas de filtrage
- [ ] Bs = 1 si l'un des blocs a des coefficients de transform non-nuls (cbf != 0)
- [ ] Bs = 1 si les reference pictures different entre les deux blocs
- [ ] Bs = 0 — conditions detaillees pour **uni-prediction** :
  - Meme reference picture ET `|mvP - mvQ| < 4` en 1/4-pel (pour chaque composante x et y)
- [ ] Bs = 0 — conditions detaillees pour **bi-prediction** (combinatoire) :
  - Les deux blocs sont bi-pred
  - Les paires (refPic0, mv0) et (refPic1, mv1) matchent dans **l'un des deux ordres** :
    - Ordre direct : `refPicP_L0 == refPicQ_L0 && refPicP_L1 == refPicQ_L1 && |mvP_L0-mvQ_L0| < 4 && |mvP_L1-mvQ_L1| < 4`
    - Ordre croise : `refPicP_L0 == refPicQ_L1 && refPicP_L1 == refPicQ_L0 && |mvP_L0-mvQ_L1| < 4 && |mvP_L1-mvQ_L0| < 4`
  - Si **aucun** des deux ordres ne matche → Bs = 1
  - **Piege** : quand les deux ref pics L0 et L1 sont identiques, il faut tester les DEUX ordres et prendre Bs = 0 si l'un des deux matche
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
- [ ] SAO opère sur les samples **post-deblocking** :
  - Le deblocking est applique sur toute la picture d'abord
  - Puis le SAO est applique en utilisant les samples debloques
  - Pour les comparaisons EO aux bords de CTU, les samples du CTU voisin sont ceux **apres deblocking** (pas apres SAO du voisin)
- [ ] Gestion des bords de picture pour les comparaisons EO :
  - Les comparaisons qui sortent des limites de la picture ne sont **pas effectuees**
  - La categorie est fixee a 0 (pas d'offset) pour ces positions
- [ ] Gestion des bords de CTU/slice/tile pour les comparaisons EO :
  - Verifier `loop_filter_across_slices_enabled_flag` et `loop_filter_across_tiles_enabled_flag`
  - Si desactive, les samples de l'autre slice/tile ne sont pas utilises pour la comparaison

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
