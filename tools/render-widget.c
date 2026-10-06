/*
 * SPDX-FileCopyrightText: 2026 mauriciobc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* tools/render-widget.c — render one widget with one CSS file, offscreen.
 *
 *   render-widget <css-file> <out.tiff> <button|headerbar|box> <width> <height>
 *
 * Same mechanism as the overlay: a CssProvider on the default display at
 * GTK_STYLE_PROVIDER_PRIORITY_USER (800). Paints the widget via
 * GtkWidgetPaintable + GskCairoRenderer and saves a TIFF for pixel
 * analysis (PIL reads it).
 *
 * Note: briefly presents a window (~400 ms) — GtkWidgetPaintable only
 * renders mapped widgets. gtk_widget_paint() would avoid that, but it is
 * not exposed in the public headers.
 *
 * Build:
 *   gcc -O1 -o build/render-widget tools/render-widget.c \
 *       $(pkg-config --cflags --libs gtk4)
 *
 * Born in Evening 0 (docs/decisions.md); reused by the X5 test card.
 */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

/* GTK loads $XDG_CONFIG_HOME/gtk-4.0/gtk.css for every process, and on a
 * machine that has installed this overlay that path is a symlink to the
 * very sheet under test: the "control" run would silently carry the
 * overlay too, and every A/B would measure the same file twice. Point
 * XDG_CONFIG_HOME at an empty directory so the css argument is the only
 * stylesheet in the process. KEEP_CONFIG=1 restores the real environment. */
static void
hermetic_config (void)
{
  gchar *dir;
  if (g_getenv ("KEEP_CONFIG") != NULL)
    return;
  dir = g_dir_make_tmp ("render-widget-XXXXXX", NULL);
  if (dir != NULL) {
    g_setenv ("XDG_CONFIG_HOME", dir, TRUE);
    g_free (dir);
  }
}

int main(int argc, char **argv)
{
  if (argc != 6) {
    g_printerr("usage: %s css out widget w h\n", argv[0]);
    return 2;
  }
  hermetic_config();
  gtk_init();

  GdkDisplay *display = gdk_display_get_default();
  GtkCssProvider *provider = gtk_css_provider_new();
  const char *contrast = g_getenv("CONTRAST");
  if (contrast && strcmp(contrast, "more") == 0)
    g_object_set(provider, "prefers-contrast", GTK_INTERFACE_CONTRAST_MORE, NULL);
  gtk_css_provider_load_from_path(provider, argv[1]);
  gtk_style_context_add_provider_for_display(
      display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
  g_object_unref(provider);

  GtkWidget *widget;
  if (strcmp(argv[3], "headerbar") == 0) {
    widget = gtk_header_bar_new();
  } else if (strcmp(argv[3], "box") == 0) {
    /* a centered button inside a padded box: shows outset shadows */
    widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *btn = gtk_button_new_with_label("Test");
    gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget), btn);
  } else {
    widget = gtk_button_new_with_label("Test");
  }
  GtkWidget *win = gtk_window_new();

  gtk_window_set_child(GTK_WINDOW(win), widget);

  int w = atoi(argv[4]), h = atoi(argv[5]);
  gtk_window_set_default_size(GTK_WINDOW(win), w, h);
  gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, NULL, NULL, NULL, NULL);
  gtk_widget_allocate(widget, w, h, -1, NULL);
  gtk_widget_realize(win);
  gtk_widget_realize(widget);
  gtk_window_present(GTK_WINDOW(win));
  gint64 end = g_get_monotonic_time() + 400000;  /* 400 ms of mapping */
  while (g_get_monotonic_time() < end)
    g_main_context_iteration(NULL, FALSE);
  g_print("sizes: win %dx%d, widget %dx%d\n",
          gtk_widget_get_width(GTK_WIDGET(win)),
          gtk_widget_get_height(GTK_WIDGET(win)),
          gtk_widget_get_width(widget),
          gtk_widget_get_height(widget));

  GdkPaintable *paintable = gtk_widget_paintable_new(widget);
  GtkSnapshot *snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, snapshot, w, h);
  GskRenderNode *node = gtk_snapshot_to_node(snapshot);

  if (node == NULL) {
    g_printerr("empty render node\n");
    return 1;
  }

  GskRenderer *renderer = gsk_cairo_renderer_new();
  gsk_renderer_realize(renderer, NULL, NULL);
  GdkTexture *texture = gsk_renderer_render_texture(renderer, node, NULL);
  gdk_texture_save_to_tiff(texture, argv[2]);

  graphene_rect_t bounds;
  gsk_render_node_get_bounds(node, &bounds);
  g_print("rendered %s (%g x %g)\n", argv[2],
          bounds.size.width, bounds.size.height);
  return 0;
}
