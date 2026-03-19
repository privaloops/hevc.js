# Phase 3 — Parameter Sets & Slice Header Parsing

## Objectif
Parser les VPS, SPS, PPS et slice headers complets, avec toutes les valeurs dérivées.

## Prérequis
Phase 2 complétée (NAL parsing, BitstreamReader avec ue/se).

## Spec refs
- §7.3.2 : VPS syntax
- §7.3.3 : SPS syntax (+ profile_tier_level, scaling_list, short_term_ref_pic_set)
- §7.3.4 : PPS syntax
- §7.3.6 : Slice segment header
- §7.4.2-7 : Semantics correspondantes
- Annexe A : Profiles, tiers, levels

## Tâches

### 3.1 — Profile/Tier/Level
- [ ] Parsing de `profile_tier_level()`
- [ ] Extraction du profil, tier, level
- [ ] Validation des contraintes de profil

### 3.2 — VPS
- [ ] Parsing complet de `video_parameter_set_rbsp()`
- [ ] Stockage par `vps_video_parameter_set_id`
- [ ] Sub-layer ordering info
- [ ] Timing info (optionnel)

### 3.3 — SPS
- [ ] Parsing complet de `seq_parameter_set_rbsp()`
- [ ] Chroma format + separate_colour_plane_flag
- [ ] Dimensions + conformance window
- [ ] Bit depth luma/chroma
- [ ] Quad-tree config (MinCb, Ctb, MinTb, MaxTb sizes)
- [ ] Toutes les valeurs dérivées (§7.4.3.2.1)
- [ ] Scaling list data (peut être stubbed avec flat 16 d'abord)
- [ ] Short-term reference picture sets (§7.3.7)
- [ ] Long-term reference pics config
- [ ] VUI parameters (§E.2, peut être stubbed)
- [ ] Tests : chaque valeur dérivée vérifiée

### 3.4 — PPS
- [ ] Parsing complet de `pic_parameter_set_rbsp()`
- [ ] Tiles layout (column widths, row heights)
- [ ] Tables de conversion CtbAddr (raster<->tile scan)
- [ ] Deblocking filter control
- [ ] Weighted prediction flags
- [ ] QP init + offsets
- [ ] Tests : tiles layout correct

### 3.5 — Slice Header
- [ ] Parsing complet de `slice_segment_header()`
- [ ] Gestion de tous les champs conditionnels (dépend du SPS/PPS actif)
- [ ] Slice type (I/P/B)
- [ ] POC (slice_pic_order_cnt_lsb)
- [ ] Short-term RPS selection/parsing
- [ ] Long-term ref pics
- [ ] Reference picture list modification
- [ ] Pred weight table
- [ ] SAO flags
- [ ] Deblocking overrides
- [ ] QP delta
- [ ] Byte alignment
- [ ] Dependent slice segments

### 3.6 — Parameter Set Management
- [ ] Stockage des VPS/SPS/PPS par ID
- [ ] Activation du SPS/PPS courant via le slice header
- [ ] Gestion de la mise à jour (nouveau SPS/PPS remplace l'ancien)

### 3.7 — CLI de test amélioré
- [ ] `hevc-torture --dump-headers input.265`
- [ ] Dump complet de tous les champs parsés
- [ ] Format aligné avec `dec265 --dump-headers`

## Critère de sortie

- Tous les champs des VPS, SPS, PPS, slice headers parsés correctement
- Comparaison champ-par-champ avec libde265 sur au moins 10 bitstreams variés
- Toutes les valeurs dérivées calculées correctement

## Validation oracle
```bash
# Comparer tous les champs
./hevc-torture --dump-headers test.265 > our_headers.txt
dec265 test.265 --dump-headers > ref_headers.txt
diff our_headers.txt ref_headers.txt
```

## Estimation de complexité
Élevée. Le SPS et le slice header sont très denses. Le short-term RPS est la partie la plus complexe.
