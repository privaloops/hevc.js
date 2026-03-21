# 5.2 -- RPS + Reference Picture Lists

## Objectif

Verifier que les Reference Picture Sets (RPS) sont correctement derives et que
RefPicList0/RefPicList1 sont construites comme attendu pour chaque slice.

## Spec refs

- S8.3.2 : RPS derivation (short-term, long-term, delta_rps, inter-prediction)
- S8.3.3 : Reference picture list construction
- S8.3.4 : Decoding process for reference picture marking
- S7.3.6.2 : ref_pic_lists_modification

## Code existant

`src/decoding/dpb.cpp` : `derive_rps()`, `construct_ref_pic_lists()`

## Points critiques a verifier

### RPS derivation (S8.3.2)
- st_ref_pic_set parsing avec inter_ref_pic_set_prediction_flag
- Calcul DeltaPocS0/S1 et UsedByCurrPicS0/S1
- Long-term refs (poc_lsb_lt, delta_poc_msb_present)

### RefPicList construction (S8.3.3)
- P-slice : RefPicList0 = RefPicListTemp0 (sauf si modification)
- B-slice : RefPicList0 et RefPicList1 construits separement
- Ordre dans RefPicListTemp : RefPicSetStCurrBefore, StCurrAfter, LtCurr
- `ref_pic_list_modification` override des positions

### Reference picture marking (S8.3.4)
- "no reference picture" pour les indices manquants
- Handling des lost pictures

## Audit a faire

1. Lire S8.3.2-8.3.4 du PDF (pages 141-145)
2. Ajouter un log RefPicList0/1 par slice : `[ref_idx] POC=X`
3. Comparer avec HM qui loggue aussi les ref lists

## Test de validation

```cpp
// Test unitaire :
// - Sequence P-only (10 frames) : RefPicList0 = [poc-1, poc-2, ...]
// - Sequence B hierarchique : verifier RefPicList0 et 1 pour chaque type de frame
// - ref_pic_list_modification active
```

Comparaison automatisee :
```bash
# Notre decodeur loggue les ref lists
HEVC_DEBUG=DPB ./build/hevc-decode tests/conformance/fixtures/p_qcif_10f.265 -o /dev/null 2>&1 | grep "RefPicList"

# HM loggue aussi les ref lists (a verifier)
```

## Critere de sortie

- [ ] RefPicList0 identique a HM pour chaque P-slice de `p_qcif_10f.265`
- [ ] RefPicList0 et RefPicList1 identiques a HM pour chaque B-slice de `b_qcif_10f.265`
- [ ] Test unitaire avec ref_pic_list_modification
