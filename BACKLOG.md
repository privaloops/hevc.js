# Backlog

Etat d'avancement par phase et prochaines taches.

## Statut par phase

| Phase | Statut | Progression |
|-------|--------|-------------|
| 1 — Infrastructure | **Terminee** | CMake, BitstreamReader, types, tests, oracle script, Picture, debug logging, CI GitHub Actions, bitstreams real-world. |
| 2 — Bitstream & NAL | **Terminee** | NalParser, start codes, NAL header, AU boundaries, --dump-nals, 22 tests |
| 3 — Parameter Sets | **Terminee** | PTL, VPS, SPS, PPS, SliceHeader, ParameterSetManager, --dump-headers, 17 tests |
| 4 — Intra Prediction | **En cours** | 4A fait, 4B-4D a valider, 4E-4F fait (toy tests) |
| 5 — Inter Prediction | A faire | — |
| 6 — Loop Filters | A faire | — |
| 7 — High Profiles | A faire | — |
| 8 — WASM Integration | A faire | — |
| 9 — Performance | A faire | — |

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

### 4F — Intra Prediction + Reconstruction (EN COURS)
- [x] 35 modes intra (Planar, DC, Angular 2-34)
- [x] Reference samples, filtrage, strong smoothing
- [x] 5 bugs intra fixes (invAngle, corner filter, chroma ref filter, DC chroma, angular chroma)
- [x] 3 toy tests pixel-perfect
- [ ] **BUG RESTANT** : 2023 pixels luma faux a x+y>=60, source pixel (60,0) dans CU NxN (56,0) PU mode 27. Probleme probable dans `build_reference_samples()` / `is_reconstructed()` z-scan availability
- [ ] i_64x64_qp22 pixel-perfect

## Phase 5 — Apres Phase 4 (subdivisee en 4 sous-phases)

Voir `docs/phases/phase-05-inter.md` pour le plan detaille.

- [ ] **Prealable** : Executer `tools/fetch_conformance.sh phase5`
- [ ] 5A — DPB + Ref Lists (POC, RPS, RefPicList construction)
- [ ] 5B — MV Derivation (Merge 5+1 candidats, AMVP, TMVP, MV scaling)
- [ ] 5C — Interpolation (8-tap luma, 4-tap chroma, shifts bit-exact)
- [ ] 5D — Integration (P-frames puis B-frames pixel-perfect)

## Phase 6 — Apres Phase 5 (subdivisee en 4 sous-phases)

Voir `docs/phases/phase-06-loop-filters.md` pour le plan detaille.

- [ ] **Prealable** : Executer `tools/fetch_conformance.sh phase6`
- [ ] 6A — Deblocking Luma (Bs derivation, strong/weak filter)
- [ ] 6B — Deblocking Chroma (Bs==2 seulement)
- [ ] 6C — SAO (Edge offset, Band offset, merge)
- [ ] 6D — Integration Full Main Profile ← **jalon majeur**
- [ ] **Validation finale** : Telecharger les bitstreams de conformite officiels JCT-VC

## Taches transverses

- [x] Générer des bitstreams de test réels avec x265 (voir `docs/spec/test-bitstreams.md`)
- [x] Bitstreams real-world Big Buck Bunny (1080p 50f, 4K 25f)
- [ ] Télécharger les bitstreams de conformité HEVC officiels
- [x] Setup CI GitHub Actions
