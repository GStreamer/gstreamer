/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
#include <config.h>
#endif

#include "metadata_editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gst/gst.h>

#if !GTK_CHECK_VERSION (2, 17, 7)
static void
gtk_widget_get_allocation (GtkWidget * w, GtkAllocation * a)
{
  *a = w->allocation;
}
#endif

/*
 * Global constants
 */

enum
{
  COL_TAG = 0,
  COL_VALUE,
  NUM_COLS
};
/* *INDENT-OFF* */
typedef enum _AppOptions {
  APP_OPT_DEMUX_EXIF = (1 << 0),
  APP_OPT_DEMUX_IPTC = (1 << 1),
  APP_OPT_DEMUX_XMP  = (1 << 2),
  APP_OPT_MUX_EXIF   = (1 << 3),
  APP_OPT_MUX_IPTC   = (1 << 4),
  APP_OPT_MUX_XMP    = (1 << 5),
  APP_OPT_ALL        = (1 << 6) - 1,
} AppOptions;

#define ENC_ERROR   (-1)
#define ENC_DONE    (0)
#define ENC_UNKNOWN (1)

/* *INDENT-OFF* */

/*
 * functions prototypes
 */

/* gstreamer related functions */

static void me_gst_cleanup_elements ();
static int me_gst_setup_view_pipeline (const gchar * filename);
static int
me_gst_setup_capture_pipeline (const gchar * src_file, const gchar * dest_file,
    gint * encode_status, gboolean use_v4l2);
static int
me_gst_setup_encode_pipeline (const gchar * src_file, const gchar * dest_file,
    gint * encode_status);

/* ui related functions */

static void ui_refresh (void);
static void process_file(void);

/*
 * Global Vars 
 */

GstElement *gst_source = NULL;
GstElement *gst_metadata_demux = NULL;
GstElement *gst_metadata_mux = NULL;
GstElement *gst_image_dec = NULL;
GstElement *gst_image_enc = NULL;
GstElement *gst_video_scale = NULL;
GstElement *gst_video_convert = NULL;
GstElement *gst_video_sink = NULL;
GstElement *gst_file_sink = NULL;
GstElement *gst_pipeline = NULL;

GdkPixbuf *last_pixbuf = NULL;  /* image as pixbuf at original size */
GdkPixbuf *draw_pixbuf = NULL;  /* pixbuf resized for drawing       */

AppOptions app_options = APP_OPT_ALL;

GstTagList *tag_list = NULL;

GtkBuilder *builder = NULL;
GtkWidget *ui_main_window = NULL;
GtkWidget *ui_drawing = NULL;
GtkWidget *ui_tree = NULL;

GtkEntry *ui_entry_insert_tag = NULL;
GtkEntry *ui_entry_insert_value = NULL;

GtkToggleButton *ui_chk_bnt_capture_v4l2 = NULL;
GtkToggleButton *ui_chk_bnt_capture_test = NULL;

GString *filename = NULL;

/*
 * Helper functions
 */

static void
dump_tag_buffer(const char *tag, guint8 * buf, guint32 size)
{
  guint32 i;
  printf("\nDumping %s (size = %u)\n\n", tag, size);

  for(i=0; i<size; ++i) {

    if (i % 16 == 0)
      printf("%04x:%04x | ", i >> 16, i & 0xFFFF);

    printf("%02x", buf[i]);

    if (i % 16 != 15)
      printf(" ");
    else
      printf("\n");

  }

  printf("\n\n");

}

static void
insert_tag_on_tree (const GstTagList * list, const gchar * tag,
    gpointer user_data)
{
  gchar *str = NULL;
  GtkTreeView *tree_view = NULL;
  GtkTreeStore *tree_store = NULL;
  GtkTreeIter iter;

  tree_view = GTK_TREE_VIEW (user_data);

  if (gst_tag_get_type (tag) == G_TYPE_STRING) {
    if (!gst_tag_list_get_string_index (list, tag, 0, &str))
      g_assert_not_reached ();
  } else if ( gst_tag_get_type (tag) == GST_TYPE_BUFFER ) {
    const GValue *val = NULL;
    GstBuffer *buf;
    val = gst_tag_list_get_value_index (list, tag, 0);
    buf = gst_value_get_buffer (val);
    dump_tag_buffer(tag, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
    str = g_strdup("It has been printed to stdout");
  } else {
    str = g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, 0));
  }

  tree_store = GTK_TREE_STORE (gtk_tree_view_get_model (tree_view));
  gtk_tree_store_append (tree_store, &iter, NULL);
  gtk_tree_store_set (tree_store, &iter, COL_TAG, tag, COL_VALUE, str, -1);

  g_free (str);

}

static gboolean
change_tag_list (GstTagList ** list, const gchar * tag, const gchar * value)
{
  GType type;
  gboolean ret = FALSE;

  if (list == NULL || tag == NULL || value == NULL)
    goto done;

  if (!gst_tag_exists (tag)) {
    fprintf (stderr, "%s is not a GStreamer registered tag\n", tag);
    goto done;
  }

  if (*list == NULL)
    *list = gst_tag_list_new ();

  type = gst_tag_get_type (tag);

  if (type == GST_TYPE_FRACTION) {
    /* FIXME: Ask GStreamer guys to add GST_FRACTION support to TAGS */
    /* Even better: ask GLib guys to add this type */
    gint n, d;

    sscanf (value, "%d/%d", &n, &d);
    gst_tag_list_add (*list, GST_TAG_MERGE_REPLACE, tag, n, d, NULL);
    ret = TRUE;
  } else {
    switch (type) {
      case G_TYPE_STRING:
        gst_tag_list_add (*list, GST_TAG_MERGE_REPLACE, tag, value, NULL);
        ret = TRUE;
        break;
      case G_TYPE_FLOAT:
      {
        gfloat fv = (gfloat) g_strtod (value, NULL);

        gst_tag_list_add (*list, GST_TAG_MERGE_REPLACE, tag, fv, NULL);
        ret = TRUE;
      }
        break;
      case G_TYPE_INT:
        /* fall through */
      case G_TYPE_UINT:
      {
        gint iv = atoi (value);

        gst_tag_list_add (*list, GST_TAG_MERGE_REPLACE, tag, iv, NULL);
        ret = TRUE;
      }
        break;
      default:
        g_printerr ("Tags of type '%s' are not supported yet for editing"
                    " by this application.\n",
            g_type_name (type));
        break;
    }
  }

done:

  return ret;

}

static void
update_draw_pixbuf (guint max_width, guint max_height)
{
  gdouble wratio, hratio;
  gint w = 0, h = 0;

  if (last_pixbuf) {
    w = gdk_pixbuf_get_width (last_pixbuf);
    h = gdk_pixbuf_get_height (last_pixbuf);
  }

  g_print ("Allocation: %dx%d, pixbuf: %dx%d\n", max_width, max_height, w, h);

  if (last_pixbuf == NULL)
    return;

  g_return_if_fail (max_width > 0 && max_height > 0);
  wratio = w * 1.0 / max_width * 1.0;
  hratio = h * 1.0 / max_height;
  g_print ("ratios = %.2f / %.2f\n", wratio, hratio);

  if (hratio > wratio) {
    w = (gint) (w * 1.0 / hratio);
    h = (gint) (h * 1.0 / hratio);
  } else {
    w = (gint) (w * 1.0 / wratio);
    h = (gint) (h * 1.0 / wratio);
  }

  if (draw_pixbuf != NULL && gdk_pixbuf_get_width (draw_pixbuf) == w &&
      gdk_pixbuf_get_height (last_pixbuf) == h) {
    return; /* nothing to do */
  }

  g_print ("drawing pixbuf at %dx%d\n", w, h);

  if (draw_pixbuf)
    g_object_unref (draw_pixbuf);

  draw_pixbuf =
      gdk_pixbuf_scale_simple (last_pixbuf, w, h, GDK_INTERP_BILINEAR);
  
}

static void
ui_drawing_size_allocate_cb (GtkWidget * drawing_area,
    GtkAllocation * allocation, gpointer data)
{
  update_draw_pixbuf (allocation->width, allocation->height);
}

/*
 * UI handling functions (mapped by GtkBuilder)
 */

gboolean
on_drawingMain_expose_event (GtkWidget * widget, GdkEventExpose * event,
    gpointer data)
{
  GtkAllocation a;
  cairo_t *cr;
  GdkRectangle rect;

  gtk_widget_get_allocation (widget, &a);

  if (draw_pixbuf == NULL)
    return FALSE;

  rect.width = gdk_pixbuf_get_width (draw_pixbuf);
  rect.height = gdk_pixbuf_get_height (draw_pixbuf);

  /* center image */
  rect.x = (a.width - rect.width) / 2;
  rect.y = (a.height - rect.height) / 2;

  /* sanity check, shouldn't happen */
  if (rect.x < 0)
    rect.x = 0;
  if (rect.y < 0)
    rect.y = 0;

  cr = gdk_cairo_create (event->window);

  gdk_cairo_set_source_pixbuf (cr, draw_pixbuf, 0, 0);
  gdk_cairo_rectangle (cr, &rect);
  cairo_fill (cr);

  cairo_destroy (cr);

  return TRUE; /* handled expose event */
}

void
on_windowMain_delete_event (GtkWidget * widget, GdkEvent * event,
    gpointer user_data)
{
  gst_element_set_state (gst_pipeline, GST_STATE_NULL);
  gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  gtk_main_quit ();
}

void
on_buttonInsert_clicked (GtkButton * button, gpointer user_data)
{
  GtkTreeStore *store = NULL;
  GtkTreeIter iter;
  const gchar *tag = gtk_entry_get_text (ui_entry_insert_tag);
  const gchar *value = gtk_entry_get_text (ui_entry_insert_value);

  if ( tag_list == NULL ) {
    tag_list = gst_tag_list_new ();
  }

  if (tag && value && tag[0] != '\0') {

    /* insert just new tags (the ones already in list should be modified) */
    if (gst_tag_list_get_tag_size (tag_list, tag)) {
      fprintf (stderr, "%s tag is already in the list try to modify it\n", tag);
    } else {
      if (change_tag_list (&tag_list, tag, value)) {
        /* just add to ui_tree if it has been added to tag_list */
        store =
            GTK_TREE_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (ui_tree)));
        gtk_tree_store_append (store, &iter, NULL);
        gtk_tree_store_set (store, &iter, COL_TAG, tag, COL_VALUE, value, -1);
      }
    }

  }

  return;

}

static void
setup_new_filename (GString * str, const gchar * ext)
{
  int i = 0;

  for (i = str->len - 1; i > 0; --i) {
    if (str->str[i] == '/') {
      ++i;
      break;
    }
  }
  g_string_insert (str, i, "_new_");
  if (ext) {
    int len = strlen (ext);

    if (len > str->len)
      g_string_append (str, ext);
    else if (strcasecmp (ext, &str->str[str->len - len]))
      g_string_append (str, ext);

  }
}

void
on_buttonSaveFile_clicked (GtkButton * button, gpointer user_data)
{

  GString *src_file = NULL;
  gint enc_status = ENC_UNKNOWN;
  const gboolean use_v4l2 =
    gtk_toggle_button_get_active (ui_chk_bnt_capture_v4l2);
  const gboolean use_test =
    gtk_toggle_button_get_active (ui_chk_bnt_capture_test);

  gst_element_set_state (gst_pipeline, GST_STATE_NULL);
  gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  src_file = g_string_new (filename->str);

  if (use_v4l2 || use_test) {
    setup_new_filename (filename, ".jpg");
    if (me_gst_setup_capture_pipeline (src_file->str, filename->str,
        &enc_status, use_v4l2)) {
      goto done;
    }
  } else {
    setup_new_filename (filename, NULL);
    if (me_gst_setup_encode_pipeline (src_file->str, filename->str,
            &enc_status)) {
      goto done;
    }
  }

  ui_refresh ();
  remove (filename->str);

  if (tag_list && gst_metadata_mux) {
    GstTagSetter *setter = GST_TAG_SETTER (gst_metadata_mux);

    if (setter) {
      gst_element_set_state (gst_pipeline, GST_STATE_READY);
      gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      gst_tag_setter_merge_tags (setter, tag_list, GST_TAG_MERGE_REPLACE);

    }
  }

  gst_element_set_state (gst_pipeline, GST_STATE_PLAYING);
  gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* wait until finished */
  gtk_main ();

  gst_element_set_state (gst_pipeline, GST_STATE_NULL);
  gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (enc_status == ENC_DONE) {

    /* view new file */
    if (tag_list) {
      gst_tag_list_unref (tag_list);
      tag_list = NULL;
    }

    me_gst_setup_view_pipeline (filename->str);

    gst_element_set_state (gst_pipeline, GST_STATE_PLAYING);
    gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  }

done:

  if (src_file)
    g_string_free (src_file, TRUE);

}

void
on_checkbuttonCaptureV4l2_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    gtk_toggle_button_set_active(ui_chk_bnt_capture_test, FALSE);
}

void
on_checkbuttonCaptureTest_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    gtk_toggle_button_set_active(ui_chk_bnt_capture_v4l2, FALSE);
}

void
on_checkbuttonOptionsDemuxExif_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    app_options |= APP_OPT_DEMUX_EXIF;
  else
    app_options &= ~APP_OPT_DEMUX_EXIF;
}

void
on_checkbuttonOptionsDemuxIptc_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    app_options |= APP_OPT_DEMUX_IPTC;
  else
    app_options &= ~APP_OPT_DEMUX_IPTC;
}

void
on_checkbuttonOptionsDemuxXmp_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    app_options |= APP_OPT_DEMUX_XMP;
  else
    app_options &= ~APP_OPT_DEMUX_XMP;
}

void
on_checkbuttonOptionsMuxExif_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    app_options |= APP_OPT_MUX_EXIF;
  else
    app_options &= ~APP_OPT_MUX_EXIF;
}

void
on_checkbuttonOptionsMuxIptc_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    app_options |= APP_OPT_MUX_IPTC;
  else
    app_options &= ~APP_OPT_MUX_IPTC;
}

void
on_checkbuttonOptionsMuxXmp_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton))
    app_options |= APP_OPT_MUX_XMP;
  else
    app_options &= ~APP_OPT_MUX_XMP;
}

void
on_buttonOpenFile_clicked (GtkButton * button, gpointer user_data)
{
  GtkWidget *dialog;
  gboolean open = FALSE;

  dialog = gtk_file_chooser_dialog_new ("Open File",
                                        GTK_WINDOW(ui_main_window),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                        NULL);

  if (filename) {
    const char *p = filename->str;
    char *q = filename->str + filename->len - 1;
    for (;p != q; --q) {
      if ( *q == '/' )
        break;
    }
    if ( p != q )
      *q = '\0';
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (dialog),
        filename->str);
    if ( p != q )
      *q = '/';
  }

  open = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT;

  if (open) {
    char *str;

    str = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
    if (filename)
      g_string_free (filename, TRUE);
    filename = g_string_new(str);
    g_free (str);
  }

  gtk_widget_destroy (dialog);

  if (open) {
    process_file();
  }

}


/*
 * UI handling functions
 */

void
on_cell_edited (GtkCellRendererText * renderer, gchar * str_path,
    gchar * new_text, gpointer user_data)
{
  GtkTreePath *path = NULL;
  GtkTreeIter iter;
  GtkTreeModel *model = NULL;
  const gint col_index = GPOINTER_TO_INT (user_data);
  const gchar *tag = gtk_entry_get_text (ui_entry_insert_tag);

  path = gtk_tree_path_new_from_string (str_path);
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (ui_tree));

  if (change_tag_list (&tag_list, tag, new_text)) {

    if (gtk_tree_model_get_iter (model, &iter, path)) {
      gtk_tree_store_set (GTK_TREE_STORE (model), &iter, col_index, new_text,
          -1);
      gtk_entry_set_text (ui_entry_insert_value, new_text);
    }

  }

  if (path)
    gtk_tree_path_free (path);

}

static void
on_tree_selection_changed (GtkTreeSelection * selection, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;

  if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
    gchar *tag;
    gchar *value;

    gtk_tree_model_get (model, &iter, COL_TAG, &tag, -1);
    gtk_tree_model_get (model, &iter, COL_VALUE, &value, -1);

    gtk_entry_set_text (ui_entry_insert_tag, tag);
    gtk_entry_set_text (ui_entry_insert_value, value);

    g_free (value);
    g_free (tag);

  }

}

/*
 * UI helper functions
 */

static int
ui_add_columns (GtkTreeView * tree_view, const gchar * title, gint col_index,
    gboolean editable)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *tree_col;
  int ret = 0;

  renderer = gtk_cell_renderer_text_new ();

  if (editable) {
    g_object_set (renderer, "editable", TRUE, NULL);
    g_signal_connect (G_OBJECT (renderer), "edited",
        G_CALLBACK (on_cell_edited), GINT_TO_POINTER (col_index));
  }

  if ((tree_col = gtk_tree_view_column_new_with_attributes (title, renderer,
              "text", col_index, NULL))) {
    gtk_tree_view_append_column (tree_view, tree_col);
  } else {
    fprintf (stderr, "UI: could not create column %s\n", title);
    ret = -201;
    goto done;
  }

done:

  return ret;

}


static int
ui_setup_tree_view (GtkTreeView * tree_view)
{
  int ret = 0;
  GtkTreeStore *tree_store = NULL;
  GtkTreeSelection *select;

  if ((ret = ui_add_columns (tree_view, "tag", COL_TAG, FALSE)))
    goto done;

  if ((ret = ui_add_columns (tree_view, "value", COL_VALUE, TRUE)))
    goto done;

  tree_store = gtk_tree_store_new (NUM_COLS, G_TYPE_STRING, G_TYPE_STRING);

  gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (tree_store));

  select = gtk_tree_view_get_selection (tree_view);
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed",
      G_CALLBACK (on_tree_selection_changed), NULL);

done:

  if (tree_store)
    g_object_unref (tree_store);

  return ret;
}

static void
ui_refresh (void)
{
  GtkTreeStore *store =
      GTK_TREE_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (ui_tree)));
  gtk_tree_store_clear (store);
  if (filename)
    gtk_window_set_title (GTK_WINDOW (ui_main_window), filename->str);
}

static int
ui_create (void)
{
  GError *error = NULL;
  int ret = 0;

  builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder, "metadata_editor.ui", &error))
  {
    g_warning ("Couldn't load builder file: %s", error->message);
    g_error_free (error);
    ret = -101;
    goto done;
  }

  ui_main_window = GTK_WIDGET (gtk_builder_get_object (builder, "windowMain"));

  ui_drawing = GTK_WIDGET (gtk_builder_get_object (builder, "drawingMain"));

  ui_tree = GTK_WIDGET (gtk_builder_get_object (builder, "treeMain"));

  ui_entry_insert_tag =
          GTK_ENTRY (gtk_builder_get_object (builder, "entryTag"));

  ui_entry_insert_value =
          GTK_ENTRY (gtk_builder_get_object (builder, "entryValue"));

  ui_chk_bnt_capture_v4l2 =
          GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder,
                                                     "checkbuttonCaptureV4l2"));

  ui_chk_bnt_capture_test =
          GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder,
                                                     "checkbuttonCaptureTest"));

  if (!(ui_main_window && ui_drawing && ui_tree
          && ui_entry_insert_tag && ui_entry_insert_value
          && ui_chk_bnt_capture_v4l2 && ui_chk_bnt_capture_test)) {
    fprintf (stderr, "Some widgets couldn't be created\n");
    ret = -105;
    goto done;
  }

  g_signal_connect_after (ui_drawing, "size-allocate",
      G_CALLBACK (ui_drawing_size_allocate_cb), NULL);

  gtk_builder_connect_signals (builder, NULL);

  ui_setup_tree_view (GTK_TREE_VIEW (ui_tree));

  ui_refresh ();

  gtk_widget_show_all (ui_main_window);

done:

  return ret;

}

/*
 * GStreamer functions
 */


static gboolean
me_gst_bus_callback_encode (GstBus * bus, GstMessage * message, gpointer data)
{
  gint *encode_status = (gint *) data;

  fflush (stdout);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      fprintf (stderr, "Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      *encode_status = ENC_ERROR;
      gtk_main_quit ();
    }
      break;
    case GST_MESSAGE_TAG:
    {
      /* ignore, we alredy have the tag list */
    }
      break;
    case GST_MESSAGE_EOS:
    {
      *encode_status = ENC_DONE;
      gtk_main_quit ();
    }
      break;
    default:
      /* unhandled message */
      break;
  }

  /* we want to be notified again the next time there is a message
   * on the bus, so returning TRUE (FALSE means we want to stop watching
   * for messages on the bus and our callback should not be called again)
   */
  return TRUE;
}

static gboolean
me_gst_bus_callback_view (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      fprintf (stderr, "Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      gtk_main_quit ();
    }
      break;
    case GST_MESSAGE_TAG:
    {
      if (tag_list == NULL)
        gst_message_parse_tag (message, &tag_list);
      else {
        GstTagList *tl = NULL;
        GstTagList *ntl = NULL;

        gst_message_parse_tag (message, &tl);
        if (tl) {
          ntl = gst_tag_list_merge (tag_list, tl, GST_TAG_MERGE_PREPEND);
          if (ntl) {
            gst_tag_list_unref (tag_list);
            tag_list = ntl;
          }
          gst_tag_list_unref (tl);
        }
      }
      /* remove whole chunk tags */
      gst_tag_list_remove_tag (tag_list, "exif");
      gst_tag_list_remove_tag (tag_list, "iptc");
      gst_tag_list_remove_tag (tag_list, "xmp");
    }
      break;
    case GST_MESSAGE_EOS:
      if (tag_list) {
        gst_tag_list_foreach (tag_list, insert_tag_on_tree, ui_tree);
      }
      break;
    case GST_MESSAGE_ELEMENT: {
      const GValue *val;
      GtkAllocation a;

      /* only interested in element messages from our gdkpixbufsink */
      if (message->src != GST_OBJECT_CAST (gst_video_sink))
        break;

      /* only interested in the first image (not any smaller previews) */
      if (gst_structure_has_name (message->structure, "pixbuf"))
        break;

      if (!gst_structure_has_name (message->structure, "preroll-pixbuf"))
        break;

      val = gst_structure_get_value (message->structure, "pixbuf");
      g_return_val_if_fail (val != NULL, TRUE);

      if (last_pixbuf)
        g_object_unref (last_pixbuf);

      last_pixbuf = GDK_PIXBUF (g_value_dup_object (val));

      g_print ("Got image pixbuf: %dx%d\n", gdk_pixbuf_get_width (last_pixbuf),
          gdk_pixbuf_get_height (last_pixbuf));

      gtk_widget_get_allocation (GTK_WIDGET (ui_drawing), &a);
      update_draw_pixbuf (a.width, a.height);

      gtk_widget_queue_draw (ui_drawing);
      break;
    }
    default:
      /* unhandled message */
      break;
  }

  /* we want to be notified again the next time there is a message
   * on the bus, so returning TRUE (FALSE means we want to stop watching
   * for messages on the bus and our callback should not be called again)
   */
  return TRUE;
}

static void
me_gst_cleanup_elements (void)
{
  /* when adding an element to pipeline rember to set it to NULL or add extra ref */

  if (gst_source) {
    gst_object_unref (gst_source);
    gst_source = NULL;
  }
  if (gst_metadata_demux) {
    gst_object_unref (gst_metadata_demux);
    gst_metadata_demux = NULL;
  }
  if (gst_metadata_mux) {
    gst_object_unref (gst_metadata_mux);
    gst_metadata_mux = NULL;
  }
  if (gst_image_dec) {
    gst_object_unref (gst_image_dec);
    gst_image_dec = NULL;
  }
  if (gst_image_enc) {
    gst_object_unref (gst_image_enc);
    gst_image_enc = NULL;
  }
  if (gst_video_scale) {
    gst_object_unref (gst_video_scale);
    gst_video_scale = NULL;
  }
  if (gst_video_convert) {
    gst_object_unref (gst_video_convert);
    gst_video_convert = NULL;
  }
  if (gst_video_sink) {
    gst_object_unref (gst_video_sink);
    gst_video_sink = NULL;
  }
  if (gst_file_sink) {
    gst_object_unref (gst_file_sink);
    gst_file_sink = NULL;
  }

  if (gst_pipeline) {
    gst_element_set_state (gst_pipeline, GST_STATE_NULL);
    gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
    gst_object_unref (gst_pipeline);
    gst_pipeline = NULL;
  }

}

/* dummy function that looks the file extension */
static gboolean
is_png (const gchar * filename)
{
  gboolean ret = FALSE;
  guint32 len;

  if (!filename)
    goto done;

  if ((len = strlen (filename)) < 4)    /* at least ".png" */
    goto done;

  if (0 == strcasecmp (filename + (len - 4), ".png"))
    ret = TRUE;

done:

  return ret;

}

static int
me_gst_setup_capture_pipeline (const gchar * src_file, const gchar * dest_file,
    gint * encode_status, gboolean use_v4l2)
{
  int ret = 0;
  GstBus *bus = NULL;
  gboolean linked;

  *encode_status = ENC_ERROR;

  me_gst_cleanup_elements ();

  /* create elements */
  if ( use_v4l2 )
    gst_source = gst_element_factory_make ("v4l2src", NULL);
  else
    gst_source = gst_element_factory_make ("videotestsrc", NULL);
  gst_video_convert = gst_element_factory_make ("videoconvert", NULL);
  gst_image_enc = gst_element_factory_make ("jpegenc", NULL);
  gst_metadata_mux = gst_element_factory_make ("metadatamux", NULL);
  gst_file_sink = gst_element_factory_make ("filesink", NULL);

  if (!(gst_source && gst_video_convert && gst_image_enc && gst_metadata_mux
          && gst_file_sink)) {
    fprintf (stderr, "An element couldn't be created for ecoding\n");
    ret = -300;
    goto done;
  }

  /* create gst_pipeline */
  gst_pipeline = gst_pipeline_new (NULL);

  if (NULL == gst_pipeline) {
    fprintf (stderr, "Pipeline couldn't be created\n");
    ret = -305;
    goto done;
  }

  /* set elements's properties */
  g_object_set (gst_source, "num-buffers", 1, NULL);
  g_object_set (gst_file_sink, "location", dest_file, NULL);
  if ( app_options & APP_OPT_MUX_EXIF )
    g_object_set (gst_metadata_mux, "exif", TRUE, NULL);
  else
    g_object_set (gst_metadata_mux, "exif", FALSE, NULL);

  if ( app_options & APP_OPT_MUX_IPTC )
    g_object_set (gst_metadata_mux, "iptc", TRUE, NULL);
  else
    g_object_set (gst_metadata_mux, "iptc", FALSE, NULL);

  if ( app_options & APP_OPT_MUX_XMP )
    g_object_set (gst_metadata_mux, "xmp", TRUE, NULL);
  else
    g_object_set (gst_metadata_mux, "xmp", FALSE, NULL);

  /* adding and linking elements */
  gst_bin_add_many (GST_BIN (gst_pipeline), gst_source, gst_video_convert,
      gst_image_enc, gst_metadata_mux, gst_file_sink, NULL);

  linked =
      gst_element_link_many (gst_source, gst_video_convert, gst_image_enc,
      gst_metadata_mux, gst_file_sink, NULL);

  /* now element are owned by pipeline (for videosink we keep a extra ref) */
  gst_source = gst_video_convert = gst_image_enc = gst_file_sink = NULL;
  gst_object_ref (gst_metadata_mux);

  if (!linked) {
    fprintf (stderr, "Elements couldn't be linked\n");
    ret = -310;
    goto done;
  }

  *encode_status = ENC_UNKNOWN;

  /* adding message bus */
  bus = gst_pipeline_get_bus (GST_PIPELINE (gst_pipeline));
  gst_bus_add_watch (bus, me_gst_bus_callback_encode, encode_status);
  gst_object_unref (bus);

done:

  return ret;

}

static int
me_gst_setup_encode_pipeline (const gchar * src_file, const gchar * dest_file,
    gint * encode_status)
{
  int ret = 0;
  GstBus *bus = NULL;
  gboolean linked;

  *encode_status = ENC_ERROR;

  me_gst_cleanup_elements ();

  /* create elements */
  gst_source = gst_element_factory_make ("filesrc", NULL);
  gst_metadata_demux = gst_element_factory_make ("metadatademux", NULL);
  gst_metadata_mux = gst_element_factory_make ("metadatamux", NULL);
  gst_file_sink = gst_element_factory_make ("filesink", NULL);


  if (!(gst_source && gst_metadata_demux && gst_metadata_mux && gst_file_sink)) {
    fprintf (stderr, "An element couldn't be created for ecoding\n");
    ret = -300;
    goto done;
  }


  /* create gst_pipeline */
  gst_pipeline = gst_pipeline_new (NULL);

  if (NULL == gst_pipeline) {
    fprintf (stderr, "Pipeline couldn't be created\n");
    ret = -305;
    goto done;
  }

  /* set elements's properties */
  g_object_set (gst_source, "location", src_file, NULL);
  g_object_set (gst_file_sink, "location", dest_file, NULL);

  if ( app_options & APP_OPT_DEMUX_EXIF )
    g_object_set (gst_metadata_demux, "exif", TRUE, NULL);
  else
    g_object_set (gst_metadata_demux, "exif", FALSE, NULL);

  if ( app_options & APP_OPT_DEMUX_IPTC )
    g_object_set (gst_metadata_demux, "iptc", TRUE, NULL);
  else
    g_object_set (gst_metadata_demux, "iptc", FALSE, NULL);

  if ( app_options & APP_OPT_DEMUX_XMP )
    g_object_set (gst_metadata_demux, "xmp", TRUE, NULL);
  else
    g_object_set (gst_metadata_demux, "xmp", FALSE, NULL);

  if ( app_options & APP_OPT_MUX_EXIF )
    g_object_set (gst_metadata_mux, "exif", TRUE, NULL);
  else
    g_object_set (gst_metadata_mux, "exif", FALSE, NULL);

  if ( app_options & APP_OPT_MUX_IPTC )
    g_object_set (gst_metadata_mux, "iptc", TRUE, NULL);
  else
    g_object_set (gst_metadata_mux, "iptc", FALSE, NULL);

  if ( app_options & APP_OPT_MUX_XMP )
    g_object_set (gst_metadata_mux, "xmp", TRUE, NULL);
  else
    g_object_set (gst_metadata_mux, "xmp", FALSE, NULL);

  /* adding and linking elements */
  gst_bin_add_many (GST_BIN (gst_pipeline), gst_source, gst_metadata_demux,
      gst_metadata_mux, gst_file_sink, NULL);

  linked =
      gst_element_link_many (gst_source, gst_metadata_demux, gst_metadata_mux,
      gst_file_sink, NULL);

  /* now element are owned by pipeline (for videosink we keep a extra ref) */
  gst_source = gst_metadata_demux = gst_file_sink = NULL;
  gst_object_ref (gst_metadata_mux);

  if (!linked) {
    fprintf (stderr, "Elements couldn't be linked\n");
    ret = -310;
    goto done;
  }

  *encode_status = ENC_UNKNOWN;

  /* adding message bus */
  bus = gst_pipeline_get_bus (GST_PIPELINE (gst_pipeline));
  gst_bus_add_watch (bus, me_gst_bus_callback_encode, encode_status);
  gst_object_unref (bus);

done:

  return ret;

}

static int
me_gst_setup_view_pipeline (const gchar * filename)
{
  int ret = 0;
  GstBus *bus = NULL;
  gboolean linked;

  me_gst_cleanup_elements ();

  /* create elements */
  gst_source = gst_element_factory_make ("filesrc", NULL);
  gst_metadata_demux = gst_element_factory_make ("metadatademux", NULL);
  /* let's do a dummy stuff to avoid decodebin */
  if (is_png (filename))
    gst_image_dec = gst_element_factory_make ("pngdec", NULL);
  else
    gst_image_dec = gst_element_factory_make ("jpegdec", NULL);
  gst_video_scale = gst_element_factory_make ("videoscale", NULL);
  gst_video_convert = gst_element_factory_make ("videoconvert", NULL);
  gst_video_sink = gst_element_factory_make ("gdkpixbufsink", NULL);

  if (gst_video_sink == NULL) {
    if (!gst_default_registry_check_feature_version ("gdkpixbufdec", 0, 10, 0))
      g_warning ("Could not create 'gdkpixbufsink' element");
    else {
      g_warning ("Could not create 'gdkpixbufsink' element. "
          "(May be your gst-plugins-good is too old?)");
    ret = -400;
    }
    goto done;
  }
  if (!(gst_source && gst_metadata_demux && gst_image_dec && gst_video_scale
          && gst_video_convert && gst_video_sink)) {
    fprintf (stderr, "An element couldn't be created for viewing\n");
    ret = -400;
    goto done;
  }

  /* create gst_pipeline */
  gst_pipeline = gst_pipeline_new (NULL);

  if (NULL == gst_pipeline) {
    fprintf (stderr, "Pipeline couldn't be created\n");
    ret = -405;
    goto done;
  }

  /* set elements's properties */
  g_object_set (gst_source, "location", filename, NULL);
  g_object_set (gst_metadata_demux, "parse-only", TRUE, NULL);


  /* adding and linking elements */
  gst_bin_add_many (GST_BIN (gst_pipeline), gst_source, gst_metadata_demux,
      gst_image_dec, gst_video_scale, gst_video_convert, gst_video_sink, NULL);

  linked = gst_element_link_many (gst_source, gst_metadata_demux, gst_image_dec,
      gst_video_scale, gst_video_convert, gst_video_sink, NULL);

  /* now element are owned by pipeline (for videosink we keep a extra ref) */
  gst_source = gst_metadata_demux = gst_image_dec = gst_video_scale =
      gst_video_convert = NULL;
  gst_object_ref (gst_video_sink);

  if (last_pixbuf) {
    g_object_unref (last_pixbuf);
    last_pixbuf = NULL;
  }

  if (!linked) {
    fprintf (stderr, "Elements couldn't be linked\n");
    ret = -410;
    goto done;
  }

  /* adding message bus */
  bus = gst_pipeline_get_bus (GST_PIPELINE (gst_pipeline));
  gst_bus_add_watch (bus, me_gst_bus_callback_view, NULL);
  gst_object_unref (bus);


done:

  return ret;

}

static void
process_file(void)
{
  /* filename for future usage (title and file name to be created) */
  me_gst_cleanup_elements ();

  if (tag_list) {
    gst_tag_list_unref (tag_list);
    tag_list = NULL;
  }

  /* create pipeline */
  me_gst_setup_view_pipeline (filename->str);

  gst_element_set_state (gst_pipeline, GST_STATE_PLAYING);

  ui_refresh ();

}

int
main (int argc, char *argv[])
{
  int ret = 0;

  if (argc >= 2) {
    if (filename)
      g_string_free (filename, TRUE);
    filename = g_string_new (argv[1]);
  }

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  /* create UI */
  if ((ret = ui_create ())) {
    goto done;
  }

  if (argc >= 2) {
    process_file();
  }

  gtk_main ();

done:

  me_gst_cleanup_elements ();

  if (tag_list) {
    gst_tag_list_unref (tag_list);
    tag_list = NULL;
  }

  if (filename) {
    g_string_free (filename, TRUE);
    filename = NULL;
  }

  return ret;
}
