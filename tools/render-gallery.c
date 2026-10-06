/*
 * SPDX-FileCopyrightText: 2026 mauriciobc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* tools/render-gallery.c — render the house widget gallery with one CSS file, offscreen.
 *
 *   render-gallery <css-file|none> <out-dir> [family]
 *
 * Renders the fixed list of widget families below, one real widget tree
 * per window, into <out-dir>/<family>.tiff, and prints one line per
 * rendered family:
 *
 *   <family> <w>x<h>
 *
 * <w>x<h> is the window's real size, which is also the TIFF's size.
 * `none` installs no provider at all: the stock Adwaita baseline the
 * overlay (build/gtk.css) is diffed against by tools/gallery-diff. With a
 * family argument only that family is rendered. A family that cannot be
 * built prints `<family> SKIP <reason>` on stderr and the run continues.
 *
 * Mechanism: the same one as tools/render-widget.c and
 * tools/probe-motion.c — one GtkCssProvider on the default display at
 * GTK_STYLE_PROVIDER_PRIORITY_USER (800), every family a real widget tree
 * in a real GtkWindow, painted via GtkWidgetPaintable + GskCairoRenderer
 * and saved as a TIFF (PIL reads it). Presenting a window is unavoidable:
 * GtkWidgetPaintable only holds a render node for mapped widgets.
 *
 * adw_init() is required here, not optional: it installs libadwaita's own
 * stylesheet at PRIORITY_THEME, and every overlay surface is derived from
 * its palette (--window-bg-color, --headerbar-bg-color, ...). Without it
 * those variables are undefined and the derived surfaces — the window
 * background first of all — do not paint at all.
 *
 * The process is made hermetic before anything else (see main): GTK loads
 * $XDG_CONFIG_HOME/gtk-4.0/gtk.css for every process at the same 800
 * priority this tool uses, and tools/build installs the overlay at exactly
 * that path — so without the redirect `none` is not stock, it is the
 * overlay, and both runs render byte-identical families. KEEP_CONFIG=1
 * opts back into the real user environment.
 *
 * Env knobs, mirroring probe-motion: CONTRAST=more, SCHEME=dark, REDUCE=1
 * (prefers-reduced-motion: reduce — the overlay's motion tokens collapse
 * to 0ms), plus KEEP_CONFIG=1 to keep the user's own GTK stylesheet.
 * gtk-enable-animations is always FALSE: an animation caught
 * mid-flight would make the render non-deterministic, so probe-motion's
 * NOANIM knob would be a no-op on top of it.
 *
 * STATE=<prelight|active|checked|focus-visible|drop> is a stress shot, not
 * a precise per-widget state: after mapping, the mask is applied to EVERY
 * widget in the family, so one image shows the hover (or press, or drop)
 * register of the whole surface at once. At rest the hover glow, the
 * accent drop ring and the press well are all invisible by construction —
 * that is the point of them — so this is the only way to review them per
 * family. `STATE=prelight tools/track …` pairs with it for the live
 * version.
 *
 * Each family renders after ~400ms of mapping and then a queue_draw plus a
 * couple of frames: GtkWidgetPaintable hands back the widget's last
 * rendered node, and GtkWidget skips re-snapshotting a widget that is not
 * marked dirty — a family whose tree is re-allocated after its first frame
 * would otherwise be captured at the old size. State masks a builder asks
 * for (GTK_STATE_FLAG_DROP_ACTIVE and friends) are re-asserted right
 * before the snapshot, for the reason probe-motion gives: GTK recomputes
 * PRELIGHT/BACKDROP from the real pointer and window focus.
 *
 * The popover family pops a real GtkPopover up over the window
 * (gtk_popover_popup, waited on until mapped) and then composes it into the
 * window's snapshot at its real position: a popover lives on a surface of
 * its own, so the window's own render node cannot contain it.
 *
 * Known GTK 4.22 gap: a spin button's inner widget is a GtkText, not a
 * GtkEntry, so `spinbutton > progress` cannot be fed a fraction from
 * public API (gtk_entry_set_progress_fraction needs a GtkEntry) — see
 * set_inner_progress(), which asks the editable delegate anyway.
 *
 * Build:
 *   gcc -O1 -o build/render-gallery tools/render-gallery.c \
 *       $(pkg-config --cflags --libs gtk4 libadwaita-1)
 *
 * Born in the 23 Sep 2026 widget-gallery pass (the stock/overlay pixel
 * diff behind tools/gallery-diff).
 */
#include <gtk/gtk.h>
#include <adwaita.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

/* What a builder registers here is re-asserted right before the snapshot:
 * a state mask set mid-run can be clobbered by GTK's own state re-evaluation,
 * and a spin button's value set before the window is mapped can be lost to
 * its entry text (see set_spin_value). */
#define MAX_STATES 4
#define MAX_VALUES 4

typedef struct {
  GtkWindow *window;
  GtkWidget *overlay;                  /* composed over the window, or NULL */
  char reason[160];                    /* filled in by fail() */
  GtkWidget *state_widget[MAX_STATES];
  GtkStateFlags state_mask[MAX_STATES];
  int n_states;
  GtkWidget *value_widget[MAX_VALUES];
  double value[MAX_VALUES];
  int n_values;
} Gallery;

typedef struct {
  const char *name;
  int width, height;
  GtkWidget *(*build) (Gallery *g);           /* the snapshot target */
  gboolean (*after_present) (Gallery *g);     /* NULL when unneeded */
} Family;

static void
fail (Gallery *g, const char *reason)
{
  g_strlcpy (g->reason, reason, sizeof g->reason);
}

static void
pump (gint64 microseconds)
{
  gint64 end = g_get_monotonic_time () + microseconds;

  while (g_get_monotonic_time () < end)
    g_main_context_iteration (NULL, FALSE);
}

/* Mark the whole tree dirty, not just its root: GtkWidget skips
 * re-snapshotting a widget that is not dirty, so a widget whose allocation
 * moved after its last frame would keep a render node drawn for the old
 * one. */
static void
queue_draw_tree (GtkWidget *widget)
{
  GtkWidget *child;

  gtk_widget_queue_draw (widget);
  for (child = gtk_widget_get_first_child (widget); child != NULL;
       child = gtk_widget_get_next_sibling (child))
    queue_draw_tree (child);
}

/* Every widget in the tree gets the mask — the STATE= stress shot. Applied
 * before the family's own per-widget masks so the specific ones still win
 * where a family sets them. */
static GtkStateFlags
state_mask_from_env (void)
{
  const char *state = g_getenv ("STATE");

  if (state == NULL)
    return 0;
  if (g_strcmp0 (state, "prelight") == 0)
    return GTK_STATE_FLAG_PRELIGHT;
  if (g_strcmp0 (state, "active") == 0)
    return GTK_STATE_FLAG_ACTIVE;
  if (g_strcmp0 (state, "checked") == 0)
    return GTK_STATE_FLAG_CHECKED;
  if (g_strcmp0 (state, "focus-visible") == 0)
    return GTK_STATE_FLAG_FOCUSED | GTK_STATE_FLAG_FOCUS_VISIBLE;
  if (g_strcmp0 (state, "drop") == 0)
    return GTK_STATE_FLAG_DROP_ACTIVE;
  g_printerr ("STATE=%s: unknown (prelight, active, checked, focus-visible, drop)\n",
              state);
  return 0;
}

static void
state_tree (GtkWidget *widget, GtkStateFlags mask)
{
  GtkWidget *child;

  gtk_widget_set_state_flags (widget, mask, FALSE);
  for (child = gtk_widget_get_first_child (widget); child != NULL;
       child = gtk_widget_get_next_sibling (child))
    state_tree (child, mask);
}

/* An unfocused window renders in the theme's backdrop style — dimmed
 * text, flat controls — and whether the compositor has handed our window
 * focus by the time we snapshot is not something a render harness can rely
 * on: an unfocused run and a focused run differ across the whole image.
 * Clearing BACKDROP once a frame makes every family render as the active
 * window. Unset rather than "set flags with clear", because PRELIGHT
 * shares the propagation mask and the dnd family needs it to survive.
 * A tick callback is dropped by GTK when the window is unrealized. */
static gboolean
keep_active (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
  gtk_widget_unset_state_flags (widget, GTK_STATE_FLAG_BACKDROP);
  return G_SOURCE_CONTINUE;
}

/* --- small widget helpers ------------------------------------------- */

/* A padded vertical box: the page every family arranges its widgets in. */
static GtkWidget *
page (int spacing)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, spacing);

  gtk_widget_set_margin_top (box, 16);
  gtk_widget_set_margin_bottom (box, 16);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);
  return box;
}

static void
add (GtkWidget *box, GtkWidget *child)
{
  gtk_box_append (GTK_BOX (box), child);
}

static GtkWidget *
css (GtkWidget *widget, const char *class_name)
{
  gtk_widget_add_css_class (widget, class_name);
  return widget;
}

static GtkWidget *
aligned (GtkWidget *widget, GtkAlign align)
{
  gtk_widget_set_halign (widget, align);
  return widget;
}

static GtkWidget *
label (const char *text)
{
  return aligned (gtk_label_new (text), GTK_ALIGN_START);
}

/* A caption naming the widget below it, so the TIFFs are readable. */
static GtkWidget *
caption (const char *text)
{
  return css (label (text), "dim-label");
}

static GtkWidget *
icon_button (const char *icon_name)
{
  return gtk_button_new_from_icon_name (icon_name);
}

static GtkWidget *
menu_button (const char *icon_name)
{
  GtkWidget *button = gtk_menu_button_new ();

  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (button), icon_name);
  return button;
}

static GtkWidget *
scrolled (GtkWidget *child, int height)
{
  GtkWidget *window = gtk_scrolled_window_new ();

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (window), child);
  gtk_widget_set_size_request (window, -1, height);
  return window;
}

/* Register a state mask for the pre-snapshot re-assertion. */
static void
set_state (Gallery *g, GtkWidget *widget, GtkStateFlags flags)
{
  gtk_widget_set_state_flags (widget, flags, FALSE);

  if (g->n_states < MAX_STATES)
    {
      g->state_widget[g->n_states] = widget;
      g->state_mask[g->n_states] = flags;
      g->n_states++;
    }
}

/* GtkSpinButton lays out its entry text at realize, so a value set before
 * the window is mapped can be undone: the entry keeps the empty text and
 * commits that as 0 as soon as its focus goes. Set now and re-assert once
 * the widget is really on screen. */
static void
set_spin_value (Gallery *g, GtkWidget *spin, double value)
{
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), value);

  if (g->n_values < MAX_VALUES)
    {
      g->value_widget[g->n_values] = spin;
      g->value[g->n_values] = value;
      g->n_values++;
    }
}

/* GtkSpinButton's inner widget is a GtkText, not a GtkEntry, so there is
 * no public way to give it a progress fraction on GTK 4.22
 * (gtk_entry_set_progress_fraction needs a GtkEntry). Ask the editable
 * delegate for one anyway: correct the day GTK hands an entry back. */
static void
set_inner_progress (GtkWidget *spin, double fraction)
{
  GtkEditable *delegate = gtk_editable_get_delegate (GTK_EDITABLE (spin));

  if (delegate != NULL && GTK_IS_ENTRY (delegate))
    gtk_entry_set_progress_fraction (GTK_ENTRY (delegate), fraction);
}

/* --- list item factories -------------------------------------------- */

static void
text_setup (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
  GtkWidget *widget = gtk_label_new (NULL);

  gtk_widget_set_halign (widget, GTK_ALIGN_START);
  gtk_label_set_ellipsize (GTK_LABEL (widget), PANGO_ELLIPSIZE_END);
  gtk_list_item_set_child (item, widget);
}

static void
cell_setup (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
  GtkWidget *widget = gtk_label_new (NULL);

  gtk_widget_set_size_request (widget, 76, 56);
  gtk_widget_add_css_class (widget, "card");
  gtk_list_item_set_child (item, widget);
}

static const char *
item_text (GtkListItem *item)
{
  GObject *object = gtk_list_item_get_item (item);

  return object != NULL ? gtk_string_object_get_string (GTK_STRING_OBJECT (object))
                        : "";
}

/* A cell of the grid view: one string per item. */
static void
cell_bind (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
  gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (item)),
                      item_text (item));
}

/* A cell of the column view: rows are tab-separated fields, `data` is the
 * column's field index. */
static void
field_bind (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
  int field = GPOINTER_TO_INT (data);
  char **fields = g_strsplit (item_text (item), "\t", -1);

  gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (item)),
                      field < (int) g_strv_length (fields) ? fields[field] : "");
  g_strfreev (fields);
}

/* --- families ------------------------------------------------------- */

static GtkWidget *
build_buttons (Gallery *g)
{
  GtkWidget *box = page (10);

  add (box, caption ("button"));
  add (box, gtk_button_new_with_label ("Normal"));
  add (box, css (gtk_button_new_with_label ("Flat"), "flat"));
  add (box, css (gtk_button_new_with_label ("Suggested"), "suggested-action"));
  add (box, css (gtk_button_new_with_label ("Destructive"), "destructive-action"));
  add (box, caption ("toggle.pill / check / radio"));
  add (box, aligned (css (gtk_toggle_button_new_with_label ("Pill"), "pill"),
                     GTK_ALIGN_CENTER));
  add (box, gtk_check_button_new_with_label ("Check"));
  add (box, css (gtk_check_button_new_with_label ("Radio"), "radio"));
  add (box, caption ("raised / image-button / circular / link"));
  add (box, css (gtk_button_new_with_label ("Raised"), "raised"));
  add (box, aligned (css (icon_button ("open-menu-symbolic"), "image-button"),
                     GTK_ALIGN_CENTER));
  add (box, aligned (css (css (icon_button ("list-add-symbolic"), "image-button"),
                          "circular"),
                     GTK_ALIGN_CENTER));
  add (box, css (gtk_button_new_with_label ("Link"), "link"));

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_headerbar (Gallery *g)
{
  GtkWidget *bar = gtk_header_bar_new ();
  GtkWidget *title = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *body = page (10);

  /* Decorated + a header bar as titlebar: GTK packs the window controls
   * (the close/settings buttons) into the bar itself. */
  gtk_window_set_decorated (g->window, TRUE);
  gtk_header_bar_set_show_title_buttons (GTK_HEADER_BAR (bar), TRUE);
  gtk_window_set_titlebar (g->window, bar);

  gtk_header_bar_pack_start (GTK_HEADER_BAR (bar),
                             css (icon_button ("go-previous-symbolic"), "flat"));
  gtk_header_bar_pack_start (GTK_HEADER_BAR (bar),
                             css (icon_button ("go-next-symbolic"), "flat"));
  /* the bar's own button look: an icon-only button that is not .flat */
  gtk_header_bar_pack_start (GTK_HEADER_BAR (bar),
                             css (icon_button ("document-open-symbolic"),
                                  "image-button"));

  add (title, css (label ("Gallery"), "title"));
  add (title, css (label ("header bar"), "subtitle"));
  gtk_header_bar_set_title_widget (GTK_HEADER_BAR (bar), title);

  gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), menu_button ("open-menu-symbolic"));

  add (body, caption ("window body under the header bar"));
  add (body, gtk_button_new_with_label ("Body button"));
  gtk_window_set_child (g->window, body);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_entries (Gallery *g)
{
  const char *items[] = { "One", "Two", "Three", NULL };
  GtkWidget *box = page (8);
  GtkWidget *entry;
  GtkWidget *spin;

  add (box, caption ("entry"));
  entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), "Placeholder");
  add (box, entry);

  add (box, caption ("entry.flat"));
  entry = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (entry), "Flat entry");
  add (box, css (entry, "flat"));

  add (box, caption ("searchentry"));
  add (box, gtk_search_entry_new ());

  add (box, caption ("password_entry"));
  add (box, gtk_password_entry_new ());

  add (box, caption ("entry with progress"));
  entry = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (entry), "Loading");
  gtk_entry_set_progress_fraction (GTK_ENTRY (entry), 0.35);
  gtk_entry_set_progress_pulse_step (GTK_ENTRY (entry), 0.05);
  add (box, entry);

  add (box, caption ("spinbutton.vertical"));
  spin = gtk_spin_button_new_with_range (0, 100, 1);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (spin), GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request (spin, 64, 88);
  add (box, aligned (spin, GTK_ALIGN_START));

  add (box, caption ("dropdown"));
  add (box, gtk_drop_down_new_from_strings (items));

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_spinbutton (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *spin;

  add (box, caption ("spinbutton with up/down arrows"));
  spin = gtk_spin_button_new_with_range (0, 100, 1);
  set_spin_value (g, spin, 42);
  add (box, spin);

  add (box, caption ("spinbutton.flat"));
  spin = gtk_spin_button_new_with_range (0, 100, 1);
  set_spin_value (g, spin, 7);
  add (box, css (spin, "flat"));

  add (box, caption ("spinbutton, inner entry progress"));
  spin = gtk_spin_button_new_with_range (0, 100, 1);
  set_inner_progress (spin, 0.5);
  add (box, spin);

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_textview (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *scroller;
  GtkWidget *view;
  GtkWidget *inline_view;
  GtkTextBuffer *buffer;

  add (box, caption ("scrolledwindow > textview"));
  view = gtk_text_view_new ();
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD_CHAR);
  gtk_widget_set_margin_top (view, 6);
  gtk_widget_set_margin_bottom (view, 6);
  gtk_widget_set_margin_start (view, 6);
  gtk_widget_set_margin_end (view, 6);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  gtk_text_buffer_set_text (buffer,
                            "First line of the text view.\n"
                            "Second line, a little longer than the first.\n"
                            "Third line: 0123456789.\n"
                            "Fourth line, wrapping across the view.\n"
                            "Fifth line.\n"
                            "Sixth and last line.\n",
                            -1);
  scroller = scrolled (view, 180);
  add (box, scroller);

  add (box, caption ("textview.inline"));
  inline_view = gtk_text_view_new ();
  gtk_widget_add_css_class (inline_view, "inline");
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (inline_view), GTK_WRAP_WORD_CHAR);
  gtk_widget_set_margin_top (inline_view, 6);
  gtk_widget_set_margin_start (inline_view, 6);
  add (box, scrolled (inline_view, 90));

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
list_box_row (const char *text)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *child = label (text);

  gtk_widget_set_margin_top (child, 8);
  gtk_widget_set_margin_bottom (child, 8);
  gtk_widget_set_margin_start (child, 8);
  gtk_widget_set_margin_end (child, 8);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), child);
  return row;
}

static GtkWidget *
build_lists (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *list;
  GtkWidget *boxed;
  GtkWidget *cards;
  GtkWidget *card;
  GtkWidget *plain;

  add (box, caption ("listbox with an activatable, selected row"));
  list = gtk_list_box_new ();
  for (int i = 1; i <= 3; i++)
    {
      char *text = g_strdup_printf ("Row %d", i);
      GtkWidget *row = list_box_row (text);

      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
      gtk_list_box_append (GTK_LIST_BOX (list), row);
      g_free (text);
    }
  gtk_list_box_select_row (GTK_LIST_BOX (list),
                           gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 1));
  add (box, list);

  add (box, caption ("list.boxed-list"));
  boxed = css (gtk_list_box_new (), "boxed-list");
  gtk_list_box_append (GTK_LIST_BOX (boxed), list_box_row ("Boxed row one"));
  gtk_list_box_append (GTK_LIST_BOX (boxed), list_box_row ("Boxed row two"));
  add (box, boxed);

  add (box, caption ("box.card next to a plain box"));
  cards = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  card = css (gtk_box_new (GTK_ORIENTATION_VERTICAL, 0), "card");
  gtk_widget_set_size_request (card, 180, 76);
  add (card, label ("card"));
  plain = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request (plain, 180, 76);
  add (plain, label ("plain box"));
  gtk_box_append (GTK_BOX (cards), card);
  gtk_box_append (GTK_BOX (cards), plain);
  add (box, cards);

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_cells (Gallery *g)
{
  const char *items[] = { "one", "two", "three", "four", "five", "six", NULL };
  GtkWidget *box = page (10);
  GtkWidget *flow;
  GtkSingleSelection *selection;
  GtkListItemFactory *factory;

  add (box, caption ("flowbox, one selected child"));
  flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_SINGLE);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 6);
  for (int i = 1; i <= 6; i++)
    {
      char *text = g_strdup_printf ("%d", i);
      GtkWidget *child = css (gtk_label_new (text), "card");

      gtk_widget_set_size_request (child, 76, 56);
      gtk_flow_box_append (GTK_FLOW_BOX (flow), child);
      g_free (text);
    }
  add (box, flow);
  gtk_flow_box_select_child (GTK_FLOW_BOX (flow),
                             gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (flow),
                                                              2));

  add (box, caption ("gridview over a string list, one selected"));
  selection = gtk_single_selection_new (G_LIST_MODEL (gtk_string_list_new (items)));
  gtk_single_selection_set_selected (selection, 1);
  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (cell_setup), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (cell_bind), NULL);
  add (box, gtk_grid_view_new (GTK_SELECTION_MODEL (selection), factory));

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_columns (Gallery *g)
{
  /* Three sortable string columns over one tab-separated field per row:
   * a column view binds every column's factory to the view's own items,
   * so the field index rides on the factory. */
  const char *rows[] = { "Name\tSize\tKind",
                         "Image\t2.4 MB\tPNG",
                         "Document\t18 KB\tText",
                         "Archive\t9.1 MB\tZip",
                         NULL };
  const char *titles[] = { "Name", "Size", "Kind" };
  GtkWidget *box = page (10);
  GtkWidget *view;
  GtkNoSelection *selection;
  GtkStringList *model;

  add (box, caption ("columnview, sortable columns, four rows"));
  model = gtk_string_list_new (rows);
  selection = gtk_no_selection_new (G_LIST_MODEL (model));
  view = gtk_column_view_new (GTK_SELECTION_MODEL (selection));
  gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
  gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);
  for (int field = 0; field < 3; field++)
    {
      GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
      GtkColumnViewColumn *column;

      g_signal_connect (factory, "setup", G_CALLBACK (text_setup), NULL);
      g_signal_connect (factory, "bind", G_CALLBACK (field_bind),
                        GINT_TO_POINTER (field));
      column = gtk_column_view_column_new (titles[field], factory);
      gtk_column_view_column_set_sorter (
        column,
        GTK_SORTER (gtk_string_sorter_new (
          gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string"))));
      gtk_column_view_column_set_expand (column, TRUE);
      gtk_column_view_append_column (GTK_COLUMN_VIEW (view), column);
    }
  add (box, view);

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_notebook (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *notebook = gtk_notebook_new ();
  GtkWidget *switcher = gtk_stack_switcher_new ();
  GtkWidget *stack = gtk_stack_new ();

  add (box, caption ("notebook, second tab selected"));
  for (int i = 1; i <= 3; i++)
    {
      char *text = g_strdup_printf ("Tab %d", i);

      gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                                label (text), gtk_label_new (text));
      g_free (text);
    }
  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 1);
  add (box, notebook);

  add (box, caption ("stackswitcher + stack"));
  gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (stack));
  add (box, switcher);
  for (int i = 1; i <= 3; i++)
    {
      char *text = g_strdup_printf ("Page %d", i);
      char *name = g_strdup_printf ("page%d", i);

      gtk_stack_add_titled (GTK_STACK (stack), label (text), name, text);
      g_free (text);
      g_free (name);
    }
  gtk_stack_set_visible_child_name (GTK_STACK (stack), "page2");
  gtk_widget_set_size_request (stack, -1, 72);
  add (box, stack);

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_expander (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *expander;

  add (box, caption ("expander, expanded"));
  expander = gtk_expander_new ("Expanded");
  gtk_expander_set_child (GTK_EXPANDER (expander), label ("child of the expander"));
  gtk_expander_set_expanded (GTK_EXPANDER (expander), TRUE);
  add (box, expander);

  add (box, caption ("expander, collapsed"));
  expander = gtk_expander_new ("Collapsed");
  gtk_expander_set_child (GTK_EXPANDER (expander), label ("hidden child"));
  add (box, expander);

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_calendar (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *calendar = gtk_calendar_new ();

  add (box, caption ("calendar"));
  add (box, aligned (calendar, GTK_ALIGN_START));

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
popover_button (const char *text)
{
  return aligned (css (gtk_button_new_with_label (text), "modelbutton"),
                  GTK_ALIGN_FILL);
}

static gboolean
popover_after_present (Gallery *g)
{
  /* A popover can be dismissed before it ever maps (the compositor is
   * still moving focus to a window we created a moment ago), so the popup
   * is retried and the popover itself was built without autohide. */
  for (int attempt = 0; attempt < 3; attempt++)
    {
      gint64 deadline = g_get_monotonic_time () + 500000;

      gtk_popover_popup (GTK_POPOVER (g->overlay));
      while (!gtk_widget_get_mapped (g->overlay) &&
             g_get_monotonic_time () < deadline)
        pump (50000);
      if (gtk_widget_get_mapped (g->overlay))
        return TRUE;
    }

  fail (g, "popover never mapped");
  return FALSE;
}

static GtkWidget *
build_popover (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *anchor = gtk_button_new_with_label ("Anchor");
  GtkWidget *popover = gtk_popover_new ();
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  gtk_widget_set_halign (anchor, GTK_ALIGN_CENTER);

  gtk_widget_set_margin_top (content, 6);
  gtk_widget_set_margin_bottom (content, 6);
  gtk_widget_set_margin_start (content, 6);
  gtk_widget_set_margin_end (content, 6);
  add (content, popover_button ("Model item"));
  add (content, aligned (gtk_check_button_new_with_label ("Check"),
                         GTK_ALIGN_START));
  add (content, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
  add (content, popover_button ("Second item"));
  gtk_popover_set_child (GTK_POPOVER (popover), content);
  /* Autohide would let a focus change tear the popover down between the
   * popup and the snapshot; nothing here interacts with it by hand. */
  gtk_popover_set_autohide (GTK_POPOVER (popover), FALSE);

  add (box, caption ("window body behind the popover"));
  add (box, anchor);
  add (box, label ("The popover is popped up over this window."));
  gtk_window_set_child (g->window, box);

  gtk_widget_set_parent (popover, anchor);
  g->overlay = popover;
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_controls (Gallery *g)
{
  GtkWidget *box = page (8);
  GtkWidget *widget;
  GtkWidget *scroller;
  GtkWidget *rows;

  add (box, caption ("switch on / off"));
  widget = gtk_switch_new ();
  gtk_switch_set_active (GTK_SWITCH (widget), TRUE);
  add (box, aligned (widget, GTK_ALIGN_START));
  add (box, aligned (gtk_switch_new (), GTK_ALIGN_START));

  add (box, caption ("scale horizontal"));
  widget = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_range_set_value (GTK_RANGE (widget), 35);
  gtk_scale_set_draw_value (GTK_SCALE (widget), FALSE);
  add (box, widget);

  add (box, caption ("scale vertical"));
  widget = gtk_scale_new_with_range (GTK_ORIENTATION_VERTICAL, 0, 100, 1);
  gtk_range_set_value (GTK_RANGE (widget), 60);
  gtk_scale_set_draw_value (GTK_SCALE (widget), FALSE);
  gtk_widget_set_size_request (widget, -1, 110);
  add (box, aligned (widget, GTK_ALIGN_START));

  add (box, caption ("progressbar"));
  widget = gtk_progress_bar_new ();
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.4);
  add (box, widget);

  add (box, caption ("levelbar"));
  widget = gtk_level_bar_new ();
  gtk_level_bar_set_value (GTK_LEVEL_BAR (widget), 0.6);
  add (box, widget);

  add (box, caption ("scrollbar"));
  rows = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  for (int i = 1; i <= 24; i++)
    {
      char *text = g_strdup_printf ("Scroll row %d", i);

      add (rows, label (text));
      g_free (text);
    }
  scroller = scrolled (rows, 120);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scroller), FALSE);
  add (box, scroller);

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_dnd (Gallery *g)
{
  GtkWidget *box = page (10);
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *button;
  GtkWidget *entry;

  add (box, caption ("drop-active / prelight / plain button"));
  button = gtk_button_new_with_label ("Drop here");
  set_state (g, button, GTK_STATE_FLAG_DROP_ACTIVE);
  gtk_box_append (GTK_BOX (row), button);
  button = gtk_button_new_with_label ("Hover");
  set_state (g, button, GTK_STATE_FLAG_PRELIGHT);
  gtk_box_append (GTK_BOX (row), button);
  gtk_box_append (GTK_BOX (row), gtk_button_new_with_label ("Plain"));
  add (box, row);

  add (box, caption ("entry with the drop-active flag"));
  entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), "Drop target");
  set_state (g, entry, GTK_STATE_FLAG_DROP_ACTIVE);
  add (box, entry);

  gtk_window_set_child (g->window, box);
  return GTK_WIDGET (g->window);
}

static GtkWidget *
build_adw (Gallery *g)
{
  GtkWidget *view = adw_toolbar_view_new ();
  GtkWidget *bar = adw_header_bar_new ();
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *banner = adw_banner_new ("Banner title");
  GtkWidget *tab_view = GTK_WIDGET (adw_tab_view_new ());
  GtkWidget *tab_bar = GTK_WIDGET (adw_tab_bar_new ());
  GtkWidget *status = adw_status_page_new ();

  gtk_widget_set_margin_top (content, 12);
  gtk_widget_set_margin_bottom (content, 12);
  gtk_widget_set_margin_start (content, 12);
  gtk_widget_set_margin_end (content, 12);

  adw_header_bar_pack_start (ADW_HEADER_BAR (bar),
                             icon_button ("document-open-symbolic"));
  adw_header_bar_pack_end (ADW_HEADER_BAR (bar), menu_button ("open-menu-symbolic"));
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), bar);

  adw_banner_set_revealed (ADW_BANNER (banner), TRUE);
  add (content, banner);

  /* AdwToggleGroup is `toggle-group > toggle`, not `button`: none of the
   * button material reaches it, which is how it went unstyled until the
   * 26 Sep rest-register pass. One group, second toggle active. */
  {
    GtkWidget *group = adw_toggle_group_new ();
    const char *names[] = { "Left", "Right", "Both" };

    for (int i = 0; i < 3; i++)
      {
        AdwToggle *toggle = adw_toggle_new ();

        adw_toggle_set_label (toggle, names[i]);
        adw_toggle_set_name (toggle, names[i]);
        adw_toggle_group_add (ADW_TOGGLE_GROUP (group), toggle);
      }
    adw_toggle_group_set_active_name (ADW_TOGGLE_GROUP (group), "Right");
    add (content, aligned (group, GTK_ALIGN_START));
  }

  /* A Nautilus-style path bar: the app's own classes, one well, three crumbs,
   * the last one the current folder, which Nautilus stretches to fill the
   * bar (surfaces/_pathbar.scss). Nautilus's own
   * pathbar CSS is not loaded here; the harness sheet must append it. */
  {
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    const char *crumbs[] = { "EndeavourOS", "tmp", "renders" };

    gtk_widget_add_css_class (bar, "nautilus-pathbar");
    for (int i = 0; i < 3; i++)
      {
        GtkWidget *crumb = gtk_button_new_with_label (crumbs[i]);

        gtk_widget_add_css_class (crumb, "nautilus-path-button");
        if (i == 2)
          {
            gtk_widget_add_css_class (crumb, "current-dir");
            gtk_widget_set_hexpand (crumb, TRUE);
          }
        gtk_box_append (GTK_BOX (bar), crumb);
      }
    add (content, bar);
  }

  for (int i = 1; i <= 3; i++)
    {
      char *text = g_strdup_printf ("Tab %d", i);
      GtkWidget *child = label (text);

      adw_tab_page_set_title (adw_tab_view_append (ADW_TAB_VIEW (tab_view), child),
                              text);
      g_free (text);
    }
  adw_tab_bar_set_view (ADW_TAB_BAR (tab_bar), ADW_TAB_VIEW (tab_view));
  gtk_widget_set_size_request (tab_view, -1, 120);
  add (content, tab_bar);
  add (content, tab_view);

  adw_status_page_set_icon_name (ADW_STATUS_PAGE (status), "starred-symbolic");
  adw_status_page_set_title (ADW_STATUS_PAGE (status), "Status page");
  adw_status_page_set_description (ADW_STATUS_PAGE (status), "Description text");
  add (content, status);

  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (view), content);
  gtk_window_set_child (g->window, view);
  return GTK_WIDGET (g->window);
}

static const Family families[] = {
  { "buttons",    480, 620, build_buttons,    NULL },
  { "headerbar",  560, 200, build_headerbar,  NULL },
  { "entries",    480, 560, build_entries,    NULL },
  { "spinbutton", 460, 320, build_spinbutton, NULL },
  { "textview",   520, 460, build_textview,   NULL },
  { "lists",      520, 560, build_lists,      NULL },
  { "cells",      560, 480, build_cells,      NULL },
  { "columns",    620, 340, build_columns,    NULL },
  { "notebook",   520, 440, build_notebook,   NULL },
  { "expander",   480, 320, build_expander,   NULL },
  { "calendar",   360, 360, build_calendar,   NULL },
  { "popover",    520, 420, build_popover,    popover_after_present },
  { "controls",   520, 600, build_controls,   NULL },
  { "dnd",        520, 340, build_dnd,        NULL },
  { "adw",        640, 620, build_adw,        NULL },
};

#define N_FAMILIES (G_N_ELEMENTS (families))

/* Render the window (and the popover riding over it) at exactly its real
 * size, which is the size printed for the family. */
static gboolean
render_window (Gallery *g, const char *path)
{
  int width = gtk_widget_get_width (GTK_WIDGET (g->window));
  int height = gtk_widget_get_height (GTK_WIDGET (g->window));
  GdkPaintable *paintable;
  GtkSnapshot *snapshot;
  GskRenderNode *node;
  GskRenderer *renderer;
  GdkTexture *texture;
  gboolean ok;

  if (width <= 0 || height <= 0)
    {
      fail (g, "window has no allocation");
      return FALSE;
    }

  snapshot = gtk_snapshot_new ();
  /* Clipped to the window: the composed popover can reach past its edge,
   * and the TIFF must stay exactly the size that is printed. */
  gtk_snapshot_push_clip (snapshot, &GRAPHENE_RECT_INIT (0, 0, width, height));
  paintable = gtk_widget_paintable_new (GTK_WIDGET (g->window));
  gdk_paintable_snapshot (paintable, snapshot, width, height);
  g_object_unref (paintable);

  if (g->overlay != NULL)
    {
      graphene_rect_t bounds;

      if (!gtk_widget_compute_bounds (g->overlay, GTK_WIDGET (g->window), &bounds))
        {
          fail (g, "popover has no bounds in the window");
          g_object_unref (snapshot);
          return FALSE;
        }
      paintable = gtk_widget_paintable_new (g->overlay);
      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &bounds.origin);
      gdk_paintable_snapshot (paintable, snapshot,
                              bounds.size.width, bounds.size.height);
      gtk_snapshot_restore (snapshot);
      g_object_unref (paintable);
    }

  gtk_snapshot_pop (snapshot);
  node = gtk_snapshot_to_node (snapshot);
  g_object_unref (snapshot);
  if (node == NULL)
    {
      fail (g, "empty render node");
      return FALSE;
    }

  renderer = gsk_cairo_renderer_new ();
  gsk_renderer_realize (renderer, NULL, NULL);
  texture = gsk_renderer_render_texture (renderer, node, NULL);
  gsk_render_node_unref (node);
  gsk_renderer_unrealize (renderer);
  g_object_unref (renderer);
  if (texture == NULL)
    {
      fail (g, "renderer produced no texture");
      return FALSE;
    }

  ok = gdk_texture_save_to_tiff (texture, path);
  g_object_unref (texture);
  if (!ok)
    {
      fail (g, "could not write the TIFF");
      return FALSE;
    }
  return TRUE;
}

static void
render_family (const Family *family, const char *out_dir)
{
  Gallery g = { 0 };
  char *path = g_strdup_printf ("%s.tiff", family->name);
  char *full = g_build_filename (out_dir, path, NULL);
  gboolean ok;

  g.window = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_default_size (g.window, family->width, family->height);

  if (family->build (&g) == NULL)
    {
      g_printerr ("%s SKIP %s\n", family->name, g.reason);
      goto out;
    }

  gtk_window_present (g.window);
  gtk_widget_add_tick_callback (GTK_WIDGET (g.window), keep_active, NULL, NULL);
  pump (400000);

  /* GTK hands the initial keyboard focus to the first focusable widget in
   * a newly mapped window, which colours that widget and puts a caret in
   * whichever text widget it lands on — probe-motion drops the focus for
   * the same reason. Cleared twice: the initial focus can land after the
   * 400ms of mapping. */
  gtk_window_set_focus (g.window, NULL);

  if (family->after_present != NULL && !family->after_present (&g))
    {
      g_printerr ("%s SKIP %s\n", family->name, g.reason);
      goto out;
    }

  /* The mask first, then the frame: a state flag set after the last frame
   * would still be missing from the render node we read back. */
  GtkStateFlags state_mask = state_mask_from_env ();
  if (state_mask != 0)
    state_tree (GTK_WIDGET (g.window), state_mask);
  for (int i = 0; i < g.n_states; i++)
    gtk_widget_set_state_flags (g.state_widget[i], g.state_mask[i], FALSE);
  for (int i = 0; i < g.n_values; i++)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (g.value_widget[i]), g.value[i]);
  gtk_window_set_focus (g.window, NULL);
  queue_draw_tree (GTK_WIDGET (g.window));
  if (g.overlay != NULL)
    queue_draw_tree (g.overlay);
  pump (200000);

  ok = render_window (&g, full);
  if (ok)
    g_print ("%s %dx%d\n", family->name,
             gtk_widget_get_width (GTK_WIDGET (g.window)),
             gtk_widget_get_height (GTK_WIDGET (g.window)));
  else
    g_printerr ("%s SKIP %s\n", family->name, g.reason);

out:
  /* The popover is parented to a widget in the window but lives on its
   * own surface: GTK wants it unparented before the window goes. */
  if (g.overlay != NULL && gtk_widget_get_parent (g.overlay) != NULL)
    gtk_widget_unparent (g.overlay);
  gtk_window_destroy (g.window);
  pump (100000);
  g_free (path);
  g_free (full);
}

static void
usage (const char *program)
{
  g_printerr ("usage: %s <css-file|none> <out-dir> [family]\n"
              "families:", program);
  for (guint i = 0; i < N_FAMILIES; i++)
    g_printerr (" %s", families[i].name);
  g_printerr ("\n");
}

int
main (int argc, char **argv)
{
  const char *css, *out_dir, *only = NULL;
  GtkSettings *settings;

  if (argc < 3 || argc > 4)
    {
      usage (argv[0]);
      return 2;
    }
  css = argv[1];
  out_dir = argv[2];
  if (argc == 4)
    only = argv[3];

  /* Hermetic by default: GTK loads $XDG_CONFIG_HOME/gtk-4.0/gtk.css for
   * every process at the very priority this tool uses (800), and on a
   * machine where tools/build has installed the overlay there, `none`
   * would not be stock at all — the overlay is in the process before our
   * provider is. Point the config dir at an empty one so the css-file
   * argument is the only stylesheet in play; KEEP_CONFIG=1 asks for the
   * real user environment instead. */
  if (g_getenv ("KEEP_CONFIG") == NULL)
    {
      char *dir = g_build_filename (g_get_tmp_dir (), "render-gallery-config", NULL);

      if (g_mkdir_with_parents (dir, 0700) == 0)
        g_setenv ("XDG_CONFIG_HOME", dir, TRUE);
      else
        g_printerr ("cannot create %s; keeping the user stylesheet\n", dir);
      g_free (dir);
    }

  if (only != NULL)
    {
      guint i;

      for (i = 0; i < N_FAMILIES; i++)
        if (g_strcmp0 (families[i].name, only) == 0)
          break;
      if (i == N_FAMILIES)
        {
          g_printerr ("unknown family: %s\n", only);
          usage (argv[0]);
          return 2;
        }
    }

  if (g_mkdir_with_parents (out_dir, 0755) != 0)
    {
      g_printerr ("cannot create %s\n", out_dir);
      return 1;
    }

  gtk_init ();
  /* libadwaita's stylesheet at PRIORITY_THEME: the overlay derives every
   * surface from its palette, and without it those variables are invalid
   * and the derived surfaces do not paint. */
  adw_init ();

  settings = gtk_settings_get_default ();
  /* Deterministic renders: no transition may be mid-flight when the
   * snapshot is taken, and a blinking caret would land somewhere else in
   * every run. probe-motion's NOANIM knob is on permanently here. */
  g_object_set (settings, "gtk-enable-animations", FALSE, NULL);
  g_object_set (settings, "gtk-cursor-blink", FALSE, NULL);

  /* Scheme goes on GtkSettings, not the provider (the upstream palette is
   * defined inside the theme's own scheme media queries), and libadwaita
   * is told too: its own palette is what the overlay's --ov-* tokens are
   * derived from, so both have to agree. */
  if (g_strcmp0 (g_getenv ("SCHEME"), "dark") == 0)
    {
      g_object_set (settings, "gtk-interface-color-scheme",
                    GTK_INTERFACE_COLOR_SCHEME_DARK, NULL);
      adw_style_manager_set_color_scheme (adw_style_manager_get_default (),
                                          ADW_COLOR_SCHEME_FORCE_DARK);
    }

  if (g_strcmp0 (css, "none") != 0)
    {
      GtkCssProvider *provider;

      if (!g_file_test (css, G_FILE_TEST_IS_REGULAR))
        {
          g_printerr ("cannot read %s\n", css);
          return 1;
        }

      provider = gtk_css_provider_new ();
      /* The provider's own media queries do not follow GtkSettings here
       * (same as prefers-contrast below): without this the overlay's
       * `prefers-color-scheme: dark` token blocks never applied under
       * SCHEME=dark — found 26 Sep 2026 by the gel's dark-only deepen. */
      if (g_strcmp0 (g_getenv ("SCHEME"), "dark") == 0)
        g_object_set (provider, "prefers-color-scheme",
                      GTK_INTERFACE_COLOR_SCHEME_DARK, NULL);
      if (g_strcmp0 (g_getenv ("CONTRAST"), "more") == 0)
        g_object_set (provider, "prefers-contrast",
                      GTK_INTERFACE_CONTRAST_MORE, NULL);
      if (g_strcmp0 (g_getenv ("REDUCE"), "1") == 0)
        g_object_set (provider, "prefers-reduced-motion",
                      GTK_REDUCED_MOTION_REDUCE, NULL);
      gtk_css_provider_load_from_path (provider, css);
      gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                  GTK_STYLE_PROVIDER (provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_USER);
      g_object_unref (provider);
    }

  for (guint i = 0; i < N_FAMILIES; i++)
    if (only == NULL || g_strcmp0 (families[i].name, only) == 0)
      render_family (&families[i], out_dir);

  return 0;
}
