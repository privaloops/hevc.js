# Chapitre 9 — Parsing Process (CABAC)

Spec ref : §9.1 (general), §9.2 (initialization), §9.3 (binarization), §9.4 (context modeling), §9.5 (decoding engine)

## Vue d'ensemble

CABAC (Context-Adaptive Binary Arithmetic Coding) est le seul mode d'entropy coding en HEVC (contrairement a H.264 qui avait aussi CAVLC).

Chaque syntax element `ae(v)` dans le chapitre 7 est decode par CABAC.

### Architecture CABAC

```
Bitstream → Arithmetic Decoder → Binary decisions (bins)
                                        │
                                   Binarization
                                        │
                                  Syntax Elements
```

Le processus :
1. **Binarization** : chaque syntax element a un schema de binarization (comment il est represente en bins)
2. **Context modeling** : chaque bin utilise un "contexte" qui stocke la probabilite
3. **Arithmetic decoding** : decodage du bin base sur la probabilite du contexte

## 9.2 — Initialisation

### Initialisation des contextes

```cpp
// Au debut de chaque slice :
// §9.2.1.1 — Context variables initialization

struct CabacContext {
    uint8_t pStateIdx;  // probability state index (0-62)
    uint8_t valMps;     // most probable symbol (0 or 1)
};

void init_context(CabacContext& ctx, int initValue, int SliceQpY) {
    int slope = (initValue >> 4) * 5 - 45;
    int offset = ((initValue & 15) << 3) - 16;
    int initState = Clip3(1, 126, ((slope * Clip3(0, 51, SliceQpY)) >> 4) + offset);

    if (initState >= 64) {
        ctx.pStateIdx = initState - 64;
        ctx.valMps = 1;
    } else {
        ctx.pStateIdx = 63 - initState;
        ctx.valMps = 0;
    }
}

// Il y a ~200 contextes differents en HEVC
// Chaque type de syntax element a ses propres contextes
// Les initValues sont tabules dans la spec (Tables 9-5 a 9-42)
```

### Initialisation du moteur arithmetique

```cpp
struct ArithmeticDecoder {
    uint16_t ivlCurrRange;   // current range [256, 510]
    uint16_t ivlOffset;      // current offset

    void init(BitstreamReader& bs) {
        ivlCurrRange = 510;
        ivlOffset = bs.read_bits(9);
    }
};
```

## 9.3 — Binarization

Chaque syntax element a un schema de binarization specifique :

### Types de binarization

| Type | Utilise pour | Description |
|------|-------------|-------------|
| FL (Fixed Length) | flags, petits entiers | n bins, valeur directe |
| TR (Truncated Rice) | borne | comme unary mais tronque |
| EGk (Exp-Golomb kth order) | grands entiers | prefix unary + suffix |
| TU (Truncated Unary) | petits entiers bornes | unary tronque a cMax |

### Exemples

```cpp
// split_cu_flag : FL, 1 bin, context-coded
// Binarization triviale : le bin EST la valeur

// part_mode : variable selon slice type et CU size
// Binarization depend du contexte (Table 9-34)

// coeff_abs_level_remaining : Golomb-Rice + EG
// §9.3.2.7 — prefix TR (cRiceParam) + suffix EGk
// C'est le plus complexe

// last_sig_coeff_x_prefix : TR binarization
// Les bins utilisent des contextes differents selon la position
```

### residual_coding binarization (la plus complexe)

```cpp
// §9.3.3.1 — coeff_abs_level_greater1_flag : 1 bin, context-coded
// §9.3.3.2 — coeff_abs_level_greater2_flag : 1 bin, context-coded
// §9.3.3.3 — coeff_abs_level_remaining : bypass-coded (pas de contexte)
//   → Golomb-Rice code avec parametre adaptatif

int decode_coeff_abs_level_remaining(ArithmeticDecoder& ad, int cRiceParam) {
    // Prefix : TR binarization (bypass)
    int prefix = 0;
    while (prefix < 5 && ad.decode_bypass())  // max 5 bins unary
        prefix++;

    int value;
    if (prefix < 5) {
        // Suffix : cRiceParam bits (bypass)
        value = (prefix << cRiceParam) + ad.decode_bypass_bits(cRiceParam);
    } else {
        // Exp-Golomb suffix
        int length = cRiceParam + 1;
        while (ad.decode_bypass())
            length++;
        value = (((1 << length) - 1) << cRiceParam) + ad.decode_bypass_bits(length + cRiceParam);
    }
    return value;
}
```

## 9.4 — Context Modeling

### Choix du contexte

Chaque bin d'un syntax element utilise un contexte specifique. Le choix du contexte depend souvent de :

1. **Position dans la binarization** (quel bin du mot)
2. **Valeurs des voisins** (contexte spatial)
3. **Profondeur du quad-tree**
4. **Type de slice** (I, P, B)
5. **Composante** (luma, Cb, Cr)

```cpp
// Exemple : split_cu_flag context (§9.3.4.2.2)
int ctx_split_cu_flag(int x0, int y0, int log2CbSize) {
    // Contexte base sur la profondeur des CU voisins
    int condL = (left_neighbor_depth > current_depth) ? 1 : 0;
    int condA = (above_neighbor_depth > current_depth) ? 1 : 0;
    return condL + condA;  // ctx index 0, 1, or 2
}

// Exemple : sig_coeff_flag context (§9.3.4.2.8) — tres complexe
// Depend de : cIdx, log2TrafoSize, scan position, coded_sub_block_flag des voisins
// ~42 contextes pour luma, ~16 pour chroma
```

### Table des contextes (resume)

| Syntax element | Nombre de contextes | Init table |
|---------------|--------------------:|------------|
| split_cu_flag | 3 | 9-5 |
| cu_skip_flag | 3 | 9-6 |
| pred_mode_flag | 1 | 9-7 |
| part_mode | 4 | 9-8 |
| prev_intra_luma_pred_flag | 1 | 9-9 |
| intra_chroma_pred_mode | 1 | 9-10 |
| merge_flag | 1 | 9-11 |
| merge_idx | 1 | 9-12 |
| inter_pred_idc | 5 | 9-13 |
| ref_idx | 2 | 9-14 |
| mvd (abs_mvd_greater0/1) | 2 | 9-15 |
| cbf_luma | 2 | 9-18 |
| cbf_cb/cr | 5 | 9-19 |
| transform_skip_flag | 2 | 9-20 |
| last_sig_coeff_x/y_prefix | 18+18 | 9-21/22 |
| coded_sub_block_flag | 4 | 9-23 |
| sig_coeff_flag | 42+16 | 9-24/25 |
| coeff_abs_level_greater1 | 24 | 9-26 |
| coeff_abs_level_greater2 | 6 | 9-27 |
| sao_merge/type/offset | 3+1 | 9-28/29 |

## 9.5 — Arithmetic Decoding Engine

### Decode Decision (context-coded bin)

```cpp
// §9.3.4.3.2 — Arithmetic decoding process for a binary decision
int ArithmeticDecoder::decode_decision(CabacContext& ctx) {
    uint8_t qRangeIdx = (ivlCurrRange >> 6) & 3;
    uint16_t ivlLpsRange = rangeTabLps[ctx.pStateIdx][qRangeIdx];

    ivlCurrRange -= ivlLpsRange;

    int binVal;
    if (ivlOffset >= ivlCurrRange) {
        // LPS (least probable symbol)
        binVal = 1 - ctx.valMps;
        ivlOffset -= ivlCurrRange;
        ivlCurrRange = ivlLpsRange;

        if (ctx.pStateIdx == 0)
            ctx.valMps = 1 - ctx.valMps;
        ctx.pStateIdx = transIdxLps[ctx.pStateIdx];
    } else {
        // MPS (most probable symbol)
        binVal = ctx.valMps;
        ctx.pStateIdx = transIdxMps[ctx.pStateIdx];
    }

    // Renormalization
    renormalize();

    return binVal;
}
```

### Decode Bypass (equiprobable bin)

```cpp
// §9.3.4.3.4
int ArithmeticDecoder::decode_bypass() {
    ivlOffset = (ivlOffset << 1) | read_bit();

    if (ivlOffset >= ivlCurrRange) {
        ivlOffset -= ivlCurrRange;
        return 1;
    }
    return 0;
}
```

### Renormalization

```cpp
void ArithmeticDecoder::renormalize() {
    while (ivlCurrRange < 256) {
        ivlCurrRange <<= 1;
        ivlOffset = (ivlOffset << 1) | read_bit();
    }
}
```

### Tables LPS

```cpp
// Table 9-48 : rangeTabLps[64][4]
// 64 states x 4 range indices
// Ces tables sont dans la spec, ~256 entrees a recopier exactement

// Table 9-49 : transIdxMps[64] — next state after MPS
// Table 9-50 : transIdxLps[64] — next state after LPS
```

## Strategie d'implementation

1. **Moteur arithmetique** en premier (decode_decision, decode_bypass, renormalize)
2. **Initialisation des contextes** (recopier les tables d'init)
3. **Binarization** de chaque syntax element un par un
4. **Tests** : decoder un slice simple (I-frame) et verifier les syntax elements

Le CABAC est la partie la plus sensible aux erreurs : un seul bit mal lu fait diverger tout le reste. C'est pourquoi il faut le tester tres finement.

## Pieges connus

1. **Bit-exactness** : L'arithmetique est entiere et doit etre exactement conforme a la spec
2. **Renormalization** : Chaque oubli de renorm corrompt le decodage
3. **Context index** : Le moindre mauvais contexte produit des resultats absurdes
4. **cabac_init_flag** : Peut permuter les tables d'init entre I et P/B
5. **Dependent slice segments** : Le CABAC continue l'etat du slice precedent
6. **End of slice detection** : end_of_slice_segment_flag + byte alignment

## Checklist

- [ ] Arithmetic decoder (decode_decision, decode_bypass, terminate)
- [ ] Renormalization
- [ ] Tables rangeTabLps, transIdxMps, transIdxLps (recopier de la spec)
- [ ] Context initialization avec toutes les tables d'initValues
- [ ] Binarization de chaque syntax element ae(v)
- [ ] Context selection pour chaque syntax element
- [ ] cabac_init_flag handling
- [ ] Dependent slice segment CABAC continuation
- [ ] end_of_slice_segment_flag detection
- [ ] Tests : decoder un I-frame minimal et verifier tous les syntax elements
