/*
 * SPDX-FileCopyrightText: 2026 mauriciobc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* tools/probe-accent.c — is the accent register actually live?
 *
 *   probe-accent <css-file> [upstream-css]
 *
 * Loads one CSS file at GTK_STYLE_PROVIDER_PRIORITY_USER (800) — the same
 * mechanism as the overlay, render-widget and probe-motion — and, when
 * given, a second at PRIORITY_THEME (200): pass upstream/cache/<pinned>/
 * gtk.css to stand in for libadwaita, without which every surface derived
 * from an upstream variable is invalid and nothing measures.
 *
 * THE QUESTION. The accent register is a pure function of upstream's
 * --accent-color: L0 aliases it once (--ov-up-accent) and derives the lit
 * edges and the neon pair from that with relative colour syntax. So when
 * the system accent changes, the whole material system should follow with
 * no rebuild and no restart. That is the overlay's most distinctive
 * property and it is a property of the TOKEN GRAPH, not of libadwaita —
 * which is why it is measured here rather than asserted in the README.
 *
 * The mechanism under test is the one documented in src/_tokens.scss:
 * "GTK computes a custom property that holds var() on the element that
 * declares it, and children inherit the computed value." If that were not
 * true — if a derived token froze at the value it had when the parent was
 * first computed — every rung in the register would be pinned to whatever
 * the accent was at process start, and the register would look correct in
 * a screenshot while being dead at runtime.
 *
 * So the probe re-points the UPSTREAM property (--accent-color, the tail
 * of the chain, not our alias) and asks whether the head of the chain
 * moved. Re-pointing our own alias would prove less: it would not exercise
 * the var() hop that the register actually depends on.
 *
 * Verdict per pair: LIVE when the rendered mean moved past SAME, STALE
 * when it did not. A control pair re-points a property nothing derives
 * from and must read STALE — without it, a probe that reports LIVE for
 * everything is indistinguishable from one that simply always paints.
 *
 * This tool touches NO system state: no GSettings, no libadwaita, no write
 * to the user's config, no file. What libadwaita does when the real accent
 * key changes is a separate question with a separate answer, recorded in
 * docs/decisions.md ("Accent liveness", 6 Oct 2026); this measures whether
 * OUR sheet survives such a change, which is the part that is ours.
 *
 * Build:
 *   gcc -O1 -o build/probe-accent tools/probe-accent.c \
 *       $(pkg-config --cflags --libs gtk4)
 */
#include <gtk/gtk.h>
#include <stdlib.h>

typedef struct {
  int r, g, b, a;
} Pixel;

/* GTK loads $XDG_CONFIG_HOME/gtk-4.0/gtk.css for every process, and on a
 * machine that has installed this overlay that path is a symlink to the
 * very sheet under test: the css argument would be loaded twice over, so
 * the A/B would really be A/B of the sheet against itself. Point
 * XDG_CONFIG_HOME at an empty directory so the css arguments are the only
 * stylesheets in the process. KEEP_CONFIG=1 restores the real
 * environment. libadwaita's own sheet still applies (it comes from the
 * theme search path, not from the user config), which is why upstream-css
 * stays optional. */
static void
hermetic_config (void)
{
  gchar *dir;
  if (g_getenv ("KEEP_CONFIG") != NULL)
    return;
  dir = g_dir_make_tmp ("probe-accent-XXXXXX", NULL);
  if (dir != NULL) {
    g_setenv ("XDG_CONFIG_HOME", dir, TRUE);
    g_free (dir);
  }
}

/* Anything at or under this distance reads as "the same rendering". */
#define SAME 2

/* A whole-widget MEAN is the wrong statistic for this question: the lit
 * edge is a 1px hairline on a 300x24 node, so it moves the mean by single
 * digits and sits in the same range as unrelated drift — which is exactly
 * how the first cut of this tool came to report its own negative control
 * as LIVE. So compare the two renderings pixel by pixel and report the
 * LARGEST single-pixel move. The register either lights a hairline, a gel
 * fill and a bloom somewhere, or it moves nothing at all; there is no
 * in-between to average away. */
static guchar *
snapshot (GtkWidget *w, int *out_w, int *out_h)
{
  int width, height;
  *out_w = *out_h = 0;
  if (w == NULL || !GTK_IS_WIDGET (w))
    return NULL;
  width = gtk_widget_get_width (w);
  height = gtk_widget_get_height (w);
  *out_w = width; *out_h = height;
  if (width <= 0 || height <= 0)
    return NULL;

  GdkPaintable *paintable = gtk_widget_paintable_new (w);
  GtkSnapshot *snap = gtk_snapshot_new ();
  gdk_paintable_snapshot (paintable, snap, width, height);
  GskRenderNode *node = gtk_snapshot_to_node (snap);
  if (node == NULL) {
    g_object_unref (snap);
    g_object_unref (paintable);
    return NULL;
  }
  GskRenderer *renderer = gsk_cairo_renderer_new ();
  gsk_renderer_realize (renderer, NULL, NULL);
  GdkTexture *texture = gsk_renderer_render_texture (renderer, node, NULL);

  gsize stride = 4 * (gsize) width;
  guchar *data = g_malloc0 (stride * height);
  gdk_texture_download (texture, data, stride);

  g_object_unref (texture);
  gsk_render_node_unref (node);
  gsk_renderer_unrealize (renderer);
  g_object_unref (renderer);
  g_object_unref (snap);
  g_object_unref (paintable);
  return data;
}

/* Largest per-pixel RGBA move between two renderings of the same node. */
static int
max_delta (const guchar *a, const guchar *b, int w, int h)
{
  int worst = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const guchar *pa = &a[(y * w + x) * 4];
      const guchar *pb = &b[(y * w + x) * 4];
      int d = 0;
      for (int c = 0; c < 4; c++)
        d += ABS ((int) pa[c] - (int) pb[c]);
      if (d > worst)
        worst = d;
    }
  return worst;
}

static void
pump (gint64 microseconds)
{
  gint64 end = g_get_monotonic_time () + microseconds;
  while (g_get_monotonic_time () < end)
    g_main_context_iteration (NULL, FALSE);
}

/* Re-point one custom property on :root, from a provider ABOVE the sheet
 * under test, so the only thing that changes between the two samples is
 * the value of that property. 801 rather than 800: GTK compares provider
 * priority before selector specificity, so an equal-priority provider
 * would be ambiguous.
 *
 * add_provider_for_display() takes its OWN reference, so the caller must
 * remove_provider_for_display() — unreffing alone leaves the provider
 * installed, and every later sample is then polluted by the previous
 * pair's override. That is why the control pair is not decoration: it is
 * what catches the leak, reading LIVE once overrides have piled up. */
static GtkCssProvider *
override_prop (const char *declaration)
{
  /* The trailing ";" is not optional: GTK's block parser wants it and
   * warns (loudly, on stderr) without it, which would put a parser warning
   * in the middle of the probe's own output. */
  char *css = g_strdup_printf (":root { %s; }", declaration);
  GtkCssProvider *p = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (p, css);
  g_free (css);
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (p),
                                              801);
  return p;
}

static void
drop_override (GtkCssProvider *p)
{
  if (p == NULL)
    return;
  gtk_style_context_remove_provider_for_display (gdk_display_get_default (),
                                                 GTK_STYLE_PROVIDER (p));
  g_object_unref (p);
}

/* GTK re-derives PRELIGHT/FOCUS/CHECKED from the real pointer and window
 * focus, so a manually set flag can be clobbered mid-probe. Asserting
 * immediately before the sample, with no main-loop iteration in between,
 * is what makes the numbers comparable run to run (the same reason
 * probe-motion.c carries sample_in_state). */
static void assert_state (GtkWidget *w);

/* Sample until the node stops moving.
 *
 * The first cut took ONE baseline sample and compared it with one after the
 * override. That is not enough, and the failure is not subtle: the switch
 * runs a 180ms state animation, so a baseline captured while it is still
 * settling is compared against a settled "after" and the pair reads STALE
 * whenever the timing goes the wrong way. It was reproducible enough to look
 * like a real regression — the same command on the same bytes returned LIVE
 * max_delta=709 and STALE max_delta=0 on consecutive runs, and three
 * intermediate variants of the sheet appeared to contradict each other.
 *
 * So: take samples until two consecutive ones are identical, and report
 * UNSTABLE if that never happens within the budget. A verdict computed from
 * a node that is still moving is not a measurement.
 */
#define SETTLE_TRIES 40

static int
settle_stable (GtkWidget *w, int *out_w, int *out_h, guchar **out_buf)
{
  guchar *prev = NULL, *cur = NULL;
  int pw = 0, ph = 0, cw = 0, ch = 0, ok = 0;

  for (int i = 0; i < SETTLE_TRIES; i++) {
    pump (15000);
    assert_state (w);
    cur = snapshot (w, &cw, &ch);
    if (cur == NULL)
      break;
    if (prev != NULL && cw == pw && ch == ph &&
        max_delta (prev, cur, cw, ch) <= SAME) {
      ok = 1;
      break;
    }
    g_free (prev);
    prev = cur;
    pw = cw; ph = ch;
  }
  if (prev != NULL) {
    *out_w = pw; *out_h = ph; *out_buf = prev;
  } else {
    *out_w = *out_h = 0; *out_buf = NULL;
  }
  return ok;
}

static void
assert_state (GtkWidget *w)
{
  if (GTK_IS_SWITCH (w) && gtk_switch_get_active (GTK_SWITCH (w)))
    gtk_widget_set_state_flags (w, GTK_STATE_FLAG_CHECKED, TRUE);
}

/* Re-point, pump, re-render, measure the largest pixel move, un-install.
 * The override is removed whatever the verdict.
 *
 * UNSTABLE is a real verdict and it is not a pass: it means the node was
 * still moving between the two baseline samples, so the run measured the
 * animation rather than the register and the number is meaningless. */
static const char *
run_pair (const char *prop, const char *declaration,
          GtkWidget *w, int bw, int bh, const guchar *before,
          gint64 settle_us)
{
  guchar *after;
  int aw, ah, d;
  const char *verdict;
  GtkCssProvider *p = override_prop (declaration);

  pump (settle_us);
  after = snapshot (w, &aw, &ah);
  d = (after == NULL) ? 0
                      : max_delta (before, after, bw < aw ? bw : aw,
                                   bh < ah ? bh : ah);
  g_free (after);
  drop_override (p);
  pump (settle_us);

  verdict = d > SAME ? "LIVE" : "STALE";
  g_print ("  %-22s %-26s %-8s max_delta=%-4d %s\n", prop, declaration,
           verdict, d, d > SAME ? "" : "(register did not follow)");
  return verdict;
}

int
main (int argc, char **argv)
{
  if (argc < 2) {
    g_printerr ("usage: %s css [upstream-css]\n", argv[0]);
    return 2;
  }
  hermetic_config ();
  gtk_init ();

  const char *scheme = g_getenv ("SCHEME");
  if (g_strcmp0 (scheme, "dark") == 0)
    g_object_set (gtk_settings_get_default (), "gtk-interface-color-scheme",
                  GTK_INTERFACE_COLOR_SCHEME_DARK, NULL);

  /* "none" is the stock-control sentinel every sibling tool takes
   * (render-gallery, render-widget): load no sheet at all, so a run can
   * prove the probe fails rather than passes by construction. */
  if (g_strcmp0 (argv[1], "none") != 0) {
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (provider, argv[1]);
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref (provider);
  }

  if (argc > 2) {
    GtkCssProvider *theme = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (theme, argv[2]);
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (theme),
                                                GTK_STYLE_PROVIDER_PRIORITY_THEME);
    g_object_unref (theme);
  }

  /* progressbar carries ov-lit-edge() / ov-lit-halo(), i.e. the --ov-lit-*
   * rungs directly; the suggested-action button carries the gel, i.e.
   * --ov-up-accent-fill. Two different ends of the chain, so a register
   * that is live for one and dead for the other is visible. */
  /* Hold every node explicitly. gtk_window_new() hands back a FLOATING
   * reference that nothing here sinks, and gtk_box_append sinks only its
   * own child — so a collection inside pump() could finalize the whole
   * tree between two pairs and every later sample would then measure a
   * dangling pointer (which is exactly what the first cut of this tool
   * did: it printed GTK_IS_WIDGET assertion failures and called them
   * "no size"). ref_sink the window, ref the two nodes under test. */
  GtkWidget *win = g_object_ref_sink (gtk_window_new ());
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_window_set_child (GTK_WINDOW (win), box);

  /* A CHECKED SWITCH, not a progressbar. The lit rungs only paint in a
   * state: src/surfaces/_switch.scss puts ov-lit-edge()/ov-lit-halo() on
   * `switch:checked > slider`, and _controls.scss on `scale:hover`. A
   * resting knob paints neither, so a probe pointed at one measures a
   * register that is switched off rather than a register that is dead —
   * which is what the first cut of this tool concluded, wrongly.
   * GTK re-evaluates CHECKED from the widget's own state, so it is
   * re-asserted immediately before every sample (see assert_state). */
  GtkWidget *sw = g_object_ref (gtk_switch_new ());
  gtk_switch_set_active (GTK_SWITCH (sw), TRUE);
  gtk_widget_set_size_request (sw, 80, 40);
  gtk_box_append (GTK_BOX (box), sw);

  GtkWidget *cta = g_object_ref (gtk_button_new_with_label ("Suggested"));
  gtk_widget_add_css_class (cta, "suggested-action");
  gtk_widget_set_size_request (cta, 200, 44);
  gtk_box_append (GTK_BOX (box), cta);

  gtk_window_present (GTK_WINDOW (win));
  pump (600000);

  g_print ("css: %s%s   SCHEME=%s\n",
           g_strcmp0 (argv[1], "none") == 0 ? "none (stock control)" : argv[1],
           argc > 2 ? " + theme" : "", scheme ?: "light");
  g_print ("  %-22s %-18s %-6s %s\n", "re-pointed", "on :root", "", "verdict");

  const gint64 settle = 250000;
  int live = 0, total = 0, control_ok = 0, sanity_ok = 1;
  struct {
    const char *prop;
    const char *decl;
    GtkWidget *w;
    int is_control;     /* scored the other way round: must read STALE */
  } pairs[] = {
    /* SANITY: --accent-bg-color paints the switch track outright. If this
     * does not move, the override never reached the node and every other
     * number here is void — a gate, not a data point. */
    { "--accent-bg-color", "--accent-bg-color: #d56199", sw, 0 },
    /* The real question, at the head of the chain: upstream's STANDALONE
     * colour, which L0 aliases once (--ov-up-accent) and the lit rungs
     * derive from through a single var() hop. */
    { "--accent-color", "--accent-color: #d56199", sw, 0 },
    /* The other end of the chain: the gel. Note the input is
     * --accent-bg-color and NOT --accent-color — L0 aliases the two
     * separately (--ov-up-accent-fill reads the BACKGROUND colour,
     * --ov-up-accent the standalone), so a pair that re-points
     * --accent-color at the gel is asking the wrong question and reads
     * STALE for a sheet that is perfectly live. */
    { "--accent-bg-color", "--accent-bg-color: #3a944a", cta, 0 },
    /* Negative control: nothing in the sheet derives from this, so it MUST
     * read STALE. A probe that cannot fail is not a measurement. */
    { "--ov-not-a-token", "--ov-not-a-token: 12px", sw, 1 },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (pairs); i++) {
    guchar *base = NULL;
    int w = 0, h = 0;
    const char *v;

    /* Baseline: the node must be genuinely at rest, or the number below is
     * measuring an animation rather than the register. */
    if (!settle_stable (pairs[i].w, &w, &h, &base)) {
      g_print ("  %-22s %-26s %-8s never settled in %d tries\n",
               pairs[i].prop, pairs[i].decl, "UNSTABLE", SETTLE_TRIES);
      g_free (base);
      if (i > 0) { total++; }
      continue;
    }

    v = run_pair (pairs[i].prop, pairs[i].decl, pairs[i].w, w, h, base, settle);
    g_free (base);
    if (i == 0 && g_strcmp0 (v, "LIVE") != 0) {
      /* The sanity pair is the instrument check: it proves the override
       * reached the node at all. If it did not, every LIVE below it is
       * worthless and the run must fail — an earlier version excluded it
       * from the tally without ever requiring it, so a broken instrument
       * reported success. */
      sanity_ok = 0;
      g_print ("  -> SANITY PAIR FAILED: the override never reached the node,"
               " so this run measures nothing\n");
    }
    if (pairs[i].is_control) {
      /* The control is scored backwards: it must read STALE. If it reads
       * LIVE the instrument is unsound and every LIVE above it is
       * worthless, so it fails the run on its own. */
      if (g_strcmp0 (v, "STALE") == 0)
        control_ok = 1;
    } else if (i > 0) {    /* pair 0 is the sanity gate, not a register link */
      total++;
      if (g_strcmp0 (v, "LIVE") == 0)
        live++;
    }
  }

  g_print ("\nsummary: %d/%d register links live   control %s\n",
           live, total, control_ok ? "correctly STALE" : "WRONGLY LIVE (unsound)");
  if (!sanity_ok)
    g_print ("  the sanity pair read STALE: the override did not reach the"
             " node, so no verdict below it means anything\n");
  if (!control_ok)
    g_print ("  the control moved with nothing deriving from it: this run"
             " measured noise, not the register\n");
  if (live != total)
    g_print ("  a link read STALE: the register does not re-derive from the"
             " property it was re-pointed at\n");
  return (live == total && control_ok && sanity_ok) ? 0 : 1;
}