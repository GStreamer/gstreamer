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
 * This application runs various tests and messures how long it takes.
 * FIXME: It needs to figure sane defaults for different hardware or support
 * we could use GOption for specifying the parameters
 * The config should have:
 * - target times
 * - filter-caps
 * - preview-caps
 * - user-res-fps
 * - element-names: videoenc, audioenc, videomux, imageenc, videosrc, audiosrc
 * Most of it is interpreted in setup_pipeline()
 *
 * gcc `pkg-config --cflags --libs gstreamer-0.10` gst-camera-perf.c -ogst-camera-perf
 *
 * plain linux:
 * ./gst-camera-perf --src-colorspace=YUY2 --image-width=640 --image-height=480 --video-width=640 --video-height=480 --view-framerate-num=15 --view-framerate-den=1
 *
 * maemo:
 * ./gst-camera-perf --src-colorspace=UYVY --image-width=640 --image-height=480 --video-width=640 --video-height=480 --view-framerate-num=1491 --view-framerate-den=100 --video-src=v4l2camsrc --audio-enc=nokiaaacenc --video-enc=dspmpeg4enc --video-mux=hantromp4mux --image-enc=dspjpegenc --target-times=1000,1500,1500,2000,500,2000,3500,1000,1000
 * ./gst-camera-perf --src-colorspace=UYVY --image-width=2576 --image-height=1936 --video-width=640 --video-height=480 --view-framerate-num=2999 --view-framerate-den=100 --video-src=v4l2camsrc --audio-enc=nokiaaacenc --video-enc=dspmpeg4enc --video-mux=hantromp4mux
 * ./gst-camera-perf --src-colorspace=UYVY --image-width=2576 --image-height=1936 --video-width=640 --video-height=480 --view-framerate-num=126 --view-framerate-den=5 --video-src=v4l2camsrc --audio-enc=nokiaaacenc --video-enc=dspmpeg4enc --video-mux=hantromp4mux --image-enc=dspjpegenc
 */

/*
 * Includes
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* save the snapshot images
 * gcc `pkg-config --cflags --libs gstreamer-0.10 gdk-pixbuf-2.0` gst-camera-perf.c -ogst-camera-perf
 *
#define SAVE_SNAPSHOT 1
 **/
#include <gst/gst.h>
#ifdef SAVE_SNAPSHOT
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
/*
 * debug logging
 */
GST_DEBUG_CATEGORY_STATIC (camera_perf);
#define GST_CAT_DEFAULT camera_perf


/*
 * enums, typedefs and defines
 */

#define GET_TIME(t)                                     \
do {                                                    \
  t = gst_util_get_timestamp ();                        \
  GST_DEBUG("%2d ----------------------------------------", test_ix); \
} while(0)

#define DIFF_TIME(e,s,d) d=GST_CLOCK_DIFF(s,e)

#define CONT_SHOTS 10
#define TEST_CASES 9

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
static gchar *videomux_name = NULL;
static gchar *src_csp = NULL;
static gint image_width = 0;
static gint image_height = 0;
static gint video_width = 0;
static gint video_height = 0;
static gint view_framerate_num = 0;
static gint view_framerate_den = 0;

/* test configuration for common callbacks */
static GString *filename = NULL;
static guint32 num_pics = 0;
static guint32 num_pics_cont = 0;
//static guint32 num_vids = 0;
static guint test_ix = 0;
static gboolean signal_vf_sink = FALSE;
static gboolean signal_vid_sink = FALSE;
static gboolean signal_img_enc = FALSE;
static gboolean signal_shot = FALSE;
static gboolean signal_cont = FALSE;

static gboolean need_pad_probe = FALSE;
static gboolean need_ienc_pad_probe = FALSE;
static gboolean need_vmux_pad_probe = FALSE;

static gboolean have_img_captured = FALSE;
static gboolean have_img_done = FALSE;

/* time samples and test results */
static GstClockTime t_initial = G_GUINT64_CONSTANT (0);
static GstClockTime t_final[CONT_SHOTS] = { G_GUINT64_CONSTANT (0), };

static GstClockTime test_06_taget, test_09_taget;
static GstClockTimeDiff diff;
static ResultType result;

/* these can be overridden with commandline args --target-times */
static GstClockTime target[TEST_CASES] = {
  1000 * GST_MSECOND,
  1500 * GST_MSECOND,
  1500 * GST_MSECOND,
  2000 * GST_MSECOND,           /* this should be shorter, as we can take next picture before preview is ready */
  500 * GST_MSECOND,
  2000 * GST_MSECOND,
  3500 * GST_MSECOND,
  1000 * GST_MSECOND,
  1000 * GST_MSECOND
};

static const gchar *test_names[TEST_CASES] = {
  "Camera OFF to VF on",
  "(3A latency)",               /* time to get AF? */
  "Shot to snapshot",
  "Shot to shot",
  "Serial shooting",
  "Shutter lag",
  "Image saved",
  "Mode change",
  "Video recording"
};

/*
 * Prototypes
 */

static void print_result (void);
static gboolean run_test (gpointer user_data);
static gboolean setup_add_pad_probe (GstElement * elem, const gchar * pad_name,
    GCallback handler, gpointer data);


/*
 * Callbacks
 */

static gboolean
pad_has_buffer (GstPad * pad, GstBuffer * buf, gpointer user_data)
{
  gboolean *signal_sink = (gboolean *) user_data;
  gboolean print_and_restart = FALSE;

  if (*signal_sink) {
    *signal_sink = FALSE;
    GET_TIME (t_final[0]);
    GST_DEBUG_OBJECT (pad, "%2d pad has buffer", test_ix);
    switch (test_ix) {
      case 5:                  // shutter lag
        DIFF_TIME (t_final[num_pics_cont], t_initial, diff);
        result.avg = result.min = result.max = diff;
        print_and_restart = TRUE;
        break;
      case 8:                  // video recording start
        DIFF_TIME (t_final[num_pics_cont], t_initial, diff);
        result.avg = result.min = result.max = diff;
        //g_signal_emit_by_name (camera_bin, "capture-stop", 0);
        print_and_restart = TRUE;
        break;
      default:
        GST_WARNING_OBJECT (pad, "%2d pad has buffer, not handled", test_ix);
        break;
    }
  }
  if (print_and_restart) {
    print_result ();
    g_idle_add ((GSourceFunc) run_test, NULL);
  }
  return TRUE;
}

static void
element_added (GstBin * bin, GstElement * element, gpointer user_data)
{
  GstElement *elem;

  if (GST_IS_BIN (element)) {
    g_signal_connect (element, "element-added", (GCallback) element_added,
        NULL);
  }

  if (need_vmux_pad_probe) {
    g_object_get (camera_bin, "video-muxer", &elem, NULL);
    if (elem) {
      need_vmux_pad_probe = FALSE;
      GST_INFO_OBJECT (elem, "got default video muxer");
      if (setup_add_pad_probe (elem, "src", (GCallback) pad_has_buffer,
              &signal_vid_sink)) {
        /* enable test */
        target[8] = test_09_taget;
      }
    }
  }
  if (need_ienc_pad_probe) {
    g_object_get (camera_bin, "image-encoder", &elem, NULL);
    if (elem) {
      need_ienc_pad_probe = FALSE;
      GST_INFO_OBJECT (elem, "got default image encoder");
      if (setup_add_pad_probe (elem, "src", (GCallback) pad_has_buffer,
              &signal_img_enc)) {
        /* enable test */
        target[5] = test_06_taget;
      }
    }
  }
}

static gboolean
img_capture_done (GstElement * camera, GString * fname, gpointer user_data)
{
  gboolean ret = FALSE;
  gboolean print_and_restart = FALSE;

  GST_DEBUG ("shot %d, cont %d, num %d", signal_shot, signal_cont,
      num_pics_cont);

  if (signal_shot) {
    GET_TIME (t_final[num_pics_cont]);
    signal_shot = FALSE;
    switch (test_ix) {
      case 6:
        DIFF_TIME (t_final[num_pics_cont], t_initial, diff);
        result.avg = result.min = result.max = diff;
        print_and_restart = TRUE;
        break;
    }
    GST_DEBUG ("%2d shot done", test_ix);
  }

  if (signal_cont) {
    gint i;

    if (num_pics_cont < CONT_SHOTS) {
      gchar tmp[6];

      GET_TIME (t_final[num_pics_cont]);
      num_pics_cont++;
      for (i = filename->len - 1; i > 0; --i) {
        if (filename->str[i] == '_')
          break;
      }
      snprintf (tmp, 6, "_%04d", num_pics_cont);
      memcpy (filename->str + i, tmp, 5);
      GST_DEBUG ("%2d cont new filename '%s'", test_ix, filename->str);
      g_object_set (camera_bin, "filename", filename->str, NULL);
      // FIXME: is burst capture broken? new filename and return TRUE should be enough
      // as a workaround we will kick next image from here
      // but this needs sync so that we have received "image-captured" message already
      if (have_img_captured) {
        have_img_captured = FALSE;
        g_signal_emit_by_name (camera_bin, "capture-start", NULL);
      } else {
        have_img_done = TRUE;
      }
      ret = TRUE;
    } else {
      GstClockTime max = 0;
      GstClockTime min = -1;
      GstClockTime total = 0;

      num_pics_cont = 0;
      signal_cont = FALSE;

      DIFF_TIME (t_final[0], t_initial, diff);
      max < diff ? max = diff : max;
      min > diff ? min = diff : min;
      total += diff;

      DIFF_TIME (t_final[1], t_final[0], diff);
      max < diff ? max = diff : max;
      min > diff ? min = diff : min;
      total += diff;

      for (i = 2; i < CONT_SHOTS; ++i) {
        DIFF_TIME (t_final[i], t_final[i - 1], diff);

        max < diff ? max = diff : max;
        min > diff ? min = diff : min;
        total += diff;
      }

      result.avg = total / CONT_SHOTS;
      result.min = min;
      result.max = max;
      print_and_restart = TRUE;
      GST_DEBUG ("%2d cont done", test_ix);
    }
  }

  switch (test_ix) {
    case 2:
    case 3:
      print_and_restart = TRUE;
      break;
  }

  if (print_and_restart) {
    print_result ();
    g_idle_add ((GSourceFunc) run_test, NULL);
    return FALSE;
  }
  return ret;
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  const GstStructure *st;

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
        if (GST_MESSAGE_SRC (message) == GST_OBJECT (camera_bin)) {
          if (GST_STATE_TRANSITION (oldstate,
                  newstate) == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {
            switch (test_ix) {
              case 0:          // camera on
                GET_TIME (t_final[0]);
                DIFF_TIME (t_final[0], t_initial, diff);

                result.avg = result.min = result.max = diff;
                print_result ();
                g_idle_add ((GSourceFunc) run_test, NULL);
                break;
            }
          }
        }
      }
      break;
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      GST_INFO ("got eos() - should not happen");
      g_main_loop_quit (loop);
      break;
    default:
      st = gst_message_get_structure (message);
      if (st) {
        if (gst_structure_has_name (st, "image-captured")) {
          GST_DEBUG ("%2d image-captured", test_ix);
          switch (test_ix) {
            case 3:
              GET_TIME (t_final[num_pics_cont]);
              DIFF_TIME (t_final[num_pics_cont], t_initial, diff);
              result.avg = result.min = result.max = diff;
              break;
            case 4:
              // we need to have this received before we can take next one
              if (have_img_done) {
                have_img_done = FALSE;
                g_signal_emit_by_name (camera_bin, "capture-start", NULL);
              } else {
                have_img_captured = TRUE;
              }
              break;
          }
        } else if (gst_structure_has_name (st, "preview-image")) {
          GST_DEBUG ("%2d preview-image", test_ix);
          switch (test_ix) {
            case 2:
              GET_TIME (t_final[num_pics_cont]);
              DIFF_TIME (t_final[num_pics_cont], t_initial, diff);
              result.avg = result.min = result.max = diff;
              /* turn off preview image generation again */
              g_object_set (camera_bin, "preview-caps", NULL, NULL);
              break;
          }
#ifdef SAVE_SNAPSHOT
          {
            const GValue *value = gst_structure_get_value (st, "buffer");
            GstBuffer *buf = gst_value_get_buffer (value);
            GstCaps *caps = GST_BUFFER_CAPS (buf);
            GstStructure *buf_st = gst_caps_get_structure (caps, 0);
            gint width, height, rowstride;
            GdkPixbuf *pixbuf;
            guchar *data;

            GST_INFO ("preview : buf=%p, size=%d, format=%" GST_PTR_FORMAT,
                buf, GST_BUFFER_SIZE (buf), caps);

            data = GST_BUFFER_DATA (buff);
            gst_structure_get_int (buf_st, "width", &width);
            gst_structure_get_int (buf_st, "height", &height);
            rowstride = GST_ROUND_UP_4 (width * 3);

            pixbuf = gdk_pixbuf_new_from_data (data, GDK_COLORSPACE_RGB, FALSE,
                8, width, height, rowstride, NULL, NULL);
            gdk_pixbuf_save (pixbuf, "/tmp/gst-camerabin-preview.png", "png",
                NULL, NULL);
            gdk_pixbuf_unref (pixbuf);
          }
#endif
        }
      }
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
setup_add_pad_probe (GstElement * elem, const gchar * pad_name,
    GCallback handler, gpointer data)
{
  GstPad *pad = NULL;

  if (!(pad = gst_element_get_static_pad (elem, pad_name))) {
    GST_WARNING ("sink has no pad named '%s'", pad_name);
    return FALSE;
  }

  gst_pad_add_buffer_probe (pad, (GCallback) handler, data);
  gst_object_unref (pad);

  return TRUE;
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

static gboolean
setup_pipeline (void)
{
  GstBus *bus;
  gboolean res = TRUE;
  GstElement *vmux, *ienc, *sink;

  g_string_printf (filename, "test_%04u.jpg", num_pics);

  camera_bin = gst_element_factory_make ("camerabin", NULL);
  if (NULL == camera_bin) {
    g_warning ("can't create camerabin element\n");
    goto error;
  }

  g_signal_connect (camera_bin, "image-done", (GCallback) img_capture_done,
      NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (camera_bin));
  gst_bus_add_watch (bus, bus_callback, NULL);
  gst_object_unref (bus);

  GST_INFO_OBJECT (camera_bin, "camerabin created");

  /* configure used elements */
  res &= setup_pipeline_element ("viewfinder-sink", "fakesink", &sink);
  res &= setup_pipeline_element ("audio-source", audiosrc_name, NULL);
  res &= setup_pipeline_element ("video-source", videosrc_name, NULL);
  res &= setup_pipeline_element ("audio-encoder", audioenc_name, NULL);
  res &= setup_pipeline_element ("video-encoder", videoenc_name, NULL);
  res &= setup_pipeline_element ("image-encoder", imageenc_name, &ienc);
  res &= setup_pipeline_element ("video-muxer", videomux_name, &vmux);
  if (!res) {
    goto error;
  }

  GST_INFO_OBJECT (camera_bin, "elements created");

  if (GST_STATE_CHANGE_FAILURE ==
      gst_element_set_state (camera_bin, GST_STATE_READY)) {
    g_warning ("can't set camerabin to ready\n");
    goto error;
  }
  GST_INFO_OBJECT (camera_bin, "camera ready");

  /* set properties */
  g_object_set (camera_bin, "filename", filename->str, NULL);

  if (src_csp && strlen (src_csp) == 4) {
    GstCaps *filter_caps;

    /* FIXME: why do we need to set this? */
    filter_caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC,
        GST_MAKE_FOURCC (src_csp[0], src_csp[1], src_csp[2], src_csp[3]), NULL);
    if (filter_caps) {
      g_object_set (camera_bin, "filter-caps", filter_caps, NULL);
      gst_caps_unref (filter_caps);
    } else {
      g_warning ("can't make filter-caps with format=%s\n", src_csp);
      goto error;
    }
  }

  g_object_set (sink, "sync", TRUE, NULL);

  GST_INFO_OBJECT (camera_bin, "elements configured");

  /* connect signal handlers */
  g_assert (sink);
  if (!setup_add_pad_probe (sink, "sink", (GCallback) pad_has_buffer,
          &signal_vf_sink)) {
    goto error;
  }
  if (!vmux) {
    g_object_get (camera_bin, "video-muxer", &vmux, NULL);
    if (!vmux) {
      need_pad_probe = need_vmux_pad_probe = TRUE;
      /* only run the test if we later get the element */
      test_09_taget = target[8];
      target[8] = G_GUINT64_CONSTANT (0);
    }
  }
  if (vmux) {
    if (!setup_add_pad_probe (vmux, "src", (GCallback) pad_has_buffer,
            &signal_vid_sink)) {
      goto error;
    }
  }
  if (!ienc) {
    g_object_get (camera_bin, "image-encoder", &ienc, NULL);
    if (!ienc) {
      need_pad_probe = need_ienc_pad_probe = TRUE;
      /* only run the test if we later get the element */
      test_06_taget = target[5];
      target[5] = G_GUINT64_CONSTANT (0);
    }
  }
  if (ienc) {
    if (!setup_add_pad_probe (ienc, "src", (GCallback) pad_has_buffer,
            &signal_img_enc)) {
      goto error;
    }
  }
  if (need_pad_probe) {
    g_signal_connect (camera_bin, "element-added", (GCallback) element_added,
        NULL);
  }
  GST_INFO_OBJECT (camera_bin, "probe signals connected");

  /* configure a resolution and framerate for video and viewfinder */
  if (image_width && image_height) {
    g_signal_emit_by_name (camera_bin, "set-image-resolution", image_width,
        image_height, NULL);
  }
  /* configure a resolution and framerate for video and viewfinder */
  if (video_width && video_height && view_framerate_num && view_framerate_den) {
    g_signal_emit_by_name (camera_bin, "set-video-resolution-fps", video_width,
        video_height, view_framerate_num, view_framerate_den, NULL);
  }

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

/*
 * Tests
 */

/* 01) Camera OFF to VF On
 *
 * This only tests the time it takes to create the pipeline and CameraBin
 * element and have the first video frame available in ViewFinder.
 * It is not testing the real init time. To do it, the timer must start before
 * the app.
 */
static gboolean
test_01 (void)
{
  gboolean res;

  GET_TIME (t_initial);
  if (setup_pipeline ()) {
    /* the actual results are fetched in bus_callback::state-changed */
    res = FALSE;
  } else {
    GET_TIME (t_final[0]);
    DIFF_TIME (t_final[0], t_initial, diff);

    result.avg = result.min = result.max = diff;
    res = TRUE;
  }
  result.times = 1;
  return res;
}


/* 03) Shot to snapshot
 *
 * It tests the time between pressing the Shot button and having the photo shown
 * in ViewFinder
 */
static gboolean
test_03 (void)
{
  GstCaps *snap_caps;

  /* FIXME: add options */
  snap_caps = gst_caps_from_string ("video/x-raw-rgb,width=320,height=240");
  g_object_set (camera_bin, "preview-caps", snap_caps, NULL);
  gst_caps_unref (snap_caps);

  /* switch to image mode */
  g_object_set (camera_bin, "mode", 0, NULL);
  g_object_set (camera_bin, "filename", filename->str, NULL);
  GET_TIME (t_initial);
  g_signal_emit_by_name (camera_bin, "capture-start", 0);

  /* the actual results are fetched in bus_callback::preview-image */
  result.times = 1;
  return FALSE;
}


/* 04) Shot to shot
 * It tests the time for being able to take a second shot after the first one.
 */
static gboolean
test_04 (void)
{
  /* switch to image mode */
  g_object_set (camera_bin, "mode", 0, NULL);
  GET_TIME (t_initial);
  g_signal_emit_by_name (camera_bin, "capture-start", 0);

  /* the actual results are fetched in bus_callback::image-captured */
  result.times = 1;
  return FALSE;
}


/* 05) Serial shooting
 *
 * It tests the time between shots in continuous mode.
 */
static gboolean
test_05 (void)
{
  signal_cont = TRUE;
  have_img_captured = have_img_done = FALSE;
  /* switch to image mode */
  g_object_set (camera_bin, "mode", 0, NULL);
  GET_TIME (t_initial);
  g_signal_emit_by_name (camera_bin, "capture-start", 0);

  /* the actual results are fetched in img_capture_done */
  result.times = CONT_SHOTS;
  return FALSE;
}


/* 06) Shutter lag
 * 
 * It tests the time from user-start signal to buffer reaching img-enc
 */
static gboolean
test_06 (void)
{
  signal_img_enc = TRUE;

  /* switch to image mode */
  g_object_set (camera_bin, "mode", 0, NULL);
  g_object_set (camera_bin, "filename", filename->str, NULL);
  GET_TIME (t_initial);
  g_signal_emit_by_name (camera_bin, "capture-start", 0);

  /* the actual results are fetched in pad_has_buffer */
  result.times = 1;
  return FALSE;
}


/* 07) Image saved
 * 
 * It tests the time between pressing the Shot and the final image is saved to
 * file system.
 */
static gboolean
test_07 (void)
{
  signal_shot = TRUE;

  /* switch to image mode */
  g_object_set (camera_bin, "mode", 0, NULL);
  g_object_set (camera_bin, "filename", filename->str, NULL);
  GET_TIME (t_initial);
  g_signal_emit_by_name (camera_bin, "capture-start", 0);
  /* the actual results are fetched in img_capture_done */
  result.times = 1;
  return FALSE;
}


/* 08) Mode change
 * 
 * It tests the time it takes to change between still image and video recording
 * mode (In this test we change the mode few times).
 */
static gboolean
test_08 (void)
{
  GstClockTime total = 0;
  GstClockTime max = 0;
  GstClockTime min = -1;
  const gint count = 6;
  gint i;

  /* switch to image mode */
  g_object_set (camera_bin, "mode", 0, NULL);
  g_object_set (camera_bin, "filename", filename->str, NULL);

  for (i = 0; i < count; ++i) {
    GET_TIME (t_final[i]);
    g_object_set (camera_bin, "mode", (i + 1) & 1, NULL);
    GET_TIME (t_final[i + 1]);
  }

  for (i = 0; i < count; ++i) {
    DIFF_TIME (t_final[i + 1], t_final[i], diff);
    total += diff;
    if (diff > max)
      max = diff;
    if (diff < min)
      min = diff;
  }

  result.avg = total / count;
  result.min = min;
  result.max = max;
  result.times = count;

  /* just make sure we are back to still image mode again */
  g_object_set (camera_bin, "mode", 0, NULL);
  return TRUE;
}


/* 09) Video recording
 * 
 * It tests the time it takes to start video recording.
 * FIXME: shouldn't we wait for the buffer arriving on the venc instead of sink?
 */
static gboolean
test_09 (void)
{
  signal_vid_sink = TRUE;

  /* switch to video mode */
  g_object_set (camera_bin, "mode", 1, NULL);
  g_object_set (camera_bin, "filename", filename->str, NULL);
  GET_TIME (t_initial);
  g_signal_emit_by_name (camera_bin, "capture-start", 0);

  /* the actual results are fetched in pad_has_buffer */
  result.times = 1;
  return FALSE;
}


typedef gboolean (*test_case) (void);
static test_case test_cases[TEST_CASES] = {
  test_01,
  NULL,
  test_03,
  test_04,
  test_05,
  test_06,
  test_07,
  test_08,
  test_09
};

static void
print_result (void)
{
  if (test_ix >= TEST_CASES) {
    GST_WARNING ("text case index overrun");
    return;
  }
  printf ("| %6.02f%% ", 100.0f * (float) result.avg / (float) target[test_ix]);
  printf ("|%5u ms ", (guint) GST_TIME_AS_MSECONDS (target[test_ix]));
  printf ("|%5u ms ", (guint) GST_TIME_AS_MSECONDS (result.avg));
  printf ("|%5u ms ", (guint) GST_TIME_AS_MSECONDS (result.min));
  printf ("|%5u ms ", (guint) GST_TIME_AS_MSECONDS (result.max));
  printf ("|  %3d   ", result.times);
  printf ("| %-19s |\n", test_names[test_ix]);
  test_ix++;
}

static gboolean
run_test (gpointer user_data)
{
  gboolean ret = TRUE;
  guint old_test_ix = test_ix;

  if (test_ix >= TEST_CASES) {
    GST_INFO ("done");
    g_main_loop_quit (loop);
    return FALSE;
  }

  printf ("|  %02d  ", test_ix + 1);
  fflush (stdout);
  if (test_cases[test_ix]) {
    if (target[test_ix]) {
      memset (&result, 0, sizeof (ResultType));
      ret = test_cases[test_ix] ();

      //while (g_main_context_pending (NULL)) g_main_context_iteration (NULL,FALSE);
      if (ret) {
        print_result ();
      }
    } else {
      printf ("|                      test skipped                        ");
      printf ("| %-19s |\n", test_names[test_ix]);
      test_ix++;
    }
  } else {
    printf ("|                  test not implemented                    ");
    printf ("| %-19s |\n", test_names[test_ix]);
    test_ix++;
  }
  fflush (stdout);

  if (old_test_ix == 0 && ret == TRUE && !camera_bin) {
    GST_INFO ("done (camerabin creation failed)");
    g_main_loop_quit (loop);
    return FALSE;
  }
  if (old_test_ix > 0 && !camera_bin) {
    GST_INFO ("done (camerabin was destroyed)");
    g_main_loop_quit (loop);
    return FALSE;
  }
  if (test_ix >= TEST_CASES) {
    GST_INFO ("done");
    g_main_loop_quit (loop);
    return FALSE;
  }
  GST_INFO ("%2d result: %d", test_ix, ret);
  return ret;
}

int
main (int argc, char *argv[])
{
  gchar *target_times = NULL;
  GOptionEntry options[] = {
    {"audio-src", '\0', 0, G_OPTION_ARG_STRING, &audiosrc_name,
        "audio source used in video recording", NULL},
    {"video-src", '\0', 0, G_OPTION_ARG_STRING, &videosrc_name,
        "video source used in still capture and video recording", NULL},
    {"audio-enc", '\0', 0, G_OPTION_ARG_STRING, &audioenc_name,
        "audio encoder used in video recording", NULL},
    {"video-enc", '\0', 0, G_OPTION_ARG_STRING, &videoenc_name,
        "video encoder used in video recording", NULL},
    {"image-enc", '\0', 0, G_OPTION_ARG_STRING, &imageenc_name,
        "image encoder used in still capture", NULL},
    {"video-mux", '\0', 0, G_OPTION_ARG_STRING, &videomux_name,
        "muxer used in video recording", NULL},
    {"image-width", '\0', 0, G_OPTION_ARG_INT, &image_width,
        "width for image capture", NULL},
    {"image-height", '\0', 0, G_OPTION_ARG_INT, &image_height,
        "height for image capture", NULL},
    {"video-width", '\0', 0, G_OPTION_ARG_INT, &video_width,
        "width for image capture", NULL},
    {"video-height", '\0', 0, G_OPTION_ARG_INT, &video_height,
        "height for image capture", NULL},
    {"view-framerate-num", '\0', 0, G_OPTION_ARG_INT, &view_framerate_num,
        "framerate numerator for viewfinder", NULL},
    {"view-framerate-den", '\0', 0, G_OPTION_ARG_INT, &view_framerate_den,
        "framerate denominator for viewfinder", NULL},
    {"src-colorspace", '\0', 0, G_OPTION_ARG_STRING, &src_csp,
        "colorspace format for videosource (e.g. YUY2, UYVY)", NULL},
    {"target-times", '\0', 0, G_OPTION_ARG_STRING, &target_times,
          "target test times in ms as comma separated values (0 to skip test)",
        NULL},
    {NULL}
  };
  GOptionContext *ctx;
  GError *err = NULL;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }
  g_option_context_free (ctx);

  GST_DEBUG_CATEGORY_INIT (camera_perf, "camera-perf", 0,
      "camera performcance test");

  /* init */
  filename = g_string_new_len ("", 16);
  loop = g_main_loop_new (NULL, FALSE);

  if (target_times) {
    gchar **numbers;
    gint i;

    numbers = g_strsplit (target_times, ",", TEST_CASES);
    for (i = 0; (numbers[i] && i < TEST_CASES); i++) {
      target[i] = GST_MSECOND * atoi (numbers[i]);
    }
    g_strfreev (numbers);
  }

  /* run */
  puts ("");
  puts ("+---------------------------------------------------------------------------------------+");
  puts ("| test |  rate   | target  |   avg   |   min   |   max   | trials |     description     |");
  puts ("+---------------------------------------------------------------------------------------+");
  g_idle_add ((GSourceFunc) run_test, NULL);
  g_main_loop_run (loop);
  puts ("+---------------------------------------------------------------------------------------+");
  puts ("");

  fflush (stdout);

  /* free */
  cleanup_pipeline ();
  g_main_loop_unref (loop);
  g_string_free (filename, TRUE);
  g_free (audiosrc_name);
  g_free (videosrc_name);
  g_free (audioenc_name);
  g_free (videoenc_name);
  g_free (imageenc_name);
  g_free (videomux_name);
  g_free (src_csp);
  g_free (target_times);

  return 0;
}
