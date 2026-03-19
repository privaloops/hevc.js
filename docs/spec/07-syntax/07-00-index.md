# Chapitre 7 — Syntax and Semantics (Index)

Ce chapitre est le coeur du parsing HEVC. Chaque sous-section correspond à une structure syntaxique à parser depuis le bitstream.

## Structure du chapitre

| Section | Fichier | Contenu | Phase |
|---------|---------|---------|-------|
| §7.3.1 | `07-03-nal-unit.md` | NAL unit syntax | 2 |
| §7.3.2-4 | `07-04-parameter-sets.md` | VPS, SPS, PPS | 3 |
| §7.3.6 | `07-05-slice-header.md` | Slice segment header | 3 |
| §7.3.8.1 | `07-08-slice-segment-data.md` | Boucle slice_segment_data(), end_of_slice_segment_flag | 4 |
| §7.3.8-11 | `07-06-slice-data.md` | CTU, CU, PU, TU | 4-5 |
| §7.3.7 | `07-07-sei.md` | SEI messages | Optionnel |

## Convention de lecture de la spec

La spec utilise des fonctions de lecture du bitstream :

| Notation spec | Fonction C++ | Description |
|--------------|-------------|-------------|
| `f(n)` | `read_bits(n)` | n bits fixed-length |
| `u(n)` | `read_u(n)` | unsigned integer, n bits |
| `i(n)` | `read_i(n)` | signed integer, n bits (2's complement) |
| `ue(v)` | `read_ue()` | unsigned Exp-Golomb |
| `se(v)` | `read_se()` | signed Exp-Golomb |
| `ae(v)` | `cabac_decode()` | CABAC-coded (context-adaptive) |
| `b(8)` | `read_byte()` | byte-aligned byte |

## Descripteurs conditionnels

La spec utilise abondamment des conditions de présence. Règle : **si un syntax element n'est pas présent, sa valeur inferred est définie dans la semantics (§7.4)**.

Exemple :
```
if( condition )
    syntax_element    ue(v)
```
Si `condition` est faux, `syntax_element` prend sa valeur inferred (souvent 0).

## Approche d'implémentation

1. Traduire le pseudo-code de la spec en C++ ligne par ligne
2. Chaque syntax element est stocké dans une struct
3. Les valeurs dérivées (§7.4) sont calculées immédiatement après parsing
4. Les valeurs inferred sont initialisées dans le constructeur de la struct
