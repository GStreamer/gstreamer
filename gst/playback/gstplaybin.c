/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>

#include <gst/gst-i18n-plugin.h>

#include "gstplaybasebin.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_bin_debug);
#define GST_CAT_DEFAULT gst_play_bin_debug

#define GST_TYPE_PLAY_BIN               (gst_play_bin_get_type())
#define GST_PLAY_BIN(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BIN,GstPlayBin))
#define GST_PLAY_BIN_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BIN,GstPlayBinClass))
#define GST_IS_PLAY_BIN(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BIN))
#define GST_IS_PLAY_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BIN))

#define VOLUME_MAX_DOUBLE 4.0

#ifndef GST_HAVE_GLIB_2_8
#define _gst_gvalue_set_gstobject(gvalue,obj)  \
      if (obj != NULL) {                       \
        gst_object_ref (obj);                  \
        g_value_set_object (gvalue, obj);      \
        g_object_unref (obj);                  \
      } else {                                 \
        g_value_set_object (gvalue, NULL);     \
      }
#else
#define _gst_gvalue_set_gstobject(gvalue,obj)  \
      g_value_set_object (gvalue, obj);
#endif

typedef struct _GstPlayBin GstPlayBin;
typedef struct _GstPlayBinClass GstPlayBinClass;

struct _GstPlayBin
{
  GstPlayBaseBin parent;

  /* the configurable elements */
  GstElement *fakesink;
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;
  GstElement *pending_visualisation;
  GstElement *volume_element;
  GstElement *textoverlay_element;
  gfloat volume;

  /* these are the currently active sinks */
  GList *sinks;

  /* the last captured frame for snapshots */
  GstBuffer *frame;

  /* our cache for the sinks */
  GHashTable *cache;

  /* font description */
  gchar *font_desc;
};

struct _GstPlayBinClass
{
  GstPlayBaseBinClass parent_class;
};

/* props */
enum
{
  ARG_0,
  ARG_AUDIO_SINK,
  ARG_VIDEO_SINK,
  ARG_VIS_PLUGIN,
  ARG_VOLUME,
  ARG_FRAME,
  ARG_FONT_DESC
};

/* signals */
enum
{
  LAST_SIGNAL
};

static void gst_play_bin_class_init (GstPlayBinClass * klass);
static void gst_play_bin_init (GstPlayBin * play_bin);
static void gst_play_bin_dispose (GObject * object);

static gboolean setup_sinks (GstPlayBaseBin * play_base_bin,
    GstPlayBaseGroup * group);
static void remove_sinks (GstPlayBin * play_bin);

static void gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static gboolean gst_play_bin_send_event (GstElement * element,
    GstEvent * event);
static GstStateChangeReturn gst_play_bin_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class;

//static guint gst_play_bin_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails gst_play_bin_details =
GST_ELEMENT_DETAILS ("Player Bin",
    "Generic/Bin/Player",
    "Autoplug and play media from an uri",
    "Wim Taymans <wim@fluendo.com>");

static GType
gst_play_bin_get_type (void)
{
  static GType gst_play_bin_type = 0;

  if (!gst_play_bin_type) {
    static const GTypeInfo gst_play_bin_info = {
      sizeof (GstPlayBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_bin_class_init,
      NULL,
      NULL,
      sizeof (GstPlayBin),
      0,
      (GInstanceInitFunc) gst_play_bin_init,
      NULL
    };

    gst_play_bin_type = g_type_register_static (GST_TYPE_PLAY_BASE_BIN,
        "GstPlayBin", &gst_play_bin_info, 0);
  }

  return gst_play_bin_type;
}

static void
gst_play_bin_class_init (GstPlayBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;
  GstPlayBaseBinClass *playbasebin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;
  playbasebin_klass = (GstPlayBaseBinClass *) klass;

  parent_class = g_type_class_ref (gst_play_base_bin_get_type ());

  gobject_klass->set_property = gst_play_bin_set_property;
  gobject_klass->get_property = gst_play_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_AUDIO_SINK,
      g_param_spec_object ("audio-sink", "Audio Sink",
          "the audio output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_VIS_PLUGIN,
      g_param_spec_object ("vis-plugin", "Vis plugin",
          "the visualization element to use (NULL = none)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_FRAME,
      gst_param_spec_mini_object ("frame", "Frame",
          "The last frame (NULL = no video available)",
          GST_TYPE_BUFFER, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_FONT_DESC,
      g_param_spec_string ("subtitle-font-desc",
          "Subtitle font description",
          "Pango font description of font "
          "to be used for subtitle rendering", NULL, G_PARAM_WRITABLE));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_bin_dispose);

  gst_element_class_set_details (gstelement_klass, &gst_play_bin_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_bin_change_state);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_play_bin_send_event);

  playbasebin_klass->setup_output_pads = setup_sinks;
}

static void
gst_play_bin_init (GstPlayBin * play_bin)
{
  play_bin->video_sink = NULL;
  play_bin->audio_sink = NULL;
  play_bin->visualisation = NULL;
  play_bin->pending_visualisation = NULL;
  play_bin->volume_element = NULL;
  play_bin->textoverlay_element = NULL;
  play_bin->volume = 1.0;
  play_bin->sinks = NULL;
  play_bin->frame = NULL;
  play_bin->font_desc = NULL;
  play_bin->cache = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) gst_object_unref);
}

static void
gst_play_bin_dispose (GObject * object)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);

  if (play_bin->cache != NULL) {
    remove_sinks (play_bin);
    g_hash_table_destroy (play_bin->cache);
    play_bin->cache = NULL;
  }

  if (play_bin->audio_sink != NULL) {
    gst_element_set_state (play_bin->audio_sink, GST_STATE_NULL);
    gst_object_unref (play_bin->audio_sink);
    play_bin->audio_sink = NULL;
  }
  if (play_bin->video_sink != NULL) {
    gst_element_set_state (play_bin->video_sink, GST_STATE_NULL);
    gst_object_unref (play_bin->video_sink);
    play_bin->video_sink = NULL;
  }
  if (play_bin->visualisation != NULL) {
    gst_element_set_state (play_bin->visualisation, GST_STATE_NULL);
    gst_object_unref (play_bin->visualisation);
    play_bin->visualisation = NULL;
  }
  if (play_bin->pending_visualisation != NULL) {
    gst_element_set_state (play_bin->pending_visualisation, GST_STATE_NULL);
    gst_object_unref (play_bin->pending_visualisation);
    play_bin->pending_visualisation = NULL;
  }
  if (play_bin->textoverlay_element != NULL) {
    gst_object_unref (play_bin->textoverlay_element);
    play_bin->textoverlay_element = NULL;
  }
  g_free (play_bin->font_desc);
  play_bin->font_desc = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_bin_vis_unblocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  /* Unblocked */
}

static void
gst_play_bin_vis_blocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (user_data);
  GstBin *vis_bin = NULL;
  GstPad *vis_sink_pad = NULL, *vis_src_pad = NULL, *vqueue_pad = NULL;
  GstState bin_state;

  /* We want to disable visualisation */
  if (!GST_IS_ELEMENT (play_bin->pending_visualisation)) {
    /* Set visualisation element to READY */
    gst_element_set_state (play_bin->visualisation, GST_STATE_READY);
    goto beach;
  }

  vis_bin =
      GST_BIN (gst_object_get_parent (GST_OBJECT (play_bin->visualisation)));

  if (!GST_IS_BIN (vis_bin) || !GST_IS_PAD (tee_pad)) {
    goto beach;
  }

  vis_src_pad = gst_element_get_pad (play_bin->visualisation, "src");
  vis_sink_pad = gst_pad_get_peer (tee_pad);

  /* Can be fakesink */
  if (GST_IS_PAD (vis_src_pad)) {
    vqueue_pad = gst_pad_get_peer (vis_src_pad);
  }

  if (!GST_IS_PAD (vis_sink_pad)) {
    goto beach;
  }

  /* Check the bin's state */
  GST_OBJECT_LOCK (vis_bin);
  bin_state = GST_STATE (vis_bin);
  GST_OBJECT_UNLOCK (vis_bin);

  /* Unlink */
  gst_pad_unlink (tee_pad, vis_sink_pad);
  gst_object_unref (vis_sink_pad);
  vis_sink_pad = NULL;

  if (GST_IS_PAD (vqueue_pad)) {
    gst_pad_unlink (vis_src_pad, vqueue_pad);
    gst_object_unref (vis_src_pad);
    vis_src_pad = NULL;
  }

  /* Remove from vis_bin */
  gst_bin_remove (vis_bin, play_bin->visualisation);
  /* Set state to NULL */
  gst_element_set_state (play_bin->visualisation, GST_STATE_NULL);
  /* And loose our ref */
  gst_object_unref (play_bin->visualisation);

  if (play_bin->pending_visualisation) {
    /* Ref this new visualisation element before adding to the bin */
    gst_object_ref (play_bin->pending_visualisation);
    /* Add the new one */
    gst_bin_add (vis_bin, play_bin->pending_visualisation);
    /* Synchronizing state */
    gst_element_set_state (play_bin->pending_visualisation, bin_state);

    vis_sink_pad = gst_element_get_pad (play_bin->pending_visualisation,
        "sink");
    vis_src_pad = gst_element_get_pad (play_bin->pending_visualisation, "src");

    if (!GST_IS_PAD (vis_sink_pad) || !GST_IS_PAD (vis_src_pad)) {
      goto beach;
    }

    /* Link */
    gst_pad_link (tee_pad, vis_sink_pad);
    gst_pad_link (vis_src_pad, vqueue_pad);
  }

  /* We are done */
  gst_object_unref (play_bin->visualisation);
  play_bin->visualisation = play_bin->pending_visualisation;
  play_bin->pending_visualisation = NULL;

beach:
  if (vis_sink_pad) {
    gst_object_unref (vis_sink_pad);
  }
  if (vis_src_pad) {
    gst_object_unref (vis_src_pad);
  }
  if (vqueue_pad) {
    gst_object_unref (vqueue_pad);
  }
  if (vis_bin) {
    gst_object_unref (vis_bin);
  }

  /* Unblock the pad */
  gst_pad_set_blocked_async (tee_pad, FALSE, gst_play_bin_vis_unblocked,
      play_bin);
}

static void
gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  g_return_if_fail (GST_IS_PLAY_BIN (object));

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_VIDEO_SINK:
      if (play_bin->video_sink != NULL) {
        gst_object_unref (play_bin->video_sink);
      }
      play_bin->video_sink = g_value_get_object (value);
      if (play_bin->video_sink != NULL) {
        gst_object_ref (play_bin->video_sink);
        gst_object_sink (GST_OBJECT (play_bin->video_sink));
      }
      /* when changing the videosink, we just remove the
       * video pipeline from the cache so that it will be 
       * regenerated with the new sink element */
      g_hash_table_remove (play_bin->cache, "vbin");
      break;
    case ARG_AUDIO_SINK:
      if (play_bin->audio_sink != NULL) {
        gst_object_unref (play_bin->audio_sink);
      }
      play_bin->audio_sink = g_value_get_object (value);
      if (play_bin->audio_sink != NULL) {
        gst_object_ref (play_bin->audio_sink);
        gst_object_sink (GST_OBJECT (play_bin->audio_sink));
      }
      g_hash_table_remove (play_bin->cache, "abin");
      break;
    case ARG_VIS_PLUGIN:
    {
      /* Do we already have a visualisation change pending ? */
      if (play_bin->pending_visualisation) {
        gst_object_unref (play_bin->pending_visualisation);
        play_bin->pending_visualisation = g_value_get_object (value);
        /* Take ownership */
        if (play_bin->pending_visualisation) {
          gst_object_ref (play_bin->pending_visualisation);
          gst_object_sink (GST_OBJECT (play_bin->pending_visualisation));
        }
      } else {
        play_bin->pending_visualisation = g_value_get_object (value);

        /* Take ownership */
        if (play_bin->pending_visualisation) {
          gst_object_ref (play_bin->pending_visualisation);
          gst_object_sink (GST_OBJECT (play_bin->pending_visualisation));
        }

        /* Was there a visualisation already set ? */
        if (play_bin->visualisation != NULL) {
          GstBin *vis_bin = NULL;

          vis_bin =
              GST_BIN (gst_object_get_parent (GST_OBJECT (play_bin->
                      visualisation)));

          /* Check if the visualisation is already in a bin */
          if (GST_IS_BIN (vis_bin)) {
            GstPad *vis_sink_pad = NULL, *tee_pad = NULL;

            /* Now get tee pad and block it async */
            vis_sink_pad = gst_element_get_pad (play_bin->visualisation,
                "sink");
            if (!GST_IS_PAD (vis_sink_pad)) {
              goto beach;
            }
            tee_pad = gst_pad_get_peer (vis_sink_pad);
            if (!GST_IS_PAD (tee_pad)) {
              goto beach;
            }

            /* Block with callback */
            gst_pad_set_blocked_async (tee_pad, TRUE, gst_play_bin_vis_blocked,
                play_bin);
          beach:
            if (vis_sink_pad) {
              gst_object_unref (vis_sink_pad);
            }
            if (tee_pad) {
              gst_object_unref (tee_pad);
            }
            gst_object_unref (vis_bin);
          } else {
            play_bin->visualisation = play_bin->pending_visualisation;
            play_bin->pending_visualisation = NULL;
          }
        } else {
          play_bin->visualisation = play_bin->pending_visualisation;
          play_bin->pending_visualisation = NULL;
        }
      }
      break;
    }
    case ARG_VOLUME:
      play_bin->volume = g_value_get_double (value);
      if (play_bin->volume_element) {
        g_object_set (G_OBJECT (play_bin->volume_element), "volume",
            play_bin->volume, NULL);
      }
      break;
    case ARG_FONT_DESC:
      g_free (play_bin->font_desc);
      play_bin->font_desc = g_strdup (g_value_get_string (value));
      if (play_bin->textoverlay_element) {
        g_object_set (G_OBJECT (play_bin->textoverlay_element),
            "font-desc", g_value_get_string (value), NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  g_return_if_fail (GST_IS_PLAY_BIN (object));

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_VIDEO_SINK:
      _gst_gvalue_set_gstobject (value, play_bin->video_sink);
      break;
    case ARG_AUDIO_SINK:
      _gst_gvalue_set_gstobject (value, play_bin->audio_sink);
      break;
    case ARG_VIS_PLUGIN:
      _gst_gvalue_set_gstobject (value, play_bin->visualisation);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, play_bin->volume);
      break;
    case ARG_FRAME:
      gst_value_set_mini_object (value, GST_MINI_OBJECT (play_bin->frame));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* signal fired when the identity has received a new buffer. This is used for
 * making screenshots.
 */
static void
handoff (GstElement * identity, GstBuffer * frame, gpointer data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (data);
  GstBuffer **frame_p = &play_bin->frame;

  gst_mini_object_replace ((GstMiniObject **) frame_p,
      GST_MINI_OBJECT_CAST (frame));

  /* applications need to know the buffer caps,
   * make sure they are always set on the frame */
  if (GST_BUFFER_CAPS (play_bin->frame) == NULL) {
    GstPad *pad;

    if ((pad = gst_element_get_pad (identity, "sink"))) {
      gst_buffer_set_caps (play_bin->frame, GST_PAD_CAPS (pad));
      gst_object_unref (pad);
    }
  }
}

/* make the element (bin) that contains the elements needed to perform
 * video display. We connect a handoff signal to identity so that we
 * can grab snapshots. Identity's sinkpad is ghosted to vbin.
 *
 *  +-------------------------------------------------------------+
 *  | vbin                                                        |
 *  |      +--------+   +----------+   +----------+   +---------+ |
 *  |      |identity|   |colorspace|   |videoscale|   |videosink| |
 *  |   +-sink     src-sink       src-sink       src-sink       | |
 *  |   |  +---+----+   +----------+   +----------+   +---------+ |
 * sink-+      |                                                  |
 *  +----------|--------------------------------------------------+
 *           handoff
 */
/* FIXME: this might return NULL if no videosink was found, handle
 * this in callers */
static GstElement *
gen_video_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;

  GstElement *scale;
  GstElement *sink;
  GstElement *identity;
  GstPad *pad;

  /* first see if we have it in the cache */
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    return element;
  }

  if (play_bin->video_sink) {
    sink = play_bin->video_sink;
  } else {
    sink = gst_element_factory_make ("autovideosink", "videosink");
    if (sink == NULL) {
      sink = gst_element_factory_make ("xvimagesink", "videosink");
    }
    /* FIXME: this warrants adding a CORE error category for missing
     * elements/plugins */
    if (sink == NULL) {
      GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
          (_("Both autovideosink and xvimagesink elements are missing.")),
          (NULL));
      return NULL;
    }
  }
  gst_object_ref (sink);
  g_hash_table_insert (play_bin->cache, "video_sink", sink);


  element = gst_bin_new ("vbin");
  identity = gst_element_factory_make ("identity", "id");
  g_object_set (identity, "silent", TRUE, NULL);
  g_signal_connect (identity, "handoff", G_CALLBACK (handoff), play_bin);
  gst_bin_add (GST_BIN (element), identity);
  conv = gst_element_factory_make ("ffmpegcolorspace", "vconv");
  if (conv == NULL)
    goto no_colorspace;
  scale = gst_element_factory_make ("videoscale", "vscale");
  if (scale == NULL)
    goto no_videoscale;
  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), scale);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (identity, "src", conv, "sink");
  gst_element_link_pads (conv, "src", scale, "sink");
  gst_element_link_pads (scale, "src", sink, "sink");

  pad = gst_element_get_pad (identity, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (element);
  g_hash_table_insert (play_bin->cache, "vbin", element);

  return element;

no_colorspace:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "ffmpegcolorspace"), (NULL));
    gst_object_unref (element);
    return NULL;
  }

no_videoscale:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "videoscale"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
}

/* make an element for playback of video with subtitles embedded.
 *
 *  +--------------------------------------------------+
 *  | tbin                  +-------------+            |
 *  |          +-----+      | textoverlay |   +------+ |
 *  |          | csp | +--video_sink      |   | vbin | |
 * video_sink-sink  src+ +-text_sink     src-sink    | |
 *  |          +-----+   |  +-------------+   +------+ |
 * text_sink-------------+                             |
 *  +--------------------------------------------------+
 */

static GstElement *
gen_text_element (GstPlayBin * play_bin)
{
  GstElement *element, *csp, *overlay, *vbin;
  GstPad *pad;

  /* Create our bin */
  element = gst_bin_new ("textbin");

  /* Text overlay */
  overlay = gst_element_factory_make ("textoverlay", "overlay");

  /* Create the video rendering bin */
  vbin = gen_video_element (play_bin);

  /* If no overlay return the video bin */
  if (!overlay) {
    GST_WARNING ("No overlay (pango) element, subtitles disabled");
    return vbin;
  }

  /* Set some parameters */
  g_object_set (G_OBJECT (overlay),
      "halign", "center", "valign", "bottom", NULL);
  if (play_bin->font_desc) {
    g_object_set (G_OBJECT (overlay), "font-desc", play_bin->font_desc, NULL);
  }

  /* Take a ref */
  play_bin->textoverlay_element = GST_ELEMENT (gst_object_ref (overlay));

  /* we know this will succeed, as the video bin already created one before */
  csp = gst_element_factory_make ("ffmpegcolorspace", "subtitlecsp");

  /* Add our elements */
  gst_bin_add_many (GST_BIN (element), csp, overlay, vbin, NULL);

  /* Link */
  gst_element_link_pads (csp, "src", overlay, "video_sink");
  gst_element_link_pads (overlay, "src", vbin, "sink");

  /* Add ghost pads on the subtitle bin */
  pad = gst_element_get_pad (overlay, "text_sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("text_sink", pad));
  gst_object_unref (pad);

  pad = gst_element_get_pad (csp, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  /* Set state to READY */
  gst_element_set_state (element, GST_STATE_READY);

  return element;
}

/* make the element (bin) that contains the elements needed to perform
 * audio playback.
 *
 *  +-------------------------------------------------------------+
 *  | abin                                                        |
 *  |      +---------+   +----------+   +---------+   +---------+ |
 *  |      |audioconv|   |audioscale|   | volume  |   |audiosink| |
 *  |   +-sink      src-sink       src-sink      src-sink       | |
 *  |   |  +---------+   +----------+   +---------+   +---------+ |
 * sink-+                                                         |
 *  +-------------------------------------------------------------+
 *
 */
static GstElement *
gen_audio_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;
  GstElement *volume;

  /* GstElement *scale; */
  GstPad *pad;

  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    return element;
  }
  element = gst_bin_new ("abin");
  conv = gst_element_factory_make ("audioconvert", "aconv");
  if (conv == NULL)
    goto no_audioconvert;
/*
  scale = gst_element_factory_make ("audioresample", "aresample");
  if (scale == NULL)
    goto no_audioresample;
*/
  volume = gst_element_factory_make ("volume", "volume");
  g_object_set (G_OBJECT (volume), "volume", play_bin->volume, NULL);
  play_bin->volume_element = volume;

  if (play_bin->audio_sink) {
    sink = play_bin->audio_sink;
  } else {
    sink = gst_element_factory_make ("autoaudiosink", "audiosink");
    if (sink == NULL) {
      sink = gst_element_factory_make ("alsasink", "audiosink");
    }
    if (sink == NULL) {
      GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
          (_("Both autoaudiosink and alsasink elements are missing.")), (NULL));
      return NULL;
    }
    play_bin->audio_sink = GST_ELEMENT (gst_object_ref (sink));
  }

  gst_object_ref (sink);
  g_hash_table_insert (play_bin->cache, "audio_sink", sink);

  gst_bin_add (GST_BIN (element), conv);
  /* gst_bin_add (GST_BIN (element), scale); */
  gst_bin_add (GST_BIN (element), volume);
  gst_bin_add (GST_BIN (element), sink);

  gst_element_link_pads (conv, "src",   /*scale, "sink");
                                           gst_element_link_pads (scale, "src", */ volume, "sink");
  gst_element_link_pads (volume, "src", sink, "sink");

  pad = gst_element_get_pad (conv, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (element);
  g_hash_table_insert (play_bin->cache, "abin", element);

  return element;

no_audioconvert:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioconvert"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
/*
no_audioresample:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
         "audioresample"),
        ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
*/
}

/* make the element (bin) that contains the elements needed to perform
 * visualisation ouput.  The idea is to split the audio using tee, then 
 * sending the output to the regular audio bin and the other output to
 * the vis plugin that transforms it into a video that is rendered with the
 * normal video bin. The video bin is run in a thread to make sure it does
 * not block the audio playback pipeline.
 *
 *  +--------------------------------------------------------------------+
 *  | visbin                                                             |
 *  |      +------+   +--------+   +----------------+                    |
 *  |      | tee  |   | aqueue |   |   abin ...     |                    |
 *  |   +-sink   src-sink     src-sink              |                    |
 *  |   |  |      |   +--------+   +----------------+                    |
 *  |   |  |      |                                                      |
 *  |   |  |      |   +------+   +---------+   +------+   +-----------+  |
 *  |   |  |      |   |vqueue|   |audioconv|   | vis  |   | vbin ...  |  |
 *  |   |  |     src-sink   src-sink      src-sink   src-sink         |  |
 *  |   |  |      |   +------+   +---------+   +------+   +-----------+  |
 *  |   |  |      |                                                      |
 *  |   |  +------+                                                      |
 * sink-+                                                                |
   +---------------------------------------------------------------------+
 */
static GstElement *
gen_vis_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *tee;
  GstElement *asink;
  GstElement *vsink;
  GstElement *conv;
  GstElement *vis;
  GstElement *vqueue, *aqueue;
  GstPad *pad, *rpad;

  asink = gen_audio_element (play_bin);
  if (!asink)
    return NULL;
  vsink = gen_video_element (play_bin);
  if (!vsink) {
    gst_object_unref (asink);
    return NULL;
  }

  element = gst_bin_new ("visbin");
  tee = gst_element_factory_make ("tee", "tee");

  vqueue = gst_element_factory_make ("queue", "vqueue");
  aqueue = gst_element_factory_make ("queue", "aqueue");

  gst_bin_add (GST_BIN (element), asink);
  gst_bin_add (GST_BIN (element), vqueue);
  gst_bin_add (GST_BIN (element), aqueue);
  gst_bin_add (GST_BIN (element), vsink);
  gst_bin_add (GST_BIN (element), tee);

  conv = gst_element_factory_make ("audioconvert", "aconv");
  if (conv == NULL)
    goto no_audioconvert;

  if (play_bin->visualisation) {
    gst_object_ref (play_bin->visualisation);
    vis = play_bin->visualisation;
  } else {
    vis = gst_element_factory_make ("goom", "vis");
  }

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), vis);

  gst_element_link_pads (vqueue, "src", conv, "sink");
  gst_element_link_pads (conv, "src", vis, "sink");
  gst_element_link_pads (vis, "src", vsink, "sink");

  pad = gst_element_get_pad (aqueue, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_pads (aqueue, "src", asink, "sink");

  pad = gst_element_get_pad (vqueue, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);

  pad = gst_element_get_pad (tee, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  return element;

no_audioconvert:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioconvert"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
}

/* get rid of all installed sinks */
static void
remove_sinks (GstPlayBin * play_bin)
{
  GList *sinks;
  GstObject *parent;
  GstElement *element;
  GstPad *pad, *peer;

  GST_DEBUG ("removesinks");
  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      /* we remove the element from the parent so that
       * there is no unwanted state change when the parent
       * is disposed */
      play_bin->sinks = g_list_remove (play_bin->sinks, element);
      gst_element_set_state (element, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (parent), element);
      gst_object_unref (parent);
    }
    pad = gst_element_get_pad (element, "sink");
    if (pad != NULL) {
      peer = gst_pad_get_peer (pad);
      if (peer != NULL) {
        gst_pad_unlink (peer, pad);
        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      play_bin->sinks = g_list_remove (play_bin->sinks, element);
      gst_element_set_state (element, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (parent), element);
      gst_object_unref (parent);
    }
    pad = gst_element_get_pad (element, "sink");
    if (pad != NULL) {
      peer = gst_pad_get_peer (pad);
      if (peer != NULL) {
        gst_pad_unlink (peer, pad);
        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }

  for (sinks = play_bin->sinks; sinks; sinks = g_list_next (sinks)) {
    GstElement *element = GST_ELEMENT (sinks->data);
    GstPad *pad;
    GstPad *peer;

    pad = gst_element_get_pad (element, "sink");

    GST_LOG ("removing sink %p", element);

    peer = gst_pad_get_peer (pad);
    if (peer) {
      gst_pad_unlink (peer, pad);
      gst_object_unref (peer);
    }
    gst_object_unref (pad);

    gst_element_set_state (element, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (play_bin), element);
  }
  g_list_free (play_bin->sinks);
  play_bin->sinks = NULL;

  /* FIXME: this is probably some refcounting problem */
  if (play_bin->visualisation && GST_OBJECT_PARENT (play_bin->visualisation)) {
    gst_element_set_state (play_bin->visualisation, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (GST_OBJECT_PARENT (play_bin->visualisation)),
        play_bin->visualisation);
  }

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
    play_bin->frame = NULL;
  }

  if (play_bin->textoverlay_element) {
    gst_object_unref (play_bin->textoverlay_element);
    play_bin->textoverlay_element = NULL;
  }
}

/* loop over the streams and set up the pipeline to play this
 * media file. First we count the number of audio and video streams.
 * If there is no video stream but there exists an audio stream,
 * we install a visualisation pipeline.
 * 
 * Also make sure to only connect the first audio and video pad. FIXME
 * this should eventually be handled with a tuner interface so that
 * one can switch the streams.
 */
static gboolean
add_sink (GstPlayBin * play_bin, GstElement * sink, GstPad * srcpad,
    GstPad * subtitle_pad)
{
  GstPad *sinkpad;
  GstPadLinkReturn linkres;
  GstElement *parent;
  GstStateChangeReturn stateret;

  g_return_val_if_fail (sink != NULL, FALSE);
  /* this is only for debugging */
  parent = gst_pad_get_parent_element (srcpad);
  if (parent) {
    GST_DEBUG ("Adding sink with state %d (parent: %d, peer: %d)",
        GST_STATE (sink), GST_STATE (play_bin), GST_STATE (parent));
    gst_object_unref (parent);
  }

  /* bring it to the PAUSED state so we can link to the peer without
   * breaking the flow */
  if ((stateret = gst_element_set_state (sink, GST_STATE_PAUSED)) ==
      GST_STATE_CHANGE_FAILURE)
    goto state_failed;

  gst_bin_add (GST_BIN (play_bin), sink);

  /* we found a sink for this stream, now try to install it */
  sinkpad = gst_element_get_pad (sink, "sink");
  linkres = gst_pad_link (srcpad, sinkpad);
  gst_object_unref (sinkpad);

  /* try to link the pad of the sink to the stream */
  if (GST_PAD_LINK_FAILED (linkres))
    goto link_failed;

  if (GST_IS_PAD (subtitle_pad)) {
    sinkpad = gst_element_get_pad (sink, "text_sink");
    linkres = gst_pad_link (subtitle_pad, sinkpad);
    gst_object_unref (sinkpad);
  }

  /* try to link the subtitle pad of the sink to the stream */
  if (GST_PAD_LINK_FAILED (linkres)) {
    goto subtitle_failed;
  }

  /* we got the sink succesfully linked, now keep the sink
   * in our internal list */
  play_bin->sinks = g_list_prepend (play_bin->sinks, sink);

  return TRUE;

  /* ERRORS */
state_failed:
  {
    GST_DEBUG_OBJECT (play_bin, "state change failure when adding sink");
    return FALSE;
  }
link_failed:
  {
    gchar *capsstr;
    GstCaps *caps;

    /* could not link this stream */
    caps = gst_pad_get_caps (srcpad);
    capsstr = gst_caps_to_string (caps);
    g_warning ("could not link %s: %d", capsstr, linkres);
    GST_DEBUG_OBJECT (play_bin,
        "link failed when adding sink, caps %s, reason %d", capsstr, linkres);
    g_free (capsstr);
    g_free (caps);

    gst_element_set_state (sink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (play_bin), sink);
    return FALSE;
  }
subtitle_failed:
  {
    gchar *capsstr;
    GstCaps *caps;

    /* could not link this stream */
    caps = gst_pad_get_caps (subtitle_pad);
    capsstr = gst_caps_to_string (caps);
    GST_DEBUG_OBJECT (play_bin,
        "subtitle link failed when adding sink, caps %s, reason %d", capsstr,
        linkres);
    g_free (capsstr);
    g_free (caps);

    return TRUE;
  }
}

static gboolean
setup_sinks (GstPlayBaseBin * play_base_bin, GstPlayBaseGroup * group)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (play_base_bin);
  GList *streaminfo = NULL, *s;
  gboolean need_vis = FALSE;
  gboolean need_text = FALSE;
  GstPad *textsrcpad = NULL, *pad = NULL;
  GstElement *sink;
  gboolean res = TRUE;

  /* get rid of existing sinks */
  if (play_bin->sinks) {
    remove_sinks (play_bin);
  }
  GST_DEBUG_OBJECT (play_base_bin, "setupsinks");

  /* find out what to do */
  if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads > 0 &&
      group->type[GST_STREAM_TYPE_TEXT - 1].npads > 0) {
    need_text = TRUE;
  } else if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads == 0 &&
      group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0 &&
      play_bin->visualisation != NULL) {
    need_vis = TRUE;
  }

  /* now actually connect everything */
  g_object_get (G_OBJECT (play_base_bin), "stream-info", &streaminfo, NULL);
  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    gint type;
    GstObject *object;

    g_object_get (obj, "type", &type, NULL);
    g_object_get (obj, "object", &object, NULL);
  }

  /* link audio */
  if (group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0) {
    if (need_vis) {
      sink = gen_vis_element (play_bin);
    } else {
      sink = gen_audio_element (play_bin);
    }
    if (!sink)
      return FALSE;
    pad = gst_element_get_pad (group->type[GST_STREAM_TYPE_AUDIO - 1].preroll,
        "src");
    res = add_sink (play_bin, sink, pad, NULL);
    gst_object_unref (pad);
  }

  /* link video */
  if (group->type[GST_STREAM_TYPE_VIDEO - 1].npads > 0) {
    if (need_text) {
      GstObject *parent = NULL, *grandparent = NULL;
      GstPad *ghost = NULL;

      sink = gen_text_element (play_bin);
      textsrcpad =
          gst_element_get_pad (group->type[GST_STREAM_TYPE_TEXT - 1].preroll,
          "src");
      /* This pad is from subtitle-bin, we need to create a ghost pad to have
         common grandparents */
      parent = gst_object_get_parent (GST_OBJECT (textsrcpad));
      if (!parent) {
        GST_WARNING_OBJECT (textsrcpad, "subtitle pad has no parent !");
        gst_object_unref (textsrcpad);
        textsrcpad = NULL;
        goto beach;
      }

      grandparent = gst_object_get_parent (parent);
      if (!grandparent) {
        GST_WARNING_OBJECT (textsrcpad, "subtitle pad has no grandparent !");
        gst_object_unref (parent);
        gst_object_unref (textsrcpad);
        textsrcpad = NULL;
        goto beach;
      }

      /* We ghost the pad on subtitle_bin only, if the text pad is from the
         media demuxer we keep it as it is */
      if (!GST_IS_PLAY_BIN (grandparent)) {
        GST_DEBUG_OBJECT (textsrcpad, "this subtitle pad is from a subtitle "
            "file, ghosting to a suitable hierarchy");
        ghost = gst_ghost_pad_new ("text_src", textsrcpad);
        if (!GST_IS_PAD (ghost)) {
          GST_WARNING_OBJECT (textsrcpad, "failed creating ghost pad for "
              "subtitle-bin");
          gst_object_unref (parent);
          gst_object_unref (grandparent);
          gst_object_unref (textsrcpad);
          textsrcpad = NULL;
          goto beach;
        }

        if (gst_element_add_pad (GST_ELEMENT (grandparent), ghost)) {
          gst_object_unref (textsrcpad);
          textsrcpad = gst_object_ref (ghost);
        } else {
          GST_WARNING_OBJECT (ghost, "failed adding ghost pad on subtitle-bin");
          gst_object_unref (ghost);
          gst_object_unref (textsrcpad);
          textsrcpad = NULL;
        }
      } else {
        GST_DEBUG_OBJECT (textsrcpad, "this subtitle pad is from the demuxer "
            "no changes to hierarchy needed");
      }

      gst_object_unref (parent);
      gst_object_unref (grandparent);
    } else {
      sink = gen_video_element (play_bin);
    }
  beach:
    if (!sink)
      return FALSE;
    pad = gst_element_get_pad (group->type[GST_STREAM_TYPE_VIDEO - 1].preroll,
        "src");
    res = add_sink (play_bin, sink, pad, textsrcpad);
    gst_object_unref (pad);
    if (textsrcpad) {
      gst_object_unref (textsrcpad);
    }
  }

  /* remove the sinks now, pipeline get_state will now wait for the
   * sinks to preroll */
  if (play_bin->fakesink) {
    gst_element_set_state (play_bin->fakesink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (play_bin), play_bin->fakesink);
    play_bin->fakesink = NULL;
  }

  return res;
}

/* Send an event to our sinks until one of them works; don't then send to the
 * remaining sinks (unlike GstBin)
 */
static gboolean
gst_play_bin_send_event_to_sink (GstPlayBin * play_bin, GstEvent * event)
{
  GList *sinks = play_bin->sinks;
  gboolean res = TRUE;

  while (sinks) {
    GstElement *sink = GST_ELEMENT_CAST (sinks->data);

    gst_event_ref (event);
    if ((res = gst_element_send_event (sink, event))) {
      GST_DEBUG_OBJECT (play_bin,
          "Sent event succesfully to sink %" GST_PTR_FORMAT, sink);
      break;
    }
    GST_DEBUG_OBJECT (play_bin,
        "Event failed when sent to sink %" GST_PTR_FORMAT, sink);

    sinks = g_list_next (sinks);
  }

  gst_event_unref (event);

  return res;
}

static gboolean
do_playbin_seek (GstElement * element, GstEvent * event)
{
  gdouble rate;
  GstSeekFlags flags;
  gboolean flush;
  gboolean was_playing = FALSE;
  gboolean res;

  gst_event_parse_seek (event, &rate, NULL, &flags, NULL, NULL, NULL, NULL);

  flush = flags & GST_SEEK_FLAG_FLUSH;

  if (flush) {
    GstState state;

    /* need to call _get_state() since a bin state is only updated
     * with this call. */
    gst_element_get_state (element, &state, NULL, 0);
    was_playing = state == GST_STATE_PLAYING;

    if (was_playing) {
      gst_element_set_state (element, GST_STATE_PAUSED);
      gst_element_get_state (element, NULL, NULL, 50 * GST_MSECOND);
    }
  }

  GST_DEBUG_OBJECT (element, "Sending seek event to a sink");
  res = gst_play_bin_send_event_to_sink (GST_PLAY_BIN (element), event);

  if (flush) {
    /* need to reset the stream time to 0 after a flushing seek */
    if (res)
      gst_pipeline_set_new_stream_time (GST_PIPELINE (element), 0);

    if (was_playing)
      /* and continue playing */
      gst_element_set_state (element, GST_STATE_PLAYING);
  }
  return res;
}

/* We only want to send the event to a single sink (overriding GstBin's 
 * behaviour), but we want to keep GstPipeline's behaviour - wrapping seek
 * events appropriately. So, this is a messy duplication of code. */
static gboolean
gst_play_bin_send_event (GstElement * element, GstEvent * event)
{
  gboolean res = FALSE;
  GstEventType event_type = GST_EVENT_TYPE (event);


  switch (event_type) {
    case GST_EVENT_SEEK:
      res = do_playbin_seek (element, event);
      break;
    default:
      res = gst_play_bin_send_event_to_sink (GST_PLAY_BIN (element), event);
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_play_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);


  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* this really is the easiest way to make the state change return
       * ASYNC until we added the sinks */
      if (!play_bin->fakesink) {
        play_bin->fakesink = gst_element_factory_make ("fakesink", "test");
        gst_bin_add (GST_BIN (play_bin), play_bin->fakesink);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* Set audio sink state to NULL to release the sound device,
       * but only if we own it (else we might be in chain-transition). */
      //if (play_bin->audio_sink != NULL &&
      //    GST_STATE (play_bin->audio_sink) == GST_STATE_PAUSED) {
      //  gst_element_set_state (play_bin->audio_sink, GST_STATE_NULL);
      //}
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* Check for NULL because the state transition may be done by
       * gst_bin_dispose which is called by gst_play_bin_dispose, and in that
       * case, we don't want to run remove_sinks.
       * FIXME: should the NULL test be done in remove_sinks? Should we just
       * set the state to NULL in gst_play_bin_dispose?
       */
      if (play_bin->cache != NULL) {
        remove_sinks (play_bin);
      }
      if (play_bin->fakesink) {
        gst_element_set_state (play_bin->fakesink, GST_STATE_NULL);
        gst_bin_remove (GST_BIN (play_bin), play_bin->fakesink);
        play_bin->fakesink = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_play_bin_debug, "playbin", 0, "play bin");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  return gst_element_register (plugin, "playbin", GST_RANK_NONE,
      GST_TYPE_PLAY_BIN);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "playbin",
    "player bin", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
