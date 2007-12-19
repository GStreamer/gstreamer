/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gst/gst.h>
#include <glade/glade-xml.h>
#include <gtk/gtk.h>
#include <gst/interfaces/xoverlay.h>
#include <gdk/gdkx.h>

/*
 * Global constants
 */

enum
{
  COL_TAG = 0,
  COL_VALUE,
  NUM_COLS
};

#define ENC_ERROR (-1)
#define ENC_DONE (0)
#define ENC_UNKNOWN (1)


/*
 * functions prototypes
 */

/* gstreamer related functions */

static void me_gst_cleanup_elements ();
static int
me_gst_setup_view_pipeline (const gchar * filename, GdkWindow * window);
static int
me_gst_setup_capture_pipeline (const gchar * src_file, const gchar * dest_file,
    gint * encode_status);
static int
me_gst_setup_encode_pipeline (const gchar * src_file, const gchar * dest_file,
    gint * encode_status);

/* ui related functions */

static void ui_refresh ();

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

GstTagList *tag_list = NULL;

GladeXML *ui_glade_xml = NULL;
GtkWidget *ui_main_window = NULL;
GtkWidget *ui_drawing = NULL;
GtkWidget *ui_tree = NULL;

GtkEntry *ui_entry_insert_tag = NULL;
GtkEntry *ui_entry_insert_value = NULL;

GtkToggleButton *ui_chk_bnt_capture = NULL;

GString *filename = NULL;

/*
 * Helper functions
 */

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
  } else {
    str = g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, 0));
  }

  tree_store = GTK_TREE_STORE (gtk_tree_view_get_model (tree_view));
  gtk_tree_store_append (tree_store, &iter, NULL);
  gtk_tree_store_set (tree_store, &iter, COL_TAG, tag, COL_VALUE, str, -1);

  if (str)
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
        fprintf (stderr, "This app still doesn't handle type (%s)(%ld)\n",
            g_type_name (type), type);
        break;
    }
  }

done:

  return ret;

}


/*
 * UI handling functions (mapped by glade)
 */

gboolean
on_windowMain_configure_event (GtkWidget * widget, GdkEventConfigure * event,
    gpointer user_data)
{
  GstXOverlay *xoverlay;

  if (gst_video_sink) {

    xoverlay = GST_X_OVERLAY (gst_video_sink);

    if (xoverlay != NULL && GST_IS_X_OVERLAY (xoverlay)) {
      gst_x_overlay_expose (xoverlay);
    }

  }

  return FALSE;
}

gboolean
on_drawingMain_expose_event (GtkWidget * widget, GdkEventExpose * event,
    gpointer data)
{
  if (gst_video_sink) {
    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (gst_video_sink),
        GDK_WINDOW_XWINDOW (widget->window));
  }
  return FALSE;
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

  gst_element_set_state (gst_pipeline, GST_STATE_NULL);
  gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  src_file = g_string_new (filename->str);

  if (gtk_toggle_button_get_active (ui_chk_bnt_capture)) {
    setup_new_filename (filename, ".jpg");
    if (me_gst_setup_capture_pipeline (src_file->str, filename->str,
            &enc_status)) {
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
      gst_tag_list_free (tag_list);
      tag_list = NULL;
    }

    me_gst_setup_view_pipeline (filename->str, ui_drawing->window);

    gst_element_set_state (gst_pipeline, GST_STATE_PLAYING);
    gst_element_get_state (gst_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  }

done:

  if (src_file)
    g_string_free (src_file, TRUE);

}

void
on_checkbuttonCapture_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton)) {
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
  const guint32 col_index = (guint32) user_data;
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
        G_CALLBACK (on_cell_edited), (gpointer) col_index);
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
ui_refresh ()
{
  GtkTreeStore *store =
      GTK_TREE_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (ui_tree)));
  gtk_tree_store_clear (store);
  if (filename)
    gtk_window_set_title (GTK_WINDOW (ui_main_window), filename->str);
}

static int
ui_create ()
{
  int ret = 0;

  ui_glade_xml = glade_xml_new ("MetadataEditorMain.glade", NULL, NULL);

  if (!ui_glade_xml) {
    fprintf (stderr, "glade_xml_new failed\n");
    ret = -101;
    goto done;
  }

  ui_main_window = glade_xml_get_widget (ui_glade_xml, "windowMain");

  ui_drawing = glade_xml_get_widget (ui_glade_xml, "drawingMain");

  ui_tree = glade_xml_get_widget (ui_glade_xml, "treeMain");

  ui_entry_insert_tag =
      GTK_ENTRY (glade_xml_get_widget (ui_glade_xml, "entryTag"));

  ui_entry_insert_value =
      GTK_ENTRY (glade_xml_get_widget (ui_glade_xml, "entryValue"));

  ui_chk_bnt_capture =
      GTK_TOGGLE_BUTTON (glade_xml_get_widget (ui_glade_xml,
          "checkbuttonCapture"));

  if (!(ui_main_window && ui_drawing && ui_tree && ui_entry_insert_tag
          && ui_entry_insert_value && ui_chk_bnt_capture)) {
    fprintf (stderr, "Some widgets couldn't be created\n");
    ret = -105;
    goto done;
  }

  glade_xml_signal_autoconnect (ui_glade_xml);

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
            gst_tag_list_free (tag_list);
            tag_list = ntl;
            gst_tag_list_free (tl);
          }
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
me_gst_cleanup_elements ()
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
    gint * encode_status)
{
  int ret = 0;
  GstBus *bus = NULL;
  gboolean linked;

  *encode_status = ENC_ERROR;

  me_gst_cleanup_elements ();

  /* create elements */
  gst_source = gst_element_factory_make ("v4l2src", NULL);
  gst_video_convert = gst_element_factory_make ("ffmpegcolorspace", NULL);
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
me_gst_setup_view_pipeline (const gchar * filename, GdkWindow * window)
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
  gst_video_convert = gst_element_factory_make ("ffmpegcolorspace", NULL);
  gst_video_sink = gst_element_factory_make ("ximagesink", NULL);

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
  g_object_set (gst_video_sink, "force-aspect-ratio", TRUE, NULL);


  /* adding and linking elements */
  gst_bin_add_many (GST_BIN (gst_pipeline), gst_source, gst_metadata_demux,
      gst_image_dec, gst_video_scale, gst_video_convert, gst_video_sink, NULL);

  linked = gst_element_link_many (gst_source, gst_metadata_demux, gst_image_dec,
      gst_video_scale, gst_video_convert, gst_video_sink, NULL);

  /* now element are owned by pipeline (for videosink we keep a extra ref) */
  gst_source = gst_metadata_demux = gst_image_dec = gst_video_scale =
      gst_video_convert = NULL;
  gst_object_ref (gst_video_sink);

  if (window)
    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (gst_video_sink),
        GDK_WINDOW_XWINDOW (window));

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

int
main (int argc, char *argv[])
{
  int ret = 0;

  if (argc != 2) {
    fprintf (stderr, "Give the name of a jpeg file as argument\n");
    ret = -5;
    goto done;
  }

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  /* filename for future usage (title and file name to be created) */
  if (filename)
    g_string_free (filename, TRUE);
  filename = g_string_new (argv[1]);

  /* create UI */
  if ((ret = ui_create ())) {
    goto done;
  }

  /* create pipeline */
  me_gst_setup_view_pipeline (argv[1], ui_drawing->window);

  gst_element_set_state (gst_pipeline, GST_STATE_PLAYING);

  gtk_main ();

done:

  me_gst_cleanup_elements ();

  if (tag_list) {
    gst_tag_list_free (tag_list);
    tag_list = NULL;
  }

  if (filename) {
    g_string_free (filename, TRUE);
    filename = NULL;
  }

  return ret;
}
