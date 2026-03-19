# Strategie de Validation Oracle

## Principe

libde265 sert d'oracle de reference : on decode le meme bitstream avec les deux decodeurs et on compare les frames pixel par pixel.

**Important** : On utilise libde265 uniquement pour ses sorties (frames YUV, dumps headers), JAMAIS pour son code source.

## Architecture de test

```
                    ┌──────────────┐
                    │  Bitstream   │
                    │   (.265)     │
                    └──────┬───────┘
                           │
                    ┌──────┴───────┐
                    │              │
              ┌─────▼─────┐ ┌─────▼──────┐
              │ libde265   │ │ hevc-      │
              │ (oracle)   │ │ torture    │
              └─────┬──────┘ └─────┬──────┘
                    │              │
              ┌─────▼──────┐ ┌────▼───────┐
              │ YUV frames │ │ YUV frames │
              └─────┬──────┘ └─────┬──────┘
                    │              │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │  Comparateur │
                    │  pixel-exact │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │  PASS/FAIL   │
                    │  + diff map  │
                    └──────────────┘
```

## Outils necessaires

### 1. libde265 CLI

```bash
# Installation
brew install libde265
# ou build from source pour controler la version

# Decodage en YUV
dec265 input.265 -o output_ref.yuv

# Dump headers (pour validation phase 2-3)
dec265 input.265 --dump-headers > headers_ref.txt
```

### 2. Script de comparaison (Python)

```python
#!/usr/bin/env python3
"""oracle_compare.py — Compare YUV output pixel par pixel."""

import sys
import numpy as np

def compare_yuv(ref_path, test_path, width, height, bit_depth=8, chroma='420'):
    """Compare deux fichiers YUV frame par frame."""
    bytes_per_sample = 2 if bit_depth > 8 else 1
    dtype = np.uint16 if bit_depth > 8 else np.uint8

    # Taille d'une frame
    if chroma == '420':
        frame_size = width * height * 3 // 2
    elif chroma == '422':
        frame_size = width * height * 2
    else:  # 444
        frame_size = width * height * 3
    frame_size *= bytes_per_sample

    ref_data = open(ref_path, 'rb').read()
    test_data = open(test_path, 'rb').read()

    num_frames = len(ref_data) // frame_size

    mismatches = []
    for f in range(num_frames):
        ref_frame = np.frombuffer(ref_data[f*frame_size:(f+1)*frame_size], dtype=dtype)
        test_frame = np.frombuffer(test_data[f*frame_size:(f+1)*frame_size], dtype=dtype)

        if not np.array_equal(ref_frame, test_frame):
            diff = np.abs(ref_frame.astype(int) - test_frame.astype(int))
            mismatches.append({
                'frame': f,
                'max_diff': int(diff.max()),
                'num_diff_pixels': int(np.count_nonzero(diff)),
                'first_diff_pos': int(np.argmax(diff > 0)),
                'psnr': 10 * np.log10((2**bit_depth - 1)**2 / np.mean(diff**2)) if diff.any() else float('inf')
            })

    return num_frames, mismatches

if __name__ == '__main__':
    # Usage: python3 oracle_compare.py ref.yuv test.yuv 1920 1080 [bit_depth] [chroma]
    ref, test, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    bd = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    ch = sys.argv[6] if len(sys.argv) > 6 else '420'

    num_frames, mismatches = compare_yuv(ref, test, w, h, bd, ch)

    if not mismatches:
        print(f"PASS: {num_frames} frames, pixel-perfect match")
    else:
        print(f"FAIL: {len(mismatches)}/{num_frames} frames differ")
        for m in mismatches:
            print(f"  Frame {m['frame']}: {m['num_diff_pixels']} pixels differ, "
                  f"max_diff={m['max_diff']}, PSNR={m['psnr']:.2f}dB")
        sys.exit(1)
```

### 3. Script de test integre (CTest)

```bash
#!/bin/bash
# test_oracle.sh — Execute par CTest

BITSTREAM=$1
WIDTH=$2
HEIGHT=$3
BIT_DEPTH=${4:-8}
CHROMA=${5:-420}

# Decoder avec libde265
dec265 "$BITSTREAM" -o /tmp/ref.yuv 2>/dev/null

# Decoder avec hevc-torture
./hevc-torture "$BITSTREAM" -o /tmp/test.yuv

# Comparer
python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test.yuv "$WIDTH" "$HEIGHT" "$BIT_DEPTH" "$CHROMA"
```

## Bitstreams de test

### Sources

1. **Bitstreams de conformite HEVC** (ITU-T) — la reference officielle
   - Disponibles via le HEVC test model (HM) reference software
   - Couvrent tous les outils et profils

2. **Bitstreams synthetiques** — crees avec x265/kvazaar pour cibler des cas specifiques
   - I-only, P-only, B-only
   - Tailles de bloc specifiques
   - Modes intra specifiques
   - QP varies

3. **Bitstreams reels** — encodes depuis des sequences de test standard
   - BasketballDrill, BQTerrace, etc.

### Script de telechargement

```bash
#!/bin/bash
# fetch_conformance.sh — Telecharge les bitstreams de conformite

DEST=tests/conformance
mkdir -p "$DEST"

# Les bitstreams de conformite sont organises par profil/outil
# Structure : tests/conformance/{profile}/{tool}/{bitstream}.265
```

## Strategie par phase

### Phase 2 — Bitstream
- Comparer le listing NAL (type, taille) avec `dec265 --dump-headers`
- Pas de comparaison pixel (pas encore de decodage)

### Phase 3 — Parameter Sets
- Comparer chaque champ parse (VPS, SPS, PPS, slice header) avec `dec265 --dump-headers`
- Ecrire un mode verbose qui dump les memes infos dans le meme format

### Phase 4 — Intra
- Premiere comparaison pixel-perfect sur des I-frames
- Commencer par : 1 frame, QP fixe, pas de SAO, pas de deblocking
- Puis : multi-frames I-only, QP variable, deblocking active
- Puis : toutes les tailles de bloc, tous les modes intra

### Phase 5 — Inter
- P-frames d'abord (uni-prediction)
- Puis B-frames (bi-prediction)
- Puis weighted prediction
- Tester avec differentes structures de GOP

### Phase 6 — Filtres
- Deblocking seul (SAO off)
- SAO seul (deblocking off si possible, sinon les deux)
- Les deux ensemble
- C'est souvent ici que les derniers mismatches sont resolus

### Phase 7 — Profils
- Meme strategie que Phase 4-6 mais avec 10-bit, 4:2:2, 4:4:4
- Bitstreams de conformite specifiques a chaque profil

## Debugging des mismatches

Quand un mismatch est detecte :

1. **Identifier la premiere frame en erreur**
2. **Identifier le premier pixel en erreur** (composante Y, Cb, ou Cr)
3. **Localiser le CTU/CU** contenant ce pixel
4. **Ajouter du logging** dans hevc-torture pour ce CU specifique
5. **Comparer les valeurs intermediaires** :
   - Prediction samples (avant residu)
   - Residual samples (apres transform inverse)
   - Reconstruction (avant filtres)
   - Apres deblocking
   - Apres SAO
6. **Identifier l'etape** ou la divergence commence

### Outils de debug

```cpp
// Mode debug : dumper les valeurs intermediaires pour un CU donne
#ifdef HEVC_DEBUG
void dump_cu_debug(int x, int y, int size, const char* stage,
                    const int16_t* samples, int stride) {
    fprintf(stderr, "[%s] CU(%d,%d) size=%d:\n", stage, x, y, size);
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++)
            fprintf(stderr, "%4d ", samples[j*stride+i]);
        fprintf(stderr, "\n");
    }
}
#endif
```

## CI Integration

```yaml
# .github/workflows/oracle.yml
oracle-test:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - name: Install libde265
      run: sudo apt-get install -y libde265-dev libde265-examples
    - name: Build hevc-torture
      run: cmake -B build && cmake --build build
    - name: Run oracle tests
      run: cd build && ctest --output-on-failure -L oracle
```

## Checklist

- [ ] libde265 installe et fonctionnel
- [ ] oracle_compare.py operationnel
- [ ] Script CTest pour tests oracle automatises
- [ ] Bitstreams de conformite telecharges
- [ ] CI avec tests oracle
- [ ] Mode debug avec dump des valeurs intermediaires
- [ ] Documentation du processus de debugging des mismatches
