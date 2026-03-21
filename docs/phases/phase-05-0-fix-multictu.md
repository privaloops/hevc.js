# 5.0 -- Fix I-frame multi-CTU parsing

## Objectif

Corriger le bug de parsing I-frame sur les images non-64-aligned (ex: QCIF 176x144).
Ce bug bloque TOUTE la Phase 5 car le premier frame (IDR) est un I-frame.

## Bug identifie

Le desync CABAC se produit au bin 1402 de CTU 2 (128,0) dans `p_qcif_10f.265`.

**Symptome** : `derive_sig_coeff_flag_ctx` produit un ctxInc different de HM pour un
coefficient specifique, causant une divergence de l'etat arithmetique. Tous les bypass
bins suivants lisent les mauvaises valeurs.

**Preuve** : comparaison bin-par-bin avec HM :
- 9861 decision bins matchent (CTU 0 + CTU 1 complets)
- Divergence au bin 1402 de CTU 2 : notre ctx=90 (sig_coeff_flag offset 8) donne r=274
  alors que HM donne r=500 (contexte different, renormalization differente)

## Methodologie de debug

1. Identifier quelle position de coefficient (xC, yC) dans quelle TU est au bin 1402
2. Verifier `derive_sig_coeff_flag_ctx` pour cette position specifique
3. Comparer avec HM les parametres d'entree : cIdx, log2TrafoSize, xC, yC, scanIdx,
   coded_sub_block_flag voisins, numSbPerSide
4. Trouver la condition qui diverge

## Bugs deja fixes (SAO)

- `SaoTypeIdx[2]` non herite de `SaoTypeIdx[1]` (cIdx==2 Cr)
- `sao_offset_sign` parse inconditionnellement au lieu de `if abs != 0`
- N'affecte pas `p_qcif_10f.265` (SAO desactive dans ce stream)

## Spec refs

- S9.3.4.2.5 : sig_coeff_flag context derivation (eq 9-40 a 9-55)
- S7.3.8.11 : residual_coding syntax
- Table 9-29 : sig_coeff_flag init values

## Fichiers a auditer

- `src/decoding/residual_coding.cpp` : `derive_sig_coeff_flag_ctx()`
- `src/decoding/coding_tree.cpp` : `decode_residual_coding()` appels
- `src/decoding/cabac_tables.h` : verifier les init values ctx 82-125

## Test de validation

Creer un test oracle I-frame QCIF :
```bash
# Generer un I-frame-only QCIF bitstream
ffmpeg -f lavfi -i testsrc=duration=1:size=176x144:rate=1 \
  -c:v libx265 -x265-params "keyint=1:min-keyint=1:bframes=0:sao=0:deblock=0:qp=22" \
  -frames:v 1 tests/conformance/fixtures/i_qcif_176x144.265

# Ou utiliser le premier frame de p_qcif_10f.265 comme test I-frame
```

## Critere de sortie

- [ ] Test oracle I-frame QCIF 176x144 pixel-perfect (nouveau test)
- [ ] `oracle_i_64x64_qp22` toujours pixel-perfect (non-regression)
- [ ] Tous les tests unitaires passent
