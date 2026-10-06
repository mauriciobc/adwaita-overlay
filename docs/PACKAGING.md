<!--
SPDX-FileCopyrightText: 2026 mauriciobc
SPDX-License-Identifier: LGPL-2.1-or-later
-->

# Packaging adwaita-overlay

Build system: **meson**. Release artifact: the `meson dist` tarball
(`adwaita-overlay-<version>.tar.xz`, wrapped in `adwaita-overlay-<version>/`).

```
meson setup build --prefix=/usr [--libexecdir=lib] [-Dpacman_hook=true]
meson compile -C build
meson test -C build
DESTDIR="$pkgdir" meson install -C build
```

`--libexecdir=lib` matches distros that keep helpers in `/usr/lib` (Arch);
the default is `libexec`. `-Dpacman_hook=true` is for Arch and derivatives
only; other distros leave it off (default).

## Dependencies

- **Build:** `meson`, `sassc` (libsass). `dart-sass` compatibility is
  unverified: the sheet has only ever been compiled with `sassc`, and the
  installed bytes are expected to match a local `tools/build`.
  `coreutils`. The sheet itself needs nothing.
- **Check (`meson test` and the forward contract axis):** the installed
  `libadwaita`. `meson test` alone (the reverse axis) needs no libadwaita.

## What gets installed

```
<datadir>/themes/Adwaita-overlay/index.theme
<datadir>/themes/Adwaita-overlay/gtk-4.0/gtk.css
<datadir>/themes/Adwaita-overlay/gnome-shell/gnome-shell.css   (empty stylesheet)
<bindir>/adwaita-overlay-install
<libexecdir>/adwaita-overlay/check-selectors
<libexecdir>/adwaita-overlay/_lib.sh
<libexecdir>/adwaita-overlay/contracts/{selectors,variables}.txt
<prefix>/share/libalpm/hooks/adwaita-overlay.hook              (-Dpacman_hook=true)
```

The three files under `<libexecdir>/adwaita-overlay/` must stay together:
the checker finds its data next to itself.

## Rules

- A package **MUST NOT** touch `~/.config` (no scriptlet may run
  `adwaita-overlay-install`). The per-user step is the user's, and it is the
  only way to reach libadwaita apps: libadwaita ignores the `gtk-theme`
  setting, so the installed theme directory does not reach them.
- Tell users to run `adwaita-overlay-install --update` after a package
  upgrade: the installer copies the sheet into the user's config, and that
  copy goes stale. The installer warns when it is out of date.
- Ship the Arch `check()` equivalent if your distro has one: the forward axis
  (`tools/check-selectors --sheet build/gtk.css`) fails when libadwaita no
  longer provides something the sheet depends on.

## Status

An Arch package exists in `packaging/arch/` (`PKGBUILD`,
`adwaita-overlay.install`). **No RPM, Debian or Nix recipes exist yet.**
The GTK3 front is a development tool and is not packaged.
