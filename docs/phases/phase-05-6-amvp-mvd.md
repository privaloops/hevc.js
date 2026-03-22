# 5.6 -- AMVP + MVD

## Objectif

Verifier la derivation AMVP (Advanced Motion Vector Prediction) et l'application du MVD.

## Spec refs

- S8.5.3.2.6 : AMVP candidate list construction
- S7.3.8.9 : mvd_coding syntax
- S7.3.8.6 : prediction_unit inter syntax (mvp_l0_flag, mvp_l1_flag)

## Code existant

`src/decoding/inter_prediction.cpp` : `derive_amvp_predictor()`
`src/decoding/syntax_elements.cpp` : `decode_mvd()`

## AMVP vs Merge

| Aspect | Merge | AMVP |
|--------|-------|------|
| Candidats | 5 (spatial+temporal+combined+zero) | 2 (spatial+temporal) |
| Selection | merge_idx | mvp_flag (0 ou 1) |
| MVD | non | oui (additionnel) |
| refIdx | implicite (du candidat) | explicite (parse dans le bitstream) |

## Construction AMVP list (S8.5.3.2.6)

1. **Candidat spatial A** : scan A0, A1 pour un MV vers la meme ref picture
2. **Candidat spatial B** : scan B0, B1, B2 pour un MV vers la meme ref picture
3. Si < 2 candidats : **scaling** -- meme voisin mais ref picture differente, MV scale
4. Si < 2 candidats : **Temporal** -- collocated MV (meme process que TMVP)
5. Si < 2 candidats : **Zero MV** padding

## Pieges identifies

1. **"Same ref picture"** : le candidat doit pointer vers la MEME reference picture
   (meme POC), pas juste le meme index
2. **Scaling entre ref pictures** : quand le voisin pointe vers une ref differente,
   on scale le MV par le ratio des distances POC
3. **2 candidats max** : s'arreter des qu'on en a 2
4. **Deduplication** : si candidat A == candidat B, n'en garder qu'un
5. **MV addition modular** : `mv = (mvp + mvd + 0x10000) % 0x10000` (spec eq 8-94..8-97)

## Audit a faire

1. Lire S8.5.3.2.6 du PDF (pages 176-180)
2. Verifier la condition "same ref picture" (POC comparison, pas index)
3. Verifier le MV scaling dans le fallback
4. Verifier l'addition modulaire MV + MVD
5. Comparer les MVs AMVP vs HM

## Test de validation

```bash
# Logger pour chaque PU AMVP :
# "amvp PU(%d,%d): cand[0]=(%d,%d) cand[1]=(%d,%d) mvp_flag=%d mvd=(%d,%d) final=(%d,%d)"
# Comparer avec HM
```

## Critere de sortie

- [ ] MV final identique a HM pour tous les PUs AMVP du premier P-frame
- [ ] Test unitaire : MV addition modulaire (wrap-around 16-bit)
- [ ] AMVP scaling verifie (cas ou le voisin pointe vers une ref differente)
