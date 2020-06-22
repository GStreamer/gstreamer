/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * gst-editing-services
 *
 * Copyright (C) 2013 Mathieu Duponchelle <mduponchelle1@gmail.com>
 * gst-editing-services is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gst-editing-services is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstframepositioner.h"
#include "ges-types.h"
#include "ges-internal.h"
#include "ges-smart-video-mixer.h"

#define GES_TYPE_SMART_MIXER_PAD             (ges_smart_mixer_pad_get_type ())
typedef struct _GESSmartMixerPad GESSmartMixerPad;
typedef struct _GESSmartMixerPadClass GESSmartMixerPadClass;
GES_DECLARE_TYPE (SmartMixerPad, smart_mixer_pad, SMART_MIXER_PAD);

struct _GESSmartMixerPad
{
  GstGhostPad parent;

  gdouble alpha;
  GstSegment segment;
};

struct _GESSmartMixerPadClass
{
  GstGhostPadClass parent_class;
};

enum
{
  PROP_PAD_0,
  PROP_PAD_ALPHA,
};

G_DEFINE_TYPE (GESSmartMixerPad, ges_smart_mixer_pad, GST_TYPE_GHOST_PAD);

static void
ges_smart_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GESSmartMixerPad *pad = GES_SMART_MIXER_PAD (object);

  switch (prop_id) {
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
ges_smart_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GESSmartMixerPad *pad = GES_SMART_MIXER_PAD (object);

  switch (prop_id) {
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
ges_smart_mixer_pad_init (GESSmartMixerPad * self)
{
  gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
}

static void
ges_smart_mixer_pad_class_init (GESSmartMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = ges_smart_mixer_pad_get_property;
  gobject_class->set_property = ges_smart_mixer_pad_set_property;

  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          1.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

G_DEFINE_TYPE (GESSmartMixer, ges_smart_mixer, GST_TYPE_BIN);

#define GET_LOCK(obj) (&((GESSmartMixer*)(obj))->lock)
#define LOCK(obj) (g_mutex_lock (GET_LOCK(obj)))
#define UNLOCK(obj) (g_mutex_unlock (GET_LOCK(obj)))

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw")
    );

typedef struct _PadInfos
{
  GESSmartMixer *self;
  GstPad *mixer_pad;
  GstElement *bin;
  gulong probe_id;
} PadInfos;

static void
destroy_pad (PadInfos * infos)
{
  gst_pad_remove_probe (infos->mixer_pad, infos->probe_id);

  if (G_LIKELY (infos->bin)) {
    gst_element_set_state (infos->bin, GST_STATE_NULL);
    gst_element_unlink (infos->bin, infos->self->mixer);
    gst_bin_remove (GST_BIN (infos->self), infos->bin);
  }

  if (infos->mixer_pad) {
    gst_element_release_request_pad (infos->self->mixer, infos->mixer_pad);
    gst_object_unref (infos->mixer_pad);
  }

  g_slice_free (PadInfos, infos);
}

static gboolean
ges_smart_mixer_sinkpad_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *seg;

      gst_event_parse_segment (event, &seg);

      GST_OBJECT_LOCK (pad);
      ((GESSmartMixerPad *) pad)->segment = *seg;
      GST_OBJECT_UNLOCK (pad);
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

GstPad *
ges_smart_mixer_get_mixer_pad (GESSmartMixer * self, GstPad ** mixerpad)
{
  PadInfos *info;
  GstPad *sinkpad;

  sinkpad = gst_element_get_request_pad (GST_ELEMENT (self), "sink_%u");

  if (sinkpad == NULL)
    return NULL;

  info = g_hash_table_lookup (self->pads_infos, sinkpad);
  *mixerpad = gst_object_ref (info->mixer_pad);

  return sinkpad;
}

/* These metadata will get set by the upstream framepositioner element,
   added in the video sources' bin */
static GstPadProbeReturn
parse_metadata (GstPad * mixer_pad, GstPadProbeInfo * info,
    GESSmartMixerPad * ghost)
{
  GstFramePositionerMeta *meta;
  GESSmartMixer *self = GES_SMART_MIXER (GST_OBJECT_PARENT (ghost));

  meta =
      (GstFramePositionerMeta *) gst_buffer_get_meta ((GstBuffer *) info->data,
      gst_frame_positioner_meta_api_get_type ());

  if (!meta) {
    GST_WARNING ("The current source should use a framepositioner");
    return GST_PAD_PROBE_OK;
  }

  if (!self->disable_zorder_alpha) {
    g_object_set (mixer_pad, "alpha", meta->alpha,
        "zorder", meta->zorder, NULL);
  } else {
    gint64 stream_time;
    gdouble transalpha;

    GST_OBJECT_LOCK (ghost);
    stream_time = gst_segment_to_stream_time (&ghost->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (info->data));
    GST_OBJECT_UNLOCK (ghost);

    if (GST_CLOCK_TIME_IS_VALID (stream_time))
      gst_object_sync_values (GST_OBJECT (ghost), stream_time);

    g_object_get (ghost, "alpha", &transalpha, NULL);
    g_object_set (mixer_pad, "alpha", meta->alpha * transalpha, NULL);
  }

  g_object_set (mixer_pad, "xpos", meta->posx, "ypos",
      meta->posy, "width", meta->width, "height", meta->height, NULL);

  return GST_PAD_PROBE_OK;
}

/****************************************************
 *              GstElement vmetods                  *
 ****************************************************/
static GstPad *
_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *videoconvert_srcpad, *videoconvert_sinkpad, *tmpghost;
  PadInfos *infos = g_slice_new0 (PadInfos);
  GESSmartMixer *self = GES_SMART_MIXER (element);
  GstPad *ghost;
  GstElement *videoconvert;

  infos->mixer_pad = gst_element_request_pad (self->mixer,
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self->mixer),
          "sink_%u"), NULL, NULL);

  if (infos->mixer_pad == NULL) {
    GST_WARNING_OBJECT (element, "Could not get any pad from GstMixer");
    g_slice_free (PadInfos, infos);

    return NULL;
  }

  infos->self = self;

  infos->bin = gst_bin_new (NULL);
  videoconvert = gst_element_factory_make ("videoconvert", NULL);

  gst_bin_add (GST_BIN (infos->bin), videoconvert);

  videoconvert_sinkpad = gst_element_get_static_pad (videoconvert, "sink");
  tmpghost = GST_PAD (gst_ghost_pad_new (NULL, videoconvert_sinkpad));
  gst_object_unref (videoconvert_sinkpad);
  gst_pad_set_active (tmpghost, TRUE);
  gst_element_add_pad (GST_ELEMENT (infos->bin), tmpghost);

  gst_bin_add (GST_BIN (self), infos->bin);
  ghost = g_object_new (ges_smart_mixer_pad_get_type (), "name", name,
      "direction", GST_PAD_DIRECTION (tmpghost), NULL);
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ghost), tmpghost);
  gst_pad_set_active (ghost, TRUE);
  if (!gst_element_add_pad (GST_ELEMENT (self), ghost))
    goto could_not_add;

  videoconvert_srcpad = gst_element_get_static_pad (videoconvert, "src");
  tmpghost = GST_PAD (gst_ghost_pad_new (NULL, videoconvert_srcpad));
  gst_object_unref (videoconvert_srcpad);
  gst_pad_set_active (tmpghost, TRUE);
  gst_element_add_pad (GST_ELEMENT (infos->bin), tmpghost);
  gst_pad_link (tmpghost, infos->mixer_pad);

  infos->probe_id =
      gst_pad_add_probe (infos->mixer_pad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) parse_metadata, ghost, NULL);
  gst_pad_set_event_function (GST_PAD (ghost),
      ges_smart_mixer_sinkpad_event_func);

  LOCK (self);
  g_hash_table_insert (self->pads_infos, ghost, infos);
  UNLOCK (self);

  GST_DEBUG_OBJECT (self, "Returning new pad %" GST_PTR_FORMAT, ghost);
  return ghost;

could_not_add:
  {
    GST_ERROR_OBJECT (self, "could not add pad");
    destroy_pad (infos);
    return NULL;
  }
}

static void
_release_pad (GstElement * element, GstPad * pad)
{
  GstPad *peer;
  GST_DEBUG_OBJECT (element, "Releasing pad %" GST_PTR_FORMAT, pad);

  LOCK (element);
  g_hash_table_remove (GES_SMART_MIXER (element)->pads_infos, pad);
  peer = gst_pad_get_peer (pad);
  if (peer) {
    gst_pad_unlink (peer, pad);

    gst_object_unref (peer);
  }
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);
  UNLOCK (element);
}

/****************************************************
 *              GObject vmethods                    *
 ****************************************************/
static void
ges_smart_mixer_dispose (GObject * object)
{
  GESSmartMixer *self = GES_SMART_MIXER (object);

  if (self->pads_infos != NULL) {
    g_hash_table_unref (self->pads_infos);
    self->pads_infos = NULL;
  }

  G_OBJECT_CLASS (ges_smart_mixer_parent_class)->dispose (object);
}

static void
ges_smart_mixer_finalize (GObject * object)
{
  GESSmartMixer *self = GES_SMART_MIXER (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (ges_smart_mixer_parent_class)->finalize (object);
}

static void
ges_smart_mixer_constructed (GObject * obj)
{
  GstPad *pad;
  GstElement *identity;

  GESSmartMixer *self = GES_SMART_MIXER (obj);
  gchar *cname = g_strdup_printf ("%s-compositor", GST_OBJECT_NAME (self));

  self->mixer =
      gst_element_factory_create (ges_get_compositor_factory (), cname);
  g_free (cname);
  g_object_set (self->mixer, "background", 1, NULL);

  /* See https://gitlab.freedesktop.org/gstreamer/gstreamer/issues/310 */
  GST_FIXME ("Stop dropping allocation query when it is not required anymore.");
  identity = gst_element_factory_make ("identity", NULL);
  g_object_set (identity, "drop-allocation", TRUE, NULL);
  g_assert (identity);

  gst_bin_add_many (GST_BIN (self), self->mixer, identity, NULL);
  gst_element_link (self->mixer, identity);

  pad = gst_element_get_static_pad (identity, "src");
  self->srcpad = gst_ghost_pad_new ("src", pad);
  gst_pad_set_active (self->srcpad, TRUE);
  gst_object_unref (pad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}


static void
ges_smart_mixer_class_init (GESSmartMixerClass * klass)
{
/*   GstBinClass *parent_class = GST_BIN_CLASS (klass);
 */
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* FIXME Make sure the MixerClass doesn get destroy before ourself */
  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_set_static_metadata (element_class, "GES Smart mixer",
      "Generic/Audio",
      "Use mixer making use of GES informations",
      "Thibault Saunier <thibault.saunier@collabora.com>");

  element_class->request_new_pad = GST_DEBUG_FUNCPTR (_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (_release_pad);

  object_class->dispose = ges_smart_mixer_dispose;
  object_class->finalize = ges_smart_mixer_finalize;
  object_class->constructed = ges_smart_mixer_constructed;
}

static void
ges_smart_mixer_init (GESSmartMixer * self)
{
  g_mutex_init (&self->lock);
  self->pads_infos = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) destroy_pad);
}

GstElement *
ges_smart_mixer_new (GESTrack * track)
{
  GESSmartMixer *self = g_object_new (GES_TYPE_SMART_MIXER, NULL);

  /* FIXME Make mixer smart and let it properly negotiate caps! */
  return GST_ELEMENT (self);
}
