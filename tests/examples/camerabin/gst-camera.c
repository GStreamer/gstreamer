/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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
/*
 * This is a demo application to test the camerabin element.
 * If you have question don't hesitate in contact me edgard.lima@indt.org.br
 */

/*
 * Includes
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst-camera.h"

#include <gst/gst.h>
#include <gst/interfaces/videooverlay.h>
#include <gst/interfaces/colorbalance.h>
#include <gst/interfaces/photography.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#include <string.h>

#include <sys/time.h>
#include <time.h>
#include <glib/gstdio.h>        // g_fopen()

#if !GTK_CHECK_VERSION (2, 17, 7)
static void
gtk_widget_get_allocation (GtkWidget * w, GtkAllocation * a)
{
  *a = w->allocation;
}
#endif

/*
 * enums, typedefs and defines
 */

#ifdef USE_MP4
#define VID_FILE_EXT "mp4"
#else
#define VID_FILE_EXT "ogg"
#endif

#define PREVIEW_TIME_MS (2 * 1000)
#define N_BURST_IMAGES 10
#define UI_FILE CAMERA_APPS_UIDIR G_DIR_SEPARATOR_S "gst-camera.ui"

/* Names of default elements */
#define CAMERA_APP_VIDEOSRC "v4l2src"
#define CAMERA_APP_IMAGE_POSTPROC "dummy"

#ifdef HAVE_GST_PHOTO_IFACE_H
#define EV_COMP_MAX 3.0
#define EV_COMP_MIN -3.0
#define EV_COMP_STEP 0.5
#endif

#define DEFAULT_VF_CAPS \
  "video/x-raw-yuv, width = (int) 320, height = (int) 240, framerate = (fraction) 1496/100;" \
  "video/x-raw-yuv, width = (int) 640, height = (int) 480, framerate = (fraction) 1494/100;" \
  "video/x-raw-yuv, width = (int) 800, height = (int) 480, framerate = (fraction) 2503/100;" \
  "video/x-raw-yuv, width = (int) 800, height = (int) 480, framerate = (fraction) 2988/100;" \
  "video/x-raw-yuv, width = (int) 800, height = (int) 480, framerate = (fraction) 1494/100;" \
  "video/x-raw-yuv, width = (int) 720, height = (int) 480, framerate = (fraction) 1494/100"

#define PREVIEW_CAPS \
  "video/x-raw-rgb, width = (int) 640, height = (int) 480"

/* states:
 (image) <---> (video_stopped) <---> (video_recording)
*/
typedef enum _tag_CaptureState
{
  CAP_STATE_IMAGE,
  CAP_STATE_VIDEO_STOPED,
  CAP_STATE_VIDEO_PAUSED,
  CAP_STATE_VIDEO_RECORDING,
} CaptureState;

/*
 * Global Vars
 */

static GtkBuilder *builder = NULL;
static GtkWidget *ui_main_window = NULL;
static GtkWidget *ui_drawing = NULL;
static GtkWidget *ui_drawing_frame = NULL;
static GtkWidget *ui_chk_continous = NULL;
static GtkButton *ui_bnt_shot = NULL;
static GtkButton *ui_bnt_pause = NULL;
static GtkWidget *ui_chk_mute = NULL;
static GtkWidget *ui_vbox_color_controls = NULL;
static GtkWidget *ui_chk_rawmsg = NULL;

static GtkWidget *ui_rdbntImageCapture = NULL;
static GtkWidget *ui_rdbntVideoCapture = NULL;
static GtkWidget *ui_menuitem_photography = NULL;
static GtkWidget *ui_menuitem_capture = NULL;

static GtkComboBox *ui_cbbox_resolution = NULL;
static guint ui_cbbox_resolution_count = 0;

static CaptureState capture_state = CAP_STATE_IMAGE;

static GstElement *gst_camera_bin = NULL;
static GstElement *gst_videosrc = NULL;

static GString *filename = NULL;
static guint32 num_pics = 0;
static guint32 num_pics_cont = 0;
static guint32 num_vids = 0;

static gint max_fr_n = 0;
static gint max_fr_d = 0;
static const gchar *video_post;
static const gchar *image_post;

static GList *video_caps_list = NULL;

static guint bus_handler_id = 0;

#ifdef HAVE_GST_PHOTO_IFACE_H
static gchar *iso_speed_labels[] = { "auto", "100", "200", "400" };

static struct
{
  const gchar *label;
  gint width;
  gint height;
} image_resolution_label_map[] = {
  {
  "View finder resolution", 0, 0}, {
  "VGA", 640, 480}, {
  "1,3Mpix (1280x960)", 1280, 960}, {
  "3Mpix (2048x1536)", 2048, 1536}, {
  "3,7Mpix 16:9 (2592x1456)", 2592, 1456}, {
  "5Mpix (2592x1968)", 2592, 1968}
};
#endif

/*
 * functions prototypes
 */
static gboolean me_gst_setup_pipeline (const gchar * imagepost,
    const gchar * videopost);
static void me_gst_cleanup_element (void);

static gboolean capture_mode_set_state (CaptureState state);
static void capture_mode_config_gui (void);
static gboolean capture_mode_stop (void);

static void ui_connect_signals (void);
static gboolean ui_create (void);
static void destroy_color_controls (void);
static void create_color_controls (void);
static void init_view_finder_resolution_combobox (void);

#ifdef HAVE_GST_PHOTO_IFACE_H
static void menuitem_toggle_active (GtkWidget * widget, gpointer data);
static void sub_menu_initialize (GtkWidget * widget, gpointer data);
static void fill_photography_menu (GtkMenuItem * parent_item);
#endif

/*
 * functions implementation
 */

static void
set_filename (GString * name)
{
  const gchar *datadir;

  if (capture_state == CAP_STATE_IMAGE) {
    g_string_printf (name, G_DIR_SEPARATOR_S "test_%04u.jpg", num_pics);
    datadir = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
  } else {
    g_string_printf (name, G_DIR_SEPARATOR_S "test_%04u.%s", num_vids,
        VID_FILE_EXT);
    datadir = g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS);
  }

  if (datadir == NULL) {
    gchar *curdir = g_get_current_dir ();
    g_string_prepend (name, curdir);
    g_free (curdir);
  } else {
    g_string_prepend (name, datadir);
  }
  GST_INFO ("capture to %s", name->str);
}

/* Write raw image buffer to file if found from message */
static void
handle_element_message (GstMessage * msg)
{
  const GstStructure *st;
  const GValue *image;
  GstBuffer *buf = NULL;
  guint8 *data = NULL;
  gchar *caps_string;
  guint size = 0;
  gchar *filename = NULL;
  FILE *f = NULL;
  size_t written;

  st = gst_message_get_structure (msg);
  if (g_str_equal (gst_structure_get_name (st), "autofocus-done")) {
    gtk_button_set_label (ui_bnt_pause, "Focus");
  } else if (gst_structure_has_field_typed (st, "buffer", GST_TYPE_BUFFER)) {
    image = gst_structure_get_value (st, "buffer");
    if (image) {
      buf = gst_value_get_buffer (image);
      data = GST_BUFFER_DATA (buf);
      size = GST_BUFFER_SIZE (buf);
      if (g_str_equal (gst_structure_get_name (st), "raw-image")) {
        filename = g_strdup_printf ("test_%04u.raw", num_pics);
      } else if (g_str_equal (gst_structure_get_name (st), "preview-image")) {
        filename = g_strdup_printf ("test_%04u_vga.rgb", num_pics);
      } else {
        /* for future purposes */
        g_print ("unknown buffer received\n");
        return;
      }
      caps_string = gst_caps_to_string (GST_BUFFER_CAPS (buf));
      g_print ("writing buffer to %s, buffer caps: %s\n",
          filename, caps_string);
      g_free (caps_string);
      f = g_fopen (filename, "w");
      if (f) {
        written = fwrite (data, size, 1, f);
        if (!written) {
          g_print ("errro writing file\n");
        }
        fclose (f);
      } else {
        g_print ("error opening file for raw image writing\n");
      }
      g_free (filename);
    }
  } else if (g_str_equal (gst_structure_get_name (st), "photo-capture-start")) {
    g_print ("=== CLICK ===\n");
  }
}

static GstBusSyncReply
my_bus_sync_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;

  if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
    return GST_BUS_PASS;

  /* FIXME: make sure to get XID in main thread */
  gst_x_overlay_set_window_handle (GST_X_OVERLAY (message->src),
#if GTK_CHECK_VERSION (2, 91, 6)
      GDK_WINDOW_XID (gtk_widget_get_window (ui_drawing)));
#else
      GDK_WINDOW_XWINDOW (gtk_widget_get_window (ui_drawing)));
#endif

  gst_message_unref (message);
  return GST_BUS_DROP;
}

static void
print_error_message (GstMessage * msg)
{
  GError *err = NULL;
  gchar *dbg = NULL;

  gst_message_parse_error (msg, &err, &dbg);

  g_printerr ("Camerabin won't start up!\nError: %s\nDebug Info: %s\n",
      err->message, (dbg) ? dbg : "None");

  g_error_free (err);
  g_free (dbg);
}

static gboolean
my_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_WARNING:{
      GError *err;
      gchar *debug;

      gst_message_parse_warning (message, &err, &debug);
      g_print ("Warning: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_ERROR:{
      print_error_message (message);
      me_gst_cleanup_element ();
      gtk_main_quit ();
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      gtk_main_quit ();
      break;
    case GST_MESSAGE_STATE_CHANGED:{
      GstState old, new, pending;

      gst_message_parse_state_changed (message, &old, &new, &pending);

      GST_DEBUG_OBJECT (GST_MESSAGE_SRC (message), "state-change %s -> %s",
          gst_element_state_get_name (old), gst_element_state_get_name (new));

      /* Create/destroy color controls according videosrc state */
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (gst_videosrc)) {
        GST_INFO_OBJECT (GST_MESSAGE_SRC (message), "state-change %s -> %s",
            gst_element_state_get_name (old), gst_element_state_get_name (new));

        if (old == GST_STATE_READY && new == GST_STATE_NULL) {
          destroy_color_controls ();
        } else if (old == GST_STATE_NULL && new == GST_STATE_READY) {
          create_color_controls ();
        }
      }

      /* we only care about pipeline state change messages */
      if (GST_IS_PIPELINE (GST_MESSAGE_SRC (message))) {
        /* dump graph for pipeline state changes */
        gchar *dump_name = g_strdup_printf ("camerabin.%s_%s",
            gst_element_state_get_name (old),
            gst_element_state_get_name (new));
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (GST_MESSAGE_SRC (message)),
            GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE |
            GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, dump_name);
        g_free (dump_name);
      }
      break;
    }
    case GST_MESSAGE_ELEMENT:
    {
      handle_element_message (message);
      break;
    }
    default:
      /* unhandled message */
      break;
  }
  return TRUE;
}

static void
me_set_next_cont_file_name (GString * filename)
{
  /* FIXME: better file naming (possible with signal) */
  if (G_UNLIKELY (num_pics_cont == 1)) {
    gint i;
    for (i = filename->len - 1; i > 0; --i) {
      if (filename->str[i] == '.')
        break;
    }
    g_string_insert (filename, i, "_0001");
  } else {
    gchar tmp[6];
    gint i;
    for (i = filename->len - 1; i > 0; --i) {
      if (filename->str[i] == '_')
        break;
    }
    snprintf (tmp, 6, "_%04d", num_pics_cont);
    memcpy (filename->str + i, tmp, 5);
  }
}

static gboolean
stop_image_preview (gpointer data)
{
  g_return_val_if_fail (data != NULL, FALSE);

  g_signal_emit_by_name (data, "capture-stop", 0);

  return FALSE;
}

static gboolean
me_image_capture_done (GstElement * camera, const gchar * fname,
    gpointer user_data)
{
  gboolean cont =
      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ui_chk_continous));
  GString *filename = g_string_new (fname);

  if (num_pics_cont < N_BURST_IMAGES && cont) {
    num_pics_cont++;
    me_set_next_cont_file_name (filename);
    g_object_set (G_OBJECT (camera), "filename", filename->str, NULL);
    g_string_free (filename, TRUE);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (ui_bnt_shot), TRUE);
    printf ("%u image(s) saved\n", num_pics_cont + 1);
    fflush (stdout);
    num_pics_cont = 0;

    g_timeout_add (PREVIEW_TIME_MS, (GSourceFunc) stop_image_preview, camera);

    cont = FALSE;
  }
  return cont;
}

static gboolean
me_gst_setup_pipeline_create_post_bin (const gchar * post, gboolean video)
{
  GstElement *vpp = NULL;
  GstElement *bin, *c1, *c2, *filter;
  GstPad *pad;
  GstCaps *caps;

  /* this function uses a bin just because it needs ffmpegcolorspace. For
   * performance reason one should provide an element without need for color
   * convertion */

  vpp = gst_element_factory_make (post, NULL);
  if (NULL == vpp) {
    fprintf (stderr, "cannot create \'%s\' element\n", post);
    fflush (stderr);
    goto done;
  }
  c1 = gst_element_factory_make ("ffmpegcolorspace", NULL);
  c2 = gst_element_factory_make ("ffmpegcolorspace", NULL);
  if (NULL == c1 || NULL == c2) {
    fprintf (stderr, "cannot create \'ffmpegcolorspace\' element\n");
    fflush (stderr);
    goto done;
  }
  filter = gst_element_factory_make ("capsfilter", NULL);
  if (NULL == filter) {
    fprintf (stderr, "cannot create \'capsfilter\' element\n");
    fflush (stderr);
    goto done;
  }
  bin = gst_bin_new (video ? "vid_postproc_bin" : "img_postproc_bin");
  if (NULL == bin) {
    goto done;
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'), NULL);
  g_object_set (G_OBJECT (filter), "caps", caps, NULL);
  gst_caps_unref (caps);

  gst_bin_add_many (GST_BIN (bin), c1, vpp, c2, filter, NULL);
  if (!gst_element_link_many (c1, vpp, c2, filter, NULL)) {
    fprintf (stderr, "cannot link video post proc elements\n");
    fflush (stderr);
    goto done;
  }

  pad = gst_element_get_static_pad (c1, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  pad = gst_element_get_static_pad (filter, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));

  g_object_set (gst_camera_bin,
      (video ? "video-post-processing" : "image-post-processing"), bin, NULL);
  return TRUE;
done:
  return FALSE;
}

static void
me_gst_setup_pipeline_create_codecs (void)
{
#ifdef USE_MP4
  g_object_set (gst_camera_bin, "video-encoder",
      gst_element_factory_make ("omx_mpeg4enc", NULL), NULL);

  g_object_set (gst_camera_bin, "audio-encoder",
      gst_element_factory_make ("omx_aacenc", NULL), NULL);

  g_object_set (gst_camera_bin, "video-muxer",
      gst_element_factory_make ("hantromp4mux", NULL), NULL);
#else
  /* using defaults theora, vorbis, ogg */
#endif
}

static gboolean
me_gst_setup_pipeline_create_img_post_bin (const gchar * imagepost)
{
  return me_gst_setup_pipeline_create_post_bin (imagepost, FALSE);
}

static gboolean
me_gst_setup_pipeline_create_vid_post_bin (const gchar * videopost)
{
  return me_gst_setup_pipeline_create_post_bin (videopost, TRUE);
}

static gboolean
me_gst_setup_pipeline (const gchar * imagepost, const gchar * videopost)
{
  GstBus *bus;
  GstCaps *preview_caps;

  set_filename (filename);

  me_gst_cleanup_element ();

  gst_camera_bin = gst_element_factory_make ("camerabin", NULL);
  if (NULL == gst_camera_bin) {
    goto done;
  }

  g_signal_connect (gst_camera_bin, "image-done",
      (GCallback) me_image_capture_done, NULL);

  preview_caps = gst_caps_from_string (PREVIEW_CAPS);

  bus = gst_pipeline_get_bus (GST_PIPELINE (gst_camera_bin));
  bus_handler_id = gst_bus_add_watch (bus, my_bus_callback, NULL);
  gst_bus_set_sync_handler (bus, my_bus_sync_callback, NULL);
  gst_object_unref (bus);

  /* set properties */
  g_object_set (gst_camera_bin, "filename", filename->str, NULL);
  g_object_set (gst_camera_bin, "preview-caps", preview_caps, NULL);
  g_object_set (gst_camera_bin, "flags", 0xdf, NULL);
  gst_caps_unref (preview_caps);

  gst_videosrc = gst_element_factory_make (CAMERA_APP_VIDEOSRC, NULL);
  if (gst_videosrc) {
    g_object_set (G_OBJECT (gst_camera_bin), "video-source", gst_videosrc,
        NULL);
  }

  if (imagepost) {
    if (!me_gst_setup_pipeline_create_img_post_bin (imagepost))
      goto done;
  } else {
    /* Use default image postprocessing element */
    GstElement *ipp =
        gst_element_factory_make (CAMERA_APP_IMAGE_POSTPROC, NULL);
    if (ipp) {
      g_object_set (G_OBJECT (gst_camera_bin), "image-post-processing", ipp,
          NULL);
    }
  }

  if (videopost) {
    if (!me_gst_setup_pipeline_create_vid_post_bin (videopost))
      goto done;
  }

  me_gst_setup_pipeline_create_codecs ();

  if (GST_STATE_CHANGE_FAILURE ==
      gst_element_set_state (gst_camera_bin, GST_STATE_READY)) {
    goto done;
  }

  if (!gst_videosrc) {
    g_object_get (G_OBJECT (gst_camera_bin), "video-source", &gst_videosrc,
        NULL);
  }

  init_view_finder_resolution_combobox ();

  gst_element_set_state (gst_camera_bin, GST_STATE_PLAYING);

#ifdef HAVE_GST_PHOTO_IFACE_H
  /* Initialize menus to default settings */
  GtkWidget *sub_menu =
      gtk_menu_item_get_submenu (GTK_MENU_ITEM (ui_menuitem_capture));
  gtk_container_foreach (GTK_CONTAINER (sub_menu), sub_menu_initialize, NULL);
  sub_menu =
      gtk_menu_item_get_submenu (GTK_MENU_ITEM (ui_menuitem_photography));
  gtk_container_foreach (GTK_CONTAINER (sub_menu), sub_menu_initialize, NULL);
#endif

  capture_state = CAP_STATE_IMAGE;
  return TRUE;
done:
  fprintf (stderr, "error to create pipeline\n");
  fflush (stderr);
  me_gst_cleanup_element ();
  return FALSE;
}

static gboolean
me_gst_setup_default_pipeline (gpointer data)
{
  if (!me_gst_setup_pipeline (NULL, NULL)) {
    gtk_main_quit ();
  }
  return FALSE;
}

static void
me_gst_cleanup_element (void)
{
  if (gst_camera_bin) {
    GstBus *bus;

    gst_element_set_state (gst_camera_bin, GST_STATE_NULL);
    gst_element_get_state (gst_camera_bin, NULL, NULL, GST_CLOCK_TIME_NONE);

    bus = gst_pipeline_get_bus (GST_PIPELINE (gst_camera_bin));
    gst_bus_set_sync_handler (bus, NULL, NULL);
    g_source_remove (bus_handler_id);

    gst_object_unref (gst_camera_bin);
    gst_camera_bin = NULL;

    g_list_foreach (video_caps_list, (GFunc) gst_caps_unref, NULL);
    g_list_free (video_caps_list);
    video_caps_list = NULL;
  }
}

static gboolean
capture_mode_stop (void)
{
  if (capture_state == CAP_STATE_VIDEO_PAUSED
      || capture_state == CAP_STATE_VIDEO_RECORDING) {
    return capture_mode_set_state (CAP_STATE_VIDEO_STOPED);
  } else {
    return TRUE;
  }
}

static void
capture_mode_config_gui (void)
{
  switch (capture_state) {
    case CAP_STATE_IMAGE:
      gtk_button_set_label (ui_bnt_shot, "Shot");
      gtk_button_set_label (ui_bnt_pause, "Focus");
      gtk_widget_set_sensitive (GTK_WIDGET (ui_bnt_pause), TRUE);
      gtk_widget_show (ui_chk_continous);
      gtk_widget_show (ui_chk_rawmsg);
      gtk_widget_hide (ui_chk_mute);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ui_rdbntImageCapture),
          TRUE);
      break;
    case CAP_STATE_VIDEO_STOPED:
      gtk_button_set_label (ui_bnt_shot, "Rec");
      gtk_button_set_label (ui_bnt_pause, "Pause");
      gtk_widget_set_sensitive (GTK_WIDGET (ui_bnt_pause), FALSE);
      gtk_widget_show (GTK_WIDGET (ui_bnt_pause));
      gtk_widget_show (ui_chk_mute);
      gtk_widget_hide (ui_chk_continous);
      gtk_widget_hide (ui_chk_rawmsg);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ui_rdbntVideoCapture),
          TRUE);
      break;
    case CAP_STATE_VIDEO_PAUSED:
      gtk_button_set_label (ui_bnt_pause, "Cont");
      break;
    case CAP_STATE_VIDEO_RECORDING:
      gtk_button_set_label (ui_bnt_shot, "Stop");
      gtk_button_set_label (ui_bnt_pause, "Pause");
      gtk_widget_set_sensitive (GTK_WIDGET (ui_bnt_pause), TRUE);
      break;
    default:
      break;
  }
}

static gboolean
capture_mode_set_state (CaptureState state)
{
  if (capture_state == state)
    return TRUE;

  switch (capture_state) {
    case CAP_STATE_IMAGE:
      if (state == CAP_STATE_VIDEO_PAUSED) {
        goto done;
      }
      g_object_set (gst_camera_bin, "mode", 1, NULL);
      capture_state = CAP_STATE_VIDEO_STOPED;
      if (state == CAP_STATE_VIDEO_RECORDING)
        capture_mode_set_state (state);
      break;
    case CAP_STATE_VIDEO_STOPED:
      if (state == CAP_STATE_VIDEO_PAUSED) {
        goto done;
      }
      capture_state = state;
      if (state == CAP_STATE_IMAGE)
        g_object_set (gst_camera_bin, "mode", 0, NULL);
      else {                    /* state == CAP_STATE_VIDEO_RECORDING */
        g_object_set (gst_camera_bin, "mode", 1, NULL);
        g_signal_emit_by_name (gst_camera_bin, "capture-start", 0);
      }
      break;
    case CAP_STATE_VIDEO_PAUSED:
      if (state == CAP_STATE_VIDEO_RECORDING) {
        g_signal_emit_by_name (gst_camera_bin, "capture-start", 0);
        capture_state = CAP_STATE_VIDEO_RECORDING;
      } else {
        g_signal_emit_by_name (gst_camera_bin, "capture-stop", 0);
        capture_state = CAP_STATE_VIDEO_STOPED;
        if (state == CAP_STATE_IMAGE)
          capture_mode_set_state (state);
      }
      break;
    case CAP_STATE_VIDEO_RECORDING:
      if (state == CAP_STATE_VIDEO_PAUSED) {
        g_signal_emit_by_name (gst_camera_bin, "capture-pause", 0);
        capture_state = CAP_STATE_VIDEO_PAUSED;
      } else {
        g_signal_emit_by_name (gst_camera_bin, "capture-stop", 0);
        capture_state = CAP_STATE_VIDEO_STOPED;
        if (state == CAP_STATE_IMAGE)
          capture_mode_set_state (state);
      }
      break;
  }
  return TRUE;
done:
  return FALSE;
}

void
on_windowMain_delete_event (GtkWidget * widget, GdkEvent * event, gpointer data)
{
  capture_mode_set_state (CAP_STATE_IMAGE);
  capture_mode_config_gui ();
  me_gst_cleanup_element ();
  gtk_main_quit ();
}

static void
set_metadata (void)
{
  /* for more information about image metadata tags, see:
   * http://webcvs.freedesktop.org/gstreamer/gst-plugins-bad/tests/icles/metadata_editor.c
   * and for the mapping:
   * http://webcvs.freedesktop.org/gstreamer/gst-plugins-bad/ext/metadata/metadata_mapping.htm?view=co
   */

  GstTagSetter *setter = GST_TAG_SETTER (gst_camera_bin);
  GTimeVal time = { 0, 0 };
  gchar *date_str, *desc_str;

  g_get_current_time (&time);
  date_str = g_time_val_to_iso8601 (&time);     /* this is UTC */
  desc_str = g_strdup_printf ("picture taken by %s", g_get_real_name ());

  gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
      "date-time-original", date_str,
      "date-time-modified", date_str,
      "creator-tool", "camerabin-demo",
      GST_TAG_DESCRIPTION, desc_str,
      GST_TAG_TITLE, "My picture", GST_TAG_COPYRIGHT, "LGPL", NULL);

  g_free (date_str);
  g_free (desc_str);
}

void
on_buttonShot_clicked (GtkButton * button, gpointer user_data)
{
  switch (capture_state) {
    case CAP_STATE_IMAGE:
    {
      gtk_widget_set_sensitive (GTK_WIDGET (ui_bnt_shot), FALSE);
      set_filename (filename);
      num_pics++;
      g_object_set (gst_camera_bin, "filename", filename->str, NULL);

      set_metadata ();
      g_signal_emit_by_name (gst_camera_bin, "capture-start", 0);
    }
      break;
    case CAP_STATE_VIDEO_STOPED:
      set_filename (filename);
      num_vids++;
      g_object_set (gst_camera_bin, "filename", filename->str, NULL);
      capture_mode_set_state (CAP_STATE_VIDEO_RECORDING);
      capture_mode_config_gui ();
      break;
    case CAP_STATE_VIDEO_PAUSED:
      /* fall trough */
    case CAP_STATE_VIDEO_RECORDING:
      capture_mode_set_state (CAP_STATE_VIDEO_STOPED);
      capture_mode_config_gui ();
      break;
    default:
      break;
  }
}

void
on_buttonPause_clicked (GtkButton * button, gpointer user_data)
{
  switch (capture_state) {
    case CAP_STATE_IMAGE:
      if (g_str_equal (gtk_button_get_label (ui_bnt_pause), "Focus")) {
        /* Start autofocus */
        gst_photography_set_autofocus (GST_PHOTOGRAPHY (gst_videosrc), TRUE);
        gtk_button_set_label (ui_bnt_pause, "Cancel Focus");
      } else {
        /* Cancel autofocus */
        gst_photography_set_autofocus (GST_PHOTOGRAPHY (gst_videosrc), FALSE);
        gtk_button_set_label (ui_bnt_pause, "Focus");
      }
      break;
    case CAP_STATE_VIDEO_STOPED:
      break;
    case CAP_STATE_VIDEO_PAUSED:
      capture_mode_set_state (CAP_STATE_VIDEO_RECORDING);
      capture_mode_config_gui ();
      break;
    case CAP_STATE_VIDEO_RECORDING:
      capture_mode_set_state (CAP_STATE_VIDEO_PAUSED);
      capture_mode_config_gui ();
      break;
    default:
      break;
  }
}

void
on_drawingareaView_realize (GtkWidget * widget, gpointer data)
{
#if GTK_CHECK_VERSION (2, 18, 0)
  gdk_window_ensure_native (gtk_widget_get_window (widget));
#endif
}

gboolean
on_drawingareaView_configure_event (GtkWidget * widget,
    GdkEventConfigure * event, gpointer data)
{
  GtkAllocation a;

  gtk_widget_get_allocation (widget, &a);
  gdk_window_move_resize (gtk_widget_get_window (widget),
      a.x, a.y, a.width, a.height);
  gdk_display_sync (gtk_widget_get_display (widget));

  return TRUE;
}

void
on_comboboxResolution_changed (GtkComboBox * widget, gpointer user_data)
{
  GstStructure *st;
  gint w = 0, h = 0;
  GstCaps *video_caps =
      g_list_nth_data (video_caps_list, gtk_combo_box_get_active (widget));

  if (video_caps) {
    GstState old;

    gst_element_get_state (gst_camera_bin, &old, NULL, GST_CLOCK_TIME_NONE);
    GST_DEBUG ("change resolution in %s", gst_element_state_get_name (old));

    if (old != GST_STATE_NULL) {
      gst_element_set_state (gst_camera_bin, GST_STATE_READY);
      /* source need to be NULL, otherwise changing the mode fails with device
       * busy:
       * - if src goes from NULL->PLAYING it sets new mode anyway
       * - if src goes form READY->PLAYIN new mode is activated via reverse caps
       *   negotiation, but then the device is already streaming
       */
      gst_element_set_state (gst_videosrc, GST_STATE_NULL);
    }

    st = gst_caps_get_structure (video_caps, 0);

    gst_structure_get_int (st, "width", &w);
    gst_structure_get_int (st, "height", &h);

    if (w && h) {
      g_object_set (ui_drawing_frame, "ratio", (gfloat) w / (gfloat) h, NULL);
    }

    g_object_set (G_OBJECT (gst_camera_bin), "filter-caps", video_caps, NULL);

    if (old != GST_STATE_NULL) {
      gst_element_set_state (gst_camera_bin, old);
    }
  }
}

void
on_radiobuttonImageCapture_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton)) {
    if (capture_state != CAP_STATE_IMAGE) {
      capture_mode_set_state (CAP_STATE_IMAGE);
      capture_mode_config_gui ();
    }
  }
}

void
on_radiobuttonVideoCapture_toggled (GtkToggleButton * togglebutton,
    gpointer user_data)
{
  if (gtk_toggle_button_get_active (togglebutton)) {
    if (capture_state == CAP_STATE_IMAGE) {
      capture_mode_set_state (CAP_STATE_VIDEO_STOPED);
      capture_mode_config_gui ();
    }
  }
}

static void
on_rbBntVidEff_toggled (GtkToggleButton * togglebutton, const gchar * effect)
{
  if (gtk_toggle_button_get_active (togglebutton)) {
    /* lets also use those effects to image */
    video_post = effect;
    image_post = effect;
    capture_mode_stop ();

    me_gst_cleanup_element ();
    if (!me_gst_setup_pipeline (image_post, video_post))
      gtk_main_quit ();
    capture_mode_config_gui ();
  }
}

void
on_rbBntVidEffNone_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  on_rbBntVidEff_toggled (togglebutton, NULL);
}

void
on_rbBntVidEffEdge_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  on_rbBntVidEff_toggled (togglebutton, "edgetv");
}

void
on_rbBntVidEffAging_toggled (GtkToggleButton * togglebutton, gpointer user_data)
{
  on_rbBntVidEff_toggled (togglebutton, "agingtv");
}

void
on_rbBntVidEffDice_toggled (GtkToggleButton * togglebutton, gpointer user_data)
{
  on_rbBntVidEff_toggled (togglebutton, "dicetv");
}

void
on_rbBntVidEffWarp_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  on_rbBntVidEff_toggled (togglebutton, "warptv");
}

void
on_rbBntVidEffShagadelic_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  on_rbBntVidEff_toggled (togglebutton, "shagadelictv");
}

void
on_rbBntVidEffVertigo_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  on_rbBntVidEff_toggled (togglebutton, "vertigotv");
}

void
on_rbBntVidEffRev_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  on_rbBntVidEff_toggled (togglebutton, "revtv");
}

void
on_rbBntVidEffQuark_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  on_rbBntVidEff_toggled (togglebutton, "quarktv");
}

void
on_chkbntMute_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  g_object_set (gst_camera_bin, "mute",
      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (togglebutton)), NULL);
}

void
on_chkbtnRawMsg_toggled (GtkToggleButton * togglebutton, gpointer data)
{
  const gchar *env_var = "CAMSRC_PUBLISH_RAW";
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (togglebutton))) {
    g_setenv (env_var, "1", TRUE);
  } else {
    g_unsetenv (env_var);
  }
}

void
on_hscaleZoom_value_changed (GtkRange * range, gpointer user_data)
{
  gint zoom = gtk_range_get_value (range);
  g_object_set (gst_camera_bin, "zoom", zoom, NULL);
}

void
on_color_control_value_changed (GtkRange * range, gpointer user_data)
{
  GstColorBalance *balance = GST_COLOR_BALANCE (gst_camera_bin);
  gint val = gtk_range_get_value (range);
  GstColorBalanceChannel *channel = (GstColorBalanceChannel *) user_data;
  gst_color_balance_set_value (balance, channel, val);
}

#ifndef GDK_KEY_F11
#define GDK_KEY_F11 GDK_F11
#endif

gboolean
on_key_released (GtkWidget * widget, GdkEventKey * event, gpointer user_data)
{
  g_return_val_if_fail (event != NULL, FALSE);

  switch (event->keyval) {
    case GDK_KEY_F11:
#ifdef HAVE_GST_PHOTO_IFACE_H
      gst_photography_set_autofocus (GST_PHOTOGRAPHY (gst_videosrc), FALSE);
#endif
      break;
    default:
      break;
  }

  return FALSE;
}

gboolean
on_key_pressed (GtkWidget * widget, GdkEventKey * event, gpointer user_data)
{
  g_return_val_if_fail (event != NULL, FALSE);

  switch (event->keyval) {
    case GDK_KEY_F11:
#ifdef HAVE_GST_PHOTO_IFACE_H
      gst_photography_set_autofocus (GST_PHOTOGRAPHY (gst_videosrc), TRUE);
#endif
      break;
    case 0x0:
      on_buttonShot_clicked (NULL, NULL);
      break;
    default:
      break;
  }

  return FALSE;
}

static void
ui_connect_signals (void)
{
  gtk_builder_connect_signals (builder, NULL);

  g_signal_connect (ui_main_window, "key-press-event",
      (GCallback) on_key_pressed, NULL);

  g_signal_connect (ui_main_window, "key-release-event",
      (GCallback) on_key_released, NULL);
}

static gchar *
format_value_callback (GtkScale * scale, gdouble value, gpointer user_data)
{
  GstColorBalanceChannel *channel = (GstColorBalanceChannel *) user_data;

  return g_strdup_printf ("%s: %d", channel->label, (gint) value);
}

static gint
create_menu_items_from_structure (GstStructure * structure)
{
  GtkListStore *store;
  const GValue *framerate_list = NULL;
  const gchar *structure_name;
  GString *item_str = NULL;
  guint j, num_items_created = 0, num_framerates = 1;
  gint w = 0, h = 0, n = 0, d = 1;
  guint32 fourcc = 0;

  g_return_val_if_fail (structure != NULL, 0);

  structure_name = gst_structure_get_name (structure);

  /* lets filter yuv only */
  if (0 == strcmp (structure_name, "video/x-raw-yuv")) {
    item_str = g_string_new_len ("", 128);

    if (gst_structure_has_field_typed (structure, "format", GST_TYPE_FOURCC)) {
      gst_structure_get_fourcc (structure, "format", &fourcc);
    }

    if (gst_structure_has_field_typed (structure, "width", GST_TYPE_INT_RANGE)) {
      const GValue *wrange = gst_structure_get_value (structure, "width");
      /* If range found, use the maximum */
      w = gst_value_get_int_range_max (wrange);
    } else if (gst_structure_has_field_typed (structure, "width", G_TYPE_INT)) {
      gst_structure_get_int (structure, "width", &w);
    }

    if (gst_structure_has_field_typed (structure, "height", GST_TYPE_INT_RANGE)) {
      const GValue *hrange = gst_structure_get_value (structure, "height");
      /* If range found, use the maximum */
      h = gst_value_get_int_range_max (hrange);
    } else if (gst_structure_has_field_typed (structure, "height", G_TYPE_INT)) {
      gst_structure_get_int (structure, "height", &h);
    }

    if (gst_structure_has_field_typed (structure, "framerate",
            GST_TYPE_FRACTION)) {
      gst_structure_get_fraction (structure, "framerate", &n, &d);
    } else if (gst_structure_has_field_typed (structure, "framerate",
            GST_TYPE_LIST)) {
      framerate_list = gst_structure_get_value (structure, "framerate");
      num_framerates = gst_value_list_get_size (framerate_list);
    } else if (gst_structure_has_field_typed (structure, "framerate",
            GST_TYPE_FRACTION_RANGE)) {
      const GValue *fr = gst_structure_get_value (structure, "framerate");
      const GValue *frmax = gst_value_get_fraction_range_max (fr);
      max_fr_n = gst_value_get_fraction_numerator (frmax);
      max_fr_d = gst_value_get_fraction_denominator (frmax);
    }

    if (max_fr_n || max_fr_d) {
      goto range_found;
    }

    store = GTK_LIST_STORE (gtk_combo_box_get_model (ui_cbbox_resolution));
    for (j = 0; j < num_framerates; j++) {
      GstCaps *video_caps;
      GtkTreeIter iter;

      if (framerate_list) {
        const GValue *item = gst_value_list_get_value (framerate_list, j);
        n = gst_value_get_fraction_numerator (item);
        d = gst_value_get_fraction_denominator (item);
      }
      g_string_assign (item_str, structure_name);
      g_string_append_printf (item_str, " (%" GST_FOURCC_FORMAT ")",
          GST_FOURCC_ARGS (fourcc));
      g_string_append_printf (item_str, ", %dx%d at %d/%d", w, h, n, d);
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, item_str->str, -1);

      video_caps =
          gst_caps_new_simple (structure_name, "format", GST_TYPE_FOURCC,
          fourcc,
          "width", G_TYPE_INT, w, "height", G_TYPE_INT, h,
          "framerate", GST_TYPE_FRACTION, n, d, NULL);
      video_caps_list = g_list_append (video_caps_list, video_caps);
      num_items_created++;
    }
  }

range_found:

  if (item_str) {
    g_string_free (item_str, TRUE);
  }

  return num_items_created;
}

static void
fill_resolution_combo (GstCaps * caps)
{
  guint size, num_items, i;
  GstStructure *st;

  max_fr_n = max_fr_d = 0;

  /* Create new items */
  size = gst_caps_get_size (caps);

  for (i = 0; i < size; i++) {
    st = gst_caps_get_structure (caps, i);
    num_items = create_menu_items_from_structure (st);
    ui_cbbox_resolution_count += num_items;
  }
}

static GstCaps *
create_default_caps (void)
{
  GstCaps *default_caps;

  default_caps = gst_caps_from_string (DEFAULT_VF_CAPS);

  return default_caps;
}

static void
init_view_finder_resolution_combobox (void)
{
  GstCaps *input_caps = NULL, *default_caps = NULL, *intersect = NULL;

  g_object_get (gst_camera_bin, "video-source-caps", &input_caps, NULL);
  if (input_caps) {
    fill_resolution_combo (input_caps);
  }

  /* Fill in default items if supported */
  default_caps = create_default_caps ();
  intersect = gst_caps_intersect (default_caps, input_caps);
  if (intersect) {
    fill_resolution_combo (intersect);
    gst_caps_unref (intersect);
  }
  gst_caps_unref (default_caps);

  if (input_caps) {
    gst_caps_unref (input_caps);
  }

  /* Set some item active */
  gtk_combo_box_set_active (ui_cbbox_resolution, ui_cbbox_resolution_count - 1);
}

static void
destroy_color_controls (void)
{
  GList *widgets, *item;
  GtkWidget *widget = NULL;
  gpointer user_data = NULL;

  widgets = gtk_container_get_children (GTK_CONTAINER (ui_vbox_color_controls));
  for (item = widgets; item; item = g_list_next (item)) {
    widget = GTK_WIDGET (item->data);
    user_data = g_object_get_data (G_OBJECT (widget), "channel");
    g_signal_handlers_disconnect_by_func (widget, (GFunc) format_value_callback,
        user_data);
    g_signal_handlers_disconnect_by_func (widget,
        (GFunc) on_color_control_value_changed, user_data);
    gtk_container_remove (GTK_CONTAINER (ui_vbox_color_controls), widget);
  }
  g_list_free (widgets);
}

static void
create_color_controls (void)
{
  GstColorBalance *balance = NULL;
  const GList *controls, *item;
  GstColorBalanceChannel *channel;
  GtkWidget *hscale;

  if (GST_IS_COLOR_BALANCE (gst_camera_bin)) {
    balance = GST_COLOR_BALANCE (gst_camera_bin);
  }

  if (NULL == balance) {
    goto done;
  }

  controls = gst_color_balance_list_channels (balance);
  for (item = controls; item; item = g_list_next (item)) {
    channel = item->data;

    hscale = gtk_hscale_new ((GtkAdjustment *)
        gtk_adjustment_new (gst_color_balance_get_value (balance, channel),
            channel->min_value, channel->max_value, 1, 10, 10));

    g_signal_connect (GTK_RANGE (hscale), "value-changed",
        (GCallback) on_color_control_value_changed, (gpointer) channel);
    g_signal_connect (GTK_SCALE (hscale), "format-value",
        (GCallback) format_value_callback, (gpointer) channel);
    g_object_set_data (G_OBJECT (hscale), "channel", (gpointer) channel);

    gtk_box_pack_start (GTK_BOX (ui_vbox_color_controls), GTK_WIDGET (hscale),
        FALSE, TRUE, 0);
  }

  gtk_widget_show_all (ui_vbox_color_controls);
done:
  return;
}

#ifdef HAVE_GST_PHOTO_IFACE_H
static void
menuitem_toggle_active (GtkWidget * widget, gpointer data)
{
  gboolean active;
  g_object_get (G_OBJECT (widget), "active", &active, NULL);
  if (active) {
    gtk_check_menu_item_toggled (GTK_CHECK_MENU_ITEM (widget));
  }
}

static void
sub_menu_initialize (GtkWidget * widget, gpointer data)
{
  GtkWidget *submenu;
  submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (widget));
  gtk_container_foreach (GTK_CONTAINER (submenu), menuitem_toggle_active, NULL);
}

void
photo_menuitem_toggled_cb (GtkRadioMenuItem * menuitem, gpointer user_data)
{
  gboolean active = FALSE, ret = FALSE;
  GEnumClass *eclass = (GEnumClass *) user_data;
  GType etype = G_ENUM_CLASS_TYPE (eclass);
  GEnumValue *val;
  gint set_value = -1;

  /* Get value using menu item name */
  val =
      g_enum_get_value_by_nick (eclass,
      gtk_widget_get_name (GTK_WIDGET (menuitem)));

  g_object_get (G_OBJECT (menuitem), "active", &active, NULL);
  if (active) {
    if (etype == GST_TYPE_WHITE_BALANCE_MODE) {
      GstWhiteBalanceMode mode;
      ret =
          gst_photography_set_white_balance_mode (GST_PHOTOGRAPHY
          (gst_videosrc), val->value);
      gst_photography_get_white_balance_mode (GST_PHOTOGRAPHY (gst_videosrc),
          &mode);
      set_value = (gint) mode;
    } else if (etype == GST_TYPE_SCENE_MODE) {
      GstSceneMode mode;
      ret =
          gst_photography_set_scene_mode (GST_PHOTOGRAPHY (gst_videosrc),
          val->value);
      gst_photography_get_scene_mode (GST_PHOTOGRAPHY (gst_videosrc), &mode);
      set_value = (gint) mode;
    } else if (etype == GST_TYPE_COLOUR_TONE_MODE) {
      GstColourToneMode mode;
      ret =
          gst_photography_set_colour_tone_mode (GST_PHOTOGRAPHY
          (gst_videosrc), val->value);
      gst_photography_get_colour_tone_mode (GST_PHOTOGRAPHY (gst_videosrc),
          &mode);
      set_value = (gint) mode;
    } else if (etype == GST_TYPE_FLASH_MODE) {
      GstFlashMode mode;
      ret =
          gst_photography_set_flash_mode (GST_PHOTOGRAPHY (gst_videosrc),
          val->value);
      gst_photography_get_flash_mode (GST_PHOTOGRAPHY (gst_videosrc), &mode);
      set_value = (gint) mode;
    }

    if (!ret) {
      g_print ("%s setting failed\n", val->value_name);
    } else if (val->value != set_value) {
      g_print ("%s setting failed, got %d\n", val->value_nick, set_value);
    }
  }
}

void
photo_iso_speed_toggled_cb (GtkRadioMenuItem * menuitem, gpointer user_data)
{
  gboolean active;
  const gchar *name;
  guint val = 0, set_val = G_MAXUINT;

  g_object_get (G_OBJECT (menuitem), "active", &active, NULL);
  if (active) {
    name = gtk_widget_get_name (GTK_WIDGET (menuitem));
    /* iso auto setting = 0 */
    /* FIXME: check what values other than 0 can be set */
    if (!g_str_equal (name, "auto")) {
      sscanf (name, "%d", &val);
    }
    if (!gst_photography_set_iso_speed (GST_PHOTOGRAPHY (gst_videosrc), val)) {
      g_print ("ISO speed (%d) setting failed\n", val);
    } else {
      gst_photography_get_iso_speed (GST_PHOTOGRAPHY (gst_videosrc), &set_val);
      if (val != set_val) {
        g_print ("ISO speed (%d) setting failed, got %d\n", val, set_val);
      }
    }
  }
}

void
photo_ev_comp_toggled_cb (GtkRadioMenuItem * menuitem, gpointer user_data)
{
  gboolean active;
  const gchar *name;
  gfloat val = 0.0, set_val = G_MAXFLOAT;

  g_object_get (G_OBJECT (menuitem), "active", &active, NULL);
  if (active) {
    name = gtk_widget_get_name (GTK_WIDGET (menuitem));
    sscanf (name, "%f", &val);
    if (!gst_photography_set_ev_compensation (GST_PHOTOGRAPHY (gst_videosrc),
            val)) {
      g_print ("EV compensation (%.1f) setting failed\n", val);
    } else {
      gst_photography_get_ev_compensation (GST_PHOTOGRAPHY (gst_videosrc),
          &set_val);
      if (val != set_val) {
        g_print ("EV compensation (%.1f) setting failed, got %.1f\n", val,
            set_val);
      }
    }
  }
}

static void
photo_add_submenu_from_enum (GtkMenuItem * parent_item, GType enum_type)
{
  GTypeClass *tclass;
  GEnumClass *eclass;
  GtkWidget *new_item = NULL, *new_submenu = NULL;
  guint i;
  GEnumValue *val;
  GSList *group = NULL;

  g_return_if_fail (parent_item && enum_type && G_TYPE_IS_CLASSED (enum_type));

  tclass = g_type_class_ref (enum_type);
  eclass = G_ENUM_CLASS (tclass);
  new_submenu = gtk_menu_new ();

  for (i = 0; i < eclass->n_values; i++) {
    val = g_enum_get_value (eclass, i);
    new_item = gtk_radio_menu_item_new_with_label (group, val->value_nick);
    /* Store enum nick as the menu item name */
    gtk_widget_set_name (new_item, val->value_nick);
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (new_item));
    g_signal_connect (new_item, "toggled",
        (GCallback) photo_menuitem_toggled_cb, eclass);
    gtk_menu_shell_append (GTK_MENU_SHELL (new_submenu), new_item);
    gtk_widget_show (new_item);
  }

  gtk_menu_item_set_submenu (parent_item, new_submenu);
  g_type_class_unref (tclass);
}

static void
add_submenu_from_list (GtkMenuItem * parent_item, GList * labels,
    GCallback toggled_cb)
{
  GtkWidget *new_item = NULL, *new_submenu = NULL;
  GSList *group = NULL;
  GList *l;

  new_submenu = gtk_menu_new ();

  for (l = labels; l != NULL; l = g_list_next (l)) {
    const gchar *label = l->data;
    new_item = gtk_radio_menu_item_new_with_label (group, label);
    if (g_str_equal (label, "0")) {
      /* Let's set zero as default */
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (new_item), TRUE);
    }
    gtk_widget_set_name (new_item, label);
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (new_item));
    g_signal_connect (new_item, "toggled", toggled_cb, NULL);
    gtk_menu_shell_append (GTK_MENU_SHELL (new_submenu), new_item);
    gtk_widget_show (new_item);
  }

  gtk_menu_item_set_submenu (parent_item, new_submenu);
}

static GtkMenuItem *
add_menuitem (GtkMenu * parent_menu, const gchar * item_name)
{
  GtkWidget *new_item;

  new_item = gtk_menu_item_new_with_label (item_name);
  gtk_menu_shell_append (GTK_MENU_SHELL (parent_menu), new_item);
  gtk_widget_show (new_item);

  return GTK_MENU_ITEM (new_item);
}

GList *
create_iso_speed_labels (void)
{
  GList *labels = NULL;
  gint i;
  for (i = 0; i < G_N_ELEMENTS (iso_speed_labels); i++) {
    labels = g_list_append (labels, iso_speed_labels[i]);
  }
  return labels;
}

GList *
create_ev_comp_labels (void)
{
  GList *labels = NULL;
  gdouble comp;
  char buf[G_ASCII_DTOSTR_BUF_SIZE];

  for (comp = EV_COMP_MIN; comp <= EV_COMP_MAX; comp += EV_COMP_STEP) {
    g_ascii_dtostr (buf, sizeof (buf), comp);
    labels = g_list_append (labels, g_strdup (buf));
  }
  return labels;
}

static void
fill_photography_menu (GtkMenuItem * parent_item)
{
  GtkWidget *photo_menu = gtk_menu_new ();
  GtkMenuItem *item = NULL;
  GList *labels = NULL;

  /* Add menu items and create and associate submenus to each item */
  item = add_menuitem (GTK_MENU (photo_menu), "AWB");
  photo_add_submenu_from_enum (item, GST_TYPE_WHITE_BALANCE_MODE);

  item = add_menuitem (GTK_MENU (photo_menu), "Colour Tone");
  photo_add_submenu_from_enum (item, GST_TYPE_COLOUR_TONE_MODE);

  item = add_menuitem (GTK_MENU (photo_menu), "Scene");
  photo_add_submenu_from_enum (item, GST_TYPE_SCENE_MODE);

  item = add_menuitem (GTK_MENU (photo_menu), "Flash");
  photo_add_submenu_from_enum (item, GST_TYPE_FLASH_MODE);

  item = add_menuitem (GTK_MENU (photo_menu), "ISO");
  labels = create_iso_speed_labels ();
  add_submenu_from_list (item, labels, (GCallback) photo_iso_speed_toggled_cb);
  g_list_free (labels);

  item = add_menuitem (GTK_MENU (photo_menu), "EV comp");
  labels = create_ev_comp_labels ();
  add_submenu_from_list (item, labels, (GCallback) photo_ev_comp_toggled_cb);
  g_list_free (labels);

  gtk_menu_item_set_submenu (parent_item, photo_menu);
}

void
capture_image_res_toggled_cb (GtkRadioMenuItem * menuitem, gpointer user_data)
{
  gboolean active;
  const gchar *label;
  gint i;

  g_object_get (G_OBJECT (menuitem), "active", &active, NULL);
  if (active) {
    label = gtk_widget_get_name (GTK_WIDGET (menuitem));
    /* Look for width and height corresponding to the label */
    for (i = 0; i < G_N_ELEMENTS (image_resolution_label_map); i++) {
      if (g_str_equal (label, image_resolution_label_map[i].label)) {
        /* set found values */
        g_signal_emit_by_name (gst_camera_bin, "set-image-resolution",
            image_resolution_label_map[i].width,
            image_resolution_label_map[i].height, 0);
        break;
      }
    }
  }
}

GList *
create_image_resolution_labels (void)
{
  GList *labels = NULL;
  int i;
  for (i = 0; i < G_N_ELEMENTS (image_resolution_label_map); i++) {
    labels = g_list_append (labels, image_resolution_label_map[i].label);
  }
  return labels;
}

static void
fill_capture_menu (GtkMenuItem * parent_item)
{
  GtkWidget *capture_menu = gtk_menu_new ();
  GtkMenuItem *item = NULL;
  GList *labels = NULL;

  /* Add menu items and create and associate submenus to each item */
  item = add_menuitem (GTK_MENU (capture_menu), "Image resolution");

  labels = create_image_resolution_labels ();
  add_submenu_from_list (item, labels,
      (GCallback) capture_image_res_toggled_cb);
  g_list_free (labels);

  gtk_menu_item_set_submenu (parent_item, capture_menu);
}
#endif /* HAVE_GST_PHOTO_IFACE_H */

static gboolean
ui_create (void)
{
  GError *error = NULL;

  builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder, UI_FILE, &error)) {
    g_warning ("Couldn't load builder file: %s", error->message);
    g_error_free (error);
    goto done;
  }

  ui_main_window = GTK_WIDGET (gtk_builder_get_object (builder, "windowMain"));
  ui_drawing = GTK_WIDGET (gtk_builder_get_object (builder, "drawingareaView"));
  ui_drawing_frame =
      GTK_WIDGET (gtk_builder_get_object (builder, "drawingareaFrame"));
  ui_chk_continous =
      GTK_WIDGET (gtk_builder_get_object (builder, "chkbntContinous"));
  ui_chk_rawmsg = GTK_WIDGET (gtk_builder_get_object (builder, "chkbtnRawMsg"));
  ui_bnt_shot = GTK_BUTTON (gtk_builder_get_object (builder, "buttonShot"));
  ui_bnt_pause = GTK_BUTTON (gtk_builder_get_object (builder, "buttonPause"));
  ui_cbbox_resolution =
      GTK_COMBO_BOX (gtk_builder_get_object (builder, "comboboxResolution"));
  ui_chk_mute = GTK_WIDGET (gtk_builder_get_object (builder, "chkbntMute"));
  ui_vbox_color_controls =
      GTK_WIDGET (gtk_builder_get_object (builder, "vboxColorControls"));
  ui_rdbntImageCapture =
      GTK_WIDGET (gtk_builder_get_object (builder, "radiobuttonImageCapture"));
  ui_rdbntVideoCapture =
      GTK_WIDGET (gtk_builder_get_object (builder, "radiobuttonVideoCapture"));
  ui_menuitem_photography =
      GTK_WIDGET (gtk_builder_get_object (builder, "menuitemPhotography"));
  ui_menuitem_capture =
      GTK_WIDGET (gtk_builder_get_object (builder, "menuitemCapture"));

#ifdef HAVE_GST_PHOTO_IFACE_H
  if (ui_menuitem_photography) {
    fill_photography_menu (GTK_MENU_ITEM (ui_menuitem_photography));
  }

  if (ui_menuitem_capture) {
    fill_capture_menu (GTK_MENU_ITEM (ui_menuitem_capture));
  }
#endif
  if (!(ui_main_window && ui_drawing && ui_chk_continous && ui_bnt_shot &&
          ui_bnt_pause && ui_cbbox_resolution && ui_chk_mute &&
          ui_vbox_color_controls && ui_rdbntImageCapture &&
          ui_rdbntVideoCapture && ui_chk_rawmsg && ui_menuitem_photography &&
          ui_menuitem_capture)) {
    fprintf (stderr, "Some widgets couldn't be created\n");
    fflush (stderr);
    goto done;
  }

  gtk_widget_set_double_buffered (ui_drawing, FALSE);
  ui_connect_signals ();
  gtk_widget_show_all (ui_main_window);
  capture_mode_config_gui ();
  return TRUE;
done:
  return FALSE;
}

/*
 * main
 */

int
main (int argc, char *argv[])
{
  int ret = 0;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  filename = g_string_new_len ("", 16);

  /* create UI */
  if (!ui_create ()) {
    ret = -1;
    goto done;
  }
  /* create pipeline and run */
  g_idle_add (me_gst_setup_default_pipeline, NULL);
  gtk_main ();

done:
  me_gst_cleanup_element ();
  g_string_free (filename, TRUE);
  return ret;
}
