# The mixed game in the launcher

For docs/xdl-format/ultimate.md, "As built" (after the hwvx --mix / --mix-build paragraph).

**Building.** `hwvx --mix-build RECIPE` makes a first pass over every archive's table of contents to count the packs it will load. Before each pack it prints `@convert DONE TOTAL ARCHIVE`, the same progress lines `--convert-xdl` prints, and on failure it prints `@error TEXT`. It writes into `<xdl>/mix-NAME.part`, which it empties first. When the build is complete it writes `built.cfg` there:

```
stamp=XXXXXXXX
xdl=VERSION.COMPILER.SETTINGS
packs=N
failed=N
```

`stamp` is Port_MixStamp: the recipe text, the release folders, and the sizes and times of their HW_share files. The builder then renames the old `mix-NAME` folder to `.old`, renames `.part` to `mix-NAME`, deletes `.old` and prints `@convert N N done`. If a build is cancelled or fails, the last complete build is left as it was. Playing with `--mix` still fills `mix-NAME` as packs load, but it never writes `built.cfg`.

**The launcher, Convert page.** The "Mixed game (ultimate)" row shows one of four states:
- **not built**: there is no `built.cfg`, and playing mixes each pack the first time it loads;
- **built**: N packs, N MB;
- **out of date**: the stamp or the cache version in `built.cfg` differs from what Port_MixProbe and Xdl_Stamp give now;
- **needs two or more of the GC/PC/PS2 copies**: fewer of the recipe's releases are imported (a PC or PS2 disc image doesn't count).

The buttons are Build / Build again and Delete. Build runs the program as a child process with `--mix-build ultimate`, using the same progress bar and Cancel as Convert. When a build is cancelled, the launcher removes the `.part` folder. Port_MixProbe reads the recipe and works out its stamp without starting the build.

**The launcher, Play page.** When two or more releases are imported, a "Mixed game (ultimate)" choice appears below the releases. It is saved in sources.cfg as `play=mix:ultimate`. An older launcher that reads this line sees no release chosen. If the mixed game can't be played, the launcher falls back to the first imported release. With this choice, Play (or skip=1, or HWVX_NO_DIALOG) starts the game as if run with `--mix ultimate` (Port_LauncherMix, main_sdl.c).
