# Phase 8 — Intégration WebAssembly

## Objectif
Faire tourner le décodeur dans un navigateur via WebAssembly, avec une API JS propre et un Web Worker.

## Prérequis
Phase 6 minimum (Main profile). Peut démarrer en parallèle de Phase 7.

## Tâches

### 8.1 — API C d'embedding
- [ ] Interface C propre (pas de C++ dans l'API publique) :
  ```c
  HEVCDecoder* hevc_decoder_create(void);
  void hevc_decoder_destroy(HEVCDecoder* dec);
  int hevc_decoder_feed(HEVCDecoder* dec, const uint8_t* data, size_t size);
  int hevc_decoder_get_frame(HEVCDecoder* dec, HEVCFrame* frame);
  void hevc_decoder_flush(HEVCDecoder* dec);
  int hevc_decoder_get_info(HEVCDecoder* dec, HEVCStreamInfo* info);
  ```
- [ ] Structure `HEVCFrame` : pointeurs vers les plans Y/Cb/Cr, dimensions, stride, POC
- [ ] Structure `HEVCStreamInfo` : dimensions, profil, level, bit depth, chroma format
- [ ] Gestion du feeding incrémental (pas besoin d'avoir tout le fichier)

### 8.2 — Build Emscripten
- [ ] CMake toolchain Emscripten
- [ ] Options de compilation WASM :
  - `-s WASM=1`
  - `-s ALLOW_MEMORY_GROWTH=1`
  - `-s MODULARIZE=1`
  - `-s EXPORT_NAME=HEVCDecoder`
  - `-s EXPORTED_FUNCTIONS=[...]`
  - `-O3` pour la performance
- [ ] Taille du .wasm optimisée (strip, -Os si perf OK)
- [ ] Vérifier que le build WASM produit les mêmes résultats que le build natif

### 8.3 — Bindings JavaScript
- [ ] Wrapper JS autour des fonctions C (cwrap ou Embind)
- [ ] Gestion de la mémoire WASM (malloc/free pour les buffers de données)
- [ ] API JS promise-based :
  ```js
  const decoder = await HEVCDecoder.create();
  decoder.feed(uint8Array);
  const frame = decoder.getFrame(); // { y, cb, cr, width, height, ... }
  decoder.destroy();
  ```

### 8.4 — Web Worker
- [ ] Décodage dans un Web Worker (pas de blocage du thread principal)
- [ ] Communication via postMessage
- [ ] Transfert des frames via Transferable (ou SharedArrayBuffer)
- [ ] Protocol de messages :
  ```
  Main -> Worker : { type: 'feed', data: ArrayBuffer }
  Main -> Worker : { type: 'flush' }
  Worker -> Main : { type: 'frame', frame: { y, cb, cr, ... } }
  Worker -> Main : { type: 'info', info: { width, height, ... } }
  Worker -> Main : { type: 'error', message: string }
  ```

### 8.5 — Rendu
- [ ] Option 1 : YUV -> RGB conversion en JS/WASM, puis Canvas 2D
- [ ] Option 2 : YUV -> WebGL (meilleure perf, conversion dans le shader)
- [ ] Shader YUV -> RGB :
  ```glsl
  // BT.709 (HD/4K)
  vec3 yuv = vec3(texY, texCb - 0.5, texCr - 0.5);
  vec3 rgb = mat3(1.0, 1.0, 1.0,
                  0.0, -0.187, 1.856,
                  1.575, -0.468, 0.0) * yuv;
  ```
- [ ] Gestion du chroma upsampling (4:2:0 -> 4:4:4 pour l'affichage)

### 8.6 — Démo HTML
- [ ] Page HTML minimale avec :
  - File input pour charger un .265
  - Canvas pour afficher les frames
  - Contrôles play/pause/step
  - Infos : dimensions, profil, FPS, frame count
- [ ] Streaming : lecture progressive du fichier (fetch + ReadableStream)

### 8.7 — Memory Management
- [ ] Pool de buffers pour les frames (éviter malloc/free par frame)
- [ ] Limite de mémoire WASM raisonnable (128MB initial, growth)
- [ ] Monitoring de la mémoire utilisée

## Critère de sortie

- Démo HTML fonctionnelle qui décode et affiche un fichier .265
- Fonctionne sur Chrome et Firefox
- Pas de crash mémoire sur des séquences longues
- Les frames sont identiques au build natif (pas de régression WASM)

## Estimation de complexité
Modérée. Le code de décodage existe déjà. C'est principalement de l'intégration et du plumbing.
