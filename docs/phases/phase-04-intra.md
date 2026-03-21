# Phase 4 — Intra Prediction (I-frames)

## Objectif

Decoder des frames I-only completes : CABAC + quad-tree + intra prediction + transform + reconstruction.
Pixel-perfect vs ffmpeg sur `i_64x64_qp22.265`.

## Prerequis

Phase 3 completee (parameter sets et slice headers parses).
HM reference decoder compile dans `/tmp/HM` avec tracing per-bin.

## Spec refs

- §9 : CABAC (tout le chapitre)
- §7.3.8-13 : Slice data syntax (CTU, CU, PU, TU, residual_coding)
- §8.4 : Intra prediction (35 modes)
- §8.6 : Transform inverse + scaling (dequant)

## Decoupe en sous-phases

La phase 4 est decoupee en **6 sous-phases independamment verifiables**.
Chaque sous-phase a ses propres tests unitaires qui valident SANS dependre de l'oracle full-frame.

| Sous-phase | Fichier | Statut | Critere de sortie |
|------------|---------|--------|-------------------|
| 4A — CABAC Engine | `phase-04a-cabac.md` | **Fait** | 7 tests unitaires passent |
| 4B — Coding Tree | `phase-04b-coding-tree.md` | **A valider** | Sequence de syntax elements identique a HM |
| 4C — Residual Contexts | `phase-04c-residual-contexts.md` | **A valider** | Tous les `derive_*_ctx()` matchent HM |
| 4D — Coefficient Parsing | `phase-04d-coefficient-parsing.md` | **A valider** | Coefficients TU identiques a HM |
| 4E — Transform + Dequant | `phase-04e-transform-dequant.md` | **Fait** | Tests sur matrices connues |
| 4F — Prediction + Recon | `phase-04f-prediction-recon.md` | **Fait** | 3 toy tests pixel-perfect |

## Ordre d'execution

```
4A (CABAC)  ──→  4B (Coding Tree)  ──→  4C (Residual Contexts)
                                              │
                                              ▼
                                        4D (Coefficient Parsing)
                                              │
                 4E (Transform) ◄─────────────┘
                       │
                       ▼
                 4F (Prediction + Recon)
                       │
                       ▼
                 Oracle: i_64x64_qp22 pixel-perfect
```

## Principe : valider chaque couche AVANT d'integrer

Le piege de la Phase 4 est le couplage : une erreur de contexte CABAC corrompt l'etat arithmetique, ce qui fait diverger TOUS les bins suivants. Debugger au niveau oracle (YUV pixel-perfect) est alors extremement lent.

La solution : valider chaque couche en isolation avec des tests unitaires qui comparent directement avec HM, AVANT de lancer le decode complet.

## Critere de sortie final

- `oracle_i_64x64_qp22` passe (MD5 match)
- Tous les tests unitaires des sous-phases passent
- `ctest -L phase4 --output-on-failure` : 0 failures
