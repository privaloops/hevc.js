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

## Lecons de la Phase 4+5 appliquees

1. **Audit spec AVANT le code** -- lire le PDF spec, transcrire directement
2. **Lister TOUS les processus invoques** -- chaque section spec invoque d'autres processus (ex: §7.3.8.11 invoque §9.3.4.3.6). Les oublier = bug silencieux
3. **Valider chaque couche en isolation** avant integration
4. **Ne jamais "simplifier" la spec** -- transcrire les formules telles quelles
5. **Audit systematique > debug incremental** -- auditer tout le code avant de tracer
6. **Max 2 iterations de debug** -- apres 2 echecs, relire la spec au lieu d'ajouter des traces

## Checklist spec par etape

Avant de coder chaque etape, lire EN ENTIER ces sections du PDF :

| Etape | Sections spec (lire dans le PDF, pas les resumes) |
|-------|---------------------------------------------------|
| **5.0** | §9.3.4.3.1-6 (CABAC engine COMPLET — init, decision, renorm, bypass, terminate, **alignment §9.3.4.3.6**) |
| **5.1** | §8.3.1 (POC), §8.3.2 (RPS) |
| **5.2** | §8.3.4 (RefPicList construction) |
| **5.3** | §8.5.3.2.2-3 (merge spatial), §6.4.2 (PU availability) |
| **5.4** | §8.5.3.2.8-9 (TMVP collocated MV), §8.5.3.2.12 (MV scaling) |
| **5.5** | §8.5.3.2.4 (combined bi-pred), §8.5.3.2.5 (zero MV) |
| **5.6** | §8.5.3.2.6-7 (AMVP), §7.3.8.6 (prediction_unit syntax) |
| **5.7** | §8.5.3.3.3 (luma 8-tap interp, chroma 4-tap), §8.5.3.3.2 (chroma MV derivation) |
| **5.8** | §8.5.3.3.4 (weighted prediction — default + explicit) |
| **5.9** | §7.3.8.1 (slice_segment_data — boucle CTU + end_of_slice), §9.2.2 (WPP context) |
| **5.10** | §8.3.5 (collocated picture, NoBackwardPredFlag) |

## Etat actuel du code

Le code Phase 5 existe deja (5A-5D implementes). Bugs fixes cette session :
- **§9.3.4.3.6** : alignment bypass (`ivlCurrRange = 256`) manquant avant `coeff_sign_flag` et `coeff_abs_level_remaining` — causait desync bypass bins des le 14e bin
- **WPP** : `entropy_coding_sync` boundary handling manquant (end_of_subset + byte align + context save/restore)
- **numNeg** : floor division pour l'extension negative des references angulaires

Fichiers source :
- `inter_prediction.cpp` (~660 lignes) : merge, AMVP, TMVP, MC
- `interpolation.cpp` (~300 lignes) : filtres 8-tap/4-tap
- `dpb.cpp` (~440 lignes) : DPB, POC, RPS, ref lists

Le travail restant : **auditer le code existant contre la spec (avec la checklist ci-dessus), corriger les bugs, valider par oracle test**.
