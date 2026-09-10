The original `result.json` records `aligned_word_bus: false` because the
launcher stored the `--word-bus` build-override flag in that field. CP36g
uses the already updated production board, whose archived source explicitly
sets `.ALIGNED_WORD_READS(1)`. The aligned-word interface **is enabled**.

The raw result, source archive and their hashes are preserved. The launcher
now reports `word_bus_build_override` separately and derives
`aligned_word_bus` from the actual board source. This metadata-only correction
does not change the CP36g RTL, timing or resource result.
