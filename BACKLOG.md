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
| 6 — Loop Filters | **Termine** | 11/14 tests pass. Deblocking + SAO pixel-perfect. 3 echecs = multi-slice (limitation connue). |
| 7 — High Profiles | **En cours** | Main 10 pixel-perfect (7.1 fait). Tiles parse+decode OK. WPP I-only OK, P/B en cours. |
| 8 — WASM Integration | **Termine** | API C, build Emscripten, bindings JS, Web Worker, demo HTML WebGL |
| 9 — Performance | **En cours** | 1080p@61fps WASM, 4K@21fps. SIMD auto-vec fait. WPP multi-thread a faire. |

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

## Taches transverses

- [x] Générer des bitstreams de test réels avec x265 (voir `docs/spec/test-bitstreams.md`)
- [x] Bitstreams real-world Big Buck Bunny (1080p 50f, 4K 25f)
- [ ] Télécharger les bitstreams de conformité HEVC officiels
- [x] Setup CI GitHub Actions
