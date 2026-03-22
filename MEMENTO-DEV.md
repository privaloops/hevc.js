# Memento de dev : Construire un decodeur HEVC avec Claude

> Document vivant. Historique exhaustif du projet, erreurs, pivots, enseignements.
> Destiné à servir de base pour un article de blog.

---

## 1. Genèse du projet

**Date** : 19 mars 2026

**Ambition** : Écrire un décodeur HEVC/H.265 from scratch en C++17, compilé en WebAssembly, conforme pixel-perfect à la spec ITU-T H.265 (v8, 2021, 716 pages). Validé frame-par-frame contre ffmpeg comme oracle.

**Pourquoi c'est intéressant pour un article** :
- C'est un des exercices d'implémentation les plus complexes qui existe en vidéo. HEVC est un monstre : CABAC (codage arithmétique), 35 modes de prédiction intra, quad-tree récursif à 4 niveaux, transform inverse DCT/DST, interpolation fractionnaire...
- L'objectif est **pixel-perfect** — pas "à peu près correct", mais identique au bit près à ffmpeg sur chaque frame.
- La spec fait 716 pages de pseudo-code dense avec des formules mathématiques interdépendantes. Une erreur de signe, un offset de 1, un contexte mal numéroté — et tout le décodage diverge silencieusement.

**Stack** : C++17, CMake, Emscripten (WASM), Google Test, CTest, ffmpeg (oracle), HM (décodeur de référence HEVC).

---

## 2. Chronologie

### Jour 0 — 19 mars 2026 : Préparation massive (11 commits)

Avant même de toucher au code de décodage, une journée entière a été consacrée à préparer le terrain pour un agent IA :

| Commit | Quoi |
|--------|------|
| `cec1e93` | Init avec plan d'implémentation complet (9 phases, 23 pièges de conformité identifiés) |
| `94fa395` | Audit de conformité spec — corrections des notes |
| `da5eda1` | Intégration des findings d'audit dans le plan |
| `30b23b7` | Tables de données spec en C++ (CABAC, DCT, intra, scaling) + squelette Phase 1 |
| `6865392` | Référence au PDF spec dans CLAUDE.md + gitignore |
| `927b0c0` | Enrichissement du contexte agent (agent-guide.md) |
| `e959d99` | Workflow de test oracle dans CLAUDE.md |
| `7cff949` | Fix CLAUDE.md pour conformité agent |
| `a5a371d` | Fixtures de test oracle + intégration CTest |
| `93aaf6c` | Infrastructure "agent readiness" |
| `51472a7` | .cache/ dans gitignore |

**Enseignement n°1 — Le CLAUDE.md comme cerveau externalisé** : Le fichier CLAUDE.md a été itéré 7 fois en une journée. Il sert de briefing complet pour l'agent : structure du projet, commandes de build/test, index des pages du PDF spec, workflow de debug oracle, conventions de nommage, pièges par phase. C'est le document le plus important du repo pour le travail avec Claude.

**Enseignement n°2 — Préparer les données avant le code** : Les tables de données CABAC (init values, rangeTabLps, transIdx), les matrices DCT/DST, les tables intra (angles, filtres) ont été extraites manuellement de la spec et mises sous forme de `.md` copiables en C++. Ça évite que l'agent doive lire/interpréter le PDF à chaque fois.

**Enseignement n°3 — Les "23 pièges de conformité"** : Un audit préalable de la spec a identifié 23 points où les implémentations naïves se plantent systématiquement (CABAC sig_coeff_flag context, clipping inter-passe transform, transposition angular, etc.). Chaque piège a été documenté avec la section spec, la phase concernée, et l'impact. Résultat : certains de ces pièges ont quand même frappé (cf. Phase 4), mais au moins on savait où chercher.

---

### Jour 1 — 20 mars 2026 : Sprint massif (34 commits, 8 PRs)

La journée la plus dense. Phases 1 à 4 en une seule journée.

#### Phase 1 : Infrastructure (PR #1 + #2)

- Rename du projet (hevc-torture → hevc-decode)
- CI GitHub Actions
- Bitstreams de test (toy 64x64, conformance, Big Buck Bunny 1080p/4K)
- Optimisation du BitstreamReader : lecture bit-par-bit → cache 64-bit (O(1) par read_bits)

**PR #2 — `feature/wasm-optimize`** : Optimisation précoce mais justifiée. 3 bottlenecks identifiés et fixés :
1. `read_bits()` : boucle bit-par-bit → cache 64-bit word
2. `more_rbsp_data()` : scan linéaire O(n) → pré-calcul O(1)
3. `write_yuv()` : écriture byte-par-byte → écriture par ligne

**Enseignement n°4 — Quelques optimisations méritent d'être faites tôt** : Normalement "premature optimization is the root of all evil", mais ici le read_bits() allait être appelé des millions de fois dans le décodeur CABAC. Le fixer maintenant évitait de construire tout le décodeur sur une base O(n) par bit.

#### Phase 2 : Bitstream & NAL (PR #5 + audit PR #6)

- NalParser complet : start codes, NAL headers, RBSP extraction, AU boundaries
- 22 tests unitaires
- CLI `--dump-nals`

**Audit post-merge (PR #6)** : 4 correctness issues trouvées après la PR principale :
- Des edge cases dans le parsing
- Chaque phase est suivie d'un "audit" : relecture systématique du code vs la spec

**Enseignement n°5 — Le pattern "implement + audit"** : Chaque phase suit le même cycle : implémentation en une PR, puis une PR d'audit qui corrige les subtilités. L'agent a tendance à produire du code "80% correct" au premier passage. Le 20% restant, ce sont les edge cases que seule une relecture attentive de la spec révèle.

#### Phase 3 : Parameter Sets (PR #7 + audit PR #8)

- VPS, SPS, PPS, SliceHeader complets
- Scaling list data avec fallback matrices (pas trivial)
- Short-term reference picture sets avec inter-prédiction
- ParameterSetManager
- 17 tests unitaires

**Audit (PR #8)** : 6 issues de conformité. Notamment le parsing des scaling lists et des short-term RPS.

#### Phase 4 : Le mur — Intra Prediction

Le commit `dfff932` est le "big bang" : **Phase 4 — Intra Prediction** en un seul commit massif.

Fichiers créés :
- `cabac.cpp/h` — Moteur arithmétique CABAC
- `cabac_tables.h` — 284 lignes de tables d'init
- `coding_tree.cpp/h` — Quad-tree récursif (CTU → CU → PU → TU)
- `residual_coding.cpp/h` — Parsing des coefficients
- `syntax_elements.cpp/h` — Décodage CABAC de chaque syntax element
- `intra_prediction.cpp/h` — 35 modes de prédiction intra
- `transform.cpp/h` — IDCT/IDST inverse + dequant
- `decoder.cpp/h` — Pipeline principal

**Total** : +4715 lignes ajoutées, 3493 lignes dans `src/decoding/` seul.

**Et puis... 22 commits de fix qui ont suivi.**

---

## 3. La guerre des bugs — Phase 4 en détail

C'est ici que l'histoire devient intéressante. Après le commit initial de Phase 4, le test oracle (`oracle_i_64x64_qp22`) a échoué. S'en est suivie une chasse aux bugs qui a duré 2 jours et produit 22 fix commits.

### Les bugs classés par catégorie

#### A. Bugs CABAC / contextes (les plus vicieux)

**Pourquoi c'est le pire** : CABAC est un codeur arithmétique à états. Chaque bin (bit) décodé modifie l'état interne du moteur. Si un seul contexte est mal numéroté, le moteur décode le bon bit *par hasard* pendant quelques bins, puis diverge. Le bug apparaît 50 ou 100 bins plus tard, dans un endroit totalement non relié au bug réel.

| Commit | Bug | Cause racine |
|--------|-----|-------------|
| `04c1bb9` | sig_coeff_flag context derivation | Réécriture complète per spec §9.3.4.2.5 |
| `d680208` | ctxSet cross-sub-block carry | Le carry du ctxSet entre sub-blocks était perdu |
| `d680208` | sig_coeff_flag chroma offset | Offset 27 au lieu de 28 |
| `3a02717` | transform_skip init values | Table 9-29 ctxIdx 126-131 manquantes |
| `ac3a982` | Chroma context mapping | Layout HM ≠ formule spec |
| `14367e4` | Unification luma spec + chroma HM | Refactoring après avoir compris la vraie structure |
| `483a59a` | Context set starts | Luma 8x8 firstSigCtx = 9 (pas 21) |
| `069960f` | Réécriture complète | derive_sig_coeff_flag_ctx réécrit from spec eq 9-40 à 9-55 |
| `2f0c7e3` | CBF_CHROMA 5 contextes | Spec Table 9-22 dit 5, pas 4 |

**Enseignement n°6 — La spec HEVC a des incohérences internes** : C'est le finding le plus important de tout le projet. La formule de la spec (eq 9-55) pour les contextes chroma `sig_coeff_flag` dit `ctxInc = 27 + sigCtx`. Mais les init values dans Table 9-29 sont organisées comme 28 luma + 16 chroma. La spec se contredit elle-même. Le décodeur de référence HM utilise 28, pas 27. Tous les encodeurs suivent HM. **Il faut suivre HM quand la spec est ambiguë**, pas la formule textuelle.

> "The spec formula (eq 9-55: ctxInc = 27 + sigCtx) and the init values in Table 9-29 are internally inconsistent for chroma 4x4."
> — LEARNINGS.md

**Enseignement n°7 — Un bug CABAC = debugger bin par bin** : La méthode de debug qui a fini par fonctionner : compiler HM avec des traces fprintf à chaque bin décodé, faire pareil dans notre décodeur, et diff les deux traces. Le premier bin divergent pointe au bug. C'est fastidieux mais c'est la seule méthode fiable.

#### B. Bugs de prédiction intra

| Commit | Bug | Impact |
|--------|-----|--------|
| `c5fdb77` | Transposition angular modes 2-17 | Tous les modes horizontaux étaient faux |
| `9d58e03` | MPM : DC pour voisin au-dessus d'une frontière CTB | Most Probable Mode mal dérivé |
| `484716e` | Chroma : utilisait le mode luma au lieu du mode chroma dérivé | Toute la chroma intra fausse |
| `92f0f5e` | Strong smoothing biIntFlag | Condition de filtrage incorrecte |
| `b8c6c1d` | 5 bugs intra prediction (spec audit §8.4.4) | Filtering, boundary, mode derivation |

**Enseignement n°8 — La transposition angular** : Les modes intra 2-17 (direction horizontale) fonctionnent par transposition : on transpose les samples de référence, on applique l'algorithme des modes verticaux (18-34), puis on transpose le résultat. Oublier cette transposition = modes horizontaux totalement faux, mais les modes verticaux fonctionnent parfaitement. C'est le genre de bug qui fait passer la moitié des tests mais échoue sur l'autre moitié, rendant le diagnostic confus.

#### C. Bugs de transform / résidu

| Commit | Bug | Impact |
|--------|-----|--------|
| `65c919f` | EGk binarisation off-by-one | Grands coefficients mal décodés |
| `65c919f` | Transform 2D transpose | Passe verticale/horizontale inversées |
| `65c919f` | IntraSplitFlag mal calculé | Mauvaise profondeur de transform |
| `5931e6b` | IDCT32 : terme EEEO manquant | Tous les blocs 32x32 faux |
| `ce851b5` | Scan tables diagonales : x/y swappés | Coefficients lus dans le mauvais ordre |
| `fa061e4` | 3 bugs residual_coding | Scan index, lastSigCoeff swap, MDCS range |
| `8997b30` | scanIdx derivation | Réécrit verbatim depuis §7.4.9.11 |
| `7d8c9aa` | **DST-VII inverse : M au lieu de M^T** | **2023 pixels luma faux** — dernier bug Phase 4 |

**Enseignement n°9 — Les scan tables** : Les coefficients dans un bloc de transform ne sont pas lus en raster scan mais en diagonale (z-scan). Les tables de parcours diagonal avaient x et y inversés sur les diagonales paires. Ce bug est invisible sur les blocs 4x4 (trop petits pour que ça importe beaucoup) mais catastrophique sur les blocs 8x8+.

**Enseignement n°13 — La spec donne la matrice forward, pas l'inverse** : Le bug le plus pernicieux du projet. La spec §8.6.4.2 eq 8-315 définit `transMatrix` pour la DST-VII 4x4 et écrit `y[i] = Σⱼ transMatrix[i][j] · x[j]`. On a transcrit ça fidèlement. Mais cette matrice est la matrice **forward** (analyse). Pour l'inverse (synthèse), il faut la transposée M^T, car DST-VII n'est PAS symétrique. Le piège : DCT-II (qui couvre 95% des blocs) utilise un butterfly auto-transposant — la distinction forward/inverse n'existe pas. Seul DST-VII (4x4 luma intra, ~5% des blocs) utilise une multiplication matricielle explicite, et c'est le seul endroit où ça casse. HM contourne le problème dans `fastInverseDst` via un indexing `c[row] * M[row][column]` qui calcule implicitement M^T · x, même en stockant la matrice forward. **Une transcription parfaite de la formule de la spec peut produire un résultat faux.** C'est la limite ultime du "spec-first".

#### D. Bugs de structure (coding tree, TU, chroma)

| Commit | Bug | Impact |
|--------|-----|--------|
| `67fde9e` | Ref sample availability | Z-scan heuristic au lieu de bit-interleave |
| `20cd158` | OOB chroma write | Bounds check manquant dans reconstruct_block |
| `0f995be` | Sign data hiding parity | Devait inclure le coefficient courant |
| `51d3838` | 3 fixes conformité | Intra prediction + transform unit |
| `a9f696e` | Chroma CBF héritage parent | TU 4x4 déférée : cbf chroma du parent perdu |

**Enseignement n°10 — Le "deferred chroma" 4x4** : En HEVC, quand un bloc de transform fait 4x4 en luma, le chroma correspondant ferait 2x2, ce qui n'existe pas. La spec "défère" le traitement chroma au 4ème sous-bloc (blkIdx=3), qui traite alors un bloc chroma 4x4 couvrant les 4 sous-blocs luma. Cette mécanique de report est une source infinie de bugs : position mal calculée (x0/y0 vs xBase/yBase), CBF chroma hérité du mauvais niveau, mode chroma mal propagé.

---

## 3b. Phase 5 — Inter Prediction : un autre genre de difficulté

### Jour 3 — 22 mars 2026 : Phase 5 finalisée

La Phase 5 a été implémentée progressivement sur plusieurs sessions. Contrairement à Phase 4 (un "big bang" monolithique suivi de 22 fix commits), Phase 5 a suivi un rythme plus incrémental : merge candidates, AMVP, TMVP, interpolation, motion compensation, chacun validé séparément.

**Les vrais problèmes étaient ailleurs que prévu.** L'interpolation luma/chroma et la bi-prédiction ont marché du premier coup. Les bugs étaient dans les couches "autour" : weighted prediction, output frame ordering, et un flag implicite du transform tree.

### Bug n°15 — Explicit Weighted Prediction (§8.5.3.3.4.3)

**Symptôme** : POC 6-9 ont max_diff=11, luma uniquement, chroma parfait. CABAC 100% identique à HM (vérifié sur 45967 bins). La prédiction inter luma donne 188 là où HM donne 182, avec les mêmes ref samples et le même MV.

**Investigation** : L'analyse du PPS a révélé `weighted_pred_flag = 1`. Le code appelait toujours `weighted_pred_default` (§8.5.3.3.4.2), ignorant le flag. La spec §8.5.3.3.4.1 dit :
- P-slice : `weightedPredFlag = weighted_pred_flag`
- B-slice : `weightedPredFlag = weighted_bipred_flag`

Le POC 5 (P-slice, poids triviaux w=128/denom=7) passait par hasard. Le POC 9 (P-slice, poids non-triviaux) échouait, cascadant vers les B-frames 6-8 qui le référencent.

**Fix** : `weighted_pred_explicit()` transcrit depuis spec eq 8-265 à 8-277, + routing selon `weightedPredFlag`.

**Enseignement n°16 — Un flag ignoré = un processus entier manquant** : Le `weighted_pred_flag` était parsé correctement dans le PPS et le slice header, mais jamais utilisé dans le pipeline de prédiction. C'est un pattern de bug différent de Phase 4 (où les bugs étaient des erreurs de transcription). Ici, le code était juste incomplet — un chemin de la spec simplement non implémenté.

### Bug n°16 — Output Frame Ordering Multi-GOP

**Symptôme** : `conf_b_cra_qcif` (3 GOPs avec IDR) — 19/20 frames fausses avec max_diff=138. La reconstruction à (0,0) de POC 4 donne 176 (correct !), mais le YUV de sortie montre 66.

**Investigation hallucinante** : CABAC parfaitement en sync avec HM. Prédiction correcte. Résidu correct. Reconstruction correcte. Mais la sortie est fausse. Après avoir exclu parsing, interpolation, dequant, et reconstruction — le bug était dans le *tri des frames de sortie*.

Chaque IDR remet le POC à 0. Avec 3 GOPs de POC 0-7, trier par POC mélange les frames : [GOP1-POC0, GOP2-POC0, GOP3-POC0, GOP1-POC1, GOP2-POC1, ...]. Le pixel 66 appartenait au POC 4 d'un **autre** GOP.

**Fix** : compteur `cvs_id` incrémenté à chaque IDR, tri par `(cvs_id, poc)`.

**Piège bonus** : le code avait DEUX fonctions de sortie (`DPB::get_output_pictures()` et `Decoder::output_pictures()`) avec des tris différents. Le fix dans l'une ne s'appliquait pas à l'autre. Debugger pendant 30 minutes un fix qui "ne marchait pas" alors qu'il n'était tout simplement pas au bon endroit.

**Enseignement n°17 — Vérifier le pipeline de bout en bout** : Quand la reconstruction est correcte mais la sortie est fausse, le bug est entre les deux. C'est un angle mort naturel du debug — on regarde toujours le décodage, jamais l'écriture du fichier. Résultat : la plus longue investigation de cette session pour un bug de 4 lignes.

### Bug n°17 — interSplitFlag (§7.4.9.4)

**Symptôme** : `conf_b_cabacinit_qcif` — 4 frames sur 20 échouent (8, 10, 14, 18), max_diff=195. CABAC parfait.

**Trouvé par un agent subagent** qui a comparé les SYN traces HM avec notre décodeur. Le CU (64,96) 32x32 avec `PART_2NxN` n'avait pas de TU split, alors que §7.4.9.4 l'exige quand `max_transform_hierarchy_depth_inter == 0`.

Le `split_transform_flag` n'est pas lu du bitstream quand `interSplitFlag == 1` — il est inféré à 1. Sans cette condition, le décodeur lit un bit du bitstream qui n'existe pas, décalant tout le CABAC pour la suite du CU.

**Pourquoi seulement 4 frames** : seules les B-frames avec des CUs 32x32 non-2Nx2N étaient affectées. Les CUs 64x64 forçaient déjà le split via `log2TrafoSize > MaxTbLog2SizeY`. Les CUs 2Nx2N avaient `interSplitFlag=0`.

**Enseignement n°18 — Les conditions implicites de la spec sont les plus dangereuses** : La spec dit "when split_transform_flag is not present, it is inferred to be 1 if interSplitFlag is equal to 1". C'est une phrase parmi 716 pages. Si on ne l'implémente pas, le code fonctionne pour 99% des CUs (ceux qui sont 2Nx2N ou assez gros pour forcer le split autrement). Le 1% restant corrompt silencieusement le CABAC et produit des pixels spectaculairement faux.

### Résumé Phase 5

| Métrique | Valeur |
|----------|--------|
| Tests pixel-perfect | 10/10 |
| Bugs corrigés (total Phase 5) | 17 |
| Bugs session finale | 3 (weighted pred, CVS ordering, interSplitFlag) |
| Fichiers principaux | `interpolation.cpp` (370L), `inter_prediction.cpp` (660L), `dpb.cpp` (440L) |
| Nouveaux sous-processus spec | §8.5.3.3.4.3 (explicit WP), §7.4.9.4 (interSplitFlag) |

**La différence Phase 4 vs Phase 5** : Phase 4 était une guerre de tranchées dans le CABAC — un bug de contexte corrompt tout le reste. Phase 5 était une guerre de mouvement — les bugs étaient dans des endroits surprenants (output ordering, flags PPS ignorés, conditions implicites), chacun demandant une investigation complètement différente. La compétence clé Phase 5 n'est pas "transcrire la spec" mais "vérifier le pipeline de bout en bout".

---

## 4. Les pivots majeurs

### Pivot 1 : Spec-first vs HM-first (20 mars → 21 mars)

**Avant** : L'approche initiale était de transcrire la spec mot-à-mot, en consultant HM uniquement en cas de doute.

**Le mur** : La découverte que la spec est *internalement incohérente* pour les contextes CABAC chroma a forcé un changement de stratégie. Le commit `6a32e79` ajoute une règle dans CLAUDE.md :

> "Chaque fonction de décodage DOIT être une transcription directe de la spec. Vérifier avec HM uniquement quand le résultat de la spec semble ambigu."

**Après** : En pratique, l'approche est devenue hybride — spec d'abord, mais vérification systématique avec HM pour tout ce qui touche aux contextes CABAC. La spec reste la source primaire pour la sémantique (prediction, transform, reconstruction), mais HM fait autorité pour l'organisation mémoire des contextes.

### Pivot 2 : Debug monolithique → sub-phases (21 mars)

**Avant** : Phase 4 était un bloc monolithique. On implémente tout, on lance l'oracle, on debug le mismatch pixel-par-pixel.

**Le mur** : Après 12 bug fixes, le test oracle échouait toujours. Le problème : un bug CABAC corrompt l'état arithmétique, ce qui fait diverger *tous* les bins suivants. Debugger au niveau pixel est alors inutile — il faut descendre au niveau bin.

**Après** : Le commit `5d27880` subdivise la Phase 4 en 6 sous-phases (4A-4F), chacune avec sa propre stratégie de validation :

| Sous-phase | Validation | Granularité |
|------------|-----------|-------------|
| 4A — CABAC Engine | Tests unitaires (7 tests) | Par bin |
| 4B — Coding Tree | Trace syntax elements vs HM | Par syntax element |
| 4C — Residual Contexts | Tests derive_*_ctx() vs HM | Par contexte |
| 4D — Coefficient Parsing | Trace coefficients vs HM | Par TU |
| 4E — Transform + Dequant | Tests vecteurs connus | Par bloc |
| 4F — Prediction + Recon | Oracle pixel-perfect | Par frame |

**Enseignement n°11 — Ne jamais debugger au mauvais niveau d'abstraction** : Quand l'oracle échoue, la tentation est de regarder les pixels. Mais si le bug est dans CABAC (niveau bits), chaque couche au-dessus (coefficients, transform, prediction, reconstruction) est contaminée. Il faut debugger de bas en haut : d'abord les bins, puis les syntax elements, puis les coefficients, puis les pixels. La subdivision en sous-phases formalise cette intuition.

### Pivot 3 : La spec peut être fidèlement transcrite et quand même fausse (21 mars)

**Contexte** : 2023 pixels luma faux restants. Le parsing CABAC est 100% identique à HM (vérifié bin par bin). Les reference samples de prédiction intra sont identiques. La prédiction angulaire donne le même résultat. Mais les résidus après inverse transform sont complètement différents.

**La traque** : Instrumentation de HM pour dumper coefficients, scaled (dequant), et résidus au bloc (60,0). Résultat : coefficients et dequant identiques, mais le DST inverse produit des valeurs différentes.

**Le bug** : La spec eq 8-315 écrit `y[i] = Σⱼ transMatrix[i][j] · x[j]` avec une matrice qui est en fait la matrice DST forward. Pour l'inverse 2D, il faut M^T (la transposée). On a transcrit la formule au pied de la lettre → forward au lieu d'inverse. Ce bug est invisible pour DCT (butterfly auto-transposant, 95% des blocs) et ne frappe que DST (4x4 luma intra, 5% des blocs).

**L'ironie** : C'est le seul bug du projet où une transcription **fidèle** de la spec produit un résultat **faux**. Tous les autres bugs venaient de transcriptions infidèles (simplifications, omissions, inversions). Celui-ci est le contraire : on a trop bien suivi la spec.

**Après** : La règle "spec-first" reste valide pour 99% des cas, mais avec un nouveau corollaire : **quand la spec donne une matrice de transform, vérifier si c'est la forward ou l'inverse**. Si le butterfly DCT fonctionne mais la multiplication matricielle DST échoue, c'est probablement une transposition manquante.

### Pivot 4 : L'ajout de la règle "ABSOLU" dans CLAUDE.md

Le commit `6a32e79` est un tournant dans la relation humain-agent. Après avoir constaté que Claude avait tendance à "simplifier" les conditions de la spec (fusionner des if, réorganiser des boucles), une règle a été ajoutée :

> "ABSOLU : Chaque fonction de décodage DOIT être une transcription directe de la spec. Ne pas simplifier, optimiser ou interpréter."

Avec des exemples concrets de violation :
> "simplifier `if (log2TrafoSize == 2 || (log2TrafoSize == 3 && cIdx > 0))` au lieu de transcrire la vraie condition de la spec → introduit des bugs silencieux"

**Enseignement n°12 — L'agent doit être contraint sur le style de code** : Claude est naturellement enclin à "améliorer" le code en le simplifiant. Dans un décodeur vidéo, cette tendance est mortelle. La spec est la spec — même si une condition semble redondante, elle capture un cas edge que la "simplification" perd. La règle ABSOLU a significativement réduit les bugs post-implémentation.

---

## 5. Ce qui a marché

### 5.1. Le CLAUDE.md hyper-détaillé

Le CLAUDE.md fait ~200 lignes et contient :
- L'index des pages du PDF spec (quelle section → quelles pages `pdftotext`)
- Le routing par phase (quels fichiers lire avant de coder)
- Le workflow de debug oracle
- Les conventions de nommage miroir de la spec
- Les commandes de build/test
- Les jalons oracle par phase

**Résultat** : L'agent peut naviguer dans une spec de 716 pages sans se perdre. Il sait exactement quelles pages lire pour quelle section.

### 5.2. Les tables de données pré-extraites

6 fichiers dans `docs/spec/tables/` avec les données critiques :
- `cabac-arithmetic.md` : rangeTabLps[64][4], transIdx
- `cabac-init-values.md` : Tables 9-5 à 9-31 (toutes les init values I/P/B)
- `transform-matrices.md` : DCT 8x8, 16x16, 32x32
- `intra-tables.md` : intraPredAngle[35], invAngle, filtrage
- `scaling-list-defaults.md` : Matrices par défaut 4x4/8x8

**Résultat** : L'agent copie les valeurs directement au lieu de les interpréter depuis le PDF. Zéro erreur de transcription sur les tables numériques.

### 5.3. L'agent-guide.md

Un guide dédié aux agents avec :
- Erreurs typiques par phase (les 8 pièges Phase 4)
- Workflow de debug oracle
- Commandes prêtes à copier
- Données de référence CABAC
- Bitstreams jouets pour debug step-by-step

### 5.4. Le pattern "implement → audit → fix"

Chaque phase suit le même cycle :
1. **Implement** : PR principale, implémentation complète
2. **Audit** : PR de review systématique vs spec
3. **Fix** : Corrections des issues trouvées

Ce pattern a été appliqué pour les Phases 2, 3, et 4. Il formalise le fait que la première passe est "80% correcte" et que les 20% restants nécessitent une relecture délibérée.

### 5.5. Les 3 niveaux de bitstreams de test

| Niveau | Bitstreams | Usage |
|--------|-----------|-------|
| **Toy** (16x16, 1 CTU) | `toy_qp10/30/45.265` | Debug step-by-step, un seul CU |
| **Conformance** (64x64, 176x144) | `i_64x64_qp22.265`, `p/b_qcif_10f.265` | Oracle pixel-perfect par phase |
| **Real-world** (1080p, 4K) | `bbb1080_50f.265`, `bbb4k_25f.265` | Validation finale |

**Résultat** : Les 3 toy tests sont passés pixel-perfect (4E/4F validés), ce qui prouve que le pipeline prediction+transform+reconstruction fonctionne. Le bug restant est dans les couches basses (CABAC/coding tree), pas dans les couches hautes.

---

## 6. Ce qui n'a pas marché

### 6.1. Implémenter Phase 4 en un seul commit monolithique

Le commit `dfff932` ajoute +4715 lignes d'un coup. Résultat : 22 bugs. Le code était "structurellement correct" (les bonnes fonctions aux bons endroits) mais "numériquement faux" (offsets, signes, conditions).

**Leçon** : Pour du code bit-exact, les couches basses (CABAC, contextes) auraient dû être validées isolément AVANT d'implémenter les couches hautes. La subdivision en sous-phases est venue après le fait.

### 6.2. Faire confiance à la spec pour les contextes CABAC

La spec H.265 eq 9-55 dit `ctxInc = 27 + sigCtx` pour chroma. HM utilise `28 + sigCtx`. La spec est fausse (ou du moins, incohérente avec ses propres init tables). Ça a coûté environ 6 commits de debugging.

**Leçon** : Pour les contextes CABAC, HM est la source de vérité, pas la spec. Toujours cross-référencer.

### 6.3. Le debug "top-down" (pixels → bins)

Approche initiale : l'oracle dit "42 pixels diffèrent dans la frame", on regarde quel CU c'est, on debug le CU. Problème : si le bug est dans CABAC, le CU est faux parce que *tous* les CUs après le bug sont faux.

**Leçon** : Debug bottom-up (bins → syntax elements → coefficients → pixels). C'est plus lent au début mais converge plus vite.

### 6.4. Le "contexte ordering" HM vs spec

La spec ordonne les contextes d'une certaine façon dans les formules. HM les ordonne différemment dans son enum (`CONTEXT_TYPE_4x4=0, 8x8=1, NxN=2, SINGLE=3`). Les deux sont valides mais donnent des index différents. On a passé du temps à debugger un faux problème : notre code était correct pour la spec mais faux pour HM, et c'est HM qui a raison car les init values suivent l'ordre HM.

---

## 7. Statistiques

### Volume de code

| Mesure | Valeur |
|--------|--------|
| Total C++ (src/) | 6 625 lignes |
| `src/decoding/` seul | 3 493 lignes |
| Plus gros fichier | `coding_tree.cpp` (940 lignes) |
| Tests unitaires | ~200 lignes |

### Commits

| Métrique | Valeur |
|----------|--------|
| Total commits | 55 |
| `fix:` | 27 (49%) |
| `feat:` | 11 (20%) |
| `docs:` | 11 (20%) |
| `refactor:` | 2 (4%) |
| Autres (perf, chore, merge) | 4 (7%) |

**49% de fix commits** — presque la moitié du travail est de la correction. C'est représentatif d'un projet bit-exact.

### Timeline

| Jour | Commits | Quoi |
|------|---------|------|
| 19 mars | 11 | Setup, documentation, tables, oracle |
| 20 mars | 34 | Phases 1-4 + 15 bug fixes |
| 21 mars | 13 | 10 bug fixes + docs + **Phase 4 pixel-perfect** (DST fix) |
| 22 mars | 12+ | Phase 5 finalisée — 10/10 tests pixel-perfect (weighted pred, CVS ordering, interSplitFlag) |

### Pull Requests

| # | Titre | Date |
|---|-------|------|
| 1 | feat: rename to hevc-decode, add CI and real-world tests | 20/03 |
| 2 | refactor: optimize BitstreamReader for WASM | 20/03 |
| 3 | feat: add edge-case conformance bitstream generator | 20/03 |
| 4 | perf: fix 3 critical bottlenecks before Phase 2 | 20/03 |
| 5 | feat: Phase 2 — Bitstream & NAL unit parsing | 20/03 |
| 6 | fix: 4 correctness issues from Phase 2 audit | 20/03 |
| 7 | feat: Phase 3 — Parameter Sets & Slice Header parsing | 20/03 |
| 8 | fix: 6 conformance issues from Phase 3 audit | 20/03 |

8 PRs mergées en une journée, toutes suivant le workflow git strict (branch → PR → merge, jamais push sur main).

---

## 8. État actuel (22 mars 2026)

### Ce qui fonctionne

- **Phases 1-3** : complètes, auditées, mergées dans main
- **Phase 4** : **COMPLÈTE** — `oracle_i_64x64_qp22` pixel-perfect (jalon Phase 4)
  - 4A (CABAC) : 100% identique à HM bin par bin
  - 4B-4D (Parsing) : 132 residual_coding calls matchent HM exactement
  - 4E (Transform) : DST/DCT/dequant corrects (DST fix = bug n°30)
  - 4F (Prediction) : 35 modes + reconstruction = 0 erreur luma/chroma
  - 12/13 tests Phase 4 passent (`conf_i_multislice_256` = bug pré-existant)
- **Phase 5** : **COMPLÈTE** — 10/10 tests pixel-perfect (jalon Phase 5)
  - P-frames, B-frames, weighted prediction explicite, CRA multi-GOP, AMP, TMVP, hierarchical B, open GOP, CABAC init
  - 17 bugs corrigés au total (dont 3 dans la session finale)

### Prochaine étape

**Phase 6 — Loop Filters** : Deblocking + SAO.
- 0/15 tests passent actuellement (deblocking et SAO non implémentés)
- Spec §8.7.1-8.7.2 (deblocking), §8.7.3 (SAO)
- Jalon : `oracle_full_qcif_10f` = Main profile complet

---

## 9. Enseignements pour l'article

### Sur le travail avec un agent IA

1. **Le CLAUDE.md est le ROI de la productivité** : Plus il est détaillé (commandes, pages spec, pièges), plus l'agent est autonome et précis.
2. **L'agent fait du code "structurellement correct" mais "numériquement faux"** : Les fonctions sont aux bons endroits, les signatures sont bonnes, mais les valeurs littérales (offsets, signes, conditions) sont souvent approximatives.
3. **La règle ABSOLU ("transcris la spec mot-à-mot")** a été le changement le plus impactant : elle force l'agent à ne pas "interpréter" la spec.
4. **Le pattern implement → audit → fix formalise la réalité** : 80% correct au premier jet, 20% de corrections nécessaires.
5. **Les sous-phases avec validation indépendante sont essentielles** pour les systèmes à propagation d'erreur (CABAC).

### Sur HEVC spécifiquement

6. **La spec H.265 se contredit parfois** : eq 9-55 vs Table 9-29 pour les contextes chroma.
7. **HM est la vraie spec** : quand le texte et le code divergent, c'est le code qui a raison.
8. **CABAC est le boss final** : un bug de contexte corrompt silencieusement tout le reste.
9. **Debug bottom-up** : bins → syntax elements → coefficients → pixels.
10. **Les "pièges de conformité" connus à l'avance réduisent le temps de debug**, même s'ils ne les empêchent pas tous.

### Sur le projet en général

11. **49% de fix commits** : dans du code bit-exact, la moitié du travail est de la correction.
12. **Le PDF spec de 716 pages est navigable** avec un bon index de pages dans le CLAUDE.md.
13. **3 niveaux de bitstreams** (toy/conformance/real-world) permettent de debugger à la bonne granularité.
14. **Phases 1-3 en une journée, Phase 4 bloquée depuis 2 jours** : la complexité n'est pas linéaire. CABAC + intra prédiction est un ordre de grandeur plus difficile que tout le reste.
15. **Une transcription parfaite de la spec peut être fausse** : la spec DST eq 8-315 donne la matrice forward, pas l'inverse. Transcrire fidèlement `y[i] = Σ M[i][j]·x[j]` donne la forward transform. L'inverse est `Σ M[j][i]·x[j]` (la transposée). HM le fait implicitement. C'est la limite de l'approche "spec-first".
16. **Instrumenter HM est le dernier recours mais le plus efficace** : quand le parsing match, les ref samples matchent, et la prédiction match — mais le résultat final diverge — il faut ajouter des fprintf dans HM et comparer les valeurs intermédiaires étape par étape. C'est ce qui a révélé que le DST donnait des résidus différents avec les mêmes coefficients dequantisés.

### Sur Phase 5 spécifiquement

17. **Un flag PPS ignoré = un processus manquant** : `weighted_pred_flag` était parsé mais jamais utilisé. Le code utilisait toujours le default, et ça passait pour les poids triviaux. Le fix est trivial (routing + transcription spec), mais trouver le bug demande de vérifier le pipeline complet.
18. **Vérifier le pipeline de bout en bout** : quand la reconstruction est correcte mais la sortie est fausse, le bug est entre les deux (ici, l'ordering des frames de sortie). C'est un angle mort — on debug toujours le décodage, jamais l'écriture.
19. **Les conditions implicites de la spec sont les plus dangereuses** : `interSplitFlag` est une condition "inferred" mentionnée en une phrase dans §7.4.9.4. Sans elle, 99% des CUs fonctionnent. Le 1% restant corrompt le CABAC.
20. **Les agents subagents sont efficaces pour l'investigation systématique** : le bug `interSplitFlag` a été trouvé par un agent lancé en background qui a comparé les SYN traces HM pendant que l'investigation principale se concentrait sur un autre test.

---

## 10. Annexe : Liste complète des bugs Phase 4

Pour chaque bug : commit, catégorie, description, comment trouvé, temps estimé de debug.

| # | Commit | Catégorie | Description | Section spec | Méthode de détection |
|---|--------|-----------|-------------|-------------|---------------------|
| 1 | `67fde9e` | Structure | Ref sample availability: heuristic row-major au lieu de Z-scan | §8.4.4.2.2 | Oracle mismatch |
| 2 | `67fde9e` | CABAC | Safe read: lecture au-delà du RBSP | §9.3.4 | Crash |
| 3 | `65c919f` | Résidu | EGk binarisation off-by-one | §9.3.3.11 | Gros coefficients faux |
| 4 | `65c919f` | Transform | Transpose 2D passe V/H inversée | §8.6.4.2 | Oracle mismatch |
| 5 | `65c919f` | Structure | IntraSplitFlag mal calculé | §7.4.9.8 | Mauvaise profondeur TU |
| 6 | `ce851b5` | Résidu | Diagonal scan tables x/y swappés | §6.5.3 | Coefficients dans le mauvais ordre |
| 7 | `04c1bb9` | CABAC | sig_coeff_flag context derivation | §9.3.4.2.5 | Trace bin-par-bin vs HM |
| 8 | `d680208` | CABAC | ctxSet carry entre sub-blocks | §9.3.4.2.6 | Trace bin-par-bin vs HM |
| 9 | `d680208` | CABAC | sig_coeff_flag chroma offset 27→28 | §9.3.4.2.5 | Init values mismatch |
| 10 | `c5fdb77` | Intra | Transposition angular modes 2-17 | §8.4.4.2.6 | Modes horizontaux tous faux |
| 11 | `20cd158` | Structure | OOB chroma write | §8.6.4 | Crash |
| 12 | `5931e6b` | Transform | IDCT32 terme EEEO manquant | §8.6.4.2 | Blocs 32x32 faux |
| 13 | `5931e6b` | Intra | Ref sample filter condition | §8.4.4.2.3 | Oracle mismatch |
| 14 | `92f0f5e` | Intra | Strong smoothing biIntFlag | §8.4.4.2.3 | Oracle mismatch |
| 15 | `92f0f5e` | Parsing | SPS range extension parsing | §7.3.2.2.2 | Valeurs dérivées fausses |
| 16 | `484716e` | Intra | Chroma: mode luma au lieu de mode chroma dérivé | §8.4.3 | Toute chroma fausse |
| 17 | `0f995be` | Résidu | Sign data hiding parity (coefficient courant inclus) | §7.4.9.11 | Signes aléatoirement inversés |
| 18 | `9d58e03` | Intra | MPM: DC pour voisin au-dessus de la frontière CTB | §8.4.2 | MPM faux au bord |
| 19 | `51d3838` | Multi | 3 fixes conformité (prediction + TU) | §8.4/§7.3.8.7 | Audit systématique |
| 20 | `3a02717` | CABAC | transform_skip init values Table 9-29 | §9.2 | Init mismatch |
| 21 | `ac3a982` | CABAC | Chroma context mapping HM layout | §9.3.4.2.5 | Trace HM vs nôtre |
| 22 | `14367e4` | CABAC | Unification luma spec + chroma HM | §9.3.4.2.5 | Refactoring |
| 23 | `483a59a` | CABAC | Context set starts (8x8 firstSigCtx) | §9.3.4.2.5 | Trace HM |
| 24 | `fa061e4` | Résidu | 3 bugs: scan index, lastSigCoeff swap, MDCS range | §7.3.8.11 | Oracle mismatch |
| 25 | `8997b30` | Résidu | scanIdx derivation réécrit verbatim | §7.4.9.11 | Oracle mismatch |
| 26 | `069960f` | CABAC | derive_sig_coeff_flag_ctx réécrit from spec | §9.3.4.2.5 | Refactoring définitif |
| 27 | `a9f696e` | Structure | Chroma CBF héritage parent pour TU 4x4 déférée | §7.3.8.8 | Chroma CBF perdu |
| 28 | `2f0c7e3` | CABAC | CBF_CHROMA 5 contextes (pas 4) + B-slice init | §9.2, Table 9-22 | Init mismatch |
| 29 | `b8c6c1d` | Intra | 5 bugs intra prediction (spec audit §8.4.4) | §8.4.4 | Audit systématique |
| 30 | `7d8c9aa` | **Transform** | **DST-VII inverse : matrice forward M au lieu de M^T** | §8.6.4.2 | Instrumentation HM (dequant identique, résidu différent) |

**Bug n°30 — Le dernier et le plus subtil** : 2023 pixels luma faux, tous dans des blocs 4x4 intra utilisant DST. Le parsing CABAC, le dequant, la prédiction, les reference samples — tout était correct. Seul le DST inverse donnait un résultat différent de HM avec les mêmes coefficients dequantisés en entrée. Root cause : la spec eq 8-315 donne la matrice DST forward et la formule `y[i] = Σⱼ M[i][j]·x[j]` qui est le forward transform. L'inverse correct est M^T·x. HM le fait implicitement via `c[row] * M[row][column]`. Fix : 4 lignes dans `idst4()` — transposer les coefficients. Progression : 5608 → 4239 → 2674 → 2023 → **0 (pixel-perfect)**.

---

*Dernière mise à jour : 22 mars 2026*
