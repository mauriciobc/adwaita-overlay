/*
 * SPDX-FileCopyrightText: 2026 mauriciobc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* tools/probe-foreign.c — what Chromium reads out of the GTK theme when
 * libadwaita is *not* loaded.
 *
 *   probe-foreign <css-file|none>     measure exactly that sheet
 *   probe-foreign                      measure the live user sheet
 *
 * Why this exists (25 Sep 2026): libadwaita's custom properties (--window-
 * bg-color, --headerbar-bg-color, …) exist only when libadwaita's
 * stylesheet is loaded. A GTK4 app that never calls adw_init() — Chromium
 * and its forks, Helium included — sees them as undefined, and a
 * declaration whose only value is an undefined var() computes to nothing:
 * GTK paints no background at all, it does not fall back to the theme's
 * own colour.
 *
 * Chromium builds its whole Linux palette out of rendered GTK backgrounds
 * (ui/gtk/gtk_util.cc GetBgColor renders the node into a 24x24 surface and
 * averages it; ui/gtk/gtk_color_mixers.cc consumes the result) and forces
 * the frame colour opaque:
 *
 *   frame_color = SkColorSetA(GetBgColor("headerbar.header-bar.titlebar"),
 *                             SK_AlphaOPAQUE);
 *
 * so a node that paints nothing (alpha 0 over black) becomes #ff000000:
 * black window, black toolbar, black tabs. This probe reimplements
 * GetBgColor/GetFgColor/GetBorderColor against a bare GTK4 app and prints
 * the same inputs the mixer reads, plus the two derived colours (frame,
 * toolbar). Any painted input that comes out fully transparent is a fail:
 * that is exactly what Chromium turns into black.
 *
 * The selectors are copied from gtk_color_mixers.cc /
 * gtk_util.cc (GtkCssMenu() = "popover.background.menu contents",
 * GtkCssMenuItem() = "modelbutton.flat"), so the table is the contract with
 * Chromium, not a wish list. Deltas against a libadwaita render are
 * expected in the window/bar/tooltip rows — the overlay is for libadwaita
 * apps — but nothing may paint *nothing*.
 *
 * Env knobs, mirroring render-widget and probe-motion: SCHEME=dark,
 * CONTRAST=more, KEEP_CONFIG=1 (keep the real ~/.config; the css argument
 * is otherwise the only sheet in the process).
 *
 * Build:
 *   gcc -O1 -o build/probe-foreign tools/probe-foreign.c \
 *       $(pkg-config --cflags --libs gtk4)
 *
 * Exit status: 0 all painted inputs resolve, 1 any of them paints nothing,
 * 2 usage or unreadable file.
 */
/* The style-context queries below (gtk_style_context_get_color,
 * gtk_render_background, …) are deprecated since GTK 4.10 and have no
 * replacement — Chromium itself uses them to extract the theme's colours,
 * which is exactly what this probe reproduces. GTK's own switch for
 * silencing them, so the tree stays warning-clean. */
#define GDK_DISABLE_DEPRECATION_WARNINGS
#include <cairo.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GTK loads $XDG_CONFIG_HOME/gtk-4.0/gtk.css in every process; on a machine
 * with this overlay installed that path is a symlink to the very sheet
 * under test, so an A/B would measure the same file twice. Point
 * XDG_CONFIG_HOME at an empty directory so the css argument is the only
 * stylesheet in the process. KEEP_CONFIG=1 restores the real environment. */
static void
hermetic_config (void)
{
  gchar *dir;

  if (g_getenv ("KEEP_CONFIG") != NULL)
    return;

  dir = g_dir_make_tmp ("probe-foreign-XXXXXX", NULL);
  if (dir != NULL)
    {
      g_setenv ("XDG_CONFIG_HOME", dir, TRUE);
      g_free (dir);
    }
}

/* --- the style-context chain, exactly as gtk_util.cc builds it --------- */

typedef struct
{
  GtkWidget *widget;
} Ctx;

static GtkWidget *
node_new (const char *css_name, const char *name, const char **classes,
          GtkStateFlags state)
{
  GtkWidget *widget = GTK_WIDGET (g_object_new (
      GTK_TYPE_BOX, "css-name", css_name[0] ? css_name : NULL, NULL));

  if (name[0])
    gtk_widget_set_name (widget, name);
  gtk_widget_set_css_classes (widget, classes);
  gtk_widget_set_state_flags (widget, state, FALSE);
  /* Chromium queries at the device scale factor; this probe reads colours,
   * which do not depend on it. */
  gtk_style_context_set_scale (gtk_widget_get_style_context (widget), 1);

  return widget;
}

/* AppendCssNodeToStyleContext(): "window.background", "button.text-button:
 * disabled", "popover.background.menu contents modelbutton.flat". */
static Ctx
ctx_append (Ctx parent, const char *css_node)
{
  enum { PART_NAME, PART_OBJECT_NAME, PART_CLASS, PART_PSEUDOCLASS, PART_NONE }
    part = PART_OBJECT_NAME;
  static const struct
  {
    const char *name;
    GtkStateFlags flag;
  } pseudos[] = {
    { "active", GTK_STATE_FLAG_ACTIVE },
    { "hover", GTK_STATE_FLAG_PRELIGHT },
    { "selected", GTK_STATE_FLAG_SELECTED },
    { "disabled", GTK_STATE_FLAG_INSENSITIVE },
    { "indeterminate", GTK_STATE_FLAG_INCONSISTENT },
    { "focus", GTK_STATE_FLAG_FOCUSED },
    { "focus-within", GTK_STATE_FLAG_FOCUS_WITHIN },
    { "backdrop", GTK_STATE_FLAG_BACKDROP },
    { "link", GTK_STATE_FLAG_LINK },
    { "visited", GTK_STATE_FLAG_VISITED },
    { "checked", GTK_STATE_FLAG_CHECKED },
  };
  char name[128] = "";
  char object_name[128] = "";
  char classes[16][64];
  const char *css_classes[17];
  int n_classes = 0;
  GtkStateFlags state = GTK_STATE_FLAG_NORMAL;
  const char *p = css_node;

  while (*p)
    {
      const char *end;
      char tok[128];
      size_t n;

      if (*p == '.' || *p == ':' || *p == '(' || *p == ')')
        {
          part = *p == '('   ? PART_NAME
                 : *p == ')' ? PART_NONE
                 : *p == '.' ? PART_CLASS
                             : PART_PSEUDOCLASS;
          p++;
          continue;
        }

      for (end = p; *end && *end != '.' && *end != ':' && *end != '('
                    && *end != ')'; end++)
        ;
      n = (size_t) (end - p);
      if (n >= sizeof tok)
        n = sizeof tok - 1;
      memcpy (tok, p, n);
      tok[n] = 0;
      p = end;

      switch (part)
        {
        case PART_NAME:
          g_strlcpy (name, tok, sizeof name);
          break;
        case PART_OBJECT_NAME:
          g_strlcpy (object_name, tok, sizeof object_name);
          break;
        case PART_CLASS:
          if (n_classes < 16)
            g_strlcpy (classes[n_classes++], tok, sizeof classes[0]);
          break;
        case PART_PSEUDOCLASS:
          for (gsize i = 0; i < G_N_ELEMENTS (pseudos); i++)
            if (strcmp (pseudos[i].name, tok) == 0)
              state |= pseudos[i].flag;
          break;
        case PART_NONE:
          break;
        }
    }

  /* Chromium adds a "chromium" class to every node so themes can target it. */
  if (n_classes < 16)
    g_strlcpy (classes[n_classes++], "chromium", sizeof classes[0]);
  for (int i = 0; i < n_classes; i++)
    css_classes[i] = classes[i];
  css_classes[n_classes] = NULL;

  {
    GtkWidget *widget =
        node_new (object_name, name, css_classes, state);
    if (parent.widget)
      gtk_widget_set_parent (widget, parent.widget);
    return (Ctx) { widget };
  }
}

static Ctx
ctx_from_css (const char *selector, gboolean rooted)
{
  Ctx ctx = { NULL };
  char buf[512];
  char *save = NULL;

  /* Chromium prepends window.background because every widget lives in a
   * window — except for the tooltip, which it queries as its own root. */
  if (!rooted)
    ctx = ctx_append (ctx, "window.background");

  g_strlcpy (buf, selector, sizeof buf);
  for (char *tok = strtok_r (buf, " ", &save); tok;
       tok = strtok_r (NULL, " ", &save))
    ctx = ctx_append (ctx, tok);

  return ctx;
}

/* --- GetBgColor / GetFgColor / GetBorderColor ------------------------- */

static GtkCssProvider *
provider_from (const char *css)
{
  GtkCssProvider *provider = gtk_css_provider_new ();

  if (g_strcmp0 (g_getenv ("CONTRAST"), "more") == 0)
    g_object_set (provider, "prefers-contrast", GTK_INTERFACE_CONTRAST_MORE,
                  NULL);
  gtk_css_provider_load_from_string (provider, css);
  return provider;
}

static void
apply_provider (Ctx ctx, GtkCssProvider *provider)
{
  for (GtkWidget *w = ctx.widget; w; w = gtk_widget_get_parent (w))
    gtk_style_context_add_provider (gtk_widget_get_style_context (w),
                                    GTK_STYLE_PROVIDER (provider), G_MAXUINT);
}

/* Chromium renders backgrounds without borders: some themes leave
 * background-color set to garbage because a background-image covers it. */
static GtkCssProvider *
strip_borders (void)
{
  static GtkCssProvider *provider;

  if (!provider)
    provider = provider_from (
        "* { border-radius: 0px; border-style: none; box-shadow: none; }");
  return provider;
}

static void
render_background (Ctx ctx, cairo_t *cr, int w, int h)
{
  if (!ctx.widget)
    return;
  render_background ((Ctx) { gtk_widget_get_parent (ctx.widget) }, cr, w, h);
  gtk_render_background (gtk_widget_get_style_context (ctx.widget), cr, 0, 0,
                         w, h);
}

static cairo_surface_t *
surface_new (int w, int h, cairo_t **cr_out)
{
  cairo_surface_t *surface =
      cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
  cairo_t *cr = cairo_create (surface);

  /* CairoSurface()'s constructor clears to transparent with SOURCE. */
  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

  *cr_out = cr;
  return surface;
}

static guint32
surface_average (cairo_surface_t *surface, gboolean frame)
{
  unsigned char *data;
  int w, h;
  long a = 0, r = 0, g = 0, b = 0;
  unsigned max_alpha = 0;

  cairo_surface_flush (surface);
  data = cairo_image_surface_get_data (surface);
  w = cairo_image_surface_get_width (surface);
  h = cairo_image_surface_get_height (surface);

  for (int i = 0; i < w * h; i++)
    {
      unsigned char *px = data + 4 * i; /* ARGB32 little endian: B,G,R,A */
      unsigned alpha = px[3];

      if (alpha > max_alpha)
        max_alpha = alpha;
      a += alpha;
      r += px[2];
      g += px[1];
      b += px[0];
    }

  if (a == 0)
    return 0;

  return ((frame ? max_alpha : (unsigned) (a / (w * h))) << 24)
         | ((unsigned) (r * 255 / a) << 16) | ((unsigned) (g * 255 / a) << 8)
         | (unsigned) (b * 255 / a);
}

static guint32
get_bg (const char *selector)
{
  Ctx ctx = ctx_from_css (selector, FALSE);
  cairo_surface_t *surface;
  cairo_t *cr;
  guint32 result;

  apply_provider (ctx, strip_borders ());
  surface = surface_new (24, 24, &cr);
  render_background (ctx, cr, 24, 24);
  result = surface_average (surface, FALSE);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  return result;
}

/* The tooltip is queried as its own root (gtk_color_mixers.cc builds the
 * context with no window node in front of it). */
static guint32
get_bg_rooted (const char *selector)
{
  Ctx ctx = ctx_from_css (selector, TRUE);
  cairo_surface_t *surface;
  cairo_t *cr;
  guint32 result;

  apply_provider (ctx, strip_borders ());
  surface = surface_new (24, 24, &cr);
  render_background (ctx, cr, 24, 24);
  result = surface_average (surface, FALSE);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  return result;
}

static guint32
get_border (const char *selector)
{
  Ctx ctx = ctx_from_css (selector, FALSE);
  cairo_surface_t *surface;
  cairo_t *cr;
  guint32 border;

  surface = surface_new (24, 24, &cr);
  gtk_render_frame (gtk_widget_get_style_context (ctx.widget), cr, 0, 0, 24,
                    24);
  border = surface_average (surface, TRUE);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  /* gtk_util.cc composites a translucent frame over its own background. */
  if ((border >> 24) == 255)
    return border;
  {
    guint32 bg = get_bg (selector);
    double sa = ((border >> 24) & 0xff) / 255.0;
    double da = ((bg >> 24) & 0xff) / 255.0;
    double oa = sa + da * (1 - sa);
    guint32 out = 0;

    if (oa <= 0)
      return 0;
    for (int i = 0; i < 3; i++)
      {
        unsigned shift = 16 - 8 * i;
        double s = ((border >> shift) & 0xff) / 255.0;
        double d = ((bg >> shift) & 0xff) / 255.0;
        out |= (unsigned) ((s * sa + d * da * (1 - sa)) / oa * 255 + 0.5)
               << shift;
      }
    return ((unsigned) (oa * 255 + 0.5) << 24) | out;
  }
}

static guint32
get_fg (const char *selector)
{
  Ctx ctx = ctx_from_css (selector, FALSE);
  GdkRGBA color;

  gtk_style_context_get_color (gtk_widget_get_style_context (ctx.widget),
                               &color);
  return ((unsigned) (color.alpha * 255 + 0.5) << 24)
         | ((unsigned) (color.red * 255 + 0.5) << 16)
         | ((unsigned) (color.green * 255 + 0.5) << 8)
         | (unsigned) (color.blue * 255 + 0.5);
}

/* GetSeparatorColor() at a horizontal separator is a 24 x thickness render
 * of background + frame; here the thickness is 1px, which is what the
 * question needs (does it paint anything at all). */
static guint32
get_separator (const char *selector)
{
  Ctx ctx = ctx_from_css (selector, FALSE);
  cairo_surface_t *surface;
  cairo_t *cr;
  guint32 result;

  surface = surface_new (24, 1, &cr);
  gtk_render_background (gtk_widget_get_style_context (ctx.widget), cr, 0, 0,
                         24, 1);
  gtk_render_frame (gtk_widget_get_style_context (ctx.widget), cr, 0, 0, 24, 1);
  result = surface_average (surface, FALSE);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  return result;
}

static guint32
over (guint32 top, guint32 bottom)
{
  double sa = ((top >> 24) & 0xff) / 255.0;
  double da = ((bottom >> 24) & 0xff) / 255.0;
  double oa = sa + da * (1 - sa);
  guint32 out = 0;

  if (oa <= 0)
    return 0;
  for (int i = 0; i < 3; i++)
    {
      unsigned shift = 16 - 8 * i;
      double s = ((top >> shift) & 0xff) / 255.0;
      double d = ((bottom >> shift) & 0xff) / 255.0;
      out |= (unsigned) ((s * sa + d * da * (1 - sa)) / oa * 255 + 0.5) << shift;
    }
  return ((unsigned) (oa * 255 + 0.5) << 24) | out;
}

/* --- the table -------------------------------------------------------- */

static int failures;

static void
row (const char *kind, const char *selector, guint32 color, gboolean fatal)
{
  if (fatal && (color >> 24) == 0)
    {
      printf ("  %-6s %-46s #%08x   PAINTS NOTHING -> Chromium reads #000000\n",
              kind, selector, color);
      failures++;
    }
  else
    printf ("  %-6s %-46s #%08x\n", kind, selector, color);
}

int
main (int argc, char **argv)
{
  const char *css = argc > 1 ? argv[1] : NULL;
  GtkSettings *settings;
  gchar *theme = NULL, *prefer_dark = NULL;
  guint32 primary_bg, frame, frame_inactive, toolbar;

  if (argc > 2)
    {
      g_printerr ("usage: %s [css-file|none]\n", argv[0]);
      return 2;
    }

  if (css == NULL)
    {
      /* Live environment: measure whatever ~/.config/gtk-4.0/gtk.css is. */
      g_setenv ("KEEP_CONFIG", "1", TRUE);
    }
  else if (g_strcmp0 (css, "none") != 0 && !g_file_test (css, G_FILE_TEST_IS_REGULAR))
    {
      g_printerr ("cannot read %s\n", css);
      return 2;
    }

  hermetic_config ();
  gtk_init ();

  if (css != NULL && g_strcmp0 (css, "none") != 0)
    {
      GtkCssProvider *provider = gtk_css_provider_new ();

      if (g_strcmp0 (g_getenv ("CONTRAST"), "more") == 0)
        g_object_set (provider, "prefers-contrast", GTK_INTERFACE_CONTRAST_MORE,
                      NULL);
      gtk_css_provider_load_from_path (provider, css);
      gtk_style_context_add_provider_for_display (
          gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
          GTK_STYLE_PROVIDER_PRIORITY_USER);
      g_object_unref (provider);
    }

  settings = gtk_settings_get_default ();
  if (g_strcmp0 (g_getenv ("SCHEME"), "dark") == 0)
    g_object_set (settings, "gtk-interface-color-scheme",
                  GTK_INTERFACE_COLOR_SCHEME_DARK, NULL);
  g_object_get (settings, "gtk-theme-name", &theme,
                "gtk-application-prefer-dark-theme", &prefer_dark, NULL);

  printf ("gtk %u.%u.%u  theme=%s  prefer-dark=%s  css=%s\n",
          gtk_get_major_version (), gtk_get_minor_version (),
          gtk_get_micro_version (), theme ? theme : "(unset)",
          prefer_dark ? prefer_dark : "(unset)", css ? css : "(live user sheet)");
  printf ("\nchromium inputs (ui/gtk/gtk_color_mixers.cc, via gtk_util.cc):\n");

  row ("bg", "\"\" = window.background", get_bg (""), TRUE);
  row ("bg", "headerbar.header-bar.titlebar",
       get_bg ("headerbar.header-bar.titlebar"), TRUE);
  row ("bg", "headerbar.header-bar.titlebar:backdrop",
       get_bg ("headerbar.header-bar.titlebar:backdrop"), TRUE);
  row ("bg", "menubar", get_bg ("menubar"), TRUE);
  row ("tooltip", "tooltip.background (own root)",
       get_bg_rooted ("tooltip.background"), TRUE);
  row ("bg", "button", get_bg ("button"), TRUE);
  row ("bg", "button.text-button:disabled",
       get_bg ("button.text-button:disabled"), TRUE);
  row ("bg", "button.text-button.toggle",
       get_bg ("button.text-button.toggle"), TRUE);
  row ("bg", "button.text-button.toggle:checked",
       get_bg ("button.text-button.toggle:checked"), TRUE);
  row ("border", "entry", get_border ("entry"), TRUE);
  row ("border", "entry:focus", get_border ("entry:focus"), TRUE);
  row ("bg", "treeview.view treeview.view.cell",
       get_bg ("treeview.view treeview.view.cell"), TRUE);
  row ("bg", "treeview.view treeview.view.cell:selected",
       get_bg ("treeview.view treeview.view.cell:selected"), TRUE);
  row ("bg", "textview.view", get_bg ("textview.view"), TRUE);
  row ("bg", "textview.view:disabled", get_bg ("textview.view:disabled"), TRUE);
  row ("bg", "popover.background.menu contents",
       get_bg ("popover.background.menu contents"), TRUE);
  row ("bg", "popover.background.menu contents modelbutton.flat",
       get_bg ("popover.background.menu contents modelbutton.flat"), TRUE);
  row ("bg", "popover.background.menu contents modelbutton.flat:hover",
       get_bg ("popover.background.menu contents modelbutton.flat:hover"), TRUE);
  row ("sep", "popover.background.menu contents separator.horizontal",
       get_separator ("popover.background.menu contents separator.horizontal"),
       TRUE);
  row ("bg", "scrollbar slider", get_bg ("scrollbar slider"), TRUE);
  row ("bg", "scrollbar trough", get_bg ("scrollbar trough"), TRUE);
  row ("bg", "scale trough", get_bg ("scale trough"), TRUE);
  row ("bg", "scale highlight", get_bg ("scale highlight"), TRUE);
  row ("border", "box.frame", get_border ("box.frame"), TRUE);
  row ("border", "frame border", get_border ("frame border"), TRUE);
  row ("bg", "statusbar", get_bg ("statusbar"), TRUE);
  row ("bg", "treeview.view button", get_bg ("treeview.view button"), TRUE);
  row ("bg", "notebook tab:checked", get_bg ("notebook tab:checked"), TRUE);
  row ("fg", "label", get_fg ("label"), FALSE);
  row ("fg", "label:disabled", get_fg ("label:disabled"), FALSE);

  primary_bg = get_bg ("");
  frame = (get_bg ("headerbar.header-bar.titlebar") & 0x00ffffff) | 0xff000000;
  frame_inactive =
      (get_bg ("headerbar.header-bar.titlebar:backdrop") & 0x00ffffff)
      | 0xff000000;
  toolbar = over (primary_bg, frame);

  printf ("\nderived (the two lines that decide the browser shell):\n");
  row ("frame", "opaque(bg(headerbar))", frame, FALSE);
  row ("frame", "opaque(bg(headerbar:backdrop))", frame_inactive, FALSE);
  row ("toolbar", "bg(\"\") over frame_color", toolbar, FALSE);
  row ("primary", "bg(\"\") = kColorPrimaryBackground", primary_bg, FALSE);

  g_free (theme);
  g_free (prefer_dark);

  if (failures)
    {
      printf ("\nFAIL: %d input(s) paint nothing; Chromium paints those black.\n",
              failures);
      return 1;
    }

  printf ("\nOK: every painted input resolves.\n");
  return 0;
}
