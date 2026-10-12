<!--
SPDX-FileCopyrightText: 2026 mauriciobc
SPDX-License-Identifier: LGPL-2.1-or-later
-->

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

adwaita-overlay is a SCSS stylesheet (compiled with `sassc`) loaded *above* libadwaita's own sheet: per-user `~/.config/gtk-4.0/gtk.css` at GTK_STYLE_PROVIDER_PRIORITY_USER (800) over libadwaita's THEME (200). It adds bevels, grain and depth without patching libadwaita. A theme directory does not reach libadwaita apps (AdwStyleManager ignores `gtk-theme`), so the per-user sheet is the only path; `meson.build` and `tools/install` document why.

Read before non-trivial changes: `docs/proposal.md` (design), `BACKLOG.md` (tasks, stable IDs, acceptance criteria — never renumber; strike through with a reason instead of deleting), `docs/decisions.md` (running decision record with measurements; cited throughout code comments as E1, M2, H3, etc.). Record new measured verdicts in `decisions.md`.

## Commands

```
sassc src/overlay.scss build/gtk.css   # compile only, touches nothing else
tools/build                            # compile + symlink ~/.config/gtk-4.0/gtk.css + restart GTK daemons
tools/build --no-restart               # same, no daemon kill
tools/build --debug                    # outlines every node (build/gtk-debug.css); run plain tools/build to revert
tools/check-selectors                  # forward contract vs installed libadwaita + reverse axis vs build/gtk.css
tools/check-selectors --reverse        # reverse axis only (needs build/gtk.css)
tools/check-selectors 1:1.9.4-1        # forward contract vs an archived libadwaita (network)
tools/fetch-upstream                   # upstream/cache/<pinned>/gtk.css from the Arch archive
meson setup _build && meson compile -C _build && meson test -C _build   # the only test: reverse-contract
reuse lint                             # must pass (CI runs it); every new file needs an SPDX header
```

`tools/build` refuses to run as root and refuses to replace a real (non-symlink) `~/.config/gtk-4.0/gtk.css`. GTK reads user CSS only at process start (the accent key is live; colour-scheme and sheet edits are not), so running apps need a restart.

### Offscreen verification (C probes, built against gtk4)

Pixel-level verdicts are preferred over eyeballing. Build each once into `build/`:

```
gcc -O1 -o build/render-gallery tools/render-gallery.c $(pkg-config --cflags --libs gtk4 libadwaita-1)
build/render-gallery none out/stock && build/render-gallery build/gtk.css out/overlay [family]
tools/gallery-diff out/stock out/overlay        # per-family changed_px / mean / max delta
```

Same pattern for `render-widget.c`, `probe-motion.c`, `probe-accent.c`, `probe-foreign.c` (gtk4 only). Knobs: `SCHEME=dark`, `CONTRAST=more`, `STATE=prelight|active|checked|focus-visible|drop` (gallery), `REDUCE=1` / `NOANIM=1` (motion). When the surface derives from an upstream variable, pass `upstream/cache/1:1.9.4-1/gtk.css` as the upstream sheet, or declarations are invalid at computed-value time and nothing measures. `tools/track <family> [-i]` opens the gtk4-demo / widget-factory page for a family (optionally under GTK Inspector). Full details in `docs/HACKING.md`.

## Architecture: layers and contracts

`src/overlay.scss` imports in layers (order is readability; GTK cascades by provider priority):

- **L0 `src/_tokens.scss`** — every `--ov-*` custom property, on `:root`, paints nothing. Naming `--ov-<category>-<role>`. **Every upstream libadwaita variable is read in exactly one `--ov-up-*` alias here, each with a fallback** to the private `@define-color ov_stock_*` palette (GTK 4's built-in Default values, not libadwaita's). Nothing else may reference upstream variables. Reason: in GTK4 apps without libadwaita (Chromium/Helium) and under a theme directory, an undefined `var()` paints *nothing* (black windows). Colours derive from the accent via oklab relative-colour syntax so the system accent drives everything without a rebuild.
- **L1 `src/_primitives.scss`** — mixins only, emits nothing by itself (bevel, drop, flat, depth, inset/well, gel, texture, `ov-motion()`). Each primitive emits its own `@media (prefers-contrast: more)` revert inline (there is no separate L3 block). `ov-motion()` restates upstream's focus-ring transition trio because `transition` replaces the list.
- **L2 `src/surfaces/_*.scss`** — the only layer that names upstream selectors. Generic families (cells, notebook, …) are imported after the narrower ones.
- **`src/_user.scss`** — personal overrides, imported last, empty by default. **`src/_debug.scss`** emits only when `$ov-debug` is set (via `src/overlay-debug.scss`).

GTK constraints that shape the code (decisions.md M2): `var()` substitutes single value tokens only, never whole multi-token segments like a box-shadow entry — so geometry lives in L1 mixins and L0 owns colours/dimensions/motion. Overriding `box-shadow` or `background-image` replaces upstream's value, so surface rules restate upstream's stops via `$upstream` parameters (safe because the version is pinned). No `!important`: at priority 800 a rule that doesn't win is aimed at the wrong node — use the Inspector or `--debug` build.

**Contracts** (`upstream/`, pinned to libadwaita `upstream/pinned-version`):
- `selectors.txt` — hand-written selector atoms *upstream uses* for anything L2 overrides (not necessarily atoms the overlay writes; GTK-core nodes like `window`/`dialog` deliberately absent). Update in the same commit as the L2 rule.
- `variables.txt` — every upstream variable L0 reads. Update in the same commit as `_tokens.scss`.
- `check-selectors` forward axis: does upstream still provide what's registered. Reverse axis (against the built sheet): every non-`--ov-` `var()` read must be registered, and every declared `--ov-*` must be read somewhere. Exits 1 on any miss.
- `upstream/gtk3/`, `upstream/libhandy/` belong to the GTK3 front (`tools/build-gtk3`, Python): a recompile of upstream GTK3 Adwaita + libhandy with the accent substituted. It is a dev tool, not part of 1.0 packages.

## Packaging

`meson install` installs the theme directory (`<datadir>/themes/Adwaita-overlay/`), `adwaita-overlay-install` (from `tools/install`, which appends one marked `@import` line to the user's `gtk.css` and copies the sheet as `adwaita-overlay.css`), and `check-selectors` + `_lib.sh` + contracts under `<libexecdir>/adwaita-overlay/`. `-Dpacman_hook=true` installs a hook that runs the installed checker after libadwaita upgrades (reports, never blocks). Scripts must work in both layouts (source tree and installed). Arch packaging is in `packaging/arch/`; see `docs/PACKAGING.md`. CI (`.github/workflows/ci.yml`): reuse lint, meson build/test/dist on Ubuntu, makepkg on Arch (forward contract vs current libadwaita, weekly).

## Commits

One imperative first line, often prefixed by area (`Buttons: convex face, neon hover, 0.5px rim`), then a body that says why.
