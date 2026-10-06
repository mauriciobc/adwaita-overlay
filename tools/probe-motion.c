/*
 * SPDX-FileCopyrightText: 2026 mauriciobc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* tools/probe-motion.c — measure what the overlay's transitions actually do.
 *
 *   probe-motion <css-file> [sample-ms] [upstream-css]
 *
 * Loads one CSS file at GTK_STYLE_PROVIDER_PRIORITY_USER (800) — same
 * mechanism as the overlay and as render-widget — and, when given, a
 * second file at PRIORITY_THEME (200): pass upstream/cache/<pinned>/gtk.css
 * to stand in for libadwaita, without which every surface derived from an
 * upstream variable is invalid and nothing measures. Then walks a real
 * button and a real `list.boxed-list` row through :hover, :active and
 * :focus-visible — and a headerbar through :backdrop — by setting state
 * flags (what GTK Inspector does) and samples the rendered pixels:
 * at rest, `sample-ms` and `2 * sample-ms` into the state change, and once
 * settled. Two samples because a state change lands on the frame clock: a
 * slow start must not read as "no change", and an instant change must read
 * as such at both samples.
 *
 * Verdict per probe: IN MOTION when either sample differs from both ends,
 * INSTANT when both are already at the settled rendering, NO CHANGE when
 * neither moved. That is the observable difference between house motion,
 * reduced motion (env REDUCE=1) and GTK's own animations-off (env
 * NOANIM=1); CONTRAST=more and SCHEME=dark mirror render-widget.
 *
 * Born in the 23 Sep 2026 motion review (docs/decisions.md, E6).
 *
 * Build:
 *   gcc -O1 -o build/probe-motion tools/probe-motion.c \
 *       $(pkg-config --cflags --libs gtk4)
 */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int r, g, b, a;
} Pixel;

/* GTK loads $XDG_CONFIG_HOME/gtk-4.0/gtk.css for every process, and on a
 * machine that has installed this overlay that path is a symlink to the
 * very sheet under test: the css argument would be loaded twice over, so
 * an A/B against "nothing" is really an A/B against the overlay. Point
 * XDG_CONFIG_HOME at an empty directory so the css arguments are the only
 * stylesheets in the process. KEEP_CONFIG=1 restores the real
 * environment. libadwaita's own sheet still applies (it comes from the
 * theme search path, not from the user config), which is why
 * upstream-css stays optional. */
static void
hermetic_config (void)
{
  gchar *dir;
  if (g_getenv ("KEEP_CONFIG") != NULL)
    return;
  dir = g_dir_make_tmp ("probe-motion-XXXXXX", NULL);
  if (dir != NULL) {
    g_setenv ("XDG_CONFIG_HOME", dir, TRUE);
    g_free (dir);
  }
}

static int
distance (Pixel a, Pixel b)
{
  return ABS (a.r - b.r) + ABS (a.g - b.g) + ABS (a.b - b.b) + ABS (a.a - b.a);
}

/* Anything at or under this distance reads as "the same rendering". */
#define SAME 3

/* Mean RGBA over the whole widget. A state change on these surfaces moves
 * a wash, a glow, a bevel or a well across the whole node, so the mean is
 * both stable and comparable between the three samples (per-pixel point
 * picking is not: the pixel that moves most changes between samples). */
static Pixel
sample (GtkWidget *w)
{
  Pixel p = { -1, -1, -1, -1 };
  int width = gtk_widget_get_width (w), height = gtk_widget_get_height (w);
  if (width <= 0 || height <= 0)
    return p;

  GdkPaintable *paintable = gtk_widget_paintable_new (w);
  GtkSnapshot *snap = gtk_snapshot_new ();
  gdk_paintable_snapshot (paintable, snap, width, height);
  GskRenderNode *node = gtk_snapshot_to_node (snap);
  if (node == NULL) {
    g_object_unref (snap);
    g_object_unref (paintable);
    return p;
  }
  GskRenderer *renderer = gsk_cairo_renderer_new ();
  gsk_renderer_realize (renderer, NULL, NULL);
  GdkTexture *texture = gsk_renderer_render_texture (renderer, node, NULL);

  gsize stride = 4 * width;
  guchar *data = g_malloc (stride * height);
  gdk_texture_download (texture, data, stride);

  guint64 r = 0, g = 0, b = 0, a = 0;
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++) {
      const guchar *px = &data[y * stride + x * 4];
      r += px[0]; g += px[1]; b += px[2]; a += px[3];
    }
  guint64 n = (guint64) width * height;
  p.r = r / n; p.g = g / n; p.b = b / n; p.a = a / n;

  g_free (data);
  g_object_unref (texture);
  gsk_render_node_unref (node);
  gsk_renderer_unrealize (renderer);
  g_object_unref (renderer);
  g_object_unref (snap);
  g_object_unref (paintable);
  return p;
}

/* Re-assert the exact state mask, then sample. GTK re-evaluates
 * PRELIGHT/FOCUS/BACKDROP from the real pointer and window focus, so a
 * manually set flag can be clobbered mid-probe; asserting immediately
 * before the sample (no main-loop iteration in between) is what makes the
 * four numbers comparable run to run. */
static Pixel
sample_in_state (GtkWidget *w, GtkStateFlags flags)
{
  gtk_widget_set_state_flags (w, flags, TRUE);
  return sample (w);
}

static void
pump (gint64 microseconds)
{
  gint64 end = g_get_monotonic_time () + microseconds;
  while (g_get_monotonic_time () < end)
    g_main_context_iteration (NULL, FALSE);
}

/* Walk one widget through one state change; report four means. */
static void
run_probe (const char *name, GtkWidget *w, GtkStateFlags flag,
           gint64 sample_us)
{
  Pixel rest, early, mid, settled;

  /* The rest sample clears every state flag, not just this probe's: the
   * window hands the initial keyboard focus to the first focusable widget,
   * and a focused button would otherwise colour it. */
  sample_in_state (w, 0);
  pump (600000);
  rest = sample_in_state (w, 0);

  sample_in_state (w, flag);
  pump (sample_us);
  early = sample_in_state (w, flag);
  pump (sample_us);
  mid = sample_in_state (w, flag);
  pump (800000);
  settled = sample_in_state (w, flag);
  gtk_widget_set_state_flags (w, 0, TRUE);
  pump (600000);

  /* Two samples, because a state change lands on the frame clock: a slow
   * start (the focus ring begins a frame late) must not read as "no
   * change", and an instant change must read as such at both samples. */
  const char *verdict =
    distance (early, settled) <= SAME && distance (mid, settled) <= SAME
      ? "INSTANT (state landed)"
      : distance (early, rest) <= SAME && distance (mid, rest) <= SAME
        ? "NO CHANGE"
        : "IN MOTION";

  g_print ("%-20s rest=%3d,%3d,%3d,%3d  @%-3lldms=%3d,%3d,%3d,%3d  "
           "@%-3lldms=%3d,%3d,%3d,%3d  settled=%3d,%3d,%3d,%3d  -> %s\n",
           name,
           rest.r, rest.g, rest.b, rest.a,
           (long long) (sample_us / 1000), early.r, early.g, early.b, early.a,
           (long long) (2 * sample_us / 1000), mid.r, mid.g, mid.b, mid.a,
           settled.r, settled.g, settled.b, settled.a,
           verdict);
}

int
main (int argc, char **argv)
{
  if (argc < 2) {
    g_printerr ("usage: %s css [sample-ms]\n", argv[0]);
    return 2;
  }
  hermetic_config ();
  gtk_init ();
  gint64 sample_us = (argc > 2 ? atol (argv[2]) : 80) * 1000;

  if (g_getenv ("NOANIM"))
    g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL);

  GtkCssProvider *provider = gtk_css_provider_new ();
  const char *contrast = g_getenv ("CONTRAST");
  if (g_strcmp0 (contrast, "more") == 0)
    g_object_set (provider, "prefers-contrast", GTK_INTERFACE_CONTRAST_MORE, NULL);
  if (g_strcmp0 (g_getenv ("REDUCE"), "1") == 0)
    g_object_set (provider, "prefers-reduced-motion",
                  GTK_REDUCED_MOTION_REDUCE, NULL);
  /* Scheme goes on GtkSettings, not the provider: the upstream palette
   * (e.g. --headerbar-bg-color) is defined inside the theme's own
   * scheme media queries, so a provider-only override would mix our dark
   * rules with the light theme's colours. */
  const char *scheme = g_getenv ("SCHEME");
  if (g_strcmp0 (scheme, "dark") == 0)
    g_object_set (gtk_settings_get_default (), "gtk-interface-color-scheme",
                  GTK_INTERFACE_COLOR_SCHEME_DARK, NULL);
  gtk_css_provider_load_from_path (provider, argv[1]);
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_USER);
  g_object_unref (provider);

  if (argc > 3) {
    GtkCssProvider *theme = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (theme, argv[3]);
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (theme),
                                                GTK_STYLE_PROVIDER_PRIORITY_THEME);
    g_object_unref (theme);
  }

  GtkWidget *win = gtk_window_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_window_set_child (GTK_WINDOW (win), box);

  GtkWidget *bar = gtk_header_bar_new ();
  gtk_box_append (GTK_BOX (box), bar);

  GtkWidget *button = gtk_button_new_with_label ("Test");
  gtk_widget_set_size_request (button, 160, 44);
  gtk_box_append (GTK_BOX (box), button);

  GtkWidget *list = gtk_list_box_new ();
  gtk_widget_add_css_class (list, "boxed-list");
  for (int i = 0; i < 2; i++) {
    GtkWidget *row = gtk_list_box_row_new ();
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row),
                                gtk_label_new ("A list row"));
    gtk_list_box_append (GTK_LIST_BOX (list), row);
  }
  gtk_box_append (GTK_BOX (box), list);
  GtkWidget *row = GTK_WIDGET (gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0));

  gtk_window_present (GTK_WINDOW (win));
  gtk_window_set_focus (GTK_WINDOW (win), NULL);
  pump (600000);

  g_print ("css: %s%s   sample: %lldms   REDUCE=%s NOANIM=%s CONTRAST=%s SCHEME=%s\n",
           argv[1], argc > 3 ? " + theme" : "", (long long) (sample_us / 1000),
           g_getenv ("REDUCE") ?: "0", g_getenv ("NOANIM") ?: "0",
           contrast ?: "normal", scheme ?: "light");
  run_probe ("button:hover", button, GTK_STATE_FLAG_PRELIGHT, sample_us);
  run_probe ("button:active", button, GTK_STATE_FLAG_ACTIVE, sample_us);
  run_probe ("boxed-list row:hover", row, GTK_STATE_FLAG_PRELIGHT, sample_us);
  run_probe ("button:focus-visible", button,
             GTK_STATE_FLAG_FOCUSED | GTK_STATE_FLAG_FOCUS_VISIBLE, sample_us);
  run_probe ("headerbar:backdrop", bar, GTK_STATE_FLAG_BACKDROP, sample_us);
  return 0;
}
