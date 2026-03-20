# Test Fixtures

Bitstreams HEVC de test generes avec x265 4.1. Chaque bitstream a un hash MD5
de reference calcule sur le YUV decode par ffmpeg (libavcodec).

## Bitstreams

| Fichier | Taille | Resolution | Frames | Type | Deblock | SAO | Phase | MD5 (YUV decode) |
|---------|--------|------------|--------|------|---------|-----|-------|------------------|
| `i_64x64_qp22.265` | 511B | 64x64 | 1 | I-only | off | off | 4 | `48c64cd2de381113913149e92065d66c` |
| `i_64x64_deblock.265` | 510B | 64x64 | 1 | I-only | **on** | off | 6 | `576ef497ceedffa01709ddfaa13c76c3` |
| `i_64x64_sao.265` | 515B | 64x64 | 1 | I-only | off | **on** | 6 | `eacb4952591daa891e8dc5344c6c34ee` |
| `i_64x64_full.265` | 514B | 64x64 | 1 | I-only | **on** | **on** | 6 | `dcb7ae02b4514b6e7fd801ae691d901a` |
| `p_qcif_10f.265` | 174K | 176x144 | 10 | I+P | off | off | 5 | `921650ab84b8e366a1aa4e16f2527e46` |
| `b_qcif_10f.265` | 171K | 176x144 | 10 | I+P+B | off | off | 5 | `dffe2712c0561c75ee40256d59299826` |
| `full_qcif_10f.265` | 171K | 176x144 | 10 | I+P+B | **on** | **on** | 6 | `a4984ee61d18ac9619808c5338132eaa` |

## Toy bitstreams (debug step-by-step)

| Fichier | Taille | Resolution | Frames | QP | Phase | MD5 (YUV decode) |
|---------|--------|------------|--------|----|-------|------------------|
| `toy_qp30.265` | 187B | 64x64 | 1 | 30 | 4 | `9f59878470d904dc2e162d3d191611af` |
| `toy_qp45.265` | 141B | 64x64 | 1 | 45 | 4 | `52ca5108bd4354383a9d29d96cf64b47` |
| `toy_qp10.265` | 274B | 64x64 | 1 | 10 | 4 | `ff469ce7e872152490b9b39b0d05dd7b` |

Encodage : `x265 --preset ultrafast --keyint 1 --no-deblock --no-sao --no-wpp --no-info --qp <QP>`

Ces bitstreams ont 1 seul CTU (64x64), ideal pour debugger CABAC et intra prediction etape par etape.

## Real-world bitstreams (Big Buck Bunny)

| Fichier | Taille | Resolution | Frames | FPS | Type | Phase | MD5 (YUV decode) |
|---------|--------|------------|--------|-----|------|-------|------------------|
| `bbb1080_50f.265` | 783K | 1920x1080 | 50 | 25 | I+P+B (full) | 6+ | `a7c12e24701e3636ce58246033bff6ed` |
| `bbb4k_25f.265` | 4.4M | 3840x2160 | 25 | 25 | I+P+B (full) | 6+ | `69f2ba71153ec5f3c1573fed17631ed4` |

Source : Big Buck Bunny, encode HEVC Main profile, extrait sans re-encodage depuis hevc-gpu demo streams.

## Parametres d'encodage communs

```
x265 --preset medium/slow --qp 22 --no-wpp --no-info
```

- Chroma : 4:2:0
- Bit depth : 8-bit
- Profile : Main
- Pas de WPP (single-thread CABAC)
- Pas de SEI info (bitstream plus propre)

## Comment regenerer les references

```bash
# Decoder avec ffmpeg
ffmpeg -y -i fixture.265 -pix_fmt yuv420p fixture_ref.yuv

# Calculer le MD5
md5 -q fixture_ref.yuv  # macOS
md5sum fixture_ref.yuv   # Linux
```

## Comment utiliser dans les tests

```cpp
// Le test oracle decode le bitstream, compare le MD5 du YUV output
// avec le MD5 de reference ci-dessus
TEST(Oracle, I_64x64_QP22) {
    auto output = decode_file("tests/conformance/fixtures/i_64x64_qp22.265");
    EXPECT_EQ(md5(output), "48c64cd2de381113913149e92065d66c");
}
```
