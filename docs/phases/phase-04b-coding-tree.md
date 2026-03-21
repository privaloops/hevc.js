# Phase 4B — Coding Tree Structure

## Statut : A VALIDER

## Objectif

Valider que l'arbre de syntax elements (split/cbf/residual) est IDENTIQUE a HM.
Pas besoin de valider les valeurs decodees — juste la SEQUENCE et le TYPE de chaque bin.

## Pourquoi c'est critique

Un bin manquant ou en trop dans l'arbre decale TOUT le reste du bitstream.
C'est le **bug actuel** : 3 bins manquants pour CU (16,0) 8x8 entre notre decodeur et HM.
Le probleme est probablement dans `decode_transform_tree()` : split_transform_flag
ou cbf a la mauvaise profondeur.

## Spec refs

- §7.3.8.1 : slice_segment_data (boucle CTU + end_of_slice_segment_flag)
- §7.3.8.2 : coding_tree_unit
- §7.3.8.3-5 : coding_quadtree, coding_unit, prediction_unit
- §7.3.8.6 : transform_tree (split recursif, cbf chroma, cbf luma)
- §7.3.8.7 : transform_unit

## Cas a tester

### Cas 1 : CU 16x16, PART_2Nx2N, pas de split TU
- log2CbSize=4, log2TrafoSize=4
- Chroma TU = 8x8 (log2TrafoSizeC=3)
- Sequence : cbf_cb, cbf_cr, [split_transform_flag?], cbf_luma, residual_coding

### Cas 2 : CU 8x8, PART_2Nx2N, pas de split TU
- log2CbSize=3, log2TrafoSize=3
- Chroma TU = 4x4 (log2TrafoSizeC=2, deferred si blkIdx!=3)
- **C'est le cas qui pose probleme actuellement**
- Verifier : est-ce qu'un split_transform_flag est decode ? A quelle profondeur ?

### Cas 3 : CU 8x8, PART_NxN (IntraSplitFlag=1)
- 4 PUs de 4x4, TU split force a trafoDepth=0
- 4 sous-TU de 4x4 (log2TrafoSize=2)
- Chroma deferred : processChroma seulement a blkIdx=3
- Sequence : cbf_cb(depth0), cbf_cr(depth0), puis 4x [cbf_luma(depth1), residual]

### Cas 4 : CU 32x32 avec split TU
- log2CbSize=5, split_transform_flag=1
- 4 sous-TU de 16x16, chacun avec ses cbf

## Methode de validation

### Approche 1 : Test unitaire avec sequence de syntax elements

Creer un test qui :
1. Decode un bitstream avec notre decodeur
2. Log chaque syntax element decode : type, position, profondeur
3. Compare avec la meme liste extraite de HM

Format du log :
```
[SYN] split_cu_flag (0,0) depth=0 val=1 bin=0
[SYN] split_cu_flag (0,0) depth=1 val=1 bin=1
[SYN] split_cu_flag (0,0) depth=2 val=0 bin=2
[SYN] part_mode (0,0) 16x16 val=0 bin=3
...
```

### Approche 2 : Trace comparative (plus rapide a implementer)

1. Ajouter un fprintf dans chaque `decode_*` de `syntax_elements.cpp` avec le nom du SE
2. Ajouter la meme chose dans HM (`TDecSbac.cpp`)
3. Comparer les deux traces avec diff
4. Le premier SE different pointe exactement au bug

### Variables SPS critiques a verifier

```
max_transform_hierarchy_depth_intra  → maxTrafoDepth pour intra
max_transform_hierarchy_depth_inter  → maxTrafoDepth pour inter
MinTbLog2SizeY                       → condition split_transform_flag
MaxTbLog2SizeY                       → condition split_transform_flag
MinCbLog2SizeY                       → condition split_cu_flag / part_mode
```

## Taches

- [ ] Ajouter fprintf trace dans chaque `decode_*` de `syntax_elements.cpp`
- [ ] Ajouter la meme trace dans HM
- [ ] Comparer les traces pour i_64x64_qp22 sur les 400 premiers bins
- [ ] Identifier les SE manquants ou en trop
- [ ] Fixer le(s) bug(s) dans `decode_transform_tree()`
- [ ] Creer un test unitaire qui valide la sequence de SE pour au moins 2 CU

## Critere de sortie

- [ ] La trace de syntax elements (type + bin_number) est IDENTIQUE entre HM et notre decodeur pour les 500 premiers bins de i_64x64_qp22
- [ ] Les cas 1-4 ci-dessus sont couverts par le bitstream de test
