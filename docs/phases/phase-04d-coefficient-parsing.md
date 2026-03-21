# Phase 4D — Coefficient Parsing

## Statut : A VALIDER

## Objectif

Valider que les coefficients decodes (avant dequant) sont IDENTIQUES a HM, TU par TU.
Ceci integre 4A (CABAC), 4B (coding tree), 4C (contexts) et la logique de parsing
(scan order, coded_sub_block_flag, sig_coeff_flag, levels, sign).

## Prerequis

- 4B (coding tree) valide : la sequence de syntax elements est correcte
- 4C (residual contexts) valide : chaque derive_*_ctx produit le bon index

## Spec refs

- §7.3.8.11 : residual_coding (spec)
- §9.3.3.5 : last_sig_coeff prefix binarization
- §9.3.3.11 : coeff_abs_level_remaining (Golomb-Rice + EGk)
- §7.4.9.11 : Sign data hiding

## Ce qui est implemente

`residual_coding.cpp` : decode complet avec scan orders (diag/horiz/vert),
sub-block scanning, sig_coeff_flag, greater1/2, sign, remaining, sign hiding.

## Elements a valider

### 1. Scan order derivation
- `derive_scan_idx()` : luma 4x4 et chroma 4x4 utilisent le mode intra pour choisir diag/horiz/vert
- Tous les autres : diagonal
- Verifier contre HM `TComTrQuant::getCodingParameters()`

### 2. Last significant coefficient position
- Prefix binarization avec truncated unary
- Suffix avec fixed-length bypass bins
- `maxBins = (log2TrafoSize << 1) - 1` pour X et Y separement
- Verifier que le bon log2TrafoSize est utilise (luma vs chroma)

### 3. Coefficient levels
- `coeff_abs_level_greater1_flag` : max 8 par sub-block, ctxSet rotation
- `coeff_abs_level_greater2_flag` : 1 seul par sub-block (au lastGreater1ScanPos)
- `coeff_abs_level_remaining` : Golomb-Rice avec cRiceParam adaptatif
- `prevGreater1Ctx` : carry entre sub-blocks (§9.3.4.2.6)

### 4. Sign data hiding
- Condition : `sign_data_hiding_enabled_flag && (lastSigScanPos - firstSigScanPos > 3)`
- Le signe du premier coeff significatif est derive de la parite de la somme

## Methode de validation

### Test : extraire les coefficients de HM et comparer

1. Ajouter dans HM (TDecSbac.cpp, apres parseCoeffNxN) :
```cpp
fprintf(stderr, "[HM_COEFF] (%d,%d) log2=%d cIdx=%d", x0, y0, log2, cIdx);
for (int i = 0; i < trSize*trSize; i++) fprintf(stderr, " %d", coefficients[i]);
fprintf(stderr, "\n");
```

2. Ajouter la meme trace dans notre `decode_residual_coding()` apres le remplissage de `coefficients[]`

3. Comparer les deux traces : la premiere TU differente pointe au bug

### Test unitaire : coefficients connus

Creer des bitstreams minimalistes (1 CU, 1 TU) et verifier les coefficients extraits
contre les valeurs attendues (calculees a la main ou extraites de HM).

## Taches

- [ ] Ajouter trace coefficients dans notre decodeur
- [ ] Ajouter trace equivalente dans HM
- [ ] Comparer pour i_64x64_qp22 : trouver la premiere TU differente
- [ ] Si divergence : identifier si c'est le scan, les levels, le sign, ou le cRiceParam
- [ ] Fixer le(s) bug(s)
- [ ] Test unitaire : au moins 5 TU avec coefficients verifies

## Critere de sortie

- [ ] Coefficients de toutes les TU de la frame i_64x64_qp22 sont identiques a HM
- [ ] Inclut luma 4x4, 8x8, 16x16 et chroma 4x4, 8x8
