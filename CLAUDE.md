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

# Tests unitaires (doivent TOUJOURS passer)
cd build && ctest --output-on-failure

# Tests oracle par phase (skip = pas encore implémenté, c'est normal)
cd build && ctest -L phase4 --output-on-failure
cd build && ctest -L phase5 --output-on-failure
cd build && ctest -L phase6 --output-on-failure
cd build && ctest -L oracle --output-on-failure    # tous les oracle tests

# Comparaison YUV pixel-perfect manuelle
python3 tools/oracle_compare.py ref.yuv test.yuv <width> <height>

# Lint
clang-tidy src/**/*.cpp -- -std=c++17
```

## Workflow de développement

### Procédure pour chaque phase

1. **Lire** : consulte le routing par phase (ci-dessous) pour savoir quels docs lire
2. **Coder** : implémente en suivant les tâches de `docs/phases/phase-XX-*.md`
3. **Tests unitaires** : écris des tests pour chaque fonction, lance `ctest --output-on-failure`
4. **Tests oracle** : quand ta phase est complète, les oracle tests de cette phase doivent passer
5. **Debug mismatch** : si un oracle test FAIL, suis la procédure de debug ci-dessous

### Tests oracle — comment ça marche

7 bitstreams de test sont dans `tests/conformance/fixtures/` avec des hash MD5 de référence (calculés depuis ffmpeg). Le script `tools/oracle_test.sh` :

1. Décode le bitstream avec `./build/hevc-torture <input> -o <output.yuv>`
2. Calcule le MD5 du YUV produit
3. Compare avec le MD5 de référence

| Résultat | Signification | Action |
|----------|---------------|--------|
| **Pass** | MD5 identique = pixel-perfect | Tout va bien |
| **Skip** | Le décodeur ne produit pas encore de YUV | Normal si la phase n'est pas implémentée |
| **Fail** | MD5 différent = mismatch pixel | Debugger (voir ci-dessous) |

### Jalons oracle par phase

| Phase terminée | Tests oracle qui doivent passer |
|----------------|-------------------------------|
| Phase 4 (Intra) | `oracle_i_64x64_qp22` |
| Phase 5 (Inter) | `oracle_p_qcif_10f`, `oracle_b_qcif_10f` |
| Phase 6 (Filters) | `oracle_i_64x64_deblock`, `oracle_i_64x64_sao`, `oracle_i_64x64_full`, `oracle_full_qcif_10f` |

Le test `oracle_full_qcif_10f` (label `milestone`) = **Main profile complet**. Quand il passe, le décodeur est conforme.

### Debugging d'un oracle FAIL

Quand un test oracle échoue (MD5 mismatch) :

```bash
# 1. Décoder avec hevc-torture
./build/hevc-torture tests/conformance/fixtures/i_64x64_qp22.265 -o /tmp/test.yuv

# 2. Décoder la référence avec ffmpeg
ffmpeg -y -i tests/conformance/fixtures/i_64x64_qp22.265 -pix_fmt yuv420p /tmp/ref.yuv

# 3. Comparer pixel par pixel
python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test.yuv 64 64

# Output exemple :
# FAIL: 1/1 frames differ
#   Frame 0: 42 pixels differ, max_diff=3, PSNR=45.2dB

# 4. Identifier le premier pixel faux -> localiser le CTU/CU
# 5. Ajouter du debug logging (HEVC_DEBUG) pour ce CU
# 6. Comparer les valeurs intermédiaires :
#    - prediction samples (avant résidu)
#    - residual (après transform inverse)
#    - reconstruction (avant filtres)
#    - après deblocking
#    - après SAO
```

Voir `docs/oracle/oracle-strategy.md` pour la stratégie complète de debugging.

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

## Routing par phase

Avant de travailler sur une phase, lis ces fichiers dans cet ordre :

| Phase | Fichiers à lire en premier |
|-------|---------------------------|
| **2 — Bitstream** | `BACKLOG.md` → `docs/phases/phase-02-bitstream.md` → `docs/spec/07-syntax/07-03-nal-unit.md` → `docs/spec/test-bitstreams.md` (sections 1-6) → `src/bitstream/nal_unit.h` (interface) |
| **3 — Parsing** | `BACKLOG.md` → `docs/phases/phase-03-parsing.md` → `docs/spec/07-syntax/07-04-parameter-sets.md` → `src/syntax/vps.h` + `sps.h` + `pps.h` + `slice_header.h` (interfaces) |
| **4 — Intra** | `BACKLOG.md` → `docs/phases/phase-04-intra.md` → `docs/spec/09-parsing.md` + `docs/spec/tables/cabac-*.md` → `docs/spec/08-decoding/08-05-intra-prediction.md` + `docs/spec/tables/intra-tables.md` → `docs/spec/08-decoding/08-06-transform-quant.md` + `docs/spec/tables/transform-matrices.md` |
| **5 — Inter** | `BACKLOG.md` → `docs/phases/phase-05-inter.md` → `docs/spec/08-decoding/08-03-reference-pictures.md` → `docs/spec/08-decoding/08-04-inter-prediction.md` + `docs/spec/tables/merge-table.md` |
| **6 — Filters** | `BACKLOG.md` → `docs/phases/phase-06-loop-filters.md` → `docs/spec/08-decoding/08-07-deblocking.md` → `docs/spec/08-decoding/08-08-sao.md` |

Pour la cross-reference complete (spec section → source → doc → test) : voir `docs/cross-reference.md`.

Toujours consulter `MASTER-PLAN.md` (section "Pièges de conformité") avant de commencer une phase.

## JIRA
N/A

## Conventions

- Nommer les fonctions/classes selon la spec (ex: `decode_inter_prediction_samples` → spec 8.5.3)
- Commenter chaque fonction avec la référence spec (ex: `// Spec 8.5.3.2 - Luma sample interpolation`)
- Un fichier source par section majeure de la spec
- Tests : un test par sous-section validée
