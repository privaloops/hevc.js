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

## NoRaslOutputFlag derivation (§8.1)

Le `NoRaslOutputFlag` controle le comportement aux points d'acces aleatoire. Sa derivation est critique pour le POC et le DPB.

```cpp
bool derive_no_rasl_output_flag(uint8_t nal_unit_type, bool first_picture_in_bitstream,
                                 bool HandleCraAsBlaFlag) {
    if (is_idr_nal(nal_unit_type) || is_bla_nal(nal_unit_type))
        return true;   // Toujours true pour IDR et BLA

    if (is_cra_nal(nal_unit_type)) {
        if (first_picture_in_bitstream)
            return true;   // Premiere picture du bitstream
        if (HandleCraAsBlaFlag)
            return true;   // Random access point : CRA traitee comme BLA
        return false;      // CRA normale en lecture sequentielle
    }

    return false;  // Non-IRAP : pas applicable
}
```

### HandleCraAsBlaFlag

Ce flag est signale par l'application (pas dans le bitstream). Il est mis a `true` quand le decodeur commence le decodage a un point CRA (random access). Dans ce cas :
- Les pictures RASL associees sont ignorees (non decodees, non affichees)
- Le POC est reinitialise
- Le DPB est vide

**Piege** : Oublier de set `HandleCraAsBlaFlag` lors d'un seek provoque des artefacts visuels (les RASL pics referencent des pictures inexistantes).

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

## 8.3.6 — MV Scaling (§8.5.3.2.12)

Quand un motion vector d'une reference est utilise pour predire un bloc dont la reference est a une distance POC differente, le MV doit etre mis a l'echelle.

```cpp
// §8.5.3.2.12 — Temporal MV scaling
int32_t scale_mv(int32_t mv, int32_t td, int32_t tb) {
    // td = POC distance entre la source (co-located) et sa reference
    // tb = POC distance entre la picture courante et sa reference

    td = Clip3(-128, 127, td);
    tb = Clip3(-128, 127, tb);

    int tx = (16384 + (abs(td) >> 1)) / td;  // division avec arrondi
    int distScaleFactor = Clip3(-4096, 4095, (tb * tx + 32) >> 6);

    return Clip3(-32768, 32767,
        Sign(distScaleFactor * mv) *
        ((Abs(distScaleFactor * mv) + 127) >> 8));
}
```

Le MV scaling est utilise dans :
1. **Candidat temporel merge** (§8.5.3.2.3) : MV du bloc co-localise scale vers la ref courante
2. **Candidat temporel AMVP** (§8.5.3.2.7) : idem
3. **Bi-prediction implicite** : quand les distances L0/L1 sont asymetriques

### Piege

La division `16384 / td` est une **division entiere avec arrondi**. Utiliser la formule exacte ci-dessus. Une erreur d'arrondi ici se propage a tous les MVs temporels.

## TMVP — Temporal Motion Vector Prediction (§8.5.3.2.9)

Le candidat temporel (utilise dans merge et AMVP) requiert :

### 1. Selection de la picture co-localisee

```cpp
// La picture co-localisee est determinee par le slice header :
Picture* colPic;
if (slice_type == B && !collocated_from_l0_flag)
    colPic = RefPicList1[collocated_ref_idx];
else
    colPic = RefPicList0[collocated_ref_idx];
```

### 2. Selection du bloc co-localise

```cpp
// Deux positions candidates dans la picture co-localisee :
// Position 1 : bottom-right du PU courant + (1,1)
int xColBr = xPb + nPbW;  // bottom-right x
int yColBr = yPb + nPbH;  // bottom-right y

// Position 2 : centre du PU courant
int xColCtr = xPb + (nPbW >> 1);
int yColCtr = yPb + (nPbH >> 1);

// Essayer bottom-right d'abord, puis centre si non disponible
// "Non disponible" si : hors image, hors slice/tile, ou dans un CU intra
bool brAvail = is_available(colPic, xColBr, yColBr);
MV colMv;
if (brAvail) {
    colMv = get_mv(colPic, xColBr, yColBr, refIdxCol);
} else {
    colMv = get_mv(colPic, xColCtr, yColCtr, refIdxCol);
}
```

### 3. Scaling du MV co-localise

```cpp
// Scaler le MV de la picture co-localisee vers la reference de la picture courante
int td = DiffPicOrderCnt(colPic, colRefPic);   // distance dans la co-loc
int tb = DiffPicOrderCnt(currentPic, targetRefPic);  // distance courante
MV scaledMv = scale_mv(colMv, td, tb);
```

### 4. Conditions d'activation

- `slice_temporal_mvp_enabled_flag` doit etre actif
- La picture co-localisee doit etre disponible dans le DPB
- Le bloc co-localise ne doit pas etre intra

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
- [ ] NoRaslOutputFlag derivation
- [ ] HandleCraAsBlaFlag handling pour random access/seek
- [ ] MV scaling (temporal distance compensation)
- [ ] TMVP : selection picture co-localisee
- [ ] TMVP : selection bloc co-localise (bottom-right puis centre)
- [ ] TMVP : scaling du MV co-localise
- [ ] Tests : sequence avec reordonnancement B-frames, verifier l'ordre POC
