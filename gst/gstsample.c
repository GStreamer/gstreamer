/* GStreamer
 * Copyright (C) 2011 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstsample.c: media sample
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

/**
 * SECTION:gstsample
 * @short_description: A media sample
 * @see_also: #GstBuffer, #GstCaps, #GstSegment
 *
 * A #GstSample is a small object containing data, a type, timing and
 * extra arbitrary information.
 *
 * Last reviewed on 2012-03-29 (0.11.3)
 */
#include "gst_private.h"

#include "gstsample.h"

struct _GstSample
{
  GstMiniObject mini_object;

  GstBuffer *buffer;
  GstCaps *caps;
  GstSegment segment;
  GstStructure *info;
};

GType _gst_sample_type = 0;

GST_DEFINE_MINI_OBJECT_TYPE (GstSample, gst_sample);

void
_priv_gst_sample_initialize (void)
{
  _gst_sample_type = gst_sample_get_type ();
}

static GstSample *
_gst_sample_copy (GstSample * sample)
{
  GstSample *copy;

  copy = gst_sample_new (sample->buffer, sample->caps, &sample->segment,
      gst_structure_copy (sample->info));

  return copy;
}

static void
_gst_sample_free (GstSample * sample)
{
  GST_LOG ("free %p", sample);

  if (sample->buffer)
    gst_buffer_unref (sample->buffer);
  if (sample->caps)
    gst_caps_unref (sample->caps);

  g_slice_free1 (GST_MINI_OBJECT_SIZE (sample), sample);
}

/**
 * gst_sample_new:
 * @buffer: a #GstBuffer
 * @caps: a #GstCaps
 * @segment: a #GstSegment
 * @info: a #GstStructure
 *
 * Create a new #GstSample with the provided details.
 *
 * Free-function: gst_sample_unref
 *
 * Returns: (transfer full): the new #GstSample. gst_sample_unref()
 *     after usage.
 *
 * Since: 0.10.24
 */
GstSample *
gst_sample_new (GstBuffer * buffer, GstCaps * caps, const GstSegment * segment,
    GstStructure * info)
{
  GstSample *sample;

  sample = g_slice_new0 (GstSample);

  GST_LOG ("new %p", sample);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (sample), _gst_sample_type,
      sizeof (GstSample));

  sample->mini_object.copy = (GstMiniObjectCopyFunction) _gst_sample_copy;
  sample->mini_object.free = (GstMiniObjectFreeFunction) _gst_sample_free;

  sample->buffer = buffer ? gst_buffer_ref (buffer) : NULL;
  sample->caps = caps ? gst_caps_ref (caps) : NULL;

  if (segment)
    gst_segment_copy_into (segment, &sample->segment);
  else
    gst_segment_init (&sample->segment, GST_FORMAT_TIME);

  if (info) {
    if (!gst_structure_set_parent_refcount (info,
            &sample->mini_object.refcount))
      goto had_parent;

    sample->info = info;
  }
  return sample;

  /* ERRORS */
had_parent:
  {
    gst_sample_unref (sample);
    g_warning ("structure is already owned by another object");
    return NULL;
  }
}

/**
 * gst_sample_get_buffer:
 * @sample: a #GstSample
 *
 * Get the buffer associated with @sample
 *
 * Returns: (transfer none): the buffer of @sample or NULL when there
 *  is no buffer. The buffer remains valid as long as @sample is valid.
 */
GstBuffer *
gst_sample_get_buffer (GstSample * sample)
{
  g_return_val_if_fail (GST_IS_SAMPLE (sample), NULL);

  return sample->buffer;
}

/**
 * gst_sample_get_caps:
 * @sample: a #GstSample
 *
 * Get the caps associated with @sample
 *
 * Returns: (transfer none): the caps of @sample or NULL when there
 *  is no caps. The caps remain valid as long as @sample is valid.
 */
GstCaps *
gst_sample_get_caps (GstSample * sample)
{
  g_return_val_if_fail (GST_IS_SAMPLE (sample), NULL);

  return sample->caps;
}

/**
 * gst_sample_get_segment:
 * @sample: a #GstSample
 *
 * Get the segment associated with @sample
 *
 * Returns: (transfer none): the segment of @sample.
 *  The segment remains valid as long as @sample is valid.
 */
GstSegment *
gst_sample_get_segment (GstSample * sample)
{
  g_return_val_if_fail (GST_IS_SAMPLE (sample), NULL);

  return &sample->segment;
}

/**
 * gst_sample_get_info:
 * @sample: a #GstSample
 *
 * Get extra information associated with @sample.
 *
 * Returns: (transfer none): the extra info of @sample.
 *  The info remains valid as long as @sample is valid.
 */
const GstStructure *
gst_sample_get_info (GstSample * sample)
{
  g_return_val_if_fail (GST_IS_SAMPLE (sample), NULL);

  return sample->info;
}
