# Architecture Decisions

## AD-001 : Bit depth — Runtime, pas templates

**Contexte** : Le plan mentionne "templates pour le bit depth (8/10/16)". Deux approches possibles :
- Template `<int BitDepth>` : specialisation compile-time, performance max, code duplique
- Runtime `int bit_depth` : un seul chemin de code, plus simple, branchement runtime

**Decision** : Runtime avec `uint16_t` comme type de sample universel.

**Justification** :
- Le Main profile (Phase 4-6) est 8-bit only. Pas besoin de templates avant Phase 7
- `uint16_t` contient 8-bit et 10-bit sans surcharge
- La performance viendra du SIMD (Phase 9), pas des templates
- Moins de code = moins de bugs de conformite

**Consequence** : `Pixel = uint16_t` partout. Les shifts/clips utilisent `BitDepth` en runtime.

## AD-002 : Picture layout — Planar (separate planes)

**Decision** : 3 buffers separes Y, Cb, Cr avec stride independant.

**Justification** :
- La spec traite chaque composante independamment
- Le deblocking et SAO operent plan par plan
- L'interpolation chroma a des dimensions differentes
- Les filtres SIMD preferent les donnees contiguees par plan

**Structure** :
```cpp
struct Picture {
    std::vector<uint16_t> planes[3];  // Y=0, Cb=1, Cr=2
    int width[3], height[3];           // dimensions par plan
    int stride[3];                     // stride par plan (en samples)
    int bit_depth;
    int chroma_format_idc;             // 1=420, 2=422, 3=444
    int poc;
};
```

## AD-003 : Parameter set ownership — Stored by value, indexed by ID

**Decision** : `std::array<std::optional<VPS>, 16>` pour les VPS, idem SPS (16 max), PPS (64 max).

**Justification** :
- Les IDs sont bornes par la spec (VPS: 0-15, SPS: 0-15, PPS: 0-63)
- Pas besoin de map dynamique
- Copie complete a chaque mise a jour (les PS sont petits)
- Le slice header reference le PPS actif par ID

## AD-004 : Error handling — Assertions en debug, codes d'erreur en release

**Decision** :
- Debug : `assert()` + `HEVC_DEBUG` logging pour tracer les valeurs intermediaires
- Release : fonctions retournent `bool` ou `enum HEVCError`
- Pas d'exceptions C++ (requis pour l'API C WASM)

## AD-005 : CABAC — Struct of arrays pour les contextes

**Decision** : Tous les contextes dans un seul tableau flat, indexes par ctxIdx global.

```cpp
struct CabacContext {
    uint8_t pStateIdx;
    uint8_t valMps;
};

struct CabacEngine {
    CabacContext contexts[256];  // tous les contextes
    uint16_t ivlCurrRange;
    uint16_t ivlOffset;
    BitstreamReader* bs;
};
```

**Justification** :
- Acces O(1) par ctxIdx
- Init simple (boucle sur le tableau)
- Save/restore pour WPP en un memcpy

## AD-006 : Intermediate buffers — Stack-allocated, taille max CTU (STACK_SIZE=256KB en WASM)

**Decision** : Les buffers temporaires (prediction, residual, transform) sont alloues sur la stack avec la taille max du CTU (64x64).

```cpp
// Dans la fonction de decodage d'un CU :
int16_t pred_samples[64 * 64];
int16_t residual[64 * 64];
int32_t transform_buf[64 * 64];  // precision etendue pour transform
```

**Justification** :
- Pas d'allocation dynamique dans le hot path
- 64*64*4 = 16KB par buffer, ~48KB total — acceptable sur la stack
- WASM STACK_SIZE configure a 256KB (defaut Emscripten = 64KB, insuffisant avec recursion quad-tree + 48KB buffers)
