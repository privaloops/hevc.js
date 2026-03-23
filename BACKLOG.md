# Backlog

Etat d'avancement par phase et prochaines taches.

## Statut par phase

| Phase | Statut | Progression |
|-------|--------|-------------|
| 1 — Infrastructure | **Terminee** | CMake, BitstreamReader, types, tests, oracle script, Picture, debug logging, CI GitHub Actions, bitstreams real-world. |
| 2 — Bitstream & NAL | **Terminee** | NalParser, start codes, NAL header, AU boundaries, --dump-nals, 22 tests |
| 3 — Parameter Sets | **Terminee** | PTL, VPS, SPS, PPS, SliceHeader, ParameterSetManager, --dump-headers, 17 tests |
| 4 — Intra Prediction | **Termine** | 4A-4F faits, oracle i_64x64_qp22 pixel-perfect |
| 5 — Inter Prediction | **Termine** | 10/10 tests pass. P/B pixel-perfect, weighted pred, CRA, AMP, TMVP, hier-B, open GOP, CABAC init. |
| 6 — Loop Filters | **Termine** | 12/14 tests phase6 pass. 2 echecs (bbb1080, bbb4k) = deblocking P/B avec cu_qp_delta residuel. |
| 7 — High Profiles | **En cours** | Main 10 pixel-perfect (7.1 fait). Tiles parse+decode OK. WPP complet (seek + QP). |
| 8 — WASM Integration | **Termine** | API C, build Emscripten, bindings JS, Web Worker, demo HTML WebGL |
| 9 — Performance | **En cours** | 1080p@61fps WASM, 4K@21fps. SIMD auto-vec fait. WPP multi-thread a faire. |
| 10 — Multi-Slice | **Termine** | Boucle, dependent slices, §6.4.1 availability, cross-slice deblocking + SAO. conf_b_xslice_256 pixel-perfect. |

## Phase 1 — Terminee

- [x] CI GitHub Actions (`build.yml`, `test.yml`)
- [x] `tools/fetch_conformance.sh` (edge-case conformance bitstreams par phase)
- [x] Structure `Picture` (buffer YUV avec stride) — `src/common/picture.cpp`
- [x] `HEVC_DEBUG` logging infrastructure — `src/common/debug.h`
- [x] Bitstreams jouets (toy_qp10/30/45) — `tests/conformance/fixtures/toy_*.265`
- [x] Bitstreams real-world (bbb1080_50f, bbb4k_25f) — Big Buck Bunny
- [x] Script extraction CABAC reference — `tools/extract_cabac_reference.py`
- [x] Guide agent — `docs/agent-guide.md`

## Phase 2 — Terminee

- [x] 2.1 Start code detection (find 0x000001/0x00000001)
- [x] 2.2 Emulation prevention byte removal (extract_rbsp integre dans le pipeline NalParser)
- [x] 2.3 NAL unit header parsing (nal_unit_type, layer_id, temporal_id, forbidden_zero_bit)
- [x] 2.4 Exp-Golomb (tests edge cases ajoutes : large values, negative)
- [x] 2.5 CLI `--dump-nals` (listing tabulaire + AU grouping)
- [x] 2.6 Access Unit boundary detection (prefix/suffix SEI distinction, AUD, EOS, multi-slice)
- [x] 2.7 `more_rbsp_data()` (tests edge cases : single byte, trailing zeros, multi-byte)

## Phase 3 — Terminee

- [x] 3.1 Profile/Tier/Level parsing (general + sub-layer, constraint flags)
- [x] 3.2 VPS parsing complet (timing info, layer sets)
- [x] 3.3 SPS parsing complet (scaling list data + defaults, st_ref_pic_set inter-prediction, long-term refs, VUI skip)
- [x] 3.4 PPS parsing complet (tiles layout + CtbAddrRsToTs/TsToRs/TileId derivation)
- [x] 3.5 Slice header parsing complet (entry point offsets, pred weight table, ref pic list modification, deblocking overrides, dependent slices)
- [x] 3.6 Parameter set management (ParameterSetManager, stockage par ID, activation via slice)
- [x] 3.7 CLI `--dump-headers` (VPS/SPS/PPS/SliceHeader dump complet)

## Phase 4 — En cours (subdivisee en 6 sous-phases)

Voir `docs/phases/phase-04-intra.md` pour le plan detaille.

### 4A — CABAC Engine (FAIT)
- [x] Arithmetic decoder (decode_decision, bypass, terminate)
- [x] Context initialization (155 contextes, I/P/B, cabac_init_flag)
- [x] 7 tests unitaires passent
- [x] Init values verifiees contre HM

### 4A-4D — Parsing (FAIT)
- [x] CABAC engine, coding tree, residual coding — **parsing 100% identique a HM**
- [x] 9 bugs parsing fixes cette session (scanIdx, lastSigCoeff swap, MDCS, cbf defere, CBF_CHROMA 5ctx, B-slice init, invAngle, corner filter, chroma filter)
- [x] 132 residual_coding calls : tous les bin counts matchent HM exactement
- [x] Audits spec complets : residual_coding, coding_tree, syntax_elements, cabac_tables, cabac.cpp — tous conformes Main profile

### 4E — Transform + Dequant (FAIT)
- [x] DCT/DST inverse avec clipping inter-passe — audite conforme spec
- [x] Dequant avec scaling lists — audite conforme spec
- [x] Transform skip — bug dormant (shift 7 au lieu de 5, non utilise dans Main profile)
- [ ] Tests unitaires isoles (DCT round-trip, clipping, QP chroma)

### 4F — Intra Prediction + Reconstruction (FAIT)
- [x] 35 modes intra (Planar, DC, Angular 2-34)
- [x] Reference samples, filtrage, strong smoothing
- [x] 5 bugs intra fixes (invAngle, corner filter, chroma ref filter, DC chroma, angular chroma)
- [x] 3 toy tests pixel-perfect
- [x] DST inverse fix : matrice forward (M) au lieu de la transposee (M^T) — spec eq 8-315 utilise la matrice forward mais l'inverse 2D necessite M^T
- [x] **i_64x64_qp22 pixel-perfect** (jalon Phase 4)

## Phase 5 — Inter Prediction (subdivisee en 11 sous-etapes)

Voir `docs/phases/phase-05-inter.md` pour le plan detaille.
Code existant a auditer : `inter_prediction.cpp`, `interpolation.cpp`, `dpb.cpp`.

### 5.0 — Fix parsing I-frame multi-CTU (FAIT)
- [x] Bug identifie : MPM candModeList[1] eq 8-25, `candA-2+29` → `candA+29`
- [x] I-frame QCIF 176x144 pixel-perfect (0 pixels faux)
- [x] Non-regression `oracle_i_64x64_qp22` pixel-perfect

### 5.1 — DPB + POC
- [ ] Audit `dpb.cpp` POC derivation vs spec S8.3.1
- [ ] Test POC correct pour 10 frames P et B

### 5.2 — RPS + Reference Picture Lists
- [ ] Audit RPS derivation et RefPicList construction
- [ ] RefPicList0/1 identiques a HM pour chaque slice

### 5.3 — Merge candidates (spatial)
- [ ] Audit 5 candidats spatiaux vs spec S8.5.3.2.2
- [ ] Test : candidats identiques a HM pour 5 PUs

### 5.4 — TMVP
- [ ] Audit collocated MV et MV scaling vs spec S8.5.3.2.7
- [ ] Test unitaire MV scaling (5 cas)
- [ ] Candidat temporel identique a HM

### 5.5 — Merge mode complet
- [ ] Audit combined bi-pred (Table 8-8) + zero padding
- [ ] Merge list complete identique a HM

### 5.6 — AMVP + MVD
- [ ] Audit AMVP list construction vs spec S8.5.3.2.6
- [ ] MV final identique a HM pour PUs AMVP

### 5.7 — Interpolation luma (8-tap)
- [ ] Audit 4 cas de shift/clip vs spec S8.5.3.2.2
- [ ] Tests unitaires H-only, V-only, 2D

### 5.8 — Interpolation chroma + bi-pred
- [ ] Audit chroma 4-tap et bi-pred averaging
- [ ] Tests unitaires chroma + bi-pred

### 5.9 — Integration P-frames (FAIT)
- [x] `oracle_p_qcif_10f` pixel-perfect

### 5.10 — Integration B-frames (FAIT)
- [x] `oracle_b_qcif_10f` pixel-perfect
- [x] `conf_b_hier_qcif`, `conf_b_tmvp_qcif`, `conf_b_cra_qcif` pixel-perfect
- [x] `conf_p_weighted_qcif`, `conf_b_weighted_qcif` pixel-perfect
- [x] `conf_b_opengop_qcif`, `conf_p_amp_256` pixel-perfect
- [x] `conf_b_cabacinit_qcif` pixel-perfect

### Bugs fixes session 2026-03-22 (3 bugs)
- [x] Explicit weighted prediction (§8.5.3.3.4.3) — P-slices avec `weighted_pred_flag=1`
- [x] Output frame ordering multi-GOP — CVS ID pour distinguer les GOPs avec POC identiques
- [x] `interSplitFlag` (§7.4.9.4) — force TU split pour CU INTER non-2Nx2N quand `max_transform_hierarchy_depth_inter==0`

## Phase 6 — Termine (subdivisee en 4 sous-phases)

Voir `docs/phases/phase-06-loop-filters.md` pour le plan detaille.

- [x] 6A — Deblocking Luma (Bs derivation, strong/weak filter)
- [x] 6B — Deblocking Chroma (Bs==2 seulement)
- [x] 6C — SAO (Edge offset, Band offset, merge)
- [x] 6D — Integration Full Main Profile ← **jalon majeur atteint**
- [ ] **Validation finale** : Telecharger les bitstreams de conformite officiels JCT-VC

### Bug fixes
- [x] Chroma deblocking saute quand luma decision dE==0 (le `continue` sautait aussi le chroma)
- [x] WPP substream seek : crash `read past end` au CTU 899 BBB 1080p (§7.3.8.1)
- [x] EP byte accounting dans entry_point_offsets (§7.4.7.1)
- [x] QP derivation avec coordonnees QG au lieu de CU (§8.6.1)
- [x] WPP/tile QpY_prev reset a SliceQpY (§8.6.1)
- [x] QP derivation : retrait du shortcut `!IsCuQpDeltaCoded → QpY_prev` (§8.6.1)
- [x] QpY_prev_qg : qPY_PREV sauvegarde au debut de chaque QG, pas mis a jour dans le QG (§8.6.1)
  - Fixe le decode catastrophique des streams cu_qp_delta_enabled (BBB 4K : 12M→<2K diffs)
  - Fixe aussi le bug ARTE CTB=64 (meme famille)
- [x] SAO cross-slice boundary check (§8.7.3.2) — verifie slice_loop_filter_across_slices_enabled_flag
- [ ] **MV derivation P/B frames — streams real-world** (§8.5)
  - **Symptome** : I-frames pixel-perfect, P/B frames avec erreurs localisees qui propagent via inter prediction
  - **BBB 1080p** (CTB=32, QG=8x8, WPP) : frame 1 = 4517 diffs (max_diff=88), x=288-512, y=243-307. Frames I = pixel-perfect.
  - **BBB 4K** (CTB=64, QG=32x32, WPP) : frame 7 = 1782 diffs (max_diff=7), frames 9-10 = 8 diffs (max_diff=1). Frames 0-6 (I+P) = pixel-perfect.
  - **Hypotheses eliminees** :
    - Deblocking : scan complet §8.7.2 — code conforme a la spec ligne par ligne
    - QP : QP grid correct (derive_qp_y conforme a §8.6.1)
    - TU coords : fix applique (TU→CU coords dans decode_transform_unit) mais ne change rien
    - Deblocking order : per-CTB a empire, revertee
  - **Ce qui est etabli** :
    - Les pixels reconstruits different AVANT le deblocking a x>=304 (comparaison YUV directe frame 1)
    - P-side (x<=303) identique entre notre decodeur et HM, Q-side (x>=304) differe
    - CU(288,256) frame 1 : les deux decodeurs s'accordent sur PART_Nx2N 32x32
    - Left PU (288,256) : MV=(3,2) merge idx=0 — identique dans les deux decodeurs
    - Right PU (304,256) : notre decodeur decode AMVP, mvp_flag=1, mvp=(0,0), mvd=(1,-1) → MV=(1,-1)
    - Le MV de HM pour le right PU n'a pas pu etre extrait (format de trace HM per-CU, pas per-PU)
  - **Pistes restantes** :
    - Verifier merge_flag du 2e PU : est-ce que HM decode merge ou AMVP pour ce PU ?
    - Si AMVP : verifier la liste de candidats AMVP (A0/A1/B0/B1/B2) et mvp_l0_flag
    - Ajouter des marqueurs CTU a la trace CABAC bins pour comparaison bin-by-bin
  - **Tests** : `oracle_bbb1080_50f` (cible : 126/126), `oracle_bbb4k_25f` (cible : 126/126)
  - **Commande de repro** :
    ```bash
    ./build/hevc-decode tests/conformance/fixtures/bbb1080_50f.265 -o /tmp/test.yuv
    ffmpeg -y -i tests/conformance/fixtures/bbb1080_50f.265 -pix_fmt yuv420p /tmp/ref.yuv
    python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test.yuv 1920 1088
    ```

## Phase 7 — High Profiles (subdivisee en 7 sous-phases)

Voir `docs/phases/phase-07-high-profiles.md` pour le plan detaille.

### 7.1 — Main 10 (10-bit 4:2:0) (FAIT)
- [x] Audit pipeline 10-bit (QpBdOffset, shifts, clips) — tout conforme
- [x] Fix `cu_skip_flag` pred_mode race condition (bug #18)
- [x] Fix multi-frame YUV output 8-bit cast (bug #19)
- [x] Bitstreams 10-bit + oracle tests (2 tests pixel-perfect)

### 7.2 — Main 4:2:2 10
- [ ] Support `chroma_format_idc == 2`
- [ ] Adapter interpolation chroma et deblocking

### 7.3 — Main 4:4:4
- [ ] Support `chroma_format_idc == 3`

### 7.4 — Niveaux eleves
- [x] Level 5.0/5.1 — fonctionne (BBB 1080p/4K decodent, meme si multi-slice fail)

### 7.5 — Tiles (FAIT)
- [x] PPS parsing (tile columns/rows, CtbAddrRsToTs/TsToRs/TileId)
- [x] CABAC re-init aux frontieres de tiles
- [x] Tile boundary exclusion dans deblocking/SAO

### 7.6 — WPP (PARTIEL)
- [x] entropy_coding_sync_enabled_flag parse
- [x] Context save/restore au 2e CTU de chaque rangee
- [x] WPP I-frames pixel-perfect
- [ ] WPP P/B-frames (bug d'alignement CABAC aux frontieres inter)

### 7.7 — Scaling Lists (FAIT en Phase 4)
- [x] Parsing complet des scaling lists
- [x] Application dans dequant
- [x] Matrices par defaut

## Phase 8 — WASM Integration (FAIT)

Voir `docs/phases/phase-08-wasm.md` pour le plan detaille.

### 8.1 — API C (FAIT)
- [x] `hevc_decoder_create/destroy/decode/get_frame_count/get_frame/get_info`
- [x] Structures `HEVCFrame` et `HEVCStreamInfo`
- [x] Build natif OK

### 8.2 — Build Emscripten (FAIT)
- [x] CMake target `hevc-wasm` avec MODULARIZE, EXPORTED_FUNCTIONS
- [x] `-sSTACK_SIZE=1048576` (bottleneck deferred)
- [x] Build WASM OK (123KB .wasm)
- [x] Pixel-perfect verifie vs natif (MD5 match)

### 8.3 — Bindings JS (FAIT)
- [x] `hevc_decoder.js` — wrapper promise-based avec extraction frames Uint16Array
- [x] `worker.js` — Web Worker avec protocol postMessage, transferable buffers

### 8.4 — Demo HTML (FAIT)
- [x] WebGL renderer YUV->RGB (BT.709 shader)
- [x] File input, play/pause/step, clavier (fleches + espace)
- [x] `demo/serve.sh` — script build + serve

### 8.5-8.7 — Streaming, Memory Management (A FAIRE)
- [ ] Feeding incremental (ReadableStream)
- [ ] Pool de buffers frames
- [ ] Monitoring memoire

## Phase 9 — Performance (EN COURS)

### 9.1 — Quick wins (FAIT)
- [x] Interior PU interpolation sans bounds check (+32% natif)
- [x] Stack-allocated interpolation temp buffers
- [x] SAO persistent backup buffers
- [x] WASM SIMD auto-vectorisation (`-msimd128`, +44% WASM)
- [x] Fix fprintf debug dans dpb.cpp
- [x] CLI timing (fps, ms/frame)
- [x] Benchmark script (`tools/benchmark.sh`)

### 9.2 — WPP Multi-thread (A FAIRE)
- [ ] Fix bug WPP P/B-frames (CABAC alignment aux frontieres de rangees CTU)
- [ ] Thread pool natif (`std::thread`) pour decode parallele des rangees CTU
- [ ] Dependance diagonale : rangee N attend le 2e CTU de la rangee N-1
- [ ] WASM : Web Workers + SharedArrayBuffer (headers COOP/COEP)
- [ ] Benchmark WPP vs libde265 (cible : 4K@60fps)

### 9.3 — SIMD intrinsics manuels (OPTIONNEL)
- [ ] Interpolation luma 8-tap SIMD
- [ ] Interpolation chroma 4-tap SIMD
- [ ] Transform inverse SIMD
- [ ] Deblocking SIMD

### Resultats actuels

| Resolution | Natif (1 thread) | WASM (Chrome) |
|-----------|-----------------|---------------|
| 720p | 187 fps | — |
| 1080p | 77 fps | 61 fps |
| 4K | 27.5 fps | 21 fps |

## Phase 10 -- Multi-Slice

Voir `docs/phases/phase-10-multi-slice.md` pour le plan detaille.

### 10.1 -- Slice Index Map (per-CTU)
- [ ] Ajouter `slice_idx_buf_` dans Decoder et `slice_idx` dans DecodingContext
- [ ] Initialiser a 0 en debut de picture
- [ ] Ecrire l'index du slice courant pour chaque CTU decode dans `decode_slice_segment_data()`
- [ ] Non-regression : tous les tests existants passent

### 10.2 -- Boucle Multi-Slice dans `decode_picture()`
- [ ] Collecter tous les VCL NALs d'une meme picture dans `decode()`
- [ ] Boucler sur chaque NAL VCL : parse header, bitstream, `decode_slice_segment_data()`
- [ ] POC et ref lists depuis le premier slice uniquement
- [ ] Filtres appliques une seule fois apres tous les slices
- [ ] `conf_i_multislice_256` pixel-perfect

### 10.3 -- Heritage Dependent Slices
- [ ] `SliceHeader::parse()` accepte `const SliceHeader* prev_independent_sh`
- [ ] Copier tous les champs du parent quand `dependent_slice_segment_flag == 1`
- [ ] Deriver `SliceAddrRs` correctement pour dependent slices (S7.4.7.1)

### 10.4 -- Cross-Slice Deblocking (FAIT)
- [x] Remplacer check `addr < sh.slice_segment_address` par `slice_idx[p] != slice_idx[q]`
- [x] Gerer `slice_loop_filter_across_slices_enabled_flag` per-slice (cote Q)
- [x] Gerer `slice_beta_offset_div2` / `slice_tc_offset_div2` per-slice (via sh_at_ctb)
- [x] `conf_b_xslice_256` pixel-perfect — SAO cross-slice boundary fix (§8.7.3.2)

### 10.5 -- Cross-Slice SAO (FAIT)
- [x] Verifier parsing SAO merge (leftCtbInSliceSeg / upCtbInSliceSeg) avec boucle multi-slice
- [x] SAO cross-slice boundary check (§8.7.3.2) — fixe les 5 diffs chroma
- [x] `oracle_bbb1080_50f` / `oracle_bbb4k_25f` — I-frames pixel-perfect apres fix QP derivation. P/B frames : voir bug deblocking cu_qp_delta dans Phase 6.

### Bugs identifies (non multi-slice)
- [x] QP derivation cu_qp_delta (§8.6.1) — shortcut `!IsCuQpDeltaCoded` et QpY_prev_qg
- [ ] Deblocking P/B frames cu_qp_delta (§8.7.2.5.3) — voir description detaillee dans Phase 6 Bug fixes

## Taches transverses

- [x] Générer des bitstreams de test réels avec x265 (voir `docs/spec/test-bitstreams.md`)
- [x] Bitstreams real-world Big Buck Bunny (1080p 50f, 4K 25f)
- [ ] Télécharger les bitstreams de conformité HEVC officiels
- [x] Setup CI GitHub Actions
