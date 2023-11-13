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
#include "ges-frame-composition-meta.h"
#include "ges-types.h"
#include "ges-internal.h"
#include "ges-smart-video-mixer.h"
#include <gst/base/base.h>

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
  gint refcount;

  GESSmartMixer *self;
  GstPad *mixer_pad;
  GstPad *ghostpad;
  GstPad *real_mixer_pad;
} PadInfos;

static void
pad_infos_unref (PadInfos * infos)
{
  if (g_atomic_int_dec_and_test (&infos->refcount)) {
    GST_DEBUG_OBJECT (infos->mixer_pad, "Releasing pad");
    if (infos->mixer_pad) {
      gst_element_release_request_pad (infos->self->mixer, infos->mixer_pad);
      gst_object_unref (infos->mixer_pad);
    }
    gst_clear_object (&infos->real_mixer_pad);

    g_free (infos);
  }
}

static PadInfos *
pad_infos_new (void)
{
  PadInfos *info = g_new0 (PadInfos, 1);
  g_atomic_int_set (&info->refcount, 1);

  return info;
}

static PadInfos *
pad_infos_ref (PadInfos * info)
{
  g_atomic_int_inc (&info->refcount);
  return info;
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

  sinkpad = gst_element_request_pad_simple (GST_ELEMENT (self), "sink_%u");

  if (sinkpad == NULL)
    return NULL;

  info = g_hash_table_lookup (self->pads_infos, sinkpad);
  *mixerpad = gst_object_ref (info->mixer_pad);

  return sinkpad;
}

static void
set_pad_properties_from_composition_meta (GstPad * mixer_pad,
    GstSample * sample, GESSmartMixerPad * ghost)
{
  GESFrameCompositionMeta *meta;
  GstBuffer *buf = gst_sample_get_buffer (sample);
  GESSmartMixer *self = GES_SMART_MIXER (GST_OBJECT_PARENT (ghost));

  meta =
      (GESFrameCompositionMeta *) gst_buffer_get_meta (buf,
      GES_TYPE_META_FRAME_COMPOSITION);

  if (!meta) {
    GST_WARNING ("The current source should use a framecomposition");
    return;
  }

  if (!self->is_transition) {
    g_object_set (mixer_pad, "alpha", meta->alpha,
        "zorder", meta->zorder, NULL);
  } else {
    gint64 stream_time;
    gdouble transalpha;

    stream_time = gst_segment_to_stream_time (gst_sample_get_segment (sample),
        GST_FORMAT_TIME, GST_BUFFER_PTS (buf));

    /* When used in a transition we aggregate the alpha value value if the
     * transition pad and the alpha value from upstream frame positioner */
    if (GST_CLOCK_TIME_IS_VALID (stream_time))
      gst_object_sync_values (GST_OBJECT (ghost), stream_time);

    g_object_get (ghost, "alpha", &transalpha, NULL);
    g_object_set (mixer_pad, "alpha", meta->alpha * transalpha, NULL);
  }

  g_object_set (mixer_pad, "xpos", meta->posx, "ypos",
      meta->posy, "width", meta->width, "height", meta->height, NULL);

  if (self->ABI.abi.has_operator)
    g_object_set (mixer_pad, "operator", meta->operator, NULL);
}

/****************************************************
 *              GstElement vmetods                  *
 ****************************************************/
static GstPad *
_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  PadInfos *infos = pad_infos_new ();
  GESSmartMixer *self = GES_SMART_MIXER (element);
  GstPad *ghost;
  gchar *mixer_pad_name;

  infos->mixer_pad = gst_element_request_pad (self->mixer,
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self->mixer),
          "sink_%u"), NULL, NULL);

  if (infos->mixer_pad == NULL) {
    GST_WARNING_OBJECT (element, "Could not get any pad from GstMixer");
    pad_infos_unref (infos);

    return NULL;
  }

  /* We can rely on this because the mixer bin uses the same name pad
     as the internal mixer when creating the ghost pad. */
  mixer_pad_name = gst_pad_get_name (infos->mixer_pad);
  infos->real_mixer_pad = gst_element_get_static_pad (self->real_mixer,
      mixer_pad_name);
  g_free (mixer_pad_name);
  if (infos->real_mixer_pad == NULL) {
    GST_WARNING_OBJECT (element, "Could not get the real mixer pad");
    pad_infos_unref (infos);

    return NULL;
  }

  infos->self = self;

  ghost = g_object_new (ges_smart_mixer_pad_get_type (), "name", name,
      "direction", GST_PAD_DIRECTION (infos->mixer_pad), NULL);
  infos->ghostpad = ghost;
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ghost), infos->mixer_pad);
  gst_pad_set_active (ghost, TRUE);
  if (!gst_element_add_pad (GST_ELEMENT (self), ghost))
    goto could_not_add;

  gst_pad_set_event_function (GST_PAD (ghost),
      ges_smart_mixer_sinkpad_event_func);

  LOCK (self);
  g_hash_table_insert (self->pads_infos, ghost, infos);
  g_hash_table_insert (self->pads_infos, infos->mixer_pad,
      pad_infos_ref (infos));
  g_hash_table_insert (self->pads_infos, infos->real_mixer_pad,
      pad_infos_ref (infos));
  UNLOCK (self);

  GST_DEBUG_OBJECT (self, "Returning new pad %" GST_PTR_FORMAT, ghost);
  return ghost;

could_not_add:
  {
    GST_ERROR_OBJECT (self, "could not add pad");
    pad_infos_unref (infos);
    return NULL;
  }
}

static PadInfos *
ges_smart_mixer_find_pad_info (GESSmartMixer * self, GstPad * pad)
{
  PadInfos *info;

  LOCK (self);
  info = g_hash_table_lookup (self->pads_infos, pad);
  UNLOCK (self);

  if (info)
    pad_infos_ref (info);

  return info;
}

static void
_release_pad (GstElement * element, GstPad * pad)
{
  GstPad *peer;
  GESSmartMixer *self = GES_SMART_MIXER (element);
  PadInfos *info = ges_smart_mixer_find_pad_info (self, pad);

  GST_DEBUG_OBJECT (element, "Releasing pad %" GST_PTR_FORMAT, pad);

  LOCK (element);
  g_hash_table_remove (GES_SMART_MIXER (element)->pads_infos, pad);
  g_hash_table_remove (GES_SMART_MIXER (element)->pads_infos, info->mixer_pad);
  g_hash_table_remove (GES_SMART_MIXER (element)->pads_infos,
      info->real_mixer_pad);
  peer = gst_pad_get_peer (pad);
  if (peer) {
    gst_pad_unlink (peer, pad);

    gst_object_unref (peer);
  }
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);
  UNLOCK (element);

  pad_infos_unref (info);
}

static gboolean
compositor_sync_properties_with_meta (GstElement * compositor,
    GstPad * sinkpad, GESSmartMixer * self)
{
  PadInfos *info = ges_smart_mixer_find_pad_info (self, sinkpad);
  GstSample *sample;

  if (!info) {
    GST_WARNING_OBJECT (self, "Couldn't find pad info?!");

    return TRUE;
  }

  sample = gst_aggregator_peek_next_sample (GST_AGGREGATOR (compositor),
      GST_AGGREGATOR_PAD (sinkpad));

  if (sample) {
    set_pad_properties_from_composition_meta (sinkpad,
        sample, GES_SMART_MIXER_PAD (info->ghostpad));
    gst_sample_unref (sample);
  } else {
    GST_INFO_OBJECT (sinkpad, "No sample set!");
  }
  pad_infos_unref (info);

  return TRUE;
}

static void
ges_smart_mixer_samples_selected_cb (GstElement * compositor,
    GstSegment * segment, GstClockTime pts, GstClockTime dts,
    GstClockTime duration, GstStructure * info, GESSmartMixer * self)
{
  gst_element_foreach_sink_pad (compositor,
      (GstElementForeachPadFunc) compositor_sync_properties_with_meta, self);
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
  gst_clear_object (&self->real_mixer);

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
  GstElement *identity, *videoconvert;
  GESSmartMixer *self = GES_SMART_MIXER (obj);
  gchar *cname = g_strdup_printf ("%s-compositor", GST_OBJECT_NAME (self));

  self->mixer =
      gst_element_factory_create (ges_get_compositor_factory (), cname);
  self->ABI.abi.has_operator =
      gst_compositor_operator_get_type_and_default_value (NULL) != G_TYPE_NONE;
  g_free (cname);

  if (GST_IS_BIN (self->mixer)) {
    g_object_get (self->mixer, "mixer", &self->real_mixer, NULL);
    g_assert (self->real_mixer);
  } else {
    self->real_mixer = gst_object_ref (self->mixer);
  }

  g_object_set (self->real_mixer, "background", 1, "emit-signals", TRUE, NULL);
  g_signal_connect (self->real_mixer, "samples-selected",
      G_CALLBACK (ges_smart_mixer_samples_selected_cb), self);

  /* See https://gitlab.freedesktop.org/gstreamer/gstreamer/issues/310 */
  GST_FIXME ("Stop dropping allocation query when it is not required anymore.");
  identity = gst_element_factory_make ("identity", NULL);
  g_object_set (identity, "drop-allocation", TRUE, NULL);
  g_assert (identity);

  videoconvert = gst_element_factory_make ("videoconvert", NULL);
  g_assert (videoconvert);

  gst_bin_add_many (GST_BIN (self), self->mixer, identity, videoconvert, NULL);
  gst_element_link_many (self->mixer, identity, videoconvert, NULL);

  pad = gst_element_get_static_pad (videoconvert, "src");
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
      "Use mixer making use of GES information",
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
      NULL, (GDestroyNotify) pad_infos_unref);
}

GstElement *
ges_smart_mixer_new (GESTrack * track)
{
  GESSmartMixer *self = g_object_new (GES_TYPE_SMART_MIXER, NULL);

  /* FIXME Make mixer smart and let it properly negotiate caps! */
  return GST_ELEMENT (self);
}
