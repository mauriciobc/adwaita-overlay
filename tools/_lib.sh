#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mauriciobc
# SPDX-License-Identifier: LGPL-2.1-or-later
# Shared helpers for the adwaita-overlay tools. Sourced, never executed.

# extract_sheet <libadwaita.so> — print the compiled stylesheet to stdout.
# Handles both resource layouts:
#   1.9+    a single /org/gnome/Adwaita/styles/gtk.css
#   <= 1.6  four files (base / defaults-light / defaults-dark / base-hc),
#           concatenated — cascade order is irrelevant for the presence
#           checks the guard runs.
extract_sheet() {
  local so="$1" p
  local paths
  paths="$(gresource list "$so" 2>/dev/null | grep '^/org/gnome/Adwaita/styles/.*\.css$' || true)"
  if [[ -z "$paths" ]]; then
    echo "error: no stylesheets found in $so" >&2
    return 1
  fi
  if grep -qx '/org/gnome/Adwaita/styles/gtk.css' <<<"$paths"; then
    gresource extract "$so" /org/gnome/Adwaita/styles/gtk.css
    return 0
  fi
  for p in base.css defaults-light.css defaults-dark.css base-hc.css; do
    grep -qx "/org/gnome/Adwaita/styles/$p" <<<"$paths" || continue
    gresource extract "$so" "/org/gnome/Adwaita/styles/$p"
  done
}

# extract_selectors <gtk.css> — print the sorted set of selector atoms.
# Atom-level: "button, entry" yields two entries. @media preludes are
# stripped so nested rules are counted; @define-color lines dropped.
# It is a heuristic, but the same heuristic everywhere, which is all a
# diff needs.
extract_selectors() {
  sed 's/@media[^{]*{//g' "$1" \
    | grep -v '@define-color' \
    | tr '{' '\n' | grep -v '}' \
    | tr ',' '\n' | sed 's/^ *//;s/ *$//' \
    | grep -v '^$' | sort -u
}

# extract_variables <gtk.css> — print the sorted set of custom properties
# that are *defined* in the sheet (not merely referenced via var()).
extract_variables() {
  grep -oE -- '--[a-z0-9][a-z0-9-]*:' "$1" | tr -d ':' | sort -u
}

# extract_var_refs <gtk.css> — print the sorted set of custom properties the
# sheet READS via var(), leading dash runs normalised to exactly two.
# The normalisation is what the contract file already does by hand:
# libadwaita spells one property with three dashes (---slider-border-color,
# defined on `scale`) and tools/check-selectors matches it from the second
# dash on, so upstream/variables.txt lists the two-dash form.
extract_var_refs() {
  grep -oE -- 'var\(\s*-{2,}[a-z0-9][a-z0-9-]*' "$1" \
    | sed -E 's/var\(\s*//; s/^-{2,}/--/' | sort -u
}

# extract_declared_props <gtk.css> — print the sorted set of custom properties
# the sheet DEFINES, all --ov-* by construction (L0 is the only layer that
# declares, and it declares nothing else).
# Not anchored to line start: a declaration may follow "{" on the same line,
# so matching on the "--ov-x:" shape alone is both simpler and stricter.
# Comments are stripped first so a commented-out token is not counted.
extract_declared_props() {
  sed 's:/\*.*\*/::g' "$1" | grep -oE -- '--ov-[a-z0-9-]+[[:space:]]*:' \
    | grep -oE -- '--ov-[a-z0-9-]+' | sort -u
}

# extract_sheet_gtk3 <libgtk-3.so> — print GTK3's built-in Adwaita sheet,
# light then dark (gtk-contained{,-dark}.css). Concatenated: the presence
# checks do not care about cascade order.
extract_sheet_gtk3() {
  local so="$1" p
  for p in gtk-contained.css gtk-contained-dark.css; do
    gresource extract "$so" "/org/gtk/libgtk/theme/Adwaita/$p" || return 1
  done
}

# extract_named_colors <gtk.css> — print the sorted set of @define-color
# names the sheet defines. GTK3's variable axis: no custom properties there.
extract_named_colors() {
  grep -oE '@define-color [a-z_][a-z0-9_]*' "$1" | cut -d' ' -f2 | sort -u
}

# gtk3_tag <pacman version> — the upstream git tag for a gtk3 package
# version: 1:3.24.52-1 -> 3.24.52.
gtk3_tag() {
  local v="${1#*:}"
  echo "${v%-*}"
}

# same_modulo_order <a.css> <b.css> — true when both have the same line
# count and every differing line pair holds the same characters (a
# reordered compound selector). Anything else is a real difference.
same_modulo_order() {
  python3 - "$1" "$2" <<'EOF'
import sys
a, b = (open(p).read().splitlines() for p in sys.argv[1:3])
sys.exit(0 if len(a) == len(b) and all(
    x == y or sorted(x) == sorted(y) for x, y in zip(a, b)) else 1)
EOF
}
