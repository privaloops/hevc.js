# Learnings

## Session 2026-03-24 — Phase 9B Thread Pool WPP

### condition_variable + atomic : store sous le mutex (CRITICAL)

**Pattern fautif** :
```cpp
// Thread A (signaleur)
completed_col[row].store(col, std::memory_order_release);  // sans lock
row_cv[row].notify_all();

// Thread B (waiter)
std::unique_lock<std::mutex> lock(row_mutex[row]);
row_cv[row].wait(lock, [&] {
    return completed_col[row].load(std::memory_order_acquire) >= needed;
});
```

**Le bug** : `condition_variable::wait(lock, pred)` fait `while(!pred()) { unlock; sleep; lock; }`. Entre `!pred()` (retourne true car l'atomic n'est pas encore mis à jour) et `sleep` (le thread relâche le mutex et s'endort), le signaleur peut faire `store` + `notify_all`. Le `notify_all` arrive quand le waiter n'est pas encore endormi → signal perdu. Le waiter s'endort → deadlock permanent.

**Fix** : stocker la valeur **sous le lock** avant de notify :
```cpp
{
    std::lock_guard<std::mutex> lock(row_mutex[row]);
    completed_col[row].store(col, std::memory_order_release);
}
row_cv[row].notify_all();
```

Cela garantit que le `store` est visible au waiter quand il re-vérifie le predicate après le `notify`. Le `lock_guard` empêche le signaleur de passer entre le check du predicate et le sleep du waiter.

**Symptôme** : deadlock intermittent, apparaît seulement sur des streams longs (120 frames 1080p = 34 rows × 120 frames = 4080 synchronisations, probabilité ~1% par sync). Les streams courts (50 frames) passaient systématiquement.

### Thread pool vs thread-per-row : gain modeste (HIGH)

Le passage de thread-per-row à thread pool persistant donne +29% sur 1080p WPP (99→128 fps). Le gain vient principalement de l'élimination de la création/destruction de 34 `std::thread` par frame (4080 créations pour 120 frames). Le remplacement du spin-wait par des `condition_variable` a un impact moindre mais libère du CPU pour les workers.

L'écart avec libde265 (128 vs 477 fps WPP 1080p, ratio 0.27x) est principalement dû au single-thread (63 vs 84 fps, ratio 0.75x). libde265 utilise des SIMD intrinsics NEON/SSE manuels pour les hotpaths (interpolation 8-tap, transform butterfly, deblocking). L'auto-vectorisation Clang ne couvre pas ces patterns complexes. Le scaling WPP (2.03x pour nous vs 5.7x pour libde265) suggère aussi que libde265 a une synchronisation plus légère ou un meilleur overlapping entre rows.

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

## Session 2026-03-21b — Phase 4 DST inverse fix (pixel-perfect!)

### DST-VII inverse uses M^T, not M (CRITICAL)

La spec §8.6.4.2 eq 8-315 definit la 1D transform comme `y[i] = sum_j transMatrix[i][j] * x[j]`. La matrice `transMatrix` est la matrice DST-VII **forward** (valeurs: {29,55,74,84}, {74,74,0,-74}, ...). Pour l'inverse 2D, il faut utiliser la **transposee** M^T car DST-VII n'est PAS symetrique (contrairement a DCT-II qui utilise un butterfly symetrique).

**Evidence**: Coefficients dequantises identiques entre HM et nous, mais residus completement differents. HM's `fastInverseDst` utilise `c[row] * M[row][column]` (= M^T · x), pas `M[i][k] * c[k]` (= M · x). La verification numerique confirme: M^T donne le bon resultat (match HM), M donne un resultat faux.

**Pourquoi DCT n'a pas ce probleme**: Les fonctions `idct4/8/16/32` utilisent un butterfly decomposition qui est intrinsequement symetrique (le butterfly pour DCT-II est auto-transposant). Seul DST-VII (4x4 luma intra) utilise une multiplication matricielle explicite, et c'est la que l'erreur se produit.

**Regle**: Quand la spec donne une matrice de transform, verifier si c'est la matrice forward ou inverse. Pour DST-VII, la spec donne la forward → l'inverse est la transposee. Pour DCT, le butterfly est auto-transposant.

### Bug found and fixed session 2026-03-21b (1 bug)

10. **DST inverse matrix** — utilisait la matrice forward M au lieu de M^T (spec eq 8-315 + HM fastInverseDst). Causait 2023 pixels luma faux dans i_64x64_qp22. Fix: transposer la matrice dans `idst4()`.

### Milestone atteint

**oracle_i_64x64_qp22 = pixel-perfect** (jalon Phase 4). Progression pixels faux: 5608 → 4239 → 2674 → 2023 → **0**.

## Session 2026-03-21c — Phase 5 spec audit + CABAC bypass fix + multi-CTU investigation

### cabac_bypass_alignment_enabled_flag — RExt only (CRITICAL)

Le commit `71f86ab` avait ajoute `cabac.align_bypass()` (ivlCurrRange=256) avant les coeff_sign_flag et coeff_abs_level_remaining de facon **inconditionnelle**. Or `cabac_bypass_alignment_enabled_flag` est un flag SPS RExt (§7.4.3.2.2), infere a 0 pour Main profile. Forcer ivlCurrRange=256 corrompait l'etat CABAC pour TOUS les bypass bins, causant un decodage de 52 minutes et des coefficients aberrants.

**Fix**: conditionner `align_bypass()` sur `ctx.sps->cabac_bypass_alignment_enabled_flag`. Stocker le flag dans le SPS (il etait parse mais ignore).

**Resultat**: oracle_i_64x64_qp22 pixel-perfect restaure.

### TMVP collocated_from_l0_flag inversion (§8.5.3.2.9)

Le code avait `colList = collocated_from_l0_flag ? 0 : 1` — inversé. La spec dit "with N being the value of collocated_from_l0_flag", donc `colList = collocated_from_l0_flag` directement.

### AMVP rewrite (§8.5.3.2.7)

Reecrit pour suivre la spec: tracking de `isScaledFlagLX`, scaling B-group seulement quand `isScaledFlagLX==0`, disponibilite §6.4.2 (z-scan + cross-CTU), check `LongTermRefPic` dans le pass scaling.

### Multi-CTU I-frame mismatch — investigation exhaustive sans resolution

**Symptome**: oracle_i_64x64_qp22 (1 CTU) pixel-perfect, mais p_qcif_10f I-frame (176x144, 9 CTUs) a 21593 pixels faux. Premier pixel faux a (160,0) dans CTU 2.

**Audit spec-first effectue** (toutes sections conformes):
- §7.3.8.1 slice_segment_data (boucle CTU)
- §9.2.2 CABAC context propagation
- §6.4.1 z-scan availability (cross-CTU)
- §8.4.4.2.2 ref sample construction + substitution
- §9.3.4.2.2 split_cu_flag context
- §9.3.4.2.4 coded_sub_block_flag context
- §9.3.4.2.5 sig_coeff_flag context (eq 9-40 a 9-55)
- §7.3.8.8 transform_tree (split, cbf)
- §7.3.8.10 transform_unit (chroma deferred)
- §8.4.2 MPM derivation
- §8.4.3 chroma mode derivation

**Comparaison bin-par-bin avec HM**: 18322 decision bins identiques (val, range, offset), divergence au bin 18323. Le contexte ctx=90 (sig_coeff_flag ctxInc=8, 4x4 luma) a pStateIdx=62 (sature) chez nous vs pStateIdx ~31 chez HM. Cause: certains bins que nous assignons a ctx=90 sont assignes a un autre contexte dans HM, malgre des r/o identiques (les deux contextes ont le meme etat par coincidence).

**Conclusion**: le bug est dans le mapping sig_coeff_flag pour les 4x4 TUs. La spec (eq 9-41 + ctxIdxMap + Table 9-50) et notre code sont conformes en apparence, mais HM utilise un mapping subtillement different. L'audit spec n'a pas pu le trouver car le spec et HM sont incoherents sur ce point (deja documente dans LEARNINGS Session 2025-03-20).

**Decision**: consulter libde265 comme source de verite alternative pour identifier le mapping exact que les decodeurs conformes utilisent. Ce n'est pas du debug iteratif — c'est une consultation de reference d'implementation.

### Bugs corriges cette session (3 bugs)

11. **CABAC bypass alignment conditionnel** — `align_bypass()` inconditionnel → conditionnel sur `cabac_bypass_alignment_enabled_flag`
12. **TMVP colList inversion** — `collocated_from_l0_flag ? 0 : 1` → `collocated_from_l0_flag`
13. **AMVP rewrite §8.5.3.2.7** — isScaledFlagLX, §6.4.2 availability, LongTermRefPic check

## Session 2026-03-21d — Fix multi-CTU I-frame (pixel-perfect!)

### MPM candModeList[1] formula — off-by-2 (CRITICAL)

La spec eq 8-25 dit: `candModeList[1] = 2 + ((candIntraPredModeA + 29) % 32)`.
Notre code avait: `2 + ((candA - 2 + 29) % 32)` — un `-2` en trop.

**Impact**: quand candA==candB >= 2, le 2e candidat MPM était décalé de 2 (ex: 29 au lieu de 31 pour candA=32). Cela causait un mode intra incorrect (mode 31 au lieu de 30) pour certains CUs dans les CTU 2+. Mode 31 est hors [22,30] → scanIdx=0 (diagonal) au lieu de scanIdx=1 (horizontal) → sig_coeff_flag contexts faux → divergence CABAC → 21593 pixels faux.

**Méthode de découverte**: trace bin-par-bin avec ctxIdx ajouté à HM. La divergence au bin 18323 a révélé un mauvais scan type. Remonté au mode intra 31 vs 30. Comparaison MPM HM preds=[32,31,33] vs nôtre [32,29,33] → formule eq 8-25 fausse.

**Leçon**: les formules modulaires de la spec (avec `% 32`) sont faciles à mal transcrire. Toujours vérifier le résultat numérique pour quelques valeurs de candA (ex: candA=2, 10, 26, 32) contre HM.

### Bug #14

14. **MPM candModeList[1]** — `candA-2+29` au lieu de `candA+29` (spec eq 8-25). Causait 21593 pixels faux dans la frame I multi-CTU 176x144.

### Milestone atteint

**I-frame multi-CTU 176x144 = pixel-perfect** (bloqueur Phase 5 levé).

## Session 2026-03-22 — Phase 5 completion (3 bugs, 10/10 tests)

### Explicit weighted prediction — ne pas ignorer les flags PPS (CRITICAL)

Le PPS contient `weighted_pred_flag` (P-slices) et `weighted_bipred_flag` (B-slices). La spec §8.5.3.3.4.1 route vers default (§8.5.3.3.4.2) ou explicit (§8.5.3.3.4.3) selon `weightedPredFlag`. Le code appelait toujours `weighted_pred_default`, ignorant les flags.

**Impact**: P-slices avec poids non-triviaux produisaient des valeurs luma fausses (max_diff=11), cascadant vers les B-frames qui les référencent.

**Méthode de découverte**: analyse de la sortie `--dump-headers` montrant `weighted_pred = 1` dans le PPS. Le fix est une transcription directe de la spec eq 8-265 à 8-277.

**Leçon**: toujours vérifier les flags PPS qui routent vers des processus différents. Un flag ignoré = un processus entier manquant.

### Output frame ordering multi-GOP — POC n'est PAS un identifiant global (HIGH)

Quand un bitstream contient plusieurs IDR, le POC remet à 0 à chaque IDR. Trier la sortie par POC seul mélange les frames de GOPs différents. La reconstruction pixel-perfect produisait 176 à position (0,0), mais le YUV montrait 66 (une frame d'un autre GOP à la mauvaise position).

**Fix**: ajouter un compteur `cvs_id` (Coded Video Sequence) incrémenté à chaque IDR, trier par `(cvs_id, poc)`.

**Piège supplémentaire**: le code avait DEUX fonctions de sortie (`DPB::get_output_pictures()` et `Decoder::output_pictures()`) avec des tris différents. Le fix dans l'une ne s'appliquait pas à l'autre. Toujours vérifier qu'il n'y a pas de duplication de logique.

### interSplitFlag — condition implicite facile à oublier (HIGH)

La spec §7.4.9.4 dit que quand `split_transform_flag` n'est pas présent dans le bitstream, il est inféré à 1 si `interSplitFlag == 1`. L'`interSplitFlag` se déclenche quand:
- `max_transform_hierarchy_depth_inter == 0`
- `CuPredMode == MODE_INTER`
- `PartMode != PART_2Nx2N`
- `trafoDepth == 0`

Sans cette condition, le décodeur lit `split_transform_flag` du bitstream alors qu'il devrait être inféré, corrompant l'état CABAC pour tous les éléments suivants.

**Pourquoi seulement 4 frames**: seules les B-frames avec des CUs 32x32 non-2Nx2N (PART_2NxN) étaient affectées. Les CUs 64x64 forçaient déjà le split via `log2TrafoSize > MaxTbLog2SizeY`. Les CUs 2Nx2N avaient `interSplitFlag=0`.

**Méthode de découverte**: agent subagent avec investigation systématique — comparaison HM SYN trace pour identifier le CU exact (64,96 PART_2NxN dans POC 8), puis relecture spec §7.4.9.4.

### Bugs #15-17

15. **Explicit weighted prediction** — `weighted_pred_default` appelé systématiquement au lieu de router selon `weightedPredFlag` (§8.5.3.3.4.1)
16. **Output ordering multi-GOP** — tri par POC seul mélange les frames de GOPs avec POC identiques
17. **interSplitFlag** — condition implicite manquante pour le split du transform tree (§7.4.9.4)

### Milestone atteint

**Phase 5 Inter Prediction = 10/10 tests pixel-perfect** : `oracle_p_qcif_10f`, `oracle_b_qcif_10f`, `conf_p_weighted_qcif`, `conf_b_weighted_qcif`, `conf_b_hier_qcif`, `conf_b_tmvp_qcif`, `conf_b_cra_qcif`, `conf_b_opengop_qcif`, `conf_p_amp_256`, `conf_b_cabacinit_qcif`.

## Session 2026-03-22b — Phase 7 Main 10 (10-bit 4:2:0)

### cu_skip_flag pred_mode race condition — latent bug since Phase 5 (CRITICAL)

Le CU grid stocke `pred_mode` a la ligne 691 de `coding_tree.cpp`, APRES l'appel a `decode_prediction_unit_inter` (ligne 553 pour skip, 624 pour inter). Or `decode_prediction_unit_inter` lit `cu.pred_mode` du grid pour choisir entre merge (skip) et AMVP (inter). Pour les CUs skip, le grid a encore `MODE_INTRA` (default) → la fonction entre dans le chemin AMVP au lieu de merge, lisant un bin CABAC supplementaire (`merge_flag`) qui n'existe pas dans le bitstream.

**Pourquoi masque en 8-bit** : les bitstreams de test 8-bit n'avaient pas de CUs skip, ou les CUs skip tombaient dans des etats CABAC ou le bin `merge_flag` fantome valait 1 (= merge), produisant le meme resultat que le chemin correct. Le bitstream 10-bit a QP=45 avec `ultrafast` (100% skip, tous sur des positions anciennement intra) a expose le bug.

**Fix** : stocker `pred_mode = MODE_SKIP` dans le CU grid AVANT l'appel a `decode_prediction_unit_inter`.

**Lecon** : tester avec des bitstreams qui maximisent les CUs skip (QP eleve + ultrafast + 10-bit). Les bitstreams "medium preset, QP 22" n'exercent pas les modes skip assez agressivement.

### Architecture 10-bit = zero-effort grace a AD-002 (POSITIVE)

La decision d'architecture AD-002 (`Pixel = uint16_t`) prise en Phase 1 a rendu le support 10-bit quasi transparent :
- Aucun `255` hardcode nulle part — tous les clips utilisent `(1 << bitDepth) - 1`
- `QpBdOffsetY`/`QpBdOffsetC` correctement derives et utilises dans QP, dequant, deblocking beta/tC
- Shifts d'interpolation parametres par `bitDepth` (§8.5.3.3.3)
- SAO band shift = `bitDepth - 5`
- YUV output 10-bit deja gere dans `write_yuv()` (8-bit = uint8_t, 10-bit = uint16_t LE)

**Seul probleme** : le chemin multi-frame dans `main.cpp` castait en `uint8_t` (bug trivial fixe en 5 min).

### WPP inter-frame = bug restant (LOW)

Le WPP fonctionne pour les I-frames mais echoue pour les P/B-frames. Le contexte CABAC save/restore aux frontieres de rangees CTU a un probleme d'alignement ou de timing en mode inter. Defere — le WPP est optionnel pour le Main 10 profile et prepare la Phase 9 (parallelisme).

### Bugs #18-19

18. **cu_skip_flag pred_mode race** — pred_mode stocke dans le CU grid apres `decode_prediction_unit_inter` au lieu d'avant, causant le chemin AMVP au lieu de merge pour les CUs skip
19. **Multi-frame YUV 8-bit cast** — `main.cpp` multi-frame path castait en `uint8_t`, ignorant le bit depth + chroma crop hardcode `/2`

### Milestone atteint

**Phase 7 Main 10 = pixel-perfect** : `oracle_i_64x64_10bit`, `oracle_full_qcif_10f_10bit`. 124/128 tests passent (4 echecs = multi-slice, inchange).

## Session 2026-03-23 — WPP crash + QP derivation bugs

### WPP substream seek (CRITICAL)

Avec WPP, chaque rangee CTB est un substream separe. Les `entry_point_offset_minus1` donnent la taille en octets de chaque substream dans le coded slice data. Le CABAC engine lit des bits en avance via `renormalize()` (`read_bits_safe`), ce qui decale progressivement la position du BitstreamReader. Sans repositionnement explicite via `seek_to_byte`, l'erreur s'accumule et le decodeur crash (`read past end`) apres quelques rangees.

**Piege** : les entry_point_offsets comptent les emulation prevention bytes (spec §7.4.7.1 : "emulation prevention bytes are counted as part of the slice segment data"). Le BitstreamReader opere sur le RBSP (EP bytes supprimes). Il faut convertir les offsets coded → RBSP en comptant les EP bytes avant chaque offset.

### QP derivation : coordonnees QG, pas CU (CRITICAL)

La spec §8.6.1 dit que les predicteurs QP (qPY_A, qPY_B) sont lus aux positions `(xQg-1, yQg)` et `(xQg, yQg-1)` ou (xQg, yQg) sont les coordonnees du quantization group, PAS du CU. Erreur invisible sur les petits streams (QG = CTB = toute l'image) mais provoque des erreurs QP systematiques de ±4 sur les streams larges (1080p+) ou `cu_qp_delta_enabled_flag` est actif.

### WPP QpY_prev reset

La spec §8.6.1 dit explicitement : "qPY_PREV is set equal to SliceQpY when the current quantization group is the first quantization group in a CTB row of a tile and entropy_coding_sync_enabled_flag is equal to 1". Facile a oublier car le texte est noye dans une liste de conditions. Meme reset requis pour les frontieres de tiles.

## Session 2026-03-23 — QP derivation cu_qp_delta + SAO cross-slice

### qPY_PREV scope (CRITICAL)

La spec §8.6.1 definit `qPY_PREV` comme "the QpY of the last CU in the **previous quantization group**". Notre code mettait a jour `QpY_prev` apres chaque CU, ce qui double-comptait le `CuQpDeltaVal` quand les voisins A/B retombaient sur `QpY_prev` (hors du CTB courant). Introduit `QpY_prev_qg` sauvegarde au debut de chaque QG. Corrige le decode catastrophique BBB 4K (12M diffs → <2K).

### derive_qp_y shortcut removal

Le shortcut `if (!IsCuQpDeltaCoded) return QpY_prev` semblait correct mais ne l'est pas : la spec ne fait pas ce raccourci. Meme quand `CuQpDeltaVal=0`, le `qPY_PRED` doit etre calcule avec les voisins A/B (qui peuvent etre dans le meme CTB et avoir un QP different de `QpY_prev`). Visible uniquement avec `diff_cu_qp_delta_depth > 0` (QG < CTB).

### SAO cross-slice boundary (§8.7.3.2)

Le SAO edge offset doit verifier si les voisins sont dans un slice different quand `slice_loop_filter_across_slices_enabled_flag == 0`. La logique spec est asymetrique : si le voisin est dans un slice anterieur (Z-scan), on verifie le flag du slice courant ; si dans un slice posterieur, on verifie le flag du voisin. Invisible sur les streams single-slice.

### Bugs restants identifies

- **~~Deblocking P/B cu_qp_delta~~** : RESOLU — le vrai bug etait AMVP §6.4.2, pas le deblocking (voir session 2026-03-24).

## Session 2026-03-24 — Fix AMVP prediction block availability (§6.4.2)

### §6.4.1 (z-scan) vs §6.4.2 (prediction block) — deux niveaux d'availability (CRITICAL)

La spec definit DEUX processus d'availability :
- **§6.4.1** : z-scan order block availability — verifie que le voisin a ete decode en z-scan order dans le CTB courant (ou dans un CTB precedent).
- **§6.4.2** : prediction block availability — wraps §6.4.1 mais ajoute une exception **sameCb** : si le voisin est dans le meme coding block, il est toujours disponible (sauf NxN partIdx=1 exception). Pas de z-scan check.

L'AMVP et le merge utilisent §6.4.2, pas §6.4.1 directement. Notre code utilisait §6.4.1 partout.

**Consequence** : pour un CU 32x32 PART_Nx2N, le PU 1 (NE quadrant, z-scan ~16) cherche le voisin A1 dans le PU 0 (SW quadrant, z-scan ~47). Z-scan dit "non disponible" (47 > 16). §6.4.2 dit "disponible" (meme CU). Resultat : l'AMVP du PU 1 ne trouvait pas le candidat A → `isScaledFlagLX=0` → B copie vers A → A==B → pruning → padding zero-MV → MVP faux → MV faux.

**Piege additionnel** : le `pred_mode` du CU n'etait pas ecrit dans la grille avant le traitement des PUs. Le voisin A1 (meme CU, PU 0) avait encore `MODE_INTRA` du frame I precedent → `is_amvp_nb_available` rejetait le voisin au check "must not be intra". Double erreur.

**Comparaison libde265** : libde265 `available_pred_blk()` (image.cc:810) fait exactement ce que dit §6.4.2 :
```cpp
if (!sameCb) { availableN = available_zscan(xP,yP,xN,yN); }
else { availableN = !(NxN && partIdx==1 && in_third_quadrant); }
```

**Attention merge** : appliquer le meme fix a `is_pu_available` (merge) sans stocker `part_mode` tot cause une regression massive (128K diffs). Cause : `derive_spatial_merge_candidates` lit `ctx.cu_at(xPb, yPb).part_mode` pour les exclusions partition-specifiques (PART_Nx2N partIdx=1 → skip A1), mais `part_mode` n'est ecrit dans la grille qu'apres TOUS les PUs (ligne 794). Quand le fix rend les voisins sameCb disponibles, les exclusions utilisent un `part_mode` stale → mauvaises exclusions → regression. Le fix correct necessite les DEUX changements simultanes : `is_pu_available` + ecriture anticipee de `part_mode`. Voir session 2026-03-24b.

### oracle_compare.py height mismatch (MEDIUM)

La commande de repro dans le BACKLOG utilisait `h=1088` (pic_height_in_luma_samples) au lieu de `h=1080` (display height apres conformance window crop). Les deux decodeurs (notre + ffmpeg) outputtent `1920x1080`. Consequence : les positions des pixels faux etaient decalees (CTB row misaligned), et l'analyse pointait vers (311,243) au lieu de (311,255).

**Regle** : toujours verifier la taille du fichier YUV output avant d'utiliser oracle_compare. `file_size / (w * h * 1.5)` doit etre un entier.

### Bugs #20

20. **AMVP §6.4.2 + early pred_mode** — `is_amvp_nb_available` utilisait z-scan (§6.4.1) au lieu de §6.4.2 pour les voisins intra-CU + `pred_mode` stocke trop tard dans le CU grid. Causait 4517 diffs Y frame 1 BBB 1080p (max_diff=88). Fix: check `sameCb` dans AMVP + ecriture pred_mode avant PUs.

## Session 2026-03-24b — Fix merge candidate availability §6.4.2 (128/128 tests!)

### Merge `is_pu_available` — deux bugs correles (CRITICAL)

Le meme pattern que le bug AMVP (#20), mais avec une subtilite supplementaire qui causait la regression du fix naif.

**Bug 1 — `is_pu_available` z-scan avant sameCb** : identique au bug AMVP. La fonction appliquait §6.4.1 (z-scan) avant de verifier sameCb. §6.4.2 dit explicitement : si sameCb, ne PAS invoquer §6.4.1. Restructure pour matcher `is_amvp_nb_available`.

**Bug 2 — `part_mode` non stocke tot** : `derive_spatial_merge_candidates` lit `ctx.cu_at(xPb, yPb).part_mode` (ligne 265) pour les exclusions partition-specifiques (PART_Nx2N partIdx=1 → skip A1, PART_2NxN partIdx=1 → skip B1). Mais `part_mode` n'etait ecrit dans la grille qu'a la ligne 794 de `coding_tree.cpp`, APRES tous les PUs. Pour partIdx=1, la grille contenait le `part_mode` du CU precedent → exclusions incorrectes.

**Pourquoi le fix naif regressait** : sans l'ecriture anticipee de `part_mode`, le fix de `is_pu_available` rendait les voisins sameCb disponibles, mais les exclusions utilisaient un `part_mode` stale. Par exemple, si le CU precedent avait `PART_2Nx2N` et le CU courant a `PART_Nx2N`, l'exclusion A1 ne se declenchait pas → candidat merge faux → MV faux → 128K diffs.

**Fix** : deux changements simultanes :
1. `coding_tree.cpp` : ecriture anticipee de `part_mode` dans la grille CU, a cote de l'ecriture anticipee de `pred_mode` (avant le traitement des PUs)
2. `inter_prediction.cpp` : restructuration de `is_pu_available` pour checker sameCb avant z-scan (identique a `is_amvp_nb_available`)

**Lecon** : quand un fix "naif" cause une regression, la cause n'est souvent PAS le fix lui-meme mais un bug latent correle. Le z-scan check incorrect masquait le bug de `part_mode` stale — les deux bugs se compensaient. La regression du fix naif etait le signal qu'un deuxieme bug existait, pas que le fix etait faux.

### Bugs #21

21. **Merge §6.4.2 + early part_mode** — `is_pu_available` utilisait z-scan (§6.4.1) avant sameCb + `part_mode` stocke trop tard pour les exclusions merge. Causait 4677 diffs frames 18-23 BBB 1080p (max_diff=49). Fix: check sameCb dans merge + ecriture part_mode avant PUs.

### Milestone atteint

**128/128 tests pixel-perfect** — tous les bitstreams de test passent. Le decodeur est conforme Main profile + Main 10 sur l'integralite des tests (toy, conformance, oracle, real-world BBB 1080p/4K).

## Session 2026-03-24 — Player plugins (dashjs, hlsjs)

### Architecture MSE intercept pour players tiers

**Approche retenue** : patcher `MediaSource` au niveau global plutot que d'integrer avec l'API interne de chaque player. dash.js et hls.js utilisent `MediaSource`/`SourceBuffer` de facon identique → un seul intercept partage.

Points cles :
1. **Proxy SourceBuffer** — il faut reporter `updating = true` pendant le transcodage, sinon le player bombarde les segments sans attendre. dash.js check `sb.updating` avant chaque `appendBuffer`.
2. **`Reflect.get(target, prop, target)`** — les getters natifs de SourceBuffer (`buffered`, `appendWindowStart`) lancent `Illegal invocation` si `this` est un Proxy. Il faut utiliser `target` comme receiver, pas `receiver`.
3. **hvcC parameter sets** — les VPS/SPS/PPS sont dans le `hvcC` box de l'init segment fMP4, pas dans les media segments. Il faut les extraire et les feeder au decodeur WASM avant les NAL units des segments.
4. **mp4box.js `sample.data`** — retourne un `Uint8Array` (pas `ArrayBuffer`). `new DataView(sample.data)` echoue → utiliser `data.buffer.slice(data.byteOffset, ...)`.
5. **H.264 codec level** — `avc1.42001f` (Baseline L3.1) plafonne a 720p. Pour 1080p utiliser `avc1.64002a` (High L4.2), pour 4K `avc1.640033` (High L5.1). Selection dynamique basee sur la resolution.
6. **`navigator.mediaCapabilities.decodingInfo()`** — dash.js 4.x l'utilise en priorite sur `MediaSource.isTypeSupported()`. Il faut patcher les deux.
7. **Emscripten WASM glue** — n'est pas un ES module, `import()` echoue. Solution : charger via `<script>` tag + detecter le global `HEVCDecoderModule` dans `HEVCDecoder.create()`.

8. **Web Worker pour le transcodage** — `SegmentTranscoder` tourne dans un Worker classique (pas module). Le WASM glue est charge via `importScripts()` dans le Worker. Le main thread envoie les segments via `postMessage` avec transfert zero-copy (`ArrayBuffer` transfer). Apres `abort()` (seek), le Worker detruit et recree le transcoder avec le meme config — le client reset `initParsed`/`initAppended` pour accepter un nouveau init segment.

9. **`workerUrl` non passe au intercept** — bug subtil : les plugins dashjs/hlsjs passaient `wasmUrl`, `fps`, `bitrate` a `installMSEIntercept` mais oubliaient `workerUrl`. Le Worker n'etait jamais cree. Fix: inclure `workerUrl` dans l'appel.

### Architecture finale du monorepo

Code partage dans `@hevcjs/core` (MSE intercept, SegmentTranscoder, demuxer mp4box.js, muxer fMP4, H264Encoder). Les plugins dashjs/hlsjs ne sont que des thin wrappers (~60 lignes chacun) qui appellent `installMSEIntercept()` + enregistrent les filtres specifiques au player.
