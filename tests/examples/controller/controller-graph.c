/* GStreamer
 * Copyright (C) <2015> Stefan Sauer <ensonic@users.sf.net>
 *
 * controller-graph: explore interpolation types
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include <glib.h>
#include <gtk/gtk.h>

#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>

GtkWidget *graph;
GstControlSource *cs = NULL;
gdouble yval[] = { 0.0, 0.2, 0.8, 0.1, 0.1, 1.0 };

static gboolean
on_graph_draw (GtkWidget * widget, cairo_t * cr, gpointer user_data)
{
  GtkStyleContext *style = gtk_widget_get_style_context (widget);
  GtkAllocation alloc;
  gint x, y, w, h;
  gdouble *data;
  guint64 ts, ts_step;
  gint i;
  GstTimedValueControlSource *tvcs = (GstTimedValueControlSource *) cs;

  gtk_widget_get_allocation (widget, &alloc);
  w = alloc.width;
  h = alloc.height;
  gtk_render_background (style, cr, 0, 0, w, h);
  // add some border:
  x = 5;
  y = 5;
  w -= (x + x);
  h -= (y + y);

  // build graph
  ts = G_GUINT64_CONSTANT (0);
  ts_step = w / (G_N_ELEMENTS (yval) - 1);
  gst_timed_value_control_source_unset_all (tvcs);
  for (i = 0; i < G_N_ELEMENTS (yval); i++) {
    gst_timed_value_control_source_set (tvcs, ts, yval[i]);
    ts += ts_step;
  }
  data = g_new (gdouble, w);
  gst_control_source_get_value_array (cs, 0, 1, w, data);

  // draw background
  cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
  cairo_rectangle (cr, x, y, w, h);
  cairo_stroke_preserve (cr);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_fill (cr);


  // plot graph
  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_set_line_width (cr, 1.0);
  cairo_move_to (cr, x, y + data[0] * h);
  for (i = 1; i < w; i++) {
    cairo_line_to (cr, x + i, y + CLAMP (data[i], 0.0, 1.0) * h);
  }
  cairo_stroke (cr);

  // plot control points
  ts = G_GUINT64_CONSTANT (0);
  for (i = 0; i < G_N_ELEMENTS (yval); i++) {
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_arc (cr, x + ts, y + yval[i] * h, 3.0, 0.0, 2 * M_PI);
    cairo_stroke_preserve (cr);
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_fill (cr);
    ts += ts_step;
  }

  g_free (data);

  return TRUE;
}

static void
on_mode_changed (GtkComboBox * combo, gpointer user_data)
{
  g_object_set (cs, "mode", gtk_combo_box_get_active (combo), NULL);
  gtk_widget_queue_draw (graph);
}

static void
on_yval_changed (GtkSpinButton * spin, gpointer user_data)
{
  guint ix = GPOINTER_TO_UINT (user_data);
  yval[ix] = gtk_spin_button_get_value (spin);
  gtk_widget_queue_draw (graph);
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *layout, *label, *combo, *box, *spin;
  GEnumClass *enum_class;
  GEnumValue *enum_value;
  gint i;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  cs = gst_interpolation_control_source_new ();
  g_object_set (cs, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (window), "delete-event", gtk_main_quit, NULL);
  gtk_window_set_default_size (GTK_WINDOW (window), 320, 240);
  gtk_window_set_title (GTK_WINDOW (window),
      "GstInterpolationControlSource demo");

  layout = gtk_grid_new ();

  graph = gtk_drawing_area_new ();
  gtk_widget_add_events (graph, GDK_POINTER_MOTION_MASK);
  g_signal_connect (graph, "draw", G_CALLBACK (on_graph_draw), NULL);
  g_object_set (graph, "hexpand", TRUE, "vexpand", TRUE, "margin-bottom", 3,
      NULL);
  gtk_grid_attach (GTK_GRID (layout), graph, 0, 0, 2, 1);

  // add controls to move the yvals
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
  g_object_set (box, "homogeneous", TRUE, "margin-bottom", 3, NULL);
  for (i = 0; i < G_N_ELEMENTS (yval); i++) {
    spin = gtk_spin_button_new_with_range (0.0, 1.0, 0.05);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), yval[i]);
    g_signal_connect (spin, "changed", G_CALLBACK (on_yval_changed),
        GUINT_TO_POINTER (i));
    gtk_container_add (GTK_CONTAINER (box), spin);
  }
  gtk_grid_attach (GTK_GRID (layout), box, 0, 1, 2, 1);

  // combo for interpolation modes
  label = gtk_label_new ("interpolation mode");
  gtk_grid_attach (GTK_GRID (layout), label, 0, 2, 1, 1);

  combo = gtk_combo_box_text_new ();
  enum_class = g_type_class_ref (GST_TYPE_INTERPOLATION_MODE);
  for (i = enum_class->minimum; i <= enum_class->maximum; i++) {
    if ((enum_value = g_enum_get_value (enum_class, i))) {
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo),
          enum_value->value_nick);
    }
  }
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
      GST_INTERPOLATION_MODE_LINEAR);
  g_signal_connect (combo, "changed", G_CALLBACK (on_mode_changed), NULL);
  g_object_set (combo, "hexpand", TRUE, "margin-left", 3, NULL);
  gtk_grid_attach (GTK_GRID (layout), combo, 1, 2, 1, 1);

  gtk_container_set_border_width (GTK_CONTAINER (window), 6);
  gtk_container_add (GTK_CONTAINER (window), layout);
  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
