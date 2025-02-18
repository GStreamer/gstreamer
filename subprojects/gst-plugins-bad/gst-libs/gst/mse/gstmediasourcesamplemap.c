/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Copyright (C) 2022, 2023 Collabora Ltd.
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

#include "gstmediasourcesamplemap-private.h"
#include "gstmselogging-private.h"

G_DEFINE_BOXED_TYPE (GstMediaSourceCodedFrameGroup,
    gst_media_source_coded_frame_group,
    gst_media_source_coded_frame_group_copy,
    gst_media_source_coded_frame_group_free);

#define TIME_RANGE_FORMAT \
    "[%" GST_TIMEP_FORMAT "..%" GST_TIMEP_FORMAT ")"
#define TIME_RANGE_ARGS(a, b) &(a), &(b)
#define CODED_FRAME_GROUP_ARGS(g) TIME_RANGE_ARGS ((g)->start, (g)->end)

struct _GstMediaSourceSampleMap
{
  GstObject parent_instance;

  GHashTable *samples;
  GSequence *samples_by_dts;
  GSequence *samples_by_pts;

  gsize storage_size;
};

G_DEFINE_TYPE (GstMediaSourceSampleMap, gst_media_source_sample_map,
    GST_TYPE_OBJECT);

static GstMediaSourceCodedFrameGroup *new_coded_frame_group_from_memory (const
    GstMediaSourceCodedFrameGroup * src);
static gint compare_pts (GstSample * a, GstSample * b, gpointer user_data);
static GSequenceIter *next_coded_frame_group (GSequenceIter * it, GValue * val);

static inline GstClockTime
sample_duration (GstSample * sample)
{
  return GST_BUFFER_DURATION (gst_sample_get_buffer (sample));
}

static inline GstClockTime
sample_dts (GstSample * sample)
{
  return GST_BUFFER_DTS (gst_sample_get_buffer (sample));
}

static inline GstClockTime
sample_dts_end (GstSample * sample)
{
  return sample_dts (sample) + sample_duration (sample);
}

static inline GstClockTime
sample_pts (GstSample * sample)
{
  return GST_BUFFER_PTS (gst_sample_get_buffer (sample));
}

static gsize
sample_buffer_size (GstSample * sample)
{
  return gst_buffer_get_size (gst_sample_get_buffer (sample));
}

static inline gboolean
sample_is_key_unit (GstSample * sample)
{
  GstBuffer *buffer = gst_sample_get_buffer (sample);
  return !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
}

static gint
compare_dts (GstSample * a, GstSample * b, gpointer user_data)
{
  GstClockTime a_dts = sample_dts (a);
  GstClockTime b_dts = sample_dts (b);
  if (a_dts == b_dts) {
    return compare_pts (a, b, user_data);
  }
  return a_dts > b_dts ? +1 : -1;
}

static gint
compare_pts (GstSample * a, GstSample * b, gpointer user_data)
{
  GstClockTime a_pts = sample_pts (a);
  GstClockTime b_pts = sample_pts (b);
  if (a_pts == b_pts) {
    return 0;
  }
  return a_pts > b_pts ? +1 : -1;
}

static inline void
remove_sample (GSequence * sequence, GCompareDataFunc compare,
    GstSample * sample, GstMediaSourceSampleMap * self)
{
  GSequenceIter *match = g_sequence_lookup (sequence, sample, compare, self);
  if (match == NULL) {
    return;
  }
  g_sequence_remove (match);
}

GstMediaSourceSampleMap *
gst_media_source_sample_map_new (void)
{
  gst_mse_init_logging ();
  return gst_object_ref_sink (g_object_new (GST_TYPE_MEDIA_SOURCE_SAMPLE_MAP,
          NULL));
}

static void
gst_media_source_sample_map_finalize (GObject * object)
{
  GstMediaSourceSampleMap *self = GST_MEDIA_SOURCE_SAMPLE_MAP (object);
  g_sequence_free (self->samples_by_dts);
  g_sequence_free (self->samples_by_pts);
  g_hash_table_unref (self->samples);
  G_OBJECT_CLASS (gst_media_source_sample_map_parent_class)->finalize (object);
}

static void
gst_media_source_sample_map_class_init (GstMediaSourceSampleMapClass * klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = GST_DEBUG_FUNCPTR (gst_media_source_sample_map_finalize);
}

static void
gst_media_source_sample_map_init (GstMediaSourceSampleMap * self)
{
  self->samples = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      (GDestroyNotify) gst_sample_unref, NULL);
  self->samples_by_dts = g_sequence_new ((GDestroyNotify) gst_sample_unref);
  self->samples_by_pts = g_sequence_new ((GDestroyNotify) gst_sample_unref);
}

void
gst_media_source_sample_map_add (GstMediaSourceSampleMap * self,
    GstSample * sample)
{
  g_return_if_fail (GST_IS_MEDIA_SOURCE_SAMPLE_MAP (self));
  g_return_if_fail (GST_IS_SAMPLE (sample));
  GstBuffer *buffer = gst_sample_get_buffer (sample);
  g_return_if_fail (GST_BUFFER_DTS_IS_VALID (buffer));
  g_return_if_fail (GST_BUFFER_PTS_IS_VALID (buffer));
  g_return_if_fail (GST_BUFFER_DURATION_IS_VALID (buffer));

  if (g_hash_table_contains (self->samples, sample)) {
    return;
  }

  g_hash_table_add (self->samples, gst_sample_ref (sample));
  g_sequence_insert_sorted (self->samples_by_dts, gst_sample_ref (sample),
      (GCompareDataFunc) compare_dts, self);
  g_sequence_insert_sorted (self->samples_by_pts, gst_sample_ref (sample),
      (GCompareDataFunc) compare_pts, self);
  self->storage_size += gst_buffer_get_size (buffer);
  GST_TRACE_OBJECT (self, "new storage size=%" G_GSIZE_FORMAT,
      self->storage_size);
}

void
gst_media_source_sample_map_remove (GstMediaSourceSampleMap * self,
    GstSample * sample)
{
  g_return_if_fail (GST_IS_MEDIA_SOURCE_SAMPLE_MAP (self));
  g_return_if_fail (GST_IS_SAMPLE (sample));

  if (!g_hash_table_contains (self->samples, sample)) {
    return;
  }

  gsize buffer_size = sample_buffer_size (sample);
  remove_sample (self->samples_by_dts, (GCompareDataFunc) compare_dts, sample,
      self);
  remove_sample (self->samples_by_pts, (GCompareDataFunc) compare_pts, sample,
      self);
  g_hash_table_remove (self->samples, sample);
  self->storage_size -= MIN (buffer_size, self->storage_size);
}

gboolean
gst_media_source_sample_map_contains (GstMediaSourceSampleMap * self,
    GstSample * sample)
{
  g_return_val_if_fail (GST_IS_MEDIA_SOURCE_SAMPLE_MAP (self), FALSE);
  return g_hash_table_contains (self->samples, sample);
}

static GSequenceIter *
next_key_unit (GSequenceIter * it)
{
  for (; !g_sequence_iter_is_end (it); it = g_sequence_iter_next (it)) {
    GstSample *sample = g_sequence_get (it);
    if (sample_is_key_unit (sample)) {
      break;
    }
  }
  return it;
}

static GSequenceIter *
next_coded_frame_group (GSequenceIter * it, GValue * val)
{
  it = next_key_unit (it);
  if (g_sequence_iter_is_end (it)) {
    return it;
  }
  GstSample *head = g_sequence_get (it);
  g_return_val_if_fail (sample_is_key_unit (head), NULL);
  GstMediaSourceCodedFrameGroup group = {
    .start = sample_dts (head),
    .end = sample_dts_end (head),
    .samples = g_list_prepend (NULL, gst_sample_ref (head)),
    .size = 1,
  };
  it = g_sequence_iter_next (it);
  for (; !g_sequence_iter_is_end (it); it = g_sequence_iter_next (it)) {
    GstSample *sample = g_sequence_get (it);
    if (sample_is_key_unit (sample)) {
      goto done;
    }
    group.end = sample_dts_end (sample), group.size++;
    group.samples = g_list_prepend (group.samples, gst_sample_ref (sample));
  }
done:
  group.samples = g_list_reverse (group.samples);
  GstMediaSourceCodedFrameGroup *box =
      new_coded_frame_group_from_memory (&group);
  gst_value_take_media_source_coded_frame_group (val, box);
  return it;
}

static GSequenceIter *
find_start_point (GstMediaSourceSampleMap * self, GSequenceIter * it,
    GList ** to_remove, GstClockTime earliest, GstClockTime latest)
{
  while (TRUE) {
    GValue value = G_VALUE_INIT;
    g_value_init (&value, GST_TYPE_MEDIA_SOURCE_CODED_FRAME_GROUP);
    it = next_coded_frame_group (it, &value);
    GstMediaSourceCodedFrameGroup *group =
        gst_value_get_media_source_coded_frame_group (&value);
    if (group == NULL) {
      GST_TRACE_OBJECT (self,
          "reached end of coded frames before finding start point");
      goto done;
    }

    if (group->start >= earliest && group->end <= latest) {
      GST_TRACE_OBJECT (self, "found start point for %" GST_TIMEP_FORMAT ": "
          TIME_RANGE_FORMAT, &earliest, CODED_FRAME_GROUP_ARGS (group));
      *to_remove = g_list_prepend (*to_remove,
          gst_media_source_coded_frame_group_copy (group));
      goto done;
    }

    g_value_unset (&value);
    continue;
  done:
    g_value_unset (&value);
    return it;
  }
  return it;
}

static GSequenceIter *
find_end_point (GstMediaSourceSampleMap * self, GSequenceIter * it, GList **
    to_remove, GstClockTime latest)
{
  while (TRUE) {
    GValue value = G_VALUE_INIT;
    g_value_init (&value, GST_TYPE_MEDIA_SOURCE_CODED_FRAME_GROUP);
    it = next_coded_frame_group (it, &value);
    GstMediaSourceCodedFrameGroup *group =
        gst_value_get_media_source_coded_frame_group (&value);
    if (group == NULL) {
      GST_TRACE_OBJECT (self,
          "reached end of coded frames before finding end point");
      goto done;
    }
    if (group->end >= latest) {
      GST_TRACE_OBJECT (self, "found end point for %" GST_TIMEP_FORMAT ": "
          TIME_RANGE_FORMAT, &latest, CODED_FRAME_GROUP_ARGS (group));
      goto done;
    }
    *to_remove = g_list_prepend (*to_remove,
        gst_media_source_coded_frame_group_copy (group));
    g_value_unset (&value);
    continue;

  done:
    g_value_unset (&value);
    return it;
  }
  return it;
}

gsize
gst_media_source_sample_map_remove_range (GstMediaSourceSampleMap * self,
    GstClockTime earliest, GstClockTime latest)
{
  g_return_val_if_fail (GST_IS_MEDIA_SOURCE_SAMPLE_MAP (self), 0);
  g_return_val_if_fail (earliest <= latest, 0);

  GST_TRACE_OBJECT (self, "request remove range " TIME_RANGE_FORMAT,
      TIME_RANGE_ARGS (earliest, latest));

  GSequenceIter *it = g_sequence_get_begin_iter (self->samples_by_dts);

  GList *to_remove = NULL;

  it = find_start_point (self, it, &to_remove, earliest, latest);

  if (to_remove == NULL) {
    return 0;
  }

  it = find_end_point (self, it, &to_remove, latest);

  to_remove = g_list_reverse (to_remove);

  gsize bytes_removed = 0;
  for (GList * group_iter = to_remove; group_iter != NULL;
      group_iter = g_list_next (group_iter)) {
    GstMediaSourceCodedFrameGroup *group = group_iter->data;
    for (GList * sample_iter = group->samples; sample_iter != NULL;
        sample_iter = g_list_next (sample_iter)) {
      GstSample *sample = sample_iter->data;
      bytes_removed += sample_buffer_size (sample);
      gst_media_source_sample_map_remove (self, sample);
    }
  }

  g_list_free_full (to_remove,
      (GDestroyNotify) gst_media_source_coded_frame_group_free);

  GST_TRACE_OBJECT (self, "removed=%" G_GSIZE_FORMAT "B, latest=%"
      GST_TIMEP_FORMAT, bytes_removed, &latest);
  return bytes_removed;
}

GstClockTime
gst_media_source_sample_map_get_highest_end_time (GstMediaSourceSampleMap *
    self)
{
  g_return_val_if_fail (GST_IS_MEDIA_SOURCE_SAMPLE_MAP (self),
      GST_CLOCK_TIME_NONE);
  GSequenceIter *iter = g_sequence_get_end_iter (self->samples_by_pts);
  iter = g_sequence_iter_prev (iter);
  if (g_sequence_iter_is_begin (iter)) {
    return GST_CLOCK_TIME_NONE;
  }
  GstSample *sample = g_sequence_get (iter);
  GstBuffer *buffer = gst_sample_get_buffer (sample);
  g_return_val_if_fail (GST_BUFFER_PTS_IS_VALID (buffer), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (GST_BUFFER_DURATION_IS_VALID (buffer),
      GST_CLOCK_TIME_NONE);
  return GST_BUFFER_PTS (buffer) + GST_BUFFER_DURATION (buffer);
}

guint
gst_media_source_sample_map_get_size (GstMediaSourceSampleMap * self)
{
  g_return_val_if_fail (GST_IS_MEDIA_SOURCE_SAMPLE_MAP (self), 0);
  return g_hash_table_size (self->samples);
}

gsize
gst_media_source_sample_map_get_storage_size (GstMediaSourceSampleMap * self)
{
  g_return_val_if_fail (GST_IS_MEDIA_SOURCE_SAMPLE_MAP (self), 0);
  return self->storage_size;
}

typedef struct _SampleMapIterator SampleMapIterator;

struct _SampleMapIterator
{
  GstIterator iterator;
  GstMediaSourceSampleMap *map;
  GSequenceIter *(*reset_func) (SampleMapIterator *);

  GSequenceIter *current_iter;
};

static void
iter_copy (const SampleMapIterator * it, SampleMapIterator * copy)
{
  copy->map = gst_object_ref (it->map);
  copy->reset_func = it->reset_func;

  copy->current_iter = it->current_iter;
}

static GSequenceIter *
iter_reset_by_dts (SampleMapIterator * it)
{
  return g_sequence_get_begin_iter (it->map->samples_by_dts);
}

static GSequenceIter *
iter_reset_by_pts (SampleMapIterator * it)
{
  return g_sequence_get_begin_iter (it->map->samples_by_pts);
}

static GstIteratorResult
iter_next_sample (SampleMapIterator * it, GValue * result)
{

  if (g_sequence_iter_is_end (it->current_iter)) {
    return GST_ITERATOR_DONE;
  }

  GstSample *sample = g_sequence_get (it->current_iter);

  it->current_iter = g_sequence_iter_next (it->current_iter);

  gst_value_set_sample (result, sample);

  return GST_ITERATOR_OK;
}

static GstIteratorResult
iter_next_group (SampleMapIterator * it, GValue * result)
{
  if (g_sequence_iter_is_end (it->current_iter)) {
    return GST_ITERATOR_DONE;
  }

  it->current_iter = next_coded_frame_group (it->current_iter, result);

  return GST_ITERATOR_OK;
}

static void
iter_resync (SampleMapIterator * it)
{
  GST_TRACE_OBJECT (it->map, "resync");
  it->current_iter = it->reset_func (it);
}

static void
iter_free (SampleMapIterator * it)
{
  gst_clear_object (&it->map);
}

GstIterator *
gst_media_source_sample_map_iter_samples_by_dts (GstMediaSourceSampleMap * self,
    GMutex * lock, guint32 * master_cookie)
{
/* *INDENT-OFF* */
  SampleMapIterator *it = (SampleMapIterator *) gst_iterator_new (
      sizeof (SampleMapIterator),
      GST_TYPE_MEDIA_SOURCE_CODED_FRAME_GROUP,
      lock,
      master_cookie,
      (GstIteratorCopyFunction) iter_copy,
      (GstIteratorNextFunction) iter_next_group,
      (GstIteratorItemFunction) NULL,
      (GstIteratorResyncFunction) iter_resync,
      (GstIteratorFreeFunction) iter_free
  );
/* *INDENT-ON* */

  it->map = gst_object_ref (self);
  it->reset_func = iter_reset_by_dts;
  it->current_iter = iter_reset_by_dts (it);

  return GST_ITERATOR (it);
}

GstIterator *
gst_media_source_sample_map_iter_samples_by_pts (GstMediaSourceSampleMap * self,
    GMutex * lock, guint32 * master_cookie)
{
/* *INDENT-OFF* */
  SampleMapIterator *it = (SampleMapIterator *) gst_iterator_new (
      sizeof (SampleMapIterator),
      GST_TYPE_SAMPLE,
      lock,
      master_cookie,
      (GstIteratorCopyFunction) iter_copy,
      (GstIteratorNextFunction) iter_next_sample,
      (GstIteratorItemFunction) NULL,
      (GstIteratorResyncFunction) iter_resync,
      (GstIteratorFreeFunction) iter_free
  );
/* *INDENT-ON* */

  it->map = gst_object_ref (self);
  it->reset_func = iter_reset_by_pts;
  it->current_iter = iter_reset_by_pts (it);

  return GST_ITERATOR (it);
}

static GstMediaSourceCodedFrameGroup *
new_coded_frame_group_from_memory (const GstMediaSourceCodedFrameGroup * src)
{
  GstMediaSourceCodedFrameGroup *box =
      g_atomic_rc_box_new0 (GstMediaSourceCodedFrameGroup);
  memcpy (box, src, sizeof (GstMediaSourceCodedFrameGroup));
  return box;
}

GstMediaSourceCodedFrameGroup *
gst_media_source_coded_frame_group_copy (GstMediaSourceCodedFrameGroup * self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
free_group_inner (GstMediaSourceCodedFrameGroup * self)
{
  g_list_free_full (self->samples, (GDestroyNotify) gst_sample_unref);
}

void
gst_media_source_coded_frame_group_free (GstMediaSourceCodedFrameGroup * self)
{
  g_atomic_rc_box_release_full (self, (GDestroyNotify) free_group_inner);
}
