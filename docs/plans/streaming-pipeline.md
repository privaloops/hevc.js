# Plan : Pipeline incrémental (streaming decode→encode)

## Problème

Avec des gros segments CMAF (ARTE : 300 frames / 12s / 1.4MB), le premier frame H.264 apparaît après **5.5s** car le pipeline est séquentiel :

```
feed(300 NALs) → drain(300 frames) → encode(300) → flush → mux → append MSE
```

Objectif : **<500ms au premier frame** via un pipeline incrémental.

## Architecture actuelle (séquentielle)

### Fichiers concernés

| Fichier | Rôle | Lignes clés |
|---------|------|-------------|
| `packages/core/src/segment-transcoder.ts` | Pipeline demux→decode→encode→mux | `processMediaSegment()` L118-251 |
| `packages/core/src/transcode-worker.ts` | Worker wrapper, protocole postMessage | `mediaSegment` handler L60-95 |
| `packages/core/src/transcode-worker-client.ts` | Client main-thread du worker | `processMediaSegment()` L55-75 |
| `packages/core/src/mse-intercept.ts` | Proxy MSE, appelle transcodeMedia | `processNext()` L305-455, `transcodeMedia()` L285-290 |
| `packages/core/src/h264-encoder.ts` | WebCodecs VideoEncoder wrapper | `encode()` L82-121, `flush()` L124 |
| `packages/core/src/fmp4-muxer.ts` | Muxer fMP4 (moof+mdat) | `muxSegment()` L38 |
| `packages/core/src/decoder.ts` | WASM HEVC decoder wrapper | `feed()` L194, `drain()` L210 |

### Flow actuel

```
mse-intercept.ts : processNext()
  → transcodeMedia(hevcSegment)                    // L340
    → [Worker] processMediaSegment(data)           // transcode-worker.ts L69
      → segmentTranscoder.processMediaSegment()    // segment-transcoder.ts L118
        1. demuxer.parseSegment(data)              // → samples[] avec NAL units
        2. extractTfdt(data)                       // → segmentBaseTime
        3. for sample: decoder.feed(nalBuffer)     // feed TOUS les NALs d'un coup
        4. decoder.drain()                         // drain TOUTES les frames d'un coup
        5. for frame: encoder.encode(frame, ts)    // encode TOUTES les frames
        6. encoder.flush()                         // attendre que WebCodecs finisse
        7. muxer.muxSegment(chunks, baseTime)      // mux en un seul fMP4
        8. return mediaSegment                     // un seul Uint8Array
  ← h264Segment (Uint8Array)
  → realAppend(h264Segment)                        // append MSE unique
```

## Architecture cible (incrémentale)

### Principe

Le décodeur HEVC a un DPB (Decoded Picture Buffer). Quand on feed un NAL, le DPB peut libérer 0 ou N frames (selon les B-frames en attente). On peut appeler `drain()` après chaque `feed()` pour récupérer les frames disponibles.

On accumule les frames décodées et, dès qu'on atteint un batch de ~30 frames (~1.2s à 25fps), on encode+mux+émet un segment partiel. Le premier batch sort après ~30 feeds × ~15ms/frame = **~450ms** au lieu de 5.5s.

### Flow cible

```
mse-intercept.ts : processNext()
  → transcodeMediaStreaming(hevcSegment, onPartial)
    → [Worker] processMediaSegmentStreaming(data)
      → segmentTranscoder.processMediaSegmentStreaming(data, onChunk)
        1. demuxer.parseSegment(data)
        2. extractTfdt(data)
        3. for EACH sample:
           a. decoder.feed(nalBuffer)              // 1 sample
           b. frames = decoder.drain()             // 0-N frames
           c. for frame: encoder.encode(frame, ts) // encode immédiatement
           d. si batchFrames >= BATCH_SIZE:
              - encoder.flush()
              - muxer.muxSegment(batchChunks, batchBaseTime)
              - onChunk(partialSegment)            // ← EMIT PARTIEL
              - reset batch
        4. encoder.flush() final
        5. muxer.muxSegment(remaining) → onChunk   // dernier batch
  ← [Worker postMessage pour chaque chunk partiel]
  → MSE intercept: realAppend(partialSegment)      // append immédiat
  → waitForUpdateEnd
  → [prochain chunk partiel arrive...]
```

## Étapes d'implémentation

### Étape 1 — `SegmentTranscoder.processMediaSegmentStreaming()`

**Fichier** : `packages/core/src/segment-transcoder.ts`

Ajouter une nouvelle méthode (garder `processMediaSegment` inchangée pour compatibilité) :

```ts
async processMediaSegmentStreaming(
  data: Uint8Array,
  onChunk: (h264: Uint8Array, init: TranscodedInit | null) => void,
): Promise<void>
```

**Logique** :
- Demux normalement (samples = demuxer.parseSegment)
- Boucle sur chaque sample :
  - `decoder.feed(nalBuffer)` pour ce sample
  - `frames = decoder.drain()` — peut retourner 0, 1 ou N frames
  - Pour chaque frame drainée : `encoder.encode(frame, timestampUs, isKeyframe)`
  - Les chunks encodés arrivent dans `encoder.onChunk` callback (async WebCodecs)
  - Quand `batchChunks.length >= BATCH_SIZE` (30) :
    - `await encoder.flush()` — force WebCodecs à sortir les chunks pending
    - Générer `initResult` si pas encore fait (premier keyframe donne avcC)
    - `muxer.muxSegment(batchChunks, batchBaseTime)` → segment partiel
    - `onChunk(partialSegment, initResult)` — émettre
    - Reset batch, avancer `batchBaseTime`
- Après la boucle : flush final + émettre le dernier batch

**Points d'attention** :
- Le premier `encoder.encode()` avec `keyFrame=true` déclenche la génération de `avcC` (dans `_handleOutput` de h264-encoder.ts L160-172). Le `codecDescription` n'est disponible qu'après `flush()`. L'init segment ne peut être généré qu'au premier batch.
- `batchBaseTime` doit avancer de la somme des durées des samples du batch (en timescale units).
- Les timestamps par frame : utiliser les `sampleOffsets` cumulés comme actuellement, pas l'index de batch.
- Le `BATCH_SIZE` de 30 est un bon compromis : ~1.2s à 25fps, assez pour que WebCodecs produise des chunks, pas trop pour garder la latence basse.

### Étape 2 — Protocole Worker streaming

**Fichier** : `packages/core/src/transcode-worker.ts`

Ajouter un nouveau message type `mediaSegmentStreaming` :

```ts
// Main → Worker
{ type: "mediaSegmentStreaming", data: ArrayBuffer, id: number }

// Worker → Main (pour CHAQUE chunk partiel)
{ type: "partialTranscoded", id: number, h264: ArrayBuffer, initSegment?: ArrayBuffer, codec?: string, isFirst: boolean }

// Worker → Main (quand le segment est fini)
{ type: "streamingDone", id: number }
```

Le handler dans le worker :
```ts
case "mediaSegmentStreaming":
  await transcoder.processMediaSegmentStreaming(
    new Uint8Array(msg.data),
    (h264, init) => {
      const transfers = [h264.buffer];
      const resp: any = { type: "partialTranscoded", id: msg.id, h264, isFirst: false };
      if (init) { resp.initSegment = init.initSegment; resp.codec = init.codec; transfers.push(init.initSegment.buffer); resp.isFirst = true; }
      self.postMessage(resp, transfers);
    }
  );
  self.postMessage({ type: "streamingDone", id: msg.id });
  break;
```

**Attention** : les `ArrayBuffer` transférés sont détachés après `postMessage`. Chaque chunk doit être un buffer frais (ce qui est le cas car `muxSegment` crée un nouveau `Uint8Array`).

### Étape 3 — `TranscodeWorkerClient` streaming

**Fichier** : `packages/core/src/transcode-worker-client.ts`

Ajouter :

```ts
async processMediaSegmentStreaming(
  data: Uint8Array,
  onChunk: (h264: Uint8Array, initSegment: Uint8Array | null, codec: string | null) => void,
): Promise<void> {
  const id = this._segmentId++;
  return new Promise<void>((resolve, reject) => {
    const handler = (e: MessageEvent) => {
      const msg = e.data;
      if (msg.id !== id) return;
      if (msg.type === "partialTranscoded") {
        const init = msg.initSegment ? new Uint8Array(msg.initSegment) : null;
        onChunk(new Uint8Array(msg.h264), init, msg.codec ?? null);
      } else if (msg.type === "streamingDone") {
        this._worker.removeEventListener("message", handler);
        resolve();
      } else if (msg.type === "error") {
        this._worker.removeEventListener("message", handler);
        reject(new Error(msg.message));
      }
    };
    this._worker.addEventListener("message", handler);
    this._worker.postMessage(
      { type: "mediaSegmentStreaming", data: data.buffer, id },
      [data.buffer]
    );
  });
}
```

### Étape 4 — MSE intercept : append incrémental

**Fichier** : `packages/core/src/mse-intercept.ts`

Dans `processNext()` (L305-455), remplacer l'appel `transcodeMedia(segment)` par `transcodeMediaStreaming()` :

```ts
// Avant (L340-427) :
const h264Segment = await transcodeMedia(segment);
// ... append init if needed
// ... append media segment

// Après :
await transcodeMediaStreaming(segment, async (h264, initSeg, codec) => {
  if (abortGeneration !== myGeneration) return;
  
  // Append init segment on first chunk
  if (initSeg && !initAppended) {
    initAppended = true;
    lastInitSegment = initSeg;
    if (updatingGetter.call(realSB)) await waitForRealUpdateEnd();
    if (abortGeneration !== myGeneration) return;
    realAppend(initSeg.buffer);
    await waitForRealUpdateEnd();
    if (abortGeneration !== myGeneration) return;
  }
  
  // Append partial H.264 segment
  if (updatingGetter.call(realSB)) await waitForRealUpdateEnd();
  if (abortGeneration !== myGeneration) return;
  realAppend(h264.buffer);
  await waitForRealUpdateEnd();
});
```

**Points d'attention** :
- Chaque `realAppend` + `waitForRealUpdateEnd` est nécessaire car MSE n'accepte qu'un append à la fois.
- Le `abortGeneration` check est critique — si un seek arrive pendant le streaming, on doit arrêter d'appendre les vieux chunks.
- La backpressure (`fakeUpdating`) doit être relâchée **après le premier chunk**, pas après tout le segment.

### Étape 5 — Ajouter `transcodeMediaStreaming` dans l'intercept

**Fichier** : `packages/core/src/mse-intercept.ts`

À côté de `transcodeMedia()` (L285-290), ajouter :

```ts
async function transcodeMediaStreaming(
  segment: Uint8Array,
  onChunk: (h264: Uint8Array, initSegment: Uint8Array | null, codec: string | null) => void,
): Promise<void> {
  if (workerClient) {
    return workerClient.processMediaSegmentStreaming(segment, onChunk);
  }
  // Main-thread fallback
  return transcoder!.processMediaSegmentStreaming(segment, (h264, init) => {
    onChunk(h264, init?.initSegment ?? null, init?.codec ?? null);
  });
}
```

### Étape 6 — Tests

1. **Test local BBB ABR** : `LOCAL_DEMO=1 npx playwright test --project=local-chromium tests/e2e/bugfix-validation.spec.ts` — vérifier que les tests existants passent (backward compatible).
2. **Test ARTE videojs** : ouvrir `http://localhost:8080/videojs.html` avec `forceTranscode: true`, charger ARTE, vérifier que le premier frame apparaît en <1s.
3. **Mesurer** : ajouter un log `[hevc.js] First chunk emitted in ${ms}ms` dans `processMediaSegmentStreaming`.

### Étape 7 — Nettoyage

- Retirer les logs de debug (`[hevc.js] addSourceBuffer(...)` pour tous les mimeTypes — garder seulement HEVC).
- Retirer `forceTranscode: true` de `demo/videojs.html`.
- Rebuild tous les bundles : `pnpm build:demo`.
- Commit + push.

## Risques

1. **WebCodecs `flush()` asynchrone** : `encoder.flush()` attend que WebCodecs sorte tous les chunks pending. Entre deux `encode()` calls, les chunks peuvent ne pas être immédiatement disponibles. Il faut flush par batch, pas par frame.
2. **DPB latence** : avec des B-frames (ARTE), le DPB retient 2-4 frames avant de les sortir. Le premier `drain()` après les premiers feeds peut retourner 0 frames. Le premier batch de 30 frames peut nécessiter ~34 feeds.
3. **Init segment timing** : `codecDescription` (avcC) n'est disponible qu'après le premier `flush()` contenant un keyframe. L'init segment ne peut être émis qu'avec le premier batch.
4. **Abort pendant streaming** : si un seek arrive pendant que `processMediaSegmentStreaming` émet des chunks, il faut que le callback arrête d'appendre. Le `abortGeneration` gère ça.
5. **Ordre des chunks MSE** : chaque chunk partiel doit avoir des timestamps strictement croissants. Le `batchBaseTime` doit être correctement avancé.

## Métriques attendues

| Segment ARTE (300f, 12s) | Avant | Après |
|--------------------------|-------|-------|
| Temps au premier frame | 5.5s | <500ms |
| Temps total segment | 5.5s | ~5.5s (inchangé) |
| Nombre d'appends MSE | 1 | ~10 (un par batch de 30f) |
