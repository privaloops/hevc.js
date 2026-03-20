# Backlog

Etat d'avancement par phase et prochaines taches.

## Statut par phase

| Phase | Statut | Progression |
|-------|--------|-------------|
| 1 — Infrastructure | **Terminee** | CMake, BitstreamReader, types, tests, oracle script, Picture, debug logging, CI GitHub Actions, bitstreams real-world. |
| 2 — Bitstream & NAL | **Prochaine** | — |
| 3 — Parameter Sets | A faire | — |
| 4 — Intra Prediction | A faire | — |
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

## Phase 2 — Prochaine

- [ ] 2.1 Start code detection (find 0x000001/0x00000001)
- [ ] 2.2 Emulation prevention byte removal (extract_rbsp deja fait, intégrer dans le pipeline)
- [ ] 2.3 NAL unit header parsing (nal_unit_type, layer_id, temporal_id)
- [ ] 2.4 Exp-Golomb (read_ue/read_se deja fait, ajouter tests vecteurs)
- [ ] 2.5 CLI `--dump-nals` (implementer)
- [ ] 2.6 Access Unit boundary detection (prefix/suffix SEI distinction)
- [ ] 2.7 `more_rbsp_data()` (deja fait, ajouter tests edge cases)

## Phase 3 — Après Phase 2

- [ ] 3.1 Profile/Tier/Level parsing
- [ ] 3.2 VPS parsing complet
- [ ] 3.3 SPS parsing complet (avec scaling list fallback)
- [ ] 3.4 PPS parsing complet (avec tiles layout)
- [ ] 3.5 Slice header parsing complet (avec entry point offsets, pred weight table)
- [ ] 3.6 Parameter set management (stockage par ID)
- [ ] 3.7 CLI `--dump-headers`

## Phase 4 — Après Phase 3

- [ ] **Prealable** : Executer `tools/fetch_conformance.sh phase4` pour generer les bitstreams edge-case (PCM, transform skip, scaling lists, QP extremes, constrained intra, dependent slices)
- [ ] 4.1 CABAC engine (decode_decision, bypass, terminate)
- [ ] 4.2 CABAC context init (toutes les tables)
- [ ] 4.3 Binarization (FL, TU, TR, EGk)
- [ ] 4.4a slice_segment_data() boucle avec end_of_slice_segment_flag
- [ ] 4.4b Coding tree (quad-tree split)
- [ ] 4.5 Intra mode parsing (MPM)
- [ ] 4.6 Transform tree + residual_coding (sig_coeff_flag, cRiceParam)
- [ ] 4.7 Dequantization (QP derivation, chroma mapping, scaling lists)
- [ ] 4.8 Transform inverse (DCT/DST, clipping inter-passe, transform skip)
- [ ] 4.9 Intra prediction (Planar, DC, Angular + transposition modes 2-17)
- [ ] 4.10 Reconstruction (pred + residual, clipping)
- [ ] 4.10b PCM mode (byte alignment, CABAC reset)
- [ ] 4.11 SAO parsing (stub)

## Phase 5 — Prealables conformance

- [ ] **Prealable** : Executer `tools/fetch_conformance.sh phase5` pour generer les bitstreams edge-case (weighted pred, B hierarchique, TMVP, CRA/RASL, open GOP, AMP)

## Phase 6 — Prealables conformance

- [ ] **Prealable** : Executer `tools/fetch_conformance.sh phase6` pour generer les bitstreams edge-case (deblocking bi-pred, SAO edge/band, multi-slice cross-filter, QP variable, offsets beta/tc)
- [ ] **Validation finale** : Telecharger les bitstreams de conformite officiels JCT-VC pour validation Main profile stricte (optionnel, voir docs/conformance-sources.md)

## Taches transverses

- [x] Générer des bitstreams de test réels avec x265 (voir `docs/spec/test-bitstreams.md`)
- [x] Bitstreams real-world Big Buck Bunny (1080p 50f, 4K 25f)
- [ ] Télécharger les bitstreams de conformité HEVC officiels
- [x] Setup CI GitHub Actions
