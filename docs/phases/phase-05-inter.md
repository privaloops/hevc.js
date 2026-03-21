# Phase 5 — Inter Prediction (P/B frames)

## Objectif

Decoder les frames P et B : motion compensation, interpolation, bi-prediction.
Pixel-perfect vs ffmpeg sur `p_qcif_10f` et `b_qcif_10f` (sans deblocking/SAO).

## Prerequis

Phase 4 completee (I-frames pixel-perfect sur i_64x64_qp22).

## Spec refs

- §8.3 : Reference picture management (DPB, RPS, ref lists)
- §8.5.3 : Inter prediction (motion compensation)
- §7.3.11 : prediction_unit syntax (merge, AMVP)

## Decoupe en sous-phases

La phase 5 est subdivisee en **4 sous-phases** avec validation independante,
suivant le meme principe que la phase 4.

| Sous-phase | Fichier | Validation |
|------------|---------|------------|
| 5A — DPB + Ref Lists | `phase-05a-dpb.md` | Test : ordre POC correct, ref lists matchent HM |
| 5B — MV Derivation | `phase-05b-mv-derivation.md` | Test : MV de chaque PU identique a HM |
| 5C — Interpolation | `phase-05c-interpolation.md` | Test unitaire : filtres 8-tap/4-tap sur vecteurs connus |
| 5D — Integration | `phase-05d-integration.md` | Oracle pixel-perfect P puis B frames |

## Ordre d'execution

```
5A (DPB + Ref Lists) ──→ 5B (MV Derivation) ──→ 5D (Integration)
                                                       ↑
5C (Interpolation) ────────────────────────────────────┘
```

5A et 5C sont independants et peuvent etre faits en parallele.

---

### 5A — DPB + Reference Picture Lists

**Objectif** : Gerer le buffer d'images decodees, construire les listes de reference.

**Ce qui est specifique :**
- POC derivation avec wrap-around (§8.3.1)
- `NoRaslOutputFlag` + `HandleCraAsBlaFlag` (§8.1) — critique pour random access
- Reference Picture Set derivation (§8.3.2) — short-term et long-term
- DPB management : add, bump, remove, sizing par level
- RefPicList0/1 construction (§8.3.3) + modification

**Validation** :
- Test unitaire : verifier l'ordre POC sur une sequence IPBBP
- Test unitaire : verifier les ref lists pour differentes structures de GOP
- Trace comparative vs HM : POC et ref lists pour chaque slice

**Critere de sortie** :
- [ ] POC correct pour 30 frames d'une sequence hierarchique B
- [ ] RefPicList0/1 identiques a HM pour chaque slice

---

### 5B — MV Derivation (Merge + AMVP)

**Objectif** : Deriver le MV correct pour chaque PU.

**Ce qui est specifique :**
- Merge mode : 5 candidats spatiaux (A1, B1, B0, A0, B2) + temporel + combined bi-pred + zero padding
- AMVP mode : 2 candidats spatiaux + temporel + zero padding + MVD
- TMVP : picture co-localisee, MV scaling par distance POC (§8.5.3.2.12)
- CU skip mode : merge sans residual

**Pieges identifies :**
- Positions d'echantillonnage des voisins (Figure 8-5) — off-by-one facile
- TMVP : bottom-right (+1,+1) en priorite, fallback au centre
- MV scaling : `Clip3(-4096, 4095, (tb * tx + 32) >> 6)` avec `tx = (16384 + (abs(td) >> 1)) / td`
- Combined bi-pred : Table 8-8, seulement pour B-slices

**Validation** :
- Trace comparative vs HM : pour chaque PU, logger (x, y, size, mv_L0, mv_L1, ref_idx)
- Le PREMIER PU avec un MV different pointe au bug
- Test unitaire pour MV scaling avec des distances POC variees

**Critere de sortie** :
- [ ] MV de chaque PU identique a HM pour 10 frames P-only
- [ ] MV de chaque PU identique a HM pour 10 frames avec B hierarchique

---

### 5C — Interpolation (Luma + Chroma)

**Objectif** : Filtres d'interpolation bit-exact.

**Ce qui est specifique :**
- Luma : filtre 8-tap (Table 8-1), 16 positions fractionnaires (1/4 pel)
- Chroma : filtre 4-tap (Table 8-2), 8 positions fractionnaires (1/8 pel)
- MV chroma derivation depuis MV luma
- Interpolation 2D : H puis V avec precision intermediaire

**Pieges identifies (source #1 de mismatch apres CABAC) :**
- **H-only** : shift = BitDepth - 8, clip final
- **V-only** : shift = 14 - BitDepth, clip final
- **2D passe H** : shift = BitDepth - 8, PAS de clip intermediaire
- **2D passe V** : shift = 6, clip final
- **Bi-pred** : precision etendue (pas de clip), puis `(predL0 + predL1 + offset) >> shift` avec shift = 15 - BitDepth
- Buffer intermediaire : int16_t pour 8-bit

**Validation** :
- Test unitaire : interpolation 1D (H-only, V-only) sur un vecteur 16 samples
- Test unitaire : interpolation 2D sur un bloc 8x8
- Test unitaire : bi-pred averaging avec precision etendue
- Verifier chaque cas de shift/clip independamment

**Critere de sortie** :
- [ ] 10+ tests unitaires couvrant les 4 cas de shift (H, V, 2D-H, 2D-V)
- [ ] Uni-pred et bi-pred testes

---

### 5D — Integration

**Objectif** : Decoder des sequences P et B completes, pixel-perfect.

**Prerequis** : 5A, 5B, 5C valides independamment.

**Taches** :
- [ ] Integrer DPB + MV + interpolation dans le pipeline de decodage
- [ ] Weighted prediction (explicit) — parsing deja fait en Phase 3
- [ ] Partition modes inter : 2Nx2N, 2NxN, Nx2N, AMP (2NxnU/nD, nLx2N/nRx2N)
- [ ] rqt_root_cbf pour inter (residual optionnel)

**Validation oracle** :
```bash
# P-frames only
ctest -R oracle_p_qcif_10f --output-on-failure

# B-frames
ctest -R oracle_b_qcif_10f --output-on-failure
```

**Critere de sortie** :
- [ ] `oracle_p_qcif_10f` pixel-perfect (MD5 match)
- [ ] `oracle_b_qcif_10f` pixel-perfect (MD5 match)

## Estimation de complexite

Elevee. La derivation MV (merge/AMVP) est la partie la plus complexe.
L'interpolation est volumineuse mais bien definie et testable isolement.
Le DPB est delicat pour les edge cases (random access, long-term refs).
