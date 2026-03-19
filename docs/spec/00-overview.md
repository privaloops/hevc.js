# Spec Overview — ITU-T H.265 (V8, 2021)

## Structure de la spec

La spec H.265 est organisee en chapitres majeurs. Seuls les chapitres d'implementation sont couverts ici.

| Chapitre | Contenu | Priorite | Phase |
|----------|---------|----------|-------|
| 6 | Source, coded, decoded and output data formats | Haute | 1 |
| 7 | Syntax and semantics | Critique | 2-3 |
| 8 | Decoding process | Critique | 4-6 |
| 9 | Parsing process (CABAC) | Critique | 4 |
| Annexe A | Profiles, tiers, levels | Haute | 3, 7 |
| Annexe B | Byte stream format | Haute | 2 |
| Annexe C | HRD (Hypothetical Reference Decoder) | Basse | - |
| Annexe D | SEI messages | Basse | - |

## Concepts fondamentaux

### Hierarchie de blocs

```
CVS (Coded Video Sequence)
└── AU (Access Unit) = 1 frame
    └── NAL Unit
        └── Slice Segment
            └── CTU (Coding Tree Unit) — taille max 64x64
                └── CU (Coding Unit) — quad-tree split
                    ├── PU (Prediction Unit) — prediction
                    └── TU (Transform Unit) — transform quad-tree
```

### Types de NAL Units importants

| nal_unit_type | Nom | Description |
|---------------|-----|-------------|
| 0-1 | TRAIL_N/R | Trailing picture (P/B slice) |
| 2-3 | TSA_N/R | Temporal sub-layer access |
| 4-5 | STSA_N/R | Step-wise temporal sub-layer |
| 6-7 | RADL_N/R | Random access decodable leading |
| 8-9 | RASL_N/R | Random access skipped leading |
| 16-18 | BLA_W_LP/W_RADL/N_LP | Broken link access |
| 19-20 | IDR_W_RADL/N_LP | Instantaneous decoding refresh |
| 21 | CRA_NUT | Clean random access |
| 32 | VPS_NUT | Video Parameter Set |
| 33 | SPS_NUT | Sequence Parameter Set |
| 34 | PPS_NUT | Picture Parameter Set |
| 35 | AUD_NUT | Access Unit Delimiter |
| 36 | EOS_NUT | End of Sequence |
| 37 | EOB_NUT | End of Bitstream |
| 39-40 | PREFIX/SUFFIX_SEI | SEI messages |

### Picture Order Count (POC)

Le POC est le mecanisme central d'ordonnancement des frames. Il est derive dans le slice header (§8.3.1) :

```
PicOrderCntVal = PicOrderCntMsb + slice_pic_order_cnt_lsb
```

Le MSB est derive par rapport a la frame precedente, avec des regles de wrap-around.

### Decoded Picture Buffer (DPB)

Le DPB stocke les frames de reference et les frames en attente d'output. Sa taille est definie par le level (Annexe A). Le processus de gestion est decrit en §C.5.

### Processus de decodage global

```
1. Parse NAL unit header
2. Si VPS/SPS/PPS : parser et stocker
3. Si slice :
   a. Parser slice header
   b. Pour chaque CTU dans le slice :
      i.   Parser coding_tree_unit (CABAC)
      ii.  Pour chaque CU :
           - Si PCM : reconstruction directe (bypass prediction/transform), reset CABAC
           - Si intra : derive intra prediction (35 modes luma, 5 modes chroma)
           - Si inter : derive motion vectors (merge/AMVP), interpolation (8-tap luma, 4-tap chroma)
           - Si skip : merge mode, pas de résidu
      iii. Transform inverse + dequant
      iv.  Reconstruction = prediction + residual
   c. Loop filters (deblocking + SAO) sur la picture complete
   d. Stocker dans le DPB
   e. Output si conditions remplies
```

## Mapping spec → code

| Section spec | Module C++ | Fichier |
|-------------|-----------|---------|
| Annexe B | Bitstream | src/bitstream/byte_stream.cpp |
| §7.3.1 | NAL Parser | src/bitstream/nal_unit.cpp |
| §7.3.2 | VPS | src/syntax/vps.cpp |
| §7.3.3 | SPS | src/syntax/sps.cpp |
| §7.3.4 | PPS | src/syntax/pps.cpp |
| §7.3.6 | Slice Header | src/syntax/slice_header.cpp |
| §7.3.8 | CTU | src/syntax/coding_tree.cpp |
| §8.3 | Ref Pic Mgmt | src/decoding/ref_pictures.cpp |
| §8.5.3 | Inter Pred | src/decoding/inter_prediction.cpp |
| §8.5.4 | Intra Pred | src/decoding/intra_prediction.cpp |
| §8.6 | Transform | src/decoding/transform.cpp |
| §8.7.2 | Deblocking | src/filters/deblocking.cpp |
| §8.7.3 | SAO | src/filters/sao.cpp |
| §9 | CABAC | src/bitstream/cabac.cpp |
| §7.3.10.2 | PCM Samples | src/syntax/pcm.cpp |
| §8.1 | General Decoding | src/decoding/decoder.cpp |
| §8.3.1 | POC Derivation | src/decoding/ref_pictures.cpp |
| §C.5 | DPB Output/Bumping | src/common/dpb.cpp |
| Annexe A | Profile/Level | src/syntax/profile_tier_level.cpp |
