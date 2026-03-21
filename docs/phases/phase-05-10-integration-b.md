# 5.10 -- Integration B-frames

## Objectif

Pixel-perfect sur `oracle_b_qcif_10f` (B-frames, 176x144, 10 frames).

## Prerequis

5.9 valide (P-frames pixel-perfect).

## Ce qui change par rapport aux P-frames

### Bi-prediction
- `inter_pred_idc` : PRED_L0 (0), PRED_L1 (1), PRED_BI (2)
- Bi-pred : MC depuis les deux listes, averaging (5.8)

### RefPicList1
- Construit separement de RefPicList0 (S8.3.3)
- Ordre inverse des StCurrBefore/StCurrAfter

### Combined bi-pred merge candidates
- Table 8-8 (5.5) : seulement pour B-slices
- Prend des paires de candidats L0/L1 des candidats existants

### Reordering de decodage
- Les B-frames ne sont pas decodees dans l'ordre d'affichage
- Le DPB doit gerer l'output reordering (bumping process)
- Verifier que les ref pictures sont correctement identifiees par POC

### GOP hierarchique
- B-frames peuvent referencer d'autres B-frames
- Le TemporalId determine la hierarchie
- Verifier les distances POC pour le MV scaling

## Tests de conformance supplementaires

```bash
# B-frame hierarchique
ctest -R conf_b_hier_qcif --output-on-failure

# B-frame avec TMVP
ctest -R conf_b_tmvp_qcif --output-on-failure

# CRA (Clean Random Access)
ctest -R conf_b_cra_qcif --output-on-failure

# Open GOP
ctest -R conf_b_opengop_qcif --output-on-failure

# CABAC init flag (cabac_init_present_flag + cabac_init_flag)
ctest -R conf_b_cabacinit_qcif --output-on-failure
```

## Debugging B-frames

```bash
# Frame order vs decode order :
# Si le bitstream a la structure IDR B B P B B P...
# L'ordre de decodage est : IDR P B B P B B...
# Verifier que le DPB output les bonnes frames au bon moment

# Comparer l'output frame par frame
python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test.yuv 176 144
```

## Critere de sortie

- [ ] `oracle_b_qcif_10f` pixel-perfect (MD5 match)
- [ ] `conf_b_hier_qcif` pixel-perfect
- [ ] `conf_b_tmvp_qcif` pixel-perfect
- [ ] `conf_b_cra_qcif` pixel-perfect
- [ ] `conf_b_opengop_qcif` pixel-perfect
- [ ] `conf_b_cabacinit_qcif` pixel-perfect
