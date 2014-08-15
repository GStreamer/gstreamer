/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gessource
 * @short_description: Base Class for single-media sources
 */

#include "ges-internal.h"
#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-source.h"
#include "ges-layer.h"
#include "gstframepositionner.h"

G_DEFINE_TYPE (GESSource, ges_source, GES_TYPE_TRACK_ELEMENT);

struct _GESSourcePrivate
{
  /*  Dummy variable */
  GstFramePositionner *positionner;
};

/******************************
 *   Internal helper methods  *
 ******************************/
static void
_pad_added_cb (GstElement * element, GstPad * srcpad, GstPad * sinkpad)
{
  gst_element_no_more_pads (element);
  gst_pad_link (srcpad, sinkpad);
}

static void
_ghost_pad_added_cb (GstElement * element, GstPad * srcpad, GstElement * bin)
{
  GstPad *ghost;

  ghost = gst_ghost_pad_new ("src", srcpad);
  gst_pad_set_active (ghost, TRUE);
  gst_element_add_pad (bin, ghost);
  gst_element_no_more_pads (element);
}

GstElement *
ges_source_create_topbin (const gchar * bin_name, GstElement * sub_element, ...)
{
  va_list argp;

  GstElement *element;
  GstElement *prev = NULL;
  GstElement *first = NULL;
  GstElement *bin;
  GstPad *sub_srcpad;

  va_start (argp, sub_element);
  bin = gst_bin_new (bin_name);
  gst_bin_add (GST_BIN (bin), sub_element);

  while ((element = va_arg (argp, GstElement *)) != NULL) {
    gst_bin_add (GST_BIN (bin), element);
    if (prev)
      gst_element_link (prev, element);
    prev = element;
    if (first == NULL)
      first = element;
  }

  va_end (argp);

  sub_srcpad = gst_element_get_static_pad (sub_element, "src");

  if (prev != NULL) {
    GstPad *srcpad, *sinkpad, *ghost;

    srcpad = gst_element_get_static_pad (prev, "src");
    ghost = gst_ghost_pad_new ("src", srcpad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (bin, ghost);

    sinkpad = gst_element_get_static_pad (first, "sink");
    if (sub_srcpad)
      gst_pad_link (sub_srcpad, sinkpad);
    else
      g_signal_connect (sub_element, "pad-added", G_CALLBACK (_pad_added_cb),
          sinkpad);

    gst_object_unref (srcpad);
    gst_object_unref (sinkpad);

  } else if (sub_srcpad) {
    GstPad *ghost;

    ghost = gst_ghost_pad_new ("src", sub_srcpad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (bin, ghost);
  } else {
    g_signal_connect (sub_element, "pad-added",
        G_CALLBACK (_ghost_pad_added_cb), bin);
  }

  if (sub_srcpad)
    gst_object_unref (sub_srcpad);

  return bin;
}

static void
ges_source_class_init (GESSourceClass * klass)
{
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESSourcePrivate));

  track_class->nleobject_factorytype = "nlesource";
  track_class->create_element = NULL;
}

static void
ges_source_init (GESSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_SOURCE, GESSourcePrivate);
}
