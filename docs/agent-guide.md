# Guide Agent — HEVC Torture

Guide condensé pour les agents codeurs. Lis ce fichier AVANT de coder.

## Ordre des opérations

Chaque phase est **séquentielle**. Ne commence pas la phase N+1 tant que la N ne passe pas les tests.

```
Phase 2 (Bitstream/NAL) → Phase 3 (Parsing) → Phase 4 (Intra) → Phase 5 (Inter) → Phase 6 (Filters)
```

## Avant de coder une phase

1. Lis `BACKLOG.md` — état d'avancement, tâches restantes
2. Lis `docs/phases/phase-XX-*.md` — tâches détaillées
3. Lis `MASTER-PLAN.md` section "Pièges de conformité" — les erreurs connues
4. Lis `DECISIONS.md` — conventions d'architecture
5. Lis les headers existants (`src/`) — les interfaces sont déjà définies
6. Lis la spec PDF quand un doute surgit : `pdftotext -f <page> -l <page> docs/spec/pdf/T-REC-H.265-202108-S.pdf -`

## Structure du code

```
src/bitstream/   → BitstreamReader, NAL parser (Phase 2)
src/syntax/      → VPS, SPS, PPS, SliceHeader (Phase 3)
src/decoding/    → CABAC, Intra, Inter, Transform (Phase 4-5)
src/filters/     → Deblocking, SAO (Phase 6)
src/common/      → Types, Picture, debug logging
```

## Conventions

- Nommer les fonctions comme la spec : `decode_inter_prediction_samples` → spec §8.5.3
- Commenter chaque fonction : `// Spec §8.5.3.2 — Luma sample interpolation`
- Un fichier source par section majeure de la spec
- `Pixel = uint16_t` partout (AD-001)
- Picture en layout planaire Y/Cb/Cr séparés (AD-002)
- Parameter sets stockés par ID dans des `std::array<std::optional<T>>` (AD-003)

## Debug logging

Utilise `HEVC_LOG(CATEGORY, "format", args...)` pour tracer les valeurs intermédiaires.

```cpp
#include "common/debug.h"

HEVC_LOG(CABAC, "decode_decision ctxIdx=%d pStateIdx=%d valMps=%d → bin=%d",
         ctx_idx, ctx.pStateIdx, ctx.valMps, bin_val);
HEVC_LOG(INTRA, "pred mode=%d block %dx%d at (%d,%d)",
         mode, w, h, x0, y0);
HEVC_LOG(TRANSFORM, "coeff[%d][%d] = %d (after dequant)", row, col, val);
```

Catégories : BITSTREAM, NAL, PARSE, CABAC, TREE, INTRA, INTER, TRANSFORM, QUANT, FILTER, RECON, DPB.

Filtrage runtime : `HEVC_DEBUG_FILTER=CABAC,INTRA ./hevc-torture input.265`

## Comment debugger un oracle FAIL

Quand le MD5 du YUV ne matche pas :

```bash
# 1. Décoder avec hevc-torture
./build/hevc-torture tests/conformance/fixtures/i_64x64_qp22.265 -o /tmp/test.yuv

# 2. Décoder la référence avec ffmpeg
ffmpeg -y -i tests/conformance/fixtures/i_64x64_qp22.265 -pix_fmt yuv420p /tmp/ref.yuv

# 3. Comparer pixel par pixel
python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test.yuv 64 64

# 4. Activer le debug sur la catégorie suspecte
HEVC_DEBUG_FILTER=CABAC,INTRA ./build/hevc-torture input.265 -o /tmp/test.yuv 2>/tmp/debug.log

# 5. Chercher le premier pixel faux dans le log
# Le PSNR et la position du premier mismatch pointent vers le CTU/CU problématique
```

## Erreurs typiques par phase

### Phase 2 — Bitstream
- Oublier les emulation prevention bytes dans le RBSP
- Suffix SEI qui crée un faux AU boundary (il appartient au même AU)
- `more_rbsp_data()` ne doit PAS juste vérifier "reste-t-il des bits"

### Phase 3 — Parsing
- Oublier les valeurs dérivées après parsing du SPS (appeler `derive()`)
- Short-term RPS : les delta_poc sont cumulatifs, pas absolus
- Scaling list fallback : les matrices par défaut 8x8+ ne sont PAS flat 16

### Phase 4 — Intra (la plus dure)
- **CABAC `sig_coeff_flag` context** (§9.3.4.2.8) — le contexte dépend de la position dans le sub-block ET des `coded_sub_block_flag` des voisins. C'est le bug #1.
- **`cRiceParam` adaptatif** — commence à 0 par sub-block, incrémente quand `baseLevel > 3 * (1 << cRiceParam)`. Sans ça, grands coefficients = mismatch.
- **Clipping inter-passe transform** — après la passe verticale, clipper à [-32768, 32767]. Sans ça = overflow dans la passe horizontale.
- **Transposition angular** — modes 2-17 doivent transposer les samples de référence, appliquer le mode miroir, puis transposer le résultat.
- **`end_of_slice_segment_flag`** — décodé via `decode_terminate()` après CHAQUE CTU.
- **PCM byte alignment** — aligner le bitstream reader avant de lire les samples PCM.
- **`cabac_init_flag`** — permute les tables d'init P↔B.
- **QP derivation** — le QP prédicteur est la moyenne des QP des CU voisins (gauche et dessus), PAS le QP du CU précédent.

### Phase 5 — Inter
- **MV scaling** pour les candidats temporels (TMVP) — ne pas oublier le facteur d'échelle POC.
- **Shifts interpolation** — les shifts sont fixés par la spec, pas par le bit depth du sample.
- **`HandleCraAsBlaFlag`** — nécessaire pour le random access.

### Phase 6 — Filters
- **Boundary strength bi-pred** — la formule pour les B-frames est différente de celle des P-frames.
- **SAO edge offset** — les 4 classes d'edge utilisent des offsets signés.

## Tests

```bash
# Build debug
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# Tests unitaires
cd build && ctest --output-on-failure

# Oracle par phase
ctest -L phase4 --output-on-failure
ctest -L phase5 --output-on-failure
ctest -L oracle --output-on-failure

# Tous les tests
ctest --output-on-failure
```

## Données de référence CABAC

Pour générer des données de test CABAC depuis un bitstream :

```bash
python3 tools/extract_cabac_reference.py tests/conformance/fixtures/i_64x64_qp22.265
# Produit tests/conformance/reference_data/i_64x64_qp22.h
# Contient les RBSP bytes de chaque NAL unit, prêts pour les tests unitaires
```

## Bitstreams jouets (ultra-simples)

Pour générer des bitstreams 16x16 pour le debug step-by-step :

```bash
bash tools/gen_toy_bitstreams.sh
# Produit tests/conformance/fixtures/toy_*.265
```

Ces bitstreams ont 1 seul CTU, ce qui rend le debug CABAC/intra beaucoup plus simple.
