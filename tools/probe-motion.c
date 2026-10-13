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
 * upstream variable is invalid and nothing measures. Then walks a table of
 * real widgets through one state change each — by setting state flags
 * (what GTK Inspector does), flipping `sensitive`, toggling a CSS class or
 * flipping `active` — and samples the rendered pixels: at rest, `sample-ms`
 * and `2 * sample-ms` into the state change, and once settled. Two samples
 * because a state change lands on the frame clock: a slow start must not
 * read as "no change", and an instant change must read as such at both
 * samples.
 *
 * Verdict per probe: IN MOTION when either sample differs from both ends,
 * INSTANT when both are already at the settled rendering, NO CHANGE when
 * neither moved. That is the observable difference between house motion,
 * reduced motion (env REDUCE=1) and GTK's own animations-off (env
 * NOANIM=1); CONTRAST=more and SCHEME=dark mirror render-widget.
 *
 * What is covered is the probe table in main(): every family whose state
 * the overlay's L2 selectors touch. Libadwaita-only nodes (toggle-group,
 * splitbutton, tabbar/tab) are real GTK widgets given the same CSS node
 * names — CSS matches on node names, classes and state flags, so the
 * stand-ins exercise the same rules without linking libadwaita. What
 * genuinely cannot be reached is listed as NOT PROBED with its reason at
 * the end of the run, never skipped silently.
 *
 * env PROBE=<substring> runs only the probes whose name contains it.
 * env EMPTY_THEME=1 replaces GTK's built-in theme with an empty one, as
 * libadwaita does (see setup_empty_theme); off by default.
 *
 * Headless: GTK needs a display. On a desktop session the window shows
 * briefly; for a quiet, deterministic run use a virtual X server:
 *   GDK_BACKEND=x11 xvfb-run -a build/probe-motion build/gtk.css 80 \
 *       upstream/cache/1:1.9.4-1/gtk.css
 *
 * Born in the 23 Sep 2026 motion review (docs/decisions.md, E6); made
 * table-driven in the motion audit.
 *
 * Build:
 *   gcc -O1 -Wall -Wextra -o build/probe-motion tools/probe-motion.c \
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

/* libadwaita pins `Adwaita-empty` at PRIORITY_THEME, so in a libadwaita app
 * GTK's own built-in theme is gone and the upstream sheet is the only
 * theme. A plain-GTK process still carries the built-in one underneath
 * the upstream-css argument — for a button that is the opaque
 * `background-image` gradient, which paints over `background-color` and
 * changes what every button probe measures. env EMPTY_THEME=1 installs an
 * empty theme directory (in the same throw-away tree) and selects it, the
 * same replacement libadwaita makes. The one thing the real stylesheet
 * gets from outside itself is the accent: AdwStyleManager injects
 * @accent_bg_color / @accent_fg_color at runtime and the pinned sheet only
 * references them, so the theme defines GNOME's stock blue pair — without
 * it every accent fill (checked check/radio/switch, selected rows) is an
 * invalid declaration and paints nothing. Off by default so the numbers
 * stay comparable with every earlier run of this probe. */
static gboolean empty_theme;

static void
setup_empty_theme (void)
{
  gchar *dir, *themedir;
  if (g_getenv ("EMPTY_THEME") == NULL)
    return;
  dir = g_dir_make_tmp ("probe-motion-theme-XXXXXX", NULL);
  if (dir == NULL)
    return;
  themedir = g_build_filename (dir, "themes", "ov-probe-empty", "gtk-4.0", NULL);
  if (g_mkdir_with_parents (themedir, 0700) == 0) {
    gchar *css = g_build_filename (themedir, "gtk.css", NULL);
    static const char accent[] =
      "@define-color accent_bg_color #3584e4;\n"
      "@define-color accent_fg_color #ffffff;\n";
    if (g_file_set_contents (css, accent, sizeof accent - 1, NULL)) {
      g_setenv ("XDG_DATA_HOME", dir, TRUE);
      empty_theme = TRUE;
    }
    g_free (css);
  }
  g_free (themedir);
  g_free (dir);
}

static int
distance (Pixel a, Pixel b)
{
  return ABS (a.r - b.r) + ABS (a.g - b.g) + ABS (a.b - b.b) + ABS (a.a - b.a);
}

/* Anything at or under this distance reads as "the same rendering". */
#define SAME 3

/* One measurement: the mean, plus the pixels it came from. */
typedef struct {
  Pixel mean;
  guchar *px;       /* RGBA, `stride` bytes per row; NULL when unmeasurable */
  int width, height;
} Frame;

static void
frame_clear (Frame *f)
{
  g_free (f->px);
  f->px = NULL;
}

static void
discard (Frame f)
{
  frame_clear (&f);
}

/* Mean per-pixel L1 distance between two frames (RGBA channels summed):
 * 0 for identical renderings. The mean above cannot see a state that only
 * touches a few pixels of a small node — a knob's halo, a hairline — so a
 * probe whose means agree is re-judged on this before it is called
 * NO CHANGE. */
static double
energy (const Frame *a, const Frame *b)
{
  if (a->px == NULL || b->px == NULL || a->width != b->width ||
      a->height != b->height)
    return 0;
  guint64 sum = 0;
  gsize n = (gsize) 4 * a->width * a->height;
  for (gsize i = 0; i < n; i++)
    sum += ABS ((int) a->px[i] - (int) b->px[i]);
  return (double) sum / ((double) a->width * a->height);
}

/* Below this, two renderings are the same picture (renderings are
 * deterministic: identical states give exactly 0). */
#define ENERGY_SAME 0.1

/* Mean RGBA over the whole widget. A state change on these surfaces moves
 * a wash, a glow, a bevel or a well across the whole node, so the mean is
 * both stable and comparable between the three samples (per-pixel point
 * picking is not: the pixel that moves most changes between samples).
 * `pad` widens the measured region by that many pixels on every side, for
 * a small node whose state is an outset halo or drop (a scale knob): the
 * widget's own box would clip it away. 0 = the box exactly. */
static Frame
sample (GtkWidget *w, int pad)
{
  Frame f = { { -1, -1, -1, -1 }, NULL, 0, 0 };
  int width = gtk_widget_get_width (w) + 2 * pad;
  int height = gtk_widget_get_height (w) + 2 * pad;
  if (width <= 2 * pad || height <= 2 * pad)
    return f;
  int wbox = width - 2 * pad, hbox = height - 2 * pad;
  gsize stride = 4 * width;
  f.width = width;
  f.height = height;
  f.px = g_malloc0 (stride * height);

  GdkPaintable *paintable = gtk_widget_paintable_new (w);
  GtkSnapshot *snap = gtk_snapshot_new ();
  gdk_paintable_snapshot (paintable, snap, wbox, hbox);
  GskRenderNode *node = gtk_snapshot_to_node (snap);
  /* A node that draws nothing (a bare separator) is transparent, not an
   * error: all-zero pixels, so a state that paints something still moves. */
  if (node != NULL) {
    GskRenderer *renderer = gsk_cairo_renderer_new ();
    gsk_renderer_realize (renderer, NULL, NULL);
    /* An explicit viewport: without one the texture takes the render
     * node's bounds, and any outset shadow, glow or outline (a checked
     * toggle's drop, a focus ring) makes it wider than the widget, so the
     * download below would read past its stride. The measured region is
     * the widget's own box (plus `pad`). */
    graphene_rect_t viewport = GRAPHENE_RECT_INIT (-pad, -pad, width, height);
    GdkTexture *texture = gsk_renderer_render_texture (renderer, node, &viewport);
    gdk_texture_download (texture, f.px, stride);
    g_object_unref (texture);
    gsk_render_node_unref (node);
    gsk_renderer_unrealize (renderer);
    g_object_unref (renderer);
  }

  guint64 r = 0, g = 0, b = 0, a = 0;
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++) {
      const guchar *px = &f.px[y * stride + x * 4];
      r += px[0]; g += px[1]; b += px[2]; a += px[3];
    }
  guint64 n = (guint64) width * height;
  f.mean.r = r / n; f.mean.g = g / n; f.mean.b = b / n; f.mean.a = a / n;

  g_object_unref (snap);
  g_object_unref (paintable);
  return f;
}

static void
pump (gint64 microseconds)
{
  gint64 end = g_get_monotonic_time () + microseconds;
  while (g_get_monotonic_time () < end)
    g_main_context_iteration (NULL, FALSE);
}

/* ---- CSS-name stand-ins for libadwaita nodes ----------------------------
 * A subclass that only renames the CSS node. libadwaita's own sheet (or the
 * pinned upstream copy) and the overlay select on node names, classes and
 * state flags, so a GtkBox named `toggle-group` meets exactly the rules
 * AdwToggleGroup meets. Internal structure beyond the named nodes (the
 * real widgets' extra wrapper nodes) is not reproduced. */
#define OV_CSS_TYPE(Name, name, ParentCType, parent_gtype, css)            \
  typedef struct { ParentCType parent; } Name;                            \
  typedef struct { ParentCType##Class parent_class; } Name##Class;        \
  G_DEFINE_TYPE (Name, name, parent_gtype)                                \
  static void name##_class_init (Name##Class *klass)                      \
  { gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), css); }      \
  static void name##_init (Name *self) { (void) self; }

OV_CSS_TYPE (OvToggleGroup, ov_toggle_group, GtkBox, GTK_TYPE_BOX, "toggle-group")
OV_CSS_TYPE (OvToggle, ov_toggle, GtkToggleButton, GTK_TYPE_TOGGLE_BUTTON, "toggle")
OV_CSS_TYPE (OvSplitButton, ov_split_button, GtkBox, GTK_TYPE_BOX, "splitbutton")
OV_CSS_TYPE (OvTabBar, ov_tab_bar, GtkBox, GTK_TYPE_BOX, "tabbar")
OV_CSS_TYPE (OvTabBox, ov_tab_box, GtkBox, GTK_TYPE_BOX, "tabbox")
OV_CSS_TYPE (OvTab, ov_tab, GtkBox, GTK_TYPE_BOX, "tab")

/* ---- the probe table ---------------------------------------------------- */

typedef enum {
  M_FLAGS,      /* set_state_flags (rest -> on), what GTK Inspector does */
  M_SENSITIVE,  /* rest = insensitive, on = sensitive (disabled -> enabled) */
  M_CLASS,      /* rest = without css class `cls`, on = with it */
  M_ACTIVE      /* rest = active FALSE, on = TRUE (the real property flip) */
} Mode;

typedef struct {
  const char *name;
  GtkWidget *ctx;        /* receives the state change */
  GtkWidget *also;       /* optional second node given the same flags */
  GtkWidget *sample;     /* node whose pixels are measured (NULL = ctx) */
  Mode mode;
  GtkStateFlags rest, on;
  int pad;               /* sample region margin, see sample () */
  const char *cls;
  const char *skip;      /* non-NULL: NOT PROBED, with this reason */
} Probe;

#define MAX_PROBES 160
static Probe probes[MAX_PROBES];
static int n_probes;

static Probe *
add_probe (const char *name, GtkWidget *ctx, GtkWidget *sample_w, Mode mode)
{
  g_assert (n_probes < MAX_PROBES);
  Probe *p = &probes[n_probes++];
  memset (p, 0, sizeof *p);
  p->name = name;
  p->ctx = ctx;
  p->sample = sample_w ? sample_w : ctx;
  p->mode = mode;
  return p;
}

/* Rest has no flags, the state is `flags`. */
static Probe *
add_flags (const char *name, GtkWidget *ctx, GtkWidget *sample_w,
           GtkStateFlags flags)
{
  Probe *p = add_probe (name, ctx, sample_w, M_FLAGS);
  p->on = flags;
  return p;
}

/* Rest already carries `rest`, the state is `on`. */
static Probe *
add_flags_from (const char *name, GtkWidget *ctx, GtkWidget *sample_w,
                GtkStateFlags rest, GtkStateFlags on)
{
  Probe *p = add_probe (name, ctx, sample_w, M_FLAGS);
  p->rest = rest;
  p->on = on;
  return p;
}

static void
add_skip (const char *name, const char *reason)
{
  Probe *p = add_probe (name, NULL, NULL, M_FLAGS);
  p->skip = reason;
}

static void
apply (const Probe *p, gboolean on)
{
  switch (p->mode) {
  case M_FLAGS: {
    GtkStateFlags f = on ? p->on : p->rest;
    gtk_widget_set_state_flags (p->ctx, f, TRUE);
    if (p->also != NULL)
      gtk_widget_set_state_flags (p->also, f, TRUE);
    break;
  }
  case M_SENSITIVE:
    gtk_widget_set_sensitive (p->ctx, on);
    break;
  case M_CLASS:
    if (on)
      gtk_widget_add_css_class (p->ctx, p->cls);
    else
      gtk_widget_remove_css_class (p->ctx, p->cls);
    break;
  case M_ACTIVE:
    g_object_set (p->ctx, "active", on, NULL);
    break;
  }
}

/* Re-assert the exact state, then sample. GTK re-evaluates
 * PRELIGHT/FOCUS/BACKDROP from the real pointer and window focus, so a
 * manually set flag can be clobbered mid-probe; asserting immediately
 * before the sample (no main-loop iteration in between) is what makes the
 * four numbers comparable run to run. */
static Frame
sample_in_state (const Probe *p, gboolean on)
{
  apply (p, on);
  return sample (p->sample, p->pad);
}

/* Find the n-th descendant (pre-order, root excluded) with this CSS node
 * name and, when given, CSS class. */
static GtkWidget *
find_walk (GtkWidget *root, const char *name, const char *cls, int *n)
{
  for (GtkWidget *c = gtk_widget_get_first_child (root); c != NULL;
       c = gtk_widget_get_next_sibling (c)) {
    if (g_str_equal (gtk_widget_get_css_name (c), name) &&
        (cls == NULL || gtk_widget_has_css_class (c, cls)) && (*n)-- == 0)
      return c;
    GtkWidget *r = find_walk (c, name, cls, n);
    if (r != NULL)
      return r;
  }
  return NULL;
}

static GtkWidget *
find_node (GtkWidget *root, const char *name, const char *cls, int nth)
{
  int n = nth;
  return root != NULL ? find_walk (root, name, cls, &n) : NULL;
}

static GtkWidget *
labelled_button (const char *label)
{
  GtkWidget *b = gtk_button_new_with_label (label);
  gtk_widget_set_size_request (b, 120, 36);
  return b;
}

/* Walk one probe through one state change; report four means. */
static void
run_probe (const Probe *p, gint64 sample_us)
{
  Frame rest, early, mid, settled;

  if (p->skip != NULL) {
    g_print ("%-20s NOT PROBED  (%s)\n", p->name, p->skip);
    return;
  }
  if (gtk_widget_get_width (p->sample) <= 0 ||
      gtk_widget_get_height (p->sample) <= 0) {
    g_print ("%-20s NOT PROBED  (node has no allocation in the probe window)\n",
             p->name);
    return;
  }

  /* The rest sample clears every state flag, not just this probe's: the
   * window hands the initial keyboard focus to the first focusable widget,
   * and a focused button would otherwise colour it. */
  discard (sample_in_state (p, FALSE));
  pump (450000);
  rest = sample_in_state (p, FALSE);

  discard (sample_in_state (p, TRUE));
  pump (sample_us);
  early = sample_in_state (p, TRUE);
  pump (sample_us);
  mid = sample_in_state (p, TRUE);
  pump (450000);
  settled = sample_in_state (p, TRUE);
  apply (p, FALSE);
  if (p->mode == M_SENSITIVE)
    apply (p, TRUE);
  pump (450000);

  /* NO CHANGE first: when rest and settled render the same there is nothing
   * to animate, and "both samples equal settled" would otherwise read as
   * INSTANT. Then two samples, because a state change lands on the frame
   * clock: a slow start (the focus ring begins a frame late) must not read
   * as "no change", and an instant change must read as such at both.
   * The means decide. Only when they agree that rest and settled are the
   * same does the pixel comparison get a say (a state confined to a few
   * pixels moves no mean): it then judges the same three questions on
   * per-pixel energy, and the line says so. */
  const char *verdict;
  char tag[48] = "";
  if (distance (rest.mean, settled.mean) > SAME) {
    verdict = distance (early.mean, settled.mean) <= SAME &&
              distance (mid.mean, settled.mean) <= SAME
                ? "INSTANT (state landed)"
                : "IN MOTION";
  } else if (energy (&rest, &settled) > ENERGY_SAME) {
    verdict = energy (&early, &settled) <= ENERGY_SAME &&
              energy (&mid, &settled) <= ENERGY_SAME
                ? "INSTANT (state landed)"
                : "IN MOTION";
    char num[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd (num, sizeof num, "%.2f", energy (&rest, &settled));
    g_snprintf (tag, sizeof tag, "  [pixels: %s]", num);
  } else {
    verdict = "NO CHANGE";
  }

  g_print ("%-20s rest=%3d,%3d,%3d,%3d  @%-3lldms=%3d,%3d,%3d,%3d  "
           "@%-3lldms=%3d,%3d,%3d,%3d  settled=%3d,%3d,%3d,%3d  -> %s%s\n",
           p->name,
           rest.mean.r, rest.mean.g, rest.mean.b, rest.mean.a,
           (long long) (sample_us / 1000),
           early.mean.r, early.mean.g, early.mean.b, early.mean.a,
           (long long) (2 * sample_us / 1000),
           mid.mean.r, mid.mean.g, mid.mean.b, mid.mean.a,
           settled.mean.r, settled.mean.g, settled.mean.b, settled.mean.a,
           verdict, tag);
  frame_clear (&rest);
  frame_clear (&early);
  frame_clear (&mid);
  frame_clear (&settled);
}

static void
glist_item_setup (GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
  (void) f; (void) data;
  gtk_list_item_set_child (item, gtk_label_new (NULL));
}

static void
glist_item_bind (GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
  (void) f; (void) data;
  gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (item)),
                      gtk_string_object_get_string (gtk_list_item_get_item (item)));
}

int
main (int argc, char **argv)
{
  if (argc < 2) {
    g_printerr ("usage: %s css [sample-ms] [upstream-css]\n", argv[0]);
    return 2;
  }
  hermetic_config ();
  setup_empty_theme ();
  gtk_init ();
  if (empty_theme)
    g_object_set (gtk_settings_get_default (), "gtk-theme-name", "ov-probe-empty", NULL);
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

  /* The original probes' widgets (header bar, button, boxed list) live in a
   * window built exactly as they were before this probe had a table, so
   * their numbers stay comparable with every earlier run: a mean over a
   * node depends on how wide the node is laid out. */
  GtkWidget *win = gtk_window_new ();
  GtkWidget *legacy = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top (legacy, 12);
  gtk_widget_set_margin_bottom (legacy, 12);
  gtk_widget_set_margin_start (legacy, 12);
  gtk_widget_set_margin_end (legacy, 12);
  gtk_window_set_child (GTK_WINDOW (win), legacy);

  GtkWidget *bar = gtk_header_bar_new ();
  gtk_box_append (GTK_BOX (legacy), bar);

  GtkWidget *button = gtk_button_new_with_label ("Test");
  gtk_widget_set_size_request (button, 160, 44);
  gtk_box_append (GTK_BOX (legacy), button);

  GtkWidget *list = gtk_list_box_new ();
  gtk_widget_add_css_class (list, "boxed-list");
  for (int i = 0; i < 2; i++) {
    GtkWidget *r = gtk_list_box_row_new ();
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (r), TRUE);
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (r),
                                gtk_label_new ("A list row"));
    gtk_list_box_append (GTK_LIST_BOX (list), r);
  }
  gtk_box_append (GTK_BOX (legacy), list);
  GtkWidget *row = GTK_WIDGET (gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0));

  /* Everything else: three columns, so the window stays inside a normal
   * screen and every node keeps its allocation. */
  GtkWidget *win2 = gtk_window_new ();
  GtkWidget *cols = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 24);
  GtkWidget *col[3];
  gtk_widget_set_margin_top (cols, 12);
  gtk_widget_set_margin_bottom (cols, 12);
  gtk_widget_set_margin_start (cols, 12);
  gtk_widget_set_margin_end (cols, 12);
  for (int i = 0; i < 3; i++) {
    col[i] = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand (col[i], TRUE);
    gtk_box_append (GTK_BOX (cols), col[i]);
  }
  gtk_window_set_child (GTK_WINDOW (win2), cols);

  /* ---- column 0: buttons -------------------------------------------- */
  GtkWidget *box = col[0];

  /* A second boxed list for the states the first one is not walked through. */
  GtkWidget *list2 = gtk_list_box_new ();
  gtk_widget_add_css_class (list2, "boxed-list");
  for (int i = 0; i < 2; i++) {
    GtkWidget *r = gtk_list_box_row_new ();
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (r), TRUE);
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (r),
                                gtk_label_new ("A list row"));
    gtk_list_box_append (GTK_LIST_BOX (list2), r);
  }
  gtk_box_append (GTK_BOX (box), list2);
  GtkWidget *row_press = GTK_WIDGET (gtk_list_box_get_row_at_index (GTK_LIST_BOX (list2), 0));
  GtkWidget *row_sel = GTK_WIDGET (gtk_list_box_get_row_at_index (GTK_LIST_BOX (list2), 1));

  GtkWidget *flat_btn = labelled_button ("Flat");
  gtk_widget_add_css_class (flat_btn, "flat");
  GtkWidget *sugg_btn = labelled_button ("Suggested");
  gtk_widget_add_css_class (sugg_btn, "suggested-action");
  GtkWidget *dest_btn = labelled_button ("Destructive");
  gtk_widget_add_css_class (dest_btn, "destructive-action");
  GtkWidget *tog_btn = gtk_toggle_button_new_with_label ("Toggle");
  gtk_widget_set_size_request (tog_btn, 120, 36);
  GtkWidget *dis_btn = labelled_button ("Disabled");
  GtkWidget *menu_btn = gtk_menu_button_new ();
  gtk_menu_button_set_label (GTK_MENU_BUTTON (menu_btn), "Menu");
  GtkWidget *menu_inner = find_node (menu_btn, "button", NULL, 0);
  /* Icon buttons for the press dip (-gtk-icon-transform on the glyph): an
   * opaque one, one with a label beside the icon, a `.flat` one. */
  GtkWidget *icon_btn = gtk_button_new_from_icon_name ("edit-copy-symbolic");
  GtkWidget *icon_label_btn = gtk_button_new ();
  {
    GtkWidget *inner = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append (GTK_BOX (inner), gtk_image_new_from_icon_name ("edit-copy-symbolic"));
    gtk_box_append (GTK_BOX (inner), gtk_label_new ("Copy"));
    gtk_button_set_child (GTK_BUTTON (icon_label_btn), inner);
  }
  GtkWidget *icon_flat_btn = gtk_button_new_from_icon_name ("edit-copy-symbolic");
  gtk_widget_add_css_class (icon_flat_btn, "flat");
  GtkWidget *links[] = { flat_btn, sugg_btn, dest_btn, tog_btn, dis_btn, menu_btn,
                         icon_btn, icon_label_btn, icon_flat_btn };
  for (guint i = 0; i < G_N_ELEMENTS (links); i++)
    gtk_box_append (GTK_BOX (box), links[i]);

  /* Header bar with an icon button and the window controls. */
  GtkWidget *bar2 = gtk_header_bar_new ();
  GtkWidget *hb_btn = gtk_button_new_from_icon_name ("go-previous-symbolic");
  gtk_header_bar_pack_start (GTK_HEADER_BAR (bar2), hb_btn);
  gtk_box_append (GTK_BOX (box), bar2);
  GtkWidget *wc_btn = NULL;   /* found once the controls have been built */

  /* A `.toolbar` row: flat context for icon buttons and the split button. */
  GtkWidget *toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class (toolbar, "toolbar");
  GtkWidget *tb_btn = gtk_button_new_from_icon_name ("edit-copy-symbolic");
  gtk_box_append (GTK_BOX (toolbar), tb_btn);
  GtkWidget *tb_split = g_object_new (ov_split_button_get_type (),
                                      "orientation", GTK_ORIENTATION_HORIZONTAL, NULL);
  GtkWidget *tb_split_main = gtk_button_new_with_label ("Split");
  GtkWidget *tb_split_sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
  GtkWidget *tb_split_menu = gtk_menu_button_new ();
  gtk_box_append (GTK_BOX (tb_split), tb_split_main);
  gtk_box_append (GTK_BOX (tb_split), tb_split_sep);
  gtk_box_append (GTK_BOX (tb_split), tb_split_menu);
  gtk_box_append (GTK_BOX (toolbar), tb_split);
  gtk_box_append (GTK_BOX (box), toolbar);

  /* The same split button outside any flat context. */
  GtkWidget *split = g_object_new (ov_split_button_get_type (),
                                   "orientation", GTK_ORIENTATION_HORIZONTAL, NULL);
  GtkWidget *split_main = gtk_button_new_with_label ("Split");
  GtkWidget *split_sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
  GtkWidget *split_menu = gtk_menu_button_new ();
  gtk_box_append (GTK_BOX (split), split_main);
  gtk_box_append (GTK_BOX (split), split_sep);
  gtk_box_append (GTK_BOX (split), split_menu);
  gtk_box_append (GTK_BOX (box), split);

  /* Nautilus-style crumb. */
  GtkWidget *crumbs = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (crumbs, "nautilus-pathbar");
  GtkWidget *crumb = gtk_button_new_with_label ("Crumb");
  gtk_widget_add_css_class (crumb, "nautilus-path-button");
  gtk_box_append (GTK_BOX (crumbs), crumb);
  gtk_box_append (GTK_BOX (box), crumbs);

  /* ---- column 1: selection controls, range controls, entries --------- */
  box = col[1];

  GtkWidget *check = gtk_check_button_new_with_label ("Check");
  GtkWidget *radio_a = gtk_check_button_new_with_label ("Radio A");
  GtkWidget *radio_b = gtk_check_button_new_with_label ("Radio B");
  gtk_check_button_set_group (GTK_CHECK_BUTTON (radio_b), GTK_CHECK_BUTTON (radio_a));
  GtkWidget *check_ind = find_node (check, "check", NULL, 0);
  GtkWidget *radio_ind = find_node (radio_b, "radio", NULL, 0);
  gtk_box_append (GTK_BOX (box), check);
  gtk_box_append (GTK_BOX (box), radio_a);
  gtk_box_append (GTK_BOX (box), radio_b);

  GtkWidget *sw = gtk_switch_new ();
  gtk_widget_set_halign (sw, GTK_ALIGN_START);
  GtkWidget *sw_slider = find_node (sw, "slider", NULL, 0);
  gtk_box_append (GTK_BOX (box), sw);

  GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_range_set_value (GTK_RANGE (scale), 50);
  gtk_widget_set_size_request (scale, 200, -1);
  GtkWidget *scale_trough = find_node (scale, "trough", NULL, 0);
  GtkWidget *scale_slider = find_node (scale, "slider", NULL, 0);
  GtkWidget *scale_hl = find_node (scale, "highlight", NULL, 0);
  gtk_box_append (GTK_BOX (box), scale);

  GtkWidget *progress = gtk_progress_bar_new ();
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress), 0.6);
  gtk_widget_set_size_request (progress, 200, -1);
  GtkWidget *progress_fill = find_node (progress, "progress", NULL, 0);
  gtk_box_append (GTK_BOX (box), progress);

  GtkAdjustment *adj = gtk_adjustment_new (0, 0, 100, 1, 10, 30);
  GtkWidget *scrollbar = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, adj);
  gtk_widget_set_size_request (scrollbar, 200, -1);
  gtk_widget_add_css_class (scrollbar, "overlay-indicator");
  GtkWidget *sb_trough = find_node (scrollbar, "trough", NULL, 0);
  GtkWidget *sb_slider = find_node (scrollbar, "slider", NULL, 0);
  gtk_box_append (GTK_BOX (box), scrollbar);

  GtkWidget *entry = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (entry), "Entry");
  gtk_box_append (GTK_BOX (box), entry);

  GtkWidget *spin = gtk_spin_button_new_with_range (0, 100, 1);
  GtkWidget *spin_up = find_node (spin, "button", "up", 0);
  gtk_box_append (GTK_BOX (box), spin);

  GtkWidget *expander = gtk_expander_new ("Expander");
  gtk_expander_set_child (GTK_EXPANDER (expander), gtk_label_new ("Body"));
  GtkWidget *exp_title = find_node (expander, "title", NULL, 0);
  gtk_box_append (GTK_BOX (box), expander);

  GtkWidget *calendar = gtk_calendar_new ();
  GtkWidget *cal_day = find_node (calendar, "label", "day-number", 5);
  gtk_box_append (GTK_BOX (box), calendar);

  /* ---- column 2: groups, tabs, cells -------------------------------- */
  box = col[2];

  GtkWidget *tg = g_object_new (ov_toggle_group_get_type (),
                                "orientation", GTK_ORIENTATION_HORIZONTAL, NULL);
  GtkWidget *tg_a = g_object_new (ov_toggle_get_type (), "label", "One", NULL);
  GtkWidget *tg_sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
  GtkWidget *tg_b = g_object_new (ov_toggle_get_type (), "label", "Two", NULL);
  gtk_box_append (GTK_BOX (tg), tg_a);
  gtk_box_append (GTK_BOX (tg), tg_sep);
  gtk_box_append (GTK_BOX (tg), tg_b);
  gtk_box_append (GTK_BOX (box), tg);

  GtkWidget *tabbar = g_object_new (ov_tab_bar_get_type (),
                                    "orientation", GTK_ORIENTATION_HORIZONTAL, NULL);
  GtkWidget *tabbar_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (tabbar_box, "box");
  GtkWidget *tabbox = g_object_new (ov_tab_box_get_type (),
                                    "orientation", GTK_ORIENTATION_HORIZONTAL, NULL);
  GtkWidget *tab_a = g_object_new (ov_tab_get_type (),
                                   "orientation", GTK_ORIENTATION_HORIZONTAL, NULL);
  GtkWidget *tab_b = g_object_new (ov_tab_get_type (),
                                   "orientation", GTK_ORIENTATION_HORIZONTAL, NULL);
  gtk_box_append (GTK_BOX (tab_a), gtk_label_new ("Tab A"));
  gtk_box_append (GTK_BOX (tab_b), gtk_label_new ("Tab B"));
  gtk_widget_set_size_request (tab_a, 90, 34);
  gtk_widget_set_size_request (tab_b, 90, 34);
  gtk_box_append (GTK_BOX (tabbox), tab_a);
  gtk_box_append (GTK_BOX (tabbox), tab_b);
  gtk_box_append (GTK_BOX (tabbar_box), tabbox);
  gtk_box_append (GTK_BOX (tabbar), tabbar_box);
  gtk_box_append (GTK_BOX (box), tabbar);

  GtkWidget *nb = gtk_notebook_new ();
  gtk_widget_set_size_request (nb, 220, 80);
  GtkWidget *nb_l0 = gtk_label_new ("Page A");
  GtkWidget *nb_l1 = gtk_label_new ("Page B");
  gtk_notebook_append_page (GTK_NOTEBOOK (nb), gtk_label_new ("A"), nb_l0);
  gtk_notebook_append_page (GTK_NOTEBOOK (nb), gtk_label_new ("B"), nb_l1);
  GtkWidget *nb_tab = gtk_widget_get_parent (nb_l1);   /* the non-current tab */
  gtk_box_append (GTK_BOX (box), nb);

  GtkWidget *card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (card, "card");
  gtk_widget_add_css_class (card, "activatable");
  gtk_box_append (GTK_BOX (card), gtk_label_new ("Activatable card"));
  gtk_widget_set_size_request (card, -1, 44);
  gtk_box_append (GTK_BOX (box), card);

  GtkWidget *plain = gtk_list_box_new ();
  GtkWidget *plain_row = gtk_list_box_row_new ();
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (plain_row), gtk_label_new ("Plain row"));
  gtk_list_box_append (GTK_LIST_BOX (plain), plain_row);
  gtk_box_append (GTK_BOX (box), plain);

  GtkWidget *nav = gtk_list_box_new ();
  gtk_widget_add_css_class (nav, "navigation-sidebar");
  GtkWidget *nav_row[3];
  for (int i = 0; i < 3; i++) {
    nav_row[i] = gtk_list_box_row_new ();
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (nav_row[i]), gtk_label_new ("Sidebar row"));
    gtk_list_box_append (GTK_LIST_BOX (nav), nav_row[i]);
  }
  gtk_box_append (GTK_BOX (box), nav);

  GtkWidget *flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
  for (int i = 0; i < 2; i++)
    gtk_flow_box_append (GTK_FLOW_BOX (flow), gtk_label_new ("Cell"));
  GtkWidget *flow_child = GTK_WIDGET (gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (flow), 0));
  gtk_box_append (GTK_BOX (box), flow);

  GtkStringList *strings = gtk_string_list_new (NULL);
  for (int i = 0; i < 4; i++)
    gtk_string_list_append (strings, "Item");
  GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (glist_item_setup), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (glist_item_bind), NULL);
  GtkWidget *grid = gtk_grid_view_new (
    GTK_SELECTION_MODEL (gtk_no_selection_new (G_LIST_MODEL (strings))), factory);
  GtkWidget *grid_scroll = gtk_scrolled_window_new ();
  gtk_widget_set_size_request (grid_scroll, 220, 90);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (grid_scroll), grid);
  gtk_box_append (GTK_BOX (box), grid_scroll);

  gtk_window_present (GTK_WINDOW (win));
  gtk_window_set_focus (GTK_WINDOW (win), NULL);
  gtk_window_set_default_size (GTK_WINDOW (win2), 900, 100);
  gtk_window_present (GTK_WINDOW (win2));
  gtk_window_set_focus (GTK_WINDOW (win2), NULL);
  pump (800000);
  GtkWidget *grid_child = gtk_widget_get_first_child (grid);
  wc_btn = find_node (bar2, "button", "close", 0);

  g_print ("css: %s%s   sample: %lldms   REDUCE=%s NOANIM=%s CONTRAST=%s SCHEME=%s\n",
           argv[1], argc > 3 ? " + theme" : "", (long long) (sample_us / 1000),
           g_getenv ("REDUCE") ?: "0", g_getenv ("NOANIM") ?: "0",
           contrast ?: "normal", scheme ?: "light");

  const GtkStateFlags HOVER = GTK_STATE_FLAG_PRELIGHT;
  const GtkStateFlags PRESS = GTK_STATE_FLAG_ACTIVE;
  const GtkStateFlags FOCUS = GTK_STATE_FLAG_FOCUSED | GTK_STATE_FLAG_FOCUS_VISIBLE;
  const GtkStateFlags WITHIN = FOCUS | GTK_STATE_FLAG_FOCUS_WITHIN;
  const GtkStateFlags CHECKED = GTK_STATE_FLAG_CHECKED;
  const GtkStateFlags SELECTED = GTK_STATE_FLAG_SELECTED;

  /* The five original probes, in their original order. */
  add_flags ("button:hover", button, NULL, HOVER);
  add_flags ("button:active", button, NULL, PRESS);
  add_flags ("boxed-list row:hover", row, NULL, HOVER);
  add_flags ("button:focus-visible", button, NULL, FOCUS);
  add_flags ("headerbar:backdrop", bar, NULL, GTK_STATE_FLAG_BACKDROP);

  /* Buttons. */
  add_flags ("button.flat:hover", flat_btn, NULL, HOVER);
  add_flags ("button.flat:active", flat_btn, NULL, PRESS);
  add_flags ("btn.suggested:hover", sugg_btn, NULL, HOVER);
  add_flags ("btn.suggested:active", sugg_btn, NULL, PRESS);
  add_flags ("btn.destructive:hov", dest_btn, NULL, HOVER);
  add_flags ("button:checked", tog_btn, NULL, CHECKED);
  add_probe ("button:enabled", dis_btn, NULL, M_SENSITIVE);
  add_probe ("button.flat:enabled", flat_btn, NULL, M_SENSITIVE);
  if (menu_inner != NULL) {
    Probe *p = add_flags ("menubutton:hover", menu_inner, NULL, HOVER);
    p->also = menu_btn;
  } else
    add_skip ("menubutton:hover", "menubutton has no button node");
  add_flags ("headerbar btn:hover", hb_btn, NULL, HOVER);
  add_probe ("headerbar btn:enabld", hb_btn, NULL, M_SENSITIVE);
  if (wc_btn != NULL)
    add_flags ("windowcontrols:hover", wc_btn, NULL, HOVER);
  else
    add_skip ("windowcontrols:hover", "no windowcontrols button in the header bar");
  /* Icon press: the glyph's own pixels (the `image` node) dip under the
   * pressed button. The mean of a scaled symbolic icon barely moves, so
   * these read through the pixel comparison ([pixels: x]). The :release
   * rows walk the other way (pressed -> rest) to measure the exit. The
   * window-control glyph is the control: it must NOT dip. */
  {
    GtkWidget *img;
    if ((img = find_node (icon_btn, "image", NULL, 0)) != NULL) {
      add_flags ("icon btn:active", icon_btn, img, PRESS);
      add_flags_from ("icon btn:release", icon_btn, img, PRESS, 0);
    }
    if ((img = find_node (icon_label_btn, "image", NULL, 0)) != NULL)
      add_flags ("icon+label:active", icon_label_btn, img, PRESS);
    if ((img = find_node (icon_flat_btn, "image", NULL, 0)) != NULL)
      add_flags ("icon flat:active", icon_flat_btn, img, PRESS);
    if ((img = find_node (hb_btn, "image", NULL, 0)) != NULL)
      add_flags ("hdrbar icon:active", hb_btn, img, PRESS);
    if (wc_btn != NULL && (img = find_node (wc_btn, "image", NULL, 0)) != NULL)
      add_flags ("wcontrols img:act", wc_btn, img, PRESS);
  }
  add_flags ("toolbar btn:hover", tb_btn, NULL, HOVER);
  add_probe ("toolbar btn:enabled", tb_btn, NULL, M_SENSITIVE);
  {
    Probe *p = add_flags ("splitbutton:hover", split, NULL, HOVER);
    p->also = split_main;
    GtkWidget *split_menu_btn = find_node (split_menu, "button", NULL, 0);
    p = add_flags ("splitbtn menu:hover", split_menu_btn ? split_menu_btn : split_menu,
                   NULL, HOVER);
    p->also = split;
    p = add_flags ("toolbar split:hover", tb_split, NULL, HOVER);
    p->also = tb_split_main;
    add_flags ("toolbar split sep", tb_split, tb_split_sep, HOVER);
    add_flags ("splitbutton sep", split, split_sep, HOVER);
  }
  add_flags ("pathbar crumb:hover", crumb, NULL, HOVER);
  add_flags ("pathbar crumb:active", crumb, NULL, PRESS);

  /* Check, radio, switch. */
  if (check_ind != NULL) {
    add_flags ("check:hover", check_ind, NULL, HOVER);
    add_flags ("check:active", check_ind, NULL, PRESS);
    add_flags ("check:checked", check_ind, NULL, CHECKED);
    add_flags_from ("check:checked:active", check_ind, NULL, CHECKED, CHECKED | PRESS);
    add_probe ("check:enabled", check, check_ind, M_SENSITIVE);
  } else
    add_skip ("check:*", "no check node in GtkCheckButton");
  if (radio_ind != NULL) {
    add_flags ("radio:hover", radio_ind, NULL, HOVER);
    add_flags ("radio:active", radio_ind, NULL, PRESS);
    add_flags ("radio:checked", radio_ind, NULL, CHECKED);
    add_flags_from ("radio:checked:active", radio_ind, NULL, CHECKED, CHECKED | PRESS);
  } else
    add_skip ("radio:*", "no radio node in GtkCheckButton");
  add_flags ("switch:checked (css)", sw, NULL, CHECKED);
  add_flags_from ("switch:checked:hover", sw, NULL, CHECKED, CHECKED | HOVER);
  add_flags_from ("switch:chk:active", sw, NULL, CHECKED, CHECKED | PRESS);
  add_flags ("switch:hover", sw, NULL, HOVER);
  if (sw_slider != NULL) {
    add_flags ("switch>slider:chkd", sw, sw_slider, CHECKED);
    add_probe ("switch>slider:enabled", sw, sw_slider, M_SENSITIVE);
  }
  add_probe ("switch:active (prop)", sw, NULL, M_ACTIVE);
  add_probe ("switch:enabled", sw, NULL, M_SENSITIVE);

  /* Range controls. */
  if (scale_slider != NULL) {
    Probe *p = add_flags ("scale>slider:hover", scale, scale_slider, HOVER);
    p->also = scale_slider;
    p->pad = 4;
    p = add_flags ("scale>slider:active", scale, scale_slider, PRESS | HOVER);
    p->also = scale_slider;
    p->pad = 4;
    add_probe ("scale>slider:enabled", scale, scale_slider, M_SENSITIVE)->pad = 4;
    /* The knob lift is a CSS transform on the slider node: a node's own
     * transform is applied by its PARENT's snapshot, so the slider's own
     * paintable cannot show it. Sample the whole scale (the knob overhangs
     * the trough), padded for the grown knob, and read the lift as the
     * difference against the sheet without the transform. */
    p = add_flags ("scale:active (lift)", scale, scale, PRESS | HOVER);
    p->also = scale_slider;
    p->pad = 6;
    p = add_flags_from ("scale:release", scale, scale, PRESS | HOVER, 0);
    p->also = scale_slider;
    p->pad = 6;
  }
  add_probe ("scale:enabled", scale, NULL, M_SENSITIVE);
  if (scale_trough != NULL) {
    add_flags ("scale>trough:hover", scale, scale_trough, HOVER);
    add_flags ("scale>trough:active", scale, scale_trough, PRESS | HOVER);
  }
  if (scale_hl != NULL) {
    add_flags ("scale>highlt:hover", scale, scale_hl, HOVER);
    add_flags ("scale>highlt:active", scale, scale_hl, PRESS | HOVER);
  } else
    add_skip ("scale>highlight", "no highlight node at this value");
  add_flags ("progressbar:backdrop", progress, NULL, GTK_STATE_FLAG_BACKDROP);
  add_probe ("progressbar:enabled", progress, NULL, M_SENSITIVE);
  if (progress_fill != NULL)
    add_probe ("progress fill:enabled", progress, progress_fill, M_SENSITIVE);
  if (sb_slider != NULL)
    add_flags ("scrollbar>slider:hov", sb_slider, NULL, HOVER);
  if (sb_trough != NULL) {
    add_probe ("scrollbar trough:hovg", scrollbar, sb_trough, M_CLASS)->cls = "hovering";
    add_flags ("scrollbar trough:hov", scrollbar, sb_trough, HOVER);
  }

  /* Entry, spinbutton. */
  add_flags ("entry:focus", entry, NULL, WITHIN);
  add_probe ("entry:enabled", entry, NULL, M_SENSITIVE);
  add_flags ("spinbutton:focus", spin, NULL, WITHIN);
  add_probe ("spinbutton:enabled", spin, NULL, M_SENSITIVE);
  if (spin_up != NULL)
    add_flags ("spinbtn up:hover", spin_up, NULL, HOVER);

  /* Expander, calendar. */
  if (exp_title != NULL) {
    add_flags ("expander title:hov", exp_title, NULL, HOVER);
    add_flags ("expander title:act", exp_title, NULL, PRESS);
  } else
    add_skip ("expander title", "no title node in GtkExpander");
  if (cal_day != NULL)
    add_flags ("calendar day:checked", cal_day, NULL, CHECKED);
  else
    add_skip ("calendar day:checked", "no day-number label found");

  /* Toggle group (stand-in), tabbar (stand-in), notebook. */
  add_flags ("toggle:hover", tg_a, NULL, HOVER);
  add_flags ("toggle:active", tg_a, NULL, PRESS);
  add_flags ("toggle:checked", tg_a, NULL, CHECKED);
  add_flags_from ("toggle:checked:hover", tg_a, NULL, CHECKED, CHECKED | HOVER);
  add_flags ("toggle-group sep", tg_a, tg_sep, CHECKED);
  add_probe ("toggle:checked flip", tg_b, NULL, M_ACTIVE);
  add_probe ("toggle:enabled", tg_a, NULL, M_SENSITIVE);
  add_probe ("toggle-group:enabled", tg, NULL, M_SENSITIVE);
  add_flags ("tabbar tab:selected", tab_a, NULL, SELECTED);
  add_flags ("tabbar tab:hover", tab_b, NULL, HOVER);
  add_flags ("tabbar tab:active", tab_b, NULL, PRESS);
  if (nb_tab != NULL && g_str_equal (gtk_widget_get_css_name (nb_tab), "tab")) {
    add_flags ("notebook tab:hover", nb_tab, NULL, HOVER);
    add_flags ("notebook tab:active", nb_tab, NULL, PRESS);
    add_flags ("notebook tab:checked", nb_tab, NULL, CHECKED);
  } else
    add_skip ("notebook tab:*", "tab label's parent is not the `tab` node");

  /* Rows and cells. */
  add_flags ("boxed row:selected", row_sel, NULL, SELECTED);
  add_flags ("boxed row:active", row_press, NULL, PRESS);
  add_flags ("card.activatable:hov", card, NULL, HOVER);
  add_flags ("card.activatable:act", card, NULL, PRESS);
  add_flags ("list row:hover", plain_row, NULL, HOVER);
  add_flags ("list row:active", plain_row, NULL, PRESS);
  add_flags ("nav row:hover", nav_row[0], NULL, HOVER);
  add_flags ("nav row:active", nav_row[0], NULL, PRESS);
  add_flags ("nav row:selected", nav_row[1], NULL, SELECTED);
  add_flags_from ("nav row:sel:hover", nav_row[1], NULL, SELECTED, SELECTED | HOVER);
  add_flags_from ("nav row:sel:active", nav_row[1], NULL, SELECTED, SELECTED | PRESS);
  add_flags ("flowboxchild:hover", flow_child, NULL, HOVER);
  add_flags ("flowboxchild:active", flow_child, NULL, PRESS);
  if (grid_child != NULL && g_str_equal (gtk_widget_get_css_name (grid_child), "child")) {
    add_flags ("gridview child:hover", grid_child, NULL, HOVER);
    add_flags ("gridview child:act", grid_child, NULL, PRESS);
  } else
    add_skip ("gridview child:*", "no `child` node realised in the probe window");

  /* Reached by no plain-GTK stand-in, or with no state-dependent rule. */
  add_skip ("popover.menu rows", "needs a mapped popover surface; the popover node only exists while popped up (E6)");
  add_skip ("popover>contents>mb", "same: lives inside a popover surface");
  add_skip ("bottom-sheet>sheet", "AdwBottomSheet only; box-shadow + reveal animation, no plain-GTK equivalent");
  add_skip ("toast", "AdwToast only; one static shadow, no state-dependent rule");
  add_skip ("levelbar blocks", "no state-dependent overlay rule (static gel fill)");
  add_skip (".sidebar/.content-pane", "static background-image, no state-dependent rule");
  add_skip ("tabbar tab close btn", "AdwTab internals; only a box-shadow/filter reset, no state-dependent rule");
  add_skip ("real AdwTabBar/AdwTab", "libadwaita-only; probed through css-name stand-ins (tabbar > .box > tabbox > tab)");
  add_skip ("real AdwToggleGroup", "libadwaita-only; probed through css-name stand-ins (toggle-group > toggle, separator)");
  add_skip ("real AdwSplitButton", "libadwaita-only; probed through a css-name stand-in (splitbutton > button, separator, menubutton)");

  const char *only = g_getenv ("PROBE");
  for (int i = 0; i < n_probes; i++)
    if (only == NULL || strstr (probes[i].name, only) != NULL)
      run_probe (&probes[i], sample_us);
  return 0;
}
