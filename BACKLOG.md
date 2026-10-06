<!--
SPDX-FileCopyrightText: 2026 mauriciobc
SPDX-License-Identifier: LGPL-2.1-or-later
-->
# BACKLOG — adwaita-overlay

How to read this file:

- One checkbox = one task. A task is **done when its acceptance criterion
  passes**, not when code exists.
- IDs are stable. Never renumber; drop a task by striking it through with a
  reason, never by deleting it.
- Priorities: **P0** blocks the current milestone, **P1** is
  current-milestone work, **P2** is hardening or deferred (pull a P2
  forward when it starts blocking).
- Milestone numbers follow docs/proposal.md, including the new Milestone 0
  (Evening 0).

---

## M0 — Evening 0: mechanism experiments

*Gate for everything downstream. Run the four tests in
[docs/evening-0.css](docs/evening-0.css) in GTK Inspector
(`GTK_DEBUG=interactive <app>`), one at a time. Record every verdict in
`docs/decisions.md`.*

- [x] **E1** (P0) border-image × border-radius. Paste T1 onto a window with
  regular rounded buttons; inspect the corners.
  *Accept:* verdict recorded — does the bevel clip to the rounded fill or
  draw square corners? If square (expected), 9-slice is descoped,
  `bevel()` becomes inset shadows / clipped gradient layers, and the
  assets/ SVG 9-slice pipeline is struck from the plan.
- [x] **E2** (P0) Inset bevel crispness. T2 on default and pill buttons.
  *Accept:* crisp at 1× ✓ (pixel profile in decisions.md); the 1.25×/1.5×
  cells are covered by X5's test card (display runs 1×) — not blocking.
- [ ] **E3** (P0) Pressed inversion. T3.
  *Accept:* press reads as physical depression; zero layout shift
  (transform only); works on default and pill variants.
- [ ] **E4** (P0) Grain tile. T4 on a headerbar, light and dark.
  *Accept:* renders via librsvg; no shimmer while scrolling a list under
  the headerbar; opacity verdict (0.03 / 0.05 / 0.08); provisional
  keep-or-drop call.
- [x] **E5** (P0) `docs/decisions.md` written: one section per experiment,
  the `bevel()` mixin shape decided, texture provisional call, and the
  elevation ladder sketch (4 rungs: flat / raised / overlay / window).

## M1 — Pipeline

*Done when: a no-op build installs, loads, and survives a
rebuild-and-restart cycle.*

- [x] **P1.0** (P0) Install sassc: `sudo pacman -S --needed sassc`
  (everything else in the toolchain is already present).
- [x] **P1.1** (P0) No-op build: `tools/build` compiles the comment-only
  skeleton, symlinks `~/.config/gtk-4.0/gtk.css`, restarts daemons.
  *Accept:* rebuild + restart leaves every app visually unchanged; two
  consecutive builds produce byte-identical output.
- [x] **P1.2** (P0) sassc round-trip: add one `color-mix()` and one
  relative-colour derivation to `_tokens.scss`.
  *Accept:* both survive compilation verbatim in `build/gtk.css` — libsass
  must pass modern colour syntax through, not mangle it. If it mangles,
  switch to Dart Sass and record why in decisions.md.
- [x] **P1.3** (P0) Load test: temporary `headerbar { background: red; }`
  in the overlay.
  *Accept:* visible in a native GNOME app; then removed.
- ~~**P1.4** (P1) Flatpak access: `flatpak override --user --filesystem=xdg-config/gtk-4.0`.
  *Accept:* a Flatpak GNOME app shows the P1.3 red rule.~~
  *(Deferred 18 Sep 2026 — Flatpak out of scope for now; the override that
  had been applied was reverted. Reinstate when Flatpak returns to scope.)*
- [x] **P1.5** (P0) Migrate the existing `~/.config/gtk-4.0/gtk.css`
  (window / content-pane / sidebar-pane translucency, 10 lines) into
  `src/surfaces/`, raw `rgb()` values replaced by upstream-variable
  derivations, selectors registered in the contract.
  *Accept:* old file replaced by the build symlink; same visual effect; no
  raw colours outside L0.
- [x] **P1.6** (P1) Install the pacman hook
  (installed by the package when built with `-Dpacman_hook=true`), then dry-run
  the hook path: `sudo tools/check-selectors`.
  *Accept:* exit 0; `git status` clean afterwards — proof the guard never
  writes into the repo as root.
- [x] **P1.7** (P1) Contract self-test: append one bogus selector and one
  bogus variable to the contracts, run `tools/check-selectors`, revert.
  *Accept:* exit 1, both misses reported by name.
- [x] **P1.8** (P2) Confirm `upstream/pinned-version` equals
  `pacman -Q libadwaita` (it did at scaffold time: 1:1.9.4-1).

## M2 — Design system: L0 + L1

*Done when: the primitives render correctly on a single test widget.*

- [x] **D1** (P0) Elevation ladder: rungs flat / raised / overlay /
  window. *Amended by M2 findings:* stop *geometry* lives in L1 functions
  (`ov-depth-raised()` etc. — var() does not substitute whole multi-stop
  segments), stop *colours* derive in L0 from `var(--dark-5)`.
  *Accept:* no raw colours outside L0 ✓ (grep); light/dark verification
  pending — Inspector session, with E3/E4.
- [x] **D2** (P0) `bevel()` mixin, mechanism per E1 verdict (inset pair).
  Surfaces restate upstream's stops via `ov-elevate($upstream)`; the
  contrast revert restores them.
  *Accept:* priority-800 win with no `!important` ✓ (harness, provider at
  800); aesthetic pass pending — Inspector.
- [x] **D3** (P0) `depth()` — box-shadow ladders only, `filter` banned in
  v1. *Accept:* raised ↔ flat A/B moved to the Inspector session (harness
  geometry unreliable — decisions.md M2); mechanism + kill switch verified
  by construction and by the texture-mode analogue.
- [x] **D4** (P1) `texture()` mixin — grain tile `data:` URI, opacity
  baked into the tile at 0.05 (the budget ceiling; data: URIs cannot read
  CSS variables). *Mechanism verified:* renders (stddev 0.618),
  `--ov-texture-image: none` → flat, HC → flat. Headerbar application
  lands with M3.
- [x] **D5** (P0, amended) Per-family kill switches, one edit each —
  mechanism corrected by the var() constraint (decisions.md M2):
  `--ov-bevel-width: 0px` (length component ✓ verified),
  `--ov-depth-color: transparent` (shared stop colour; pending Inspector
  A/B), `--ov-texture-image: none` (✓ verified flat).
- [x] **D6** (P0) Contrast reverts emitted inline by every L1 mixin.
  *Accept:* grep audit ✓ (every `ov-elevate`/`ov-texture` emits the
  `prefers-contrast: more` sibling); harness HC knob flattens the test
  widget ✓ (texture: stddev 0.618 → 0.000).
- [x] **D7** (P1) Naming audit: `--ov-<category>-<role>`, three parts,
  lowercase; kill-switch tokens are the documented exceptions.
- [ ] **D8** (P2) Perf baseline: `gtk4-demo` animated page with each family
  on/off. *Accept:* no visible frame drops; formal measurement deferred to
  M4 where node counts are highest.

## M3 — Headerbar & toolbar — GO/NO-GO

*Done when: the largest visual mass carries the new material and the
verdict is recorded. Stop the project if it is a no-go.*

- [ ] **H1** (P0) Headerbar cluster, incl. `.flat`, `.boxed`, window
  controls area. Every selector registered in `upstream/selectors.txt` as
  it lands; every rule commented with the upstream source file.
- [ ] **H2** (P0) Window surface: headerbar↔content hairline separator
  (alpha, never solid); window rung of the ladder.
- [ ] **H3** (P1) Toolbar / searchbar / actionbar.
- [ ] **H4** (P0) Side-by-side verification: Files + Epiphany;
  light/dark × HC/normal × 1×/1.25×/1.5×.
  *Accept:* no regressions in any matrix cell. *(The Flatpak matrix cell is
  deferred with Flatpak — proposal amendment 11.)*
- [ ] **H5** (P0) GO/NO-GO recorded in `docs/decisions.md`. If no-go:
  revert the surfaces, keep the repo, the guard and the pipeline.
- [x] **H6** (P1) Headerbar/bar backdrop state feedback: upstream's
  `headerbar:backdrop { background-color: var(--headerbar-backdrop-color);
  transition: background-color 200ms ease-out; }` never showed, because the
  overlay's `background:` shorthand set the colour in every state (found
  in the 23 Sep motion review — window controls still dim via
  `windowhandle`, the surface did not).
  *Accept:* decision recorded — either restate the backdrop colour in the
  overlay's bar rule (surface dims again, with upstream's fade) or accept
  and strike this with the reason.
  **Restored** via `--ov-bar-base` / `--ov-bar-backdrop-base` (the colour
  moves inside the gradient, since the stops are opaque); dark-mode
  recession measured 232 → 228 and confirmed live; light mode is below the
  noise floor. Kept 23 Sep 2026.

## M4 — Lists & cards — density/perf gate

- [ ] **C1** (P0) `list.boxed-list`, `.boxed-list-separate`, `.card`;
  hairline row separators. No shadows on rows — shadows belong to elevation
  transitions only.
- [ ] **C2** (P0) Performance measurement on the Intel iGPU: Files grid +
  list view, heavy scrolling, each family toggled.
  *Accept:* no perceptible jank; `--ov-depth: none` documented in README
  as the escape hatch.

## M5 — Controls

- [ ] **U1** (P0) Button matrix: default / flat / pill / suggested /
  destructive / osd / opaque; states hover, `:active` (inverted bevel per
  E3), `:checked`, `:disabled`, `:focus-visible`. Ladders on default
  variants only — flat stays flat.
  **23 Sep 2026:** the 23 Sep review (BACKLOG "Button review") closed the
  material questions for default / suggested / destructive and the focus
  ring; what it did not reach is `osd` / `opaque` / `:disabled` on a
  plain-GTK sheet (B5) and a live checked CTA, so this stays open.
  **25 Sep 2026 — the flat rung reopened (user): "the flat variation is ALL
  FLAT".** What the sheet does today: `.flat` is touched by *nothing* —
  `button:not(.flat)` keeps it out of the material and `:not(.flat)` keeps
  it out of the reset — while the families upstream paints flat *without*
  the class are hard-zeroed. So the run scoped to the variation itself
  (`button.flat` + the three `.flat`-parent bridges, which promotion moves
  out of `$ov-flat-structural`; the implicit families stay where the 23 Sep
  verdict left them) and produced three candidates for one axis — what
  carries the register's presence at rest: **A contour** (0.5px
  `currentColor` ring), **B sheen** (lit top → shade foot, no edge),
  **C bevel-lite** (the house bevel pair at reduced amplitude + a whisper
  sheen). Evidence: gallery `buttons` / `headerbar` / `adw` × light/dark/HC/
  prelight, twice each pass — the renderer is *not* run-to-run
  deterministic (up to 19k px of text antialiasing differ between two
  renders of one sheet), so every diff is noise-filtered against that
  floor. Peak deltas vs baseline, light/dark: **A 7/7, B 9/16, C 12/16**;
  the opaque rest bevel measures **+1/−10 (light), +17/−3 (dark)** on the
  same page, so B/C land *at* the house's own rest amplitude and A at about
  half (a closed contour reads heavier at the same delta). **Light bump,
  same day (user: "almost invisible in light"):** the physics constants are
  not scheme-symmetric — a white highlight moves a near-white page by +1
  (measured on the opaque button's own top hairline), so in the light scheme
  only the *shade* half of a candidate can act, while dark is the mirror of
  that. B and C therefore gained per-scheme values, calibrated to equal
  measured presence across candidates and schemes: **B and C both 17 (light)
  / 16 (dark)**, dark blocks left byte-identical to the values above.
  Peak at equal amplitude, distribution differs, and that *is* the choice:
  over the button's lower third B spreads 11.8/255 of shade against C's
  4.1/255, i.e. C puts its presence into the 0.5px rule at the edge (a
  surface with a bottom rule, exactly how the opaque buttons themselves read
  in light per B6) and B spreads it into a foot ramp (a soft lift, which in
  light reads as a shadow at the foot). Blast radius:
  only `.flat` buttons — the matched node set from a solid-red probe equals
  the noise-filtered diff, and 12 of 15 families are pixel-clean; HC is
  clean in all 15 for all three, both schemes; the hover wash is intact
  (button mean
  −13.9 baseline vs −13.4…−13.9 candidates). **Landed 25 Sep 2026 — verdict
  C (user), with B kept as a valid variation.** Promoted as L1
  `ov-flat-register()` + four L0 tokens (`--ov-flat-edge-top/-bottom`,
  `--ov-flat-hilite`, `--ov-flat-shade`); the three `.flat`-parent bridges
  moved out of `$ov-flat-structural` into the variation. B is preserved as a
  token variation of the same rule (edges → `transparent`, higher shade), not
  as dead code. The register is the sheet's one primitive that guards itself
  with `(prefers-contrast: no-preference)` instead of replaying a
  `prefers-contrast: more` revert, because upstream's `button.flat` owns
  box-shadow in its own states — reasons and the measured cells in
  decisions.md, "Flat register". Verified after promotion: 12 of 15 families
  pixel-identical, HC 0 px in rest *and* hover × both schemes,
  `probe-motion` unchanged, contract OK. Harness (A + the renders):
  `/tmp/ov-flat-variants`, disposable. Still open: register **scope** (the
  implicit flat families keep the 23 Sep reset; carve-outs needed for
  `button.link` and `windowcontrols` if it ever widens), the flat rung's
  press/held **states** (still upstream's), and the disabled cell
  (`:not(:disabled)` is structural, not measured — no disabled `.flat`
  button in the gallery).
- [ ] **U2** (P1) Entry + search + password-reveal.
- [ ] **U3** (P1) Popover + menu — overlay rung of the ladder.
- [x] **U4** (P2) Splitbutton / dropdown / combobox. (splitbutton inherits
  button surface and well; dropdown button/popover covered by existing
  surfaces — documented restraint).
- [ ] **U5** (P1) Hardcoded-hex audit at the pinned version: extract every
  hex outside `:root`, classify palette-following vs structural (shadow
  `rgb()` is fine), decide override or accept per case.
  *Accept:* the classified list recorded in `docs/decisions.md`.
- [x] **U6** (P2) Inclusion decision: scrollbar, switch, slider,
  AdwTabBar / viewswitcher — identity surfaces: switch ported, scrollbar
  trough deepened, scale slider + progress channels added; tabbar/viewswitcher
  deferred per U6 decision.
- [x] **U7** (P2) Disabled switch knob: the overlay's resting
  `switch > slider` rule outranks upstream's
  `switch > slider:disabled { box-shadow: 0 2px 4px transparent }`, so a
  disabled switch keeps a fully raised knob (found in the 23 Sep motion
  review; state legibility, not motion).
  *Accept:* the disabled knob reads recessed/flat while the track dims.
  **Done 23 Sep 2026** — disabled is flat: no drop, no insets, no gradient;
  the track keeps upstream's own `filter` dim. Evidence in decisions.md,
  "Widget-family sweep".

## M6 — Contrast & accessibility

- [ ] **A1** (P0) Full HC pass over every surface via the Inspector
  contrast toggle.
  *Accept:* every material effect flat; text contrast untouched.
- [ ] **A2** (P0) Focus visibility audit on bevelled surfaces.
  *Accept:* focus rings clearly visible over bevel + texture in both
  schemes.
  **23 Sep 2026:** the ring is now 1.5px by user decision (BACKLOG B2,
  decisions.md "Button review"), and it renders over the bevel as measured
  there — identical geometry to stock apart from the width. The live eye
  over bevel + texture is still owed, so this stays open.

## M7 — Daily driving

- [ ] **DD1** (P0) Two weeks of normal use before adding anything further.
  Annoyances are appended to the section below.
- [ ] **DD2** (P1) After two weeks: final texture keep/drop; only then
  consider `filter`, and only for a surface that demonstrably needs blur.

---

## Widget-family sweep — 23 Sep 2026

*"Apply the recorded design language to all GTK4 widgets." Method, evidence
and rejected candidates: decisions.md, "Widget-family sweep". Tracking:
`tools/track` opens the demo page per family; `tools/render-gallery` +
`tools/gallery-diff` give per-family pixel evidence.*

- [x] **W1** (P0) Material scope: the button material leaked onto every
  family upstream paints flat without the class (bar icon buttons, window
  controls, table/calendar headers, spinbutton arrows, thumbnails, model
  buttons, pathbar crumbs, bottom-sheet actions, `.flat` parents'
  children). *Accept:* stock look restored on those families, material
  unchanged on opaque buttons.
  **Scope verdict kept** (user, 23 Sep 2026): offered the glow back on bar
  icon buttons, answered "keep it as it is". Also caught `button.link`,
  which was rendering as a glow chip. Lever if it ever reopens: delete
  `$ov-flat-bar-contexts` from the loop in `_button.scss`.
- [x] **W2** (P0) Drop state: `button`/`entry`/`spinbutton`/`.card`
  `:drop(active)` accent rings were erased by our `box-shadow` at priority
  800. *Accept:* the accent ring renders again on every drop target.
- [x] **W3** (P0) `.card` definition ring restated (a white card had no
  edge on a light window).
- [x] **W4** (P1) `spinbutton` gets `ov-inset()`, the material the BACKLOG
  claimed it inherited from `entry` — it does not; `spinbutton` is a
  sibling node in GTK 4.
- [x] **W5** (P1) House timing on the content cells that had upstream
  washes and no timing: bare `row.activatable`, flowbox/gridview children,
  list-based menu rows, notebook tabs, calendar day cells.
- [x] **W6** (P1) `expander-widget` title row wash (the one additive
  material: upstream's only feedback was the arrow's opacity).
- [x] **W7** (P1) `bottom-sheet` / `floating-sheet` on the window rung
  (`ov-depth-window()` — the rung had no surface until now).
- [x] **W8** (P1) `filter: none` on disabled buttons erased upstream's
  `filter: opacity()` dim across the bar families. *Accept:* disabled
  buttons dim again.
- [x] **W9** (P2) Checked-switch hover/press feedback restated (our dish
  gradient swallowed upstream's second layer).
- [x] **W10** (P2) `.view` / `textview > text` container scoop — tried and
  **reverted**: both nodes are content-sized, so the shade scrolls with the
  content (probes in decisions.md). Keeps upstream's flat fill.
- [x] **W11** (P2) GtkCalendar verdict: no demo page in
  `gtk4-widget-factory` or `gtk4-demo` shows a calendar, so the family was
  judged in `tools/render-gallery`'s `calendar` family instead — stock vs
  overlay, light/dark/HC, plus `STATE=prelight` for the header wash. No
  regression; the calendar node itself is stock at rest and only its timing
  changed. A live witness does not exist on this machine (scanned
  `/usr/bin`, `/usr/lib` for `gtk_calendar_new` / `GtkCalendarPopover`:
  telegram-desktop, yad, gtk4-icon-editor, libgtk, libwebkit2gtk — no GNOME
  surface). *Accept:* verdict recorded; reopen if an app that shows one
  enters daily use.
- [x] **W12** (P1) Lit channel fills: the coloured part of a progress bar,
  a scale and a levelbar block was a flat `background-color` (upstream) and
  read as a sticker in the recessed trough. *Accept:* the fill reads lit —
  new L1 register `ov-lit-fill()`, colour underneath left opaque because it
  is data. Numbers, rejected variants and the channel HC-ring defect found
  in passing: decisions.md, "Lit channel fills". Values are one edit wide in
  `_primitives.scss` if the dome wants to be stronger or softer.

---

## Button review — 23 Sep 2026

*Verdicts, measurements and the rejected looks: decisions.md, "Button review
— glass retired, ring thinned". Evidence: gallery `buttons` family, five
states × light/dark/HC, label pairs measured, plus a 2× crop pass.*

- [x] **B1** (P0) The accent glass (translucent fill + hover/press outer
  bloom) is retired: `.suggested-action` wears the lit fill (opaque accent
  under `ov-lit-curve()`, mid stop at zero alpha), `.destructive-action`
  falls through to the generic material, which also re-hues its hover glow
  to the destructive hue for free.
  *Accept:* no CTA label below upstream's own pair, no external drop shadow
  on a button, one lighting language across the family. **Done** — suggested
  3.95 / 3.42 / 5.58 / 5.18 against stock 3.77 / 3.27 / 5.46 / 4.99; the gap
  pixel between two prelight CTAs no longer moves.
- [x] **B2** (P0) Focus ring 2px → 1.5px (user decision), one rule for every
  button family, HC restores 2px. Token: `--ov-focus-ring-width`.
  *Accept:* one weight across flat and opaque buttons; ring still anchored
  to the button edge. **Done** — +1 contract atom.
- [ ] **B3** (P1) Destructive hover/press/held sit 0.2-0.7 below stock
  (4.35 / 3.46 / 2.93 / 2.78 vs 4.60 / 4.15 / 3.06 / 3.06): the house glow
  and well are added to a container whose own upstream pairs are already
  3.06. Lever: exempt `.destructive-action` from the hover glow in
  `_button.scss` (one selector) if the live eye prefers upstream's wash
  alone.
- [ ] **B4** (P1) Hover entry is 280ms on the highest-frequency control
  (`--ov-motion-enter`), 2× the ≤150ms rule for a high-frequency
  interaction. User-approved in the 19 Sep probe and re-kept in the 23 Sep
  live pass, so it was left alone: a global motion decision, not a button
  one.
- [ ] **B5** (P1) The `:disabled` material resets `background-image: none`
  outside HC, which erases the *gradient* face a plain-GTK app's disabled
  button has — the same trap the 19 Sep HC audit found at the resting
  selector. libadwaita buttons are unaffected (flat `background-color`).
  *Accept:* a plain-GTK render (X5 card) decides whether the reset drops to
  `box-shadow` only.
- [ ] **B6** (P2) The bevel pair is asymmetric in the light scheme: the top
  hairline moves the fill +1 level, the bottom −13 (measured), so a resting
  button reads as carrying a bottom rule rather than a bevelled edge.
  *Accept:* re-balance `--ov-bevel-highlight` / `--ov-bevel-shadow`, or
  accept as the same-surface whisper — needs the live eye first.
- [ ] **B7** (P2) A checked toggle may have no press feedback distinct from
  held: the `:active` rule sits *before* `:checked` in `_button.scss`, so
  pressing a checked toggle resolves to the held well and only the `filter`
  dim changes. Not reproduced — no gallery family member carries both
  flags; needs a targeted render before it is trusted.

---

## Hardening (P2 — pull forward when blocking)

- ~~**X1** Flatpak runtime guard: extend `check-selectors` to also
  extract from installed `org.gnome.Platform` runtimes and check the
  contract against the **oldest** libadwaita actually running.~~
  *(Deferred with Flatpak, 18 Sep 2026 — proposal amendment 11.)*
- [ ] **X2** `tools/selftest`: automate P1.7 (bogus contract entries →
  exit 1, both axes reported) so the guard is testable in one command.
- [ ] **X3** Regenerate the proposal's metrics from the pinned version —
  lines, `:root` variable count, hardcoded-hex count, `filter` /
  `background-image` tallies, selector survival — and replace the pre-pin
  numbers in docs/proposal.md.
- [ ] **X4** Bisect rehearsal: deliberately break one visual rule, `git
  bisect` to it, fix it. Verifies one-visual-change-per-commit actually
  pays.
- [ ] **X5** Fractional-scale test card: fixed checklist (headerbar,
  button, list, popover at 1×/1.25×/1.5×) run before every milestone
  sign-off.
- [ ] **X7** The **selector** reverse axis (6 Oct 2026). `check-selectors
  --reverse` covers the variable axis and dead tokens; the selector axis does
  not invert, because `upstream/selectors.txt` deliberately registers the
  atoms *upstream* uses rather than the ones the overlay writes
  (`decisions.md` H2). Consequence: `src/_user.scss` can name an
  unregistered upstream selector and the guard cannot see it. Worth a
  separate allowlist file rather than by inverting the existing contract —
  the contract's own rule is right and must not change.
  *Accept:* a selector named in `_user.scss` but absent from both contracts
  fails `check-selectors --reverse`.

- [ ] **X9** Theme-directory vs user-config rendering (6 Oct 2026). The
  installed `gtk-4.0/gtk.css` and the user-config import are the same bytes;
  GTK loads them by different mechanisms and there is an unexplained
  difference somewhere. A Δ51 figure was quoted and then **withdrawn** — the
  probe had no libadwaita, so the overlay's CTA rules had no upstream
  `background-image` to restate and the sampled pixel was never the overlay's
  material (decisions.md H6). Not chased: the cause is GTK's named-theme
  precedence, not expressible in our CSS, `!important` is banned by rule 4,
  and upstream states no expectation to converge on. Only worth resuming with
  a probe that can render the overlay's CTA in a libadwaita-free environment —
  i.e. a real harness change, not a pixel tweak.
  *Accept:* a probe that (a) renders a CTA through both mechanisms with the
  same bytes, (b) can prove the overlay's own rule is the one painting, and
  (c) reports Δ. Until (b) exists, no number here means anything.

- [ ] **X8** Reconcile the `filter` ban with what the sheet actually does
  (6 Oct 2026). `docs/proposal.md` amendment 7 says "`filter` is banned in
  v1", naming `blur()` as the iGPU risk. The sheet emits **44** `filter:`
  declarations: `brightness(0.96)` for the press read plus `filter: none`
  reverts, and `_button.scss` transitions `filter` on hover/press. The
  intent reads as "no `blur()`, depth is box-shadow ladders", and
  `brightness()` is a press modulation rather than depth — but the text says
  banned and the tree says otherwise. Decide which, then change the doc or
  the sheet. Not done here: 44 declarations is a visual change, and it is
  not this pass's call.
  *Accept:* either the sheet carries no `filter` at all, or amendment 7 says
  in words which functions are allowed and why.

- [ ] **X6** Fold `tools/probe-motion` into the X5 card: build it
  alongside `render-widget`, and run the motion checklist (hover, press,
  focus ring, row entry) against the built sheet in normal, `REDUCE=1` and
  `NOANIM=1` before every milestone sign-off. Catches the two failure
  modes the 23 Sep review fixed: a declaration that replaces upstream's
  transition list, and entry/exit timing declared on the wrong state.
  *Accept:* one command reports every state as IN MOTION, INSTANT or NO
  CHANGE, and the reduce run reports INSTANT for all of them.

## Found during daily drive

- [x] **DD1** (P0) Window translucency read as a layer, not a tint: the
  Extensions window over a browser showed the page's own body text through
  its content. `--ov-surface-window` was the 84% alpha migrated verbatim at
  P1.5 — 16% bleed, ~38/255 of ghost contrast.
  *Accept:* no backdrop text legible through a window in either scheme, the
  backdrop's cast still present, one number changed. **Done** — surface
  alpha measured 215/255 → 245/255 (light and dark); ghost contrast 37/255
  → 9/255 over a #0f1419-on-white backdrop; gallery `headerbar` is the only
  family that moved. Verdict: decisions.md, "Window translucency: 84% →
  96%".

- [x] **DD3** (P0) Helium (Chromium 154, GTK4, `system_theme = kGtk`) opened
  completely black: tab strip, toolbar and viewport. L0 derived its tokens from
  libadwaita custom properties, which do not exist in an app without
  libadwaita — the declarations computed to nothing, and Chromium's GTK colour
  mixer turns "paints nothing" into an opaque black frame (`SkColorSetA(...,
  SK_AlphaOPAQUE)` over an empty 24x24 render). DD2 is the reserved M7 texture
  call.
  *Accept:* no GTK4 app paints nothing; libadwaita apps pixel-identical.
  **Done** — L0 grew the `--ov-up-*` alias layer (one alias per upstream
  variable, GTK named-colour fallback), `upstream/variables.txt` now lists all
  ten, `tools/probe-foreign` reports 9 → 0 inputs painting nothing, gallery
  15/15 families pixel-identical in both schemes, and a live A/B in the
  session reads stock (36386 black px) where the old sheet read 79598.
  Verdict: decisions.md, "Foreign apps: Helium came up black".

---

## Pen pass — 26 Sep 2026

Study and port of LukyVj's *Futuristic Dial Button* (`xxyEYMJ`). The report is
decisions.md, "Pen dial pass: the lit edge" and the sections that follow it.
Four moves, landed separately because each one is a visual change.

- [x] **PN2** (P1) `ov-grow()` — the spread-only push ring, worn by the checked
  switch thumb (1.5px dish in the track's own colour).
  *Accept:* the thumb's footprint grows without any layout change; the dish
  colour is within a few 1/255 of the track it lands on; HC 0 px.
  **Done 26 Sep 2026** — 63 px at rest, +3 to +11/255, rows above and below
  the thumb only; HC 0 px outside the scrollbar strip (decisions.md, "Pen dial
  pass 2/4: the dish ring"). The scale knob deliberately does not wear one:
  its surround is not uniform (decisions.md, same section).

- [x] **PN3** (P1) The lit fill's curve and hairline pair from the accent's own
  hue, on the one fill whose colour is decoration (`.suggested-action`).
  *Accept:* the CTA's light and shade rungs carry the fill's hue; the label's
  band is bit-identical; the semantic fills (progressbar variants, levelbar,
  scale highlight) do not move; HC 0 px.
  **Done 26 Sep 2026** — 10244 px light / 11384 dark, confined to rows 167-177
  and 189-200, 0 px in the label band (178-188); `controls` 0 px; HC 0 px
  (decisions.md, "Pen dial pass 3/4").

- [x] **PN4** (P1) `--ov-debug`, as a build rather than a token: an outline on
  every node for hunting a rule that is aimed at the wrong node.
  *Accept:* `tools/build --debug` installs an outlined sheet and a plain
  `tools/build` restores the previous one; the shipped sheet carries no
  `outline` on `*`; defaults emit nothing.
  **Done 26 Sep 2026** — debug sheet 864 lines to the shipped 858, `controls`
  10541 px vs the shipped sheet; shipped sheet has no `* {` rule
  (decisions.md, "Debug build").

- [x] **PN5** (P1) Review pass over PN1-PN4 (26 Sep 2026): the lit rungs moved
  from srgb approximations to relative HSL — the pen's exact stops were
  reachable all along (upstream's own gtk.css:1429 uses the syntax).
  *Accept:* hue held (thumb hairline hsl(213,63,49) vs track 213,63,51), CTA
  label pair unchanged at 3.81:1, HC 0 px, determinism 0 px across all eight
  cells.
  **Done 26 Sep 2026** — decisions.md, "Review verdict on the pen pass".

- [x] **RR4** (P0) Neon hover (hot core + tube + halo) replaces the gradient
  glow; the accent register is re-derived on `.destructive-action` so it glows
  red; the button face (inset convex/concave) and a 0.5px rim, toned down per
  the user. **Done 26 Sep 2026** — decisions.md, "Button face, neon hover, rim".

- [x] **RR5** (P1) Nautilus path bar: every crumb is flat text in a recessed
  well (a current-folder cap was tried and dropped — Nautilus stretches that
  crumb across the bar). **Done 26 Sep 2026** — decisions.md, "Path bar".

- [x] **RR6** (P1) Tabs (reopens U6): AdwTabBar strip wears the bar material,
  the selected tab is the raised cap, its close button is flat; notebook's
  checked tab is a cap with a lit accent underline. **Done 26 Sep 2026** —
  decisions.md, "Tabs".

- [x] **RR1** (P0) Rest register: bevel x4 + a soft drop on raised buttons
  (user: "these buttons look pretty darn flat"). Candidates rendered first;
  x4 + drop chosen; the 19 Sep no-drop rule retired (user).
  **Done 26 Sep 2026** — decisions.md, "Rest register".
- [ ] **RR2** (P2) Explain the 11/255 lighter fill on the Normal button under
  `STATE=checked` after RR1 (bisected to the bevel tokens alone; no `:checked`
  rule reads them).
- [x] **RR3** (P0) Toggle groups styled (well + raised cap); gallery `adw` gains
  a toggle group. **Done 26 Sep 2026** — decisions.md, "Toggle groups".

- [x] **PN1** (P1) Accent-lit bevel on the engaged thumbs.
  *Accept:* a checked switch thumb and a hovered/dragged scale knob carry the
  accent's own light instead of the white/black hairline pair; both schemes;
  HC 0 px; an unchecked switch at rest is untouched.
  **Done 26 Sep 2026** — rest 92 px, prelight 292 px, dark 192 px, HC 0 px
  (decisions.md, "Pen dial pass: the lit edge"). Reverses the 19 Sep thumb
  calibration for `switch:checked` — thumb only, track untouched.

---

## G — GTK3 accent — 27 Sep 2026

*GTK3's built-in Adwaita (gtk3 1:3.24.52-1) has no accent: `_colors.scss`
hardcodes `#3584e4` and sassc bakes it and every derivation into literals, so
every GTK3 app here paints blue under the `teal` accent. Scope: accent only;
material (bevel/texture/depth) on GTK3 waits for daily-drive evidence (G6).
Method and counts: decisions.md, "GTK3 accent: scope and method".*

- [x] **G0** (P0) Scope call: GTK3 is in, accent only.
- [x] **G1** (P0) GTK3 contract axis. `fetch-upstream --gtk3` caches the
  pinned tag's Adwaita SCSS and refuses it unless sassc reproduces the
  checked-in sheets byte for byte; `check-selectors` checks both toolkits
  with no argument (the hook, now also triggered by `gtk3`) and reports
  SHEET DRIFT when the installed sheet is not the pinned tag's.
  *Accept:* contract OK on both; a planted missing selector and a drifted
  cache each exit 1. **Done 27 Sep 2026.**
- [x] **G2** (P0) Accent inventory: `tools/gtk3-accent-sites` compiles the
  source with the stock accent and two sentinels and lists every declaration
  whose value moves. *Accept:* declaration-level TSV for both variants.
  **Done 27 Sep 2026** — 251 rules / 373 declarations (light 131/194, dark
  120/179), vs 71 literal `#3584e4` in the light sheet.
- [x] **G3** (P0) Generated theme `Adwaita-overlay`: `tools/build-gtk3`
  recompiles the pinned GTK3 Adwaita + libhandy Adwaita SCSS with the accent
  and installs it as a theme (priority 200), not the user sheet — revised
  from the plan, see decisions.md "G3: a theme, not the user sheet".
  *Accept:* stock accent renders pixel-identical to built-in Adwaita (light
  and dark); for each non-blue accent the stock-blue pixel count falls to the
  accent-independent floor. **Done 27 Sep 2026** — stock accent: identical
  bytes, both schemes; stock sheet 17225/16561 blue px (light/dark) →
  228–253 / 303–424 for red, pink, orange, yellow, green, purple, teal (the
  floor is the same widgets under red, so not accent); slate 369/462, being
  blue-grey itself. Disabled CTA unchanged vs stock. HC still swaps GTK3 to
  `HighContrast` with the theme active; GTK4 `probe-foreign` output identical.
- [ ] **G4** (P1) Accent sync: a user service on `gsettings monitor
  org.gnome.desktop.interface accent-color` reruns `tools/build-gtk3`. Dark
  (GTK3's own `gtk-dark.css` switch) and high contrast (GDK swaps to
  `HighContrast`, verified) need nothing. *Accept:* changing the accent
  rewrites the theme; a restarted GTK3 app follows (check whether a running
  one does).
- [ ] **G5** (P1) `render-gallery3`: GTK3 offscreen renderer + `gallery-diff`,
  so G3/G4 are judged in numbers.
- [x] **gtk3 contract was vacuous** (6 Oct 2026). `diff_contract` returned 0
  on a missing contract file, and `upstream/gtk3/selectors.txt` /
  `variables.txt` were never written — so `contract OK against gtk3` had
  been printing green while checking nothing. Both files are now written
  (G6) and the axis discriminates: verified by doctoring a cached sheet —
  dropping an accent-carrying atom reports exactly that atom and exits 1,
  renaming a `@define-color` reports the old name and exits 1, and dropping
  an unregistered atom correctly reports nothing. `diff_contract` also still
  prints `CONTRACT NOT WRITTEN` for an absent contract rather than a false
  pass, and still returns 0: an absent contract is not an upstream break, and
  the pacman hook must never block a system upgrade.

- [x] **G6** (P2) Material on GTK3 — only if daily driving shows GTK3 apps
  reading inconsistent. Hand-written; would use `upstream/gtk3/selectors.txt`
  and `variables.txt` (named colours), which `check-selectors` already reads.
  **The contract half is DONE (6 Oct 2026); the material half is not.**
  `upstream/gtk3/variables.txt` (36 names) and `upstream/gtk3/selectors.txt`
  (434 atoms) are written, so the GTK3 axis of `check-selectors` is no longer
  vacuous. What is still open is the *material* question in the first
  sentence: whether GTK3 apps should get the bevel/texture/depth system, not
  just the accent. Deliberately untouched — the GTK3 front is a recompile of
  upstream's own Adwaita, so adding material means forking that SCSS, which
  is a different decision from the one this contract guards.

---

## Out of scope — explicit, do not creep

- The GNOME Shell stylesheet (a separate project, only if the desktop
  reads inconsistent once apps are done)
- Upstream contributions, issues, MRs — libadwaita does not accept
  AI-assisted contributions; nothing from this tree is ever filed
- Redistribution of any kind
- Patching, forking or replacing libadwaita
- Widget internals, layout, metrics, padding, adaptive breakpoints, window
  controls
- Flatpak apps and runtimes — deferred 18 Sep 2026, revisit if Flatpak
  GNOME apps enter daily use

---

## Daily drive — STARTED 19 Sep 2026

The clock is running. The stylesheet is complete and live; from here the
project's only input is real use. Annoyances, breakages, and fatigues go
to "Found during daily drive" above. The gate: two weeks (M7), then the
final texture keep/drop call (DD2).


## Accent register + contracts pass — 6 Oct 2026

Started as "borrow what other projects do"; ended as a correctness pass over
the accent register and a hole in the contracts. Verdicts and measurements
are in `docs/decisions.md` H1–H4.

- [x] **Accent register in oklab.** HSL spread the same lit rung across
  **0.132** of perceived lightness over the nine system accents (0.852 red →
  0.983 yellow); an absolute oklab target spreads it across none, because
  upstream already pins the sources into L 0.499–0.509. Blue-anchored, so
  the accent that was tuned by eye does not move. The 92%/83% saturation
  choice did **not** port: measured C(rung)/C(source) runs 0.34–1.82, so only
  the intent carried over. Verdict: decisions.md H1.
  *Accept, met:* 12/15 families byte-identical vs the HSL build in light,
  13/15 in dark; **0 px changed across all 15 under `prefers-contrast: more`**;
  `probe-foreign` exit 0; `calc(a * 0.40)` proven to resolve by forcing the
  target and watching pixels move.

- [x] **Contract reverse axis.** `check-selectors --reverse`: every upstream
  property the built sheet reads is in `variables.txt`, every `--ov-*` it
  declares is read somewhere (52 tokens, 0 dead). Verified to bite by
  injecting both fault classes. The selector axis does not invert — that is
  the contract's own rule, and it is **X7**. Verdict: decisions.md H2.

- [x] **`tools/probe-accent`.** The register is live: 2/2 links LIVE in light
  and dark, control STALE, exit 0; stock control exits 1. Along the way it
  established that the lit rungs paint only in a state, and that L0 aliases
  `--accent-bg-color` and `--accent-color` **separately**. Verdict:
  decisions.md H3.
  *Accept, met:* the probe fails on a sheet with no register.

- [x] **`src/_user.scss`.** Personal overrides, last word, empty by default.
  Safe because of the reverse axis, which was the reason to build that first.
  *Accept, met:* a legal use passes; an unregistered upstream variable fails
  with exit 1 and names the line to add.

- [x] **Cascade trap recorded.** Equal-specificity custom properties are
  last-wins; a prepended `:root` palette override silently does nothing.
  Verdict: decisions.md H4.

### Metrics at the 6 Oct 2026 pin (for X3)

| | |
| --- | --- |
| SCSS | 2757 lines |
| built sheet | 1064 lines, 84 813 bytes |
| `--ov-*` tokens | 52 (0 dead) |
| upstream properties read | 13 (all registered) |
| hardcoded hex outside L0 | **0** |
| `color-mix()` | 248 |
| relative-colour forms | 29, of which `oklab(from …)` 12 |
| `!important` | **0** |
| `prefers-contrast` blocks | 35 |
| `filter:` declarations | **44** — see X8 |

These replace the pre-pin numbers quoted in `docs/proposal.md`, which X3
still owns; the `filter` row is the one that moved.
