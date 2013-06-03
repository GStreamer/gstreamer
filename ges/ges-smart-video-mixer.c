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

#include "gstframepositionner.h"
#include "ges-types.h"
#include "ges-internal.h"
#include "ges-smart-video-mixer.h"

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

  if (infos->mixer_pad)
    gst_element_release_request_pad (infos->self->mixer, infos->mixer_pad);

  g_slice_free (PadInfos, infos);
}

/* These metadata will get set by the upstream framepositionner element,
   added in the video sources' bin */
static GstPadProbeReturn
parse_metadata (GstPad * mixer_pad, GstPadProbeInfo * info, gpointer unused)
{
  GstFramePositionnerMeta *meta;

  meta =
      (GstFramePositionnerMeta *) gst_buffer_get_meta ((GstBuffer *) info->data,
      gst_frame_positionner_meta_api_get_type ());

  if (!meta) {
    GST_WARNING ("The current source should use a framepositionner");
    return GST_PAD_PROBE_OK;
  }

  g_object_set (mixer_pad, "alpha", meta->alpha, "xpos", meta->posx, "ypos",
      meta->posy, "zorder", meta->zorder, NULL);

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
  ghost = gst_ghost_pad_new (NULL, tmpghost);
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
      (GstPadProbeCallback) parse_metadata, NULL, NULL);

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
  GST_DEBUG_OBJECT (element, "Releasing pad %" GST_PTR_FORMAT, pad);

  LOCK (element);
  g_hash_table_remove (GES_SMART_MIXER (element)->pads_infos, pad);
  UNLOCK (element);
}

/****************************************************
 *              GObject vmethods                    *
 ****************************************************/
static void
ges_smart_mixer_finalize (GObject * object)
{
  GESSmartMixer *self = GES_SMART_MIXER (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (ges_smart_mixer_parent_class)->finalize (object);
}

static void
ges_smart_mixer_class_init (GESSmartMixerClass * klass)
{
/*   GstBinClass *parent_class = GST_BIN_CLASS (klass);
 */
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* FIXME Make sure the MixerClass doesn get destroy before ourself */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_static_metadata (element_class, "GES Smart mixer",
      "Generic/Audio",
      "Use mixer making use of GES informations",
      "Thibault Saunier <thibault.saunier@collabora.com>");

  element_class->request_new_pad = GST_DEBUG_FUNCPTR (_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (_release_pad);

  object_class->finalize = ges_smart_mixer_finalize;
}

static void
ges_smart_mixer_init (GESSmartMixer * self)
{
  GstPad *pad;

  g_mutex_init (&self->lock);

  self->mixer = gst_element_factory_make ("videomixer", "smart-mixer-mixer");
  g_object_set (self->mixer, "background", 1, NULL);
  gst_bin_add (GST_BIN (self), self->mixer);

  pad = gst_element_get_static_pad (self->mixer, "src");
  self->srcpad = gst_ghost_pad_new ("src", pad);
  gst_pad_set_active (self->srcpad, TRUE);

  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->pads_infos = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) destroy_pad);
}

GstElement *
ges_smart_mixer_new (GESTrack * track)
{
  GESSmartMixer *self = g_object_new (GES_TYPE_SMART_MIXER, NULL);
  self->track = track;

  /* FIXME Make mixer smart and let it properly negotiate caps! */
  return GST_ELEMENT (self);
}
