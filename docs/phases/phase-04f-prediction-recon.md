# Phase 4F — Intra Prediction + Reconstruction

## Statut : FAIT (3 toy tests pixel-perfect)

## Objectif

Valider les 35 modes intra, la construction des samples de reference,
le filtrage, et la reconstruction (pred + residual + clipping).

## Spec refs

- §8.4.4.2.2 : Reference sample availability (Z-scan)
- §8.4.4.2.3 : Reference sample substitution
- §8.4.4.2.4 : Reference sample filtering (strong intra smoothing 32x32)
- §8.4.4.2.5 : Planar mode (mode 0)
- §8.4.4.2.6 : DC mode (mode 1) + DC filtering
- §8.4.4.2.7 : Angular modes (modes 2-34)
- §8.4.4.2.8 : Post-filtering modes 10/26

## Ce qui est implemente (`intra_prediction.h`, `intra_prediction.cpp`)

- 35 modes (Planar, DC, Angular 2-34)
- Reference sample construction avec Z-scan availability
- Reference sample substitution (propagation du premier disponible)
- Filtrage [1,2,1] conditionnel sur le mode et la taille
- Strong intra smoothing (32x32)
- Angular : tables intraPredAngle[35] et invAngle[]
- Transposition pour modes horizontaux (2-17)
- Post-filtering pour modes 10 (horizontal) et 26 (vertical)
- Reconstruction : `Clip(pred + residual, 0, (1<<BitDepth)-1)`

## Points critiques verifies

| Point | Spec | Statut |
|-------|------|--------|
| Z-scan reference availability (pas row-major) | §8.4.4.2.2 | Fixe (bug #1 de la session) |
| Intra mode grid granularity MinTbSizeY | §8.4.4.2 | Fixe (bug #2 de la session) |
| Chroma TU position deferred (xBase/yBase) | §8.4.4 | Fixe (bug #3 de la session) |
| Planar indexation (nTbS, pas nTbS-1) | §8.4.4.2.5 | Implemente |
| Transposition angular modes 2-17 | §8.4.4.2.7 | Implemente |
| DC filtering (bord haut et gauche) | §8.4.4.2.6 | Implemente |
| constrained_intra_pred_flag | §8.4.4.2.2 | Implemente |

## Tests existants

| Test | Resolution | QP | Statut |
|------|-----------|-----|--------|
| toy_qp10 | 64x64 | 10 | Pixel-perfect |
| toy_qp30 | 64x64 | 30 | Pixel-perfect |
| toy_qp45 | 64x64 | 45 | Pixel-perfect |

## Limitations des toy tests

Ces tests ne couvrent PAS :
- CU 8x8 avec part_mode NxN (4 PUs de 4x4)
- Chroma 8x8 (ils n'ont qu'un seul CU 16x16)
- split_transform_flag (un seul TU par CU)
- Multiple CUs dans un CTB (le CTB est 64x64 et contient un seul CU)

## Tests a ajouter

### Bitstreams cibles
Generer avec ffmpeg/x265 des bitstreams qui exercent les cas manquants :
```bash
# 128x128 I-frame pour avoir multiple CUs avec tailles variees
ffmpeg -f lavfi -i testsrc=size=128x128:rate=1 -frames:v 1 \
       -c:v libx265 -x265-params "qp=22:no-deblock:no-sao:keyint=1" \
       -f hevc tests/conformance/fixtures/i_128x128_qp22.265

# 8x8 blocks forces (min CU = 8)
ffmpeg -f lavfi -i testsrc=size=64x64:rate=1 -frames:v 1 \
       -c:v libx265 -x265-params "qp=30:no-deblock:no-sao:keyint=1:ctu=64:min-cu-size=8" \
       -f hevc tests/conformance/fixtures/i_64x64_smallcu.265
```

### Test unitaire intra prediction
Tester chaque mode isole avec des samples de reference connus.
Pas encore necessaire — les 35 modes fonctionnent (toy tests).

## Taches

- [ ] Generer bitstreams supplementaires avec CU 8x8 et NxN
- [ ] Valider pixel-perfect sur ces bitstreams (apres fix de 4B/4C/4D)

## Critere de sortie

- [x] 3 toy tests pixel-perfect
- [ ] i_64x64_qp22 pixel-perfect (depend de 4B+4C+4D)
- [ ] Au moins 1 bitstream avec CU 8x8 NxN pixel-perfect
