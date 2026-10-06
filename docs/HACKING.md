<!--
SPDX-FileCopyrightText: 2026 mauriciobc
SPDX-License-Identifier: LGPL-2.1-or-later
-->
# Hacking on adwaita-overlay

Read [proposal.md](proposal.md) first (the design document, with the accepted
review amendments), then [BACKLOG.md](../BACKLOG.md) (the working backlog) and
[decisions.md](decisions.md) (the running decision record).

## Layout

```
src/            L0 tokens, L1 primitives, L2 surfaces, _user.scss last
                (SCSS, built by sassc)
meson.build     the conventional build (meson install -> theme directory,
                installer, checker, optional pacman hook; `meson test`)
assets/         empty: the grain is an inline SVG data: URI in src/_tokens.scss
                (ov-grain()); 9-slice descoped —
                border-image does not follow border-radius (decisions.md E1)
upstream/       pinned-version, selector + variable contracts, cache/ (gitignored);
                gtk3/ and libhandy/pinned-version for the GTK3 dev tool
tools/          fetch-upstream, check-selectors, install, build, render-widget.c,
                probe-motion.c, probe-accent.c, probe-foreign.c,
                render-gallery.c, gallery-diff, track, gtk3-accent-sites,
                build-gtk3
share/gnome-shell/  the empty gnome-shell front
index.theme     theme metadata for the installed directory
hooks/          pacman hook template (installed with -Dpacman_hook=true)
packaging/      distro packaging (arch/)
LICENSES/       license texts (REUSE)
docs/           proposal, decisions, HACKING, PACKAGING, evening-0 experiments,
                screenshots/
build/          sassc output (gitignored)
```

## Toolchain

All present: sassc, glib2 (gresource), bsdtar, git, gcc (for render-widget),
python3 (gtk3-accent-sites).

## Commands

```
tools/fetch-upstream             # fill upstream/cache/<pinned>/gtk.css from the Arch archive
tools/fetch-upstream 1:1.6.5-1   # any archived version (handles the pre-1.9 four-file layout)
tools/check-selectors            # libadwaita contract vs the *installed* sheet, AND the
                                 # reverse axis vs build/gtk.css (what the pacman hook runs)
tools/check-selectors --reverse  # the reverse axis alone
tools/check-selectors --sheet P  # reverse axis against the built sheet at P (any mode)
tools/check-selectors 1:1.9.4-1  # libadwaita contract vs an archived version (network)
tools/fetch-upstream --gtk3      # GTK3 Adwaita SCSS for the pinned tag, from GNOME GitLab
tools/fetch-upstream --libhandy  # libhandy's theme SCSS for its pinned tag
tools/check-selectors --gtk3     # gtk3 only (explicit; not in the default run); also
                                 # reports SHEET DRIFT (gtk3, libhandy) vs the pins
tools/gtk3-accent-sites          # TSV of every GTK3 declaration that depends on the
                                 # accent (BACKLOG G, decisions.md "GTK3 accent")
tools/build-gtk3                 # GTK3 theme Adwaita-overlay: upstream Adwaita + libhandy
                                 # recompiled with the GNOME accent (--accent to override)
tools/build-gtk3 --activate      # ...and set gtk-theme to it; --deactivate goes back to Adwaita
tools/build                      # sassc + symlink ~/.config/gtk-4.0/gtk.css + restart daemons
tools/build --debug              # the same, plus a 1px outline on every node
                                 # (build/gtk-debug.css) — see "Aiming a rule"
```

## Aiming a rule

An L2 rule that is "not winning" is nearly always a rule aimed at the wrong
node — which is why the Inspector exists. Two ways to see the boxes: GTK
Inspector (`GTK_DEBUG=interactive <app>`), and the debug build, which outlines
every node at once. That second one is the pen pass's only inspection aid
(codepen `xxyEYMJ`, its `--debug` token):

```
tools/build --debug --no-restart                              # installs the outlined sheet
build/render-gallery build/gtk-debug.css /tmp/boxes controls  # ...or offscreen
tools/build --no-restart                                      # back to the shipped sheet
```

It is a build and not a runtime token on purpose: `outline` on `*` at priority
800 outranks every upstream focus ring, so at a hypothetical `--debug: 0` the
declaration would still be there and would replace the focus ring with a 0px
one. `src/_debug.scss` emits nothing unless `$ov-debug` is set, so the shipped
sheet cannot grow the rule by accident.

Offscreen render of one widget with one CSS file (pixel-level verdicts,
X5 test card):

```
gcc -O1 -o build/render-widget tools/render-widget.c $(pkg-config --cflags --libs gtk4)
build/render-widget <css-file> <out.tiff> <button|headerbar> <width> <height>
```

Offscreen motion and state verdicts — walks a real button and a real
`.boxed-list` row through hover, press and focus, and a headerbar through
backdrop, sampling at rest / mid-transition / settled:

```
gcc -O1 -o build/probe-motion tools/probe-motion.c $(pkg-config --cflags --libs gtk4)
build/probe-motion build/gtk.css                                   # house motion
build/probe-motion build/gtk.css 80 upstream/cache/1:1.9.4-1/gtk.css  # + upstream vars
REDUCE=1 build/probe-motion build/gtk.css ...                       # prefers-reduced-motion
NOANIM=1 build/probe-motion build/gtk.css ...                       # gtk-enable-animations=false
SCHEME=dark build/probe-motion build/gtk.css ...                    # dark scheme
```

Pass the upstream sheet whenever the surface under test derives from an
upstream variable (`--headerbar-bg-color` and friends): without it the
declaration is invalid at computed-value time and nothing measures.

## Accent

The register in `src/_tokens.scss` is a pure function of upstream's accent:
`--ov-up-accent` aliases it once and every lit edge and neon rung derives
from that through relative colour syntax. So the system accent drives the
material with no rebuild — and in oklab rather than HSL, because HSL
lightness spread the same rung across 0.132 of *perceived* lightness over the
nine accents where an absolute oklab target spreads it over none
(`decisions.md` H1).

`tools/probe-accent` measures that rather than assuming it. It re-points one
property on `:root` from a provider above the sheet and reports the largest
single-pixel move between two renderings of the same node — a mean is the
wrong statistic, because the lit edge is a 1px hairline. It probes a
*checked* switch, because the lit rungs paint only in a state
(`switch:checked > slider`, `scale:hover`).

```
gcc -O1 -o build/probe-accent tools/probe-accent.c $(pkg-config --cflags --libs gtk4)
build/probe-accent build/gtk.css upstream/cache/1:1.9.4-1/gtk.css   # the real sheet
SCHEME=dark build/probe-accent build/gtk.css upstream/cache/1:1.9.4-1/gtk.css
build/probe-accent none upstream/cache/1:1.9.4-1/gtk.css            # stock: must exit 1
```

Current verdict: 2/2 register links LIVE in both schemes, control STALE,
exit 0. The stock control exits 1, so the probe can fail.

It has three ways to report failure, not one: a **sanity pair** that must go
LIVE (it proves the override reached the node — without it a broken
instrument reports success), a **control** that must read STALE, and
**UNSTABLE** when the node never settles, which refuses to give a verdict
rather than measuring an animation.

Scope, measured and narrower than it sounds (`decisions.md` H3): the accent
follows libadwaita's accent key live, but **colour-scheme does not** and
**editing `gtk.css` does not reload** — which is why `tools/build` ends with
a daemon restart.

## Tracking widget coverage

Two halves. The eyeball half is the demo apps; the pixel half is the
gallery. Both are launchers/renderers — neither writes into the repo.

```
tools/track                        # family -> demo page -> surface file
tools/track <family>               # open that page (gtk4-widget-factory or
                                   # gtk4-demo --run=<example>)
tools/track <family> -i            # ...under GTK Inspector

build/render-gallery none       out/stock    # every family, stock Adwaita
build/render-gallery build/gtk.css out/overlay
tools/gallery-diff out/stock out/overlay      # per-family pixel delta
```

Build the gallery once:

```
gcc -O1 -o build/render-gallery tools/render-gallery.c \
    $(pkg-config --cflags --libs gtk4 libadwaita-1)
```

`render-gallery` renders 15 widget families offscreen (same mechanism as
`render-widget`: a CssProvider at priority 800, `GtkWidgetPaintable` →
`GskCairoRenderer` → TIFF), one TIFF per family, and takes the same
`CONTRAST=more` / `SCHEME=dark` knobs. `gallery-diff` reports
`changed_px  mean_delta  max_delta` per family, so a material change is
visible as a number before it is judged by eye. `tools/track` names which
family lives in which surface file, which is what makes coverage
auditable rather than remembered.

`STATE=<prelight|active|checked|focus-visible|drop>` renders that state on
every widget in the family at once — a stress shot, not a per-widget
state. The hover glow, the accent drop ring and the press well are
invisible at rest by construction, so this is the only way to review the
interaction register per family. It is how the bar-button glow and the
`button.link` leak were judged:

```
STATE=prelight build/render-gallery build/gtk.css out/hover buttons
STATE=prelight build/render-gallery none          out/hover-stock buttons
tools/gallery-diff out/hover-stock out/hover
```

## Motion

Motion lives in L0 (`--ov-motion-*`: curve, enter/exit/press/switch/ring)
and every declaration emits through `ov-motion()` in L1 — which also
restates upstream's focus-ring motion, because a `transition` declaration
replaces the list rather than extending it. `prefers-reduced-motion:
reduce` collapses every duration to `0ms` (same state language, no
animation); `gtk-enable-animations=false` already does that globally in
GTK itself. Rationale and measurements: `docs/decisions.md`, "Motion
review — 23 Sep 2026".

## Foreign apps (no libadwaita)

GTK loads this sheet in **every** GTK4 process, including apps that never
call `adw_init()` — Chromium and its forks (Helium), anything GTK4 without
libadwaita. libadwaita's custom properties do not exist there, and a
declaration whose only value is an undefined `var()` computes to *nothing*:
GTK paints no background at all rather than falling back to the theme's own
colour. Chromium builds its whole Linux palette out of rendered GTK nodes
(`ui/gtk/gtk_color_mixers.cc` over `gtk_util.cc`'s `GetBgColor`) and forces
the frame colour opaque, so "paints nothing" became a completely black
browser window (decisions.md, "Foreign apps: Helium came up black",
25 Sep 2026).

The rule that keeps that from happening: every libadwaita variable the sheet
reads is read in exactly one L0 alias (`--ov-up-*`), each carrying a fallback;
surfaces and primitives reference the aliases and nothing else.

The first version of those fallbacks was a GTK built-in **named** colour
(`@theme_bg_color`), which is wrong for a *second* reason found while
packaging: a theme directory replaces GTK's built-in palette, so the named
colours do not exist there either and the sheet painted nothing. The
fallbacks are now a private palette (`@define-color ov_stock_*`) matching GTK
4's own built-in `Default` theme — verified 0 px changed across all 15
families against the old adaptive version. `decisions.md` H6.

`probe-foreign` reimplements Chromium's colour mixer against a bare GTK4 app
and prints the inputs it reads plus the derived frame/toolbar colours. It
exits 1 when any painted input comes out fully transparent — the exact
condition Chromium renders as black:

```
gcc -O1 -o build/probe-foreign tools/probe-foreign.c \
    $(pkg-config --cflags --libs gtk4)
build/probe-foreign                    # what this machine reads now
build/probe-foreign none               # stock GTK control
build/probe-foreign build/gtk.css      # ...or any sheet, in isolation
SCHEME=dark build/probe-foreign build/gtk.css
CONTRAST=more build/probe-foreign build/gtk.css
```

## Theme directory vs per-user sheet

A theme directory does not reach libadwaita (see `meson.build` for the
reasons); the per-user sheet installed by `tools/install` does. User-facing
install steps are in the top-level `README.md`, packagers' in
`docs/PACKAGING.md`.

### What is NOT claimed about the theme directory

An earlier draft of this section quoted a `button.suggested-action` Δ51
between the two paths. **That number was withdrawn.** It came from a probe
with no libadwaita, where the overlay's CTA rules restate an upstream
`background-image` that does not exist in that environment — so neither path
was rendering the overlay's CTA at all. Appending a maximally specific
`button.suggested-action` rule to the sheet changed neither path's pixels,
which is what showed the probe was measuring GTK's image layer rather than
our cascade. There is no trustworthy number for this yet, and no upstream
statement about what a third-party theme should do under `GTK_THEME`, so
there is no acceptance criterion to chase toward either.

What *is* verified: identical bytes in both paths load with **zero GTK parser
errors**, and both paint — same reading on every widget sampled, including the
one unpainted region (the progressbar trough), which agrees in both. See
BACKLOG X9 for the open question and for what a trustworthy probe would need.

## The contracts

- `upstream/selectors.txt` — every selector atom L2 depends on upstream
  having. Hand-written on purpose: adding a line is a deliberate act of
  taking on a dependency.
- `upstream/variables.txt` — every upstream variable L0 reads, all of them
  through the `--ov-up-*` aliases. Guards the load-bearing layer; a renamed
  upstream variable is caught here, not by the selector contract.

Both files are checked in one direction only — *does upstream still provide
what we registered?* — so `check-selectors --reverse` adds the other
direction against the **built** sheet: every upstream property it reads must
be in `variables.txt`, and every `--ov-*` it declares must be read somewhere.
That is what makes `src/_user.scss` safe rather than a hole. It is skipped,
loudly, when `build/gtk.css` is absent, because the pacman hook runs as root
and must never build into the user's tree. The **selector** axis does not
invert — `upstream/selectors.txt` deliberately registers the atoms *upstream*
uses, not the ones the overlay writes (`.content-pane`, `window`, `dialog`
are absent on purpose); see that file's header and `decisions.md` H2.

The GTK3 pair works the same way but guards a **different** thing: the GTK3
front is a recompile of GTK3's own Adwaita with the accent substituted, so it
has no hand-written rules to list. `upstream/gtk3/variables.txt` is the whole
36-name `@define-color` palette it resolves, and `upstream/gtk3/selectors.txt`
is the 434 atoms that carry an accent declaration — the surface
`tools/build-gtk3` actually modifies. The failure it catches is a widget that
silently keeps `#3584e4` baked in. See `decisions.md` H7. The GTK3 front is a
dev tool and is not part of the 1.0 packages.

`tools/check-selectors` exits 1 on any miss. The Arch package installs a
pacman hook (`-Dpacman_hook=true`) that runs the *installed* checker against
the *installed* sheet after every libadwaita upgrade — no network, nothing
written into the repo, safe as root. The checker works in two layouts: from
the source tree (`tools/`, contracts in `upstream/`) and installed
(`<libexecdir>/adwaita-overlay/`, contracts in `contracts/`; no GTK3, no
archive fetch).

## Your own CSS

`tools/build` owns `~/.config/gtk-4.0/gtk.css` by symlink, so there is
nowhere else for a personal override to live. `src/_user.scss` is that place:
imported last, so it has the last word, and empty by default (it emits
nothing, like `src/_debug.scss`).

```
$EDITOR src/_user.scss && tools/build
git update-index --skip-worktree src/_user.scss   # keep it out of git status
```

What you write there is checked like everything else: `check-selectors
--reverse` reads the built sheet, so an unregistered upstream variable you
introduce fails the check and names the line to add. That is the point of
routing personal tweaks through here rather than around the build. Two
things it does *not* check: unregistered upstream **selectors**, and
`!important` — both are judgement calls, and the file's header says why.
