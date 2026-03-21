# Learnings

## Session 2025-03-20 — Phase 4 Intra debugging

### sig_coeff_flag chroma context offset (CRITICAL)

**Spec eq 9-55** says `ctxInc = 27 + sigCtx` for chroma sig_coeff_flag. However, the HM reference model uses offset **28** (not 27). The init values in Table 9-29 are organized as **28 luma + 16 chroma = 44 total**, NOT 27+17.

Evidence: bin-by-bin comparison with HM showed divergence at the first chroma sig_coeff_flag decoded (bin 326 in i_64x64_qp22.265). Our context 108 (=81+27+0) had state=12/mps=1 while HM had state=16/mps=0 — different init values because we used luma init at index 27 instead of chroma init at index 28.

**Root cause**: The spec formula `ctxInc = 27 + sigCtx` appears to conflict with the init table organization (28 luma entries). The reference implementation (HM) uses `FIRST_SIG_FLAG_CTX_CHROMA = 28`, and the init values are 28 luma + 16 chroma concatenated in Table 9-29.

**Lesson**: Always cross-reference spec formulas with the HM reference implementation.

**IMPORTANT FINDING**: The spec formula (eq 9-55: `ctxInc = 27 + sigCtx`) and the init values in Table 9-29 are **internally inconsistent** for chroma 4x4. Table 9-29 was designed for HM's context mapping (which uses `firstSignificanceMapContext + ctxIdxMap` offsets that differ from the spec formula). Example: chroma 4x4 position (0,1) → spec formula gives ctxInc=29 (initValue=182) but HM maps to chroma[11] (initValue=111). Both are internally consistent with their own mapping, but produce different decode results. Since HM is the conformance reference and all encoders follow it, we MUST use HM's mapping.

This is NOT a PDF extraction error — Table 9-29 values verified with `-layout` extraction. The spec's context assignment formulas (eq 9-40 to 9-55) simply don't match the init value organization in Table 9-29.

### HM context type ordering (CRITICAL CORRECTION)

**`significanceMapContextSetStart[LUMA]` = {SINGLE=0, 4x4=9, 8x8=21, NxN=27}** was WRONG interpretation!

Actual HM enum: `CONTEXT_TYPE_4x4=0, CONTEXT_TYPE_8x8=1, CONTEXT_TYPE_NxN=2, CONTEXT_TYPE_SINGLE=3`. So the array maps as:
- `[0]=4x4` → start=0 (for SINGLE, this was confused with 4x4!)
- `[1]=8x8` → start=9
- `[2]=NxN` → start=**21** (NOT 27!)
- `[3]=SINGLE` → start=27

This means **luma NxN (≥16x16) uses firstSigCtx=21**, which is EXACTLY what the spec formula `+21` gives. Our original code was correct all along for luma NxN. The value 27 is for transform_skip (SINGLE), not NxN.

### Context enum completeness

Our context enum was missing `rqt_root_cbf` (Table 9-14, 1 context). Also found discrepancies in `NUM_CHROMA_PRED_CTX` (spec=2, ours=1) and `NUM_DELTA_QP_CTX` (HM=3, ours=2). Full audit needed against HM `ContextTables.h`.

### Bugs found and fixed session 2025-03-20

1. **Z-scan reference sample availability** — heuristic was row-major instead of Z-scan bit-interleave
2. **Intra mode grid granularity** — used MinCbSizeY (8) instead of MinTbSizeY (4), NxN modes silently dropped
3. **Chroma TU position for deferred chroma** — used x0/y0 instead of xBase/yBase when log2TrafoSize==2
4. **sig_coeff_flag chroma context offset** — used 27 instead of 28 (this entry)

## Session 2026-03-21 — Phase 4 systematic spec audit

### Ne jamais "simplifier" la spec (CRITICAL)

Tous les bugs de cette session venaient d'**interprétations** de la spec au lieu de **transcriptions directes**. Exemples :
- `scanIdx` condition : codé `log2TrafoSize == 2 || (log2TrafoSize == 3 && cIdx > 0)` au lieu de transcrire la spec §7.4.9.11 qui dit `cIdx == 0` (inversé !)
- `invAngle` : stocké positif et utilisé `i * invA` au lieu de suivre la spec eq 8-54 qui utilise des valeurs négatives — résultat : `(-i) * invA` requis
- `cbf_chroma` : 4 contextes au lieu des 5 de Table 9-22 — jamais vérifié contre la spec

**Règle ajoutée au CLAUDE.md** : chaque fonction de décodage doit être une transcription directe de la spec, pas une interprétation.

### Chroma reference filtering — luma only pour 4:2:0 (HIGH)

La spec §8.4.4.2.3 dit que le filtrage des reference samples ne s'applique que quand `cIdx == 0 || ChromaArrayType == 3`. Pour 4:2:0 (Main profile), **pas de filtrage chroma**. Notre code filtrait inconditionnellement.

Idem pour le DC boundary filtering (eq 8-48..8-51) et le angular post-filtering modes 10/26 (eq 8-60/8-68) : la spec dit `cIdx == 0` uniquement.

### Chroma cbf deferred — les valeurs parentes doivent être héritées (HIGH)

Quand `log2TrafoSize <= 2` (4:2:0), les cbf_cb/cbf_cr ne sont pas parsés au niveau courant. La spec §7.3.8.10 utilise `cbfDepthC = trafoDepth - 1` pour le cas déféré (blkIdx==3), ce qui signifie les cbf du **parent**. Notre code initialisait cbf_cb/cbf_cr à `false`, perdant les valeurs parentes → chroma jamais décodée pour les CU NxN.

### MDCS s'applique aux blocs <= 8x8 pixels, pas seulement 4x4 (HIGH)

La spec §7.4.9.11 dit que le scan non-diagonal (mode-dependent) s'applique quand :
- `log2TrafoSize == 2` (4x4)
- `log2TrafoSize == 3 && cIdx == 0` (8x8 luma)
- `log2TrafoSize == 3 && ChromaArrayType == 3` (8x8 en 4:4:4)

Notre code excluait le cas 8x8 luma. HM confirme avec `MDCS_MAXIMUM_WIDTH = 8`.

### Corner sample filter — cross-reference refTop et refLeft (MEDIUM)

La spec eq 8-41 dit : `pF[-1][-1] = (p[-1][0] + 2*p[-1][-1] + p[0][-1] + 2) >> 2`. Le corner utilise le premier sample de **chaque** côté (left et top). Notre code filtrait refTop et refLeft séparément sans croiser le corner. Fix : calculer le corner filtré avant les deux appels à `filter_reference_samples`, puis écraser dans les deux arrays.

### Audit systématique = plus efficace que debug incrémental

Le debug bin-par-bin a trouvé 3 bugs en 1h. L'audit systématique (3 agents parallèles × 7 fichiers) a trouvé 6 bugs supplémentaires en 30 min. **Toujours auditer avant de tracer.**

### Bugs found and fixed session 2026-03-21 (9 bugs)

1. **Chroma scan index** — `intra_mode_at()` (luma) au lieu de `chroma_mode_at()` pour scanIdx chroma
2. **LastSigCoeff swap** — missing swap(X,Y) pour vertical scan (spec eq 7-78)
3. **MDCS scanIdx** — condition inversée, réécrit depuis spec §7.4.9.11
4. **Chroma cbf deferred** — parent cbf perdu pour TU 4x4 (spec §7.3.8.8/7.3.8.10)
5. **CBF_CHROMA contexts** — 4 au lieu de 5 (spec Table 9-22), décalait tous les offsets
6. **B-slice split_transform_flag init** — valeurs P dupliquées dans B (spec Table 9-20)
7. **invAngle projection** — `i*invA` au lieu de `(-i)*invA` (spec eq 8-54)
8. **Corner sample filter** — p[-1][-1] non cross-filtré (spec eq 8-41)
9. **Chroma filtering** — ref filter, DC filter, angular filter appliqués au chroma (spec dit cIdx==0)
