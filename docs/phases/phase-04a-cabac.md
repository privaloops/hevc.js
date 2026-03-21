# Phase 4A — CABAC Engine + Context Init

## Statut : FAIT

## Objectif

Moteur arithmetique CABAC fonctionnel avec initialisation correcte de tous les contextes.

## Spec refs

- §9.3.4.3 : Arithmetic decoding (decode_decision, decode_bypass, decode_terminate)
- §9.3.1.1 : Context initialization (slope/offset formula, initValue tables)
- §9.3.4.2 : Renormalization
- Tables 9-5 a 9-31 : Init values pour chaque syntax element

## Ce qui est implemente

### CABAC Engine (`cabac.h`, `cabac.cpp`)
- `init_decoder()` : initialise ivlCurrRange et ivlOffset depuis le bitstream
- `init_contexts()` : formule slope/offset avec Clip3, pour I/P/B slices
- `decode_decision()` : decode un bin avec contexte (rangeTabLps, transIdx)
- `decode_bypass()` : decode un bin sans contexte
- `decode_terminate()` : decode end_of_slice_segment_flag
- `decode_bypass_bins()` : decode N bins bypass d'un coup
- `cabac_init_flag` : permutation des tables P/B

### Tables (`cabac_tables.h`)
- `rangeTabLps[64][4]` : table LPS range (spec Table 9-48)
- `transIdxMps[64]`, `transIdxLps[64]` : tables de transition
- `cabacInitValues[155]` : init values pour 155 contextes (I/P/B)
- Enum `CabacCtxOffset` : offset de chaque syntax element dans le tableau plat

## Tests existants (`test_cabac.cpp`)

| Test | Verifie |
|------|---------|
| InitValueFormula | split_cu_flag[0] init pour I-slice QP=26 |
| AllContextsInRange | pStateIdx [0,63], valMps {0,1} pour tous les ctx, tous les QP |
| CabacInitFlagPermutation | P-slice avec cabac_init_flag=1 == B-slice normal |
| DecodeTerminate | Bin terminate sur donnees connues |
| DecodeBypass | Bins bypass sur donnees connues |
| DecodeDecision | Decision bin sur donnees connues |
| SaveRestoreContexts | Save/restore du tableau de contextes |

## Critere de sortie

- [x] 7 tests passent
- [x] Init values verifiees contre HM ContextTables.h pour les 44 sig_coeff_flag contextes
- [x] Moteur arithmetique produit les memes bins que HM pour les 326 premiers bins de i_64x64_qp22
