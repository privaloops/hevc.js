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
├── bitstream/      # BitstreamReader, NAL unit parsing, RBSP, Exp-Golomb
├── syntax/         # VPS, SPS, PPS, slice header, slice data (à implémenter)
├── decoding/       # Intra, inter, transform, quant (à implémenter)
├── filters/        # Deblocking, SAO (à implémenter)
├── common/         # Types (Pixel, MV, NalUnitType, enums), Picture
└── wasm/           # JS bindings, worker interface (à implémenter)
tests/
├── unit/           # Tests unitaires par module (16 tests BitstreamReader)
├── conformance/    # Bitstreams de conformité HEVC (à télécharger)
└── oracle/         # Comparaison frame-by-frame vs libde265
tools/
├── oracle_compare.py   # Script de comparaison YUV (opérationnel)
└── fetch_conformance.sh # Téléchargement bitstreams de test (à créer)
docs/
├── spec/           # Notes par chapitre de la spec H.265
│   ├── pdf/        # PDF de la spec ITU-T (gitignored, source de vérité)
│   └── tables/     # Tables de données extraites (CABAC, DCT, filtres, etc.)
├── phases/         # Plan d'implémentation par phase (9 phases)
├── oracle/         # Stratégie de test oracle
└── adr/            # Architecture Decision Records
MASTER-PLAN.md      # Plan global avec dépendances et 23 pièges de conformité
DECISIONS.md        # Décisions d'architecture (AD-001 à AD-006)
```

## Profils cibles (par ordre d'implémentation)

1. Main (8-bit 4:2:0) — Phase 4-6
2. Main 10 (10-bit 4:2:0) — Phase 7
3. Main 4:2:2 10 — Phase 7
4. Main 4:4:4 — Phase 7
5. Niveaux jusqu'à 5.1 (4K@60fps) — Phase 7

## Spec de référence

ITU-T H.265 (V8, 08/2021) — "High Efficiency Video Coding"

### PDF de la spec (source de vérité)

Le PDF officiel est stocké localement dans `docs/spec/pdf/` (gitignored).

**IMPORTANT** : Quand tu implémentes une section de la spec, **lis le PDF** en priorité. Les notes dans `docs/spec/` sont des résumés — le PDF fait autorité en cas de divergence.

```
docs/spec/pdf/T-REC-H.265-202108-S.pdf    # Spec complète (716 pages, 12 Mo)
```

Pour lire une section spécifique, utilise `pdftotext` via Bash avec un range de pages :
```bash
pdftotext -f 200 -l 210 docs/spec/pdf/T-REC-H.265-202108-S.pdf - 2>/dev/null
```

Si le PDF n'est pas présent, le télécharger :
```bash
mkdir -p docs/spec/pdf && curl -sL -o docs/spec/pdf/T-REC-H.265-202108-S.pdf \
  "https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-H.265-202108-S!!PDF-E&type=items"
```

### Tables de données extraites

Les valeurs numériques critiques (tables CABAC, matrices DCT, coefficients de filtre, etc.) sont dans `docs/spec/tables/` sous forme de code C++ directement copiable :

| Fichier | Contenu |
|---------|---------|
| `tables/cabac-arithmetic.md` | rangeTabLps[64][4], transIdxMps[64], transIdxLps[64], algorithmes decode |
| `tables/cabac-init-values.md` | Toutes les tables d'init contextes (Tables 9-5 à 9-31) pour I/P/B |
| `tables/transform-matrices.md` | DCT 8x8, 16x16, 32x32 complètes + partial butterfly |
| `tables/intra-tables.md` | intraPredAngle[35], invAngle[35], filtrage, MPM |
| `tables/scaling-list-defaults.md` | Matrices par défaut 4x4/8x8 intra+inter, fallback |
| `tables/merge-table.md` | Table 8-8 (combined bi-pred candidates) |

### Chapitres clés

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
