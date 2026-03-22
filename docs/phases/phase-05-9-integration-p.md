# 5.9 -- Integration P-frames

## Objectif

Pixel-perfect sur `oracle_p_qcif_10f` (P-frames only, 176x144, 10 frames).

## Prerequis

Toutes les etapes 5.0 a 5.8 validees independamment.

## Pipeline complet

Pour chaque P-frame :
1. Parse slice header (type=P)
2. Construct RefPicList0 (5.2)
3. Pour chaque CU :
   a. Parse cu_skip_flag, pred_mode_flag
   b. Si MODE_INTER : parse prediction_unit (merge ou AMVP)
   c. Derive MV (merge list ou AMVP + MVD) (5.3/5.5/5.6)
   d. Motion compensation : interpolation luma + chroma (5.7/5.8)
   e. Si residual : parse transform tree + residual_coding
   f. Reconstruction : pred + residual, clip
4. Store picture in DPB (5.1)

## Points d'integration a verifier

### CU inter parsing (S7.3.8.5)
- `cu_skip_flag` : merge sans residual
- `pred_mode_flag` : 0=INTER, 1=INTRA (attention : INTRA dans P-slice possible !)
- `rqt_root_cbf` : parse seulement si NOT (PART_2Nx2N && merge_flag)
- Si `rqt_root_cbf == 0` : pas de transform tree

### Partition modes inter
- PART_2Nx2N : 1 PU = CU entier
- PART_2NxN, PART_Nx2N : 2 PUs
- PART_NxN : 4 PUs (seulement au min CU size)
- AMP (si amp_enabled) : PART_2NxnU/nD, PART_nLx2N/nRx2N

### Reconstruction inter
```cpp
// Inter : prediction deja ecrite dans le picture buffer par MC
// Si residual (rqt_root_cbf=1) :
//   1. Parse transform tree
//   2. Pour chaque TU : parse coefficients, dequant, IDCT
//   3. Ajouter residual a la prediction : sample = pred + residual
//   4. Clip to [0, maxVal]
// Si pas de residual : la prediction MC est le resultat final
```

### CU intra dans P-slice
- Possible ! `pred_mode_flag=1` dans un P-slice signifie INTRA
- Utiliser le meme pipeline intra que Phase 4

## Debugging

Si le MD5 ne matche pas :
```bash
# Comparer frame par frame
python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test.yuv 176 144

# Si frame 0 (I-frame) echoue : c'est un bug 5.0
# Si frame 1+ echoue : c'est un bug inter
# Identifier le premier pixel faux → localiser le CU
# Checker si c'est MV, interpolation, ou residual
```

## Critere de sortie

- [ ] `oracle_p_qcif_10f` pixel-perfect (MD5 match)
- [ ] Frame 0 (I-frame) pixel-perfect (regression Phase 4)
- [ ] Frame 1 pixel-perfect (premier P-frame)
- [ ] 10/10 frames pixel-perfect
