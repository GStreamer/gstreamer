/* GStreamer
 * Copyright (C) 2008 Jan Schmidt <thaytan@noraisin.net>
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
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "resindvdbin.h"
#include "resindvdsrc.h"
#include "rsnstreamselector.h"
#include "rsnaudiomunge.h"
#include "rsnparsetter.h"

#include "gstmpegdemux.h"

GST_DEBUG_CATEGORY_EXTERN (resindvd_debug);
#define GST_CAT_DEFAULT resindvd_debug

#define DECODEBIN_AUDIO 0
#define USE_VIDEOQ 0

#define DVDBIN_LOCK(d) g_mutex_lock((d)->dvd_lock)
#define DVDBIN_UNLOCK(d) g_mutex_unlock((d)->dvd_lock)

#define DEFAULT_DEVICE "/dev/dvd"
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE
};

/* FIXME: Could list specific video and audio caps: */
static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS
    ("video/mpeg, mpegversion=(int) { 1, 2 }, systemstream=false")
    );

static GstStaticPadTemplate audio_src_template =
    GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw-int;audio/x-raw-float")
    );

static GstStaticPadTemplate subpicture_src_template =
GST_STATIC_PAD_TEMPLATE ("subpicture",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-dvd-subpicture")
    );

static void rsn_dvdbin_do_init (GType rsn_dvdbin_type);
static void rsn_dvdbin_finalize (GObject * object);
static void rsn_dvdbin_uri_handler_init (gpointer g_iface, gpointer iface_data);

GST_BOILERPLATE_FULL (RsnDvdBin, rsn_dvdbin, GstBin,
    GST_TYPE_BIN, rsn_dvdbin_do_init);

static void demux_pad_added (GstElement * element, GstPad * pad,
    RsnDvdBin * dvdbin);
static void viddec_pad_added (GstElement * element, GstPad * pad,
    gboolean last, RsnDvdBin * dvdbin);
#if DECODEBIN_AUDIO
static void auddec_pad_added (GstElement * element, GstPad * pad,
    gboolean last, RsnDvdBin * dvdbin);
#endif
static void rsn_dvdbin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void rsn_dvdbin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn rsn_dvdbin_change_state (GstElement * element,
    GstStateChange transition);
static void dvdbin_pad_blocked_cb (GstPad * pad, gboolean blocked,
    RsnDvdBin * dvdbin);

static void
rsn_dvdbin_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "rsndvdbin",
    "Generic/Bin/Player",
    "DVD playback element",
    "Jan Schmidt <thaytan@noraisin.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subpicture_src_template));
  gst_element_class_set_details (element_class, &element_details);

  element_class->change_state = GST_DEBUG_FUNCPTR (rsn_dvdbin_change_state);
}

static void
rsn_dvdbin_class_init (RsnDvdBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = rsn_dvdbin_finalize;
  gobject_class->set_property = rsn_dvdbin_set_property;
  gobject_class->get_property = rsn_dvdbin_get_property;

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device", "DVD device location",
          NULL, G_PARAM_READWRITE));
}

static void
rsn_dvdbin_do_init (GType rsn_dvdbin_type)
{
  static const GInterfaceInfo urihandler_info = {
    rsn_dvdbin_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (rsn_dvdbin_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
}

static void
rsn_dvdbin_init (RsnDvdBin * dvdbin, RsnDvdBinClass * gclass)
{
  dvdbin->dvd_lock = g_mutex_new ();
}

static void
rsn_dvdbin_finalize (GObject * object)
{
  RsnDvdBin *dvdbin = RESINDVDBIN (object);

  g_mutex_free (dvdbin->dvd_lock);
  g_free (dvdbin->last_uri);
  g_free (dvdbin->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* URI interface */
static guint
rsn_dvdbin_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
rsn_dvdbin_uri_get_protocols (void)
{
  static gchar *protocols[] = { "dvd", NULL };

  return protocols;
}

static const gchar *
rsn_dvdbin_uri_get_uri (GstURIHandler * handler)
{
  RsnDvdBin *dvdbin = RESINDVDBIN (handler);

  DVDBIN_LOCK (dvdbin);
  g_free (dvdbin->last_uri);
  if (dvdbin->device)
    dvdbin->last_uri = g_strdup_printf ("dvd://%s", dvdbin->device);
  else
    dvdbin->last_uri = g_strdup ("dvd://");
  DVDBIN_UNLOCK (dvdbin);

  return dvdbin->last_uri;
}

static gboolean
rsn_dvdbin_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  RsnDvdBin *dvdbin = RESINDVDBIN (handler);
  gboolean ret;
  gchar *protocol, *location;

  protocol = gst_uri_get_protocol (uri);

  ret = (protocol && !strcmp (protocol, "dvd")) ? TRUE : FALSE;

  g_free (protocol);
  protocol = NULL;

  if (!ret)
    return ret;

  location = gst_uri_get_location (uri);
  if (!location)
    return ret;

  /*
   * URI structure: dvd:///path/to/device
   */
  if (g_str_has_prefix (uri, "dvd://")) {
    g_free (dvdbin->device);
    dvdbin->device = g_strdup (uri + 6);
  }
#if 0
  /*
   * Parse out the new t/c/a and seek to them
   */
  {
    gchar **strs;
    gchar **strcur;
    gint pos = 0;

    strcur = strs = g_strsplit (location, ",", 0);
    while (strcur && *strcur) {
      gint val;

      if (!sscanf (*strcur, "%d", &val))
        break;

      switch (pos) {
        case 0:
          if (val != dvdbin->uri_title) {
            dvdbin->uri_title = val;
            dvdbin->new_seek = TRUE;
          }
          break;
        case 1:
          if (val != dvdbin->uri_chapter) {
            dvdbin->uri_chapter = val;
            dvdbin->new_seek = TRUE;
          }
          break;
        case 2:
          dvdbin->uri_angle = val;
          break;
      }

      strcur++;
      pos++;
    }

    g_strfreev (strs);
  }
#endif

  g_free (location);

  return ret;
}

static void
rsn_dvdbin_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = rsn_dvdbin_uri_get_type;
  iface->get_protocols = rsn_dvdbin_uri_get_protocols;
  iface->get_uri = rsn_dvdbin_uri_get_uri;
  iface->set_uri = rsn_dvdbin_uri_set_uri;
}

static gboolean
try_create_piece (RsnDvdBin * dvdbin, gint index,
    const gchar * factory, GType type, const gchar * name, const gchar * descr)
{
  GstElement *e;

  DVDBIN_LOCK (dvdbin);
  if (dvdbin->pieces[index] != NULL) {
    DVDBIN_UNLOCK (dvdbin);
    return TRUE;                /* Already exists */
  }
  DVDBIN_UNLOCK (dvdbin);

  if (factory != NULL) {
    e = gst_element_factory_make (factory, name);
  } else {
    if (name)
      e = g_object_new (type, "name", name, NULL);
    else
      e = g_object_new (type, NULL);
  }
  if (e == NULL)
    goto create_failed;

  if (!gst_bin_add (GST_BIN (dvdbin), e))
    goto add_failed;

  GST_DEBUG_OBJECT (dvdbin, "Added %s element: %" GST_PTR_FORMAT, descr, e);

  DVDBIN_LOCK (dvdbin);
  dvdbin->pieces[index] = e;
  DVDBIN_UNLOCK (dvdbin);

  return TRUE;
create_failed:
  GST_ELEMENT_ERROR (dvdbin, CORE, MISSING_PLUGIN, (NULL),
      ("Could not create %s element '%s'", descr, factory));
  return FALSE;
add_failed:
  gst_object_unref (e);
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Could not add %s element to bin", descr));
  return FALSE;
}

static gboolean
create_elements (RsnDvdBin * dvdbin)
{
  GstPad *src = NULL;
  GstPad *sink = NULL;

  if (!try_create_piece (dvdbin, DVD_ELEM_SOURCE, NULL,
          RESIN_TYPE_DVDSRC, "dvdsrc", "DVD source")) {
    return FALSE;
  }

  /* FIXME: Locking */
  if (dvdbin->device) {
    g_object_set (G_OBJECT (dvdbin->pieces[DVD_ELEM_SOURCE]),
        "device", dvdbin->device, NULL);
  }

  if (!try_create_piece (dvdbin, DVD_ELEM_DEMUX,
          NULL, GST_TYPE_FLUPS_DEMUX, "dvddemux", "DVD demuxer"))
    return FALSE;

  if (gst_element_link (dvdbin->pieces[DVD_ELEM_SOURCE],
          dvdbin->pieces[DVD_ELEM_DEMUX]) == FALSE)
    goto failed_connect;

  /* Listen for new pads from the demuxer */
  g_signal_connect (G_OBJECT (dvdbin->pieces[DVD_ELEM_DEMUX]), "pad-added",
      G_CALLBACK (demux_pad_added), dvdbin);

  if (!try_create_piece (dvdbin, DVD_ELEM_MQUEUE, "multiqueue", 0, "mq",
          "multiqueue"))
    return FALSE;

  g_object_set (dvdbin->pieces[DVD_ELEM_MQUEUE],
      "max-size-time", (7 * GST_SECOND / 10), "max-size-bytes", 0,
      "max-size-buffers", 0, NULL);

  /* Decodebin will throw a missing element message to find an MPEG decoder */
  if (!try_create_piece (dvdbin, DVD_ELEM_VIDDEC, "decodebin", 0, "viddec",
          "video decoder"))
    return FALSE;

  g_signal_connect (G_OBJECT (dvdbin->pieces[DVD_ELEM_VIDDEC]),
      "new-decoded-pad", G_CALLBACK (viddec_pad_added), dvdbin);

  if (!try_create_piece (dvdbin, DVD_ELEM_PARSET, NULL, RSN_TYPE_RSNPARSETTER,
          "rsnparsetter", "Aspect ratio adjustment"))
    return FALSE;

#if USE_VIDEOQ
  /* Add a small amount of queueing after the video decoder. */
  if (!try_create_piece (dvdbin, DVD_ELEM_VIDQ, "queue", 0, "vid_q",
          "video decoder buffer"))
    return FALSE;
  g_object_set (dvdbin->pieces[DVD_ELEM_VIDQ],
      "max-size-time", G_GUINT64_CONSTANT (0), "max-size-bytes", 0,
      "max-size-buffers", 3, NULL);

  src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_PARSET], "src");
  sink = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_VIDQ], "sink");
  if (src == NULL || sink == NULL)
    goto failed_vidq_connect;
  if (GST_PAD_LINK_FAILED (gst_pad_link (src, sink)))
    goto failed_vidq_connect;
  gst_object_unref (src);
  gst_object_unref (sink);
  src = sink = NULL;

  src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_VIDQ], "src");
#else
  src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_PARSET], "src");
#endif
  if (src == NULL)
    goto failed_video_ghost;
  dvdbin->video_pad = gst_ghost_pad_new ("video", src);
  if (dvdbin->video_pad == NULL)
    goto failed_video_ghost;
  gst_object_unref (src);
  src = NULL;

  if (!try_create_piece (dvdbin, DVD_ELEM_SPU_SELECT, NULL,
          RSN_TYPE_STREAM_SELECTOR, "subpselect", "Subpicture stream selector"))
    return FALSE;

  src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_SPU_SELECT], "src");
  if (src == NULL)
    goto failed_spu_ghost;
  dvdbin->subpicture_pad = gst_ghost_pad_new ("subpicture", src);
  if (dvdbin->subpicture_pad == NULL)
    goto failed_spu_ghost;
  gst_pad_set_active (dvdbin->subpicture_pad, TRUE);
  gst_pad_set_blocked_async (dvdbin->subpicture_pad, TRUE,
      (GstPadBlockCallback) dvdbin_pad_blocked_cb, dvdbin);
  gst_object_unref (src);
  src = NULL;

  if (!try_create_piece (dvdbin, DVD_ELEM_AUD_SELECT, NULL,
          RSN_TYPE_STREAM_SELECTOR, "audioselect", "Audio stream selector"))
    return FALSE;

  /* rsnaudiomunge goes after the audio decoding to regulate the stream */
  if (!try_create_piece (dvdbin, DVD_ELEM_AUD_MUNGE, NULL,
          RSN_TYPE_AUDIOMUNGE, "audiomunge", "Audio output filter"))
    return FALSE;

#if DECODEBIN_AUDIO
  /* Decodebin will throw a missing element message to find a suitable
   * decoder */
  if (!try_create_piece (dvdbin, DVD_ELEM_AUDDEC, "decodebin", 0, "auddec",
          "audio decoder"))
    return FALSE;

  g_signal_connect (G_OBJECT (dvdbin->pieces[DVD_ELEM_AUDDEC]),
      "new-decoded-pad", G_CALLBACK (auddec_pad_added), dvdbin);
#else
  if (!try_create_piece (dvdbin, DVD_ELEM_AUDDEC, "a52dec", 0, "auddec",
          "audio decoder"))
    return FALSE;

  src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_AUDDEC], "src");
  sink =
      gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_AUD_MUNGE], "sink");
  if (src == NULL || sink == NULL)
    goto failed_aud_connect;
  if (GST_PAD_LINK_FAILED (gst_pad_link (src, sink)))
    goto failed_aud_connect;
  gst_object_unref (sink);
  gst_object_unref (src);
  src = sink = NULL;
#endif

  src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_AUD_SELECT], "src");
  sink = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_AUDDEC], "sink");
  if (src == NULL || sink == NULL)
    goto failed_aud_connect;
  if (GST_PAD_LINK_FAILED (gst_pad_link (src, sink)))
    goto failed_aud_connect;
  gst_object_unref (sink);
  gst_object_unref (src);
  src = sink = NULL;

  /* ghost audio munge output pad onto bin */
  src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_AUD_MUNGE], "src");
  if (src == NULL)
    goto failed_aud_ghost;
  dvdbin->audio_pad = gst_ghost_pad_new ("audio", src);
  if (dvdbin->audio_pad == NULL)
    goto failed_aud_ghost;
  gst_pad_set_active (dvdbin->audio_pad, TRUE);
  gst_pad_set_blocked_async (dvdbin->audio_pad, TRUE,
      (GstPadBlockCallback) dvdbin_pad_blocked_cb, dvdbin);
  gst_object_unref (src);
  src = NULL;

  return TRUE;

failed_connect:
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Could not connect DVD source and demuxer elements"));
  goto error_out;
#if USE_VIDEOQ
failed_vidq_connect:
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Could not connect DVD aspect ratio adjuster and video buffer elements"));
  goto error_out;
#endif
failed_video_ghost:
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Could not ghost SPU output pad"));
  goto error_out;
failed_spu_ghost:
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Could not ghost SPU output pad"));
  goto error_out;
failed_aud_connect:
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Could not connect DVD audio decoder"));
  goto error_out;
failed_aud_ghost:
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Could not ghost audio output pad"));
  goto error_out;
error_out:
  if (src != NULL)
    gst_object_unref (src);
  if (sink != NULL)
    gst_object_unref (sink);
  return FALSE;
}

static void
remove_elements (RsnDvdBin * dvdbin)
{
  gint i;
  GList *tmp;

  if (dvdbin->pieces[DVD_ELEM_MQUEUE] != NULL) {
    for (tmp = dvdbin->mq_req_pads; tmp; tmp = g_list_next (tmp)) {
      gst_element_release_request_pad (dvdbin->pieces[DVD_ELEM_MQUEUE],
          GST_PAD (tmp->data));
    }
  }
  g_list_free (dvdbin->mq_req_pads);
  dvdbin->mq_req_pads = NULL;

  for (i = 0; i < DVD_ELEM_LAST; i++) {
    DVDBIN_LOCK (dvdbin);
    if (dvdbin->pieces[i] != NULL) {
      GstElement *piece = dvdbin->pieces[i];

      dvdbin->pieces[i] = NULL;
      DVDBIN_UNLOCK (dvdbin);

      gst_element_set_state (piece, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (dvdbin), piece);
    } else
      DVDBIN_UNLOCK (dvdbin);
  }
  if (dvdbin->video_pad) {
    if (dvdbin->video_added)
      gst_element_remove_pad (GST_ELEMENT (dvdbin), dvdbin->video_pad);
    else
      gst_object_unref (dvdbin->video_pad);
  }
  if (dvdbin->audio_pad) {
    if (dvdbin->audio_added)
      gst_element_remove_pad (GST_ELEMENT (dvdbin), dvdbin->audio_pad);
    else
      gst_object_unref (dvdbin->audio_pad);
  }
  if (dvdbin->subpicture_pad) {
    if (dvdbin->subpicture_added)
      gst_element_remove_pad (GST_ELEMENT (dvdbin), dvdbin->subpicture_pad);
    else
      gst_object_unref (dvdbin->subpicture_pad);
  }

  dvdbin->video_added = dvdbin->audio_added = dvdbin->subpicture_added = FALSE;
  dvdbin->video_pad = dvdbin->audio_pad = dvdbin->subpicture_pad = NULL;
}

static GstPad *
connect_thru_mq (RsnDvdBin * dvdbin, GstPad * pad)
{
  GstPad *mq_sink;
  GstPad *mq_src;
  gchar *tmp, *sinkname, *srcname;

  /* Request a pad from multiqueue, then connect this one, then
   * discover the corresponding output pad and return it */
  mq_sink = gst_element_get_request_pad (dvdbin->pieces[DVD_ELEM_MQUEUE],
      "sink%d");
  if (mq_sink == NULL)
    return FALSE;
  dvdbin->mq_req_pads = g_list_prepend (dvdbin->mq_req_pads, mq_sink);

  if (gst_pad_link (pad, mq_sink) != GST_PAD_LINK_OK)
    return FALSE;

  sinkname = gst_pad_get_name (mq_sink);
  tmp = sinkname + 4;
  srcname = g_strdup_printf ("src%s", tmp);

  mq_src = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_MQUEUE],
      srcname);

  g_free (sinkname);
  g_free (srcname);

  return mq_src;
}

static void
demux_pad_added (GstElement * element, GstPad * pad, RsnDvdBin * dvdbin)
{
  gboolean skip_mq = FALSE;
  GstPad *mq_pad = NULL;
  GstPad *dest_pad = NULL;
  GstCaps *caps;
  GstStructure *s;

  GST_DEBUG_OBJECT (dvdbin, "New pad: %" GST_PTR_FORMAT, pad);

  caps = gst_pad_get_caps (pad);
  if (caps == NULL) {
    GST_WARNING_OBJECT (dvdbin, "NULL caps from pad %" GST_PTR_FORMAT, pad);
    return;
  }
  if (!gst_caps_is_fixed (caps)) {
    GST_WARNING_OBJECT (dvdbin, "Unfixed caps %" GST_PTR_FORMAT
        " on pad %" GST_PTR_FORMAT, caps, pad);
    gst_caps_unref (caps);
    return;
  }

  s = gst_caps_get_structure (caps, 0);
  g_return_if_fail (s != NULL);

  if (g_str_equal (gst_structure_get_name (s), "video/mpeg")) {
    dest_pad =
        gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_VIDDEC], "sink");
  } else if (g_str_equal (gst_structure_get_name (s), "video/x-dvd-subpicture")) {
    dest_pad =
        gst_element_get_request_pad (dvdbin->pieces[DVD_ELEM_SPU_SELECT],
        "sink%d");
    skip_mq = TRUE;
  } else if (g_str_equal (gst_structure_get_name (s), "audio/x-private1-ac3")) {
    dest_pad =
        gst_element_get_request_pad (dvdbin->pieces[DVD_ELEM_AUD_SELECT],
        "sink%d");
  }

  gst_caps_unref (caps);

  if (dest_pad == NULL) {
    GST_DEBUG_OBJECT (dvdbin, "Don't know how to handle pad. Ignoring");
    return;
  }

  if (skip_mq) {
    mq_pad = gst_object_ref (pad);
  } else {
    mq_pad = connect_thru_mq (dvdbin, pad);
    if (mq_pad == NULL)
      goto failed;
    GST_DEBUG_OBJECT (dvdbin, "Linking new pad %" GST_PTR_FORMAT
        " through multiqueue to %" GST_PTR_FORMAT, pad, dest_pad);
  }

  gst_pad_link (mq_pad, dest_pad);

  gst_object_unref (mq_pad);
  gst_object_unref (dest_pad);

  return;
failed:
  GST_ELEMENT_ERROR (dvdbin, CORE, FAILED, (NULL),
      ("Failed to handle new demuxer pad %s", GST_PAD_NAME (pad)));
  if (mq_pad)
    gst_object_unref (mq_pad);
  if (dest_pad)
    gst_object_unref (dest_pad);
  return;
}

static void
dvdbin_pad_blocked_cb (GstPad * pad, gboolean blocked, RsnDvdBin * dvdbin)
{
  gboolean changed = FALSE;
  if (!blocked)
    return;

  if (pad == dvdbin->subpicture_pad) {
    if (!dvdbin->subpicture_added) {
      gst_element_add_pad (GST_ELEMENT (dvdbin), dvdbin->subpicture_pad);
      dvdbin->subpicture_added = TRUE;
      changed = TRUE;
    }

    gst_pad_set_blocked_async (pad, FALSE,
        (GstPadBlockCallback) dvdbin_pad_blocked_cb, dvdbin);
  } else if (pad == dvdbin->audio_pad) {
    if (!dvdbin->audio_added) {
      gst_element_add_pad (GST_ELEMENT (dvdbin), dvdbin->audio_pad);
      dvdbin->audio_added = TRUE;
      changed = TRUE;
    }

    gst_pad_set_blocked_async (pad, FALSE,
        (GstPadBlockCallback) dvdbin_pad_blocked_cb, dvdbin);
  }

  if (changed &&
      dvdbin->video_added && dvdbin->audio_added && dvdbin->subpicture_added) {
    gst_element_no_more_pads (GST_ELEMENT (dvdbin));
  }
}

static void
viddec_pad_added (GstElement * element, GstPad * pad, gboolean last,
    RsnDvdBin * dvdbin)
{
  GstPad *q_pad;

  GST_DEBUG_OBJECT (dvdbin, "New video pad: %" GST_PTR_FORMAT, pad);

  q_pad = gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_PARSET], "sink");
  gst_pad_link (pad, q_pad);

  gst_object_unref (q_pad);

  if (!dvdbin->video_added) {
    gst_pad_set_active (dvdbin->video_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (dvdbin), dvdbin->video_pad);
    dvdbin->video_added = TRUE;

    if (dvdbin->video_added && dvdbin->audio_added && dvdbin->subpicture_added) {
      gst_element_no_more_pads (GST_ELEMENT (dvdbin));
    }
  }
}

#if DECODEBIN_AUDIO
static void
auddec_pad_added (GstElement * element, GstPad * pad, gboolean last,
    RsnDvdBin * dvdbin)
{
  GstPad *out_pad;

  GST_DEBUG_OBJECT (dvdbin, "New audio pad: %" GST_PTR_FORMAT, pad);

  out_pad =
      gst_element_get_static_pad (dvdbin->pieces[DVD_ELEM_AUD_MUNGE], "sink");
  gst_pad_link (pad, out_pad);

  gst_object_unref (out_pad);
}
#endif

static void
rsn_dvdbin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  RsnDvdBin *dvdbin = RESINDVDBIN (object);

  switch (prop_id) {
    case ARG_DEVICE:
      DVDBIN_LOCK (dvdbin);
      g_free (dvdbin->device);
      if (g_value_get_string (value) == NULL)
        dvdbin->device = g_strdup (DEFAULT_DEVICE);
      else
        dvdbin->device = g_value_dup_string (value);

      if (dvdbin->pieces[DVD_ELEM_SOURCE]) {
        g_object_set_property (G_OBJECT (dvdbin->pieces[DVD_ELEM_SOURCE]),
            "device", value);
      }
      DVDBIN_UNLOCK (dvdbin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rsn_dvdbin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  RsnDvdBin *dvdbin = RESINDVDBIN (object);

  switch (prop_id) {
    case ARG_DEVICE:
      DVDBIN_LOCK (dvdbin);
      if (dvdbin->device)
        g_value_set_string (value, dvdbin->device);
      else if (dvdbin->pieces[DVD_ELEM_SOURCE])
        g_object_get_property (G_OBJECT (dvdbin->pieces[DVD_ELEM_SOURCE]),
            "device", value);
      else
        g_value_set_string (value, DEFAULT_DEVICE);
      DVDBIN_UNLOCK (dvdbin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
rsn_dvdbin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  RsnDvdBin *dvdbin = RESINDVDBIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!create_elements (dvdbin)) {
        remove_elements (dvdbin);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      remove_elements (dvdbin);
      break;
    default:
      break;
  }

  return ret;
}
