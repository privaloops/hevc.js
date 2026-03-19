# 8.3 — Reference Picture Management

Spec ref : 8.3.1 (POC derivation), 8.3.2 (RPS derivation), 8.3.3 (ref pic list construction), 8.3.4 (DPB marking)

## 8.3.1 — Derivation du Picture Order Count

Le POC est l'identifiant d'ordre d'affichage de chaque frame.

### Pseudo-code spec

```
// Pour chaque picture avec un slice header :
if (IRAP picture && NoRaslOutputFlag)
    prevPicOrderCntLsb = 0
    prevPicOrderCntMsb = 0
else
    // Utiliser les valeurs du slice precedent qui n'est pas RASL/RADL/sub-layer-non-ref

// MSB derivation (wrap-around detection)
if (slice_pic_order_cnt_lsb < prevPicOrderCntLsb &&
    prevPicOrderCntLsb - slice_pic_order_cnt_lsb >= MaxPicOrderCntLsb / 2)
    PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb
else if (slice_pic_order_cnt_lsb > prevPicOrderCntLsb &&
         slice_pic_order_cnt_lsb - prevPicOrderCntLsb > MaxPicOrderCntLsb / 2)
    PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb
else
    PicOrderCntMsb = prevPicOrderCntMsb

PicOrderCntVal = PicOrderCntMsb + slice_pic_order_cnt_lsb
```

### Traduction C++

```cpp
int32_t derive_poc(const SliceHeader& sh, const SPS& sps, DecoderState& state) {
    int32_t max_poc_lsb = 1 << (sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
    int32_t poc_msb;

    if (is_irap_nal(sh.nal_unit_type) && state.no_rasl_output_flag) {
        poc_msb = 0;
        state.prev_poc_lsb = 0;
        state.prev_poc_msb = 0;
    } else {
        int32_t prev_lsb = state.prev_poc_lsb;
        int32_t prev_msb = state.prev_poc_msb;
        int32_t cur_lsb = sh.slice_pic_order_cnt_lsb;

        if (cur_lsb < prev_lsb && (prev_lsb - cur_lsb) >= max_poc_lsb / 2)
            poc_msb = prev_msb + max_poc_lsb;
        else if (cur_lsb > prev_lsb && (cur_lsb - prev_lsb) > max_poc_lsb / 2)
            poc_msb = prev_msb - max_poc_lsb;
        else
            poc_msb = prev_msb;
    }

    return poc_msb + sh.slice_pic_order_cnt_lsb;
}
```

## 8.3.2 — Derivation du Reference Picture Set

Le RPS determine quelles frames sont des references (short-term/long-term) et quelles frames doivent etre supprimees du DPB.

### Listes derivees

```
Apres derivation du RPS pour la picture courante :

RefPicSetStCurrBefore[] — ref pics ST avant la picture courante (POC < current)
RefPicSetStCurrAfter[]  — ref pics ST apres la picture courante (POC > current)
RefPicSetStFoll[]       — ref pics ST non utilisees par la picture courante
RefPicSetLtCurr[]       — ref pics LT utilisees
RefPicSetLtFoll[]       — ref pics LT non utilisees

NumPocStCurrBefore, NumPocStCurrAfter, NumPocStFoll
NumPocLtCurr, NumPocLtFoll
```

### Processus

```cpp
void derive_reference_picture_set(const SliceHeader& sh, const SPS& sps, DPB& dpb) {
    // 1. Construire les listes de POC a partir du Short-Term RPS actif
    auto& rps = sh.short_term_ref_pic_set_sps_flag
                ? sps.st_rps[sh.short_term_ref_pic_set_idx]
                : sh.st_rps;

    // 2. Pour chaque entree dans DeltaPocS0 (negatifs) :
    //    POC = PicOrderCntVal + DeltaPocS0[i]
    //    Si UsedByCurrPicS0[i] -> StCurrBefore, sinon -> StFoll

    // 3. Pour chaque entree dans DeltaPocS1 (positifs) :
    //    POC = PicOrderCntVal + DeltaPocS1[i]
    //    Si UsedByCurrPicS1[i] -> StCurrAfter, sinon -> StFoll

    // 4. Long-term ref pics (si presentes dans le slice header)

    // 5. Marquer les pictures dans le DPB comme "used for reference" ou "unused"
    //    Les pictures qui ne sont dans aucune liste -> marquer "unused for reference"
}
```

## 8.3.3 — Construction des listes de reference

```
// Liste L0 (toujours construite pour P et B slices)
RefPicList0 = RefPicSetStCurrBefore ++ RefPicSetStCurrAfter ++ RefPicSetLtCurr
// Repeter cycliquement jusqu'a NumRefIdxL0Active entrees

// Liste L1 (B slices seulement)
RefPicList1 = RefPicSetStCurrAfter ++ RefPicSetStCurrBefore ++ RefPicSetLtCurr
// Repeter cycliquement jusqu'a NumRefIdxL1Active entrees

// Puis appliquer ref_pic_list_modification si present
```

## 8.3.4 — DPB (Decoded Picture Buffer)

```cpp
struct DecodedPictureBuffer {
    struct Entry {
        int32_t poc;
        bool used_for_reference;
        bool needed_for_output;
        std::shared_ptr<Picture> picture;
    };

    std::vector<Entry> entries;
    size_t max_size;  // derive du level (Annexe A)

    // C.5.2.2 — Picture output process
    void bump(int32_t current_poc);

    // C.5.2.3 — Removal process
    void remove_unused();

    // Ajouter une picture decodee
    void add(int32_t poc, std::shared_ptr<Picture> pic, bool output_flag);
};
```

## Pieges connus

1. **POC wrap-around** : Le MSB wrap est source de nombreux bugs. Tester avec des sequences longues.
2. **IRAP handling** : IDR reset le POC a 0. CRA ne le fait pas toujours (depend de NoRaslOutputFlag).
3. **DPB bumping** : L'output order n'est pas le decode order. Le bumping doit respecter l'ordre POC.
4. **Temporal sub-layers** : Les pictures de sub-layers superieurs ne sont pas des references pour les sub-layers inferieurs.

## Checklist

- [ ] POC derivation avec wrap-around correct
- [ ] Short-term RPS derivation (from SPS et from slice header)
- [ ] Long-term ref pic derivation
- [ ] RefPicList0/List1 construction avec modification
- [ ] DPB management (add, bump, remove)
- [ ] DPB sizing selon le level
- [ ] Tests : sequence avec reordonnancement B-frames, verifier l'ordre POC
