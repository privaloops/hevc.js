# §7.3.8.1 — Slice Segment Data

Spec ref : §7.3.8.1 (general slice segment data syntax)

## Vue d'ensemble

`slice_segment_data()` est la boucle principale qui itere sur les CTU d'un slice. C'est le coeur du decodeur.

## Pseudo-code (spec)

```
slice_segment_data() {
    do {
        CtbAddrInTs = CtbAddrRsToTs[CtbAddrInRs]

        coding_tree_unit()

        end_of_slice_segment_flag                ae(v)  // decode_terminate()

        CtbAddrInTs++
        CtbAddrInRs = CtbAddrTsToRs[CtbAddrInTs]
    } while (!end_of_slice_segment_flag)
}
```

## Points critiques

### `end_of_slice_segment_flag`

- Decode via `decode_terminate()` (CABAC terminate mode, §9.3.4.5), **PAS** via `decode_decision()`
- `decode_terminate()` utilise un sous-range fixe de 2 :
  - `ivlCurrRange -= 2`
  - Si `ivlOffset >= ivlCurrRange` → flag = 1 (fin du slice), puis lire `alignment bits`
  - Sinon → flag = 0, renormalize
- Parse apres **chaque** CTU sans exception
- Un oubli de ce flag cause un overrun sur le bitstream ou un decrochage CABAC

### Scan order (tiles)

- `CtbAddrInRs` : adresse raster scan (gauche→droite, haut→bas dans la picture)
- `CtbAddrInTs` : adresse tile scan (gauche→droite, haut→bas **dans chaque tile**)
- Les tables de conversion `CtbAddrRsToTs[]` et `CtbAddrTsToRs[]` sont derivees du layout des tiles (PPS)
- Sans tiles, les deux ordres sont identiques

### Reinitialisation CABAC aux frontieres de tiles

Si `tiles_enabled_flag == 1` et qu'on entre dans un nouveau tile (le CTU courant est le premier d'un tile) :
- Reinitialiser tous les contextes CABAC
- Reinitialiser le decodeur arithmetique (lire les bits d'init)
- Sauf si `dependent_slice_segment_flag == 1` pour ce slice

### WPP (Wavefront Parallel Processing)

Si `entropy_coding_sync_enabled_flag == 1` :
- Au debut de chaque ligne de CTU (sauf la premiere) :
  - Restaurer les contextes CABAC sauvegardes a la fin du 2eme CTU de la ligne precedente
  - Reinitialiser le decodeur arithmetique
- A la fin du 2eme CTU de chaque ligne :
  - Sauvegarder l'etat de tous les contextes CABAC

### `end_of_sub_stream_one_bit`

Si le slice contient des entry points (tiles ou WPP), `end_of_sub_stream_one_bit` est lu a la fin de chaque sous-stream (sauf le dernier) au lieu de `end_of_slice_segment_flag` :
- `end_of_sub_stream_one_bit` doit valoir 1
- Suivi de `byte_alignment()` pour aligner le reader avant le prochain sous-stream

## Implementation C++

```cpp
void Decoder::decode_slice_segment_data(SliceHeader& sh) {
    int ctb_addr_rs = sh.slice_segment_address;

    do {
        int ctb_addr_ts = ctb_addr_rs_to_ts[ctb_addr_rs];

        // Reinit CABAC si nouveau tile (sauf dependent slice)
        if (is_first_ctb_in_tile(ctb_addr_ts) && ctb_addr_rs != sh.slice_segment_address) {
            cabac.init_contexts(sh);
            cabac.init_decoder(bitstream);
        }

        // WPP : restaurer les contextes si debut de ligne CTU
        if (entropy_coding_sync_enabled && is_first_ctb_in_row(ctb_addr_rs)) {
            if (has_saved_wpp_contexts) {
                cabac.restore_contexts(saved_wpp_contexts);
                cabac.init_decoder(bitstream);
            }
        }

        decode_coding_tree_unit(ctb_addr_rs);

        // WPP : sauvegarder les contextes apres le 2eme CTU de la ligne
        if (entropy_coding_sync_enabled && is_second_ctb_in_row(ctb_addr_rs)) {
            cabac.save_contexts(saved_wpp_contexts);
            has_saved_wpp_contexts = true;
        }

        // Detecter fin de sous-stream (tiles/WPP) ou fin de slice
        if (is_end_of_sub_stream(ctb_addr_ts, sh)) {
            // end_of_sub_stream_one_bit (doit valoir 1)
            assert(cabac.decode_terminate() == 1);
            bitstream.byte_alignment();
            end_of_slice_segment_flag = false;
        } else {
            end_of_slice_segment_flag = cabac.decode_terminate();
        }

        ctb_addr_ts++;
        ctb_addr_rs = ctb_addr_ts_to_rs[ctb_addr_ts];

    } while (!end_of_slice_segment_flag);
}
```

## Checklist

- [ ] Boucle do-while avec `end_of_slice_segment_flag` via `decode_terminate()`
- [ ] `decode_terminate()` implemente correctement (range -= 2, test offset)
- [ ] Scan order tile-scan (CtbAddrInTs / CtbAddrInRs conversion)
- [ ] Reinit CABAC aux frontieres de tiles
- [ ] WPP : sauvegarde/restauration des contextes aux lignes CTU
- [ ] `end_of_sub_stream_one_bit` + `byte_alignment()` aux frontieres de sous-streams
- [ ] Tests : verifier le nombre de CTU decodes par slice
- [ ] Tests : bitstream multi-tile, verifier la reinit CABAC
