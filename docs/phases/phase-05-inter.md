# Phase 5 -- Inter Prediction (P/B frames)

## Objectif

Decoder les frames P et B : motion compensation, interpolation, bi-prediction.
Pixel-perfect vs ffmpeg sur `p_qcif_10f` et `b_qcif_10f` (sans deblocking/SAO).

## Prerequis

Phase 4 completee (I-frames pixel-perfect, y compris multi-CTU non-64-aligned).

## Spec refs

- S8.3 : Reference picture management (DPB, RPS, ref lists)
- S8.5.3 : Inter prediction (motion compensation)
- S7.3.8.6 : prediction_unit syntax (merge, AMVP)
- S8.5.3.2 : Luma interpolation (8-tap)
- S8.5.3.3 : Chroma interpolation (4-tap)
- S8.5.3.4 : Weighted prediction

## Decoupe en sous-etapes (10 etapes + 1 prealable)

Inspiree du succes de la Phase 4 (6 sous-phases avec validation independante).
Principe : **un test par etape, on ne passe pas a la suivante tant que ca echoue**.

| Etape | Fichier | Validation | Dependances |
|-------|---------|------------|-------------|
| **5.0** | `phase-05-0-fix-multictu.md` | I-frame QCIF 176x144 pixel-perfect | Phase 4 |
| **5.1** | `phase-05-1-dpb-poc.md` | POC correct pour 10 frames | -- |
| **5.2** | `phase-05-2-rps-reflists.md` | RefPicList0/1 vs HM par slice | 5.1 |
| **5.3** | `phase-05-3-merge-spatial.md` | 5 candidats spatiaux vs HM sur 5 PUs | -- |
| **5.4** | `phase-05-4-tmvp.md` | MV temporel vs HM sur premier P-frame | 5.1, 5.2 |
| **5.5** | `phase-05-5-merge-full.md` | Merge list complete vs HM (spatial+temp+combined+zero) | 5.3, 5.4 |
| **5.6** | `phase-05-6-amvp-mvd.md` | MV final vs HM pour PUs AMVP | 5.2 |
| **5.7** | `phase-05-7-interp-luma.md` | Test unitaire 8-tap (H, V, 2D-H, 2D-V) | -- |
| **5.8** | `phase-05-8-interp-chroma-bipred.md` | Test unitaire 4-tap chroma + bi-pred averaging | 5.7 |
| **5.9** | `phase-05-9-integration-p.md` | `oracle_p_qcif_10f` pixel-perfect | 5.0-5.8 |
| **5.10** | `phase-05-10-integration-b.md` | `oracle_b_qcif_10f` pixel-perfect | 5.9 |

## Graphe de dependances

```
5.0 (Fix multi-CTU) ────────────────────────────────────────────┐
                                                                 │
5.1 (DPB+POC) ──> 5.2 (RPS+RefLists) ──> 5.6 (AMVP+MVD) ──────┤
                       │                                         │
                       v                                         │
5.3 (Merge spatial) ──> 5.4 (TMVP) ──> 5.5 (Merge full) ───────┤
                                                                 │
5.7 (Interp luma) ──> 5.8 (Interp chroma+bipred) ──────────────┤
                                                                 │
                                                          5.9 (P-frames)
                                                                 │
                                                          5.10 (B-frames)
```

5.0, 5.1, 5.3, 5.7 sont independants (parallelisables).

## Lecons de la Phase 4 appliquees

1. **Audit spec AVANT le code** -- lire le PDF spec, transcrire directement
2. **Comparaison HM bin-par-bin** pour le parsing CABAC
3. **Valider chaque couche en isolation** avant integration
4. **Ne jamais "simplifier" la spec** -- transcrire les formules telles quelles
5. **Audit systematique > debug incremental** -- auditer tout le code avant de tracer

## Etat actuel du code

Le code Phase 5 existe deja (5A-5D implementes) mais contient des bugs :
- `inter_prediction.cpp` (662 lignes) : merge, AMVP, TMVP, MC
- `interpolation.cpp` (303 lignes) : filtres 8-tap/4-tap
- `dpb.cpp` (437 lignes) : DPB, POC, RPS, ref lists
- Bug connu : desync `sig_coeff_flag` context au bin 1402 de CTU 2 (non-64-aligned)
- SAO parsing : 2 bugs fixes (cIdx==2 type, sao_offset_sign conditionnel)

Le travail de chaque etape est donc : **auditer le code existant contre la spec,
corriger les bugs, ajouter des tests de validation**.
