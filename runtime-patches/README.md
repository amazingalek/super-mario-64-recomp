# runtime-patches

This game's own changes to the submodules, applied by `CMakeLists.txt` on top of N64Recomp-browser's patches, in order
of name, when it configures. A new change is a new file per submodule, `<submodule>-<n>-<what>.patch`, numbered after
that submodule's highest, with a line here.

```
RecompFrontend-0001    built-in texture packs: packs the game makes itself (Hi-res Font, src/game/hires_font.cpp),
                       loaded below every pack the player enables
```
