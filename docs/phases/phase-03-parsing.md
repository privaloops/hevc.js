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
- [ ] Scaling list data avec mecanisme de fallback complet (§7.4.5) :
  - Si `scaling_list_enabled_flag == 0` : facteurs flat = 16 pour toutes les tailles
  - Si `scaling_list_enabled_flag == 1` et `sps_scaling_list_data_present_flag == 0` : utiliser les matrices par defaut (Tables 7-3, 7-4, 7-5 — **non flat** pour 8x8, 16x16, 32x32)
  - Si `scaling_list_pred_mode_flag == 0` : copier depuis une autre matrice via `scaling_list_pred_matrix_id_delta`
  - Les matrices par defaut sont differentes pour intra et inter
- [ ] Short-term reference picture sets (§7.3.7)
- [ ] Long-term reference pics config :
  - `num_long_term_ref_pics_sps` et `lt_ref_pic_poc_lsb_sps[i]`
  - `used_by_curr_pic_lt_sps_flag[i]`
  - Ces valeurs sont referencees dans le slice header par index
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
- [ ] Long-term ref pics (parsing complet) :
  - `num_long_term_sps` : nombre de LT refs provenant du SPS
  - `num_long_term_pics` : nombre de LT refs specifiees directement
  - `lt_idx_sps[i]` : index dans la table SPS
  - `poc_lsb_lt[i]` : POC LSB pour les LT specifiees dans le slice
  - `used_by_curr_pic_lt_flag[i]`
  - `delta_poc_msb_present_flag[i]` et `delta_poc_msb_cycle_lt[i]` : pour disambiguer les LT dont le POC LSB wrappe
- [ ] Reference picture list modification
- [ ] Pred weight table (`pred_weight_table()`) :
  - `luma_log2_weight_denom` (ue)
  - `delta_chroma_log2_weight_denom` (se)
  - Pour chaque ref dans L0/L1 : `luma_weight_l0_flag[i]`, `chroma_weight_l0_flag[i]`
  - Si flag=1 : `delta_luma_weight_l0[i]` (se), `luma_offset_l0[i]` (se)
  - Si flag=0 : weight = `1 << luma_log2_weight_denom`, offset = 0 (valeurs par defaut)
  - Chroma : `delta_chroma_weight_l0[i][j]`, `delta_chroma_offset_l0[i][j]`
- [ ] SAO flags
- [ ] Deblocking overrides :
  - `slice_deblocking_filter_disabled_flag`
  - `slice_beta_offset_div2`, `slice_tc_offset_div2`
- [ ] QP delta
- [ ] Entry point offsets (requis meme en Phase 3 pour parser le slice header complet) :
  - `num_entry_point_offsets` (ue) — vaut 0 si pas de tiles ni WPP
  - Si > 0 : `offset_len_minus1` (ue) et `entry_point_offset_minus1[i]` (u(v))
  - Le parsing est necessaire pour atteindre le byte alignment correctement
- [ ] Byte alignment
- [ ] Dependent slice segments

### 3.6 — Parameter Set Management
- [ ] Stockage des VPS/SPS/PPS par ID
- [ ] Activation du SPS/PPS courant via le slice header
- [ ] Gestion de la mise à jour (nouveau SPS/PPS remplace l'ancien)

### 3.7 — CLI de test amélioré
- [ ] `hevc-decode --dump-headers input.265`
- [ ] Dump complet de tous les champs parsés
- [ ] Format tabulaire : un champ par ligne, nom = valeur

## Critère de sortie

- Tous les champs des VPS, SPS, PPS, slice headers parsés correctement
- Comparaison avec ffmpeg : décoder un bitstream de test et vérifier que les dimensions, chroma format, bit depth, POC, slice type matchent
- Toutes les valeurs dérivées calculées correctement

## Validation oracle
```bash
# Vérifier les dimensions/format parsés vs ffprobe
ffprobe -v quiet -show_streams tests/conformance/fixtures/i_64x64_qp22.265
./hevc-decode --dump-headers tests/conformance/fixtures/i_64x64_qp22.265
# Comparer width, height, chroma, bit_depth, num_frames
```

## Estimation de complexité
Élevée. Le SPS et le slice header sont très denses. Le short-term RPS est la partie la plus complexe.
