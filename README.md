# hevc-torture

Décodeur HEVC/H.265 en C++17 compilé en WebAssembly, conforme à la spec ITU-T H.265 (v8, 2021).

## Objectif

Implémentation from-scratch d'un décodeur HEVC, validé pixel-perfect contre libde265 comme oracle de référence. Conçu pour tourner dans un player web via WebAssembly.

## Profils supportés

- Main (8-bit 4:2:0)
- Main 10 (10-bit 4:2:0)
- Main 4:2:2 10
- Main 4:4:4
- Niveaux jusqu'à 5.1 (4K@60fps)

## Build

### Natif
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### WebAssembly
```bash
emcmake cmake -B build-wasm
cmake --build build-wasm
```

### Tests
```bash
cd build && ctest --output-on-failure
```

## Documentation

- [MASTER-PLAN.md](MASTER-PLAN.md) — Plan d'implémentation complet
- [docs/spec/](docs/spec/) — Notes par chapitre de la spec H.265
- [docs/phases/](docs/phases/) — Détail de chaque phase
- [docs/oracle/](docs/oracle/) — Stratégie de validation
- [docs/adr/](docs/adr/) — Décisions d'architecture

## Licence

TBD
