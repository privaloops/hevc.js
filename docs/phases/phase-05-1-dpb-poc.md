# 5.1 -- DPB + POC Derivation

## Objectif

Verifier que le DPB (Decoded Picture Buffer) gere correctement les images decodees
et que le POC (Picture Order Count) est derive correctement pour chaque frame.

## Spec refs

- S8.3.1 : POC derivation (wrap-around MSB/LSB)
- S8.1 : General decoding process (NoRaslOutputFlag, HandleCraAsBlaFlag)
- C.5 : DPB management (output, bumping, removal)
- Annexe A : MaxDpbSize par level

## Code existant

`src/decoding/dpb.cpp` (437 lignes) -- deja implemente, a auditer.

## Points critiques a verifier

### POC derivation (S8.3.1)
- Wrap-around MSB quand LSB wraps (eq 8-1, 8-2)
- `prevTid0Pic` : seul le dernier slice avec `TemporalId == 0` et pas RASL/RADL sert
  de reference pour le MSB/LSB precedent
- IDR : reset `prevPicOrderCntLsb = 0, prevPicOrderCntMsb = 0`

### DPB sizing
- `MaxDpbSize` = min(MaxDecPicBuffering, 16) par level
- Bumping process quand DPB est plein

### NoRaslOutputFlag (S8.1)
- Premier IRAP : `NoRaslOutputFlag = 1`
- IDR : toujours `NoRaslOutputFlag = 1`
- CRA apres IDR : `NoRaslOutputFlag = 1` aussi (HandleCraAsBlaFlag)

## Audit a faire

1. Lire S8.3.1 du PDF (pages 139-141) et comparer avec `dpb.cpp`
2. Verifier la condition `prevTid0Pic` (souvent buggee)
3. Verifier le wrap-around MSB avec des sequences > 256 frames
4. Comparer POC vs HM sur 10 frames de `p_qcif_10f.265`

## Test de validation

```bash
# Ajouter un log du POC pour chaque frame dans le decodeur
# Comparer avec ffmpeg :
ffprobe -show_frames -select_streams v tests/conformance/fixtures/p_qcif_10f.265 2>/dev/null | grep coded_picture_number
```

Test unitaire :
- POC pour sequence [IDR, P, P, P, P, P, P, P, P, P] avec poc_lsb incrementaux
- POC pour sequence avec wrap-around (poc_lsb_bits = 4, 16 frames)

## Critere de sortie

- [ ] POC correct pour les 10 frames de `p_qcif_10f.265` (compare vs ffprobe)
- [ ] POC correct pour les 10 frames de `b_qcif_10f.265`
- [ ] Test unitaire wrap-around POC
