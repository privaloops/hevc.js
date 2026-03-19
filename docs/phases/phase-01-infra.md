# Phase 1 — Infrastructure

## Objectif
Mettre en place le projet, la toolchain, le CI, et l'oracle de test.

## Prérequis
Aucun (première phase).

## Tâches

### 1.1 — Initialisation du projet
- [ ] Structure de dossiers (src/, tests/, tools/, docs/)
- [ ] CMakeLists.txt principal avec options :
  - `BUILD_TESTS` (ON par défaut)
  - `BUILD_WASM` (OFF par défaut)
  - `HEVC_DEBUG` (ON en Debug)
- [ ] `.gitignore` (build/, *.o, *.wasm, *.yuv)
- [ ] README.md minimal

### 1.2 — Toolchain C++
- [ ] C++17 standard
- [ ] Clang ou GCC comme compilateur natif
- [ ] Warnings stricts : `-Wall -Wextra -Wpedantic -Werror`
- [ ] AddressSanitizer en mode Debug
- [ ] Emscripten toolchain file pour WASM

### 1.3 — Google Test
- [ ] FetchContent ou submodule pour Google Test
- [ ] CMake `add_test` setup
- [ ] Premier test trivial qui passe

### 1.4 — Oracle libde265
- [ ] Vérifier que `dec265` est installé
- [ ] Script `tools/oracle_compare.py` — comparaison YUV pixel-perfect
- [ ] Script `tools/fetch_conformance.sh` — téléchargement bitstreams de test
- [ ] Test CTest `oracle_smoke` qui vérifie que le script fonctionne

### 1.5 — CI GitHub Actions
- [ ] Workflow `build.yml` : build natif (Linux, macOS)
- [ ] Workflow `build-wasm.yml` : build Emscripten
- [ ] Workflow `test.yml` : exécution des tests unitaires
- [ ] Cache des dépendances (Google Test, Emscripten)

### 1.6 — Structures de données fondamentales
- [ ] `BitstreamReader` : lecture de bits depuis un buffer
  - `read_bits(n)`, `read_u(n)`, `read_ue()`, `read_se()`, `read_flag()`
  - `byte_aligned()`, `more_rbsp_data()`
  - Tests unitaires complets
- [ ] Types de base : `Pixel` (template 8/16 bit), dimensions, coordonnées
- [ ] `Picture` : buffer YUV avec stride, bit depth, chroma format

## Critère de sortie

- Le projet compile en natif et WASM sans erreurs
- Les tests passent (y compris le test BitstreamReader)
- L'oracle fonctionne avec un fichier YUV de test
- Le CI est vert

## Validation oracle
Pas de comparaison pixel à ce stade. Validation par tests unitaires seulement.

## Estimation de complexité
Modérée. Principalement du setup, mais le BitstreamReader et Picture sont fondamentaux.
