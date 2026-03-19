# 8.7.2 — Deblocking Filter

Spec ref : 8.7.2.1 (general), 8.7.2.2 (boundary derivation), 8.7.2.3 (boundary filtering strength), 8.7.2.4 (filtering process for luma), 8.7.2.5 (filtering process for chroma)

## Vue d'ensemble

Le deblocking filter lisse les aretes de blocs pour reduire les artefacts visuels. Il opere sur les frontieres horizontales et verticales des CU/TU/PU.

### Ordre de traitement

```
1. Filtrer toutes les frontieres VERTICALES (pour chaque CTU, gauche -> droite, haut -> bas)
2. Filtrer toutes les frontieres HORIZONTALES (meme ordre)

Important : les verticales sont filtrees AVANT les horizontales.
```

## 8.7.2.2 — Boundary Derivation

Les frontieres a filtrer sont aux positions alignees sur 8 pixels (luma) :

```cpp
// Pour chaque edge verticale a position xB :
// - xB doit etre multiple de 8 (luma) ou 4 (si conditions)
// - L'edge existe si deux blocs adjacents ont des frontieres de CU/TU/PU

// Pas de filtrage si :
// - L'edge est sur le bord de la picture
// - L'edge est sur un bord de tile avec loop_filter_across_tiles_enabled_flag = 0
// - L'edge est sur un bord de slice avec loop_filter_across_slices_enabled_flag = 0
```

## 8.7.2.3 — Boundary Filtering Strength (Bs)

```cpp
int derive_boundary_strength(const CU& p, const CU& q) {
    // p = bloc a gauche/au-dessus, q = bloc a droite/en-dessous

    if (p.is_intra || q.is_intra)
        return 2;

    // Si l'un des blocs a des coefficients residuels non-nuls
    if (p.has_nonzero_coeffs || q.has_nonzero_coeffs)
        return 1;

    // Comparer les motion vectors et ref pics
    if (p.refPic != q.refPic)
        return 1;

    if (abs(p.mv.x - q.mv.x) >= 4 || abs(p.mv.y - q.mv.y) >= 4)
        return 1;  // MV differe de >= 1 pixel (MV est en 1/4 pel)

    return 0;  // Pas de filtrage
}
```

## 8.7.2.4 — Luma Filtering

### Decision process

```cpp
// Pour chaque edge de 4 samples :
// Lire les samples de part et d'autre : p0,p1,p2,p3 | q0,q1,q2,q3

// beta et tC dependent du QP
int Q = (QpP + QpQ + 1) >> 1;  // QP moyen
int beta = beta_table[Clip3(0, 51, Q + slice_beta_offset)];
int tc = tc_table[Clip3(0, 53, Q + 2*(bS-1) + slice_tc_offset)];

// Condition de filtrage :
// |p0-q0| < beta, |p3-p0| + |q3-q0| < beta >> 3, |p0-q0| < (5*tC+1) >> 1
bool filter = (dp + dq < beta);
// dp = |p2-p0|, dq = |q2-q0| pour les 2 lignes de l'edge
```

### Strong vs Weak filter

```cpp
// Strong filter condition :
// 2*(dp+dq) < beta/4 && |p0-q0| < (5*tC+1) >> 1 && |p3-p0| + |q3-q0| < beta >> 3
bool strong = (2*(dp+dq) < (beta >> 2)) && (abs(p0-q0) < ((5*tc+1)>>1));

if (strong) {
    // Strong filter : modifie p0,p1,p2 et q0,q1,q2
    p0' = Clip3(p0-2*tc, p0+2*tc, (p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3);
    p1' = Clip3(p1-2*tc, p1+2*tc, (p2 + p1 + p0 + q0 + 2) >> 2);
    p2' = Clip3(p2-2*tc, p2+2*tc, (2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3);
    // Symetrique pour q0', q1', q2'
} else {
    // Weak filter : modifie p0,p1 et q0,q1
    int delta = (9*(q0-p0) - 3*(q1-p1) + 8) >> 4;
    delta = Clip3(-tc, tc, delta);
    p0' = Clip1Y(p0 + delta);
    q0' = Clip1Y(q0 - delta);

    // Optionnel : modifier p1, q1
    int deltaP = Clip3(-tc>>1, tc>>1, ((p2+p0+1)>>1) - p1 + delta/2);
    // ...
}
```

## 8.7.2.5 — Chroma Filtering

Plus simple que luma : pas de strong/weak distinction.

```cpp
// Filtrage chroma seulement si Bs == 2
if (bS == 2) {
    int delta = Clip3(-tc, tc, ((q0-p0)*4 + p1-q1 + 4) >> 3);
    p0' = Clip1C(p0 + delta);
    q0' = Clip1C(q0 - delta);
}
```

## Tables beta et tC

```cpp
// Table 8-16 (simplifie)
const uint8_t beta_table[52] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 20,
    22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
    42, 44, 46, 48, 50, 52, 54, 56, 58, 60,
    62, 64
};

const uint8_t tc_table[54] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 5, 5,
    6, 6, 7, 8, 9, 10, 11, 13, 14, 16,
    18, 20, 22, 24
};
```

## Pieges connus

1. **Ordre V puis H** : Filtrer les verticales d'abord sur toute la picture, puis les horizontales
2. **Alignement 8 pixels** : Les edges ne sont filtrees que si alignees sur des multiples de 8
3. **Bords de picture** : Pas de filtrage sur les bords de l'image
4. **Dependance inter-CTU** : Le deblocking d'un CTU peut necessiter les samples du CTU voisin

## Checklist

- [ ] Boundary derivation (quelles edges filtrer)
- [ ] Boundary strength calculation (Bs = 0, 1, 2)
- [ ] beta et tC lookup avec QP + offsets
- [ ] Luma strong filter
- [ ] Luma weak filter
- [ ] Chroma filter
- [ ] Ordre correct : V puis H
- [ ] Gestion des bords de picture/tile/slice
- [ ] Tests : pixel-perfect vs libde265 (les filtres sont souvent la source de mismatch)
