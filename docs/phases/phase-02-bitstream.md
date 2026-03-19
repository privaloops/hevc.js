# Phase 2 — Bitstream & NAL Unit Parsing

## Objectif
Lire un flux HEVC brut (Annexe B), isoler les NAL units, extraire les RBSP.

## Prérequis
Phase 1 complétée (BitstreamReader, infrastructure).

## Spec refs
- Annexe B : Byte stream format
- §7.3.1 : NAL unit syntax
- §7.4.2 : NAL unit semantics

## Tâches

### 2.1 — Start Code Detection
- [ ] Trouver les start codes 3-byte (0x000001) et 4-byte (0x00000001)
- [ ] Gérer les leading_zero_8bits et trailing_zero_8bits
- [ ] Découper le byte stream en NAL units bruts
- [ ] Tests : fichier avec multiples NAL units

### 2.2 — Emulation Prevention Byte Removal
- [ ] Détecter et supprimer les bytes 0x03 dans les séquences 0x000003xx
- [ ] Produire les RBSP data propres
- [ ] Tests : vérifier que les 0x000000 et 0x000001 dans les données sont correctement préservés

### 2.3 — NAL Unit Header Parsing
- [ ] `forbidden_zero_bit` (vérification)
- [ ] `nal_unit_type` (6 bits)
- [ ] `nuh_layer_id` (6 bits)
- [ ] `nuh_temporal_id_plus1` (3 bits)
- [ ] Dérivation de `TemporalId`
- [ ] Helpers : `is_vcl()`, `is_irap()`, `is_idr()`, etc.

### 2.4 — Exp-Golomb Coding
- [ ] `read_ue()` : unsigned Exp-Golomb
- [ ] `read_se()` : signed Exp-Golomb
- [ ] Edge cases : valeur 0, grandes valeurs, erreurs de bitstream
- [ ] Tests unitaires exhaustifs

### 2.5 — CLI de test
- [ ] Programme `hevc-torture --dump-nals input.265`
- [ ] Affiche pour chaque NAL : offset, type, size, TemporalId
- [ ] Format similaire à `dec265 --dump-headers` pour faciliter la comparaison

## Critère de sortie

- Parser n'importe quel fichier .265 et lister correctement tous les NAL units
- Comparaison du listing avec `dec265 --dump-headers` : 100% match sur le type et le nombre de NAL units

## Validation oracle
```bash
# Générer les listings
./hevc-torture --dump-nals test.265 > our_nals.txt
dec265 test.265 --dump-headers 2>&1 | grep "NAL" > ref_nals.txt
diff our_nals.txt ref_nals.txt
```

## Estimation de complexité
Faible à modérée. Code fondamental mais bien défini par la spec.
