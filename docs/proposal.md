<!--
SPDX-FileCopyrightText: 2026 mauriciobc
SPDX-License-Identifier: LGPL-2.1-or-later
-->
# Adwaita Visual Overhaul — Setup and Guidelines

*Proposal of 18 Sep 2026, @Mauricio Barbosa e Castro. Local, versioned copy.
Corrections accepted after the 18 Sep review are recorded in
[Amendments](#amendments-accepted-18-sep-2026) — where an amendment and the
body disagree, the amendment wins. The premise underneath the body changed on
6 Oct 2026 and is recorded in
[Amendments accepted 6 Oct 2026](#amendments-accepted-6-oct-2026), which wins
over both the body and the 18 Sep list.*

This project adds bevelled borders, layered surface texture and depth to
libadwaita on EndeavourOS/GNOME as an additive CSS overlay loaded at
`GTK_STYLE_PROVIDER_PRIORITY_USER` (800) over libadwaita's own sheet at
`PRIORITY_THEME` (200). No fork, no upstream contribution, no redistribution.

## Scope and constraints

The overlay changes how libadwaita surfaces look. It changes nothing about
how they behave, lay out or size.

In scope: borders (9-slice bevels), surface texture and layering, depth via
shadow and filter, and the colour derivations that drive them.

Out of scope: widget internals, layout, metrics, padding, adaptive
breakpoints, window controls, and anything requiring a patched
libadwaita-1.so.

| Constraint | Consequence |
| --- | --- |
| Personal use only, never redistributed | LGPL-2.1 imposes no obligations. No theme format, no compatibility layer, no support burden. |
| No upstream contribution | libadwaita does not accept AI-assisted contributions. Nothing from this tree is ever proposed upstream, and no issue or MR is filed from it. |
| Compatibility with upstream only | Never patch, fork or replace libadwaita. Only add CSS above it. |
| One machine, one maintainer | Undocumented selectors are fair targets. Version pinning is acceptable. The cost of a break is one evening, not a user base. |
| High contrast must survive | `prefers-contrast: more` is a system preference apps cannot disable. Every material effect reverts to flat under it. |

## Why an overlay rather than a fork

Measured across 1.5.4 → 1.11.alpha: selectors survive each release at
99–100%, while the stylesheet's file layout was rewritten twice (four files
→ two → one). A fork binds to the unstable axis; an overlay binds to the
stable one. The overlay also inherits accent, dark mode and contrast
derivation for free, because libadwaita's CSS variables still resolve
underneath it.

## Repository layout and toolchain

One git repo, built with sassc into a single `gtk.css`, symlinked into
place. Nothing is generated at runtime.

```
adwaita-overlay/
├── src/
│   ├── overlay.scss              # entry point, imports everything below
│   ├── _tokens.scss              # L0: --ov-* variables derived from upstream vars
│   ├── _primitives.scss          # L1: bevel / texture / depth mixins
│   └── surfaces/                 # L2: one file per node family
│       ├── _headerbar.scss
│       ├── _toolbar.scss
│       ├── _boxed-list.scss
│       ├── _button.scss
│       ├── _entry.scss
│       └── _popover.scss
├── assets/                       # SVG 9-slice sources and texture tiles
├── upstream/
│   ├── selectors.txt             # the selector contract (see Upstream tracking)
│   ├── pinned-version            # libadwaita version this tree targets
│   └── cache/                    # extracted gtk.css per version, gitignored
├── tools/
│   ├── fetch-upstream            # pull a version from archive.archlinux.org
│   ├── check-selectors           # diff contract against a fetched version
│   └── build                     # sassc + install + restart daemons
├── hooks/
│   └── adwaita-overlay.hook      # pacman PostTransaction hook
└── README.md                     # points at this doc
```

| Package | Why |
| --- | --- |
| sassc | Build the overlay. Same compiler upstream and Arch use, so output matches. |
| glib2 (gresource) | Extract gtk.css from a libadwaita package. |
| zstd / bsdtar | Unpack .pkg.tar.zst from the Arch archive. |
| git | History. Every visual change should be bisectable. |

## Install target

The built file goes to `~/.config/gtk-4.0/gtk.css`. Symlink it to the repo's
`build/gtk.css` rather than copying, so a rebuild takes effect without a
second install step.

## Development loop

GTK Inspector is the iteration tool, not rebuild-and-restart.
`GTK_DEBUG=interactive <app>` opens it; its CSS editor applies changes live
with no restart, and its node tree shows the real element names and classes
so you can find targets without reading the compiled CSS.

The libadwaita page in Inspector toggles dark and high contrast without
touching system settings, so the full test matrix sits in one window.
`ADW_DEBUG_ADAPTIVE_PREVIEW=1` covers narrow widths.

When you do rebuild, remember libadwaita reads user CSS at process startup
only. `--gapplication-service` daemons such as Nautilus keep painting the
old look until killed, so `tools/build` ends with a restart step.
(Verified 6 Oct 2026, decisions.md H3: the startup-only part is right for
the *file* — libadwaita exposes no monitor on `gtk.css`, only on its accent
GSettings key. So the accent is live and colour-scheme is not, which is the
asymmetry worth knowing: watch the accent change with an app open and it
follows; edit `gtk.css` and it does not.)

## Stylesheet architecture

Four layers. L2 is the only layer that names upstream selectors, and
therefore the only layer the upgrade guard has to watch on the selector
axis (see Amendments: the variable axis is guarded too).

```
U  libadwaita gtk.css — priority 200
   └─ L0 tokens — --ov-* derived vars, :root only
      └─ L1 primitives — bevel / texture / depth mixins, no output
         └─ L2 surfaces — one file per node family
            └─ L3 contrast — reverts, emitted inline by the L1 mixins
```

**L0 — tokens.** Defines every `--ov-*` variable on `:root`, each derived
from an upstream variable using `color-mix()` or relative colour syntax.
This is the load-bearing layer: because the values derive rather than
hardcode, accent changes, dark mode and contrast continue to drive the
material system with no extra work. L0 declares no selectors other than
`:root` and paints nothing.

**L1 — primitives.** SCSS mixins only, no output of their own. Three
families: `bevel()` (mechanism decided by the Evening 0 experiments),
`texture()` (layered `background-image` composited with
`background-blend-mode`), `depth()` (`box-shadow` ladders; `filter` banned
in v1). Every mixin emits its own `prefers-contrast: more` revert inline —
with one documented exception, `ov-flat-register()`, which guards its
declarations inside `(prefers-contrast: no-preference)` instead: upstream's
`button.flat` owns `box-shadow` in its states (a ring on hover / active /
checked), so a `none` revert would erase them (decisions.md, "Flat register").

**L2 — surfaces.** The only layer that names upstream selectors. One file
per node family, each calling L1 mixins with L0 tokens. No raw colour
values, no raw geometry.

**L3 — contrast.** Not a separate file: the reverts are emitted by the L1
mixins themselves, so no material rule can drift from its revert.

## Variable naming

`--ov-<category>-<role>`, always three parts, always lowercase.

| Prefix | Holds | Example |
| --- | --- | --- |
| `--ov-bevel-*` | Edge highlight and shadow colours, bevel width | `--ov-bevel-highlight` |
| `--ov-texture-*` | Tile URL, opacity, blend mode, scale | `--ov-texture-opacity` |
| `--ov-depth-*` | Shadow offsets, blur radii, ladder stops | `--ov-depth-raised` |
| `--ov-surface-*` | Per-surface tints layered over upstream backgrounds | `--ov-surface-tint` |
| `--ov-flat-*` | The `.flat` variation's rest material: edge pair + sheen | `--ov-flat-edge-top` |

Documented exceptions to the scheme: the per-family kill switches
`--ov-bevel` / `--ov-texture` / `--ov-depth`. Setting one to `none`
disables exactly that family of effects — which is how you bisect a
frame-time regression without losing the other two families.

## Conventions and rules

Hard rules:

1. Never hardcode a colour outside L0. Every colour in L2 resolves through
   an `--ov-*` token, which resolves through an upstream variable. The
   hardcoded hex values in upstream's sheet are the corners that will not
   follow the palette — a list to audit, not a pattern to copy.
2. Every L2 selector is registered in `upstream/selectors.txt`, and every
   upstream variable L0 derives from is in `upstream/variables.txt`. An
   unregistered dependency is invisible to the upgrade guard and will break
   silently.
3. Every material effect has a contrast revert, emitted by its own mixin.
   No exceptions. A bevelled low-contrast edge is an accessibility
   regression, not a style.
4. No `!important`. You are at priority 800 against upstream's 200. If a
   rule is not winning, the selector is wrong, and `!important` will hide
   that from you until the next upgrade.
5. Assets are SVG or `data:` URIs by default. Raster only where genuinely
   necessary, and then always paired through `-gtk-scaled()`. Fractional
   scaling at 1.25× and 1.5× is the failure case to test, not 2×.
6. One visual change per commit. The message names the node family and the
   effect. This is what makes a regression bisectable six months from now.

Soft conventions:

- Prefer `color-mix()` and relative colour syntax over new tokens. A
  derived value keeps tracking accent and dark mode; a new token is another
  thing to maintain.
- Comment every L2 rule with the upstream selector's source file, e.g.
  `// widgets/_header-bar.scss`. When the guard reports a selector moved,
  this is how you find what replaced it.
- Test in both colour schemes and both contrast modes before committing,
  using the Inspector toggles rather than system settings.
- Keep `upstream/cache/` gitignored. It is reproducible from the archive
  and adds megabytes per version.

## What not to reach for

| Wanted | Status |
| --- | --- |
| GLSL shaders on surfaces | Unavailable. GskGLShader deprecated in GTK 4.16; the Vulkan renderer from 4.14 does not support it. Not reachable from CSS at any priority. |
| `backdrop-filter` | Not a GTK CSS property. Behind-window blur is compositor territory. |
| `mask-image`, `clip-path` | Not supported. |
| Pseudo-elements (`::before`, `::after`) | No generated content in GTK CSS. Use extra background layers instead. |

## Upstream tracking

The overlay depends on upstream selectors and variables continuing to
exist. The guard turns a silent break into a line in your pacman output.

**The contracts.** `upstream/selectors.txt` lists every selector atom L2
targets, one per line, sorted; `upstream/variables.txt` lists every
upstream variable L0 derives from. Both are written by hand as surfaces are
added, not generated from the overlay, so that adding a dependency is a
deliberate act.

**The check.** With no argument, `tools/check-selectors` reads the
*installed* `/usr/lib/libadwaita-1.so.0` — that is what the pacman hook
runs, so the check always matches what just landed on disk and needs no
network. With a version argument it checks the archived copy (fetching it
first if needed). Exit non-zero on any miss, either axis.

**The hook.** `hooks/adwaita-overlay.hook`:

```ini
[Trigger]
Operation = Upgrade
Operation = Install
Type = Package
Target = libadwaita

[Action]
Description = Checking Adwaita overlay selector contract
When = PostTransaction
Exec = /home/<user>/adwaita-overlay/tools/check-selectors
```

PostTransaction hooks cannot abort a transaction — `AbortOnFail` is a
no-op there, and setting it makes pacman warn on every run — so it is left
unset. The guard exits non-zero and pacman prints the report; a missing
selector or variable is cosmetic and must never block a system upgrade.

## Expected volume

Selector survival between consecutive libadwaita releases, measured across
six versions:

| Transition | Old selectors surviving |
| --- | --- |
| 1.5.4 → 1.6.5 | 84% |
| 1.6.5 → 1.7.6 | 99% |
| 1.7.6 → 1.8.4 | 100% |
| 1.8.4 → 1.9.2 | 100% |
| 1.9.2 → 1.11.alpha | 99% |

The 1.6 outlier was the migration from `@define-color` to CSS custom
properties, a one-off. Since then the surface only grows. Expect zero to
five hits per six-month cycle, each a few minutes of work.

## Upgrade runbook

1. Hook reports missing selectors or variables after a `pacman -Syu`.
2. Fetch the new version's gtk.css (`tools/fetch-upstream <version>`) and
   grep for the widget's node name to find what replaced the selector.
3. Cross-check against the SCSS source for that release if the compiled
   form is unclear. A git checkout at the tag gives you
   `widgets/_<widget>.scss` directly.
4. Update the L2 rule and the contract file in the same commit.
5. Bump `upstream/pinned-version`.
6. Rebuild, restart daemons, verify in both colour schemes and both
   contrast modes.

## Milestones, risks and open questions

| # | Milestone | Done when |
| --- | --- | --- |
| 0 | Evening 0 experiments | The four mechanism tests in docs/evening-0.css have recorded verdicts; bevel() shape decided. |
| 1 | Pipeline | A no-op L0 build installs, loads, and survives a rebuild-and-restart cycle. |
| 2 | Primitives | `bevel()` and `texture()` render correctly on a single test widget. |
| 3 | Header bar and toolbar | The largest visual mass carries the new material. This is the go/no-go on the whole aesthetic. |
| 4 | Lists and cards | `list.boxed-list`, `.boxed-list-separate`, `.card`. The density test for performance. |
| 5 | Controls | `button`, `entry`, `popover`, `splitbutton` variants. |
| 6 | Contrast layer | Every effect reverts flat under `prefers-contrast: more`. |
| 7 | Daily driving | Two weeks of normal use before adding anything further. |

Milestone 3 is the real decision point. The header bar cluster is the
biggest single surface, and one evening there will tell you whether the
direction is what you pictured. Stop if it is not; everything after that
only multiplies the commitment.

| Risk | Mitigation |
| --- | --- |
| Blur and filter cost on the Intel iGPU | `filter` banned in v1; per-family kill switches from day one; measure frame times at milestone 4, where node counts are highest |
| Raster textures breaking at 1.25× and 1.5× fractional scaling | SVG by default; `-gtk-scaled()` pairs for anything raster; test both scales before committing a texture |
| Hardcoded hex values upstream that will not follow the palette | Audit them once at milestone 5 and decide case by case: override or accept |
| Texture re-coupling the look to fixed pixels, undoing the recolourability Adwaita was rebuilt for | Keep texture as an overlay layer over upstream backgrounds, never a replacement; use `-gtk-recolor()` where a tile needs to track the palette |
| Forgetting the daemon restart and debugging a change that did apply | Restart step baked into `tools/build`, not run by hand |

## Amendments (accepted 18 Sep 2026)

From the review of the original proposal; the scaffold implements these.

1. **The guard reads the installed sheet, not the archive.** A
   PostTransaction hook that downloads from archive.archlinux.org is
   network-fragile and races the archive's lag behind the repos;
   `check-selectors` (no argument) extracts from `/usr/lib` instead —
   zero network, guaranteed match, safe as root (mktemp only, never writes
   into the repo). The archive path remains available as an explicit
   argument for runbook work.
2. **A second contract axis: variables.** L0 derives from upstream
   variable *names*; a rename there silently breaks every token and the
   selector contract would never notice. `upstream/variables.txt` closes
   that hole.
3. **Per-family kill switches**, not one global `--ov-effects`. CSS has no
   conditionals; one variable cannot null out `border-image-source`,
   `background-image`, `box-shadow` and `filter` at once. For bisecting a
   frame-time regression you want depth off while bevels survive — depth
   is the suspected cost.
4. **L3 reverts are emitted inline by the L1 mixins.** A separate contrast
   file names the same selectors a second time and will drift;
   `_contrast.scss` is removed from the tree.
5. **Evening 0 precedes milestone 1** (`docs/evening-0.css`). The
   border-image × border-radius question is a feasibility gate, not a taste
   choice: web CSS does not clip border-image to border-radius, and
   Adwaita's identity is rounded surfaces. If GTK matches, 9-slice dies and
   `bevel()` is inset shadows / clipped gradients.
6. **Flatpak is a guarded hole, not an invisible one.** Flatpak GNOME apps
   run the `org.gnome.Platform` runtime's own libadwaita, which the pacman
   hook cannot see. The overlay is exposed to them deliberately (setup
   step 8), so the contract must eventually be checked against the oldest
   runtime actually in use (BACKLOG.md X1).
7. **`filter` is banned in v1.** Upstream's entire `filter` usage is
   `opacity()` and `drop-shadow()` — `blur()` is unproven and is the iGPU
   risk. Box-shadow ladders carry all depth.
8. **Numbers in this doc predate the pin.** The measured counts (lines,
   variable count, hardcoded-hex count, usage tallies) were taken before
   pinning 1:1.9.4-1; regenerate them from the pinned version (BACKLOG.md
   X3) before quoting them again.
9. **Pinned at 1:1.9.4-1**, the version currently offered by the repos and
   installed here — not the 1.9.2 of the original examples.
10. **The existing `~/.config/gtk-4.0/gtk.css`** (window/pane translucency
    tweaks) migrates into `src/surfaces/` as part of milestone 1;
    `tools/build` refuses to replace a real file until then.
11. **Flatpak is out of scope for now** (user decision, 18 Sep 2026). This
    supersedes amendment 6 until Flatpak GNOME apps enter daily use: the
    `filesystem=xdg-config/gtk-4.0` override that had been granted was
    reverted, and the backlog's Flatpak tasks (P1.4, X1) are struck. When
    Flatpak returns to scope, re-apply the override and reinstate both.

## Amendments accepted 6 Oct 2026

The proposal's premise was **"personal use only, never redistributed"**, and
two rows of the constraint table leaned on it: *no support burden* and *the
cost of a break is one evening, not a user base*. Both are now false, and the
tree has become a distributable artifact. What did **not** change is the
architecture, and the distinction matters more now than it did then.

### Superseded: the personal-use premise

| was | now |
| --- | --- |
| "Personal use only, never redistributed" (header, constraint table) | Installable by anyone — `tools/install` |
| "No support burden" | There is a user-facing install path, so there is a support surface |
| "The cost of a break is one evening, not a user base" | There is a user base, which is why `tools/check-selectors` grew a **reverse** axis and why the accent register was measured rather than assumed |
| "Never redistributed, nothing proposed upstream" (README) | Redistributable. Still nothing proposed upstream, and now for a second reason: see below |

The rationale behind those rows is gone, so the rows themselves are struck
rather than rewritten. Concretely, the rows that said undocumented selectors
are acceptable because "the cost of a break is one evening" no longer hold:
`upstream/selectors.txt` is now the difference between a reported line and a
silently broken user's desktop.

### Retained, and now load-bearing: not a fork

**libadwaita is still never patched, replaced or rebuilt.** That was the
proposal's central bet and it has only strengthened: selector survival
measured at 99–100% per release (84% once, on the `@define-color` →
custom-properties migration) while the *file layout* was rewritten twice.
A fork binds to the unstable axis; the overlay binds to the stable one.

What is new is that the bet is now **checked** rather than argued. Three
things turned "we believe an overlay is safer" into a measurement:

- `tools/check-selectors` — forward axis: does upstream still provide
  everything the contracts register? Reverse axis: does the built sheet
  depend on anything unregistered? (decisions.md H2)
- `tools/probe-accent` — the accent register is measured live, 2/2 links
  LIVE in both schemes, with a negative control that must read STALE and a
  stock control that must exit 1. (decisions.md H3)
- `tools/probe-foreign` — the reason the `--ov-up-*` fallback layer exists at
  all: a Chromium window rendered `#ff000000` because an undefined `var()`
  paints nothing and Chromium averages that into an opaque black frame.

So the "not a fork" claim is no longer a statement of intent. It is a
property the tooling fails the build over.

### Also settled since the body was written

- **The three fronts are now understood, and libadwaita's is the product.**
  GTK4/libadwaita installs through a `@import` sidecar in
  `~/.config/gtk-4.0/` because libadwaita ignores `gtk-theme` outright
  (`AdwStyleManager` pins `Adwaita-empty`). 220 of the sheet's 242 built
  selectors — 91% — are GTK4-only, so it cannot be retargeted at GTK3.
  The GTK3 front is a real theme; the gnome-shell front does not exist and
  would be a separate project (GNOME 50's shell links no GTK at all).
- **The accent register moved from relative HSL to oklab.** HSL lightness
  spread one rung across 0.132 of perceived lightness over the nine system
  accents; an absolute oklab target spreads it across none, because upstream
  already pins the sources into L 0.499–0.509. Blue-anchored, so the accent
  that was tuned by eye does not move. (decisions.md H1)
- **Open, and deliberately not decided here:** licensing. The tree has no
  LICENSE and the body assumed redistribution never happened, so no grant
  was ever needed or chosen. That is a decision for the maintainer, not a
  consequence of the code, and it is not addressed above.
- **Still open:** `upstream/gtk3/selectors.txt` and `variables.txt` do not
  exist (G6), and `diff_contract` returns 0 on a missing contract file, so
  the `contract OK against gtk3` line has been printing a vacuous pass. That
  is a live bug in the guard, independent of everything above.
