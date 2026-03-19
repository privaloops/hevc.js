# HEVC Torture - Décodeur HEVC/H.265 en C++ (WASM)

Décodeur HEVC conforme à la spec ITU-T H.265 (v8, 2021), compilé en WebAssembly pour intégration dans un player web.

## mdma
- **Workflow** : `default`
- **Git** : `default`

## Stack

| Composant | Technologie |
|-----------|-------------|
| Langage | C++17 |
| Build | CMake + Emscripten |
| WASM | Emscripten SDK |
| Tests | Google Test + CTest |
| Oracle | libde265 (comparaison pixel-perfect) |
| CI | GitHub Actions |

## Commandes

```bash
# Build natif (debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# Build WASM
emcmake cmake -B build-wasm && cmake --build build-wasm

# Tests
cd build && ctest --output-on-failure

# Oracle comparison
python3 tools/oracle_compare.py --input test.265 --decoder ./build/hevc-torture --oracle libde265

# Lint
clang-tidy src/**/*.cpp -- -std=c++17
```

## Structure

```
src/
├── bitstream/      # NAL unit parsing, RBSP, Exp-Golomb
├── syntax/         # VPS, SPS, PPS, slice header, slice data
├── decoding/       # Intra, inter, transform, quant
├── filters/        # Deblocking, SAO
├── common/         # Types, buffers, picture management
└── wasm/           # JS bindings, worker interface
tests/
├── unit/           # Tests unitaires par module
├── conformance/    # Bitstreams de conformité HEVC
└── oracle/         # Comparaison frame-by-frame vs libde265
tools/
├── oracle_compare.py   # Script de comparaison YUV
└── fetch_conformance.sh # Téléchargement bitstreams de test
docs/
├── spec/           # Notes par chapitre de la spec H.265
├── phases/         # Plan d'implémentation par phase
├── oracle/         # Stratégie de test oracle
└── adr/            # Architecture Decision Records
```

## Profils cibles (par ordre d'implémentation)

1. Main (8-bit 4:2:0) — Phase 4-6
2. Main 10 (10-bit 4:2:0) — Phase 7
3. Main 4:2:2 10 — Phase 7
4. Main 4:4:4 — Phase 7
5. Niveaux jusqu'à 5.1 (4K@60fps) — Phase 7

## Spec de référence

ITU-T H.265 (V8, 08/2021) — "High Efficiency Video Coding"

Les chapitres clés :
- **Ch. 7** : Syntax and semantics (NAL, parameter sets, slice)
- **Ch. 8** : Decoding process (prediction, transform, filters)
- **Ch. 9** : Parsing process (CABAC, binarization)

## JIRA
N/A

## Conventions

- Nommer les fonctions/classes selon la spec (ex: `decode_inter_prediction_samples` → spec 8.5.3)
- Commenter chaque fonction avec la référence spec (ex: `// Spec 8.5.3.2 - Luma sample interpolation`)
- Un fichier source par section majeure de la spec
- Tests : un test par sous-section validée
