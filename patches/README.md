# patches

The chimera patch series over extern/FBNeo, applied by waterbox/apply-patches.sh
(meson.build runs it).

- 0001: a game reading its controls is marked (CHIMERA_INPUT_READ), which is
  how the core counts lag frames. Everything else this core needs of a
  frontend is in waterbox/.
