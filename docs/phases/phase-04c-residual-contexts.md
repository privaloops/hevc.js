# Phase 4C — Residual Coding Context Derivation

## Statut : A VALIDER

## Objectif

Valider que CHAQUE fonction `derive_*_ctx()` produit le MEME index de contexte que HM,
pour toutes les combinaisons de parametres. Test purement fonctionnel, pas de CABAC.

## Pourquoi c'est critique

Un mauvais index de contexte ne change pas forcement la valeur du bin decode
(deux contextes differents peuvent decoder la meme valeur par coincidence),
mais il corrompt l'etat arithmetique du moteur CABAC. Le bug apparait alors
des dizaines ou centaines de bins plus tard, rendant le debug quasi-impossible.

## Bugs trouves dans cette zone

| Bug | Cause | Fix |
|-----|-------|-----|
| Chroma sig_coeff_flag offset 27 vs 28 | Spec eq 9-55 vs HM layout | Base chroma = 28 |
| Chroma 4x4 firstSigCtx = 9 au lieu de 0 | Confusion avec luma 4x4 | firstSigCtx[CHROMA][4x4] = 0 |
| Chroma 8x8 firstSigCtx = 12 au lieu de 9 | Decalage d'un context type | firstSigCtx[CHROMA][8x8] = 9 |
| Luma 8x8 firstSigCtx = 21 au lieu de 9 | Confusion NxN vs 8x8 | firstSigCtx[LUMA][8x8] = 9 |

## Spec refs

- §9.3.4.2.5 : sig_coeff_flag context derivation (le plus complexe)
- §9.3.4.2.6 : coeff_abs_level_greater1_flag ctxSet
- §9.3.4.2.7 : coeff_abs_level_greater2_flag ctxSet
- §9.3.3.5 : last_sig_coeff prefix context
- HM `ContextTables.h` : tables de reference
- HM `TComTrQuant.cpp` : `getSigCtxInc()`, `calcPatternSigCtx()`
- HM `TComChromaFormat.h` : `getContextSetIndex()`, `significanceMapContextSetStart`

## Fonctions a tester

### 1. `derive_sig_coeff_flag_ctx(cIdx, log2TrafoSize, xC, yC, scanIdx, coded_sub_block_flag, numSbPerSide)`

Valeurs de reference HM (`significanceMapContextSetStart` + `getSigCtxInc`) :

```
Luma (base=0) :
  DC (xC+yC==0) → 0
  4x4 non-DC → ctxIdxMap[pos]  (range 1-8)
  8x8 diag → 9 + offset  (range 9-14)
  8x8 non-diag → 15 + offset  (range 15-20)
  NxN (>=16x16) → 21 + offset  (range 21-26)
  transform_skip → 27

Chroma (base=28) :
  DC → 28
  4x4 non-DC → 28 + ctxIdxMap[pos]  (range 29-36)
  8x8 → 28 + 9 + offset  (range 37-39)
  NxN (>=16x16) → 28 + 12 + offset  (range 40-42)
  transform_skip → 28 + 15 = 43
```

Cas de test :
```cpp
// Luma DC
EXPECT_EQ(derive_sig_coeff_flag_ctx(0, 4, 0, 0, 0, csbf, 4), 0);
// Luma 4x4, pos (1,0)
EXPECT_EQ(derive_sig_coeff_flag_ctx(0, 2, 1, 0, 0, csbf, 1), 1);
// Luma 8x8 diag, first group, pos (1,0) with prevCsbf=0
EXPECT_EQ(derive_sig_coeff_flag_ctx(0, 3, 1, 0, 0, csbf, 2), 9+2);
// Chroma DC
EXPECT_EQ(derive_sig_coeff_flag_ctx(1, 3, 0, 0, 0, csbf, 2), 28);
// Chroma 4x4, pos (0,1)
EXPECT_EQ(derive_sig_coeff_flag_ctx(1, 2, 0, 1, 0, csbf, 1), 28+2);
// Chroma 8x8, first group, pos (1,0) with prevCsbf=0
EXPECT_EQ(derive_sig_coeff_flag_ctx(1, 3, 1, 0, 0, csbf, 2), 28+9+2);
```

### 2. `decode_coeff_abs_level_greater1_flag` context

HM `getContextSetIndex` :
```
Luma:   ctxSet = (subsetIdx > 0 ? 2 : 0) + (prevGreater1==0 ? 1 : 0)
Chroma: ctxSet = 0 + (prevGreater1==0 ? 1 : 0)
ctxIdx = (cIdx==0 ? 0 : 16) + ctxSet*4 + greater1Ctx
```

### 3. `decode_coeff_abs_level_greater2_flag` context

```
ctxIdx = (cIdx==0 ? ctxSet : 4 + (ctxSet & 1))
```

### 4. `decode_last_sig_coeff_prefix` context

```
Luma:   ctxOff = 3*(log2-2) + ((log2-1)>>2), ctxShift = (log2+1)>>2
Chroma: ctxOff = 15, ctxShift = log2-2
ctxIdx = ctxOffset + ctxOff + (i >> ctxShift)
```

### 5. `decode_coded_sub_block_flag` context

```
ctxInc = (cIdx > 0 ? 2 : 0) + (csbfRight || csbfBelow ? 1 : 0)
```

## Methode de validation

### Option A : Test unitaire pur (recommande)

Creer `tests/unit/test_residual_contexts.cpp` qui appelle chaque `derive_*` avec des parametres varies et compare avec des valeurs attendues calculees a la main depuis HM.

Avantage : rapide, deterministe, pas de build HM necessaire.

### Option B : Extraction automatique depuis HM

1. Modifier HM pour logger `(cIdx, log2, xC, yC, scanIdx, prevCsbf) → ctxIdx` pour chaque sig_coeff_flag
2. Parser le log
3. Rejouer les memes parametres dans notre `derive_sig_coeff_flag_ctx()`
4. Comparer

Avantage : couverture exhaustive sur un vrai bitstream.

## Taches

- [ ] Creer `tests/unit/test_residual_contexts.cpp`
- [ ] Tester `derive_sig_coeff_flag_ctx` : 20+ cas couvrant luma/chroma x DC/4x4/8x8/NxN x prevCsbf
- [ ] Tester `decode_coeff_abs_level_greater1_flag` context : luma/chroma x ctxSet x greater1Ctx
- [ ] Tester `decode_coeff_abs_level_greater2_flag` context : luma/chroma x ctxSet
- [ ] Tester `decode_last_sig_coeff_prefix` context : luma/chroma x log2 2-5
- [ ] Tester `decode_coded_sub_block_flag` context : luma/chroma x neighbours
- [ ] Valider par trace comparative vs HM sur i_64x64_qp22 (Option B)

## Critere de sortie

- [ ] test_residual_contexts passe avec 30+ cas
- [ ] Trace sig_coeff_flag ctx identique a HM pour les 100 premiers sig_coeff_flag de i_64x64_qp22
