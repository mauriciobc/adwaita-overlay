<!--
SPDX-FileCopyrightText: 2026 mauriciobc
SPDX-License-Identifier: LGPL-2.1-or-later
-->

# Contributing

By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Build and check

```
tools/build                  # sassc src/overlay.scss -> build/gtk.css (also rewires
                             # ~/.config/gtk-4.0/gtk.css; see docs/HACKING.md)
sassc src/overlay.scss build/gtk.css   # compile only, touches nothing else
tools/check-selectors        # contracts vs installed libadwaita, + reverse axis
meson setup _build && meson test -C _build
```

Read [docs/HACKING.md](docs/HACKING.md) and [docs/decisions.md](docs/decisions.md)
before touching the L0 tokens (`src/_tokens.scss`): every upstream variable is
read through exactly one `--ov-up-*` alias there, and registered in
`upstream/variables.txt` in the same commit.

## Commits

One imperative first line, as in `git log` (for example
`Buttons: convex face, neon hover, 0.5px rim`), then a body that says why.

## Licensing

Contributions are licensed LGPL-2.1-or-later. New files carry an SPDX header:

```
SPDX-FileCopyrightText: <year> <your name>
SPDX-License-Identifier: LGPL-2.1-or-later
```

`reuse lint` must pass.
