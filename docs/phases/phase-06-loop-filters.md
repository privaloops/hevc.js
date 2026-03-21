# Phase 6 — Loop Filters (Deblocking + SAO)

## Objectif

Implementer les filtres in-loop pour obtenir un decodeur Main profile complet.
C'est le **jalon majeur** du projet.

## Prerequis

Phase 5 completee (inter prediction fonctionnelle, sans filtres).

## Spec refs

- §8.7.2 : Deblocking filter
- §8.7.3 : Sample Adaptive Offset

## Pourquoi cette phase est plus simple

Contrairement aux phases 4/5, les loop filters sont du **traitement d'image pur** :
- Pas de CABAC (le parsing SAO est deja fait en Phase 4)
- Pas de propagation d'erreur en cascade
- Les bugs sont locaux : un filtre incorrect ne corrompt que les pixels concernes
- Le debug est facile : comparer pixel par pixel pre/post filtre

## Decoupe en sous-phases

| Sous-phase | Validation |
|------------|------------|
| 6A — Deblocking Luma | Test : comparer pixels post-deblock vs ffmpeg (deblock only) |
| 6B — Deblocking Chroma | Test : idem pour chroma |
| 6C — SAO | Test : comparer pixels post-SAO vs ffmpeg (SAO only) |
| 6D — Integration | Oracle pixel-perfect full Main profile |

## Ordre d'execution

```
6A (Deblock Luma) ──→ 6B (Deblock Chroma) ──→ 6C (SAO) ──→ 6D (Integration)
```

Sequentiel car SAO opere sur les samples post-deblocking.

---

### 6A — Deblocking Luma

**Taches** :
- [ ] Identifier les edges a filtrer (frontieres CU/TU/PU, grille 8 pixels)
- [ ] Boundary Strength derivation (§8.7.2.4) :
  - Bs=2 : intra ou PCM
  - Bs=1 : cbf non-zero OU ref pics differentes OU MV delta >= 4 (1/4-pel)
  - Bs=0 : conditions detaillees uni/bi-pred
- [ ] Tables beta et tC (Tables 8-16, 8-17)
- [ ] QP moyen + offsets (slice_beta_offset_div2, slice_tc_offset_div2)
- [ ] Decision de filtrage (dp, dq, beta threshold)
- [ ] Strong filter (p0, p1, p2, q0, q1, q2)
- [ ] Weak filter (p0, q0 + optionnel p1, q1)
- [ ] Ordre : edges verticales d'abord (toute la picture), puis horizontales
- [ ] Exclusion : bords picture/tile/slice, flags across_tiles/across_slices

**Pieges identifies :**
- Bs bi-pred : tester les DEUX ordres (direct et croise) — voir Table §8.7.2.4.5
- Ordre V puis H est obligatoire (les H filtrent les samples deja debloques en V)
- `cu_transquant_bypass_flag == 1` : pas de filtrage pour ce CU
- `pcm_loop_filter_disabled_flag` : pas de filtrage pour edges PCM

**Validation** :
- Test unitaire : Bs derivation pour les cas intra, inter uni, inter bi
- Test : decoder un bitstream avec `--no-sao` et comparer vs ffmpeg

**Critere de sortie** :
- [ ] `oracle_i_64x64_deblock` pixel-perfect (I-frame + deblocking)

---

### 6B — Deblocking Chroma

**Taches** :
- [ ] Filtrage seulement si Bs == 2
- [ ] Filtre simple (p0, q0 modifies)
- [ ] QP chroma (utiliser la table non-lineaire)

**Validation** :
- Meme bitstream que 6A, verifier les plans Cb/Cr

**Critere de sortie** :
- [ ] Chroma pixels corrects dans oracle_i_64x64_deblock

---

### 6C — SAO (Sample Adaptive Offset)

**Taches** :
- [ ] Edge Offset : 4 classes (H, V, diag135, diag45), categorisation, offsets
- [ ] Band Offset : 32 bandes, 4 offsets consecutifs, band_position
- [ ] SAO merge (left, up) — propagation des parametres
- [ ] Bords de picture : pas de comparaison EO hors limites
- [ ] Bords CTU/slice/tile : flags across_slices/across_tiles

**Important** : SAO opere sur les samples **post-deblocking** :
- Deblocking applique sur toute la picture d'abord
- Puis SAO applique en utilisant les samples debloques
- Pour les comparaisons EO aux bords de CTU, utiliser les samples apres deblocking (pas apres SAO du voisin)

**Validation** :
- Test : decoder un bitstream avec `--no-deblock` et comparer vs ffmpeg (SAO only)

**Critere de sortie** :
- [ ] `oracle_i_64x64_sao` pixel-perfect (I-frame + SAO)

---

### 6D — Integration Full Main Profile

**Prerequis** : 6A, 6B, 6C valides.

**Taches** :
- [ ] Pipeline complet : reconstruction → deblocking V → deblocking H → SAO
- [ ] Tester sur I-frames, P-frames, B-frames

**Validation oracle** :
```bash
ctest -R oracle_i_64x64_full --output-on-failure
ctest -R oracle_full_qcif_10f --output-on-failure  # milestone Main profile
ctest -R oracle_bbb1080 --output-on-failure         # real-world
```

**Critere de sortie** :
- [ ] `oracle_i_64x64_full` pixel-perfect
- [ ] `oracle_full_qcif_10f` pixel-perfect — **Main profile complet**
- [ ] `oracle_bbb1080_50f` pixel-perfect — real-world Big Buck Bunny

## Estimation de complexite

Moderee. Le deblocking est la partie la plus delicate (combinatoire Bs, edge cases bords).
SAO est simple algorithmiquement. Pas de propagation d'erreur CABAC.
