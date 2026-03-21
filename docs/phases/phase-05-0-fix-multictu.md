# 5.0 — Fix multi-CTU I-frame CABAC context divergence

## Objectif

Corriger le bug qui cause 21593 pixels faux dans la frame I (176x144, 9 CTUs) alors que le single-CTU (64x64) est pixel-perfect. Ce bug bloque TOUTE la Phase 5.

## Diagnostic complet (session 2026-03-21)

### Ce qui fonctionne
- `oracle_i_64x64_qp22` (1 CTU) : pixel-perfect
- 82 tests unitaires : PASS
- 12 sections spec auditées : toutes conformes

### Ce qui échoue
- Frame 0 de `p_qcif_10f.265` : 21593 pixels faux, premier à (160,0) dans CTU 2

### Localisation par comparaison bin-par-bin avec HM

- 18322 decision bins identiques (val, range, offset)
- Divergence au decision bin 18323 :
  - Notre ctx=90 (sig_coeff_flag luma ctxInc=8) : pStateIdx=62 (saturé)
  - HM même contexte logique : pStateIdx≈31
- Cause : certains bins sont assignés à ctx=90 chez nous mais à un AUTRE contexte dans HM. Les deux contextes ont par coïncidence le même état → r/o identiques → invisible.

### Ce qui a été vérifié et éliminé
- ctxIdxMap (Table 9-50) : identique à libde265
- Scan tables (diag/horiz/vert) : identiques
- coded_sub_block_flag context (§9.3.4.2.4) : identique
- coeff_abs_level_greater1 context (§9.3.4.2.6) : identique
- cRiceParam derivation (§9.3.3.11) : identique
- QP derivation, reference samples, MPM, chroma mode : tous conformes

## Plan de résolution — 6 étapes

### Étape 1 — Ajouter le ctxIdx à la trace HM (20 min)

**Problème** : HM trace val/r/o mais pas le ctxIdx. On ne peut pas comparer les assignations de contexte.

**Fichier** : `/tmp/HM/source/Lib/TLibDecoder/TDecBinCoderCABAC.cpp`

La fonction `decodeBin(UInt& ruiBin, ContextModel &rcCtxModel)` reçoit une référence, pas un index.

**Approche recommandée** — calculer l'offset par adresse mémoire :

1. Trouver le tableau de base des contextes dans HM :
```bash
grep -n "ContextModel\|m_contextModels\|NUM_CTX" \
  /tmp/HM/source/Lib/TLibDecoder/TDecSbac.h | head -30
```

2. Si les contextes sont dans un tableau contigu `m_contextModels[N]`, ajouter dans `TDecBinCoderCABAC.h` :
```cpp
void setContextBase(ContextModel* base) { m_ctxBase = base; }
ContextModel* m_ctxBase = nullptr;
```

3. Appeler `setContextBase` dans `TDecSbac` après l'init

4. Dans `decodeBin`, calculer et tracer :
```cpp
int ctxOff = (m_ctxBase) ? (int)(&rcCtxModel - m_ctxBase) : -1;
fprintf(stderr, "[HD] %d ctx=%d v=%d r=%d o=%d\n",
        hm_decision_seq, ctxOff, ruiBin, m_uiRange, m_uiValue >> 7);
```

**Si les contextes NE SONT PAS contigus** (ContextModel3DBuffer séparés), utiliser l'alternative :
- Modifier la signature : `decodeBin(UInt& ruiBin, ContextModel &rcCtxModel, int ctxIdx = -1)`
- Passer l'offset depuis chaque appel dans `TDecSbac.cpp` (~30 appels)
- Chercher le pattern `m_pcTDecBinIf->decodeBin(uiBin, m_cXXXSCModel.get(...))`
- L'offset est : constante du syntax element + indices du `.get(d, s, i)`

**Rebuild** :
```bash
cd /tmp/HM && make TAppDecoder-r 2>&1 | tail -5
# Le binaire Xcode : /tmp/HM/bin/xcode/clang-17.0/x86_64/release/TAppDecoder
```

### Étape 2 — Générer les deux traces (5 min)

```bash
# Trace HM (100K decision bins suffisent, la divergence est au bin 18323) :
/tmp/HM/bin/xcode/clang-17.0/x86_64/release/TAppDecoder \
  -b tests/conformance/fixtures/p_qcif_10f.265 -o /dev/null \
  2>&1 | grep "^\[HD\]" > /tmp/hm_ctx_trace.txt

# Trace notre décodeur :
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-DCABAC_TRACE_BINS" .
cmake --build build -j8
perl -e 'alarm 120; exec @ARGV' ./build/hevc-decode \
  tests/conformance/fixtures/p_qcif_10f.265 -o /dev/null \
  2>&1 | grep "^DEC " > /tmp/our_ctx_trace.txt
```

Notre trace a le format : `DEC <total_bin> ctx=<N> val=<V> r=<R> o=<O>`
La trace HM doit avoir : `[HD] <dec_bin> ctx=<N> v=<V> r=<R> o=<O>`

**Note** : le bin counter diffère (le nôtre inclut bypass+terminate, celui de HM uniquement les decision bins). Utiliser le NUMÉRO DE LIGNE comme index de comparaison, pas le bin counter.

### Étape 3 — Trouver la première divergence de ctxIdx (5 min)

```bash
# Parser les deux traces et comparer les ctx
paste \
  <(awk '{gsub(/ctx=/,"",$3); print $3}' /tmp/hm_ctx_trace.txt) \
  <(awk '{gsub(/ctx=/,"",$3); print $3}' /tmp/our_ctx_trace.txt | head -100000) \
  | awk '{if ($1 != $2) {
      print "DIVERGE at decision bin " NR ": HM_ctx=" $1 " OUR_ctx=" $2
      exit
    }}'
```

**ATTENTION** : les deux décodeurs peuvent utiliser des numérotations de contexte différentes. Le ctxIdx de HM (global dans `TDecSbac`) n'est PAS le même que notre enum `CabacCtxOffset`. Il faut mapper les deux.

**Mapping** : dans HM, les contextes sont définis dans `TDecSbac.h` avec des offsets par syntax element. Dans notre code, c'est `CabacCtxOffset` dans `cabac_tables.h`. Construire un mapping entre les deux en comparant les premiers bins (qui sont identiques) et en notant quel ctx est utilisé pour split_cu_flag, cbf_luma, sig_coeff_flag, etc.

Alternative : si les ctx ne matchent pas par numéro, comparer les **PAIRES (ctx, val)** — deux décodeurs conformes doivent utiliser le même mapping logique même si les offsets diffèrent. Il suffit de vérifier que le ctx du bin N dans notre trace correspond au MÊME syntax element que le ctx du bin N dans HM.

### Étape 4 — Identifier le syntax element fautif (10 min)

Le bin divergent dira : "notre ctx=X, HM ctx=Y". Chercher quel syntax element utilise ctx=X dans notre code et quel syntax element utilise ctx=Y dans HM. Les possibilités :

| Hypothèse | Comment vérifier |
|-----------|-----------------|
| sig_coeff_flag avec mauvais ctxInc | Vérifier eq 9-40..9-55 pour la position (xC,yC) spécifique |
| cbf_luma/chroma avec mauvais trafoDepth | Vérifier §7.3.8.8 condition de parsing |
| split_transform_flag avec mauvais log2TrafoSize | Vérifier le log2TrafoSize passé au contexte |
| coded_sub_block_flag avec mauvais csbfCtx | Vérifier les voisins sub-block |
| Syntax element non parsé/parsé en trop | Comparer le NOMBRE de bins entre deux marqueurs |

Pour identifier le syntax element : ajouter un log dans notre code qui affiche le nom du syntax element avant chaque `decode_decision` :
```cpp
// Dans chaque decode_xxx function :
HEVC_LOG(CABAC, "SYN: split_cu_flag ctx=%d", ctxIdx);
```

### Étape 5 — Corriger le bug

Le bug sera typiquement :
- Un offset dans l'enum de contexte
- Une condition inversée ou manquante
- Un paramètre incorrect passé à une dérivation de contexte

**Mettre la référence en commentaire** dans le code corrigé :
```cpp
// Fix: [description]. Ref: HM TDecSbac.cpp:XXX / libde265 slice.cc:XXX
```

### Étape 6 — Valider et nettoyer (5 min)

```bash
# 1. Rebuild sans trace
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="" .
cmake --build build -j8

# 2. Tests unitaires
./build/hevc_tests  # 82+ tests doivent PASS

# 3. Non-régression single-CTU
./build/hevc-decode tests/conformance/fixtures/i_64x64_qp22.265 -o /tmp/test.yuv 2>/dev/null
md5 -q /tmp/test.yuv
# DOIT être : 48c64cd2de381113913149e92065d66c

# 4. Multi-CTU I-frame (le vrai test)
./build/hevc-decode tests/conformance/fixtures/p_qcif_10f.265 -o /tmp/test_p.yuv 2>/dev/null
ffmpeg -y -i tests/conformance/fixtures/p_qcif_10f.265 -frames:v 1 -pix_fmt yuv420p /tmp/ref.yuv 2>/dev/null
head -c 38016 /tmp/test_p.yuv > /tmp/test_f0.yuv
python3 tools/oracle_compare.py /tmp/ref.yuv /tmp/test_f0.yuv 176 144
# OBJECTIF : 0 pixels de différence

# 5. Committer
git add <fichiers modifiés>
git commit -m "fix: [description du bug] (§X.Y.Z)"

# 6. Mettre à jour LEARNINGS.md et BACKLOG.md
```

## Fichiers clés

| Fichier | Rôle |
|---------|------|
| `src/decoding/residual_coding.cpp:101` | `derive_sig_coeff_flag_ctx` — dérivation contexte sig_coeff_flag |
| `src/decoding/residual_coding.cpp:175` | `decode_residual_coding` — boucle principale |
| `src/decoding/syntax_elements.cpp` | Toutes les fonctions `decode_xxx` avec calcul d'offset contexte |
| `src/decoding/cabac_tables.h:108` | Enum `CabacCtxOffset` — offsets globaux |
| `src/decoding/cabac.cpp:55` | `decode_decision` — trace CABAC (activée par `CABAC_TRACE_BINS`) |
| `src/decoding/coding_tree.cpp:790` | `decode_transform_tree` — parsing cbf, split_transform |
| `/tmp/HM/source/Lib/TLibDecoder/TDecBinCoderCABAC.cpp:191` | HM trace (à modifier) |
| `/tmp/HM/source/Lib/TLibDecoder/TDecSbac.h` | HM context model declarations |
| `/tmp/libde265/libde265/slice.cc` | libde265 residual_coding (référence alternative) |

## État actuel de HM

HM a déjà été modifié dans une session précédente :
- `/tmp/HM/source/Lib/TLibDecoder/TDecBinCoderCABAC.cpp` : trace `[HD]` avec pS ajoutée
- Le binaire **umake** (`/tmp/HM/bin/umake/...`) est l'ANCIEN (sans pS)
- Le binaire **Xcode** (`/tmp/HM/bin/xcode/...`) est le NOUVEAU (avec pS)
- Pour rebuild : `cd /tmp/HM && make TAppDecoder-r`

## Mapping contextes HM ↔ notre code

HM utilise des `ContextModel3DBuffer` séparés par syntax element. Notre code utilise un enum `CabacCtxOffset` global. Les deux n'ont PAS les mêmes indices.

Pour construire le mapping, utiliser les premiers bins (identiques) de la trace :
- Bin 1 = `split_cu_flag` : notre ctx=2 (`CTX_SPLIT_CU_FLAG`), noter le ctx HM
- Suivre la séquence de syntax elements pour les premiers CUs : split_cu_flag → pred_mode (I-slice: pas parsé) → part_mode → prev_intra_luma_pred_flag → ...
- Construire un dictionnaire HM_ctx → syntax_element en lisant `TDecSbac.cpp`

Alternativement, ne PAS comparer les ctx numériquement mais comparer les **SÉQUENCES** de ctx : si notre trace a `[2, 2, 2, 10, 14, 14, ...]` et HM a `[0, 0, 0, 5, 8, 8, ...]`, les patterns doivent matcher (mêmes répétitions, mêmes transitions). Le PREMIER bin où le pattern diverge est le bug.

## Hypothèse alternative — bug structurel

Si la divergence de ctx n'est PAS un mauvais offset mais un bin EN TROP ou EN MOINS, les deux traces seront décalées d'un bin à partir d'un certain point. Pour détecter ça :
- Comparer les séquences de val : si HM a `[1,1,0,1,...]` et nous `[1,1,1,0,1,...]`, il y a un bin supplémentaire chez nous
- Chercher le point d'insertion/suppression en alignant les séquences (longest common subsequence)

Ce cas arrive quand :
- Un syntax element est parsé chez nous mais pas chez HM (ex: `transform_skip_flag` parsé alors que `transform_skip_enabled_flag=0`)
- Un cbf est parsé avec la mauvaise condition
- Un `coded_sub_block_flag` est parsé/inféré différemment

## Règles

- **Anti-debug itératif** : max 2 itérations de trace/test. Après 2 échecs → relire la spec
- **Référence en commentaire** : si la solution vient de HM/libde265, le documenter dans le code
- **Ne pas committer les traces** : les flags `-DCABAC_TRACE_BINS` sont pour le debug uniquement
- **Tester la non-régression** : `oracle_i_64x64_qp22` doit rester pixel-perfect
- **Working directory** : `/Users/privaloops/Sites/hevc-decode`
- **Timeout** : le décodeur avec `CABAC_TRACE_BINS` est lent (logging stderr). Utiliser `perl -e 'alarm 120; exec @ARGV'` pour limiter
