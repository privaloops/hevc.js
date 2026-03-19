# Chapitre 8 — Decoding Process (Index)

Ce chapitre decrit le processus de decodage complet, depuis les syntax elements parses jusqu'aux samples reconstruits.

## Structure

| Section | Fichier | Contenu | Phase |
|---------|---------|---------|-------|
| 8.3 | `08-03-reference-pictures.md` | Gestion des ref pics, DPB, POC | 5 |
| 8.4 | (inline 8.6) | Quantization matrices | 4 |
| 8.5.3 | `08-04-inter-prediction.md` | Motion compensation, interpolation | 5 |
| 8.5.4 | `08-05-intra-prediction.md` | 35 modes intra (DC, Planar, Angular) | 4 |
| 8.6 | `08-06-transform-quant.md` | Transform inverse, dequant, scaling | 4 |
| 8.7.2 | `08-07-deblocking.md` | Deblocking filter | 6 |
| 8.7.3 | `08-08-sao.md` | Sample Adaptive Offset | 6 |

## Pipeline de decodage d'un CU

```
+------------------+
| Syntax Parsing   | (CABAC -> syntax elements)
| (Chapitre 7+9)   |
+--------+---------+
         |
    +----+----+
    |         |
    v         v
+--------+ +--------+
| Intra  | | Inter  |  Prediction
| 8.5.4  | | 8.5.3  |
+---+----+ +---+----+
    |          |
    +----+-----+
         | predSamples[x][y]
         v
+------------------+
| Transform Inverse|  8.6.2-4
| + Dequant        |  8.6.1-3
+--------+---------+
         | resSamples[x][y]
         v
+------------------+
| Reconstruction   |  recSamples = Clip(predSamples + resSamples)
+--------+---------+
         |
         v  (apres tous les CUs de la picture)
+------------------+
| Deblocking       |  8.7.2
+--------+---------+
         |
         v
+------------------+
| SAO              |  8.7.3
+--------+---------+
         |
         v
+------------------+
| DPB Storage      |  Frame prete pour output/reference
+------------------+
```

## Convention

Chaque fichier 08-xx contient :
1. Le pseudo-code de la spec pour chaque sous-processus
2. La traduction C++ correspondante
3. Les edge cases et pieges identifies
4. La checklist de validation
