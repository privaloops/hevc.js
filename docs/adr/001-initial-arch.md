# ADR-001 : Architecture initiale du décodeur

## Statut
Accepté

## Date
2026-03-19

## Contexte

Nous construisons un décodeur HEVC (H.265) en C++17, compilé en WebAssembly. Le décodeur doit :
- Être conforme à la spec ITU-T H.265 v8 (2021)
- Produire des frames pixel-perfect par rapport à libde265
- Tourner dans un Web Worker pour un player web
- Supporter jusqu'à 4K@60fps (Main/Main 10/4:2:2/4:4:4, Level 5.1)

## Décision

### Architecture en modules

```
BitstreamReader (bits) → NAL Parser → Syntax Parser (VPS/SPS/PPS/Slice)
                                            │
                                     CABAC Decoder
                                            │
                              Coding Tree (CTU → CU → PU/TU)
                                            │
                                    ┌───────┴───────┐
                                    │               │
                              Intra Pred      Inter Pred
                                    │               │
                                    └───────┬───────┘
                                            │
                                   Transform Inverse
                                            │
                                    Reconstruction
                                            │
                                    Loop Filters
                                            │
                                      DPB / Output
```

### Choix techniques

1. **C++17** : templates pour le bit depth (8/10/16), constexpr pour les tables
2. **Pas d'allocation dynamique dans le hot path** : pools pré-alloués
3. **Séparation parsing / décodage** : le parsing (CABAC) produit des structures de données, le décodage les consomme
4. **API C pour WASM** : interface C pure, pas d'exceptions C++ dans l'API publique
5. **Build dual** : natif (pour les tests/debug) et WASM (pour la prod)

### Ce qu'on ne fait PAS

- Pas d'encodeur
- Pas de support multi-layer (nuh_layer_id != 0)
- Pas de HRD (Hypothetical Reference Decoder)
- Pas de SEI au-delà de decoded_picture_hash
- Pas de range extensions au-delà de 4:4:4 (pas de 12-bit, pas de cross-component prediction)

## Conséquences

- Le code suit la structure de la spec (un fichier par section majeure)
- Les noms de fonctions miroir la spec pour la traçabilité
- libde265 est utilisé comme oracle, jamais comme source de code
- Les phases sont incrémentales : chaque phase produit un décodeur fonctionnel pour un sous-ensemble de la spec
