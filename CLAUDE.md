# HEVC Decode - Décodeur HEVC/H.265 en C++ (WASM)

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
| Oracle | ffmpeg (décode la référence YUV) + MD5 pixel-perfect |
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

1. **Consulter BACKLOG.md** : voir les tâches restantes et le statut actuel
2. **Lire MASTER-PLAN.md** section "Pièges de conformité" : vérifier les pièges pour cette phase
3. **Lire DECISIONS.md** : respecter les choix d'architecture (types, layouts, ownership)
4. **Lire les docs de la phase** : consulte le routing par phase (ci-dessous) pour la liste complète
5. **Respecter les interfaces existantes** : les headers `.h` dans `src/` définissent les structs et signatures — les utiliser, ne pas en créer de nouvelles incompatibles
6. **Coder** : implémente en suivant les tâches de `docs/phases/phase-XX-*.md`
7. **Tests unitaires** : écris des tests pour chaque fonction, lance `ctest --output-on-failure`
8. **Tests oracle** : quand ta phase est complète, les oracle tests de cette phase doivent passer
9. **Debug mismatch** : si un oracle test FAIL, suis la procédure de debug ci-dessous
10. **Mettre à jour BACKLOG.md** : cocher les tâches terminées, ajouter les nouvelles si identifiées

### Règle d'implémentation depuis la spec

> **ABSOLU** : Chaque fonction de décodage DOIT être une transcription directe de la spec.

1. **Identifier** la section spec exacte (§X.Y.Z) ET toutes ses sous-sections et processus invoqués
2. **Lire le PDF** de la section ET des processus invoqués (pas les notes, pas les résumés, pas HM)
3. **Lister** tous les sous-processus invoqués (ex: §7.3.8.11 invoque §9.3.4.3.6 pour l'alignment bypass)
4. **Transcrire** les formules, conditions et boucles telles qu'écrites dans la spec — ne pas "simplifier", "optimiser" ou "interpréter"
5. **Vérifier la checklist** : chaque sous-processus de l'étape 3 est-il implémenté ? (oui/non, pas "semble correct")
6. **Nommer** les variables comme dans la spec (ex: `scanIdx`, `LastSignificantCoeffX`, `ctxInc`)
7. **Commenter** chaque bloc avec la référence spec exacte (ex: `// Spec eq 9-55`, `// §7.3.8.11 line 3`)
8. **Vérifier avec HM** uniquement quand le résultat de la spec semble ambigu ou quand un test échoue — et documenter la divergence dans LEARNINGS.md
9. **Ne jamais** combiner des parties de la spec avec des parties de HM dans la même fonction — choisir UNE source et s'y tenir

Violation type : "simplifier `if (log2TrafoSize == 2 || (log2TrafoSize == 3 && cIdx > 0))` au lieu de transcrire la vraie condition de la spec" → introduit des bugs silencieux qui se propagent dans tout le bitstream.

### Règle anti-debug itératif

> **INTERDIT** : boucle de debug empirique (hypothèse → trace → test → échec → nouvelle hypothèse).
> Ce pattern a causé des heures perdues en Phase 5. Maximum **2 itérations** de debug empirique.

Après 2 échecs de debug :
1. **STOP** — arrêter le debug, ne pas ajouter de traces supplémentaires
2. **Relire la spec** : ouvrir le PDF et lire la section COMPLÈTE correspondante à la fonction buggée, y compris les sections voisines et les processus invoqués (souvent le bug est dans un processus invoqué qu'on n'a pas implémenté, ex: §9.3.4.3.6)
3. **Checklist des processus** : pour chaque processus invoqué par la section spec, vérifier qu'il est implémenté dans le code (pas "semble implémenté" — vérifier ligne par ligne)
4. **Si toujours bloqué** → demander à l'utilisateur avant de continuer

Exemples de bugs trouvés par relecture de spec (pas par debug) :
- §9.3.4.3.6 : alignment bypass (`ivlCurrRange = 256`) avant `coeff_sign_flag` et `coeff_abs_level_remaining` — une seule ligne dans la spec, invisible au debug empirique
- §9.2.2 : WPP context save/restore au 2e CTU de chaque rangée — jamais visible en comparant des pixels

### Tests oracle — comment ça marche

12 bitstreams de test sont dans `tests/conformance/fixtures/` avec des hash MD5 de référence (calculés depuis ffmpeg). Le script `tools/oracle_test.sh` :

1. Décode le bitstream avec `./build/hevc-decode <input> -o <output.yuv>`
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
| Phase 6+ (Realworld) | `oracle_bbb1080_50f`, `oracle_bbb4k_25f` |

Le test `oracle_full_qcif_10f` (label `milestone`) = **Main profile complet**. Quand il passe, le décodeur est conforme.
Les tests `oracle_bbb1080_50f` et `oracle_bbb4k_25f` valident sur du contenu réel (Big Buck Bunny).

### Debugging d'un oracle FAIL

> **IMPORTANT** : Appliquer la règle anti-debug itératif ci-dessus. Ne PAS partir en boucle de traces.

**Étape 1 — Localiser** (1 itération max) :
```bash
./build/hevc-decode <input> -o /tmp/test.yuv
ffmpeg -y -i <input> -pix_fmt yuv420p /tmp/ref.yuv
python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test.yuv <W> <H>
```

**Étape 2 — Relire la spec** (PAS debugger) :
1. Identifier quelle fonction produit le premier pixel faux (prediction? transform? CABAC?)
2. Ouvrir le PDF de la section spec correspondante
3. Lister TOUS les processus invoqués par cette section
4. Vérifier que chaque processus est implémenté (checklist oui/non)
5. Le bug est presque toujours un processus invoqué non implémenté ou mal transcrit

**Étape 3 — Comparer avec HM** (seulement si étape 2 ne suffit pas) :
- HM est dans `/tmp/HM/`, binaire : `/tmp/HM/bin/umake/clang-17.0/x86_64/release/TAppDecoder`
- Comparer les decision bins CABAC (pas les bypass, ils sont trop nombreux)
- Si les decisions divergent, le bug est dans le parsing. Si elles matchent, le bug est dans la reconstruction.

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
├── conformance/
│   └── fixtures/   # 12 bitstreams de test + README avec hash MD5 de référence
└── oracle/         # Comparaison frame-by-frame
tools/
├── oracle_compare.py   # Comparaison YUV pixel-perfect (opérationnel)
└── oracle_test.sh      # Test oracle CTest : decode → MD5 → compare (opérationnel)
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

**IMPORTANT** : Quand tu implémentes une section de la spec, **lis le PDF** et transcris les formules directement. Ne pas paraphraser, ne pas simplifier, ne pas deviner. Les notes dans `docs/spec/` sont des résumés — le PDF fait autorité en cas de divergence. HM (`/tmp/HM/`) est la référence de conformité — le consulter uniquement pour lever une ambiguïté du texte de la spec, jamais comme source primaire d'implémentation.

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

### Index des pages du PDF

Pour lire une section avec `pdftotext -f <debut> -l <fin> docs/spec/pdf/T-REC-H.265-202108-S.pdf -` :

| Section | Sujet | Pages PDF |
|---------|-------|-----------|
| §6 | Bitstream and picture formats | 34-43 |
| §7.1-7.2 | Syntax general, more_rbsp_data | 44-53 |
| §7.3.1 | NAL unit syntax | 54-56 |
| §7.3.2 | Parameter sets (VPS, SPS, PPS) | 54-66 |
| §7.3.6 | Slice segment header | 66-71 |
| §7.3.8 | Slice data (CTU, CU, PU, TU, residual) | 71-100 |
| §7.4 | Semantics (valeurs dérivées, inferred) | 100-136 |
| §8.1 | General decoding process | 137-138 |
| §8.3 | Reference pictures, POC, DPB | 139-145 |
| §8.4 | Intra prediction (35 modes) | 146-162 |
| §8.5 | Inter prediction (merge, AMVP, interpolation) | 163-195 |
| §8.6 | Transform inverse + dequant | 196-203 |
| §8.7 | Loop filters (deblocking + SAO) | 204-224 |
| §9.1-9.2 | CABAC init, context tables | 225-237 |
| §9.3 | CABAC binarization, arithmetic decoding | 238-247 |
| Annexe A | Profiles, tiers, levels | 248-271 |
| Annexe B | Byte stream format | 272-273 |

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
