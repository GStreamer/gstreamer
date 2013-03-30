/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * gst-editing-services
 *
 * Copyright (C) 2013 Thibault Saunier <tsaunier@gnome.org>

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
#include <gst/audio/audio.h>

#include "ges-types.h"
#include "ges-internal.h"
#include "ges-smart-adder.h"

G_DEFINE_TYPE (GESSmartAdder, ges_smart_adder, GST_TYPE_BIN);

#define GET_LOCK(obj) (&((GESSmartAdder*)(obj))->lock)
#define LOCK(obj) (g_mutex_lock (GET_LOCK(obj)))
#define UNLOCK(obj) (g_mutex_unlock (GET_LOCK(obj)))

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw")
    );

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define DEFAULT_CAPS "audio/x-raw,format=(string)S32LE;"
#else
#define DEFAULT_CAPS "audio/x-raw,format=(string)S32BE;";
#endif

typedef struct _PadInfos
{
  GESSmartAdder *self;
  GstPad *ghost;
  GstPad *adder_pad;
  GstPad *parent_sinkpad;

  GstElement *volume;
  GstElement *audioconvert;
  GstElement *audioresample;
  GstElement *bin;

} PadInfos;

static void
destroy_pad_info (PadInfos * infos)
{
  GST_DEBUG_OBJECT (infos->self, "Destroying pad %" GST_PTR_FORMAT,
      infos->ghost);

  if (G_LIKELY (infos->bin)) {
    gst_element_set_state (infos->bin, GST_STATE_NULL);
    gst_element_unlink (infos->bin, infos->self->adder);
    gst_bin_remove (GST_BIN (infos->self), infos->bin);
  }

  if (infos->adder_pad)
    gst_element_release_request_pad (infos->self->adder, infos->adder_pad);

  g_slice_free (PadInfos, infos);
}

/****************************************************
 *              Callbacks                           *
 ****************************************************/
static void
_connected_to_gnlobject_cb (GstPad * pad, GstPad * peer, PadInfos * infos)
{
  GESTrack *track;
  GESLayer *layer;

  gfloat volume, track_volume, layer_volume;
  GstElement *gnlobject = gst_pad_get_parent_element (peer);
  GESTrackElement *track_element = g_object_get_qdata (G_OBJECT (gnlobject),
      GNL_OBJECT_TRACK_ELEMENT_QUARK);

  g_assert (track_element);
  g_signal_handlers_disconnect_by_func (pad, _connected_to_gnlobject_cb, infos);

  volume = track_volume = layer_volume = GES_META_VOLUME_DEFAULT;
  track = ges_track_element_get_track (track_element);
  layer = ges_clip_get_layer (GES_CLIP (GES_TIMELINE_ELEMENT_PARENT
          (track_element)));

  if (layer == NULL) {
    GST_WARNING ("TrackElement is in no layer");
    goto no_layer;
  }

  ges_meta_container_get_float (GES_META_CONTAINER (layer),
      GES_META_VOLUME, &layer_volume);
  gst_object_unref (layer);
  ges_meta_container_get_float (GES_META_CONTAINER (track),
      GES_META_VOLUME, &track_volume);

  volume = track_volume * layer_volume;
  g_object_set (infos->volume, "volume", volume, NULL);

no_layer:
  gst_object_unref (gnlobject);
}

/* Here we get the information that the pad is linked to a ghostpad GNL created,
 * what we want is to get notify when the gnloperation (ghost)sinkpad gets linked
 * to the pad of another gnlobject in the pipeline, so we can get the
 * GESTrackElement that wraps the gnlobject that is linked */
static void
_sink_pad_linked_cb (GstPad * adder_pad, GstProxyPad * peer, PadInfos * infos)
{
  GESSmartAdder *self = infos->self;
  /* The peer is a ProxyPad (inside ourself) that is linked to the gnloperation
   * ProxyPad, we want to get notify about the gnloperation ProxyPad connection
   */
  GstProxyPad *parent_sinkpad =
      gst_proxy_pad_get_internal (GST_PROXY_PAD (peer));

  LOCK (self);
  infos->parent_sinkpad = GST_PAD (parent_sinkpad);
  UNLOCK (self);

  g_signal_handlers_disconnect_by_func (adder_pad, _sink_pad_linked_cb, infos);
  g_signal_connect (parent_sinkpad, "linked",
      G_CALLBACK (_connected_to_gnlobject_cb), infos);
}


/****************************************************
 *              GstElement vmetods                  *
 ****************************************************/
static GstPad *
_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *volume_srcpad, *audioconvert_sinkpad, *tmpghost;

  PadInfos *infos = g_slice_new0 (PadInfos);
  GESSmartAdder *self = GES_SMART_ADDER (element);

  infos->adder_pad = gst_element_request_pad (self->adder, templ, NULL, caps);
  if (infos->adder_pad == NULL) {
    GST_WARNING_OBJECT (element, "Could not get any pad from GstAdder");

    return NULL;
  }
  infos->self = gst_object_ref (self);

  infos->bin = gst_bin_new (NULL);
  infos->audioconvert = gst_element_factory_make ("audioconvert", NULL);
  infos->audioresample = gst_element_factory_make ("audioresample", NULL);
  infos->volume = gst_element_factory_make ("volume", NULL);
  gst_bin_add_many (GST_BIN (infos->bin), infos->audioconvert,
      infos->audioresample, infos->volume, NULL);
  gst_element_link_many (infos->audioconvert, infos->audioresample,
      infos->volume, NULL);

  audioconvert_sinkpad = gst_element_get_static_pad (infos->audioconvert,
      "sink");
  tmpghost = GST_PAD (gst_ghost_pad_new (NULL, audioconvert_sinkpad));
  gst_object_unref (audioconvert_sinkpad);
  gst_pad_set_active (tmpghost, TRUE);
  gst_element_add_pad (GST_ELEMENT (infos->bin), tmpghost);

  gst_bin_add (GST_BIN (self), infos->bin);
  infos->ghost = gst_ghost_pad_new (NULL, tmpghost);
  gst_pad_set_active (infos->ghost, TRUE);
  if (!gst_element_add_pad (GST_ELEMENT (self), infos->ghost))
    goto could_not_add;


  volume_srcpad = gst_element_get_static_pad (infos->volume, "src");
  tmpghost = GST_PAD (gst_ghost_pad_new (NULL, volume_srcpad));
  gst_object_unref (volume_srcpad);
  gst_pad_set_active (tmpghost, TRUE);
  gst_element_add_pad (GST_ELEMENT (infos->bin), tmpghost);
  gst_pad_link (tmpghost, infos->adder_pad);

  LOCK (self);
  g_hash_table_insert (self->pads_infos, infos->ghost, infos);
  UNLOCK (self);

  g_signal_connect (infos->ghost, "linked",
      G_CALLBACK (_sink_pad_linked_cb), infos);

  GST_DEBUG_OBJECT (self, "Returning new pad %" GST_PTR_FORMAT, infos->ghost);
  return infos->ghost;

could_not_add:
  {
    GST_DEBUG_OBJECT (self, "could not add pad");
    destroy_pad_info (infos);
    return NULL;
  }
}

static void
_release_pad (GstElement * element, GstPad * pad)
{
  GST_DEBUG_OBJECT (element, "Releasing pad %" GST_PTR_FORMAT, pad);

  LOCK (element);
  g_hash_table_remove (GES_SMART_ADDER (element)->pads_infos, pad);
  UNLOCK (element);
}

/****************************************************
 *              GObject vmethods                    *
 ****************************************************/
static void
ges_smart_adder_finalize (GObject * object)
{
  GESSmartAdder *self = GES_SMART_ADDER (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (ges_smart_adder_parent_class)->finalize (object);
}

static void
ges_smart_adder_class_init (GESSmartAdderClass * klass)
{
/*   GstBinClass *parent_class = GST_BIN_CLASS (klass);
 */
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* FIXME Make sure the AdderClass doesn get destroy before ourself */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_static_metadata (element_class, "GES Smart adder",
      "Generic/Audio",
      "Use adder making use of GES informations",
      "Thibault Saunier <thibault.saunier@collabora.com>");

  element_class->request_new_pad = GST_DEBUG_FUNCPTR (_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (_release_pad);

  object_class->finalize = ges_smart_adder_finalize;
}

static void
ges_smart_adder_init (GESSmartAdder * self)
{
  GstPad *pad;

  g_mutex_init (&self->lock);

  self->adder = gst_element_factory_make ("adder", "smart-adder-adder");
  gst_bin_add (GST_BIN (self), self->adder);

  pad = gst_element_get_static_pad (self->adder, "src");
  self->srcpad = gst_ghost_pad_new ("src", pad);
  gst_pad_set_active (self->srcpad, TRUE);

  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->pads_infos = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) destroy_pad_info);
}

GstElement *
ges_smart_adder_new (GESTrack * track)
{
  GESSmartAdder *self = g_object_new (GES_TYPE_SMART_ADDER, NULL);
  self->track = track;

  /* FIXME Make adder smart and let it properly negotiate caps! */
  g_object_set (self->adder, "caps", gst_caps_from_string (DEFAULT_CAPS), NULL);
  return GST_ELEMENT (self);
}
