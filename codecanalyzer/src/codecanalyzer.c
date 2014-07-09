/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
/**
 * CodecAnalyzer is an analyzer for doing in-depth analysis
 * on compressed media which is built on top of gstreamer, gtk+ and
 * libxml2. It is capable of parsing all the syntax elements
 * from an elementary video stream.
 */
#include <stdio.h>
#include <stdlib.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <gtk/gtk.h>

#include "gst_analyzer.h"
#include "xml_parse.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
typedef struct
{

  GtkBuilder *builder;

  GtkWidget *main_window;
  GtkWidget *main_vbox;
  GtkWidget *child_vbox1;
  GtkWidget *child_vbox2;
  GtkWidget *child_vbox3;
  GtkWidget *menubar_vbox;
  GtkWidget *stream_chooser;
  GtkWidget *numframes_chooser;
  GtkWidget *analyze_button;
  GtkWidget *cancel_button;
  GtkWidget *thumbnails_scroll_window;
  GtkWidget *thumbnails_view_port;
  GtkWidget *child_hbox_in_vbox1_2;
  GtkWidget *hbox1_in_vbox2;
  GtkWidget *general_info_frame;
  GtkWidget *general_info_vbox;
  GtkWidget *general_info_treeview;
  GtkWidget *parsed_info_frame;
  GtkWidget *parsed_info_hbox;
  GtkWidget *parsed_info_vbox;
  GtkWidget *parsed_info_button_box;
  GtkWidget *tree_view;
  GtkWidget *header_button;
  GtkWidget *slice_button;
  GtkWidget *hexval_button;

  GtkUIManager *menu_manager;
  GtkWidget *menubar;

  GHashTable *notebook_hash;
  GtkWidget *prev_page;

  guint analyze_idle_id;

  gchar *file_name;
  gchar *uri;
  gchar *analyzer_home;
  gchar *codec_name;
  gchar *current_xml;
  gchar *current_hex;

  gint num_frames;
  gint num_frames_analyzed;

} AnalyzerUI;

static AnalyzerUI *ui;
GstAnalyzer *gst_analyzer;

static char *treeview_headers[] = { "Field", "Value", "NumofBits" };

enum
{
  COLUMN_NAME,
  COLUMN_VALUE,
  COLUMN_NBITS,
  NUM_COLS
};

enum
{
  GENERAL_INFO_LIST_NAME,
  GENERAL_INFO_LIST_VALUE,
  NUM_GENERAL_INFO_LIST
};

typedef enum
{
  COMPONENTS_UNKNOWN,
  COMPONENTS_HEADERS_GENERAL,
  COMPONENTS_HEADERS_SLICE,
  COMPONENTS_HEXVAL
} CodecComponents;


GtkBuilder *
make_builder (char *file_name)
{
  GtkBuilder *builder;

  builder = gtk_builder_new ();
  g_assert (gtk_builder_add_from_file (builder, file_name, NULL) > 0);

  return builder;
}

GtkWidget *
get_widget_from_builder (GtkBuilder * builder, char *widget_name)
{
  GtkWidget *widget;

  widget = GTK_WIDGET (gtk_builder_get_object (builder, widget_name));

  return widget;
}

static void
display_error_dialog (const char *msg)
{
  GtkWidget *dialog;
  dialog = gtk_message_dialog_new (GTK_WINDOW (ui->main_window),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_CANCEL, "%s", msg);
  gtk_window_set_title (GTK_WINDOW (dialog), "Error");
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
fill_tree_store (gpointer data, gpointer user_data)
{
  AnalyzerNode *node;
  GtkTreeStore *treestore;
  GtkTreeIter toplevel, child;
  gchar *buf;

  node = (AnalyzerNode *) data;
  treestore = (GtkTreeStore *) user_data;

  gtk_tree_store_append (treestore, &toplevel, NULL);
  gtk_tree_store_set (treestore, &toplevel, COLUMN_NAME, node->field_name, -1);
  if (!node->is_matrix)
    gtk_tree_store_set (treestore, &toplevel, COLUMN_VALUE, node->value, -1);
  else {
    buf = g_strdup_printf ("[%s][%s] :click description", node->rows, node->columns);
    gtk_tree_store_set (treestore, &toplevel, COLUMN_VALUE, buf, -1);
    g_free (buf);
  }
  gtk_tree_store_set (treestore, &toplevel, COLUMN_NBITS, node->nbits, -1);

  gtk_tree_store_append (treestore, &child, &toplevel);
  if (node->is_matrix)
    gtk_tree_store_set (treestore, &child, COLUMN_NAME, node->value, -1);
  else
    gtk_tree_store_set (treestore, &child,
        COLUMN_NAME, "Description.. (TODO)", -1);
}

static GtkWidget *
create_tree_view ()
{
  GtkTreeViewColumn *col;
  GtkCellRenderer *renderer;
  GtkWidget *view;
  guint i;

  view = gtk_tree_view_new ();

  for (i = 0; i < G_N_ELEMENTS (treeview_headers); i++) {

    col = gtk_tree_view_column_new ();

    gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_resizable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_title (col, treeview_headers[i]);
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), col);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (col, renderer, FALSE);
    gtk_tree_view_column_add_attribute (col, renderer, "text", i);
  }

  return view;
}

static void
populate_notebook (gpointer data, gpointer user_data)
{
  gchar *header_name;
  GtkWidget *header = NULL;
  GtkWidget *h_data;
  GtkWidget *scrolled_window;
  GtkWidget *notebook;

  notebook = (GtkWidget *) user_data;
  header_name = (gchar *) data;

  if (strcmp (header_name, "comment")) {

    header = gtk_label_new (header_name);
    gtk_label_set_text (GTK_LABEL (header), header_name);

    h_data = create_tree_view ();

    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_container_add (GTK_CONTAINER (scrolled_window), h_data);

    gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook),
        scrolled_window, header, NULL);

    gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (notebook),
        scrolled_window, TRUE);

    g_hash_table_insert (ui->notebook_hash, header_name, scrolled_window);
  }
}

static void
analyzer_display_parsed_info_button_box (GtkWidget * vbox)
{
  ui->header_button = gtk_button_new_with_label ("Headers");
  ui->slice_button = gtk_button_new_with_label ("Slices");
  ui->hexval_button = gtk_button_new_with_label ("Hex-values");

  gtk_box_pack_start (GTK_BOX (vbox), ui->header_button, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), ui->slice_button, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), ui->hexval_button, TRUE, TRUE, 2);

  gtk_widget_show_all (ui->main_window);
}

static gboolean
callback_button_box_click (GtkWidget * widget, GdkEvent * event,
    gpointer user_data)
{
  GList *list, *header_list;
  GList *hlist = NULL, *slist = NULL;
  GtkWidget *notebook = NULL;
  GtkWidget *textview = NULL;
  GFile *hexfile;
  GtkWidget *sc_window, *tree_view;
  gboolean is_header, is_slice, is_hexval;

  CodecComponents component = (CodecComponents) user_data;

  char *xml_name = ui->current_xml;
  char *hex_name = ui->current_hex;

  switch (component) {
    case COMPONENTS_HEADERS_GENERAL:
      is_header = TRUE;
      is_slice = FALSE;
      is_hexval = FALSE;
      break;
    case COMPONENTS_HEADERS_SLICE:
      is_slice = TRUE;
      is_header = FALSE;
      is_hexval = FALSE;
      break;
    case COMPONENTS_HEXVAL:
      is_hexval = TRUE;
      is_header = FALSE;
      is_slice = FALSE;
      break;
    default:
      break;
  }

  if (ui->prev_page)
    gtk_widget_destroy (GTK_WIDGET (ui->prev_page));
  if (ui->notebook_hash)
    g_hash_table_destroy (ui->notebook_hash);
  ui->notebook_hash = g_hash_table_new (g_str_hash, g_str_equal);

  if (!is_hexval) {
    header_list = analyzer_get_list_header_strings (xml_name);

    while (header_list) {
      if (strcmp (header_list->data, "comment")) {
        if (is_header && !g_str_has_prefix (header_list->data, "slice"))
          hlist = g_list_append (hlist, header_list->data);
        else if (is_slice && g_str_has_prefix (header_list->data, "slice"))
          hlist = g_list_append (hlist, header_list->data);
      }
      header_list = header_list->next;
    }

    notebook = gtk_notebook_new ();
    g_object_set (G_OBJECT (notebook), "expand", TRUE, NULL);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
    gtk_notebook_popup_enable (GTK_NOTEBOOK (notebook));
    gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), TRUE);

    g_list_foreach (hlist, (GFunc) populate_notebook, (gpointer) notebook);

    while (hlist) {
      sc_window = g_hash_table_lookup (ui->notebook_hash, hlist->data);
      if (sc_window && GTK_IS_BIN (sc_window))
        tree_view = gtk_bin_get_child (GTK_BIN (sc_window));

      if (tree_view) {
        list = analyzer_get_list_analyzer_node_from_xml (xml_name, hlist->data);
        if (list) {
          GtkTreeStore *treestore;
          GtkTreeModel *model;

          treestore = gtk_tree_store_new (NUM_COLS,
              G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

          g_list_foreach (list, (GFunc) fill_tree_store, treestore);
          analyzer_node_list_free (list);
          list = NULL;

          model = GTK_TREE_MODEL (treestore);
          gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), model);
          g_object_unref (model);
        }
      }
      hlist = hlist->next;
    }
    ui->prev_page = notebook;
    gtk_container_add (GTK_CONTAINER (ui->parsed_info_vbox), notebook);
  } else {
    /*Display the hex dump of the frame */
    GtkWidget *scrolled_window;
    GtkTextBuffer *buffer;
    gchar *contents;
    gsize length;

    textview = gtk_text_view_new ();
    gtk_text_view_set_left_margin (GTK_TEXT_VIEW (textview), 20);
    g_object_set (G_OBJECT (textview), "expand", TRUE, "editable", FALSE, NULL);

    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_container_add (GTK_CONTAINER (scrolled_window), textview);

    hexfile = g_file_new_for_path (hex_name);
    if (hexfile) {
      if (g_file_load_contents (hexfile, NULL, &contents, &length, NULL, NULL)) {
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
        gtk_text_buffer_set_text (buffer, contents, length);
        g_free (contents);
        g_object_unref (G_OBJECT (hexfile));
      }
    }
    ui->prev_page = scrolled_window;
    gtk_container_add (GTK_CONTAINER (ui->parsed_info_vbox), scrolled_window);
  }

  gtk_widget_show_all (ui->main_window);

  return TRUE;
}

static void
callback_frame_thumbnail_press (GtkWidget * event_box,
    GdkEventButton * event, gpointer user_data)
{
  GtkWidget *label;
  gchar *file_name;
  gchar *name;
  gchar *frame_name_markup;
  gint frame_num;

  frame_num = (gint) user_data;

  name = g_strdup_printf ("%s-%d.xml",ui->codec_name, frame_num);
  file_name = g_build_filename (ui->analyzer_home, "xml", name, NULL);
  if (ui->current_xml)
    g_free (ui->current_xml);
  g_free (name);
  ui->current_xml = file_name;

  name = g_strdup_printf ("%s-%d.hex",ui->codec_name, frame_num);
  file_name = g_build_filename (ui->analyzer_home, "hex", name, NULL);
  if (ui->current_hex)
    g_free (ui->current_hex);
  g_free(name);
  ui->current_hex = file_name;

  g_signal_connect (G_OBJECT (ui->header_button), "button-press-event",
      G_CALLBACK (callback_button_box_click),
      (gpointer) COMPONENTS_HEADERS_GENERAL);
  g_signal_connect (G_OBJECT (ui->slice_button), "button-press-event",
      G_CALLBACK (callback_button_box_click),
      (gpointer) COMPONENTS_HEADERS_SLICE);
  g_signal_connect (G_OBJECT (ui->hexval_button), "button-press-event",
      G_CALLBACK (callback_button_box_click), (gpointer) COMPONENTS_HEXVAL);

  /* load general headers by default */
  callback_button_box_click (NULL, NULL, (gpointer) COMPONENTS_HEADERS_GENERAL);

  /*update the label of parsed_info_frame with frame_number */
  gtk_frame_set_label (GTK_FRAME (ui->parsed_info_frame), "");
  label = gtk_frame_get_label_widget (GTK_FRAME (ui->parsed_info_frame));
  frame_name_markup =
      g_markup_printf_escaped
      ("<span style=\"italic\" size=\"xx-large\">Frame %d</span>",
      frame_num + 1);
  gtk_label_set_markup (GTK_LABEL (label), frame_name_markup);
  g_free (frame_name_markup);

  gtk_widget_show_all (ui->main_window);
}

static GtkWidget *
create_image (int frame_num)
{
  GtkWidget *image;
  GtkWidget *event_box;
  char *path;

  path =
      g_build_filename (DATADIR, "codecanalyzer", "pixmaps",
      "frame-thumbnail.png", NULL);
  image = gtk_image_new_from_file (path);
  g_free (path);

  event_box = gtk_event_box_new ();

  gtk_event_box_set_above_child (GTK_EVENT_BOX (event_box), TRUE);
  gtk_event_box_set_visible_window (GTK_EVENT_BOX (event_box), FALSE);

  gtk_container_add (GTK_CONTAINER (event_box), image);

  g_signal_connect (G_OBJECT (event_box), "button_press_event",
      G_CALLBACK (callback_frame_thumbnail_press), (gpointer)frame_num);
  return event_box;
}

static void
analyzer_create_thumbnails ()
{
  GtkWidget *image;
  guint i;

  for (i = 0; i < ui->num_frames_analyzed; i++) {
    image = create_image (i);
    g_object_set (G_OBJECT (image), "visible", TRUE, "can-focus", TRUE, NULL);
    gtk_box_pack_start (GTK_BOX (ui->hbox1_in_vbox2), image, TRUE, TRUE, 2);
    gtk_widget_show_all (image);

    /* Update the details of frame_0 by default */
    if (i == 0) {
      analyzer_display_parsed_info_button_box (ui->parsed_info_button_box);
      callback_frame_thumbnail_press (GTK_WIDGET (image), NULL, (gpointer) i);
      callback_button_box_click (NULL, NULL,
          (gpointer) COMPONENTS_HEADERS_GENERAL);
    }
  }
}

static void
analyzer_ui_destroy ()
{
  gtk_widget_destroy (ui->main_window);

  if (ui->file_name)
    g_free (ui->file_name);

  if (ui->uri)
    g_free (ui->uri);

  if (ui->codec_name)
    g_free (ui->codec_name);

  if (ui->analyzer_home)
    g_free (ui->analyzer_home);

  if (ui->current_xml)
    g_free (ui->current_xml);

  if (ui->current_hex)
    g_free (ui->current_hex);

  if (ui->notebook_hash)
    g_hash_table_destroy (ui->notebook_hash);

  g_slice_free (AnalyzerUI, ui);
}

void
callback_main_window_destroy (GtkWidget * widget, gpointer user_data)
{
  if (gst_analyzer)
    gst_analyzer_destroy (gst_analyzer);

  analyzer_ui_destroy ();

  gtk_main_quit ();
}


static void
fill_general_info_list_row (gchar * name, char *content)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkTreeView *list = GTK_TREE_VIEW (ui->general_info_treeview);

  store = GTK_LIST_STORE (gtk_tree_view_get_model (list));

  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, GENERAL_INFO_LIST_NAME, name,
      GENERAL_INFO_LIST_VALUE, content, -1);
}

static void
list_store_init (GtkWidget * treeview)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *col;
  GtkListStore *store;
  guint i;

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);

  for (i = 0; i < NUM_GENERAL_INFO_LIST; i++) {

    col = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_resizable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), col);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (col, renderer, FALSE);
    gtk_tree_view_column_add_attribute (col, renderer, "text", i);
  }

  store =
      gtk_list_store_new (NUM_GENERAL_INFO_LIST, G_TYPE_STRING, G_TYPE_STRING);

  gtk_tree_view_set_model (GTK_TREE_VIEW (treeview), GTK_TREE_MODEL (store));

  g_object_unref (store);
}

static void
analyzer_display_general_stream_info (GstAnalyzerVideoInfo * analyzer_vinfo)
{
  char *str;

  if (!analyzer_vinfo || !ui->general_info_treeview)
    return;

  list_store_init (ui->general_info_treeview);

  if (analyzer_vinfo->codec_name)
    fill_general_info_list_row ("codec", analyzer_vinfo->codec_name);

  if (analyzer_vinfo->width) {
    str = g_strdup_printf ("%d", analyzer_vinfo->width);
    fill_general_info_list_row ("width", str);
    g_free (str);
  }
  if (analyzer_vinfo->height) {
    str = g_strdup_printf ("%d", analyzer_vinfo->height);
    fill_general_info_list_row ("height", str);
    g_free (str);
  }
  if (analyzer_vinfo->depth) {
    str = g_strdup_printf ("%d", analyzer_vinfo->depth);
    fill_general_info_list_row ("depth", str);
    g_free (str);
  }
  if (analyzer_vinfo->avg_bitrate) {
    str = g_strdup_printf ("%d", analyzer_vinfo->avg_bitrate);
    fill_general_info_list_row ("avg_bitrate", str);
    g_free (str);
  }
  if (analyzer_vinfo->max_bitrate) {
    str = g_strdup_printf ("%d", analyzer_vinfo->max_bitrate);
    fill_general_info_list_row ("max_bitrate", str);
    g_free (str);
  }
  if (analyzer_vinfo->fps_n) {
    str = g_strdup_printf ("%d", analyzer_vinfo->fps_n);
    fill_general_info_list_row ("fps_n", str);
    g_free (str);
  }
  if (analyzer_vinfo->fps_d) {
    str = g_strdup_printf ("%d", analyzer_vinfo->fps_d);
    fill_general_info_list_row ("fps_d", str);
    g_free (str);
  }
  if (analyzer_vinfo->par_n) {
    str = g_strdup_printf ("%d", analyzer_vinfo->par_n);
    fill_general_info_list_row ("par_n", str);
    g_free (str);
  }
  if (analyzer_vinfo->par_d) {
    str = g_strdup_printf ("%d", analyzer_vinfo->par_d);
    fill_general_info_list_row ("par_d", str);
    g_free (str);
  }

  gtk_widget_show_all (ui->general_info_treeview);
}

static void
reset_analyzer_ui ()
{

  if (ui->hbox1_in_vbox2) {
    gtk_widget_destroy (GTK_WIDGET (ui->hbox1_in_vbox2));
    ui->hbox1_in_vbox2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    g_object_set (G_OBJECT (ui->hbox1_in_vbox2), "visible", TRUE,
        "can-focus", FALSE, NULL);
    gtk_container_add (GTK_CONTAINER (ui->thumbnails_view_port),
        ui->hbox1_in_vbox2);
  }

  if (ui->general_info_treeview) {
    gtk_widget_destroy (GTK_WIDGET (ui->general_info_treeview));
    ui->general_info_treeview = gtk_tree_view_new ();
    gtk_box_pack_end (GTK_BOX (ui->general_info_vbox),
        ui->general_info_treeview, TRUE, TRUE, 0);
  }

  if (ui->parsed_info_button_box) {
    gtk_widget_destroy (GTK_WIDGET (ui->parsed_info_button_box));
    ui->parsed_info_button_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    g_object_set (G_OBJECT (ui->parsed_info_button_box), "visible", TRUE,
        "can-focus", TRUE, NULL);
    gtk_box_pack_start (GTK_BOX (ui->parsed_info_hbox),
        ui->parsed_info_button_box, FALSE, FALSE, 0);
  }
  if (ui->parsed_info_vbox) {
    gtk_widget_destroy (GTK_WIDGET (ui->parsed_info_vbox));
    ui->parsed_info_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    g_object_set (G_OBJECT (ui->parsed_info_vbox), "visible", TRUE,
        "can-focus", TRUE, NULL);
    gtk_box_pack_start (GTK_BOX (ui->parsed_info_hbox), ui->parsed_info_vbox,
        TRUE, TRUE, 0);
  }

  if (ui->parsed_info_frame)
    gtk_frame_set_label (GTK_FRAME (ui->parsed_info_frame), "");

  if (ui->notebook_hash)
    g_hash_table_destroy (ui->notebook_hash);
  ui->notebook_hash = g_hash_table_new (g_str_hash, g_str_equal);

  ui->prev_page = NULL;

  gtk_widget_show_all (ui->main_window);
}

guint
analyze_idle_callback (gpointer data)
{
  guint i;

  if (!gst_analyzer)
    return TRUE;

  ui->num_frames_analyzed = gst_analyzer->NumOfAnalyzedFrames;

  if (!gst_analyzer->complete_analyze)
    return TRUE;

  /* Once the analysis is complete, we doesn't need to hold the gst_analyzer */
  if (gst_analyzer) {
    gst_analyzer_destroy (gst_analyzer);
    gst_analyzer = NULL;
  }

  analyzer_create_thumbnails ();

  gtk_widget_set_sensitive (ui->cancel_button, FALSE);
  gtk_widget_set_sensitive (ui->analyze_button, TRUE);

  ui->analyze_idle_id = 0;

  return FALSE;
}

void
callback_analyzer_button_analyze (GtkWidget * widget, gpointer user_data)
{
  const char *str;
  GstAnalyzerVideoInfo *analyzer_vinfo;
  guint i, j, k;

  gtk_widget_set_sensitive (ui->analyze_button, FALSE);
  gtk_widget_set_sensitive (ui->cancel_button, TRUE);
  gtk_widget_set_sensitive (ui->child_vbox3, TRUE);

  g_signal_emit_by_name (ui->numframes_chooser, "activate", NULL, NULL);
  str = gtk_entry_get_text ((GtkEntry *) ui->numframes_chooser);

  /* initialize the back-end */
  if (!gst_analyzer) {
    GstAnalyzerStatus status;
    gst_analyzer = g_slice_new0 (GstAnalyzer);
    status = gst_analyzer_init (gst_analyzer, ui->uri);
    if (status != GST_ANALYZER_STATUS_SUCCESS) {
      const gchar *msg;

      reset_analyzer_ui ();

      msg = gst_analyzer_status_get_name (status);
      display_error_dialog (msg);

      gtk_widget_set_sensitive (ui->analyze_button, TRUE);
      gtk_widget_set_sensitive (ui->cancel_button, FALSE);
      gtk_widget_set_sensitive (ui->child_vbox3, FALSE);
      if (gst_analyzer)
        gst_analyzer_destroy (gst_analyzer);
      gst_analyzer = NULL;
      goto done;
    }
  }

  /* reset the necessary UI components for each Analysis */
  reset_analyzer_ui ();

  ui->num_frames = atoi (str);

  if (gst_analyzer->codec_name)
    ui->codec_name = g_strdup (gst_analyzer->codec_name);

  if (ui->file_name)
    gst_analyzer_set_file_name (gst_analyzer, ui->file_name);
  if (ui->num_frames)
    gst_analyzer_set_num_frames (gst_analyzer, ui->num_frames);
  if (ui->analyzer_home)
    gst_analyzer_set_destination_dir_path (gst_analyzer, ui->analyzer_home);

  gst_analyzer_start (gst_analyzer);

  analyzer_display_general_stream_info (gst_analyzer->video_info);
  ui->analyze_idle_id = g_idle_add ((GSourceFunc) analyze_idle_callback, NULL);
done:{
  }
}

void
callback_cancel_button_cancel (GtkWidget * widget, gpointer user_data)
{
  g_debug ("Cancel the analysis.. \n");

  gtk_widget_set_sensitive (ui->cancel_button, FALSE);

  if (ui->analyze_idle_id)
    g_source_remove (ui->analyze_idle_id);

  if (gst_analyzer) {
    gst_analyzer_destroy (gst_analyzer);
    gst_analyzer = NULL;
  }

  /* display the frame contents which are already analyzed */
  analyzer_create_thumbnails ();
  gtk_widget_set_sensitive (ui->analyze_button, TRUE);
}

void
callback_stream_chooser_new_stream (GtkFileChooserButton * widget,
    gpointer user_data)
{
  if (ui->file_name)
    g_free (ui->file_name);
  ui->file_name = gtk_file_chooser_get_filename ((GtkFileChooser *) widget);

  if (ui->uri)
    g_free (ui->uri);
  ui->uri = gtk_file_chooser_get_uri ((GtkFileChooser *) widget);
  gtk_widget_set_sensitive (ui->analyze_button, TRUE);
}

static void
menu_quit_callback ()
{
  gtk_widget_destroy (ui->main_window);
}

static void
menu_about_callback ()
{
  GFile *license_file;
  GdkPixbuf *logo;
  gchar *contents = NULL;
  gsize length = 0;
  char *file_name;
  char *authors[] =
      { "Sreerenj Balachandran", "&lt; sreerenj.balachandran@intel.com &gt;",
    NULL
  };

  file_name = g_build_filename (DATADIR, "codecanalyzer", "pixmaps",
      "codecanalyzer-logo.png", NULL);
  if (file_name) {
    logo = gdk_pixbuf_new_from_file (file_name, NULL);
    g_free (file_name);
  }

  file_name =
      g_build_filename (DATADIR, "codecanalyzer", "ui", "LICENSE.txt", NULL);
  if (file_name) {
    license_file = g_file_new_for_path (file_name);
    if (license_file) {
      g_file_load_contents (license_file, NULL, &contents, &length, NULL, NULL);
      g_object_unref (G_OBJECT (license_file));
    }
    g_free (file_name);
  }

  gtk_show_about_dialog (GTK_WINDOW (ui->main_window),
      "program-name", "Codecanalyzer",
      "version", PACKAGE_VERSION,
      "copyright", "Copyright © Intel Corporation",
      "authors", authors,
      "comments", "An analyzer for doing in-depth analysis on compressed media",
      "license", contents, "logo", logo, NULL);
  if (logo)
    g_object_unref (G_OBJECT (logo));
  if (contents)
    g_free (contents);
}

static void
menu_help_callback ()
{
  GtkWidget *dialog;
  dialog = gtk_message_dialog_new (GTK_WINDOW (ui->main_window),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
      "See https://github.com/Codecanalyzer/codecanalyzer/blob/master/README");
  gtk_window_set_title (GTK_WINDOW (dialog), "Help");
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static GtkActionEntry entries_actiongroup[] = {
  {"MediaMenuAction", NULL, "Media"},
  {"HelpMenuAction", NULL, "Help"},

  {"QuitAction", GTK_STOCK_QUIT,
        "Quit", "<control>Q",
        "Quit",
      G_CALLBACK (menu_quit_callback)},

  {"HelpAction", GTK_STOCK_HELP,
        "Help", NULL,
        "Help",
      G_CALLBACK (menu_help_callback)},

  {"AboutAction", GTK_STOCK_ABOUT,
        "About", NULL,
        "About",
      G_CALLBACK (menu_about_callback)}
};

static gboolean
analyzer_ui_init ()
{
  GtkActionGroup *action_group;
  char *path;

  ui = g_slice_new0 (AnalyzerUI);

  path =
      g_build_filename (DATADIR, "codecanalyzer", "ui", "mainwindow.xml", NULL);
  ui->builder = make_builder (path);
  g_free (path);

  ui->main_window =
      get_widget_from_builder (ui->builder, "Codecanalyzer-main-window");
  ui->main_vbox = get_widget_from_builder (ui->builder, "MainVBox");
  ui->child_vbox1 = get_widget_from_builder (ui->builder, "child_vbox1");
  ui->child_vbox2 = get_widget_from_builder (ui->builder, "child_vbox2");
  ui->child_vbox3 = get_widget_from_builder (ui->builder, "child_vbox3");
  ui->menubar_vbox = get_widget_from_builder (ui->builder, "menubar_vbox");
  ui->stream_chooser = get_widget_from_builder (ui->builder, "StreamChooser");
  ui->numframes_chooser =
      get_widget_from_builder (ui->builder, "NumFrameEntryButton");
  ui->analyze_button = get_widget_from_builder (ui->builder, "AnalyzeButton");
  ui->cancel_button = get_widget_from_builder (ui->builder, "CancelButton");
  ui->hbox1_in_vbox2 = get_widget_from_builder (ui->builder, "hbox1_in_vbox2");
  ui->child_hbox_in_vbox1_2 = get_widget_from_builder (ui->builder,
      "child_hbox_in_vbox1_2");
  ui->thumbnails_scroll_window =
      get_widget_from_builder (ui->builder, "thumbnails_scrolled_window");
  ui->thumbnails_view_port =
      get_widget_from_builder (ui->builder, "thumbnails_view_port");
  ui->general_info_frame =
      get_widget_from_builder (ui->builder, "general_info_frame");
  ui->general_info_vbox =
      get_widget_from_builder (ui->builder, "general_info_vbox");
  ui->general_info_treeview =
      get_widget_from_builder (ui->builder, "general_info_treeview");
  ui->parsed_info_hbox =
      get_widget_from_builder (ui->builder, "parsed_info_hbox");
  ui->parsed_info_vbox =
      get_widget_from_builder (ui->builder, "parsed_info_vbox");
  ui->parsed_info_frame =
      get_widget_from_builder (ui->builder, "parsed_info_frame");
  ui->parsed_info_button_box =
      get_widget_from_builder (ui->builder, "parsed_info_button_box");

  /* Create menu */
  action_group = gtk_action_group_new ("ActionGroup");
  gtk_action_group_add_actions (action_group, entries_actiongroup,
      G_N_ELEMENTS (entries_actiongroup), NULL);
  ui->menu_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (ui->menu_manager, action_group, 0);
  path = g_build_filename (DATADIR, "codecanalyzer", "ui", "menu.xml", NULL);
  gtk_ui_manager_add_ui_from_file (ui->menu_manager, path, NULL);
  g_free (path);
  ui->menubar = gtk_ui_manager_get_widget (ui->menu_manager, "/MainMenu");
  gtk_box_pack_start (GTK_BOX (ui->menubar_vbox), ui->menubar, FALSE, FALSE, 0);
  gtk_window_add_accel_group (GTK_WINDOW (ui->main_window),
      gtk_ui_manager_get_accel_group (ui->menu_manager));

  ui->notebook_hash = g_hash_table_new (g_str_hash, g_str_equal);
  ui->prev_page = NULL;
  ui->num_frames = 0;

  gtk_window_maximize (GTK_WINDOW (ui->main_window));

  path =
      g_build_filename (DATADIR, "codecanalyzer", "pixmaps",
      "codecanalyzer-logo.png", NULL);
  if (!gtk_window_set_icon_from_file (GTK_WINDOW (ui->main_window), path, NULL))
    g_warning ("Failed to load the icon image.. ");
  g_free (path);

  return TRUE;
}

static gboolean
analyzer_create_dirs ()
{
  const gchar *user_cache_dir;
  gchar *xml_files_path;
  gchar *hex_files_path;
  gboolean ret = TRUE;

  user_cache_dir = g_get_user_cache_dir();
  if (!user_cache_dir) {
    ret = FALSE;
    goto done;
  }

  ui->analyzer_home = g_build_filename (user_cache_dir, "codecanalyzer", NULL);

  xml_files_path = g_build_filename (ui->analyzer_home, "xml", NULL);
  if (g_mkdir_with_parents (xml_files_path, 0777) < 0){
    ret = FALSE;
    goto done;
  }

  hex_files_path = g_build_filename (ui->analyzer_home, "hex", NULL);
  if (g_mkdir_with_parents (hex_files_path, 0777) < 0) {
    ret = FALSE;
    goto done;
  }

  g_debug ("Analyzer_Home %s", ui->analyzer_home);

done:
  if (xml_files_path)
    g_free (xml_files_path);
  if (hex_files_path)
    g_free (hex_files_path);

  return ret;
}

int
main (int argc, char *argv[])
{

  gboolean ret;
  gboolean debug_mode = FALSE;
  GOptionContext *ctx;
  GError *err = NULL;
  GOptionEntry options[] = {
    {"debug-mode", 'd', 0, G_OPTION_ARG_NONE, &debug_mode, "debug mode", NULL},
    {NULL}
  };

  gtk_init (&argc, &argv);

  ctx = g_option_context_new (" -codecanalyzer options");
  g_option_context_add_main_entries (ctx, options, NULL);
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    if (err)
      g_printerr ("Failed to initialize: %s\n", err->message);
    else
      g_printerr ("Failed to initialize, Unknown error\n");
    exit (1);
  }
  g_option_context_free (ctx);

  if (debug_mode) {
    g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL,
        g_log_default_handler, NULL);
    g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
    g_debug ("Codecanalyzer is in DEBUG_MODE..");
  }

  xmlKeepBlanksDefault (0);

  ret = analyzer_ui_init ();
  if (!ret) {
    g_error ("Failed to activate the gtk+-3.x backend \n");
    goto done;
  }

  ret = analyzer_create_dirs ();
  if (!ret) {
    g_error ("Failed to create the necessary dir names \n");
    goto done;
  }

  gtk_builder_connect_signals (ui->builder, NULL);

  gtk_widget_show_all (ui->main_window);

  gtk_main ();

done:
  g_printf ("Closing Codecanalyzer....\n");
  return 0;
}
