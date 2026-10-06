<!--
SPDX-FileCopyrightText: 2026 mauriciobc
SPDX-License-Identifier: LGPL-2.1-or-later
-->
# Decisions — adwaita-overlay

Running decision record. Each entry names the evidence that produced it.
Experiments reference `docs/evening-0.css` (T1–T4) and BACKLOG.md items.

## Evening 0 — 18 Sep 2026

**Method.** `tools/render-widget.c` (promoted from this session's scratch
harness): renders one widget with one CSS file offscreen via
GtkWidgetPaintable + GskCairoRenderer, using the same mechanism as the
overlay itself — a CssProvider on the default display at
GTK_STYLE_PROVIDER_PRIORITY_USER (800). Verdicts come from pixel analysis
of the TIFF output; contact sheets for human confirmation are scratch
(`/tmp`). The harness briefly presents a window (~400 ms) because
GtkWidgetPaintable only renders mapped widgets.

### E1 — border-image × border-radius: NOT clipped → 9-slice dead

- Control: plain 4px border on a 12px-radius button — 0 of 4 sampled
  pixels outside the corner radius opaque (normal borders are clipped).
- border-image: 3 of 4 opaque (only the exact corner subpixel survives),
  border band renders normally (mid-top pixel 252,0,2).
- Conclusion: GTK follows web semantics — border-image draws **square
  corners over a rounded fill**.

**Decision:** `bevel()` is inset box-shadows (primary mechanism) and
clipped gradient background layers (for bevels wider than ~2px). The
assets/ SVG 9-slice pipeline is descoped; assets/ remains for texture
tiles only, with `data:` URIs preferred. Every bevel declaration must
restate upstream's existing `box-shadow` stops (we override, not append).

### E2 — inset bevel pair: renders, crisp at 1×

Column profile through the button (x=10, left of the label):

- sharp jump into the highlight band at the top edge (202 → 250 in one
  row), flat background through the middle, a dark line at the bottom
  edge (177 at y=60).
- Blur-0 shadows stay blur-0: no smearing.

**Decision:** the inset pair is the bevel mechanism. The 1.25×/1.5×
fractional cells are not blocking (display runs 1×): they are covered by
the X5 test card before milestone sign-offs.

### E3 — pressed inversion: pending (human verdict)

Press-feel cannot be measured. Paste T3 from docs/evening-0.css into
Inspector and judge: physical depression, zero layout shift, default and
pill variants.

### E4 — grain tile: renders

- feTurbulence data: URI tile renders via librsvg: luminance stddev
  0.562 over a 32×32 crop at 5% opacity (flat background would be ~0).
- Shimmer-on-scroll verdict: pending (human).

**Provisional call:** keep texture as a headerbar-only garnish; final
keep-or-drop at DD2, and it stays independently killable via
`--ov-texture: none` regardless.

### E5 — bevel() mixin shape + elevation ladder sketch

From E1/E2:

```scss
// sketch — exact token names and restated upstream stops land with D2
@mixin ov-bevel($width: 1px) {
  box-shadow: inset 0 $width 0 var(--ov-bevel-highlight),
              inset 0 -$width 0 var(--ov-bevel-shadow);
  // plus the restated upstream box-shadow stops for this surface
  @media (prefers-contrast: more) { /* revert to upstream's stops */ }
}
```

Elevation ladder (4 rungs; exact values derive in D1 from upstream
variables — this fixes only rung count and usage map):

| Rung | Used by | Character |
| --- | --- | --- |
| flat (0) | list rows, inline content | no shadow |
| raised | buttons, entries, cards | 2 stops, shallow |
| overlay | popovers, menus | 3 stops, deeper |
| window | headerbar cluster, windows | deepest, widest |

Light source: top, everywhere. Highlights on top edges, shade on bottom
edges, all shadows fall downward.

---

**Method.** Harness renders (tools/render-widget.c) + node-tree dump
(nodes.c, scratch). The harness's texture-mode verdicts are sound; its
button/box geometry is unreliable: the widget's real allocation never
equals the requested size (win 270×217 / widget 222×34 vs requested
200×100), and GtkWidgetPaintable rescales — so absolute pixel geometry
from button/box renders must not be trusted. (Earlier Evening-0 verdicts
E1/E2/E4 are unaffected: they judged clipping/crispness/rendered-ness of
the rendered surface itself, not absolute geometry.)

### Discovered constraint: var() substitutes single value tokens only

- `box-shadow: inset 0 var(--ov-bevel-width) 0 var(--ov-bevel-highlight)` — works.
- `box-shadow: … , var(--ov-depth-raised)` where the token holds a whole
  multi-stop segment — **does not render** (single-stop segment fails too).
- GTK docs are silent on substitution semantics ("no direct replacement"
  for non-colour types); upstream's sheet follows the constraint: per-
  component vars only (`--shade-color`, `--border-opacity` inside
  `color-mix`), never a whole segment from one var.

**Decision:** geometry lives in L1's mixin (per-rung functions with literal
stop geometry); colours flow through L0 tokens. Consequences:

- Depth kill switch is `--ov-depth-color: transparent` (one token, shared
  base colour of every ladder stop — was `--ov-depth-raised: …`).
- The M0-era ladder tokens `--ov-depth-raised/overlay/window` are removed
  from L0 — the rungs are now `ov-depth-raised()` etc. functions in L1.
- D5's acceptance is reworded in BACKLOG.md to match the mechanism.

### Verified by render (texture mode + colour-mode knob, reliable)

- texture renders (stddev 0.618 @ 5% opacity)
- `--ov-texture-image: none` → flat (0.000) — kill switch works
- `CONTRAST=more` + `@media (prefers-contrast: more)` revert → flat
  (0.000) — the inline L3 revert works

### NOT verified by harness (geometry-unreliable)

- bevel pair presence/crispness *in the M2 mixins* (E2 earlier validated
  the mechanism from hand-written CSS)
- ladder rungs' outer shadow rendering

**These move to the Inspector session (with E3/E4):** paste the compiled
`d-raised.css` / `d-kill.css` on a real surface and confirm by eye — the
compiled declaration matches upstream's proven `box-shadow` pattern, so
rendering is expected; what needs a human is the aesthetic anyway.

## Pressed-state decision — 19 Sep 2026 (Inspector session, live)

User verdict after judging the refined variants: **N4 — the pressed well.**

- Resting state: the tuned 0.5px hairline pair (white@30% / black@15%),
  scoped to opaque buttons (`.raised` + default; `.flat`/`.osd` excluded).
- Pressed state: the bevel + ladder are REPLACED by an inverted well —
  shadow moves to the inside:
  ```css
  box-shadow: inset 0 2px 4px color-mix(in srgb, black 10%, transparent),
              inset 0 -1px 1px color-mix(in srgb, white 30%, transparent);
  filter: brightness(0.96);
  ```
- No outward shadows during press; no translate. The surface sinks.
- Plus the 90ms ease-out transition on press AND release (box-shadow,
  filter) — user confirmed the animation was the missing piece in the
  earlier snap version.

Implementation note: the pressed well REPLACES the resting pair and rung
(they cannot coexist — two competing inset stories). ov-elevate's pressed
companion emits exactly the well; restated upstream stops still apply in
both states.

**REVISED same day:** the user struck the resting bevel entirely —
**no resting styling at all.** Idle buttons stay stock Adwaita; N4's well
is pressed-only. The 0.5px hairline values remain on record as the tuned
bevel recipe; `--ov-bevel-width: 0px` is now effectively the standing
state (no surface emits the resting pair by default).

## Bevel tuning — 19 Sep 2026 (Inspector session, live)

The resting bevel was tuned on real buttons by the user:

- **Geometry:** 0.5px insets — sub-pixel hairlines (GSK antialiases them;
  graceful at fractional scale, unlike 1px+ pairs).
- **Strength:** highlight white@30%, shadow black@15% — "whisper" register
  (R2 direction, weaker than shipped R1's 55/30).
- **Scope:** NOT all buttons — opaque buttons only. Flat buttons (headerbar
  buttons, sidebars) stay flat; L2 will scope via `button:not(.flat)`-
  style selectors when surfaces land (M3). Upstream already uses `:not()`
  extensively, so the selector pattern is proven in-tree.
- These values are now the shipped L0 tokens (`--ov-bevel-width: 0.5px`,
  `--ov-bevel-highlight` @30%, `--ov-bevel-shadow` @15%).

## Neumorphism probe — 19 Sep 2026

N1–N3 (permanent extrude variants) rejected by the user after live judging:
carpet-bomb `button {}` selectors turned the whole window to mud — scoped
probe (block 1f, headerbar `.raised` controls only) was needed to judge
anything. **N4 — the pressed well — won:** depth exists only during
interaction; the resting state stays the 0.5px hairline whisper. Permanent
outward soft-shadows (classic neumorphism) are rejected as the resting
material.

Scoping lesson recorded: `.raised` (+ default, non-flat) buttons are the
material carriers; `.flat`/`.osd` never wear it. This is upstream's own
flat/raised split (1.4+), so the selector pattern is native.

## Neumorphism probe — 19 Sep 2026

N1–N3 (permanent extrude variants) rejected by the user after live judging:
carpet-bomb `button {}` selectors turned the whole window to mud — scoped
probe (block 1f, headerbar `.raised` controls only) was needed to judge
anything. **N4 — the pressed well — won, and was then simplified further:**
no resting styling AT ALL. Idle buttons stay stock Adwaita; the well is
pressed-only, with the 90 ms ease.

Final pressed spec (the only button material):
  button:active (and :keyboard-activating) {
    box-shadow: inset 0 2px 4px color-mix(in srgb, black 10%, transparent),
                inset 0 -1px 1px color-mix(in srgb, white 30%, transparent);
    filter: brightness(0.96);
    transition: box-shadow 90ms ease-out, filter 90ms ease-out;  /* on base */
  }
Resting bevel: none. Ladder: none on buttons. Flat/osd: never.

## E4 delivery finding — 19 Sep 2026 (Inspector session)

This Nautilus build paints its top bar with a plain **GtkBox** — no
`headerbar` node exists in the window (upstream's `.top-bar > headerbar`
selectors target other apps). Consequences, all observed live:

- `background-color` pastes reach the box (boxes paint their own bg) —
  the yellow probes worked on the sidebar AND top bar.
- `background-image` / `background: url(...)` pastes are **silently
  dropped** — plain GtkBox widgets do not paint CSS image layers.
- The feTurbulence data-URI failure earlier was never independently
  confirmed dead; the delivery failure masked it.

**E4 resolution:** the grain experiment cannot be judged in this window —
the surface it targets doesn't exist there. Options for closing E4
properly: (a) judge the grain on a真 headerbar app (gnome-calculator,
Epiphany), (b) accept texture as dormant until M3 surfaces exist and
verify with the harness+Inspector there. Record as "E4 deferred with
reason", not failed.

**Architecture note for M3:** Nautilus's headerbar-less top bar means
L2's headerbar surface rules must target the *container pattern*
(toolbarview > .top-bar children) AND real headerbars — two selector
families, both registered in the contract.

## E4 verdict — 19 Sep 2026 (live, real provider)

Grain judged on a real headerbar via the overlay file (the only working
delivery — Inspector CSS tab drops url() layers entirely; Nautilus's top
bar is a GtkBox with no image layers at all):

- **Shimmer:** none while scrolling ✓
- **Dark mode:** 5% dark-speck tile is far too noisy on dark surfaces ✗
- **Strength:** 5% on light "seems nice" (user)

**Resolution:**
- Shipped: scheme-split tiles — `assets/grain-light.png` (dark specks,
  5%, light scheme) and `assets/grain-dark.png` (light specks, 3%, dark
  scheme — dark surfaces show noise more), selected via
  `prefers-color-scheme` in the texture token pair.
- Delivery shape (proven): `background:` shorthand + restated fill, never
  bare background-image.
- Texture stays a headerbar-only garnish (M3 wires it); HC reverts it.

Findings ledger (session total):
1. feTurbulence data-URIs render empty in app processes (librsvg).
2. Plain GtkBox surfaces drop background-image layers (Nautilus top bar).
3. The Inspector CSS tab drops url() layers — only the real user
   provider delivers images. All three now documented.

## Entry surface spec — 19 Sep 2026 (live-tuned, user-approved: "AWESOME")

Search/text entries get a permanent recessed material (the first container
depth), with a deeper well on focus — the user's model: "everything that
has depth keeps it in all states; interaction only modulates depth."

```css
entry,
entry:focus-within {
  background-image: linear-gradient(to bottom,
    color-mix(in srgb, black 5%, transparent),
    color-mix(in srgb, black 0%, transparent) 40%);   /* the scoop */
  box-shadow: inset 0 1px 3px color-mix(in srgb, black 9%, transparent),
              inset 0 -1px 0 color-mix(in srgb, white 20%, transparent);
}
entry:focus-within {
  box-shadow: inset 0 2px 5px color-mix(in srgb, black 13%, transparent),
              inset 0 -1px 0 color-mix(in srgb, white 20%, transparent);
}
```

Why v1 failed perceptually: 1px black@7% over a white fill is a ~76-point
one-row band — invisible on small fields. v2 adds the interior scoop
gradient (the eye needs the FIELD shaded, not a line) + deeper focus
(13%/5px vs 9%/3px) so focus-modulation is actually perceptible.

Landing plan (M2/M3): `ov-inset()` primitive in L1 with these values;
entry/textview surfaces in L2; upstream's focus outline coexists (it is
an outline, not box-shadow — proven compatible).

## Headerbar surface spec — 19 Sep 2026 (live-tuned, user-approved)

Final recipe (user iterated the gradient live and approved "looks nice" at
the full values):

```css
headerbar {
  background:
    url("assets/grain-light.png"),                /* grain @5%, repeat */
    linear-gradient(to bottom,
      color-mix(in srgb, white 14%, var(--headerbar-bg-color)),
      color-mix(in srgb, black 10%, var(--headerbar-bg-color))),
    var(--headerbar-bg-color);
  background-repeat: repeat, no-repeat, no-repeat;
}
```

Notes:
- First gradient attempt (4%/3%) was subliminal — a ~47px bar cannot show
  a 7-point swing; user iterated to 14%/10% ("statement headerbar").
- This is M3's headerbar surface rule, pending: HC sweep verdict, dark
  mode pass, and the widget-selector split (real headerbars + Nautilus's
  GtkBox top bar need separate delivery — the gradient shorthand shape
  works for both).

## L1 landing — 19 Sep 2026

All live-tuned recipes landed as L1 primitives (src/_primitives.scss):

- ov-bevel() — tuned hairline pair, DORMANT by default
- ov-depth-*() — container ladders, unused until a surface asks
- ov-well() + ov-press() — THE button material (N4 + 90ms ease,
  :active + :keyboard-activating, $upstream restating, HC revert)
- ov-inset() — THE entry material (scoop + recess, deeper on focus,
  HC revert)
- ov-texture() — scheme-split grain (tiles land with M3 wiring)

New governing principle recorded: "everything that has depth keeps it in
all states; interaction only MODULATES depth" (entries). Buttons are the
explicit exception (no resting material; depth appears only on press).

## Hover glow — 19 Sep 2026 (user-locked, v4)

The button state machine is complete — four states, all user-tuned live:

  rest    — stock Adwaita (nothing)
  hover   — ov-glow(): five-layer soft rim, accent edge-pooling
            (16/14/11/11% rim washes + 5% center bloom), label 45% accent
  pressed — ov-well(): amplified inset well (3px/6px, black@16%) + lip,
            brightness 0.96
  held    — ov-well-held(): light inset (1px/3px, black@8%)

Geometry odyssey recorded: flat tint (rejected, "transparent overlay") →
center-bloom radial (rejected, "inverted") → hard rim (close) → v4
layered soft rim (locked). Lesson: "glow" meant edge-pooling with smooth
multi-stage falloff, not a hotspot.

Scope: .flat/.osd excluded. HC: all four states revert flat. Contract
clean against 1:1.9.4-1 after registering button:hover.

## House motion spec — 19 Sep 2026 (user-approved)

One curve, one duration for state, one press exception:

- curve: cubic-bezier(0.25, 0.46, 0.45, 0.94) — upstream's own standard
- state changes (hover glow, entry focus deepening, checked color): 200ms
- press well: keeps the snappier feel via the same curve at 90-200ms —
  user judged the unified 200ms "elegant" in the live probe; press
  inherits it rather than keeping the 90ms exception.
- switch accent wash: 180ms ease-out (matches native knob slide).

Landed: _button.scss, ov-press(), entry transition (90ms -> 200ms pending
match), switch kept at 180ms ease-out.

## C2 performance gate — 19 Sep 2026

Measured (harness: 200-row GtkListBox, 700x500, full snapshot+render via
GskCairoRenderer, 200 iterations):

  stock Adwaita:   3.19 ms/frame
  with overlay:    2.80 ms/frame

Both an order of magnitude inside the 16.7 ms budget, and the overlay
measures slightly FASTER than stock (within noise; the row washes
replace upstream's hover work rather than adding to it at rest). The
gradient+grain on chrome surfaces and row micro-washes cost nothing
measurable. No family needs a perf-based restriction. Verdict: C2 PASSED.

## Tabbar/viewswitcher decision — 19 Sep 2026 (U6)

DEFERRED, with reasoning: tabbar/viewswitcher tabs are `.flat` buttons
inside a box that upstream itself gives a subtle inset "slot" look
(the whole point of the Adwaita 4.9 tab redesign). Our material adds
nothing they don't already have — the tabs ARE a groove in upstream's
language. Restyling them would fight upstream's structure for zero
visual gain. Revisit only if the daily drive surfaces a specific tab
that looks broken against the new material.

## HC structural audit — 19 Sep 2026

The build's material declarations (box-shadow / background-image) all sit
inside @media (prefers-contrast: more) blocks or are their reverts —
zero material declarations exist outside HC coverage. 10 HC blocks cover
the full stylesheet. Structural audit PASSED. The user's live sweep
(toggle + eyeball) remains the final gate ritual, but the structure is
proven complete.

## Light/dark matrix audit — 19 Sep 2026

Color audit of the built stylesheet:

- zero raw hex literals anywhere
- `white`/`black` keywords appear ONLY inside color-mix() derivations
  (highlight/shadow pairs, scoop gradients) — they are light-direction
  physics constants, not palette colors; every *palette* color flows
  through upstream vars (--light-1, --dark-5, --window-bg-color,
  --headerbar-bg-color, --accent-*, --view-bg-color...)
- the single rgb(from ...) is the P1.5 content-pane relative-color
  derivation (user-tuned)

Scheme behavior: gradient stops mix over scheme-aware base colors, grain
tiles switch via prefers-color-scheme, well/scoop/glow use black/white
derivations that invert meaningfully in dark. Structural dark-verification
PASSED; the eyeball pass rides with daily driving (any dark-mode artifact
lands in the daily-drive section).

## Full stylesheet complete — 19 Sep 2026

The overlay covers: buttons (4-state machine + variant dials), entries,
switches, headerbars, toolbar family, lists/cards, popovers, scrollbars,
window/panes. 238-line build from 12 source files, 30+ contract entries,
house motion spec, C2 passed, HC structurally proven, zero raw colors.

The stylesheet is COMPLETE for daily driving. Remaining gates: the user's
live HC eyeball ritual, and the two-week daily-drive (M7).

## List hover softening — 19 Sep 2026

User: hover animation on list rows should be softer without jank.

The jank trap identified first: the current implementation transitions
`background-image` (a 3-stop gradient). Gradient-to-gradient
interpolation in GSK is a re-rasterization per frame — at 200ms on a
dense list that's real work, and it's why softness has felt risky here.

Fix: transition `background-color` (GPU-trivial, GSK lerps it natively)
and make the hover state a FLAT color wash, not a gradient. Softness now
comes from three honest dials: lower peak alpha, longer duration
(280ms), and the same house curve — not from a gradient shape that the
renderer struggles with.

## System-wide micro-interaction rollout — 19 Sep 2026

User: extend micro-interaction improvements across all system components;
strictly zero visual design or material alterations.

1. Asymmetric mechanical timing applied across interactive components:
   - Hover approach: gentle 280ms on the house curve `cubic-bezier(0.25, 0.46, 0.45, 0.94)`.
   - Release / departure: clean 200ms spring-back (no sluggish lingering).
   - Active press acknowledge: 120ms immediate mechanical tactile response
     on buttons (`button:not(.flat):not(.osd):active`, `ov-press()`, suggested/destructive),
     popover menu buttons, and all list rows (`boxed-list`, `content`,
     `boxed-list-separate`, expander row headers).

2. Menu / popover jank elimination:
   - Struck the legacy 2-stop `background-image` gradient on hover (a leftover from
     before M4 list row softening) in favor of the GPU-trivial flat color wash
     (`var(--accent-color) 5%` hover, `9%` active), fully aligning menu rows
     with the list row interaction spec.

3. Zero design drift:
   - No new decorative properties, shadows, or colors added to controls.
   - Flat buttons, switches, scrollbars, and entries keep their locked designs.

## Controls surface extension (scale, progress, check, radio, toast) — 19 Sep 2026

User approved Option A across remaining candidate controls:

1. Sliders & Progress (`scale`, `progressbar`, `levelbar`):
   - Troughs (`scale > trough`, `progressbar:not(.osd) > trough`, `levelbar > trough > block.empty`):
     recessed mechanical channel with top-shade hairline (`inset 0 1px 1px -1px black@40%`)
     and bottom reflection lip (`0 1px 1px -1px white@70%`), matching the `switch` track.
   - Slider knob (`scale > trough > slider`): floating disc with top-light gradient
     and soft drop shadow (`0 2px 4px @20%`), paired with `120ms` mechanical depression
     on active drag (`0 1px 2px @25%` + inverted inset well). Disabled collapses shadow.

2. Checkboxes & Radios (`check`, `radio`):
   - Tactile microcavity (`inset 0 1px 2px black@10%`) within the restated 2px ring.
   - Gentle `280ms` hover wash (`var(--accent-color) 5%`).
   - Dry, crisp `120ms` mechanical depression (`inset 0 2px 4px black@16%`) on active click.
   - Checked state active compression (`inset 0 1px 2px black@18%`).

3. Toasts (`toast`):
   - Restated at the overlay elevation rung with 3-stop diffuse shadow
     matching suspended popovers/menus.

4. Contract & Invariants:
   - All selectors registered in `upstream/selectors.txt` and verified via `tools/check-selectors`.
   - Full HC reverts under `prefers-contrast: more`.
   - Zero raw hex; zero layout shifts.

## AdwToolbarView top-bar unstyled gap fix — 19 Sep 2026

1. The Bug:
   - In apps utilizing `AdwToolbarView` with multiple top widgets (e.g. HeaderBar + SearchBar in `gnome-extensions-app`, HeaderBar + TabBar in `nautilus`), Libadwaita assigns `.collapse-spacing` to the internal vertical `GtkBox`:
     `toolbarview > .top-bar .collapse-spacing { padding-top: 3px; padding-bottom: 3px; }`
   - Because `headerbar` was styled directly, its background started at y=3. The top 3px belonged to the parent `GtkBox`, which was unstyled, leaking the underlying window / content-pane background (especially prominent with translucent / blurred window surfaces such as Blur my Shell).

2. The Solution:
   - Follow Libadwaita's architecture by applying the surface gradient and grain to `toolbarview > .top-bar` as well as standalone `headerbar`.
   - Set `background: none` on nested `toolbarview > .top-bar headerbar` to prevent double-painting.
   - Upstream contract updated with `toolbarview > .top-bar headerbar` and `toolbarview > .top-bar.raised`.


## Sidebar surface styling (split-view & navigation-sidebar) — 19 Sep 2026

1. The Surface & Scope:
   - Split-view container pane (`.sidebar-pane`) and navigation list widgets (`.navigation-sidebar` across `row`, `child` list items, and `flowboxchild`).
   - Includes legacy tree rows, `sidebar .navigation-sidebar > row`, `placessidebar .navigation-sidebar > row`, and inline item actions (`button.sidebar-button`).
   - Dedicated surface extracted to `src/surfaces/_sidebar.scss`.

2. Material & Micro-interaction Decisions:
   - Asymmetric hover entry & exit microinteractions:
     * Entry (mouse enters item): gentle `280ms` ease on the house curve (`cubic-bezier(0.25, 0.46, 0.45, 0.94)`) declared on `:hover`. Eliminates stock Adwaita's abrupt, unsmoothed flashes when hovering.
     * Exit (mouse leaves item): clean `200ms` spring-back declared on the base item (`> row`, `> child`, `> flowboxchild`, etc.), preventing sluggish trails when skimming down navigation trees.
     * Active press acknowledgment: crisp `120ms` mechanical tactile response.
   - Jank prevention: flat `background-color` washes (6% hover, 10% active, 9% selected, 13% selected hover, 16% selected active) via `currentColor` derivations. Avoids GSK gradient re-rasterization overhead on dense navigation trees.
   - `has-open-popup`: pins the hover wash when context menus are open.
   - Micro-actions: `button.sidebar-button` (unmount, eject, add bookmark) receives matching 280ms entry, 200ms exit, and 120ms active transition washes.
   - Separators: subtle hairline division via `color-mix(in srgb, currentColor 8%, transparent)`.

3. Contracts & Invariants:
   - 31 selector atoms registered in `upstream/selectors.txt` and verified via `tools/check-selectors`.
   - Clean high-contrast reversion under `prefers-contrast: more` (transparency restores upstream's 1px HC outline).
   - Zero raw hex; dark mode and accent tracking automatic.
## Motion review — 23 Sep 2026

**Scope.** Every state transition in the stylesheet against the house
motion spec (19 Sep) — plus what the spec never asked: what GTK 4.22 can
animate at all, whether the sheet was animating what it said it was, and
what the platform's accessibility axis leaves to us.

**Method.** Two scratch probes, both kept: `tools/probe-motion.c` loads
one CSS file at priority 800 (the overlay's own mechanism), walks a real
`button` and a real `.boxed-list` row through `:hover`/`:active`/
`:focus-visible` by setting state flags, and reports the widget's
mean-RGBA at rest, at two samples into the state change and once settled
(two samples because a state change lands on the frame clock, so a slow
start must not read as "no change"). A second probe (scratch, `/tmp`)
tested keyframe replay on a real popover. Rest-state verification stays
on `tools/render-widget.c`.

### E6 — what GTK 4.22 animates (measured)

- **`var()` resolves inside the `transition` shorthand.** Both
  `transition: background-color var(--d) linear` and
  `transition: background-color var(--d) var(--e)` animated with the
  substituted values (probe: 9/255 vs 18/255 mid-flight, the second
  matching the house curve's front-load). The M2 whole-segment limit does
  not apply to single-value tokens inside a list — motion is tokenisable.
- **`@media (prefers-reduced-motion: reduce)` matches** (GTK 4.22
  `GtkCssProvider:prefers-reduced-motion` /
  `GtkSettings:gtk-interface-reduced-motion`, a separate axis from
  `gtk-enable-animations`), and a `:root` custom-property override inside
  the query propagates into `transition`. A transition declared *only*
  inside the query applies only then.
- **`gtk-enable-animations=false` already zeroes every transition** at the
  toolkit level. So the reduced-motion axis is ours to deliver.
- **No popover entry animation is possible.** A keyframe animation on
  `popover > contents` fires on the *first* map only — sampled mid-flight
  100 ms after the first `popup()` (green 27) and already at target 100 ms
  after the second (255). GTK binds animations to style computation, not
  to mapping, and the popover node survives popdown. GTK also has no exit
  animation mechanism (`@starting-style` does not exist here). Verdict:
  popovers keep popping instantly; a one-shot animation is worse than none.

### Defects found and fixed

1. **The overlay was silencing upstream's motion.** `transition` replaces
   a list, it never extends it — so every overlay rule that declared one
   dropped upstream's list for that node. Measured on a button:
   `:focus-visible` was already *landed* at the first 80 ms sample on the
   19 Sep sheet, while the fixed sheet reads rest 238 → 240,236,233 at
   80/160 ms → 240,235,232 settled (the 200 ms fade, still climbing at
   160 ms). Upstream animates the focus-ring trio
   (`outline-color`/`width`/`offset`, 200 ms, the same house curve) on
   buttons, rows, entries, switch, scale slider and sidebar buttons — the
   bare `button` atom carries it in 1:1.9.4-1. `ov-motion()` (L1)
   restates it on every declaration.
2. **`list.boxed-list` and popover menu rows had entry/exit inverted.**
   The resting rule carried 280 ms and `:hover` 200 ms, so the *entry* was
   the fast one — the opposite of the house spec, of its own comment, and
   of the later `_sidebar.scss`/`_button.scss`. Measured: the row wash
   landed by the 160 ms sample before (22 → 26), and is still climbing
   there after (21 → 24 → 26) — the 280 ms approach.
3. **The switch track's dish gradient snapped** at the toggle while the
   accent fill cross-faded over 180 ms. `background-image` is now in the
   switch list, at the switch duration.
4. **No reduced-motion path existed at all** — GTK's own animations
   toggle was the only escape.

### Decisions

- **Motion is tokenised in L0**: `--ov-motion-ease`,
  `-enter` (280 ms), `-exit` (200 ms), `-press` (120 ms),
  `-switch` (180 ms), `-ring` (upstream's own 200 ms). Values unchanged;
  what changed is that they live in one place and every declaration emits
  through `ov-motion()`, which is what makes the reduced-motion override
  structural instead of a rule per surface (the D5/D6 pattern).
- **Reduced motion ⇒ 0 ms, not a gentler duration.** Same state language,
  no animation: nothing teleports, nothing is hidden, no effect is
  removed — only the time spent getting there. This sheet animates no
  movement at all (every effect is a colour or shadow change), so
  duration is the only axis, and GTK's own `gtk-enable-animations=false`
  answers the same preference with exactly 0 ms. A third behaviour
  (shorter fades) would be indistinguishable from the platform one while
  being harder to reason about.
- **The restated ring follows the same override**: under reduce, nothing
  in this sheet animates, upstream's motion included. Tokenised as
  `--ov-motion-ring` precisely so it can be silenced with everything else
  instead of staying the one exception.
- **Not added: a transition on `switch > slider`.** The overlay paints the
  slider identically in every state (upstream's hover/active white and its
  disabled shadow are both overridden), so a transition there would
  animate nothing. Recorded because "the knob should fade too" is the
  obvious next thought.

### Verification

- Rest rendering **byte-identical** to the 19 Sep build for `button`,
  `box` (centred button) and `headerbar` — the change is timing, not paint.
- `tools/probe-motion build/gtk.css` (80 ms samples): hover 129 → 44 → 11,
  press 196 → 194, row entry 21 → 24 → 26, focus ring 238 → 240,236,233 →
  settled — every probe IN MOTION.
- Same probes with `REDUCE=1`: every state lands instantly at both
  samples; the 19 Sep sheet under the same flag still animates — the
  before/after proof for the reduce delivery. `NOANIM=1` lands everything
  instantly too.
- `tools/check-selectors`: contract OK against 1:1.9.4-1 (no selector
  changed).
- **Live pass (user, 23 Sep):** hover approach, list skim and the
  reduced-motion toggle all feel right — the eyeball half of the review,
  which no pixel probe can rule on.

### Found while reviewing (not motion, deliberately untouched)

- **Headerbar backdrop dimming is dead.** Upstream's
  `headerbar:backdrop { background-color: var(--headerbar-backdrop-color);
  transition: background-color 200ms ease-out; }` can never show, because
  the overlay's `background:` shorthand (priority 800) sets the colour in
  every state. The controls still dim (`windowhandle` filter is
  untouched); the surface does not. → BACKLOG H6.
- **`switch > slider:disabled`** keeps the overlay's full knob shadow:
  the overlay's resting rule outranks upstream's
  `switch > slider:disabled { box-shadow: 0 2px 4px transparent }`. →
  BACKLOG U7.
- **`ov-press()` (L1) is unused** by every surface — the button surface
  writes the same mechanism inline. Pre-existing; left alone.

## H6 — chrome backdrop recession — 23 Sep 2026

**Finding (measured, from the motion review).** Upstream's unfocused-window
signal was dead on every chrome surface the overlay paints:
`headerbar:backdrop { background-color: var(--headerbar-backdrop-color) }`
(and the searchbar/actionbar equivalents) can never show, because our rule
paints every state. Probe, headerbar mean-RGBA with the upstream sheet
loaded at 200: rest 233 → settled 233 (light), 232 → 232 (dark) — the
backdrop state changed nothing at all.

**Why the obvious fix is not a fix.** Re-stating `background-color` under
`:backdrop` would still show nothing: the bar material's second layer is an
*opaque* `color-mix()` gradient, so whatever is painted in the
`background-color` slot underneath it is invisible. The colour has to move
*inside* the gradient's stops.

**Implementation.** The gradient moved into `ov-bar-surface()` (L1) and its
base colour into `--ov-bar-base` (L0). A backdrop rule is then one
declaration:

    headerbar:backdrop, toolbarview > .top-bar:backdrop { --ov-bar-base: var(--ov-bar-backdrop-base); }

`--ov-bar-backdrop-base: var(--headerbar-backdrop-color)`, which upstream
defines as `@window_bg_color` — the chrome recedes into the content, on the
house transition (`background`, 200 ms, reduce-aware). Side effects, all
intended: the dark grain URI now comes from `--ov-texture-image` instead of
being respelled (the texture kill switch now works in dark mode too), and
the duplicated gradient stack in `_headerbar.scss`/`_toolbar.scss` collapsed
into one definition.

**Measured effect** (probe, upstream loaded, forced `:backdrop`):

| scheme | resting base | backdrop base | probe mean (rest → settled) |
| --- | --- | --- | --- |
| light | `#ffffff` | `#fafafb` | 233 → 232 — **below the noise floor** |
| dark | `#2e2e32` | `#222226` | 232 → 230 → 228, progressive (animating) |

So the trade is a whisper in light mode and a real (stock-Adwaita) dim in
dark mode, where the chrome stops being the brightest thing on an unfocused
window.

**Kill switch** (one edit, D5 pattern): set
`--ov-bar-backdrop-base: var(--headerbar-bg-color)` in `_tokens.scss` and
the bar stays lit in every state.

**Contract.** `--headerbar-backdrop-color` added to `variables.txt`;
`headerbar:backdrop`, `searchbar > revealer > box:backdrop`,
`actionbar > revealer > box:backdrop` and `toolbarview > .top-bar.raised:backdrop`
added to `selectors.txt`; guard passes against 1:1.9.4-1. Rest-state means
are unchanged (233/232 identical to the pre-change sheet).

**Verdict: KEPT** (user, live look 23 Sep 2026) — the dark-mode recession
reads right; light mode is unchanged in practice. BACKLOG H6 closed.

**Out of scope, same family.** `.sidebar-pane:backdrop` is also dead (our
`.sidebar-pane` is deliberately `transparent`), but restoring it would mean
an opaque colour where the translucency is a user decision from P1.5 —
a different trade, not taken here.

## Widget-family sweep — 23 Sep 2026

**Scope.** "Apply the recorded design language to all GTK4 widgets" — every
family the first pass (M0–M5) left outside the material system, judged on
real widgets through `gtk4-widget-factory` and `gtk4-demo`, tracked per
family by the new `tools/track`.

**Method.**
1. **Inventory from the sheet, not from widget names.** The pinned 1:1.9.4-1
   stylesheet was parsed rule by rule and every declaration of material
   (`box-shadow`, `background-image`, `background`, `background-color`,
   `filter`) enumerated per node family, in base, dark and HC. Coverage
   decisions are therefore stated in terms of upstream's own declarations.
2. **Fixes are written in the existing vocabulary** — L0 tokens, the L1
   functions (`ov-bevel`, `ov-well`, `ov-glow`, `ov-inset`, `ov-depth-*`,
   `ov-bar-surface`), house motion through `ov-motion()`. No new token was
   needed; `ov-depth-window()` (recorded in Evening 0, unused until now)
   finally has a surface: sheets.
3. **Contract.** Every new selector atom registered in
   `upstream/selectors.txt` (86 → 164 entries).
   `tools/check-selectors` passes against the installed 1:1.9.4-1.

### Defects this sweep found — all of them our own, all fixed

**1. The button material leaked onto upstream's flat families.** The guard
was `button:not(.flat):not(.osd)`, but upstream paints whole families flat
*without* the class: bar icon buttons (`headerbar`/`searchbar`/`actionbar`/
`.toolbar` + `.image-button`/`.arrow-button`/`.image-text-button`), the
wrapper-guarded `menubutton`/`splitbutton` children, `windowcontrols`,
`tabthumbnail`, `notebook` arrows, `columnview`/`treeview` headers,
`calendar` header, `infobar .close`, popover model buttons, spinbutton
arrows, pathbar crumbs, bottom-sheet actions. Those buttons were getting
the bevel, the accent glow and the pressed well — including `windowcontrols`,
which the proposal lists as out of scope.
*Fix:* the flat families are neutralised by property (`box-shadow`,
`background-image`, `filter`) in `_button.scss`, generated from two selector
lists. The reset selectors carry ≥8 classes, which is what makes one
non-state rule outrank the richest material rule (7 classes) at equal
priority — no `!important`, no per-state explosion. Upstream's own
transition list is restated on the same rule, so their washes still fade.
*Found later the same day, while rendering the hover register:* `button.link`
belongs on that list too. It is not flat by a `background: none` rule — it is
flat because upstream never gives it a surface at all (`button.link { color:
accent; text-decoration: underline }`), so a body-scan for "background:
none" could not find it. With the material on, a link rendered as a
full-width glow chip; `STATE=prelight build/render-gallery build/gtk.css
… buttons` shows the before/after, and `button.link` is now registered in the
contract.

**Verdict on the scope — KEPT (user, 23 Sep 2026).** The accent hover glow
was demonstrably *not* lost on opaque buttons (`hover` mean 228 → 19 under
the overlay, `Suggested`/`Destructive` bloom intact), but the earlier pass
took it off the families upstream paints flat. Asked explicitly whether to
restore it on bar icon buttons — glow only, or bevel+glow+well — the answer
was **keep it as it is**: the 19 Sep scope stands, bar icons and the other
flat families stay flat, and the neon register lives on opaque buttons and
coloured CTAs. The two alternatives stay rendered for reference
(`/tmp` scratch; regenerate with `STATE=prelight build/render-gallery …`).
Reopening it means deleting `$ov-flat-bar-contexts` from the loop in
`_button.scss` — the lever is named here so the decision is one edit wide.

**2. `button:drop(active)` was erased.** Upstream marks a drop target with
`box-shadow: inset 0 0 0 2px var(--accent-bg-color)`; the overlay's resting
rule owns `box-shadow` at priority 800, and priority decides before
specificity, so the accent ring never rendered on any non-flat button. Same
for `entry:drop(active)`, `spinbutton:drop(active)` and the generic
`:not(window):drop(active)` that `.card` relies on.
*Fix:* every material selector carries `:not(:drop(active))`, so the state
is handed back to upstream's own rule untouched. No restatement needed —
that is the point.

**3. `.card` lost its definition ring.** Our card ladder replaced the whole
`box-shadow` list, dropping upstream's first stop, `0 0 0 1px RGB(0 0 6/3%)`
— the 1px ring outside the card's edge.
*Fix:* the ring is restated ahead of the ladder and deliberately does **not**
ride `--ov-depth-color` (it is definition, not depth, so the depth kill
switch must not remove the edge).
*Measured* (gallery `lists` family, vertical profile through the card's top
edge at x=120, grey value of the boundary pixel, page 255): stock 225,
pre-fix card rule 206, fixed 193 — so the ring was really gone and is really
back, but the practical damage was smaller than it looked on paper: the
overlay's own translucent window surface (`--ov-surface-window`, 84% then —
96% since the retune of 23 Sep 2026) already separates a white card from the
page. Recorded as a real defect with a modest user impact, not as the
near-invisible card the source alone suggested.

**4. `switch > slider:disabled` kept a raised knob** (BACKLOG U7): our
resting slider rule outranked upstream's
`switch > slider:disabled { box-shadow: 0 2px 4px transparent }`, so a
disabled switch read as interactive.
*Fix:* disabled is flat — no drop, no insets, no gradient; the state stays
legible because the track keeps upstream's own dim.

**5. A checked switch lost its hover and press feedback.** Upstream
modulates `switch:checked` with a second `image()` layer on `:hover` and
`:active`; our dish gradient is declared for every state, so it swallowed
that layer entirely.
*Fix:* both states restated with upstream's own layer underneath our dish.

**6. `filter: none` on disabled buttons erased upstream's dim.** Several
families (every bar button family, `label`, `scale`, `switch`) dim with
`filter: opacity(...)`; a `filter: none` at priority 800 wiped it, so a
disabled icon button read as enabled.
*Fix:* the disabled rule now neutralises only `box-shadow` and
`background-image`. Nothing else of ours may declare `filter` outside the
press state.

**7. `spinbutton` never received the entry material.** The BACKLOG recorded
"the spinbutton's text area IS an entry node" as the reason U2 needed no
work. It is not: in GTK 4 a spinbutton's node is `spinbutton` with
`spinbutton > text`, a sibling of `entry`. Entries were recessed; every
spinbutton (GtkSpinButton, AdwSpinRow) stayed stock.
*Fix:* `_entry.scss` applies `ov-inset()` to both, because upstream gives
them identical bodies (`widgets/_entry.scss` vs `widgets/_spin-button.scss`).

### The high-contrast audit was passing for the wrong reason

Applying the language to the new families meant auditing their reverts, so
the 19 Sep "structural audit PASSED" claim was re-run **properly** this
time: parse the built sheet, take every rule that declares material
(`box-shadow` / `background-image` with a value other than `none`) outside
a `prefers-contrast` block, and require an HC rule whose selector is at
least as specific. The 19 Sep audit had only checked that HC blocks
existed, so it never noticed that a revert with *fewer* classes than the
rule it reverts loses: HC is the same provider at the same priority, and
priority ties are broken by specificity, then by source order.

Eight material declarations were surviving HC as a result:

| Declaration | Why the revert lost |
| --- | --- |
| `button:hover` glow | HC listed `…:hover` without the `:not(:active):not(:checked):not(:disabled)` tail (4 classes vs 7) |
| `button:active` well, `.keyboard-activating` | the press variant was not listed at all |
| `button.suggested-action`/`destructive-action` hover, active, checked | one revert sat at the end of the rule, covering only the base selector; nested states carry their own selectors |
| `entry:focus-within` deep scoop | `ov-inset()`'s revert covered the resting selector only |
| `scale:active > trough > slider` well | HC listed the resting knob only |
| `check`/`radio` microcavity | HC listed `check`/`radio` bare (0 classes) against a 2-class guard |
| `list.boxed-list` container scoop | no revert existed at all |
| `switch:checked:hover/:active` (added in this pass) | same trap, caught before shipping |

**Fix.** Every material rule now carries a revert whose selector matches it
character for character, and each revert restates what the material took:
the button HC list was rewritten per state, `ov-inset()` grew a
`:focus-within` sibling revert, the accent glass gained `ov-accent-solid()`
repeated through each state, and the boxed-list scoop and the
scale/check cases got their own. **Audit result: 0 material
declarations without a matching revert** (was 8).

Two further HC defects surfaced only once the reverts were *measured*
rather than read, both from reverting with `none` where a restatement was
owed:

- **`background-image: none` on the resting button erased a fill that the
  sheet underneath delivers as a gradient.** Measured with
  `tools/probe-motion` against the system GTK theme (a plain-GTK app:
  `gtk4-demo`, `gtk4-widget-factory`, and every non-libadwaita app):
  probed button mean `rgb(238,239,240)/α252` at rest → `rgb(15,16,15)/α40`
  under HC. libadwaita's own buttons carry **no** `background-image` at
  all — rest/hover/active are `background-color: color-mix(currentColor
  10% / 15% / 30%, transparent)` (verified in the pinned sheet) — so a
  libadwaita app never saw this. The revert now names only what the
  material declares: background-image is reverted on the hover state,
  which is the only state that sets one.
- **`box-shadow: none` erased upstream's own HC ring.** Under HC the ring
  `inset 0 0 0 1px color-mix(currentColor var(--border-opacity))` is the
  button's boundary; the revert now restates that expression, so the ring
  keeps following the palette.

### The harnesses were measuring the wrong baseline (found 23 Sep)

`tools/render-widget`, `tools/probe-motion` and the new
`tools/render-gallery` all load one CSS file at priority 800 — but GTK
*also* loads `$XDG_CONFIG_HOME/gtk-4.0/gtk.css` for every process, and on
a machine that has installed this overlay that path is a symlink to the
sheet under test. Every "stock" run therefore already carried the overlay:
proved by `md5sum` on two gallery runs, `none` versus `build/gtk.css`,
which were byte-identical (`672472ad907272f21cc92b090e29981f` for the
headerbar family). The A/B was measuring one file twice.

All three harnesses now point `XDG_CONFIG_HOME` at a private empty
directory before `gtk_init()`, so the CSS arguments are the only
stylesheets in the process; `KEEP_CONFIG=1` restores the user environment.
After the fix the same pair reports 15/15 families changed
(`tools/gallery-diff`, light scheme). libadwaita's own stylesheet is
unaffected — it arrives from the theme search path, not from the user
config.

### The language applied to the families the first pass missed

| Family | Change | File |
| --- | --- | --- |
| Content cells — bare `row.activatable`, `flowbox > flowboxchild`, `gridview > child.activatable`, `popover.menu list > row` / `listview > row` | House timing only (280 in / 200 out / 120 press). Upstream's alphas and the accent selection pair are untouched: the recorded system-wide rollout was explicitly "timing only, zero material alterations". | `_cells.scss` |
| Notebook tabs | House timing only, on upstream's own wash. The U6 deferral stands: no material added. | `_notebook.scss` |
| `expander-widget` titles | The row wash (5% / 9%) **added**, with house timing — the one place material was added rather than re-timed, because upstream's only feedback is the arrow's opacity and the title is an activatable row. | `_expander.scss` |
| `calendar > grid > label` | House timing on the pointer wash (`:checked`). Selection semantics (`:selected` accent) untouched. | `_calendar.scss` |
| `bottom-sheet > sheet`, `floating-sheet > sheet` | The window rung of the ladder (`ov-depth-window()`) — the first surface on the rung recorded in Evening 0 for "windows". Follows `--ov-depth-color`, so the depth kill switch reaches sheets. Upstream's `outline` hairline stays. | `_sheet.scss` |

### Reviewed and deliberately NOT changed (evidence first)

- **`.view` and `textview > text` — the container scoop was tried and
  reverted.** The only node carrying upstream's view fill is content-sized,
  so a `background-image` there scrolls with the content instead of sitting
  in the viewport. Probed (scratch `probe-view.c` / `probe-textview.c`,
  red→blue gradient on the node, 300×200 viewport, two scroll positions):
  textview content 7218 px — scroll 0 renders the top band `252,0,0` and
  scroll 1 `6,0,245`; treeview `.view` content 8400 px — the middle band
  moves `51,0,201 → 170,0,79` between the two positions. A shade that moves
  while scrolling is the exact failure the E4 texture verdict rejects
  ("no shimmer while scrolling"), so the file was deleted rather than
  shipped. GtkTextView and view bodies keep upstream's flat fill.
- **Tooltip.** Upstream sets `tooltip { box-shadow: none }` deliberately —
  tooltips are flat dark bubbles. Putting them on the overlay rung would
  add depth upstream removed on purpose.
- **Keycaps** (`shortcut > .keycap`, `shortcut-label .keycap`). Already the
  house language: `inset 0 -2px var(--card-shade-color)`, i.e. shade on the
  bottom edge from an upstream token.
- **Paned separators and scroll undershoots.** Already hairlines and
  alpha-shade gradients in the same idiom as ours
  (`color-mix(currentColor var(--border-opacity))`,
  `color-mix(var(--shade-color) 75%)`). Restating them would be churn.
- **AdwTabBar / AdwViewSwitcher / tabthumbnails.** The U6 deferral stands.
  The only rule of ours that reaches them is the flat reset, which *removes*
  our material from `tabthumbnail button`.
- **GtkCalendar — harness verdict taken, live verdict impossible here.**
  No demo page shows one (`gtk4-demo --list`, 112 examples; the widget
  factory's own set — neither carries a calendar), so the family was judged
  in `tools/render-gallery`'s `calendar` family instead: stock vs overlay at
  rest 75785/129600 changed pixels (6.65 mean, all of it window and bar
  surface — the calendar node itself is untouched at rest), and
  `STATE=prelight` shows the header arrows taking upstream's own hover wash
  with our timing on it. Light, dark and HC renders all read clean, day
  numerics, the `:selected` accent and the `today` underline included.
  A live witness does not exist on this machine: `/usr/bin` and `/usr/lib`
  were scanned for `gtk_calendar_new` / `GtkCalendarPopover` and the only
  carriers are `telegram-desktop`, `yad`, `gtk4-icon-editor`, libgtk and
  libwebkit2gtk — no GNOME surface. Verdict: **no regression, timing
  accepted on harness evidence**; reopen if an app that shows one enters
  daily use (BACKLOG W11 closed with this reason).
- **`upstream/selectors.txt` header.** The 12 comment lines of its header were
  in a shuffled order (pre-existing — the same order is in HEAD, so it came
  in with an early commit): the contract's own prose read backwards and in
  fragments. **Fixed 23 Sep 2026:** the fragments were reordered into the
  intended reading order and checked word-for-word against `docs/proposal.md`
  ("Rules" section, hard rule 2), so no text was added, removed or reworded —
  `sorted(header) == sorted(HEAD header)` and `tools/check-selectors` still
  passes. The one ambiguous fragment ("silently") was placed per the
  proposal's own sentence: "An unregistered dependency is invisible to the
  upgrade guard and will break silently."

## Gallery — 15 families, stock vs overlay

`tools/render-gallery` (new, 23 Sep) renders one window per family offscreen
with the same provider mechanism as the rest of the toolchain and writes one
TIFF per family; `tools/gallery-diff` reports `changed_px/total_px`,
`mean_abs_delta` and `max_delta`. Light scheme, `build/gtk.css` vs no
provider at all (both runs hermetic — see the baseline note above):

| Family | changed/total | mean Δ | max Δ |
| --- | --- | --- | --- |
| adw (toolbarview, banner, tabbar, status page) | 340409/396800 | 9.45 | 40 |
| buttons | 297510/298080 | 14.67 | 255 |
| calendar | 75785/129600 | 6.65 | 40 |
| cells (flowbox, gridview) | 100227/316960 | 3.63 | 40 |
| columns (columnview + headers) | 112681/210800 | 6.12 | 40 |
| controls (switch, scale, progress, level, scrollbar) | 305877/312000 | 10.86 | 62 |
| dnd (drop-active button + entry) | 174815/176800 | 11.36 | 57 |
| entries | 278633/279360 | 12.45 | 63 |
| expander | 153173/153600 | 11.14 | 40 |
| headerbar | 111415/112000 | 11.74 | 40 |
| lists (listbox, boxed-list, card) | 193842/291200 | 7.30 | 40 |
| notebook | 219860/228800 | 10.82 | 40 |
| popover | 198143/218400 | 10.48 | 40 |
| spinbutton | 146786/147200 | 12.55 | 63 |
| textview | 158782/239200 | 7.41 | 40 |

15/15 families changed in every scheme (light, dark, HC). Two readings to
keep honest: the delta proves the overlay *reaches* a family, not that the
result is right — that is what the image review and the live pass are for;
and under HC the remaining delta is the *surface* set (translucent window
and panes), which is deliberately not reverted, because a surface tint is
not a material effect.

### Verification

- `tools/check-selectors`: contract OK against the installed 1:1.9.4-1 —
  169 entries, both axes (86 → 169; every new atom taken from the pinned
  sheet, not invented, `button.link` and the four channel-fill atoms last).
- `tools/build`: compiles, 861 lines, `!important` count 0, raw hex
  count 0.
- Structural HC audit (script above): 0 material declarations without a
  matching revert (53 material selectors in the final sheet).
- Motion matrix (`tools/probe-motion`, 80 ms samples, upstream sheet
  loaded, hermetic baseline): normal, `SCHEME=dark`, `CONTRAST=more`,
  `REDUCE=1` and `NOANIM=1` all report the expected verdicts — the press
  well, the row wash and the focus ring are IN MOTION in the animated
  runs, and every probed state lands instantly under `REDUCE=1` and
  `NOANIM=1` (the reduce/no-animation runs were repeated *with* the
  upstream sheet at the end of the session: without it the headerbar row
  is a no-op, because `--headerbar-bg-color` is undefined and the whole
  background declaration is invalid at computed-value time — the README
  warns about exactly this, and the first pass of this matrix skipped it).
  The hover glow lands inside the first sample in both schemes, which is
  the state the 23 Sep motion review recorded (a background-image swap,
  not a fade).
- Gallery (`tools/render-gallery`, 15 families, stock vs overlay, diffed
  by `tools/gallery-diff`) re-run on the final sheet: 15/15 families
  changed in light (largest: adw, 340409/396800), dark (338378) and HC
  (319485) — numbers below.
- Contrast of every pair this pass touches, computed from the palette the
  pinned sheet defines (translucent colours composited over the surface
  they render on, not estimated):

  | Pair | Light | Dark |
  | --- | --- | --- |
  | body text on window | 12.22:1 | 15.85:1 |
  | text on a row wash at hover (5%) | 11.20:1 | 13.71:1 |
  | text on a row wash at press (9%) | 10.42:1 | 12.06:1 |
  | text on the check/radio cavity at press (30%) | 6.87:1 | 5.87:1 |
  | destructive label on the HC solid fill | 4.83:1 | 6.11:1 |

  Every pair clears 4.5:1; the tightest is the destructive label on its
  solid fill, which is upstream's own pair (`--destructive-bg-color` /
  `--accent-fg-color`) and unchanged by this pass. The row/expander washes
  move the text contrast by at most 1.5 points, so the new expander wash
  costs nothing legible.
- Visual: the `headerbar`, `columns`, `lists` and `spinbutton` families
  were read as images in both runs. The bar's icon buttons and the column
  headers are indistinguishable from stock (the flat-family reset), the
  card edge is present in both, and the spinbutton carries the new inset
  while its up/down arrows stay flat.
- Live pass: `tools/track <family>` opens the demo page for each family;
  `tools/track <family> -i` opens the same page under GTK Inspector. The
  user's eyeball verdict rides with daily driving (M7).

## Lit channel fills — 23 Sep 2026

**Request.** "Progress bar is still looking rather flat (the coloured bits)."

**Diagnosis.** Correct, and by construction: upstream paints every filled
channel — `progressbar > trough > progress`, `scale > trough > highlight`
(one shared rule upstream, so one shared material here) and
`levelbar > trough > block` — with a flat `background-color` (accent,
`--warning-bg-color`, `--success-bg-color`) and nothing else. Ours only
recessed the trough around it, so the fill sat in a groove as a sticker.

**Decision.** A new L1 register, `ov-lit-fill()`, and **the colour stays
opaque underneath it**. Two reasons: a progress bar's colour is data, and a
levelbar's low/high/full distinction is meaning — diluting either to make
glass would trade information for shine. So the lighting is layered on top
(one light source, top): a 3-stop top-light gradient plus a 1px lit lip and
shadowed foot, in white/black physics constants only, exactly like
`ov-raised()` and `ov-glow()`. No kill switch of its own (the glow has
none either); the HC revert is the escape hatch, and it flattens the fill to
upstream's solid colour.

Values are the CTA glass's own curve (white 28% → 6% at 42% → black 10%),
compressed for a 4-12px bar and tuned by measurement: the first pass
(white 22% / black 10%) moved the top row from `rgb(53,132,228)` to
`rgb(146,189,241)` — one lit pixel out of four, still flat to the eye at 1×.
The shipped pair (30%, 8% at 45%, black 14%; insets white 55% / black 22%):

| node | before (every row) | after (top → foot) |
| --- | --- | --- |
| `scale > trough > highlight` | `rgb(53,132,228)` | `rgb(186,213,246)` → `rgb(41,95,161)` |
| `progressbar > trough > progress` | `rgb(53,132,228)` | `rgb(189,214,246)` → `rgb(39,92,158)` |
| `levelbar > trough > block` | `rgb(53,132,228)` | `rgb(189,214,246)` → `rgb(39,92,158)` |

(measured in the gallery `controls` family; each selector was first proved to
hit a painted node by painting it a flat probe colour and locating the
pixels, the same technique the textview question used.)

**Scope.** `progressbar.osd` and an empty trough are excluded, because
upstream unsets the fill there (`progressbar > trough.empty > progress { all:
unset }`). Motion: upstream animates `background` and `box-shadow` on these
nodes; both are restated through `ov-motion()`, plus `background-image`
(ours), so the colour fade keeps the house exit timing.

**Defect found in passing.** The channel's HC revert set `box-shadow: none`,
which erased upstream's own 1px HC ring on `scale > trough`,
`progressbar > trough` and `levelbar > trough > block.empty` — the third
instance of the same trap (buttons, card, now channels). The ring is
restated instead. Only the ring: we never touch the channel's
`background-color`, so upstream's HC value applies untouched.

**Contract.** +4 atoms (`scale > trough > highlight`,
`progressbar > trough > progress`, `progressbar > trough.empty > progress`,
`levelbar > trough > block:not(.empty)`), 165 → 169; guard passes.

**Verdict: KEPT** (user, 23 Sep 2026) — "nice and subtle, approved". The
register ships as measured above; the tuning lever stays `ov-lit-fill()` in
`_primitives.scss`, one edit wide, with the variant numbers on record if it
ever wants to be stronger (the bevel-only and 42%-top variants were rejected
on measurement, not on taste).

**Verification.** Gallery `controls` in light and dark (identical output —
the accent is scheme-independent, so the dome is too), HC flat at
`rgb(53,132,228)` on every row, contract OK, sheet parses, 861 lines, 0
`!important`, 0 raw hex. Live verdict rides with the user's eye:
`tools/track factory` (progress bar) and `tools/track style-classes`.
Rejected on measurement: the bevel-only variant (no gradient, hairlines
only) — crisp but it loses the dome — and a 42%-top gradient variant, which
dipped *below* the base colour through the middle.

## Button review — glass retired, ring thinned — 23 Sep 2026

Trigger (user): *"I'm not sold on the buttons… use the appropriate design
skills to review their implementation"*, then *"the 'glass' effect on the
buttons is not really very polished"*, and *"use thinner borders on
outlined buttons"* — clarified to mean the keyboard focus ring.

**Method.** Design-skill review (better-interface, routed through better-ui /
better-colors / better-accessibility) run against the *rendered* surface, not
the source: gallery `buttons` family, stock vs overlay, five states ×
light/dark/HC, the CTA block at 2×, plus a numeric pass over each label pair,
the focus-ring pixel profile, and the pixels *between* two adjacent CTAs.

### 1. The accent glass is retired (was "Accent luminous glass", 19 Sep)

Label foreground against the fill it actually renders on. 4.5:1 is the
requirement for a 13px bold label; 3:1 is the large-text floor.

| state | suggested stock | suggested glass | destructive stock | destructive glass |
| --- | --- | --- | --- | --- |
| rest  | 3.77 | **2.00** | 4.60 | **2.58** |
| hover | 3.27 | 2.27     | 4.15 | 3.12     |
| press | 5.46 | 3.12     | 3.06 | 4.20     |

One root cause — one lighting recipe painted over two different upstream
materials — produced three defects:

- **The 55% translucent fill is a function of the backdrop.** On the light
  scheme's near-white window it leaves a pale ghost (2.00:1, *below* even the
  3:1 floor); the same 55% over the dark window measures 5.44:1. A material
  whose label contrast swings with whatever sits behind it cannot be called
  polished.
- **`.destructive-action` is not a filled variant upstream.** It remaps
  `--accent-*` and paints a **15% `currentColor` container** with its own hue
  as the label, plus 20/35/45% washes on hover/active/checked (gtk.css
  L318-328). The glass's blanket `color: --accent-fg-color` turned that label
  white on a saturated red slab: an emphasis inversion (the destructive CTA
  out-shouting the accent one) *and* a 2.58:1 pair.
- **The hover/press outer bloom** (`0 4px 14px -2px` / `0 12px 28px -6px`)
  breaks the same-surface law stated in `_primitives.scss` ("buttons never
  do"), and painted into the 10px gap between two CTAs: one pixel between
  them, both prelight, moved 249,249,250 → 200,211,236 in light and
  33,33,37 → 44,57,85 in dark. It had never been decided — no entry in this
  file, only incidental mentions.

**Decision.** `.suggested-action` wears the house lit fill instead:
upstream's own **opaque** `--accent-bg-color` under `ov-lit-curve()` at button
scale, with the mid stop at **zero alpha** so the band the label sits in
keeps the base colour (the channel curve's white-8% mid cost 0.6 of contrast,
3.77 → 3.14). Upstream's own hover lift (`image(currentColor 10%)`), press
darkening (`image(RGB(0 0 6/20%))`) and held darkening
(`image(RGB(0 0 6/15%))`) are kept as the top layer, because our resting
declaration replaces their `background-image`; press and held then sink into
the same `ov-well()` / `ov-well-held()` every other button wears.

`.destructive-action` gets **no CTA material at all** and falls through to the
generic button material. That restores upstream's container, and re-hues the
house hover glow for free — upstream remaps `--accent-color` to the
destructive hue on that node (L318) and `ov-glow()` reads exactly that
variable.

| light | stock | glass | now |
| --- | --- | --- | --- |
| suggested rest / hover / press / held | 3.77 / 3.27 / 5.46 / 4.99 | 2.00 / 2.27 / 3.12 / 3.12 | 3.95 / 3.42 / 5.58 / 5.18 |
| destructive rest / hover / press / held | 4.60 / 4.15 / 3.06 / 3.06 | 2.58 / 3.12 / 4.20 / 4.20 | 4.35 / 3.46 / 2.93 / 2.78 |

Suggested is now at or *above* upstream on every state. Destructive's
hover/press/held sit 0.2-0.7 below stock because the house glow and well are
*added* to a container whose own upstream pairs are already 3.06; that trade
— the house hover language on every button versus 0.7 of label contrast on
one state of one variant — is recorded as an open item (BACKLOG B3), not
hidden. HC: `ov-lit-fill()`'s own revert hands the CTA back to upstream's flat
fill (suggested rest = exactly stock, 3.77).

### 2. Focus ring: 2px → 1.5px

User decision. Upstream's ring is 2px of `accent 50%` (gtk.css L240 — the
button focus rule whose selector list ends in the bare `button` atom — plus
per-family re-sets in the bar / CTA / flat rules). **One** rule at priority
800 (`src/surfaces/_button.scss`) sets
`outline-width: var(--ov-focus-ring-width)` for every button family at once,
flat families included, so a bar icon button and its opaque sibling cannot
drift apart. New kill-switch token `--ov-focus-ring-width` (`1.5px`; `2px`
restores upstream). Colour, per-family offset and upstream's own outline
transition are untouched, so the ring still fades in at `--ov-motion-ring`.

Rendered profile at 1×: 2px covers two device rows solid (117,163,210 twice,
light scheme); 1.5px covers one solid row plus one half-intensity outer row
(170,193,218 then 106,156,206) — the ring keeps its edge anchoring and loses a
quarter of its ink. HC restores 2px (verified: every width variant renders
92,141,192 + 106,156,206 under `CONTRAST=more`).

Known trade, stated rather than buried: the focus-appearance floor is a 2px
perimeter, and stock's 2px at 50% alpha is already only ~1px of solid ink;
1.5px keeps ~0.75px-equivalent. The same one token is the lever for a crisper
hairline at fractional scale (`1px`) and for the floor (`2px`).

**Contract.** +1 atom (`button:focus:focus-visible`), 169 → 170; guard passes
against the installed sheet and the pinned archive.

### Verification

- **Blast radius.** Gallery, old sheet vs new sheet, eight families:
  `buttons` changed (30271 px — the intended restyle), `controls`,
  `notebook`, `lists`, `popover`, `adw`, `headerbar`, `textview`
  **pixel-identical** (the `ov-lit-curve()` refactor is neutral).
- **Gallery `buttons`.** stock vs overlay × rest / hover / press / held /
  focus-visible × light / dark × normal / HC; every label pair measured as
  tabulated above; the gap pixel between the two CTAs is identical at rest
  and at prelight in both schemes (bloom gone).
- `tools/check-selectors`: OK against installed `1:1.9.4-1` and against the
  pinned archive.
- **Not verified:** a checked *or* destructive CTA in a live app (no gallery
  member wears both flags), and the ring at 1.25×/1.5× scale (X5 card).

### Review findings not acted on

Recorded in BACKLOG under "Button review" (B3-B7): destructive hover/press
contrast cost, the 280ms hover entry on a high-frequency control, the
`:disabled` `background-image` reset on plain-GTK buttons, the asymmetric
bevel pair in the light scheme, and checked-toggle press feedback.

## Window translucency: 84% → 96% — 23 Sep 2026

**Report.** A window over another window (the Extensions app over a browser):
the page's own body text read through the window's content, in the space
where the app paints nothing (list gaps, page padding — most of a plain
window). Not a new rule: the migrated P1.5 alpha.

**Cause.** `--ov-surface-window` (L0) mixes the window colour 84% with
transparent, i.e. 16% of whatever is behind the window reaches the screen.
On a light page over a dark text run that is ~38/255 of contrast — the eye
resolves that as text, not as tint.

**Measured** (gallery `headerbar`, the family that renders a real window,
bare-surface pixels). Surface alpha 215/255 = 0.843 — the token exactly; the
header bar over it stays opaque (255). Composited over #0f1419 text on a
#ffffff page: the ghost reads 213/255 against a 250/255 surface, 37/255 of
contrast. Same in dark (rgb(33,33,37) body).

**Decision.** 96% — one number in L0. Bleed drops to 4% and ghost contrast
to 9/255 (~3.5%, under the legibility floor), while the surface still takes
the backdrop's cast: the translucency survives as a tint. `dialog` rides
the same token; `.content-pane` already derives opaque and `.sidebar-pane`
is transparent, so both follow the window and neither needed an edit. No
contrast variant is added: HC must not remove surface definition, and this
direction is toward opaque anyway.

**Verification.** render-gallery `headerbar`, light and dark: surface alpha
245/255 in both, bar unchanged at 255 (`/tmp/ov-before`, `/tmp/ov-after`,
`/tmp/ov-after-dark`). `tools/gallery-diff` old→new: `headerbar`
86188/112000 changed, mean 6.11, max 30 (the alpha channel is compared).
`tools/build` reinstalled the sheet and restarted the two service daemons,
so windows opened from here get it. **Not verified:** a live window over
another window after the rebuild — the compositor path is the one the
report itself exercised at 84%, and the render-node alpha is the only
thing that changed.

## Foreign apps: Helium came up black — 25 Sep 2026

**Report.** Helium 0.18.1.1 (Chromium 154, `imputnet/helium`) with its default
`extensions.theme.system_theme = 1` (`ui::SystemTheme::kGtk`, i.e. "follow the
GTK theme") opened with a black tab strip, a black toolbar and a black
viewport: a completely dark browser window in a light session. Helium is a
plain GTK4 app — it never loads libadwaita's stylesheet, so none of our
`--ov-*` tokens derived from libadwaita custom properties resolve in it.

**Cause.** A libadwaita custom property exists only when libadwaita's sheet is
loaded. In an app that never calls `adw_init()` the reference is undefined, and
GTK does **not** fall back to the theme's own colour for that property: the
declaration computes to nothing and the node paints no background at all. So
`window { background-color: var(--ov-surface-window) }` and the bar material
(which removes upstream's gradient and repaints from `var(--headerbar-bg-color)`)
both rendered as nothing.

Chromium turns "nothing" into black. It builds its Linux palette out of
*rendered* GTK nodes — `ui/gtk/gtk_util.cc` `GetBgColor()` renders the node
into a 24x24 cairo surface and averages it, `ui/gtk/gtk_color_mixers.cc`
consumes the result — and forces the frame opaque:

```c
frame_color = SkColorSetA(GetBgColor("headerbar.header-bar.titlebar"),
                          SK_AlphaOPAQUE);
```

An empty render averages to `0x00000000` (`a == 0` in `GetAveragePixelValue`),
`SkColorSetA(..., 255)` makes it `#ff000000`, and `kColorPrimaryBackground`
(averaged `window.background`) stays transparent: black frame, black toolbar,
black viewport.

**Measured — `tools/probe-foreign` (new, offline, exits 1 on any input that
paints nothing), same selectors and averaging as `gtk_color_mixers.cc`:**

| input | pre-fix sheet | fixed sheet | stock GTK |
| --- | --- | --- | --- |
| `GetBgColor("")` (primary bg) | `#00000000` | `#f5f6f5f4` | `#fff6f5f4` |
| `opaque(bg(headerbar))` (frame) | `#ff000000` | `#ffe4e3e2` | `#ffdddad6` |
| `bg("") over frame` (toolbar) | `#ff000000` | `#fff5f4f3` | `#fff6f5f4` |
| inputs that paint nothing | 9 of 31 | 0 | 0 |

**Live A/B in the running session** (second Helium instance, `--gtk-version=4`,
`system_theme=1`, fresh profile, same 900x600 window, captured per window;
sheet swapped by `XDG_CONFIG_HOME`): window pixels classified as pure black —
stock 36367, pre-fix 79598, fixed 36386; the top chrome rows (y=10-19) read
`#eeedeb` / `#000000` / `#ededec`, i.e. the fix is stock-identical, not merely
"less black".

**Decision.** L0 gains an explicit upstream-alias layer: **one `--ov-up-*`
token per libadwaita custom property the sheet reads**, each carrying the
fallback that keeps the sheet painting when libadwaita is absent — GTK's own
built-in named colours (`@theme_bg_color`, `@theme_base_color`,
`@theme_selected_bg_color`, `@accent_color`), which are the same palette
libadwaita's variables alias and track the scheme for free (Default-light /
Default-dark define the same names — dark reads `#353535` window / `#3e3e3e`
bar). `black` and `white` appear only as the physics constants for `--dark-5`
and `--light-1`; `--ov-up-border-opacity` keeps the `100%` fallback the sheet
already used, and `--ov-up-slider-border` keeps `currentColor`. Surfaces and
primitives now reference the aliases and nothing else, so the sheet can no
longer compute a colour to nothing in *any* GTK4 app, and
`upstream/variables.txt` finally lists all ten variables it depends on
(`--headerbar-bg-color`, `--accent-color`, `--accent-bg-color`,
`--view-bg-color`, `--border-opacity` and `---slider-border-color` were
missing from the contract).

**Consequence, deliberate.** A foreign app now wears the overlay's material in
the stock palette rather than stock GTK (frame `#ffe4e3e2` vs stock
`#ffdddad6`); CSS cannot ask whether libadwaita is loaded, and painting our
material with sane colours is the only alternative to painting nothing.
Helium's own appearance setting (Classic) sidesteps GTK colours entirely for
anyone who wants the browser untouched.

**Verification.** `tools/probe-foreign`: 9 → 0 inputs painting nothing, light
and dark, plus the live-session A/B above. `tools/gallery-diff` on
`render-gallery` output, old sheet vs new, **light and dark: 15/15 families
pixel-identical, 0 changed** (the alias layer is a pure rename inside
libadwaita apps). `tools/check-selectors`: contract OK against installed
`1:1.9.4-1`. **Not verified:** a dark-scheme live Helium window (only the
probe covers dark), and the browser after the sheet was already loaded — GTK
reads user CSS at process start, so the running Helium instance needs a
restart to pick this up.

## Flat register — 25 Sep 2026

**Ask.** "Our current 'flat' variation button is ALL FLAT" (user): the flat rung
should read as ours without becoming a chip. Run as a `variant` pass — three
candidates on one axis, *what carries the register's presence at rest* —
judged in the gallery, so the decision came from pixels rather than taste.

**What the sheet did before.** Nothing at all to `.flat`: the material's guard
is `button:not(.flat)` and the reset's guard is `:not(.flat)` too, so a `.flat`
button was upstream's own look (`background: transparent; box-shadow: none`,
gtk.css L338) plus upstream's washes and rings — while the families upstream
paints flat *without* the class were hard-zeroed by the 23 Sep reset. Two
registers sharing one name.

**Candidates.** `A` contour (0.5px `currentColor` ring), `B` sheen (lit top →
shade foot, no edge), `C` bevel-lite (the house pair at reduced amplitude over
a whisper sheen). Peaks against the sheet before them, light/dark: **A 7/7,
B 9/16, C 12/16**, where the opaque rest bevel measures **+1/−10 (light)** and
**+17/−3 (dark)** on the same page.

**A rejected.** Scheme-symmetric and clean, but a closed contour has no light
direction — it breaks the one-light-source rule and reads as a drawn outline
(a bordered field), not as material.

**The light-scheme finding.** The physics constants are not scheme-symmetric:
white over the light scheme's near-white page moves it by **+1/255** (measured
on the opaque button's own top hairline), black over the dark bar by about
**−2/255**. In light only a candidate's *shade* half can act; in dark only its
*highlight* half. That is why B and C measured 9 and 12 in light against 16 in
dark, and why they read "almost invisible in light" (user). Both then gained
per-scheme values, calibrated to **equal measured presence: 17/255 light,
16/255 dark** — matching a fill-less rung to the weight an opaque button gets
from its 19/255 fill plus its 10/255 foot line.

**Verdict (user, 25 Sep 2026): C, with B kept as a valid variation.** C is the
house's own material one notch down in both schemes — light carries it on the
bottom rule (how the opaque buttons themselves read in light, B6), dark on the
top rule — so it adds no second lighting language. At equal peak the two
differ in *distribution*, not amplitude: across the button's lower third B
spreads 11.8/255 of shade against C's 4.1/255, because a rule with no edge has
to ride its whole presence on the gradient, and in light what that ink does is
imitate a shadow under the button (the register the house bans on buttons). B
survives as a *token* variation of the same rule — point
`--ov-flat-edge-{top,bottom}` at `transparent` and raise `--ov-flat-shade` —
rather than as dead code.

**Promoted.** L1 `ov-flat-register()`, called from the `.flat` variation block
at the bottom of `surfaces/_button.scss`; four L0 tokens
(`--ov-flat-edge-top`, `--ov-flat-edge-bottom`, `--ov-flat-hilite`,
`--ov-flat-shade`, the last two re-pointed under `prefers-color-scheme: dark`).
The three `.flat`-parent bridges (`menubutton.flat`, `splitbutton.flat`) moved
out of `$ov-flat-structural` into the variation: same register, and the reset's
8-class selector would otherwise outrank the variation's 7-class one.

**The HC mechanism, deliberately different from every other primitive.** This
one declares its material inside `(prefers-contrast: no-preference)` instead of
emitting a `prefers-contrast: more` revert, because upstream's `button.flat`
owns `box-shadow` in its own states: `none` at rest, a `currentColor` ring on
hover / active / checked, 50% of it under HC. A revert block could only restate
`none` — which erases those rings — so the register declines to participate in
HC at all and hands the node back to upstream in every state. Measured: the
ring does not render on the gallery's flat button even in stock HC + prelight
(`--border-opacity` reaches a `color-mix()` whose node has nothing to mix), so
this is belt and braces rather than a visible repair.

**What is NOT touched.** The implicit flat families keep the 23 Sep reset
(BACKLOG W1): `button.link`, window controls, spinbutton arrows, pathbar
crumbs, popover model buttons, tab thumbnails, notebook arrows, calendar and
column/tree headers, infobar close, bottom-sheet actions. The flat rung's
press / held / checked states keep upstream's washes and rings.

**Verification.** `promoted.py` cells, every one noise-filtered: rest ×
{light, dark} across all 15 families; HC rest, hover and HC hover ×
{light, dark} across the four families that carry `.flat` nodes. Result: the
register moves *only* `.flat` nodes — `buttons` peak 17 (light) / 16 (dark),
`headerbar` 16/13, `adw` 17/12 — and 12 of 15 families are pixel-identical to
the sheet before it; HC is 0 px in all four cells. The matched-node set from a
solid-red probe equals the noise-filtered diff, so the scope claim is
geometric, not statistical. `tools/check-selectors`: contract OK against
installed `1:1.9.4-1`. `tools/probe-motion build/gtk.css`: hover and press
INSTANT (state landed), row hover and focus-visible IN MOTION — unchanged from
before the rule. The register's transition list is byte-for-byte the set
upstream gives those nodes (`outline-*` + `background` + `box-shadow`, 200ms on
the same curve), so it silences no upstream motion.

**Method note, worth keeping.** `render-gallery` is not run-to-run
deterministic: two renders of *one* sheet differ by up to ~19k px of text
antialiasing in `buttons`, `lists`, `columns` and `notebook` (and by 0 in the
shapes-only families). Every number above is measured against that floor — a
per-cell noise mask subtracted from the A/B diff — because without it the noise
alone reads as a 15-20/255 "change" on a label glyph. Any future gallery
comparison should do the same, or report `changed_px` as meaningless below the
floor.

**Not verified.** The live eye over a populated bar in daily apps (the user's
sweep is the gate, as always); fractional scale (X5 card); a disabled `.flat`
button — the guard is `:not(:disabled)` and upstream's `filter: opacity(30%)`
dim is never declared by us, but the gallery carries no disabled `.flat`
button, so that one is structural rather than measured.

## Pen dial pass: the lit edge — 26 Sep 2026

**Ask.** "The goal is to get closer to this pen's looks as possible within GTK4
constraints" (user, 26 Sep), after a study of LukyVj's *Futuristic Dial Button*
(codepen `xxyEYMJ`). The pen's register, extracted from its 484-line SCSS and
measured live: one object, one light model, one seed number. Its **lever** is
the part worth porting — a 4%×14% bar whose bevel pair is built from the
accent's own hue (`inset 0 1px 2px hsl(h 100% 72%)` over `inset 0 -1px 2px
hsl(h 98% 61%)`) plus a two-stop bloom (`0 0 4px` / `0 0 16px`), so the part
reads as *emitting* light rather than as painted with a white highlight. The
seed idea behind it — re-hue the whole part from one number on engage/hover —
is not portable: GTK has no `@property` (`Theme parser error: Unknown @ rule`,
measured) and no `:has()` (`Unknown pseudoclass`), so no custom property
animates and no descendant state can re-hue an ancestor.

**What moved.** The two parts in this sheet that *are* moved by the user —
`switch > slider` and `scale > trough > slider` — take the accent into their
bevel pair when they are engaged:

- `switch:checked > slider` — the lit pair plus the 2px halo, over the
  unchanged drop shadow.
- `scale:hover > trough > slider` — the lit pair plus the halo: the pen's
  lever-hover, which turns the knob accent-coloured the moment the pointer is
  on it.
- `scale:active > trough > slider` keeps the house press scoop and gains only
  the halo. An accent-lit *top* edge fights the scoop's dark top, and press is
  a receding read, not a lit one.

L0 gains `--ov-lit-edge-top` / `--ov-lit-edge-bottom` / `--ov-lit-bloom`
(scheme-split), L1 gains `ov-lit-edge()` and `ov-lit-halo()`. `color-mix()` in
srgb cannot hold a hue's saturation the way the pen's `hsl()` stops do, so
these are the closest srgb rungs to the pen's two — the price of the platform,
recorded rather than hidden.

**A 19 Sep decision reversed, narrowly.** `surfaces/_switch.scss` carried
"state — position only: checked changes NOTHING else (no accent)" from the
switch's own reference pen. The thumb now carries the accent when checked; the
TRACK keeps upstream's own state colours, so the switch still reads as one
object, and the reversal is thumb-only. The user authorised it as part of this
pass. Kill lever: point `--ov-lit-edge-{top,bottom}` at
`--ov-bevel-{highlight,shadow}` and both thumbs go back to the neutral pair in
one edit.

**Verification.** Paired renders in one invocation, `controls` family: rest
**92/312000 px, max 214**; `STATE=prelight` **292/312000, max 216**; dark
**192, max 120**; `CONTRAST=more` **0/312000** — the HC reverts are structural,
not stylistic. Determinism check in the same run: **0 px** between two renders
of one sheet, so no noise floor applies to this family. Visually: at rest the
checked thumb sits in a deep-blue seat; under hover the scale knob wears the
accent rim, light at the top-left and deep at the foot — the pen's lever read.
`tools/check-selectors`: contract OK, `switch:checked > slider` added.

**Method note (new, keep).** `render-gallery` reads hover from the real
pointer, so a *default* render can come back hover-poisoned: in this pass
`out/after` differed from a re-render of the same sheet by **1262 px** confined
to `scale > trough` (the hovered 20% `currentColor` channel), and the
`STATE=prelight` pair matched it exactly. Every A/B from here on is rendered as
a pair inside one invocation; a lone default render is not a rest render.

**Not verified.** The live eye in daily apps (the user's sweep is the gate at
M7); fractional scaling (X5 card); the vertical scale and switches inside
`.adw` rows are outside the gallery, so the lit pair is measured on the
horizontal controls only.

## Pen dial pass 2/4: the dish ring — 26 Sep 2026

**What the pen does.** Its bezel *grows into the scene* when the dial opens:
`box-shadow: 0 0 0 calc(var(--radius)/13) var(--outer-bg)` — a spread-only
shadow, no offset and no blur, painted in the SURROUND's colour (the scene
behind the ring is `hsl(307 4% 94%)`, the ring's colour `hsl(223.81 0% 93%)`:
2/255 apart). What the eye reads is not the band — it is that the object's
footprint changed while nothing moved.

**What moved.** `ov-grow($width, $color)` in L1, called FIRST in a box-shadow
list (the earliest shadow paints on top, and the band has to cover the node's
own drop shadow to read as growth), with one consumer: the checked switch
thumb, which now sits in a 1.5px dish of its track's colour. L0 carries
`--ov-thumb-dish-width` (kill lever: `0px`) and `--ov-thumb-dish`, the checked
track's own appearance at the thumb's row — measured: the track reads
rgb(48,120,206) against an accent fill of rgb(53,132,228), so the token is
that fill's 8% black rung, i.e. within 1-4/255 of where it lands.

**Why exactly one consumer.** The trick needs a *uniform* surround painted by
a colour this sheet owns. The scale knob straddles its own fill boundary —
accent fill on one side, empty channel on the other — so a dish in either
colour would draw a seam across the channel. Recorded, not worked around: the
pen's ring has no seam because the pen's bezel is a disc on a flat scene, and
GTK's range knob is not that node.

**Measured** (paired renders in one invocation, `controls`, move 1 -> move 2):
**63 px changed, rows 78-86 and 95-101 only** — the bands above and below the
thumb, x 38-59 — deltas **+3 to +11/255**, every one of them toward the
track's unshadowed colour: the seat widens by the dish width and the drop
shadow starts further out. Under `STATE=prelight` the dish contributes 124 px
(max 20). `switch > slider` gains the first transition it has ever had
(`box-shadow` at `--ov-motion-switch`, additive — upstream declares none), so
the dish lands with the knob's own travel instead of snapping.

**Harness note, extends the one above.** In `SCHEME=dark CONTRAST=more` the
switch/scale work is **0 px**, but the family reports **316 px** in a vertical
strip at x 488-495, y 471-510 — the scrollbar thumb. A sheet changed by a
*comment only* reproduces the same 316 px in the same strip, so it is
parse/render timing against upstream's `scrollbar … transition: all 200ms
linear`, not a rule. Masking x >= 480 leaves **0 px** in both HC cells:
the reverts are structural in dark as well as light. The same artifact shows up
in `adw` (x 620-624, y 346-598) when that family is rendered *after* fourteen
others in an all-family run, and vanishes (0 px) when `adw` is rendered alone in
the same conditions — so it tracks when the family is rendered, not what the
sheet says. Any future A/B that lands on a scrolling family must mask the
scrollbar strip or use a comment-only control sheet.

## Pen dial pass 3/4: the accent's own light on the CTA — 26 Sep 2026

**What the pen does.** Every lit surface in it is painted from ONE hue: the lit
top `hsl(h 100% 72%)`, the foot `hsl(h 98% 61%)`, the fill gradient between
them. White appears nowhere in its material. This sheet's channels have always
lit with the physics constants — `ov-lit-curve()` white 30% → mid → black 14%,
`ov-lit-fill()` white 55% top hairline over black 22% foot — which, over an
accent fill, reads as a blue surface wearing a grey light.

**What moved.** `ov-lit-curve()` takes its two rungs as parameters
(`$light`/`$shade`, defaulting to white/black, so every existing caller is
unchanged) and `ov-lit-curve-accent()` builds the same curve out of
`--ov-lit-edge-*`. `ov-lit-fill()` takes the hairline pair the same way. One
consumer: `.suggested-action`, whose fill is the accent by construction.

**Why only `.suggested-action`.** Lighting a fill with the accent's hue is only
honest where the fill's colour is *decoration*. The progressbar's warning/error
variants, the levelbar's blocks and the scale's own highlight carry meaning in
that colour; mixing them toward the accent moves the thing the pixel is for.
They keep the physics pair — measured, not asserted: `controls` is **0 px**
changed by this commit. Same line the 23 Sep note drew, "garnish on a semantic
fill, not a new fill".

**Measured** (paired renders in one invocation, `buttons` family): light
**10244 px, max 72**; dark **11384, max 82**; `STATE=prelight` **10242, max
78**; `CONTRAST=more` **0 px**. The change is confined to the curve's two
rungs — per-row census of the CTA band: **rows 167-177 (the light stop) and
189-200 (the shade stop) changed, rows 178-188 at 0 px**, that being the band
the label sits in, because the mid stop is zero-alpha by design. The label pair
is therefore untouched, measured rather than assumed.

Top hairline rgb(189,214,245) → rgb(117,168,219); foot rgb(36,89,154) →
rgb(3,71,140): the same two rungs, now in the fill's own hue instead of white
over black. The direction is the point — the CTA reads as a lit accent surface
rather than as a blue one with a grey light on it.

## Debug build — 26 Sep 2026

**From the pen.** `xxyEYMJ` keeps a `--debug: 0` token and one declaration on
`*` (`outline: calc(var(--debug) * 1px) dotted hsl(… 60% 60%)`) that outlines
every box in the document when it is flipped on. The idea is worth keeping: an
L2 rule that is "not winning" is nearly always a rule aimed at the wrong node,
and the box answers that in one look.

**Why it is a build here, and not a token.** `outline` on `*` at priority 800
outranks every upstream focus ring. At a hypothetical `--debug: 0` the
declaration is still *there* — `0px dotted` — and it still replaces the ring
upstream draws for the keyboard. Nothing this sheet paints may cost the
keyboard its focus cue, not even while invisible. So the rule exists only in a
debug build: `src/_debug.scss` gated on `$ov-debug` (default false, so an
accidental import emits nothing), `src/overlay-debug.scss` as the entry point
(the same `@import`s in the same order, so anything the debug sheet renders
differently is the debug rule and nothing else), and `tools/build --debug` to
compile it into `build/gtk-debug.css` and install that.

**Verified.** `tools/build --debug --no-restart` writes build/gtk-debug.css
(864 lines against the shipped 858), repoints
`~/.config/gtk-4.0/gtk.css` at it and says DEBUG in the install line; a plain
`tools/build` repoints it back at build/gtk.css. The shipped sheet contains no
`* {` rule at all (grep). Rendering `controls` from build/gtk-debug.css against
the same family from the shipped sheet: **10541 px changed, max 182** — the
switch and its thumb, the scale's trough and slider, the captions, the
scrollbar and the bar's grain band, every one of them outlined.

The hue is `RGB(203 66 203)` — deliberately outside the palette, so an outline
can never be read as material — spelled in GTK's own `RGB()` form because
libsass claims `hsl()` for its own comma-separated signature. The first attempt
failed the build with "Function hsl is missing argument $saturation"; the trap
is recorded because it will recur in any future inline colour.

## GTK CSS capability probe — 26 Sep 2026

**Why.** The pen pass needed to know which of the pen's tools GTK 4.22.5
actually has, before spending a change on any of them. Measured on this
machine, not recalled: one declaration per selector in a sheet handed to
`build/render-widget` (parse errors land on stderr, and a render still
happens), then pixel checks on the TIFFs for the two that parse but might not
paint. Method worth keeping — it costs one throwaway sheet.

| Feature | GTK 4.22.5 | Evidence |
| --- | --- | --- |
| `@property` | **no** | `Theme parser error: Unknown @ rule` |
| `:has()` | **no** | `Unknown pseudoclass` (both `:has(label)` and `:has(:nth-child(2))`) |
| `::before` / `::after` | **no** | already in proposal §What not to reach for |
| `aspect-ratio`, grid | no property | irrelevant — layout is out of scope |
| `transition: <custom-prop> 1s` | parses | no interpolation type exists without `@property`, so it cannot animate |
| `conic-gradient()` | **yes, renders** | a 166px button's mid-row reads rgb(64,0,191) → rgb(191,0,64) |
| `radial-gradient()` | yes, renders | centre 255 red, falls to the rim |
| `filter: blur(8px)` | **yes, applies** | red spreads past the node box to the window edge; 200/200 px of the mid-row fully red |
| `filter: saturate(0) brightness(0.5)` | yes | `max(R-G)` goes 255 → 0 |
| `box-shadow` ladders, inset, spread | yes | the sheet's own idiom |
| `color-mix()`, relative colour syntax | yes | 416 uses in upstream's pinned sheet |

**What this settles.** Two of the pen's three load-bearing mechanisms are
unavailable — `@property` (`--angle`, `--selector-width`, `--is-selected` are
all *animated* custom properties there) and `:has()` (a checked descendant
re-hues its ancestor). That is why the port is a lighting language and not a
mechanism, and why the report's remaining lever, a conic gradient, has no
control here with a meaningful angle.

**Still on the table, unused.** `conic-gradient` renders and `filter: blur`
applies. `filter` stays banned in v1 for iGPU cost (proposal, risk table);
conic gradients have no GTK control whose angle means anything.

## Review verdict on the pen pass — 26 Sep 2026

A domain review of the pass (`better-colors` principles: measure the rendered
pair, hold the hue; `better-interface` evidence bar) over every rule the branch
added. Scope: `git diff 7814145..HEAD`, src/ + tools/ + README, re-measured on
fresh renders.

**Two findings, both fixed in this commit.**

1. **The lit rungs were srgb approximations where exact ones were reachable.**
   The first cut of `--ov-lit-edge-*` used `color-mix()` toward white/black
   ("the closest srgb rungs", because mixing cannot hold a hue's saturation).
   But GTK 4.22 supports **relative HSL**, and upstream's own sheet uses it
   (`HSL(from var(--accent-color) h …)` for `button.link:hover`, gtk.css
   L1429) — so the pen's actual stops (`hsl(h 100% 72%)` / `hsl(h 98% 61%)`)
   are one derivation each. Retuned at 92%/83% light (95%/90% dark) rather
   than 100%/98%: at full saturation the top rung reads neon next to
   upstream's own accent (s 79%), and the pair must sit inside the palette it
   lights. Measured after: thumb hairline hsl(213,63,49) against track fill
   hsl(213,63,51) — the hue held exactly, the lightness step is the bevel.
   CTA hairlines rgb(68,141,227)/rgb(55,133,226), s 74-75% against the fill's
   s 75%.
2. **Stale header.** `surfaces/_switch.scss` still opened with "state —
   position only: checked changes NOTHING else (no accent)" while its own
   engaged-thumb rule says the opposite. The track's invariant (upstream
   colours only) is kept; the thumb's reversal is now stated where it
   happened.

**Fidelity check against the pen's own numbers.** The pen: lever bevel
`inset 0 1px 2px` light-rung over `inset 0 -1px 2px` dark-rung, bloom
`0 0 4px`/`0 0 16px`, growth ring `9.85px` = 3.8% of a 256px disc in the
surround's colour. The port: the same bevel geometry at 1px/1px, halo
`0 0 2px`, dish `1.5px` = 4.4% of the 34px thumb in the track's colour —
proportionally within 0.6 percentage points. Register-by-register: accent-lit
edge ✓, bloom ✓, growth ✓, hue-as-seed ✓ (via relative HSL, the closest GTK
gets to `--angle`), mechanism ✗ (`@property`/`:has()` absent — no GTK control
could animate it), motion ✗ (the house spec bans movement; the dish lands
with the native knob slide instead).

**Pairs, measured on the rendered sheets.** CTA label: **3.81:1** worst row,
both schemes, before and after — the mid stop is zero-alpha, so the accent
register never touched the band the label sits in. Thumb boundary:
white-disc-vs-track **1.20:1 → 1.28:1** (the dish darkens the seat, which is
the point). HC: **0 px** in every cell, rest and prelight, light and dark.
Determinism: 0 px across all eight verification cells.

**Not verified.** The live eye in daily apps; fractional scaling (X5 card);
the `adw` family's own switches (outside the gallery's `controls`).

**Verdict: Approve.** No HIGH findings; both review findings fixed and
re-measured in this commit. The remaining distance to the pen is the platform
(no animated custom properties, no descendant-state re-huing, no
pseudo-elements, no rotation), not the palette.

## Rest register: bevel x4 + a drop on buttons — 26 Sep 2026

**Ask.** "These buttons look pretty darn flat to me. Not even close to the
reference" (user, a gnome-calculator screenshot), followed by three more
benchmarks — Settings, a browser, an EQ app — and a second reference pen
(jkantner, "Glowing On/Off Buttons", `gOjNdog`). The pen pass had only moved
*engaged* states; at rest a plain button carried the 19 Sep whisper bevel.

**Measured gap.** On a calculator-grey button the rest bevel moved the edges
by **+3 / -2 of 255**. Both reference pens carry a rest register roughly 10x
louder: xxyEYMJ's disc (13% insets, a 5-stop drop ladder), gOjNdog's cap
(gradient fill, `0 0.75em 0.75em 0.25em` drop that collapses on press).

**Two decisions, both the user's.** (1) Render candidates first. (2) Retire the
19 Sep "no external drops on buttons" rule.

**Candidates** (`buttons`, `controls`, `headerbar`, `lists`, rest): bevel x2,
x4 and pen-proportion, each with and without a two-stop drop. Judged on the
rendered sheet: **x4 + drop** won — pen-proportion drew the foot as a hard
line rather than a falloff, and x2 was still below what the eye reads as
raised. Mean delta vs stock on `buttons`: current 2.71, x4+drop 3.29,
pen+drop 3.59 (max 109, the harsh line).

**What landed.** `--ov-bevel-highlight` 16% -> 64%, `--ov-bevel-shadow`
8% -> 32%; L1 `ov-drop()` (`0 1px 2px` @12% + `0 2px 5px` @8% on
`--ov-depth-color`, so the depth kill switch removes it); the button rest
rule wears `ov-bevel(), ov-drop()`; the CTA wears the drop too, through a new
`$outer` parameter on `ov-lit-fill()` (channel fills pass nothing and are
byte-identical to before). Press and held replace the whole list with the
wells, so the button *sinks* — gOjNdog's press, and `probe-motion` now reports
`button:active` IN MOTION rather than INSTANT.

**Measured** (paired renders against the committed sheet): light — buttons
15036 px (max 48), headerbar 4252, entries 3612, notebook 2757, popover 2504,
dnd 1165; dark — buttons 5148 (max 53); `STATE=active` **0 px** (the well
owns press entirely); HC **0 px** (the fresh pair; a first `lists`/`spinbutton`
reading of 17967/272 px was the documented text-AA floor — a same-sheet rerun
reproduced it and a fresh pair of both sheets measured 0). Gap check: each
drop occupies ~5 rows under its button and the gap to the next button is
unchanged — no bleed into a neighbour, the failure that got the retired CTA
bloom banned.

**Open.** Under `STATE=checked` the gallery's Normal button reads 11/255
lighter across its whole fill (15081 px, reproducible, determinism 0). A
bisect puts it on the bevel tokens alone, which no `:checked` rule uses — the
checked rule replaces the list with `ov-well-held()`. Not explained yet; the
cell is a stress shot (every widget checked at once), so it is recorded rather
than chased at the cost of the user's live ask. -> BACKLOG RR2.

## Toggle groups — 26 Sep 2026

**Ask.** "The buttongroup element still seems unstyled, or less styled than
others" (user). It was literally unstyled: `AdwToggleGroup` (and the inline
view switcher built on it — Settings' Mouse/Touchpad) is
`toggle-group > toggle`, not `button`, so no button rule ever matched it, and
the gallery had no toggle group, which is how every sweep missed it.

**What landed.** `surfaces/_toggle-group.scss`: the group is a recessed well
(upstream's 10% fill plus the entries' scoop pair), the checked toggle is a
raised cap wearing the button rest material (`ov-bevel(), ov-drop()`, replacing
upstream's own two-stop lift) — gOjNdog's raised cap in a well. Unchecked
hover/press keep upstream's washes; `.flat` groups are left alone.
`:checked:disabled` is flattened (upstream does it; at priority 800 our rule
would otherwise keep a disabled cap raised — BACKLOG U7's trap). The gallery's
`adw` family gains a three-toggle group.

**HC — restated, and one deliberate difference.** The HC block restates
upstream's rings verbatim, including the disabled pair, which needed
`--disabled-opacity` through a new `--ov-up-disabled-opacity` alias (contract
entry added). In the harness, stock HC paints *no* ring on the group although
upstream declares one at `--border-opacity: 50%` — the bare-`var()`-in-
`color-mix()`-percentage behaviour recorded under "Flat register". The
overlay's alias carries a fallback, so its restatement actually paints the ring
upstream intended: 550 px (max 30), the group's boundary. That matches how
every button HC revert in this sheet already behaves.

**Measured** (`adw`, paired vs the committed sheet, determinism 0 in every
cell): light 676 px (max 41), dark 520 (max 55), prelight 676, HC 550 / dark
HC 560 (the ring above). `tools/check-selectors`: contract OK with five new
selector atoms and one variable.

## Button face, neon hover, rim — 26 Sep 2026

**Asks, in order.** "Improve the inner glow to feel more elegant and
neon-like"; on the calculator, "still very ugly"; then "this looks nice, but
tone it down a little and add a 0.5 border with a tint a tiny darker than the
background" (user).

**Neon hover.** The 19 Sep hover was five accent gradient washes (5-16%),
judged on the rendered sheet as "a tint — a smudge more than a glow". Three
box-shadow candidates rendered in both schemes (rim + inner / + halo /
"the tube": hot core + ring + halo); the tube won in both. `ov-neon()`: a 1px
core at the accent's hue, 78% lightness; a 2px tube ring; a 10px inward fall;
an 8px outer halo. `ov-glow()` deleted (no other consumer).

**The destructive hue was never free.** The candidates showed a *blue* neon
ring on the red destructive button. GTK computes a custom property holding
`var()` on the element that declares it, so `--ov-up-accent` on `:root` is the
root accent everywhere, and upstream's per-node re-point of `--accent-color`
on `.destructive-action` (gtk.css L488, L1682) never reached anything derived
from it. The 23 Sep note in `_button.scss` claiming the glow "re-hues for free"
was an inference, now disproven and rewritten. Fix: `ov-accent-register()` in
L0 derives every accent token, applied on `:root` and again on
`.destructive-action` with the alias re-pointed. Measured after: the
destructive button glows red in both schemes. Re-point atoms registered.

**The face.** "Still very ugly" was the fill: edges and a drop around a flat
colour slab read as a sticker with an outline; neither reference pen has a
flat surface. A convex face (top glow, foot shade) flipping concave on press.
First cut as a `background-image` gradient — **rejected by measurement**: on
`render-widget` (plain GTK, no libadwaita) the button centre went to
**alpha 0**, because GTK's built-in theme paints the whole button fill as a
background-image (`linear-gradient(to top, #f6f5f4 2px, #fbfafa)`), and ours
replaced it. The face is therefore two feathered **inset shadows**. The
material now declares no background-image in any button state, which also
removed two latent erasures of the same kind (the disabled rule's and the HC
hover revert's `background-image: none`). Plain-GTK button after:
rgb(248,247,247) alpha 255, identical to stock.

**Tune + rim.** Face 34/10% -> 22/7% (dark 9/20 -> 6/14), bevel 64/32 ->
48/24, drop 12/8 -> 9/6. The rim is `inset 0 0 0 0.5px` of black at 11%
(32% dark) inside `ov-bevel()`, so it composites on whatever fill a state or
an app gives the button: measured on the Normal button's edge, 216 against
the 229 fill. The toggle-group cap wears it too (same function).

**Measured** (`buttons`, paired, determinism 0): face + neon vs the committed
sheet — light 40151 px, dark 31764, prelight 87458, active 16356; HC **0 px**
in rest, prelight and dark HC. The tune vs the first face — light 17295 (max
21), dark 19922 (max 33), HC 0. Normal label contrast unchanged: 7.87:1 vs
7.95:1 light, 9.58:1 dark. `check-selectors` OK, `probe-foreign` OK,
`probe-motion`: hover INSTANT at 80ms, press IN MOTION.

## Path bar — 26 Sep 2026

**Ask.** "In the breadcrumbs up top, the raised buttons are not looking good"
(user, a Nautilus screenshot). Nautilus 50.3 draws its path bar as a 10%
currentColor well and means the crumbs to be text in it
(`.nautilus-path-button:not(:hover) { background: none }`, its style.css). The
crumbs are plain buttons without `.flat`, so the rest register (face, rim,
drop) landed on each one: a row of raised keys stacked in a sunken field,
drops clipped by it.

**What landed.** `surfaces/_pathbar.scss`, the toggle-group language: the well
wears the same scoop pair; crumbs carry no material in any state (hover keeps
libadwaita's own button hover fill, which Nautilus lets through, but not the
neon — the scrolled window around the crumbs would clip its halo); the
`.current-dir` crumb is the raised cap: `--ov-up-cap-bg` (new alias of
`--active-toggle-bg-color`, contract entry added) with `ov-bevel(), ov-face()`
and no drop, which the 3px margin would clip. HC restates Nautilus's own ring
on the well and leaves resting crumbs bare; hovered crumbs get the house HC
ring.

**Not in the selector contract, deliberately.** These are Nautilus's classes,
not libadwaita's; the guard reads libadwaita's sheet and would flag them. A
Nautilus rename degrades the crumbs back to the generic button material and
cannot break anything else.

**Verified** on a Nautilus-shaped mock added to the gallery's `adw` family (the
app's classes, three crumbs, last one current), rendered with Nautilus's own
path-bar CSS prepended to the sheet: light, dark, HC, and prelight — installed
sheet = three raised keys; new = text in a well with one white cap; HC
identical to Nautilus stock plus the hovered ring. Method trap recorded: the
first excerpt of Nautilus's CSS ended mid-block, which swallowed the whole
overlay (render came back fully stock); check that a prepended excerpt closes
every block. The live Nautilus window is the final judge.

**Cap dropped (same day).** In a live window Nautilus stretches the
`.current-dir` crumb to fill the rest of the bar, so the white cap became a
bar-wide slab that read as a text field (the mock had label-sized crumbs and
did not show it). User: "drop the cap and keep the recessed well". Every
crumb is now flat text; the well keeps its scoop. `--ov-up-cap-bg` and its
contract entry are removed (no consumer left). The gallery mock now stretches
the current crumb like Nautilus does; rendered light/dark/HC against
Nautilus's stock CSS: text in the well in all three, HC matching stock.

## Tabs — 26 Sep 2026

**Ask.** "Improve tabs visual to match the overall system look and feel"
(user). Reopens the 19 Sep U6 deferral for GtkNotebook and AdwTabBar.

**AdwTabBar** (`surfaces/_tabbar.scss`). (1) A standalone strip painted
upstream's flat headerbar colour under a headerbar wearing the bar material,
so the chrome broke in two; the strip now wears `ov-bar-surface()` with the
same backdrop re-point, and stays transparent where upstream makes it so
(inside a toolbarview bar, `.inline`) — restated, since our rule outranks
theirs. (2) The selected tab was a flat 10% patch; it is now the house raised
cap (bevel + rim + face + drop; the tab box's 6px padding leaves room for the
drop). Unselected tabs keep upstream's washes; a single tab stays bare, as
upstream has it. (3) The round close button is a plain button and was wearing
the full raised material, a 3D disc inside the tab; it now carries none.

**GtkNotebook** (`surfaces/_notebook.scss`). The checked tab is the raised cap
(bevel + face, no drop — the header border sits right under it) and its 4px
accent underline becomes a lit one: a 3px accent bar with the accent's own
light (`--ov-lit-bloom`) rising 4px into the tab. All four header positions.
Hover and switch now animate the box-shadow at house timing (it snapped).

**Measured** (paired, determinism 0 in every cell): `adw` light 28409 px /
dark 24782 / prelight 28319; `notebook` light 679 / dark 1149 / prelight 789;
HC `notebook` 0 px (the underline is restated exactly). HC `adw` 488 px, all
on the selected tab: upstream declares an HC ring there that stock does not
paint (bare `var()` in a `color-mix()` percentage, the same finding as
"Toggle groups"); the alias's fallback paints it. Contract: nine new atoms,
OK.

## Neon hover motion — 26 Sep 2026

**Ask.** "Tweak the button glow (hover state) animation to feel more organic"
— the accent-coloured neon (user).

**It never animated.** `probe-motion` had recorded hover INSTANT since the
neon landed; the cause is GTK's `shadow_value_transition()` (gtk 4.22.5,
gtkcssshadowvalue.c): it interpolates box-shadow slot by slot and fails the
whole list when one slot's `inset` flag differs. Rest was bevel/face/drop;
hover prepended the neon, so its outset halo sat opposite the face's inset
shade. A two-rule sheet reproduces it (misaligned: INSTANT; aligned: IN
MOTION). Second snap: the hover rule's `transition` listed only box-shadow,
so upstream's `background-color` wash jumped under the glow.

**What landed.** `ov-neon($lit: false)`: the four neon slots at zero alpha
with the geometry collapsed onto the edge (rings spread 0, fall blur 0, halo
`-2px` under the border). Every rest list the hover reaches opens with it;
the suggested CTA's own list is padded with three empty inset slots so it
lines up with the generic hover list it hovers into. Lighting from collapsed
geometry makes the light spread as it brightens, and the halo only clears the
border partway through: the tube strikes, then the glow blooms. New token
`--ov-motion-glow: cubic-bezier(0.23, 1, 0.32, 1)` on the neon's box-shadow in
and out; `ov-motion()` takes an optional curve per entry (entries without one
emit what they did before). Durations unchanged (280/200ms, BACKLOG B4).

**Measured.** `probe-motion` (40ms samples, upstream loaded): hover IN MOTION
light/dark/HC (HEAD: INSTANT), INSTANT under REDUCE=1 and NOANIM=1. Gallery
`buttons` vs HEAD: rest, prelight, dark and HC all 0 px — the lit and unlit
frames are unchanged, only the path between them. `check-selectors` OK.
Not the glow, left alone: prelight still moves the probe's mean instantly
with every shadow, background and transition neutralised; the press well
still snaps from a hovered button (its list does not align with the neon).

## Aqua gel — 26 Sep 2026

**Ask.** "Change the fill of the suggested button, switch, scale, progress bar
and level bar [to] use the highlight color with transparency, inspired in the
old Aqua macOS components" — then "match the overall look and feel" (user).
Reopens the 23 Sep retirement of the CTA's translucent glass by user decision;
the defect that retired it (the label pair, 2.00:1) is the constraint the gel
is tuned on.

**What landed.** One L1 material, `ov-gel-fill()` (`_primitives.scss`),
replacing the opaque lit fill and `ov-lit-curve()` / `ov-lit-curve-accent()`
(no consumers left). Over the colour upstream paints on the node:
- body — the fill with its HSL lightness lowered by `--ov-gel-deepen`
  (relative `HSL(from … h s calc(l - n))`, probed on GTK 4.22.5: works, and
  keeps saturation — the first cut mixed toward dark-5 in sRGB and rendered
  grey), then made translucent at `--ov-gel-opacity: 80%`;
- gloss — light-1 from the top to a hard edge (50% on channels, 34% on the
  CTA so the label band is body only);
- caustic — light-1 rising from the foot (55% / 32%);
- rims — a dark rung on top and a light rung at the foot, on the house 0.5px
  bevel geometry (`--ov-bevel-width`), so the gel's edges weigh what every
  other raised edge does and fall with the bevel kill switch.

Toned after the second ask: gloss 78% → 55% (light) / 60% → 40% (dark),
1px blurred rims → 0.5px hairlines; the drop, neon hover, wells and thumb
treatments are unchanged, so the gel parts share the rest of the house
register.

Consumers: `button.suggested-action` (rest/hover/press/held; the box-shadow
slot layout the neon transition relies on is unchanged), `switch:checked`
(the track keeps three shadow slots in every state so the flip still
interpolates), `scale > trough > highlight`, `progressbar > trough >
progress`, `levelbar` blocks. Vertical channels run the gloss down the leading
side (`.vertical`). The thumb dish is re-derived from the gel body: 1/255 from
the track at the thumb's row.

**Semantic colour kept.** "Highlight colour" is read as the accent where
upstream paints the accent. Levelbar `block.low` / `block.full` keep
warning / success (new aliases `--ov-up-warning-fill`, `--ov-up-success-fill`),
and `.error` / `.warning` / `.success` re-declare `--ov-up-accent-fill` in L0
— the destructive re-point trap again: the gel owns `background-color`, and
an alias computed on `:root` never sees upstream's per-node re-point.
Contract: +5 selector atoms, +2 variables; `check-selectors` OK.

**Measured** (gallery, white label on the CTA body): light 4.17:1, dark
4.95:1; stock 3.77, the retired glass 2.00. HC: every gel node reverts to
upstream's opaque fill, flat (CTA and checked switch sample 53,132,228 in both
schemes). No `prefers-reduced-transparency` in GTK 4.22.5 (its media features
are color-scheme, contrast, reduced-motion), so the translucency has no
separate opt-out; HC is the escape hatch.

**Tool fix.** `render-gallery` never told the overlay's provider the scheme:
GtkCssProvider's `prefers-color-scheme` does not follow GtkSettings, so every
`SCHEME=dark` render so far applied the LIGHT values of the overlay's
scheme-split tokens (libadwaita's own palette was dark, which hid it). Found by
the gel's dark-only deepen; fixed in the same place `prefers-contrast` is set.
`probe-motion` sets the scheme the same way and has the same gap (not fixed
here).

### Follow-up: the switch flip went janky — 26 Sep 2026

**Report.** "Switch animation got very JANKY" (user), after the gel landed.

**Two causes, both layer alignment.** Read in GTK 4.22.5's source and
confirmed on frame dumps of a real `GtkSwitch` under libadwaita (a throwaway
probe, 15ms samples, with and without :hover):
- *background-image.* GTK transitions image lists layer by layer from the top
  (`gtkcssarrayvalue.c`, `transition_extend`), cross-fading any pair whose
  gradients differ in stop count or side. The track went [dish] → [gloss,
  caustic] on the flip and [gloss, caustic] ↔ [lift, gloss, caustic] on
  hover and press, so the hard-edged gloss was cross-faded into the wrong
  layer: the band arrived late on the flip and pumped on every hover and
  click. The old dish gradient was faint enough to hide the same mismatch.
- *thumb box-shadow* (pre-existing since the pen pass, made visible by the
  gel's darker dish). Rest [drop, inset, inset] against engaged [dish, halo,
  drop, inset, inset]: slot 2 differs in its inset flag, so GTK drops the
  transition and the ring and halo popped on at the flip and off three frames
  late (`gtkcssshadowvalue.c`).

**Fix.** `ov-gel-images()` takes `$state` (a leading state slot, a flat
two-stop gradient so it interpolates natively rather than cross-fading
upstream's `image()`) and `$lit` (the same stops at zero alpha, the
`ov-neon($lit)` pattern). The switch track carries [state, gloss, caustic,
dish] in every state; the CTA carries [state, gloss, caustic] in rest, hover,
press and held. The resting thumb carries the engaged thumb's five slots with
dish and halo unlit and collapsed, so the ring grows with the slide.

**Measured.** Settled frames unchanged: gallery `controls` and `buttons`,
light and dark, and `buttons` prelight, 0 px against the janky sheet. Frame
dumps: the thumb ring now grows over the slide in both directions (was: full
at frame 0 on, gone at frame 3 off); hover on a checked switch moves the
track mean monotonically (max step 5, was 7 with a reversal). Not measured: a
live compositor (the probe renders offscreen, and the native knob travel does
not advance there).

### Follow-up: subtler, more transparent — 26 Sep 2026

**Ask.** "Make the gel effect a little more subtle, and add tiny bit more
transparency to the highlight color" (user).

**Change.** Body opacity 80% → 72% (the page now shows through 28%); light
deepen 14 → 19 HSL points to hold the CTA label above stock. Gloss top/fade
55/14% → 42/10% light, 40/8% → 30/6% dark; caustic 30% → 22% light, 20% →
14% dark; rims 70% → 50% alpha. Layer structure untouched, so the motion fix
above still holds.

**Measured** (gallery, white label on the CTA body): light 4.00:1 (was
4.17), dark 5.55:1 (was 4.95); stock 3.77.

### Follow-up: brighter, more vibrant — 26 Sep 2026

**Ask.** "Gel has too much of dark tone… add more bright to it? Make it more
vibrant?" (user).

**Change.** New token `--ov-gel-vivid: 20` (HSL saturation points added to the
body); `--ov-gel-deepen` 19 → 10 in light, 0 → -8 in dark (the body is now
lifted there); caustic 22% → 28% light.

**Trade, accepted by the ask.** The dark tone was what held the CTA label
above stock. Now: computed on blue #3584e4, 3.23:1 light / 4.57:1 dark;
rendered on the system's current green accent #3a944a, 3.17:1 / 3.51:1
(stock 3.77 / 3.81). Above the 3:1 large-text floor, below upstream's pair
and below 4.5:1 for 13px bold. HC still restores the opaque stock fill;
`--ov-gel-deepen` is the one lever to buy contrast back.

## Gel: original, lean — 26 Sep 2026

**Ask.** "It's looking like a ripoff of macOS. Can you make it original?" and
"make it more lean too; maybe use SVG instead of PNG?" (user).

**What was Aqua, and what replaced it.** The three Aqua signatures — the
hard-edged gloss band, light pooling at the foot (the caustic), the dark top
rim — are gone. The gel is now this sheet's own lighting cast in tinted glass;
every part already existed somewhere else in the theme:
- *face* — the button face (`ov-face()`): light feathered from the top, shade
  feathered from the foot, no edge anywhere (`--ov-gel-light`,
  `--ov-gel-shade`);
- *edge* — the pen's lit edge: a 0.5px top hairline in the fill's own light
  rung (`HSL(from fill h s calc(l + 24))`, `--ov-gel-edge`);
- *glow* — the neon's inner fall, dimmed and at rest: the same rung feathered
  4px in from every edge (`--ov-gel-glow`), so the part reads charged, lit
  from within, instead of glossed from outside. The one idea no other
  material here carries at rest, and what makes the gel read as this theme's.
The translucent vivid body (opacity, deepen, vivid) is unchanged. Vertical
channels light from the leading side as before.

**Leaner.** The face is one gradient (light → clear → clear → shade) instead
of two layers: the switch track carries three image layers per state (was
four), the CTA two (was three), channels one (was two); the congruent-list
rule from the jank fix holds by construction (`ov-gel-images()` builds every
state's list). The Aqua tokens (`gloss-top`, `gloss-fade`, `caustic`) and the
two rim functions are gone.

**SVG instead of PNG.** The headerbar grain was two 64px PNGs (2.6 KB and
3.1 KB) behind an absolute `file:///home/...` path — against the README's own
"SVG texture tiles (data: URIs preferred)". It is now `ov-grain()`: an inline
289-byte feTurbulence tile, one per scheme, no files to install. The 19 Sep
finding "feTurbulence data: URIs render empty in app processes" did not
reproduce on GTK 4.22.5 (render-widget and render-gallery both paint it; the
19 Sep note already suspected the delivery failure masked it). The noise's
red channel is stretched into alpha, fitted to the PNG statistics: rendered
alpha light 5.99 mean / 3.46 stddev (PNG 5.94 / 3.67), dark 3.35 / 2.07 (PNG
3.32 / 2.22); on the gallery's headerbar, a text-free patch reads 250.3 / 1.89
(PNG 250.2 / 2.04) light and 155.3 / 0.93 (155.3 / 0.95) dark. Both PNGs
deleted.

**Measured** (gallery, white label on the CTA body, blue accent): light
3.28:1, dark 4.54:1 (stock 3.77) — the brightness trade from the previous
entry, unchanged in kind.

## Toolbarview bottom bar — 26 Sep 2026

**Bug.** Nautilus' file chooser (the GNOME 50 FileChooser portal) showed a
plain white slab across the bottom bar between two gradient patches (user
screenshot). Its bottom bar is an `AdwToolbarView` bottom bar holding a
GtkCenterBox whose start and end children are separate `.toolbar` boxes; in
open mode the centre (filename widget) is empty. We painted each `.toolbar`
with `ov-bar-surface()` but left the container on upstream's flat
`toolbarview > .bottom-bar.raised` colour, which showed through the gap.

**Fix** (`surfaces/_toolbar.scss`). The mirror of the top bar: the
`toolbarview > .bottom-bar` container wears the bar material with the same
backdrop re-point, and `.toolbar`, searchbar and actionbar boxes nested in
either toolbarview bar go transparent (upstream already does that for
searchbar/actionbar there; restated, since our rule outranks theirs), so no
nested bar restarts the gradient mid-bar. Verified with an offscreen render of
the same widget tree before/after. Contract: eight new atoms, OK.

## GTK3 accent: scope and method — 27 Sep 2026

**Scope (user).** GTK3 is in, accent only (BACKLOG G). Material on GTK3 is
G6, gated on daily-drive evidence.

**Finding.** GTK3's built-in Adwaita (gtk3 1:3.24.52-1) never follows the
accent: `_colors.scss:11` is `$selected_bg_color: if($variant == 'light',
#3584e4, darken(#3584e4, 20%))`, sassc bakes it, and no rule references
`@theme_selected_bg_color`. With `accent-color` = `teal`, every GTK3 app here
(gnome-terminal, gedit, meld, evince, disks, …) paints blue. Redefining a
named colour from user CSS — libsingularity's `_write_gtk_accent` — cannot
fix Adwaita; it only works for a theme whose rules read that name.

**Method: differential compile, not literal grep.** Upstream's SCSS
recompiled with `sassc -a -M -t compact` is byte-identical to the sheet in
libgtk-3.so (verified; `fetch-upstream --gtk3` refuses a tag where it is not).
`tools/gtk3-accent-sites` compiles it with the stock accent and two sentinels
and keeps every declaration whose value moves: **251 rules / 373
declarations** (light 131/194, dark 120/179), where grepping for `#3584e4`
finds 71 lines in the light sheet only. Twenty-odd derived values carry the
accent: `#1b6acb`, `#185fb4`, `#15539e`, `#030c17`, … and text-shadow alphas
such as `rgba(0, 0, 0, 0.719216)` that sassc derives from the accent's
lightness. Two sentinels because a derivation that clamps (lighten() to
white) can coincide with stock under one.

**Consequence for G3 (revises the chat plan).** The plan was to hand-write
`mix(@theme_bg_color, @ov_accent, f)` expressions — libsingularity's
`build-css.py` idea done at source. The inventory retires that: hand-mapping
373 declarations, including lightness-dependent alphas GTK3's `mix()` cannot
express, is where drift would come from. Instead the accent sheet is
*generated*: compile the pinned source with the user's accent and keep only
the inventoried declarations. Exact by construction, no hand-written colour.
Its guard is the pin itself: `check-selectors` reports SHEET DRIFT when the
installed sheet is no longer the pinned tag's, which is when the generated
sheet stops being exact. The hand-written contract files
(`upstream/gtk3/selectors.txt`, `variables.txt`) are left for G6.

**Cost.** Runtime accent changes need regeneration (G4's user service)
instead of a GTK-side expression — acceptable: a rebuild is ~0.3 s, and GTK3
reads user CSS at startup anyway [INFERENCE, to verify in G4].

## G3: a theme, not the user sheet — 27 Sep 2026

**Problem.** Emitting only the 373 accent declarations from
`~/.config/gtk-3.0/gtk.css` breaks the cascade. GTK compares provider
priority before specificity, so the sheet's `button.suggested-action {
background-image: <accent gradient> }` at 800 beats upstream's non-accent
`button.suggested-action:disabled { background-image: image(#faf9f8) }` at
200 — every disabled CTA would paint accent. A full copy at 800 fixes that
but beats apps' own CSS at 600.

**Decision (user).** Ship the full recompiled sheet as a theme,
`Adwaita-overlay`, at `PRIORITY_THEME`: the cascade is stock's and apps'
CSS still wins. `tools/build-gtk3` writes `build/gtk3-theme/gtk-3.0/`
(`gtk.css`, `gtk-dark.css`), symlinked from
`~/.local/share/themes/Adwaita-overlay/gtk-3.0`; `--activate` sets
`gtk-theme`. Asset URLs point at the installed library's gresource.

**Constraints found on the way.**
- The name cannot be `Adwaita`: GTK3 resolves it to the built-in resource
  before any theme directory (tested with `XDG_DATA_HOME`).
- libhandy loads its `Adwaita{,-dark}.css` only under the theme name
  `Adwaita`, else `fallback.css` — a strict subset (48/48 lines in
  Adwaita.css). Both carry `#3584e4` too, so the theme appends libhandy's
  Adwaita sheets, recompiled from the pinned tag (1.8.3) against the pinned
  gtk3 SCSS with the same accent. Today's sassc reproduces them except for
  three lines per variant whose compound selectors are ordered differently
  (same characters); `fetch-upstream --libhandy` accepts exactly that. The
  expander-arrow rule is in both our sheet and libhandy's fallback at equal
  priority; which wins is untested (G5).
- sassc splits `-I` on `:`, so the epoch in the cache path needs a
  colon-free copy.
- `gsettings reset … gtk-theme` yields `Adwaita-dark` here (schema default),
  not the `Adwaita` that was set; `--deactivate` sets `Adwaita` explicitly.
- Dark: GTK3 3.24.52 does not read `color-scheme` (no such string in
  libgdk-3); it switches to `gtk-dark.css` on prefer-dark. HC: with the theme
  active and `a11y.interface high-contrast` on, GTK3 resolves `HighContrast`
  — our sheet is out of the way, rule 3 holds with no code.

**Evidence.** Throwaway GTK3 offscreen probe (suggested/disabled CTA, switch,
check, progress, scale, selected row, entry selection, HdyViewSwitcher):
numbers in BACKLOG G3. The drift guard now covers libhandy as well, and the
hook triggers on `libhandy`.

## H1: The accent register moved to oklab — 6 Oct 2026

**Problem.** 404d995 moved `--ov-lit-edge-*` off `color-mix()` onto relative
HSL, so the pen's own stops became reachable. That part stands — but HSL
lightness is a property of HSL geometry, not of how light the result looks.
Measured over the nine system accents (standalone values from libadwaita's
own table, sRGB→oklab converted offline):

| rung | oklab L per accent | spread |
| --- | --- | --- |
| `h 92% l 72%` (lit-edge-top) | 0.852 (red) … 0.983 (yellow) | **0.132** |
| `h 83% l 61%` (lit-edge-bottom) | 0.798 (red) … 0.966 (yellow) | 0.168 |

So the lit edge is a heavy near-white line on a yellow CTA and a faint one
on a red one. Nothing chose that; it is the ramp.

**Why this was already fixable.** Upstream derives the standalone colour in
oklab itself — `oklab(from --accent-bg-color min(l, 0.5) a b)` light,
`max(l, 0.85)` dark (gtk.css L2512) — which pins all nine accent sources
into oklab L **0.499–0.509**, a spread of 0.010. The inputs are already
perceptually uniform, so an ABSOLUTE lightness target is the entire fix.

**Decision.** Flat targets, oklab, anchored on blue so the accent that was
tuned by eye does not move:

| rung | L | k | blue today |
| --- | --- | --- | --- |
| `--ov-lit-edge-top` | 0.938 | 0.40 | L 0.938, C 0.066 of 0.165 |
| `--ov-lit-edge-bottom` | 0.898 | 0.51 | L 0.898, C 0.084 of 0.165 |
| `--ov-neon-core` | 0.959 | 0.34 | L 0.959, C 0.056 of 0.165 |
| `--ov-neon-tube` | 0.925 | 0.54 | L 0.925, C 0.088 of 0.165 |
| `--ov-lit-edge-top` (dark) | 0.952 | 0.70 | L 0.952, C 0.072 of 0.102 |
| `--ov-lit-edge-bottom` (dark) | 0.938 | 0.93 | L 0.938, C 0.095 of 0.102 |

**The chroma factors are not a translation of 92%/83%.** Measured,
C(rung)/C(source) under HSL runs **0.34 to 1.82** across the nine accents:
HSL saturation-% has no hue-independent oklab counterpart, because at 72%
lightness it is sRGB's gamut that compresses the chroma, not the 92%. What
carries over is the *intent* 404d995 recorded — "the pair must sit inside
the palette it lights, not outside it" — as one factor against the accent's
own `a`/`b`. `calc()` over a colour coordinate is GTK's own documented form
(`hsl(from red h calc(s * 1.3) …)`); the sheet already relied on it for the
gel rungs, so this is not a new capability.

**The neon pair lost its implicit scheme split, deliberately.** `h 100% l
78%` was a large lift over a light-scheme source (L 0.50) and almost none
over a dark-scheme source (L 0.85), so one token read at two strengths. An
absolute L reads the same in both, and `probe-accent` (H3) now measures the
dark branch separately.

**Known change, not a regression.** Slate's source chroma is 0.038 (a
near-grey blue), so scaling `a`/`b` cannot reproduce the rung's chroma the
way HSL's high-lightness gamut expansion did; its lit edge is now correctly
grey rather than tinted. Yellow moves the other way and loses some edge
definition (0.983 → 0.938) — that is the flattening, and it is the whole
point of the change.

**Evidence.** `calc(a * 0.40)` resolves and is consumed: forcing the target
to 0.30 moves `controls` (104 px, max Δ185) and nothing else. Containment
against the HSL build at the same accent — 12/15 families byte-identical in
light, 13/15 in dark; the three that move are the families that wear the
register. Blue is the anchor: max Δ8/255 on `columns` (rounding), and the
checked switch is visually identical. **Under `prefers-contrast: more`,
0 px changed across all 15 families** — the existing L1 reverts fully
neutralise the change, which is the a11y rule holding structurally.
`probe-foreign` exit 0. Nine-accent sweep sheets in `out/sw-*` vs
`out/old-*`.

## H2: The contracts only ran in one direction — 6 Oct 2026

**Problem.** `check-selectors` answered "does upstream still provide what we
registered?" It never answered "do we actually depend on anything
unregistered?" — so a mistyped variable in `_tokens.scss` was invisible, and
so was the whole of `src/_user.scss` (H4). Two ways to be wrong, one guard.

**Two things had to be built, not one.**

`extract_selectors` is a heuristic tuned for **minified** upstream CSS, where
`{` always opens a rule: split on `{` and `,`, drop `@define-color`. Run on
the *pretty-printed* built sheet it yields 495 "atoms", of which the
overwhelming majority are declaration fragments (`box-shadow: inset 0 1px
…`, `white 14%`). A brace-depth walker over the same file yields 242 sane
selectors. The heuristic is untouched — it is correct for what it is for.

Even with a correct extractor, the **selector axis does not invert**, and
that is the contract's own rule rather than a gap. `upstream/selectors.txt`
registers "the ATOMS UPSTREAM USES for anything the overlay overrides — not
necessarily the atoms the overlay itself writes", and names its own worked
example: the overlay targets `.content-pane` directly, but upstream styles
it only inside compounds, so the compound is what the guard must watch.
`window` and `dialog` are GTK-core names that must not be there at all.
Inverting it flags every compound the overlay composes in SCSS plus every
GTK-core node — ~180 of 242 false positives.

**Decision.** Add the reverse axis where it inverts cleanly: the **variable**
axis. `src/_tokens.scss` is the only layer permitted to read an upstream
property, and each is read through exactly one `--ov-up-*` alias, so
"everything the sheet reads that is not `--ov-*` is in `variables.txt`" is a
total, decidable statement. Plus a dead-token check (a `--ov-*` that is
declared and never read is a leftover, not coverage).

**Where the selector axis would belong.** `src/_user.scss` needs it most and
does not get it; a user naming an unregistered selector writes a rule that
upstream can silently invalidate. Recorded as an open gap, not solved.

**Evidence.** `--reverse` passes today: 13 upstream properties read, all
registered; 52 `--ov-*` tokens, 0 dead. Verified to bite by faulting the
built sheet — an injected `var(--totally-unregistered-var)` and an injected
`--ov-dead-token` both fail with exit 1 and an actionable line. One
normalisation is load-bearing: libadwaita spells one property
`---slider-border-color` (three dashes, defined on `scale`), so `var_refs`
collapses leading dash runs to two exactly as `variables.txt` already does
by hand. Skipped, loudly, when `build/gtk.css` is absent — the pacman hook
runs as root and must never `sassc` into the user's gitignored `build/`.

## H3: Accent liveness — measured, and narrower than advertised — 6 Oct 2026

**Problem.** The register is a pure function of upstream's accent, so changing
the system accent should re-derive the whole material system in a running
app. Nothing tested that, and the claim had been made in conversation.

**Decision.** `tools/probe-accent` — gtk4 only, no libadwaita, no GSettings,
no file written, no system state touched. It re-points one property on
`:root` from a provider at **801** (GTK compares provider priority before
specificity, so an equal-priority override is ambiguous) and reports the
**largest single-pixel move** between two renderings of the same node.

**Why max-pixel and not a mean.** The first cut averaged the widget, and
reported its own negative control as LIVE: the lit edge is a 1px hairline on
a 300×24 node, so it moves a mean by single digits, in the same range as
unrelated drift. Per-pixel max has no in-between to average away.

**Verdict — the register is live.** Light and dark, `build/gtk.css`:
`--accent-bg-color` → switch LIVE Δ709; `--accent-color` → switch LIVE Δ65
(lit rungs); `--accent-bg-color` → CTA LIVE Δ399 (gel); control STALE Δ0.
Exit 0. Stock control (`none`) exits 1, so the probe can fail.

**Four things the tool found by being wrong first.** The lit rungs paint only
in a state — `switch:checked > slider` and `scale:hover` — so a probe pointed
at a resting knob measures a register that is switched off, not a dead one.
L0 aliases the two accent inputs **separately**: `--ov-up-accent-fill` reads
`--accent-bg-color`, `--ov-up-accent` reads `--accent-color`, so a pair that
re-points `--accent-color` at the gel asks the wrong question. **The baseline
had to be taken from a settled node**: the switch runs a 180ms state
animation, so one baseline sample compared against a settled "after" returns
STALE whenever the timing goes the wrong way — the same command on the same
bytes gave LIVE max_delta=709 and STALE max_delta=0 on consecutive runs, which
looked exactly like a regression in the fallback palette added the same day
and cost a detour before it was recognised as a flaky instrument. The probe
now samples until two consecutive renders agree and reports UNSTABLE if that
never happens. **And the sanity pair is now enforced**: it is the instrument
check — it proves the override reached the node at all — and the first version
excluded it from the tally without ever requiring it, so a broken instrument
reported success. Both fixed; six consecutive runs on identical bytes now
agree, and a sheet with a genuinely dead link (the H6 fallback palette removed
by hand, leaving the aliases pointing at undefined names) is reported as
1/2 with exit 1.

**What libadwaita does, for the record.** The accent path is GSettings, not a
file watch: `strings` on the installed `libadwaita-1.so.0` shows
`adw_settings_get_accent_color` and no file-monitor symbols, and
`update_stylesheet()` reloads library *resources* on scheme/contrast change.
So `--accent-color` follows the system accent key live; **colour-scheme does
not** (relaunch needed), and **editing `gtk.css` does not** reload.
`README.md` was right to say startup-only for the file and wrong to leave the
accent asymmetry unstated. [INFERENCE — the accent-key live update is read
off the binary and the upstream docs; it has not been watched by eye with an
app open, which is a one-minute manual check.]

## H4: Equal-specificity custom properties are last-wins — 6 Oct 2026

**Problem, found while building the accent sweep.** Re-pointing the accent
for a test sheet means prepending `:root { --accent-bg-color: X }` before the
overlay. It did nothing: the pinned sheet's own `:root` comes later at equal
specificity and wins. Appending the same rule *after* upstream's `:root` and
before the overlay's worked immediately — the nine-accent sweep in H1 is
built that way.

**Decision.** Recorded because it is invisible and it will bite the next
person who adds a palette layer (the temptation is real: a colourway is the
obvious next feature and this is where it would silently do nothing). Any
future `:root` palette must be appended after L0's aliases, and the sweep
sheets are the reference implementation. No tooling enforces it.

## H6: Packaging — what convention exists, and the one that does not — 6 Oct 2026

Asked: how does this get to other people the way Linux software normally
does. The answer is that **there is no convention to follow for the product
that matters**, and that is a fact about libadwaita rather than about
packaging.

### The convention that does not exist

- libadwaita **1.10** (7 Sep 2026) still has no theme support. GNOME Circle
  #39 ruled that theming "will not affect the appearance of apps that use
  Libadwaita"; libadwaita MR !77 concluded custom stylesheets "may not ever
  be supported". `AdwStyleManager` pins `Adwaita-empty` at
  `PRIORITY_THEME` (200).
- `GTK_THEME` is, per GNOME developers, a debugging knob — "meant for
  testing". It does override the pin, and users report it breaking
  libadwaita layout (padding in Extension Manager). It is not an API.
- **Flatpak has no GTK4 theme extension point.** Only
  `org.gtk.Gtk3theme.*` exists; freedesktop-sdk's platform hardcodes it
  (flatpak#4605, gnome-build-meta#697). The working answer is a filesystem
  override: `--filesystem=xdg-config/gtk-4.0`.

So every libadwaita theme in the wild installs the same unsupported way — by
writing `~/.config/gtk-4.0/gtk.css`. Orchis and Colloid `rm -rf` that path
and the `assets/` beside it.

### Decision: meson, plus a reversible user step

`meson.build` is GNOME's own build system and is what distro packaging
already knows how to drive. `meson install` with DESTDIR produces:

```
<datadir>/themes/Adwaita-overlay/{index.theme,gtk-4.0/gtk.css,gnome-shell/gnome-shell.css}
<bindir>/adwaita-overlay-install
```

The theme directory is the conventional artifact and covers GTK3, plain GTK4
apps, and gnome-shell. The libadwaita front still needs the per-user step,
because a system package must not write into a user's `~/.config` behind
their back — so it ships as `adwaita-overlay-install`, which owns exactly one
marked `@import` line and removes exactly that line on uninstall (verified
byte-exact).

Verified: `DESTDIR=… meson install` stages correctly and the installed
`gtk.css` is byte-identical to the `tools/build` output (same sassc, so the
packaged bytes match a local build).

### The bug this surfaced, which packaging alone would have shipped

Loading the sheet as a theme directory **painted nothing**. Measured: a
button using `var(--window-bg-color, @theme_bg_color)` read `0,0,0` under
`GTK_THEME`, against `246,245,244` through the user-config path.

A theme directory **replaces** GTK's built-in palette, so `@theme_bg_color`,
`@accent_color` and the rest are simply undefined there — the DD3 "painted
nothing" mechanism, this time triggered by our own directory rather than by a
non-libadwaita app. The `--ov-up-*` alias layer (DD3) was written for exactly
this class of failure and had the same blind spot: it fell back to *named*
colours, which are precisely what stops existing.

Fixed with a private fallback palette in L0 (`@define-color ov_stock_*`),
scheme-split. The `ov_` prefix is deliberate: redeclaring `@theme_bg_color` at
priority 800 would shadow what libadwaita or a competing theme publishes.

**Two things got this wrong before it was right**, both measured:

1. Values taken from **libadwaita**'s palette. Wrong: a fallback is only read
   where libadwaita is *absent*, so the palette in force is GTK 4's built-in
   `Default` — whose dark `theme_bg_color` is `#353535` against libadwaita
   light's `#fafafb`. Cost: buttons moved max Δ113/255.
2. `var(--a, var(--b, @fallback))` as a **two-tier** fallback. It does not
   work: when `--b` is invalid at computed-value time GTK propagates the
   invalidity through the outer `var()` rather than taking the third argument
   (measured: `0,0,0`). Single tier only.

Corrected against `Default-light.css` / `Default-dark.css` extracted from
`/usr/lib/libgtk-4.so.1`. The result is **0 px changed across all 15 gallery
families** versus the old adaptive fallback — the fix adds robustness at zero
cost, because in the environment where the fallback is read, `@theme_bg_color`
resolves to exactly the value we now hardcode.

### A withdrawn measurement, and why

This entry originally claimed the two paths differ by "Δ51 on
`button.suggested-action`". **That claim is withdrawn as untrustworthy**, and
the correction matters more than the number did.

The probe behind it ran a plain GTK4 app with no libadwaita. The overlay's CTA
rules deliberately restate upstream's `background-image` — the file's own rule
is that overriding `background-image` *replaces* upstream's value, so surface
rules must restate the upstream stops alongside the overlay's. With upstream
absent there is nothing to restate, so the overlay's CTA contributed only
partial styling and the sampled pixel was never the overlay's material.
Appending a maximally specific `button.suggested-action { background-color }`
to the sheet changed **neither** path's rendered pixel, which is what exposed
the probe as measuring GTK's image layer rather than our cascade.

Verified instead, and all that can honestly be claimed:

- identical bytes load through both mechanisms with **zero GTK parser
  errors** (an earlier draft reported a `--ov-texture-image` parse error in the
  theme-directory path; that came from a test sheet with upstream's and our
  rules concatenated, and does not reproduce with the sheet alone — 0 errors
  both ways);
- both paths paint. The one `0,0,0` the probe reports is the progressbar
  TROUGH, which is unpainted in both paths equally — a probe artifact, not a
  difference between them, and a caution about reading single pixels here at
  all;
- the theme directory is **not** claimed as the supported path for
  libadwaita, which is unaffected — it cannot reach those apps at all.

**Not chased, deliberately.** The remaining difference, if there is one, is
GTK's internal precedence between a named theme and user CSS at 800. It is not
expressible in our CSS, the one lever that might bridge it (`!important`) is
banned by rule 4 on purpose, and upstream states no expectation for a
third-party theme under `GTK_THEME` — so there is no acceptance criterion to
chase toward, and the effort would be unbounded. BACKLOG X9.

### Not done, deliberately

- **Flatpak.** No extension point exists; shipping one would mean an
  override-only flatpak, which is not a distribution method.
- **A real gnome-shell front.** Empty directory and a comment. GNOME 50's
  shell links no GTK at all, shares no vocabulary, exposes no custom
  properties, and `render-gallery` cannot render St.
- **No fork.** Measured across all 19 GitHub forks: none changes libadwaita's
  styling. `libharmonia` diverges by its README alone; `nick-redwill` is 29
  lines in one function and 615 commits stale; `libadapta` is pinned to 1.5
  and its own default branch is named `1.5.x`. For the one capability a fork
  would buy — a *selectable* theme — an unmaintained 29-line patch or the AUR
  `libadwaita-without-adwaita` already exist, and neither is ours to maintain.

## H7: The GTK3 contract is a different kind of contract — 6 Oct 2026

Closing G6's contract half. It could not be a copy of the libadwaita contract,
because the GTK3 front is not the same kind of thing.

`upstream/selectors.txt` guards a sheet that **names** upstream selectors and
hand-writes rules against them, so it lists what our rules depend on. The
GTK3 front (`decisions.md` G3) is a **recompile** of GTK3's own Adwaita plus
libhandy's Adwaita sheets with the accent substituted for the `#3584e4`
literals GTK3 bakes in. There is no hand-authored rule list to guard.

So the two axes ask different questions:

- **`variables.txt` — all 36 `@define-color` names GTK 3.24's Adwaita
  defines.** The front resolves the entire palette, not a subset, so this is
  the whole axis. A rename silently paints the wrong colour.
- **`selectors.txt` — the 434 atoms that carry an accent declaration**, taken
  from `tools/gtk3-accent-sites` (373 declarations, light + dark). Not all
  2001 atoms of GTK3's sheet: that set is not hand-maintainable and would
  describe a dependency we do not have. The 434 are the surface
  `build-gtk3` actually **modifies**.

**The failure this catches is the quiet one.** GTK removes or renames a
selector carrying an accent, `build-gtk3`'s substitution table stops matching,
and one widget keeps `#3584e4` baked in while every other widget follows the
system accent. Nothing crashes; one control is permanently the wrong colour.

**Verified to discriminate**, by doctoring a cached sheet rather than trusting
a green run:

| doctored sheet | reported | exit |
| --- | --- | --- |
| dropped `label selection` rules | `label selection` | 1 |
| renamed `@wm_title` | `wm_title` | 1 |
| dropped bare `entry` rules (unregistered) | nothing | 0 |

That third row is the one that matters: the contract does not simply flag
whatever disappears, so a future entry cannot quietly turn the axis into noise.

**Still open, deliberately.** G6's other half — whether GTK3 apps should get
the material system, not just the accent — is untouched. The GTK3 front is a
recompile of upstream's Adwaita SCSS, so adding material there means forking
that SCSS, which is a different decision from the one this contract guards.

## H8: Building a real package — five bugs the build loop found — 6 Oct 2026

`packaging/arch/{PKGBUILD,adwaita-overlay.install}`. Five defects, none of
which a `meson install` into a temp prefix would have surfaced. All five are
recorded because each one produced a package that **looked successful**.

1. **`meson install` in `build()` cannot work.** makepkg creates `$pkgdir`
   with mode `0111` during the build phase on purpose — `makepkg:1521` runs
   `chmod a-srw` — so a build cannot write into the package directory; it is
   restored to `755` at `makepkg:1374`, just before packaging. So the install
   belongs in `package()`.
2. **makepkg 7.x does not export `DESTDIR` for `package()`.** Without it
   explicitly set, meson wrote into the real `/usr`. It has to be passed:
   `DESTDIR="$pkgdir" meson install -C build`.
3. **`--prefix="$pkgdir/usr"` doubles the path.** meson prepends `DESTDIR` to
   the configured prefix, so the whole tree stages under `$pkgdir/$pkgdir/usr`
   — an empty-looking package that reports success. `--prefix=/usr` and let
   `DESTDIR` do the staging.
4. **`$srcdir` is the extraction root, not the package directory.** With a
   versioned wrapper directory in the tarball, `srcdir` is `src/` and the
   sources are at `src/$pkgname-$pkgver/`. Fixed the durable way: a **flat**
   tarball, which is also the current Arch convention.
5. **The theme directory is not the package name.** `meson.build` installs to
   `Adwaita-overlay` (capitalised, because that is a GTK theme name) while the
   package is `adwaita-overlay`. Two namespaces. Caught by `package()`'s
   byte-comparison, which failed on a path that did not exist.

And one the *packaging test* found that no unit of the build would have:

6. **`tools/install` could not find its own sheet once installed.** It derived
   the repo root from `dirname $0`, which is right from `tools/` in the source
   tree and wrong the moment the package installs the script as
   `/usr/bin/adwaita-overlay-install` — it resolved "repo" to `/usr` and tried
   to copy `/usr/build/gtk.css`, so **the packaged install command failed
   outright**. Found by installing the package and running the installed
   binary rather than the one in the tree. It now probes, in order:
   `$ADWAITA_OVERLAY_SHEET`, `../build/`, `../share/themes/Adwaita-overlay/…`,
   `/usr/share/…`, `/usr/local/share/…`.

### What the package deliberately does NOT do

No post-install writes to the user's config. The GTK4 half cannot reach
libadwaita apps (H6), and the only route is `~/.config/gtk-4.0/gtk.css`,
which belongs to the user — so `.install` prints instructions and
`adwaita-overlay-install` does the work when they ask. A package that
silently rewrites a config file to make itself work is a package that will
clobber something.

`conflicts=` cannot express the real constraint either: only one tool can own
`gtk-4.0/gtk.css`, and `pacman` has no way to know about
`adw-colors`/`Gradience`/`Orchis -l`. So `.install` says it in words.

### Verified from the built package, not the tree

`makepkg` output unpacked to a fake sysroot, then:

| check | result |
| --- | --- |
| payload | `usr/share/themes/Adwaita-overlay/{index.theme,gtk-4.0/gtk.css,gnome-shell/gnome-shell.css}` + `usr/bin/adwaita-overlay-install` |
| packaged sheet vs `tools/build` | **byte-identical** |
| theme dir via `GTK_THEME` from the sysroot | loads, **0 parser errors** |
| installed installer, sandboxed | adds one marked `@import`, preserves the user's own rules and `assets/` |
| theme actually applied | CTA centre `2,33,71` against stock `148,179,216` |
| uninstall | user's `gtk.css` restored **byte for byte**, sidecar removed |

`check()` runs inside `makepkg` and refuses to finish if `tools/check-selectors`
fails against the installed libadwaita/gtk3, so the package cannot be built
against a host whose selectors it depends on have moved.
