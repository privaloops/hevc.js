# Phase 4E — Transform Inverse + Dequantization

## Statut : FAIT (a valider isolement)

## Objectif

Valider que la chaine dequant → transform inverse → residual est bit-exact.
Test sur des vecteurs de coefficients connus, sans dependre de CABAC ou du parsing.

## Spec refs

- §8.6.1 : Scaling process (dequantization)
- §8.6.2 : Scaling list derivation
- §8.6.3 : Transform coefficient scaling (level scale, QP, bit shift)
- §8.6.4 : Transform inverse (DCT/DST)
- Table 8-10 : QP chroma mapping (non-linear)

## Ce qui est implemente (`transform.h`, `transform.cpp`)

- `perform_dequant()` : scaling matrices 4x4/8x8, level scale table [40,45,51,57,64,72]
- `perform_transform_inverse()` : DST 4x4 (luma intra only), DCT 4x4/8x8/16x16/32x32
- Partial butterfly implementation
- Inter-pass clipping (§8.6.4.2) : clip [-32768, 32767] entre passe V et H
- Transform skip (bypass transform, shift specifique)

## Points critiques verifies

| Point | Spec | Statut |
|-------|------|--------|
| Clipping inter-passe | §8.6.4.2 | Implemente |
| Shift1 = 7 (apres V), Shift2 = 20 - BitDepth (apres H) | §8.6.4.2 | Implemente |
| DST 4x4 pour luma intra seulement | §8.6.4.2 | Implemente |
| Transform skip shift (15 - BitDepth) | §8.6.4.2 | Implemente |
| QP chroma mapping table non-lineaire | Table 8-10 | Implemente |
| Scaling list defaults (non-flat pour 8x8+) | §7.4.5 | Implemente |

## Tests existants

Valides indirectement par les 3 toy tests pixel-perfect.
Pas de test unitaire isole.

## Tests a ajouter

### 1. DCT inverse round-trip
Generer des coefficients connus → dequant → transform inverse → verifier le residual.
```cpp
// Coefficients : seul le DC est non-zero (cas simple)
int16_t coeffs[16] = {100, 0, 0, ..., 0};
perform_dequant(..., qp, coeffs, scaled);
perform_transform_inverse(2, 0, true, false, 8, scaled, residual);
// Verifier que residual[i] == valeur attendue (calculee a la main)
```

### 2. Clipping inter-passe
Coefficients qui produisent des valeurs intermediaires > 32767 ou < -32768.
Verifier que le clipping est applique correctement.

### 3. QP chroma mapping
Verifier la table non-lineaire pour qPi dans [30, 43].

### 4. Transform skip
Coefficients avec transform_skip=1 → residual = (coeffs + round) >> shift.

## Taches

- [ ] Creer `tests/unit/test_transform.cpp`
- [ ] Test DCT inverse 4x4 (DST) avec coefficients connus
- [ ] Test DCT inverse 8x8 avec coefficients connus
- [ ] Test clipping inter-passe
- [ ] Test QP chroma mapping table
- [ ] Test transform skip

## Critere de sortie

- [ ] 6+ tests passent dans test_transform.cpp
- [ ] Le residual pour un jeu de coefficients connus est bit-exact
