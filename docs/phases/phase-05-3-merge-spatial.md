# 5.3 -- Merge Candidates (Spatial)

## Objectif

Verifier que les 5 candidats spatiaux du merge mode sont derives correctement.

## Spec refs

- S8.5.3.2.2 : Derivation process for spatial merge candidates
- Figure 8-5 : Positions des voisins (A0, A1, B0, B1, B2)

## Code existant

`src/decoding/inter_prediction.cpp` : partie merge spatial dans `derive_merge_mode()`

## Positions des voisins (Figure 8-5)

```
                B2      B1    B0
                 +-------+-----+
                 |             |
                 |   current   |
                 |     PU      |
                 |             |
            A0---+-------+-----+
            A1   |
```

Coordonnees exactes (spec) :
- A1 : (xPb - 1, yPb + nPbH - 1)
- B1 : (xPb + nPbW - 1, yPb - 1)
- B0 : (xPb + nPbW, yPb - 1)
- A0 : (xPb - 1, yPb + nPbH)
- B2 : (xPb - 1, yPb - 1)

## Pieges identifies

1. **Ordre de test** : A1 -> B1 -> B0 -> A0 -> B2 (PAS alphabetique !)
2. **B2 est conditionnel** : seulement si < 4 candidats apres les 4 premiers
3. **Deduplication** : chaque candidat est ignore s'il a le meme MV+refIdx qu'un precedent
4. **Disponibilite** : verifier slice boundary, tile boundary, et picture boundary
5. **PU partition boundary** : pour les modes asymetriques (AMP), certains voisins sont
   dans la MEME CU -- ils doivent etre exclus si c'est le cas
6. **Off-by-one** : les positions utilisent `nPbH - 1` et `nPbW - 1`, pas `nPbH`/`nPbW`

## Audit a faire

1. Lire S8.5.3.2.2 du PDF (pages 167-172)
2. Verifier chaque position de voisin dans le code
3. Verifier la condition de deduplication (refIdx + MV comparison)
4. Verifier les conditions d'exclusion pour les PU intra-CU (PART_Nx2N, PART_2NxN, AMP)
5. Comparer les 5 candidats vs HM pour 5 PUs choisis

## Test de validation

```cpp
// Test unitaire : deriver les candidats spatiaux pour un PU (32,32) 16x16
// dans un frame ou les voisins ont des MV connus
// Verifier : nombre de candidats, MV et refIdx de chaque candidat
```

Trace comparative :
```bash
# Ajouter un log avant chaque merge :
# "merge_cand[i] pos=A1 mv=(%d,%d) ref=%d"
# Comparer avec HM
```

## Critere de sortie

- [ ] 5 candidats spatiaux identiques a HM pour le premier PU merge du premier P-frame
- [ ] Deduplication correcte (verifier un cas ou A1 == B1)
- [ ] Cas B2 conditionnel verifie
