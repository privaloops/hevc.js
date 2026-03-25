# Plan : Streaming HEVC → MSE (HLS/DASH dans Video.js)

## Contexte

hevc-decode est un décodeur HEVC/H.265 C++17 compilé en WASM (236KB, 1080p@61fps).
L'objectif est de permettre la lecture de flux HLS/DASH contenant du HEVC dans n'importe quel navigateur,
en décodant le HEVC en WASM puis en ré-encodant en H.264 via `VideoEncoder` (hardware) pour alimenter
MSE `SourceBuffer` → `<video>` natif.

## Pipeline cible

```
HLS/DASH segments (fMP4 HEVC)
  → FMP4Demuxer.parseInit() + parseSegment()     [existe]
  → extract HEVC NAL units                        [existe]
  → hevc_decoder_feed() (incrémental, par segment) [à créer]
  → hevc_decoder_drain() → HEVCFrame[] (YUV)      [à créer]
  → VideoFrame(I420)                               [à créer]
  → VideoEncoder(H.264) — hardware                 [à créer]
  → EncodedVideoChunk
  → fMP4 muxer (init avc1/avcC + media moof/mdat) [à créer]
  → MSE SourceBuffer.appendBuffer()                [à créer]
  → <video> natif — play/pause/seek/PiP/fullscreen
```

## Compatibilité navigateurs

| API requise | Chrome | Firefox | Safari |
|------------|--------|---------|--------|
| VideoEncoder H.264 | 94+ | **Non fonctionnel** (bug Mozilla) | 16.4+ |
| VideoFrame(I420) | 94+ | 130+ | 16.4+ |
| MSE + H.264 | Oui | Oui | Oui |
| HEVC natif MSE | 107+ (hw) | 139+ (partiel) | Oui |

**Firefox** : expose `VideoEncoder` depuis v130, mais l'encodage H.264 échoue systématiquement
([Bug 1918769](https://bugzilla.mozilla.org/show_bug.cgi?id=1918769)). `isConfigSupported()` peut même
retourner `true` à tort. hevc.js détecte ce cas et se désactive proprement (fallback vers les levels AVC natifs).

Safari supporte déjà HEVC nativement → notre pipeline n'est utile que quand le navigateur NE supporte PAS HEVC natif.

---

## Étape 1 — API C++ incrémentale (feed / drain / flush)

### Problème actuel

- `Decoder::decode()` est batch : prend un bitstream complet
- `output_pictures()` est peek-only : retourne TOUTES les pictures jamais décodées (croît indéfiniment)
- Pas de mécanisme de flush pour les B-frames en attente

### Contrainte spec

> **ABSOLU** : Toute modification du décodeur C++ DOIT être conforme à la spec ITU-T H.265 (v8, 2021).
> En particulier pour cette étape :
> - **§C.5 — DPB bumping process** : l'output ordering et l'éviction des pictures du DPB doivent suivre le processus défini dans la spec (bumping quand DPB plein, output en POC order au sein d'un CVS)
> - **§C.3 — Decoded picture output** : les conditions de sortie d'une picture (`PicOutputFlag`, `needed_for_output`) sont définies par la spec
> - **§C.4 — DPB parameters** : `sps_max_dec_pic_buffering_minus1`, `sps_max_num_reorder_pics`, `sps_max_latency_increase_plus1` contrôlent le comportement du DPB
>
> Ne PAS implémenter un drain/flush "simplifié" qui ignorerait ces paramètres.
> Lire le PDF (§C.3-C.5, pages ~280-285) avant de coder.

### Ce que le C++ supporte déjà

- `ParameterSetManager` persiste VPS/SPS/PPS entre les appels ✓
- `DPB` persiste entre les appels ✓
- `dpb_.get_output_pictures()` existe et DRAINE (met `needed_for_output = false`) mais n'est pas utilisé par l'API C

### Fichiers à modifier

| Fichier | Changement |
|---------|-----------|
| `src/decoding/decoder.h` | Ajouter `drain()`, `flush()` |
| `src/decoding/decoder.cpp` | Implémenter drain (utilise `dpb_.get_output_pictures()`), flush (force output restant), evict DPB |
| `src/decoding/dpb.h` / `dpb.cpp` | Ajouter `evict_unused()` pour libérer les pictures non-ref + non-output |
| `src/wasm/hevc_api.h` | Ajouter `hevc_decoder_feed()`, `hevc_decoder_drain()`, `hevc_decoder_get_drained_frame()`, `hevc_decoder_flush()` |
| `src/wasm/hevc_api.cpp` | Implémenter les wrappers C |
| `CMakeLists.txt` | Ajouter les 4 nouvelles fonctions à `EXPORTED_FUNCTIONS` |

### API C

```c
// Feed un chunk de données (un ou plusieurs NAL units complets avec start codes)
// Le Decoder accumule les parameter sets et décode les pictures.
int hevc_decoder_feed(HEVCDecoder* dec, const uint8_t* data, size_t size);

// Drain les frames nouvellement décodées (display order).
// Retourne le nombre de frames disponibles dans *count.
int hevc_decoder_drain(HEVCDecoder* dec, int* count);

// Récupérer une frame drainée par index (0 à count-1)
int hevc_decoder_get_drained_frame(HEVCDecoder* dec, int index, HEVCFrame* frame);

// Flush : force l'output de toutes les frames restantes dans le DPB (fin de stream)
int hevc_decoder_flush(HEVCDecoder* dec);
```

### Implémentation — contraintes spec

Le `drain()` doit implémenter le **DPB bumping process (§C.5.2.4)** :
- Une picture est "bumpable" quand elle est `needed_for_output` ET son POC ≤ POC de toutes les autres pictures `needed_for_output` dans le même CVS
- Le bumping est déclenché quand :
  - Le nombre de pictures `needed_for_output` dépasse `sps_max_num_reorder_pics[HighestTid]`
  - OU le DPB est plein (`sps_max_dec_pic_buffering_minus1[HighestTid] + 1`)
  - OU une latence max est atteinte (`sps_max_latency_increase_plus1`)
- L'éviction ne peut supprimer que les pictures qui sont **ni référence ni needed_for_output**

Le `flush()` doit bumper toutes les pictures restantes `needed_for_output`, en POC order.

Lire `pdftotext -f 280 -l 285 docs/spec/pdf/T-REC-H.265-202108-S.pdf -` avant de coder.

### Tests étape 1

1. **Test unitaire C++ `test_incremental_decode`** :
   - Découper `full_qcif_10f.265` en 2 chunks (au boundary d'un keyframe)
   - Feed chunk 1 → drain → vérifier qu'on obtient des frames
   - Feed chunk 2 → drain → vérifier qu'on obtient les frames restantes
   - Flush → vérifier que les B-frames buffered sont sorties
   - Concaténer tous les YUV → MD5 doit matcher le batch decode
   - **Vérifier l'output order** : les POC doivent être strictement croissants au sein de chaque CVS (conformité §C.5)

2. **Test DPB conformité** :
   - Feed un bitstream avec `sps_max_num_reorder_pics = 2`
   - Vérifier que le DPB ne contient jamais plus de `sps_max_dec_pic_buffering_minus1 + 1` pictures
   - Vérifier que drain() retourne les frames dans le bon ordre même avec des B-frames réordonnées

3. **Test mémoire DPB** :
   - Feed 50 frames → drain après chaque feed
   - Vérifier que `dpb_.pictures_.size()` reste borné

4. **Non-régression** : `ctest --output-on-failure` — les 122 tests existants doivent toujours passer

---

## Étape 2 — Wrapper TypeScript incrémental (`@hevcjs/core`)

### Fichiers à modifier

| Fichier | Changement |
|---------|-----------|
| `packages/core/src/decoder.ts` | Ajouter `feed()`, `drain()`, `flush()` avec cwrap |
| `packages/core/src/types.ts` | Ajouter types `DrainResult` |
| `packages/core/src/worker.ts` | Ajouter messages `feed` / `drain` / `flush` |
| `packages/core/src/index.ts` | Exporter les nouveaux types |

### API TypeScript

```typescript
class HEVCDecoder {
  // Existant
  static create(options?: DecoderOptions): Promise<HEVCDecoder>;
  decode(data: Uint8Array): DecodeResult;  // batch (conservé)
  destroy(): void;

  // Nouveau — incrémental
  feed(data: Uint8Array): void;
  drain(): HEVCFrame[];
  flush(): HEVCFrame[];
}
```

### Tests étape 2

1. **Test Node.js** (script `.mjs`) :
   - Charger le WASM natif (pas Emscripten, via le build natif)
   - Pas possible directement → tester via une page HTML locale

2. **Test page HTML `demo/test-incremental.html`** :
   - Charger le WASM
   - `fetch('full_qcif_10f.265')` → split en 2 chunks
   - `decoder.feed(chunk1)` → `decoder.drain()` → afficher count
   - `decoder.feed(chunk2)` → `decoder.drain()` → afficher count
   - `decoder.flush()` → afficher count
   - Total frames === 10
   - Afficher chaque frame sur un canvas WebGL pour vérification visuelle

3. **Build** : `pnpm build:wasm && pnpm -r build` doit passer

---

## Étape 3a — VideoEncoder wrapper (H.264)

### Nouveau fichier

`packages/videojs/src/h264-encoder.ts`

### API

```typescript
class H264Encoder {
  constructor(config: { width: number, height: number, fps?: number, bitrate?: number })

  // Encode une frame YUV (crée un VideoFrame I420, encode en H.264)
  encode(frame: HEVCFrame, timestampUs: number, keyFrame?: boolean): void;

  // Callback quand un chunk H.264 est prêt
  onChunk: (chunk: EncodedVideoChunk, metadata?: EncodedVideoChunkMetadata) => void;

  // Récupérer le avcC description (dispo après le 1er chunk avec metadata.decoderConfig)
  get codecDescription(): Uint8Array | null;

  // Flush l'encoder (attend les chunks en attente)
  flush(): Promise<void>;

  close(): void;
}
```

### Détails clés

- Codec : `"avc1.42001f"` (Baseline L3.1) — supporté partout
- `hardwareAcceleration: "prefer-hardware"`
- Conversion YUV : `Uint16Array` planes → `Uint8Array` I420 (shift par bit_depth - 8)
- Pattern de conversion réutilisé depuis `packages/videojs/src/renderer.ts:74-103`
- Premier chunk : capturer `metadata.decoderConfig.description` = `avcC` pour le muxer

### Tests étape 3a

1. **Test page HTML `demo/test-encoder.html`** :
   - Décoder `bbb360.265` en batch (petit fichier)
   - Encoder chaque frame avec `H264Encoder`
   - Vérifier : `onChunk` est appelé pour chaque frame
   - Vérifier : `codecDescription` est non-null après le 1er chunk
   - Vérifier : le 1er chunk est un keyframe
   - Afficher : nombre de chunks, taille totale, temps d'encodage

---

## Étape 3b — fMP4 Muxer

### Nouveau fichier

`packages/videojs/src/fmp4-muxer.ts`

### API

```typescript
class FMP4Muxer {
  // Générer le init segment (ftyp + moov avec avc1/avcC)
  generateInit(config: {
    width: number,
    height: number,
    timescale: number,
    avcC: Uint8Array,    // description from VideoEncoder metadata
  }): Uint8Array;

  // Muxer un segment média (moof + mdat) depuis des EncodedVideoChunks
  muxSegment(samples: {
    data: Uint8Array,
    duration: number,     // en timescale units
    isKeyframe: boolean,
    compositionTimeOffset: number,
  }[], baseDecodeTime: number): Uint8Array;
}
```

### Boxes à écrire

**Init segment** :
```
ftyp (isom, iso5, avc1)
moov
  mvhd
  trak
    tkhd
    mdia
      mdhd
      hdlr (vide)
      minf
        vmhd
        dinf/dref
        stbl
          stsd
            avc1
              avcC (from encoder)
          stts (empty)
          stsc (empty)
          stsz (empty)
          stco (empty)
  mvex
    trex
```

**Media segment** :
```
moof
  mfhd (sequence_number)
  traf
    tfhd (track_id, default_base_is_moof)
    tfdt (baseDecodeTime)
    trun (sample_count, data_offset, durations, sizes, flags, cto)
mdat (concatenated sample data)
```

### Référence

Le `fmp4-demuxer.ts` existant contient toutes les constantes de box types et la structure des FullBox headers — réutiliser les constantes.

### Tests étape 3b

1. **Test unitaire** : générer un init segment → vérifier avec `mp4box -info` (si dispo) ou en le parsant avec notre propre demuxer
2. **Test round-trip** : `muxer.generateInit()` → `demuxer.parseInit()` → vérifier que les tracks sont correctement lues
3. **Test segment** : `muxer.muxSegment([...])` → `demuxer.parseSegment()` → vérifier samples count et timestamps

---

## Étape 3c — MSE Controller

### Nouveau fichier

`packages/videojs/src/mse-controller.ts`

### API

```typescript
class MSEController {
  constructor(video: HTMLVideoElement);

  // Init : ouvrir MediaSource, créer SourceBuffer H.264
  init(initSegment: Uint8Array, codec?: string): Promise<void>;

  // Append un media segment (attend updateend avant le suivant)
  appendSegment(segment: Uint8Array): Promise<void>;

  // Buffer management : supprimer les anciens ranges
  trimBuffer(keepSeconds?: number): Promise<void>;

  // Fin de stream
  endOfStream(): void;

  get buffered(): TimeRanges;
  get duration(): number;

  destroy(): void;
}
```

### Détails

- MIME type : `'video/mp4; codecs="avc1.42001f"'`
- File d'attente : un seul `appendBuffer()` à la fois, attendre `updateend`
- `QuotaExceededError` : `trimBuffer()` avant retry
- Garder max ~30s de buffer en avance

### Tests étape 3c

1. **Test page HTML `demo/test-mse.html`** :
   - Générer un init segment H.264 valide (encoder 1 frame → muxer)
   - Créer `MediaSource` + `SourceBuffer`
   - `appendBuffer(initSegment)` → vérifier pas d'erreur
   - `appendBuffer(mediaSegment)` → vérifier `video.buffered.length > 0`
   - `video.play()` → vérifier que la vidéo joue

---

## Étape 3d — Pipeline Orchestrator (transcode)

### Nouveau fichier

`packages/videojs/src/transcode-pipeline.ts`

### API

```typescript
class TranscodePipeline {
  constructor(config: {
    wasmUrl?: string,
    videoElement: HTMLVideoElement,
  });

  // Initialiser le pipeline (créer decoder, encoder, MSE)
  init(): Promise<void>;

  // Traiter un init segment fMP4 HEVC
  processInitSegment(data: Uint8Array): void;

  // Traiter un media segment fMP4 HEVC → decode → encode → MSE
  processMediaSegment(data: Uint8Array): Promise<void>;

  // Fin de stream
  flush(): Promise<void>;

  destroy(): void;
}
```

### Flow par segment

```
processMediaSegment(segmentData)
  1. demuxer.parseSegment(segmentData) → DemuxedSample[]
  2. Pour chaque sample :
     a. Prépend start codes (00 00 00 01) aux NAL units
     b. Concaténer en un buffer
  3. decoder.feed(nalBuffer)
  4. decoder.drain() → HEVCFrame[]
  5. Pour chaque frame :
     a. encoder.encode(frame, timestamp, isFirstInSegment)
  6. encoder.onChunk accumule les chunks
  7. Quand tous les chunks du segment sont prêts :
     a. Si premier segment : muxer.generateInit() → mse.init()
     b. muxer.muxSegment(chunks) → mse.appendSegment()
```

### Tests étape 3d

1. **Test page HTML `demo/test-pipeline.html`** :
   - Charger `bbb360.265` (ou un fMP4 HEVC local)
   - Créer `TranscodePipeline` avec une `<video>`
   - `pipeline.init()`
   - Si raw .265 : feed directement, skip le demux
   - Si fMP4 : `processInitSegment()` + `processMediaSegment()`
   - Vérifier : `<video>` joue, `video.buffered.end(0) > 0`
   - Vérifier : pas de glitch visuel

2. **Test perf** :
   - Mesurer le temps total : decode WASM + encode H.264 + mux + MSE append
   - Cible : < 33ms par frame en 1080p (pour du 30fps real-time)

---

## Étape 4 — SourceHandler Video.js

### Approche

Remplacer la Tech actuelle par un **SourceHandler** qui s'enregistre sur le Tech Html5.
Un SourceHandler intercepte uniquement les sources HEVC, en laissant Video.js gérer l'UI.

Ref : `PLAYER-INTEGRATIONS.md` de hevc-gpu (déjà lu).

### Fichiers

| Fichier | Action |
|---------|--------|
| `packages/videojs/src/source-handler.ts` | **Nouveau** — SourceHandler |
| `packages/videojs/src/tech.ts` | Déprécier (conserver pour backward compat) |
| `packages/videojs/src/index.ts` | Exporter le SourceHandler |

### API

```typescript
// Auto-enregistrement à l'import
import 'hevc.js/videojs';

// Détection automatique : si la source est HEVC et le browser ne la supporte pas
// nativement, le SourceHandler prend le relais.

const player = videojs('my-video', {
  sources: [{
    src: 'https://example.com/stream.m3u8',
    type: 'application/x-mpegURL'
  }]
});
```

### Intégration hls.js

Le SourceHandler intercepte les segments via l'API hls.js :

```typescript
hls.on(Hls.Events.FRAG_LOADING, (event, data) => {
  // Intercept HEVC fragment loading
});

hls.on(Hls.Events.FRAG_LOADED, (event, data) => {
  // Route through TranscodePipeline instead of MSE direct
});
```

### Tests étape 4

1. **Test page HTML `demo/test-videojs.html`** :
   - Video.js player avec source HLS HEVC locale
   - Vérifier : le player charge, bufferise, joue
   - Vérifier : les contrôles Video.js fonctionnent (play/pause/seek/fullscreen)

2. **Test fallback** :
   - Si le navigateur supporte HEVC nativement → SourceHandler ne s'active pas
   - Si pas de `VideoEncoder` → fallback WebGL/canvas (renderer existant)

---

## Étape 5 — Worker + Performance

### Objectif

Déplacer le pipeline decode+encode dans un Web Worker pour ne pas bloquer le main thread.

### Architecture

```
Main thread :
  - Video.js UI
  - MSE SourceBuffer (doit rester sur le main thread)
  - Reçoit les segments fMP4 H.264 du Worker

Worker :
  - WASM decoder (feed/drain)
  - VideoEncoder (H.264)
  - fMP4 muxer
  - Envoie les segments fMP4 H.264 via postMessage (transferable)
```

### Tests étape 5

1. **Benchmark** :
   - Comparer main-thread vs Worker : fps, jank, latence
   - Cible : 0 frame drop sur 30s de 1080p

2. **Test longue durée** :
   - Jouer 5 minutes continu
   - Vérifier : mémoire stable (pas de leak), DPB borné

---

## Ordre d'exécution et dépendances

```
Étape 1 (C++ API)          ← bloquant pour tout le reste
  │
  v
Étape 2 (JS wrapper)       ← bloquant pour étape 3d
  │
  ├──────────────────┐
  v                  v
Étape 3a (Encoder)  Étape 3b (Muxer)  Étape 3c (MSE)   ← parallélisables
  │                  │                  │
  └──────────────────┴──────────────────┘
                     │
                     v
               Étape 3d (Pipeline)    ← intègre 3a+3b+3c
                     │
                     v
               Étape 4 (SourceHandler Video.js)
                     │
                     v
               Étape 5 (Worker + perf)
```

## Fichiers critiques

### Existants à modifier
- `src/decoding/decoder.h` / `decoder.cpp` — drain/flush
- `src/decoding/dpb.h` / `dpb.cpp` — evict_unused
- `src/wasm/hevc_api.h` / `hevc_api.cpp` — nouvelles fonctions C
- `CMakeLists.txt` — EXPORTED_FUNCTIONS
- `packages/core/src/decoder.ts` — feed/drain/flush TS
- `packages/core/src/types.ts` — nouveaux types
- `packages/videojs/src/index.ts` — exports

### Nouveaux fichiers
- `packages/videojs/src/h264-encoder.ts` — wrapper VideoEncoder
- `packages/videojs/src/fmp4-muxer.ts` — générateur fMP4 H.264
- `packages/videojs/src/mse-controller.ts` — MediaSource + SourceBuffer
- `packages/videojs/src/transcode-pipeline.ts` — orchestrateur
- `packages/videojs/src/source-handler.ts` — SourceHandler Video.js
- `demo/test-incremental.html` — test étape 2
- `demo/test-encoder.html` — test étape 3a
- `demo/test-mse.html` — test étape 3c
- `demo/test-pipeline.html` — test étape 3d
- `demo/test-videojs.html` — test étape 4

### Références (lecture seule)
- `packages/videojs/src/fmp4-demuxer.ts` — constantes box types pour le muxer
- `packages/videojs/src/renderer.ts:74-103` — conversion YUV I420
- `~/Sites/hevc-gpu/src/gpu/video-output.ts` — pattern VideoFrame + MediaStreamTrackGenerator
- `~/Sites/hevc-gpu/PLAYER-INTEGRATIONS.md` — stratégie SourceHandler
