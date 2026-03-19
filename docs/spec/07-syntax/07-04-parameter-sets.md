# §7.3.2-4 — Parameter Sets (VPS, SPS, PPS)

Spec ref : §7.3.2 (VPS), §7.3.3 (SPS), §7.3.4 (PPS), §7.4.3 (semantics)

## Vue d'ensemble

```
VPS (Video Parameter Set)
 └── SPS (Sequence Parameter Set) — réf un VPS
      └── PPS (Picture Parameter Set) — réf un SPS
           └── Slice Header — réf un PPS
```

Chaque niveau référence le précédent par ID. Plusieurs SPS/PPS peuvent coexister.

## VPS — §7.3.2.1

### Pseudo-code spec (simplifié)

```
video_parameter_set_rbsp() {
    vps_video_parameter_set_id    u(4)
    vps_base_layer_internal_flag  u(1)
    vps_base_layer_available_flag u(1)
    vps_max_layers_minus1         u(6)
    vps_max_sub_layers_minus1     u(3)
    vps_temporal_id_nesting_flag  u(1)
    vps_reserved_0xffff_16bits    u(16)
    profile_tier_level(1, vps_max_sub_layers_minus1)
    vps_sub_layer_ordering_info_present_flag    u(1)
    for(i = (vps_sub_layer_ordering_info_present_flag ? 0 : vps_max_sub_layers_minus1);
        i <= vps_max_sub_layers_minus1; i++) {
        vps_max_dec_pic_buffering_minus1[i]    ue(v)
        vps_max_num_reorder_pics[i]            ue(v)
        vps_max_latency_increase_plus1[i]      ue(v)
    }
    vps_max_layer_id                           u(6)
    vps_num_layer_sets_minus1                   ue(v)
    // ... layer sets, timing info (omis pour single-layer)
}
```

### Struct C++

```cpp
struct VPS {
    uint8_t vps_id;                     // 0-15
    uint8_t max_sub_layers_minus1;      // 0-6
    bool temporal_id_nesting_flag;
    ProfileTierLevel profile_tier_level;

    struct SubLayerOrdering {
        uint32_t max_dec_pic_buffering_minus1;
        uint32_t max_num_reorder_pics;
        uint32_t max_latency_increase_plus1;
    };
    std::array<SubLayerOrdering, 7> sub_layer_ordering;

    // Timing info (optionnel)
    bool timing_info_present;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
};
```

## SPS — §7.3.3.1

Le SPS est la structure la plus dense. Il définit les dimensions, le profil, le quad-tree, les tools activés, etc.

### Pseudo-code spec (éléments critiques)

```
seq_parameter_set_rbsp() {
    sps_video_parameter_set_id             u(4)
    sps_max_sub_layers_minus1              u(3)
    sps_temporal_id_nesting_flag           u(1)
    profile_tier_level(1, sps_max_sub_layers_minus1)
    sps_seq_parameter_set_id               ue(v)
    chroma_format_idc                      ue(v)
    if(chroma_format_idc == 3)
        separate_colour_plane_flag         u(1)
    pic_width_in_luma_samples              ue(v)
    pic_height_in_luma_samples             ue(v)
    conformance_window_flag                u(1)
    if(conformance_window_flag) {
        conf_win_left_offset               ue(v)
        conf_win_right_offset              ue(v)
        conf_win_top_offset                ue(v)
        conf_win_bottom_offset             ue(v)
    }
    bit_depth_luma_minus8                  ue(v)
    bit_depth_chroma_minus8                ue(v)
    log2_max_pic_order_cnt_lsb_minus4      ue(v)
    // Sub-layer ordering (même structure que VPS)
    sps_sub_layer_ordering_info_present_flag    u(1)
    // ...
    log2_min_luma_coding_block_size_minus3      ue(v)
    log2_diff_max_min_luma_coding_block_size    ue(v)
    log2_min_luma_transform_block_size_minus2   ue(v)
    log2_diff_max_min_luma_transform_block_size ue(v)
    max_transform_hierarchy_depth_inter         ue(v)
    max_transform_hierarchy_depth_intra         ue(v)
    scaling_list_enabled_flag                   u(1)
    // ... scaling lists
    amp_enabled_flag                            u(1)
    sample_adaptive_offset_enabled_flag         u(1)
    pcm_enabled_flag                            u(1)
    // ... PCM params
    num_short_term_ref_pic_sets                 ue(v)
    // ... short term RPS
    long_term_ref_pics_present_flag             u(1)
    // ... long term RPS
    sps_temporal_mvp_enabled_flag               u(1)
    strong_intra_smoothing_enabled_flag         u(1)
    vui_parameters_present_flag                 u(1)
    // ... VUI
}
```

### Valeurs dérivées critiques (§7.4.3.2.1)

```cpp
// Tailles de blocs
MinCbLog2SizeY = log2_min_luma_coding_block_size_minus3 + 3
CtbLog2SizeY = MinCbLog2SizeY + log2_diff_max_min_luma_coding_block_size
MinCbSizeY = 1 << MinCbLog2SizeY      // Min CU size (8 typiquement)
CtbSizeY = 1 << CtbLog2SizeY          // CTU size (64 typiquement)

MinTbLog2SizeY = log2_min_luma_transform_block_size_minus2 + 2
MaxTbLog2SizeY = MinTbLog2SizeY + log2_diff_max_min_luma_transform_block_size
MinTbSizeY = 1 << MinTbLog2SizeY      // Min TU size (4)
MaxTbSizeY = 1 << MaxTbLog2SizeY      // Max TU size (32)

// Dimensions en unités de CTB
PicWidthInCtbsY = Ceil(pic_width / CtbSizeY)
PicHeightInCtbsY = Ceil(pic_height / CtbSizeY)
PicSizeInCtbsY = PicWidthInCtbsY * PicHeightInCtbsY

// Dimensions en unités de min CB
PicWidthInMinCbsY = pic_width / MinCbSizeY
PicHeightInMinCbsY = pic_height / MinCbSizeY

// Bit depth
BitDepthY = 8 + bit_depth_luma_minus8
BitDepthC = 8 + bit_depth_chroma_minus8
QpBdOffsetY = 6 * bit_depth_luma_minus8
QpBdOffsetC = 6 * bit_depth_chroma_minus8

// POC
MaxPicOrderCntLsb = 1 << (log2_max_pic_order_cnt_lsb_minus4 + 4)
```

## PPS — §7.3.4.1

### Pseudo-code spec (éléments critiques)

```
pic_parameter_set_rbsp() {
    pps_pic_parameter_set_id               ue(v)
    pps_seq_parameter_set_id               ue(v)
    dependent_slice_segments_enabled_flag   u(1)
    output_flag_present_flag               u(1)
    num_extra_slice_header_bits            u(3)
    sign_data_hiding_enabled_flag          u(1)
    cabac_init_present_flag                u(1)
    num_ref_idx_l0_default_active_minus1   ue(v)
    num_ref_idx_l1_default_active_minus1   ue(v)
    init_qp_minus26                        se(v)
    constrained_intra_pred_flag            u(1)
    transform_skip_enabled_flag            u(1)
    cu_qp_delta_enabled_flag               u(1)
    if(cu_qp_delta_enabled_flag)
        diff_cu_qp_delta_depth             ue(v)
    pps_cb_qp_offset                       se(v)
    pps_cr_qp_offset                       se(v)
    pps_slice_chroma_qp_offsets_present_flag  u(1)
    weighted_pred_flag                     u(1)
    weighted_bipred_flag                   u(1)
    transquant_bypass_enabled_flag         u(1)
    tiles_enabled_flag                     u(1)
    entropy_coding_sync_enabled_flag       u(1)
    if(tiles_enabled_flag) {
        num_tile_columns_minus1            ue(v)
        num_tile_rows_minus1               ue(v)
        uniform_spacing_flag               u(1)
        // ... column/row widths si non uniforme
        loop_filter_across_tiles_enabled_flag  u(1)
    }
    // ... deblocking control, scaling list, lists modification, etc.
}
```

### Valeurs dérivées critiques (§7.4.4.1)

```cpp
// QP initial
SliceQpY = 26 + init_qp_minus26  // QP par défaut pour la picture

// CU QP delta
Log2MinCuQpDeltaSize = CtbLog2SizeY - diff_cu_qp_delta_depth

// Tiles
if (tiles_enabled_flag) {
    // Calculer colWidthInCtbsY[] et rowHeightInCtbsY[]
    // Calculer CtbAddrRsToTs[] et CtbAddrTsToRs[] (raster↔tile scan)
    // Calculer TileId[] pour chaque CTB
}
```

## Profile/Tier/Level — §7.3.3

```
profile_tier_level(profilePresentFlag, maxNumSubLayersMinus1) {
    if(profilePresentFlag) {
        general_profile_space    u(2)
        general_tier_flag        u(1)
        general_profile_idc      u(5)
        general_profile_compatibility_flag[0..31]    u(1) x32
        // ... constraint flags
    }
    general_level_idc            u(8)
    // ... sub-layer profile/level
}
```

### Profils supportés

| profile_idc | Profil | Description |
|-------------|--------|-------------|
| 1 | Main | 8-bit 4:2:0 |
| 2 | Main 10 | 10-bit 4:2:0 |
| 3 | Main Still Picture | Image fixe |
| 4 | Format Range Ext | 4:2:2, 4:4:4 |

### Levels pour 4K

| Level | MaxLumaPs | MaxDpbSize | MaxBR (Main) | Résolution type |
|-------|-----------|------------|--------------|-----------------|
| 4.0 | 2,228,224 | 6 | 12 Mbps | 1080p@30 |
| 4.1 | 2,228,224 | 6 | 20 Mbps | 1080p@60 |
| 5.0 | 8,912,896 | 6 | 25 Mbps | 4K@30 |
| 5.1 | 8,912,896 | 6 | 40 Mbps | 4K@60 |

## Checklist

- [ ] ProfileTierLevel parsing complet
- [ ] VPS parsing + stockage par ID
- [ ] SPS parsing + toutes les valeurs dérivées
- [ ] PPS parsing + tiles layout calculation
- [ ] Scaling list parsing (§7.3.4, peut être différé)
- [ ] Short-term RPS parsing dans SPS (§7.3.7)
- [ ] VUI parameters parsing (§E.2, peut être différé)
- [ ] Tests : dump tous les champs, comparer avec libde265
