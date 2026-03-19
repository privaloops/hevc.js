# Phase 7 — Profils Supérieurs (Main 10, 4:2:2, 4:4:4)

## Objectif
Étendre le décodeur au-delà de Main profile pour supporter 4K@60fps.

## Prérequis
Phase 6 complétée (Main profile pixel-perfect).

## Spec refs
- Annexe A : Profiles, tiers, levels (Tables A.1-A.8)
- §7.3.3.1 : SPS range extension flags
- Modifications bit depth dans §8.5, §8.6, §8.7

## Tâches

### 7.1 — Main 10 (10-bit 4:2:0)
- [ ] Étendre tous les buffers de 8-bit à 16-bit (template ou runtime)
- [ ] Adapter `BitDepthY`, `BitDepthC` = 10
- [ ] Adapter `QpBdOffsetY`, `QpBdOffsetC` = 12
- [ ] Vérifier tous les shifts/rounding dans :
  - Intra prediction
  - Inter prediction (interpolation filters)
  - Transform inverse
  - Dequantization
  - Deblocking (beta, tC tables avec offset)
  - SAO
- [ ] Output YUV 10-bit (2 bytes par sample, little-endian)
- [ ] Tests pixel-perfect sur bitstreams Main 10

### 7.2 — Main 4:2:2 10
- [ ] Support `chroma_format_idc == 2`
- [ ] `SubWidthC = 2`, `SubHeightC = 1`
- [ ] Adapter l'interpolation chroma (MV derivation change)
- [ ] Adapter le deblocking chroma (edges horizontales changent)
- [ ] Adapter SAO chroma (taille des CTU chroma change)
- [ ] Adapter les dimensions des buffers chroma
- [ ] Tests pixel-perfect sur bitstreams 4:2:2

### 7.3 — Main 4:4:4
- [ ] Support `chroma_format_idc == 3`
- [ ] `SubWidthC = 1`, `SubHeightC = 1`
- [ ] Chroma traitée comme luma (mêmes filtres d'interpolation 8-tap)
- [ ] Intra prediction chroma identique à luma (35 modes)
- [ ] separate_colour_plane_flag (si supporté)
- [ ] Tests pixel-perfect

### 7.4 — Niveaux élevés
- [ ] Level 5.0 : MaxLumaPs = 8,912,896 (4K@30)
- [ ] Level 5.1 : MaxLumaPs = 8,912,896 (4K@60)
- [ ] DPB sizing correct pour ces niveaux
- [ ] Bitrate max : 25-40 Mbps Main tier
- [ ] Vérifier que les buffers ne dépassent pas les limites mémoire raisonnables

### 7.5 — Tiles
- [ ] Multi-tile support (déjà parsé en Phase 3)
- [ ] Scan order CtbAddrRs -> CtbAddrTs correct
- [ ] Indépendance CABAC entre tiles
- [ ] Entry point offsets
- [ ] loop_filter_across_tiles_enabled_flag

### 7.6 — WPP (Wavefront Parallel Processing)
- [ ] entropy_coding_sync_enabled_flag
- [ ] Sauvegarde/restauration des contextes CABAC en début de ligne CTU
- [ ] Entry point offsets
- [ ] Préparation pour le parallélisme WASM (Phase 9)

### 7.7 — Scaling Lists (si non fait en Phase 4)
- [ ] Parsing complet des scaling lists (§7.3.4)
- [ ] Application dans le processus de scaling (§8.6.2)
- [ ] Matrices par défaut pour chaque taille

## Critère de sortie

- Pixel-perfect sur bitstreams de conformité Main 10
- Pixel-perfect sur bitstreams 4:2:2 (si bitstreams disponibles)
- Pixel-perfect sur bitstreams 4:4:4 (si bitstreams disponibles)
- Décodage fonctionnel de contenu 4K (Level 5.0/5.1)

## Estimation de complexité
Modérée. La plupart du code existe déjà (Main profile). Les modifications sont principalement des adaptations de bit depth et chroma format.
