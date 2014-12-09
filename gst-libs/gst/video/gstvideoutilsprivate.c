/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include "gstvideoutilsprivate.h"

/**
 * __gst_video_element_proxy_getcaps:
 * @element: a #GstElement
 * @sinkpad: the element's sink #GstPad
 * @srcpad: the element's source #GstPad
 * @initial_caps: initial caps
 * @filter: filter caps
 *
 * Returns caps that express @initial_caps (or sink template caps if
 * @initial_caps == NULL) restricted to resolution/format/...
 * combinations supported by downstream elements (e.g. muxers).
 *
 * Returns: a #GstCaps owned by caller
 */
GstCaps *
__gst_video_element_proxy_getcaps (GstElement * element, GstPad * sinkpad,
    GstPad * srcpad, GstCaps * initial_caps, GstCaps * filter)
{
  GstCaps *templ_caps;
  GstCaps *allowed;
  GstCaps *fcaps, *filter_caps;
  gint i, j;

  /* Allow downstream to specify width/height/framerate/PAR constraints
   * and forward them upstream for video converters to handle
   */
  templ_caps = initial_caps ? gst_caps_ref (initial_caps) :
      gst_pad_get_pad_template_caps (sinkpad);
  allowed = gst_pad_get_allowed_caps (srcpad);

  if (!allowed || gst_caps_is_any (allowed)) {
    fcaps = templ_caps;
    goto done;
  } else if (gst_caps_is_empty (allowed)) {
    fcaps = gst_caps_ref (allowed);
    goto done;
  }

  GST_LOG_OBJECT (element, "template caps %" GST_PTR_FORMAT, templ_caps);
  GST_LOG_OBJECT (element, "allowed caps %" GST_PTR_FORMAT, allowed);

  filter_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (templ_caps); i++) {
    GQuark q_name =
        gst_structure_get_name_id (gst_caps_get_structure (templ_caps, i));

    for (j = 0; j < gst_caps_get_size (allowed); j++) {
      const GstStructure *allowed_s = gst_caps_get_structure (allowed, j);
      const GValue *val;
      GstStructure *s;

      s = gst_structure_new_id_empty (q_name);
      if ((val = gst_structure_get_value (allowed_s, "width")))
        gst_structure_set_value (s, "width", val);
      if ((val = gst_structure_get_value (allowed_s, "height")))
        gst_structure_set_value (s, "height", val);
      if ((val = gst_structure_get_value (allowed_s, "framerate")))
        gst_structure_set_value (s, "framerate", val);
      if ((val = gst_structure_get_value (allowed_s, "pixel-aspect-ratio")))
        gst_structure_set_value (s, "pixel-aspect-ratio", val);

      filter_caps = gst_caps_merge_structure (filter_caps, s);
    }
  }

  fcaps = gst_caps_intersect (filter_caps, templ_caps);
  gst_caps_unref (filter_caps);
  gst_caps_unref (templ_caps);

  if (filter) {
    GST_LOG_OBJECT (element, "intersecting with %" GST_PTR_FORMAT, filter);
    filter_caps = gst_caps_intersect (fcaps, filter);
    gst_caps_unref (fcaps);
    fcaps = filter_caps;
  }

done:
  gst_caps_replace (&allowed, NULL);

  GST_LOG_OBJECT (element, "proxy caps %" GST_PTR_FORMAT, fcaps);

  return fcaps;
}
