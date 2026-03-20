# Learnings

## Session 2025-03-20 — Phase 4 Intra debugging

### sig_coeff_flag chroma context offset (CRITICAL)

**Spec eq 9-55** says `ctxInc = 27 + sigCtx` for chroma sig_coeff_flag. However, the HM reference model uses offset **28** (not 27). The init values in Table 9-29 are organized as **28 luma + 16 chroma = 44 total**, NOT 27+17.

Evidence: bin-by-bin comparison with HM showed divergence at the first chroma sig_coeff_flag decoded (bin 326 in i_64x64_qp22.265). Our context 108 (=81+27+0) had state=12/mps=1 while HM had state=16/mps=0 — different init values because we used luma init at index 27 instead of chroma init at index 28.

**Root cause**: The spec formula `ctxInc = 27 + sigCtx` appears to conflict with the init table organization (28 luma entries). The reference implementation (HM) uses `FIRST_SIG_FLAG_CTX_CHROMA = 28`, and the init values are 28 luma + 16 chroma concatenated in Table 9-29.

**Lesson**: Always cross-reference spec formulas with the HM reference implementation.

**IMPORTANT FINDING**: The spec formula (eq 9-55: `ctxInc = 27 + sigCtx`) and the init values in Table 9-29 are **internally inconsistent** for chroma 4x4. Table 9-29 was designed for HM's context mapping (which uses `firstSignificanceMapContext + ctxIdxMap` offsets that differ from the spec formula). Example: chroma 4x4 position (0,1) → spec formula gives ctxInc=29 (initValue=182) but HM maps to chroma[11] (initValue=111). Both are internally consistent with their own mapping, but produce different decode results. Since HM is the conformance reference and all encoders follow it, we MUST use HM's mapping.

This is NOT a PDF extraction error — Table 9-29 values verified with `-layout` extraction. The spec's context assignment formulas (eq 9-40 to 9-55) simply don't match the init value organization in Table 9-29.

### Context enum completeness

Our context enum was missing `rqt_root_cbf` (Table 9-14, 1 context). Also found discrepancies in `NUM_CHROMA_PRED_CTX` (spec=2, ours=1) and `NUM_DELTA_QP_CTX` (HM=3, ours=2). Full audit needed against HM `ContextTables.h`.

### Bugs found and fixed this session

1. **Z-scan reference sample availability** — heuristic was row-major instead of Z-scan bit-interleave
2. **Intra mode grid granularity** — used MinCbSizeY (8) instead of MinTbSizeY (4), NxN modes silently dropped
3. **Chroma TU position for deferred chroma** — used x0/y0 instead of xBase/yBase when log2TrafoSize==2
4. **sig_coeff_flag chroma context offset** — used 27 instead of 28 (this entry)
