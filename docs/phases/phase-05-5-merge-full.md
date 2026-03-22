# 5.5 -- Merge Mode Complet

## Objectif

Verifier la merge candidate list complete : spatial + temporal + combined bi-pred + zero MVs.

## Spec refs

- S8.5.3.2.1 : Merge candidate list construction (general)
- S8.5.3.2.3 : Combined bi-predictive merge candidates (B-slices only)
- S8.5.3.2.4 : Zero motion vector merge candidates (padding)
- Table 8-8 : combIdx mapping

## Code existant

`src/decoding/inter_prediction.cpp` : `derive_merge_mode()` complet

## Construction de la liste (ordre spec)

1. **Spatial candidates** (max 4, voir 5.3) : A1, B1, B0, A0, (B2 si < 4)
2. **Temporal candidate** (max 1, voir 5.4) : collocated MV
3. **Combined bi-pred** (B-slice only, Table 8-8) : paires de candidats L0/L1
4. **Zero MVs** (padding) : MV=(0,0) avec refIdx incrementaux

Total : `MaxNumMergeCand` candidats (typiquement 5).

## Pieges identifies

1. **Combined bi-pred** : seulement pour B-slices, et seulement si un candidat est
   uni-pred L0 et l'autre uni-pred L1 (ou on prend le MV L0 d'un bi-pred)
2. **Table 8-8 ordering** : l'ordre des paires est specifique (voir merge-table.md)
3. **Zero padding** : refIdx incremente de 0 a MaxNumMergeCand-1, pas seulement refIdx=0
4. **MaxNumMergeCand** : derive de `five_minus_max_num_merge_cand` dans le slice header
5. **Deduplication des zero MVs** : ne pas ajouter si deja present

## Audit a faire

1. Verifier l'ordre complet : spatial -> temporal -> combined -> zero
2. Verifier la condition `slice_type == B` pour combined bi-pred
3. Verifier Table 8-8 mapping dans le code
4. Verifier zero MV padding avec refIdx variable

## Test de validation

Trace comparative complete :
```bash
# Pour chaque PU merge, logger la liste complete :
# "merge_list PU(%d,%d): [0] mv=(%d,%d)/(%d,%d) ref=%d/%d pf=%d%d"
# Comparer avec HM
```

## Critere de sortie

- [ ] Merge list complete identique a HM pour 3 PUs du premier P-frame
- [ ] Merge list identique a HM pour 3 PUs d'un B-frame (combined bi-pred teste)
- [ ] Zero padding verifie
