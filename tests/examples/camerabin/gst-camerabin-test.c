/*
 * GStreamer
 * Copyright (C) 2010 Nokia Corporation <multimedia@maemo.org>
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
    Examples:
    ./gst-camerabin-test --image-width=2048 --image-height=1536 --image-enc=dspjpegenc
    ./gst-camerabin-test --mode=1 --capture-time=10 --image-width=848 --image-height=480 --view-framerate-num=2825 \
    --view-framerate-den=100 --audio-src=pulsesrc --audio-enc=nokiaaacenc --video-enc=dspmp4venc \
    --video-mux=mp4mux --src-colorspace=UYVY

    gst-camerabin-test --help
    Usage:
    gst-camerabin-test [OPTION...]

    camerabin command line test application.

    Help Options:
    -h, --help                        Show help options
    --help-all                        Show all help options
    --help-gst                        Show GStreamer Options

    Application Options:
    --ev-compensation                 EV compensation (-2.5..2.5, default = 0)
    --aperture                        Aperture (size of lens opening, default = 0 (auto))
    --flash-mode                      Flash mode (default = 0 (auto))
    --scene-mode                      Scene mode (default = 6 (auto))
    --exposure                        Exposure (default = 0 (auto))
    --iso-speed                       ISO speed (default = 0 (auto))
    --white-balance-mode              White balance mode (default = 0 (auto))
    --colour-tone-mode                Colour tone mode (default = 0 (auto))
    --directory                       Directory for capture file(s) (default is current directory)
    --mode                            Capture mode (default = 0 (image), 1 = video)
    --capture-time                    Time to capture video in seconds (default = 10)
    --capture-total                   Total number of captures to be done (default = 1)
    --flags                           Flags for camerabin, (default = 0x9)
    --mute                            Mute audio
    --zoom                            Zoom (100 = 1x (default), 200 = 2x etc.)
    --audio-src                       Audio source used in video recording
    --audio-bitrate                   Audio bitrate (default 128000)
    --audio-samplerate                Audio samplerate (default 48000)
    --audio-channels                  Audio channels (default 1)
    --video-src                       Video source used in still capture and video recording
    --audio-enc                       Audio encoder used in video recording
    --video-enc                       Video encoder used in video recording
    --image-enc                       Image encoder used in still capture
    --image-pp                        Image post-processing element
    --image-formatter                 Image metadata formatter element
    --video-mux                       Muxer used in video recording
    --viewfinder-sink                 Viewfinder sink (default = fakesink)
    --image-width                     Width for image capture
    --image-height                    Height for image capture
    --view-framerate-num              Framerate numerator for viewfinder
    --view-framerate-den              Framerate denominator for viewfinder
    --src-colorspace                  Colorspace format for video source (e.g. YUY2, UYVY)
    --src-format                      Video format for video source
    --preview-caps                    Preview caps (e.g. video/x-raw-rgb,width=320,height=240)
    --video-source-filter             Video filter to process all frames from video source
    --viewfinder-filter               Filter to process all frames going to viewfinder sink
    --x-width                         X window width (default = 320)
    --x-height                        X window height (default = 240)
    --no-xwindow                      Do not create XWindow

  */

/*
 * Includes
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define GST_USE_UNSTABLE_API 1

#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/interfaces/photography.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
/*
 * debug logging
 */
GST_DEBUG_CATEGORY_STATIC (camerabin_test);
#define GST_CAT_DEFAULT camerabin_test
typedef struct _ResultType
{
  GstClockTime avg;
  GstClockTime min;
  GstClockTime max;
  guint32 times;
} ResultType;

/*
 * Global vars
 */
static GstElement *camera_bin = NULL;
static GMainLoop *loop = NULL;

/* commandline options */
static gchar *audiosrc_name = NULL;
static gchar *videosrc_name = NULL;
static gchar *audioenc_name = NULL;
static gchar *videoenc_name = NULL;
static gchar *imageenc_name = NULL;
static gchar *imagepp_name = NULL;
static gchar *imageformatter_name = NULL;
static gchar *videomux_name = NULL;
static gchar *vfsink_name = NULL;
static gchar *src_csp = NULL;
static gchar *src_format = NULL;
static gint image_width = 1280;
static gint image_height = 720;
static gint view_framerate_num = 2825;
static gint view_framerate_den = 100;
static gboolean no_xwindow = FALSE;

static gint mode = 1;
static gint flags = 0x4f;
static gboolean mute = FALSE;
static gint zoom = 100;

static gint capture_time = 10;
static gint capture_count = 0;
static gint capture_total = 1;

/* photography interface command line options */
#define EV_COMPENSATION_NONE -G_MAXFLOAT
#define APERTURE_NONE -G_MAXINT
#define FLASH_MODE_NONE -G_MAXINT
#define SCENE_MODE_NONE -G_MAXINT
#define EXPOSURE_NONE -G_MAXINT64
#define ISO_SPEED_NONE -G_MAXINT
#define WHITE_BALANCE_MODE_NONE -G_MAXINT
#define COLOR_TONE_MODE_NONE -G_MAXINT
static gfloat ev_compensation = EV_COMPENSATION_NONE;
static gint aperture = APERTURE_NONE;
static gint flash_mode = FLASH_MODE_NONE;
static gint scene_mode = SCENE_MODE_NONE;
static gint64 exposure = EXPOSURE_NONE;
static gint iso_speed = ISO_SPEED_NONE;
static gint wb_mode = WHITE_BALANCE_MODE_NONE;
static gint color_mode = COLOR_TONE_MODE_NONE;

/* audio capsfilter options */
static gint audio_bitrate = 128000;
static gint audio_samplerate = 48000;
static gint audio_channels = 1;

static gchar *video_src_filter = NULL;
static gchar *viewfinder_filter = NULL;

static int x_width = 320;
static int x_height = 240;

/* test configuration for common callbacks */
static GString *filename = NULL;

static gchar *preview_caps_name = NULL;

/* X window variables */
static Display *display = NULL;
static Window window = 0;

GTimer *timer = NULL;

/*
 * Prototypes
 */
static gboolean run_pipeline (gpointer user_data);
static void set_metadata (GstElement * camera);

static void
create_host_window (void)
{
  unsigned long valuemask;
  XSetWindowAttributes attributes;

  display = XOpenDisplay (NULL);
  if (display) {
    window =
        XCreateSimpleWindow (display, DefaultRootWindow (display), 0, 0,
        x_width, x_height, 0, 0, 0);
    if (window) {
      valuemask = CWOverrideRedirect;
      attributes.override_redirect = True;
      XChangeWindowAttributes (display, window, valuemask, &attributes);
      XSetWindowBackgroundPixmap (display, window, None);
      XMapRaised (display, window);
      XSync (display, FALSE);
    } else {
      GST_DEBUG ("could not create X window!");
    }
  } else {
    GST_DEBUG ("could not open display!");
  }
}

static gboolean
img_capture_done (GstElement * camera, const gchar * fname, gpointer user_data)
{
  gboolean ret = FALSE;

  GST_DEBUG ("image done: %s", fname);
  if (capture_count < capture_total) {
    g_idle_add ((GSourceFunc) run_pipeline, NULL);
  } else {
    g_main_loop_quit (loop);
  }
  return ret;
}

static GstBusSyncReply
sync_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  const GstStructure *st;
  const GValue *image;
  GstBuffer *buf = NULL;
  guint8 *data_buf = NULL;
  gchar *caps_string;
  guint size = 0;
  gchar *preview_filename = NULL;
  FILE *f = NULL;
  size_t written;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT:{
      st = gst_message_get_structure (message);
      if (st) {
        if (gst_structure_has_name (message->structure, "prepare-xwindow-id")) {
          if (!no_xwindow && window) {
            gst_x_overlay_set_window_handle (GST_X_OVERLAY (GST_MESSAGE_SRC
                    (message)), window);
            gst_message_unref (message);
            message = NULL;
            return GST_BUS_DROP;
          }
        } else if (gst_structure_has_name (st, "image-captured")) {
          GST_DEBUG ("image-captured");
        } else if (gst_structure_has_name (st, "preview-image")) {
          GST_DEBUG ("preview-image");
          //extract preview-image from msg
          image = gst_structure_get_value (st, "buffer");
          if (image) {
            buf = gst_value_get_buffer (image);
            data_buf = GST_BUFFER_DATA (buf);
            size = GST_BUFFER_SIZE (buf);
            preview_filename = g_strdup_printf ("test_vga.rgb");
            caps_string = gst_caps_to_string (GST_BUFFER_CAPS (buf));
            g_print ("writing buffer to %s, elapsed: %.2fs, buffer caps: %s\n",
                preview_filename, g_timer_elapsed (timer, NULL), caps_string);
            g_free (caps_string);
            f = g_fopen (preview_filename, "w");
            if (f) {
              written = fwrite (data_buf, size, 1, f);
              if (!written) {
                g_print ("error writing file\n");
              }
              fclose (f);
            } else {
              g_print ("error opening file for raw image writing\n");
            }
            g_free (preview_filename);
          }
        }
      }
      break;
    }
    default:
      /* unhandled message */
      break;
  }
  return GST_BUS_PASS;
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      /* Write debug graph to file */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (camera_bin),
          GST_DEBUG_GRAPH_SHOW_ALL, "camerabin.error");

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_IS_BIN (GST_MESSAGE_SRC (message))) {
        GstState oldstate, newstate;

        gst_message_parse_state_changed (message, &oldstate, &newstate, NULL);
        GST_DEBUG_OBJECT (GST_MESSAGE_SRC (message), "state-changed: %s -> %s",
            gst_element_state_get_name (oldstate),
            gst_element_state_get_name (newstate));
      }
      break;
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      GST_INFO ("got eos() - should not happen");
      g_main_loop_quit (loop);
      break;
    default:
      /* unhandled message */
      break;
  }
  return TRUE;
}

/*
 * Helpers
 */

static void
cleanup_pipeline (void)
{
  if (camera_bin) {
    GST_INFO_OBJECT (camera_bin, "stopping and destroying");
    gst_element_set_state (camera_bin, GST_STATE_NULL);
    gst_object_unref (camera_bin);
    camera_bin = NULL;
  }
}

static gboolean
setup_pipeline_element (const gchar * property_name, const gchar * element_name,
    GstElement ** res_elem)
{
  gboolean res = TRUE;
  GstElement *elem = NULL;

  if (element_name) {
    elem = gst_element_factory_make (element_name, NULL);
    if (elem) {
      g_object_set (camera_bin, property_name, elem, NULL);
    } else {
      GST_WARNING ("can't create element '%s' for property '%s'", element_name,
          property_name);
      res = FALSE;
    }
  } else {
    GST_DEBUG ("no element for property '%s' given", property_name);
  }
  if (res_elem)
    *res_elem = elem;
  return res;
}

static GstElement *
create_audioencoder_bin (void)
{
  GstElement *bin, *aenc, *filter;
  GstPad *pad;
  GstCaps *audio_caps;

  bin = gst_bin_new ("aebin");
  filter = gst_element_factory_make ("capsfilter", "aefilter");
  aenc = gst_element_factory_make (audioenc_name, "aenc");

  if (!g_ascii_strcasecmp (audioenc_name, "pulsesrc")) {
    g_object_set (G_OBJECT (aenc),
        "bitrate", audio_bitrate, "profile", 2, NULL);
  }

  audio_caps = gst_caps_new_simple ("audio/x-raw-int",
      "channels", G_TYPE_INT, audio_channels,
      "rate", G_TYPE_INT, audio_samplerate, NULL);

  if (!audio_caps) {
    g_warning ("error generating caps");
  }

  g_object_set (G_OBJECT (filter), "caps", audio_caps, NULL);

  gst_caps_unref (audio_caps);

  gst_bin_add_many (GST_BIN (bin), filter, aenc, NULL);
  gst_element_link (filter, aenc);

  pad = gst_element_get_static_pad (filter, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  pad = gst_element_get_static_pad (aenc, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));

  return bin;
}

static GstElement *
create_ipp_bin (void)
{
  GstElement *bin = NULL, *element = NULL;
  GstPad *pad = NULL;
  gchar **elements;
  GList *element_list = NULL, *current = NULL, *next = NULL;
  int i;

  bin = gst_bin_new ("ippbin");

  elements = g_strsplit (imagepp_name, ",", 0);

  for (i = 0; elements[i] != NULL; i++) {
    element = gst_element_factory_make (elements[i], NULL);
    if (element) {
      element_list = g_list_append (element_list, element);
      gst_bin_add (GST_BIN (bin), element);
    } else
      GST_WARNING ("Could create element %s for ippbin", elements[i]);
  }

  for (i = 1; i < g_list_length (element_list); i++) {
    current = g_list_nth (element_list, i - 1);
    next = g_list_nth (element_list, i);
    gst_element_link (current->data, next->data);
  }

  current = g_list_first (element_list);
  pad = gst_element_get_static_pad (current->data, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  current = g_list_last (element_list);
  pad = gst_element_get_static_pad (current->data, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));

  g_list_free (element_list);
  g_strfreev (elements);

  return bin;
}

static gboolean
setup_pipeline (void)
{
  GstBus *bus;
  gboolean res = TRUE;
  GstElement *vmux = NULL, *ienc = NULL, *sink = NULL, *aenc = NULL, *ipp =
      NULL;
  GstCaps *filter_caps = NULL;

  camera_bin = gst_element_factory_make ("camerabin", NULL);
  if (NULL == camera_bin) {
    g_warning ("can't create camerabin element\n");
    goto error;
  }
  g_object_set (camera_bin, "flags", flags, NULL);

  g_signal_connect (camera_bin, "image-done", (GCallback) img_capture_done,
      NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (camera_bin));
  /* Add sync handler for time critical messages that need to be handled fast */
  gst_bus_set_sync_handler (bus, sync_bus_callback, NULL);
  /* Handle normal messages asynchronously */
  gst_bus_add_watch (bus, bus_callback, NULL);
  gst_object_unref (bus);

  GST_INFO_OBJECT (camera_bin, "camerabin created");

  /* configure used elements */
  res &= setup_pipeline_element ("viewfinder-sink", vfsink_name, &sink);
  res &= setup_pipeline_element ("audio-source", audiosrc_name, NULL);
  res &= setup_pipeline_element ("video-source", videosrc_name, NULL);
  res &= setup_pipeline_element ("video-source-filter", video_src_filter, NULL);
  res &= setup_pipeline_element ("viewfinder-filter", viewfinder_filter, NULL);

  if (audioenc_name) {
    aenc = create_audioencoder_bin ();
    if (aenc)
      g_object_set (camera_bin, "audio-encoder", aenc, NULL);
    else
      GST_WARNING ("Could not make audio encoder element");
  }

  if (imagepp_name) {
    ipp = create_ipp_bin ();
    if (ipp)
      g_object_set (camera_bin, "image-post-processing", ipp, NULL);
    else
      GST_WARNING ("Could not create ipp elements");
  }

  res &= setup_pipeline_element ("video-encoder", videoenc_name, NULL);
  res &= setup_pipeline_element ("image-encoder", imageenc_name, &ienc);
  res &= setup_pipeline_element ("image-formatter", imageformatter_name, NULL);
  res &= setup_pipeline_element ("video-muxer", videomux_name, &vmux);
  if (!res) {
    goto error;
  }
  GST_INFO_OBJECT (camera_bin, "elements created");

  /* set properties */
  if (src_format) {
    filter_caps = gst_caps_from_string (src_format);
  } else if (src_csp && strlen (src_csp) == 4) {
    /* Set requested colorspace format, this is needed if the default 
       colorspace negotiated for viewfinder doesn't match with e.g. encoders. */
    filter_caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC,
        GST_MAKE_FOURCC (src_csp[0], src_csp[1], src_csp[2], src_csp[3]), NULL);
  }

  if (filter_caps) {
    g_object_set (camera_bin, "filter-caps", filter_caps, NULL);
    gst_caps_unref (filter_caps);
  }

  g_object_set (sink, "sync", TRUE, NULL);

  GST_INFO_OBJECT (camera_bin, "elements configured");

  /* configure a resolution and framerate */
  if (mode == 1) {
    g_signal_emit_by_name (camera_bin, "set-video-resolution-fps", image_width,
        image_height, view_framerate_num, view_framerate_den, NULL);
  } else {
    g_signal_emit_by_name (camera_bin, "set-image-resolution", image_width,
        image_height, NULL);
  }

  if (GST_STATE_CHANGE_FAILURE ==
      gst_element_set_state (camera_bin, GST_STATE_READY)) {
    g_warning ("can't set camerabin to ready\n");
    goto error;
  }
  GST_INFO_OBJECT (camera_bin, "camera ready");

  if (GST_STATE_CHANGE_FAILURE ==
      gst_element_set_state (camera_bin, GST_STATE_PLAYING)) {
    g_warning ("can't set camerabin to playing\n");
    goto error;
  }

  GST_INFO_OBJECT (camera_bin, "camera started");
  return TRUE;
error:
  cleanup_pipeline ();
  return FALSE;
}

static gboolean
stop_capture (gpointer user_data)
{
  g_signal_emit_by_name (camera_bin, "capture-stop", 0);
  if (capture_count < capture_total) {
    g_idle_add ((GSourceFunc) run_pipeline, NULL);
  } else {
    g_main_loop_quit (loop);
  }
  return FALSE;
}

static void
set_metadata (GstElement * camera)
{
  GstTagSetter *setter = GST_TAG_SETTER (camera);
  GTimeVal time = { 0, 0 };
  gchar *desc_str;
  GDate *date = g_date_new ();

  g_get_current_time (&time);
  g_date_set_time_val (date, &time);

  desc_str = g_strdup_printf ("captured by %s", g_get_real_name ());

  gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
      GST_TAG_DATE, date,
      GST_TAG_DESCRIPTION, desc_str,
      GST_TAG_TITLE, "gst-camerabin-test capture",
      GST_TAG_GEO_LOCATION_LONGITUDE, 1.0,
      GST_TAG_GEO_LOCATION_LATITUDE, 2.0,
      GST_TAG_GEO_LOCATION_ELEVATION, 3.0,
      GST_TAG_DEVICE_MANUFACTURER, "gst-camerabin-test manufacturer",
      GST_TAG_DEVICE_MODEL, "gst-camerabin-test model", NULL);

  g_free (desc_str);
  g_date_free (date);
}

static gboolean
run_pipeline (gpointer user_data)
{
  GstCaps *preview_caps = NULL;
  gchar *filename_str = NULL;
  GstElement *video_source = NULL;
  const gchar *filename_suffix;

  g_object_set (camera_bin, "mode", mode, NULL);

  if (preview_caps_name != NULL) {
    preview_caps = gst_caps_from_string (preview_caps_name);
    if (preview_caps) {
      g_object_set (camera_bin, "preview-caps", preview_caps, NULL);
      GST_DEBUG ("Preview caps set");
    } else
      GST_DEBUG ("Preview caps set but could not create caps from string");
  }

  set_metadata (camera_bin);

  /* Construct filename */
  if (mode == 1)
    filename_suffix = ".mp4";
  else
    filename_suffix = ".jpg";
  filename_str =
      g_strdup_printf ("%s/test_%04u%s", filename->str, capture_count,
      filename_suffix);
  GST_DEBUG ("Setting filename: %s", filename_str);
  g_object_set (camera_bin, "filename", filename_str, NULL);
  g_free (filename_str);

  g_object_get (camera_bin, "video-source", &video_source, NULL);
  if (video_source) {
    if (GST_IS_ELEMENT (video_source) &&
        gst_element_implements_interface (video_source, GST_TYPE_PHOTOGRAPHY)) {
      /* Set GstPhotography interface options. If option not given as
         command-line parameter use default of the source element. */
      if (scene_mode != SCENE_MODE_NONE)
        g_object_set (video_source, "scene-mode", scene_mode, NULL);
      if (ev_compensation != EV_COMPENSATION_NONE)
        g_object_set (video_source, "ev-compensation", ev_compensation, NULL);
      if (aperture != APERTURE_NONE)
        g_object_set (video_source, "aperture", aperture, NULL);
      if (flash_mode != FLASH_MODE_NONE)
        g_object_set (video_source, "flash-mode", flash_mode, NULL);
      if (exposure != EXPOSURE_NONE)
        g_object_set (video_source, "exposure", exposure, NULL);
      if (iso_speed != ISO_SPEED_NONE)
        g_object_set (video_source, "iso-speed", iso_speed, NULL);
      if (wb_mode != WHITE_BALANCE_MODE_NONE)
        g_object_set (video_source, "white-balance-mode", wb_mode, NULL);
      if (color_mode != COLOR_TONE_MODE_NONE)
        g_object_set (video_source, "colour-tone-mode", color_mode, NULL);
    }
    g_object_unref (video_source);
  }
  g_object_set (camera_bin, "mute", mute, NULL);
  g_object_set (camera_bin, "zoom", zoom / 100.0f, NULL);

  capture_count++;
  g_timer_start (timer);
  g_signal_emit_by_name (camera_bin, "capture-start", 0);


  if (mode == 1) {
    g_timeout_add ((capture_time * 1000), (GSourceFunc) stop_capture, NULL);
  }

  return FALSE;
}

int
main (int argc, char *argv[])
{
  gchar *target_times = NULL;
  gchar *ev_option = NULL;
  gchar *fn_option = NULL;

  GOptionEntry options[] = {
    {"ev-compensation", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_STRING,
          &ev_option,
        "EV compensation for source element GstPhotography interface", NULL},
    {"aperture", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT, &aperture,
          "Aperture (size of lens opening) for source element GstPhotography interface",
        NULL},
    {"flash-mode", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT,
          &flash_mode,
        "Flash mode for source element GstPhotography interface", NULL},
    {"scene-mode", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT,
          &scene_mode,
        "Scene mode for source element GstPhotography interface", NULL},
    {"exposure", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT64,
          &exposure,
          "Exposure time (in ms) for source element GstPhotography interface",
        NULL},
    {"iso-speed", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT,
          &iso_speed,
        "ISO speed for source element GstPhotography interface", NULL},
    {"white-balance-mode", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT,
          &wb_mode,
        "White balance mode for source element GstPhotography interface", NULL},
    {"colour-tone-mode", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT,
          &color_mode,
        "Colour tone mode for source element GstPhotography interface", NULL},
    {"directory", '\0', 0, G_OPTION_ARG_STRING, &fn_option,
        "Directory for capture file(s) (default is current directory)", NULL},
    {"mode", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT, &mode,
        "Capture mode (default = 0 (image), 1 = video)", NULL},
    {"capture-time", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT,
          &capture_time,
        "Time to capture video in seconds (default = 10)", NULL},
    {"capture-total", '\0', 0, G_OPTION_ARG_INT, &capture_total,
        "Total number of captures to be done (default = 1)", NULL},
    {"flags", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT, &flags,
        "Flags for camerabin, (default = 0x9)", NULL},
    {"mute", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_NONE, &mute,
        "Mute audio", NULL},
    {"zoom", '\0', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_INT, &zoom,
        "Zoom (100 = 1x (default), 200 = 2x etc.)", NULL},
    {"audio-src", '\0', 0, G_OPTION_ARG_STRING, &audiosrc_name,
        "Audio source used in video recording", NULL},
    {"audio-bitrate", '\0', 0, G_OPTION_ARG_INT, &audio_bitrate,
        "Audio bitrate (default 128000)", NULL},
    {"audio-samplerate", '\0', 0, G_OPTION_ARG_INT, &audio_samplerate,
        "Audio samplerate (default 48000)", NULL},
    {"audio-channels", '\0', 0, G_OPTION_ARG_INT, &audio_channels,
        "Audio channels (default 1)", NULL},
    {"video-src", '\0', 0, G_OPTION_ARG_STRING, &videosrc_name,
        "Video source used in still capture and video recording", NULL},
    {"audio-enc", '\0', 0, G_OPTION_ARG_STRING, &audioenc_name,
        "Audio encoder used in video recording", NULL},
    {"video-enc", '\0', 0, G_OPTION_ARG_STRING, &videoenc_name,
        "Video encoder used in video recording", NULL},
    {"image-enc", '\0', 0, G_OPTION_ARG_STRING, &imageenc_name,
        "Image encoder used in still capture", NULL},
    {"image-pp", '\0', 0, G_OPTION_ARG_STRING, &imagepp_name,
        "List of image post-processing elements separated with comma", NULL},
    {"image-formatter", '\0', 0, G_OPTION_ARG_STRING, &imageformatter_name,
        "Image metadata formatter used in still image capture", NULL},
    {"video-mux", '\0', 0, G_OPTION_ARG_STRING, &videomux_name,
        "Muxer used in video recording", NULL},
    {"viewfinder-sink", '\0', 0, G_OPTION_ARG_STRING, &vfsink_name,
        "Viewfinder sink (default = fakesink)", NULL},
    {"image-width", '\0', 0, G_OPTION_ARG_INT, &image_width,
        "Width for image capture", NULL},
    {"image-height", '\0', 0, G_OPTION_ARG_INT, &image_height,
        "Height for image capture", NULL},
    {"view-framerate-num", '\0', 0, G_OPTION_ARG_INT, &view_framerate_num,
        "Framerate numerator for viewfinder", NULL},
    {"view-framerate-den", '\0', 0, G_OPTION_ARG_INT, &view_framerate_den,
        "Framerate denominator for viewfinder", NULL},
    {"src-colorspace", '\0', 0, G_OPTION_ARG_STRING, &src_csp,
        "Colorspace format for video source (e.g. YUY2, UYVY)", NULL},
    {"src-format", '\0', 0, G_OPTION_ARG_STRING, &src_format,
        "Video format for video source", NULL},
    {"preview-caps", '\0', 0, G_OPTION_ARG_STRING, &preview_caps_name,
        "Preview caps (e.g. video/x-raw-rgb,width=320,height=240)", NULL},
    {"video-source-filter", '\0', 0, G_OPTION_ARG_STRING, &video_src_filter,
        "Video filter to process all frames from video source", NULL},
    {"viewfinder-filter", '\0', 0, G_OPTION_ARG_STRING, &viewfinder_filter,
        "Filter to process all frames going to viewfinder sink", NULL},
    {"x-width", '\0', 0, G_OPTION_ARG_INT, &x_width,
        "X window width (default = 320)", NULL},
    {"x-height", '\0', 0, G_OPTION_ARG_INT, &x_height,
        "X window height (default = 240)", NULL},
    {"no-xwindow", '\0', 0, G_OPTION_ARG_NONE, &no_xwindow,
        "Do not create XWindow", NULL},
    {NULL}
  };

  GOptionContext *ctx;
  GError *err = NULL;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("\n\ncamerabin command line test application.");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }
  g_option_context_free (ctx);

  /* if we fail to create xwindow should we care? */
  if (!no_xwindow)
    create_host_window ();

  GST_DEBUG_CATEGORY_INIT (camerabin_test, "camerabin-test", 0,
      "camerabin test");

  /* FIXME: error handling */
  if (ev_option != NULL)
    ev_compensation = strtod (ev_option, (char **) NULL);

  if (vfsink_name == NULL)
    vfsink_name = g_strdup ("fakesink");

  filename = g_string_new (fn_option);
  if (filename->len == 0)
    filename = g_string_append (filename, ".");

  timer = g_timer_new ();

  /* init */
  if (setup_pipeline ()) {
    loop = g_main_loop_new (NULL, FALSE);
    g_idle_add ((GSourceFunc) run_pipeline, NULL);
    g_main_loop_run (loop);
    cleanup_pipeline ();
    g_main_loop_unref (loop);
  }
  /* free */
  g_string_free (filename, TRUE);
  g_free (ev_option);
  g_free (audiosrc_name);
  g_free (videosrc_name);
  g_free (audioenc_name);
  g_free (videoenc_name);
  g_free (imageenc_name);
  g_free (imageformatter_name);
  g_free (imagepp_name);
  g_free (videomux_name);
  g_free (vfsink_name);
  g_free (src_csp);
  g_free (src_format);
  g_free (target_times);
  g_timer_destroy (timer);

  if (window)
    XDestroyWindow (display, window);

  if (display)
    XCloseDisplay (display);

  return 0;
}
