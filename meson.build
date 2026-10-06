# SPDX-FileCopyrightText: 2026 mauriciobc
# SPDX-License-Identifier: LGPL-2.1-or-later
# adwaita-overlay — meson build
#
# GNOME's own build system, used here for one reason: `meson install` is what
# every distro packaging already knows how to drive, with DESTDIR support and
# no per-distro glue. The development loop is still sassc via tools/build;
# this file exists so the *installed* artifact is produced the conventional
# way rather than by a shell script each distro has to reimplement.
#
#   meson setup _build
#   meson compile -C _build
#   meson install -C _build
#   DESTDIR=/tmp/stage meson install -C _build     # what a packager does
#
# WHAT LANDS WHERE
#
#   <datadir>/themes/Adwaita-overlay/
#       index.theme
#       gtk-4.0/gtk.css        <- the overlay
#       gnome-shell/gnome-shell.css
#   <bindir>/adwaita-overlay-install
#   <libexecdir>/adwaita-overlay/{check-selectors,_lib.sh,contracts/*.txt}
#   <prefix>/share/libalpm/hooks/adwaita-overlay.hook   (-Dpacman_hook=true)
#
# THE PART NO DISTRO PACKAGING WILL FIX
#
# A theme directory does not reach libadwaita. AdwStyleManager pins
# `Adwaita-empty` and loads its own sheet at GTK_STYLE_PROVIDER_PRIORITY_THEME
# (200); GNOME Circle #39 ruled that theming "will not affect the appearance of
# apps that use Libadwaita", and libadwaita MR !77 concluded custom
# stylesheets "may not ever be supported". `gtk-theme` is a testing knob, not
# a theming API — setting it globally is reported to break libadwaita layout.
#
# So the theme directory covers non-libadwaita GTK4 apps and, with the
# user-theme extension, gnome-shell (currently an empty stylesheet). For
# libadwaita itself the only path is the
# per-user stylesheet at $XDG_CONFIG_HOME/gtk-4.0/, which GTK loads at
# GTK_STYLE_PROVIDER_PRIORITY_USER (800) — above the 200 the library uses.
# That step is a USER ACTION, not a system one, because it writes into a
# user's config directory and a system package must not do that behind their
# back. `adwaita-overlay-install` does it, reversibly.
#
# The same sheet serves both jobs: in a libadwaita app its tokens resolve from
# the library's custom properties; in any other GTK4 process every read has a
# GTK named-colour fallback, which is what `probe-foreign` exists to verify.

project(
  'adwaita-overlay',
  version: '1.0.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.62.0',
)

# One compiler for the dev loop and for the package: sassc is what upstream
# and Arch both use, so the installed bytes match a local `tools/build`.
sassc = find_program('sassc', required: true)

datadir = get_option('datadir')
bindir = get_option('bindir')
themesdir = join_paths(datadir, 'themes')
themedir = join_paths(themesdir, 'Adwaita-overlay')

# The sheet. One file for both colour schemes — it branches on
# prefers-color-scheme internally — so there is no gtk-dark.css to keep in
# sync the way a baked-palette theme needs.
gtk4_css = custom_target(
  'gtk4-css',
  input: files('src/overlay.scss'),
  output: 'gtk.css',
  command: [sassc, '@INPUT@', '@OUTPUT@'],
  build_by_default: true,
  install: true,
  install_dir: join_paths(themedir, 'gtk-4.0'),
)

install_data(
  files('index.theme'),
  install_dir: themedir,
)

# gnome-shell front. Deliberately a comment and nothing else: GNOME 50's shell
# links no GTK at all (verified: zero libgtk-3/libgtk-4/libadwaita), so the
# shell is a St/Clutter surface that shares no widget vocabulary, no custom
# properties to inherit, and no renderer to verify against. Shipping the
# directory keeps the front discoverable and selectable via the user-theme
# extension without pretending it is implemented.
install_data(
  files('share/gnome-shell/gnome-shell.css'),
  install_dir: join_paths(themedir, 'gnome-shell'),
)

# The reversible user-config step. Installed because it is the only way to
# reach libadwaita apps, and kept out of any post-install script: writing to a
# user's ~/.config is theirs to trigger.
install_data(
  files('tools/install'),
  install_dir: bindir,
  install_mode: 'rwxr-xr-x',
  rename: ['adwaita-overlay-install'],
)

# Contract checker. Installed under libexecdir (not bindir): it is a helper
# for the pacman hook and for the curious, not a user command. It finds its
# data next to itself, so the three pieces must stay in one directory.
helperdir = get_option('prefix') / get_option('libexecdir') / 'adwaita-overlay'
install_data(files('tools/check-selectors'),
  install_dir: helperdir, install_mode: 'rwxr-xr-x')
install_data(files('tools/_lib.sh'),
  install_dir: helperdir, install_mode: 'rw-r--r--')
install_data(files('upstream/selectors.txt', 'upstream/variables.txt'),
  install_dir: helperdir / 'contracts')

# Hermetic reverse axis: built sheet vs. registered upstream variables. Needs
# no installed libadwaita, so it runs in any distro build chroot.
test('reverse-contract', find_program('bash'),
  args: [files('tools/check-selectors'), '--reverse', '--sheet', gtk4_css])

# Pacman hook (Arch and derivatives). Off by default: other distros have
# their own trigger mechanisms and a stray /usr/share/libalpm is noise there.
installed_sheet = get_option('prefix') / themedir / 'gtk-4.0' / 'gtk.css'
configure_file(
  input: 'hooks/adwaita-overlay.hook.in',
  output: 'adwaita-overlay.hook',
  configuration: {
    'CHECKER': helperdir / 'check-selectors',
    'SHEET': installed_sheet,
  },
  install: get_option('pacman_hook'),
  install_dir: get_option('prefix') / 'share' / 'libalpm' / 'hooks',
)

summary({
  'theme directory': themedir,
  'pacman hook': get_option('pacman_hook'),
  'libadwaita': 'needs adwaita-overlay-install (per-user)',
}, section: 'adwaita-overlay')