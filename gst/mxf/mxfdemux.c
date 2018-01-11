/* GStreamer
 * Copyright (C) 2008-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-mxfdemux
 * @title: mxfdemux
 *
 * mxfdemux demuxes an MXF file into the different contained streams.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v filesrc location=/path/to/mxf ! mxfdemux ! audioconvert ! autoaudiosink
 * ]| This pipeline demuxes an MXF file and outputs one of the contained raw audio streams.
 *
 */

/* TODO:
 *   - Handle timecode tracks correctly (where is this documented?)
 *   - Handle drop-frame field of timecode tracks
 *   - Handle Generic container system items
 *   - Implement correct support for clip-wrapped essence elements.
 *   - Post structural metadata and descriptive metadata trees as a message on the bus
 *     and send them downstream as event.
 *   - Multichannel audio needs channel layouts, define them (SMPTE S320M?).
 *   - Correctly handle the different rectangles and aspect-ratio for video
 *   - Add more support for non-standard MXF used by Avid (bug #561922).
 *   - Fix frame layout stuff, i.e. interlaced/progressive
 *   - In pull mode first find the first buffer for every pad before pushing
 *     to prevent jumpy playback in the beginning due to resynchronization.
 *
 *   - Implement SMPTE D11 essence and the digital cinema/MXF specs
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mxfdemux.h"
#include "mxfessence.h"

#include <string.h>

static GstStaticPadTemplate mxf_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/mxf")
    );

static GstStaticPadTemplate mxf_src_template =
GST_STATIC_PAD_TEMPLATE ("track_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (mxfdemux_debug);
#define GST_CAT_DEFAULT mxfdemux_debug

static GstFlowReturn
gst_mxf_demux_pull_klv_packet (GstMXFDemux * demux, guint64 offset, MXFUL * key,
    GstBuffer ** outbuf, guint * read);
static GstFlowReturn
gst_mxf_demux_handle_index_table_segment (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer, guint64 offset);

static void collect_index_table_segments (GstMXFDemux * demux);

GType gst_mxf_demux_pad_get_type (void);
G_DEFINE_TYPE (GstMXFDemuxPad, gst_mxf_demux_pad, GST_TYPE_PAD);

static void
gst_mxf_demux_pad_finalize (GObject * object)
{
  GstMXFDemuxPad *pad = GST_MXF_DEMUX_PAD (object);

  if (pad->tags) {
    gst_tag_list_unref (pad->tags);
    pad->tags = NULL;
  }

  G_OBJECT_CLASS (gst_mxf_demux_pad_parent_class)->finalize (object);
}

static void
gst_mxf_demux_pad_class_init (GstMXFDemuxPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_mxf_demux_pad_finalize;
}

static void
gst_mxf_demux_pad_init (GstMXFDemuxPad * pad)
{
  pad->position = 0;
  pad->current_material_track_position = 0;
}

enum
{
  PROP_0,
  PROP_PACKAGE,
  PROP_MAX_DRIFT,
  PROP_STRUCTURE
};

static gboolean gst_mxf_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_mxf_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_mxf_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

#define gst_mxf_demux_parent_class parent_class
G_DEFINE_TYPE (GstMXFDemux, gst_mxf_demux, GST_TYPE_ELEMENT);

static void
gst_mxf_demux_remove_pad (GstMXFDemuxPad * pad, GstMXFDemux * demux)
{
  gst_flow_combiner_remove_pad (demux->flowcombiner, GST_PAD_CAST (pad));
  gst_element_remove_pad (GST_ELEMENT (demux), GST_PAD_CAST (pad));
}

static void
gst_mxf_demux_remove_pads (GstMXFDemux * demux)
{
  g_ptr_array_foreach (demux->src, (GFunc) gst_mxf_demux_remove_pad, demux);
  g_ptr_array_foreach (demux->src, (GFunc) gst_object_unref, NULL);
  g_ptr_array_set_size (demux->src, 0);
}

static void
gst_mxf_demux_partition_free (GstMXFDemuxPartition * partition)
{
  mxf_partition_pack_reset (&partition->partition);
  mxf_primer_pack_reset (&partition->primer);

  g_free (partition);
}

static void
gst_mxf_demux_reset_mxf_state (GstMXFDemux * demux)
{
  guint i;

  GST_DEBUG_OBJECT (demux, "Resetting MXF state");

  g_list_foreach (demux->partitions, (GFunc) gst_mxf_demux_partition_free,
      NULL);
  g_list_free (demux->partitions);
  demux->partitions = NULL;

  demux->current_partition = NULL;

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *t =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

    if (t->offsets)
      g_array_free (t->offsets, TRUE);

    g_free (t->mapping_data);

    if (t->tags)
      gst_tag_list_unref (t->tags);

    if (t->caps)
      gst_caps_unref (t->caps);
  }
  g_array_set_size (demux->essence_tracks, 0);
}

static void
gst_mxf_demux_reset_linked_metadata (GstMXFDemux * demux)
{
  guint i;

  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *pad = g_ptr_array_index (demux->src, i);

    pad->material_track = NULL;
    pad->material_package = NULL;
    pad->current_component = NULL;
  }

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *track =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

    track->source_package = NULL;
    track->source_track = NULL;
  }

  demux->current_package = NULL;
}

static void
gst_mxf_demux_reset_metadata (GstMXFDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "Resetting metadata");

  g_rw_lock_writer_lock (&demux->metadata_lock);

  demux->update_metadata = TRUE;
  demux->metadata_resolved = FALSE;

  gst_mxf_demux_reset_linked_metadata (demux);

  demux->preface = NULL;

  if (demux->metadata) {
    g_hash_table_destroy (demux->metadata);
  }
  demux->metadata = mxf_metadata_hash_table_new ();

  if (demux->tags) {
    gst_tag_list_unref (demux->tags);
    demux->tags = NULL;
  }

  g_rw_lock_writer_unlock (&demux->metadata_lock);
}

static void
gst_mxf_demux_reset (GstMXFDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "cleaning up MXF demuxer");

  demux->flushing = FALSE;

  demux->footer_partition_pack_offset = 0;
  demux->offset = 0;

  demux->pull_footer_metadata = TRUE;

  demux->run_in = -1;

  memset (&demux->current_package_uid, 0, sizeof (MXFUMID));

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);

  if (demux->close_seg_event) {
    gst_event_unref (demux->close_seg_event);
    demux->close_seg_event = NULL;
  }

  gst_adapter_clear (demux->adapter);

  gst_mxf_demux_remove_pads (demux);

  if (demux->random_index_pack) {
    g_array_free (demux->random_index_pack, TRUE);
    demux->random_index_pack = NULL;
  }

  if (demux->pending_index_table_segments) {
    GList *l;

    for (l = demux->pending_index_table_segments; l; l = l->next) {
      MXFIndexTableSegment *s = l->data;
      mxf_index_table_segment_reset (s);
      g_free (s);
    }
    g_list_free (demux->pending_index_table_segments);
    demux->pending_index_table_segments = NULL;
  }

  if (demux->index_tables) {
    GList *l;

    for (l = demux->index_tables; l; l = l->next) {
      GstMXFDemuxIndexTable *t = l->data;
      g_array_free (t->offsets, TRUE);
      g_free (t);
    }
    g_list_free (demux->index_tables);
    demux->index_tables = NULL;
  }

  demux->index_table_segments_collected = FALSE;

  gst_mxf_demux_reset_mxf_state (demux);
  gst_mxf_demux_reset_metadata (demux);

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;
}

static GstFlowReturn
gst_mxf_demux_pull_range (GstMXFDemux * demux, guint64 offset,
    guint size, GstBuffer ** buffer)
{
  GstFlowReturn ret;

  ret = gst_pad_pull_range (demux->sinkpad, offset, size, buffer);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (demux,
        "failed when pulling %u bytes from offset %" G_GUINT64_FORMAT ": %s",
        size, offset, gst_flow_get_name (ret));
    *buffer = NULL;
    return ret;
  }

  if (G_UNLIKELY (*buffer && gst_buffer_get_size (*buffer) != size)) {
    GST_WARNING_OBJECT (demux,
        "partial pull got %" G_GSIZE_FORMAT " when expecting %u from offset %"
        G_GUINT64_FORMAT, gst_buffer_get_size (*buffer), size, offset);
    gst_buffer_unref (*buffer);
    ret = GST_FLOW_EOS;
    *buffer = NULL;
    return ret;
  }

  return ret;
}

static gboolean
gst_mxf_demux_push_src_event (GstMXFDemux * demux, GstEvent * event)
{
  gboolean ret = TRUE;
  guint i;

  GST_DEBUG_OBJECT (demux, "Pushing '%s' event downstream",
      GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *pad = GST_MXF_DEMUX_PAD (g_ptr_array_index (demux->src, i));

    if (pad->eos && GST_EVENT_TYPE (event) == GST_EVENT_EOS)
      continue;

    ret |= gst_pad_push_event (GST_PAD_CAST (pad), gst_event_ref (event));
  }

  gst_event_unref (event);

  return ret;
}

static GstMXFDemuxPad *
gst_mxf_demux_get_earliest_pad (GstMXFDemux * demux)
{
  guint i;
  GstClockTime earliest = GST_CLOCK_TIME_NONE;
  GstMXFDemuxPad *pad = NULL;

  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);

    if (!p->eos && p->position < earliest) {
      earliest = p->position;
      pad = p;
    }
  }

  return pad;
}

static gint
gst_mxf_demux_partition_compare (GstMXFDemuxPartition * a,
    GstMXFDemuxPartition * b)
{
  if (a->partition.this_partition < b->partition.this_partition)
    return -1;
  else if (a->partition.this_partition > b->partition.this_partition)
    return 1;
  else
    return 0;
}

static GstFlowReturn
gst_mxf_demux_handle_partition_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  MXFPartitionPack partition;
  GList *l;
  GstMXFDemuxPartition *p = NULL;
  GstMapInfo map;
  gboolean ret;

  GST_DEBUG_OBJECT (demux,
      "Handling partition pack of size %" G_GSIZE_FORMAT " at offset %"
      G_GUINT64_FORMAT, gst_buffer_get_size (buffer), demux->offset);

  for (l = demux->partitions; l; l = l->next) {
    GstMXFDemuxPartition *tmp = l->data;

    if (tmp->partition.this_partition + demux->run_in == demux->offset &&
        tmp->partition.major_version == 0x0001) {
      GST_DEBUG_OBJECT (demux, "Partition already parsed");
      p = tmp;
      goto out;
    }
  }


  gst_buffer_map (buffer, &map, GST_MAP_READ);
  ret = mxf_partition_pack_parse (key, &partition, map.data, map.size);
  gst_buffer_unmap (buffer, &map);
  if (!ret) {
    GST_ERROR_OBJECT (demux, "Parsing partition pack failed");
    return GST_FLOW_ERROR;
  }

  if (partition.this_partition != demux->offset + demux->run_in) {
    GST_WARNING_OBJECT (demux, "Partition with incorrect offset");
    partition.this_partition = demux->offset + demux->run_in;
  }

  if (partition.type == MXF_PARTITION_PACK_HEADER)
    demux->footer_partition_pack_offset = partition.footer_partition;

  for (l = demux->partitions; l; l = l->next) {
    GstMXFDemuxPartition *tmp = l->data;

    if (tmp->partition.this_partition + demux->run_in == demux->offset) {
      p = tmp;
      break;
    }
  }

  if (p) {
    mxf_partition_pack_reset (&p->partition);
    memcpy (&p->partition, &partition, sizeof (MXFPartitionPack));
  } else {
    p = g_new0 (GstMXFDemuxPartition, 1);
    memcpy (&p->partition, &partition, sizeof (MXFPartitionPack));
    demux->partitions =
        g_list_insert_sorted (demux->partitions, p,
        (GCompareFunc) gst_mxf_demux_partition_compare);
  }

  for (l = demux->partitions; l; l = l->next) {
    GstMXFDemuxPartition *a, *b;

    if (l->next == NULL)
      break;

    a = l->data;
    b = l->next->data;

    b->partition.prev_partition = a->partition.this_partition;
  }

out:
  demux->current_partition = p;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_primer_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  GstMapInfo map;
  gboolean ret;

  GST_DEBUG_OBJECT (demux,
      "Handling primer pack of size %" G_GSIZE_FORMAT " at offset %"
      G_GUINT64_FORMAT, gst_buffer_get_size (buffer), demux->offset);

  if (G_UNLIKELY (!demux->current_partition)) {
    GST_ERROR_OBJECT (demux, "Primer pack before partition pack");
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (demux->current_partition->primer.mappings)) {
    GST_DEBUG_OBJECT (demux, "Primer pack already exists");
    return GST_FLOW_OK;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  ret = mxf_primer_pack_parse (key, &demux->current_partition->primer,
      map.data, map.size);
  gst_buffer_unmap (buffer, &map);
  if (!ret) {
    GST_ERROR_OBJECT (demux, "Parsing primer pack failed");
    return GST_FLOW_ERROR;
  }

  demux->current_partition->primer.offset = demux->offset;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_resolve_references (GstMXFDemux * demux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GHashTableIter iter;
  MXFMetadataBase *m = NULL;
  GstStructure *structure;

  g_rw_lock_writer_lock (&demux->metadata_lock);

  GST_DEBUG_OBJECT (demux, "Resolve metadata references");
  demux->update_metadata = FALSE;

  if (!demux->metadata) {
    GST_ERROR_OBJECT (demux, "No metadata yet");
    g_rw_lock_writer_unlock (&demux->metadata_lock);
    return GST_FLOW_ERROR;
  }

  g_hash_table_iter_init (&iter, demux->metadata);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer) & m)) {
    m->resolved = MXF_METADATA_BASE_RESOLVE_STATE_NONE;
  }

  g_hash_table_iter_init (&iter, demux->metadata);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer) & m)) {
    gboolean resolved;

    resolved = mxf_metadata_base_resolve (m, demux->metadata);

    /* Resolving can fail for anything but the preface, as the preface
     * will resolve everything required */
    if (!resolved && MXF_IS_METADATA_PREFACE (m)) {
      ret = GST_FLOW_ERROR;
      goto error;
    }
  }

  demux->metadata_resolved = TRUE;

  structure =
      mxf_metadata_base_to_structure (MXF_METADATA_BASE (demux->preface));
  if (!demux->tags)
    demux->tags = gst_tag_list_new_empty ();

  gst_tag_list_add (demux->tags, GST_TAG_MERGE_REPLACE, GST_TAG_MXF_STRUCTURE,
      structure, NULL);

  gst_structure_free (structure);

  g_rw_lock_writer_unlock (&demux->metadata_lock);

  return ret;

error:
  demux->metadata_resolved = FALSE;
  g_rw_lock_writer_unlock (&demux->metadata_lock);

  return ret;
}

static MXFMetadataGenericPackage *
gst_mxf_demux_find_package (GstMXFDemux * demux, const MXFUMID * umid)
{
  MXFMetadataGenericPackage *ret = NULL;
  guint i;

  if (demux->preface->content_storage
      && demux->preface->content_storage->packages) {
    for (i = 0; i < demux->preface->content_storage->n_packages; i++) {
      MXFMetadataGenericPackage *p =
          demux->preface->content_storage->packages[i];

      if (!p)
        continue;

      if (mxf_umid_is_equal (&p->package_uid, umid)) {
        ret = p;
        break;
      }
    }
  }

  return ret;
}

static MXFMetadataGenericPackage *
gst_mxf_demux_choose_package (GstMXFDemux * demux)
{
  MXFMetadataGenericPackage *ret = NULL;
  guint i;

  if (demux->requested_package_string) {
    MXFUMID umid = { {0,}
    };

    if (!mxf_umid_from_string (demux->requested_package_string, &umid)) {
      GST_ERROR_OBJECT (demux, "Invalid requested package");
    }
    g_free (demux->requested_package_string);
    demux->requested_package_string = NULL;

    ret = gst_mxf_demux_find_package (demux, &umid);
  }

  if (!ret && !mxf_umid_is_zero (&demux->current_package_uid))
    ret = gst_mxf_demux_find_package (demux, &demux->current_package_uid);

  if (ret && (MXF_IS_METADATA_MATERIAL_PACKAGE (ret)
          || (MXF_IS_METADATA_SOURCE_PACKAGE (ret)
              && MXF_METADATA_SOURCE_PACKAGE (ret)->top_level)))
    goto done;
  else if (ret)
    GST_WARNING_OBJECT (demux,
        "Current package is not a material package or top-level source package, choosing the first best");
  else if (!mxf_umid_is_zero (&demux->current_package_uid))
    GST_WARNING_OBJECT (demux,
        "Current package not found, choosing the first best");

  ret = demux->preface->primary_package;
  if (ret && (MXF_IS_METADATA_MATERIAL_PACKAGE (ret)
          || (MXF_IS_METADATA_SOURCE_PACKAGE (ret)
              && MXF_METADATA_SOURCE_PACKAGE (ret)->top_level)))
    goto done;
  ret = NULL;

  for (i = 0; i < demux->preface->content_storage->n_packages; i++) {
    if (demux->preface->content_storage->packages[i] &&
        MXF_IS_METADATA_MATERIAL_PACKAGE (demux->preface->content_storage->
            packages[i])) {
      ret =
          MXF_METADATA_GENERIC_PACKAGE (demux->preface->content_storage->
          packages[i]);
      break;
    }
  }

  if (!ret) {
    GST_ERROR_OBJECT (demux, "No material package");
    return NULL;
  }

done:
  if (mxf_umid_is_equal (&ret->package_uid, &demux->current_package_uid)) {
    gchar current_package_string[96];

    gst_mxf_demux_remove_pads (demux);
    memcpy (&demux->current_package_uid, &ret->package_uid, 32);

    mxf_umid_to_string (&ret->package_uid, current_package_string);
    demux->current_package_string = g_strdup (current_package_string);
    g_object_notify (G_OBJECT (demux), "package");

    if (!demux->tags)
      demux->tags = gst_tag_list_new_empty ();
    gst_tag_list_add (demux->tags, GST_TAG_MERGE_REPLACE, GST_TAG_MXF_UMID,
        demux->current_package_string, NULL);
  }
  demux->current_package = ret;

  return ret;
}

static GstFlowReturn
gst_mxf_demux_update_essence_tracks (GstMXFDemux * demux)
{
  guint i, j, k;

  g_return_val_if_fail (demux->preface->content_storage, GST_FLOW_ERROR);
  g_return_val_if_fail (demux->preface->content_storage->essence_container_data,
      GST_FLOW_ERROR);

  for (i = 0; i < demux->preface->content_storage->n_essence_container_data;
      i++) {
    MXFMetadataEssenceContainerData *edata;
    MXFMetadataSourcePackage *package;

    if (demux->preface->content_storage->essence_container_data[i] == NULL)
      continue;

    edata = demux->preface->content_storage->essence_container_data[i];

    if (!edata->linked_package) {
      GST_WARNING_OBJECT (demux, "Linked package not resolved");
      continue;
    }

    package = edata->linked_package;

    if (!package->parent.tracks) {
      GST_WARNING_OBJECT (demux, "Linked package with no resolved tracks");
      continue;
    }

    for (j = 0; j < package->parent.n_tracks; j++) {
      MXFMetadataTimelineTrack *track;
      GstMXFDemuxEssenceTrack *etrack = NULL;
      GstCaps *caps = NULL;
      gboolean new = FALSE;

      if (!package->parent.tracks[j]
          || !MXF_IS_METADATA_TIMELINE_TRACK (package->parent.tracks[j]))
        continue;

      track = MXF_METADATA_TIMELINE_TRACK (package->parent.tracks[j]);
      if ((track->parent.type & 0xf0) != 0x30)
        continue;

      if (track->edit_rate.n <= 0 || track->edit_rate.d <= 0) {
        GST_WARNING_OBJECT (demux, "Invalid edit rate");
        continue;
      }

      for (k = 0; k < demux->essence_tracks->len; k++) {
        GstMXFDemuxEssenceTrack *tmp =
            &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack,
            k);

        if (tmp->track_number == track->parent.track_number &&
            tmp->body_sid == edata->body_sid) {
          if (tmp->track_id != track->parent.track_id ||
              !mxf_umid_is_equal (&tmp->source_package_uid,
                  &package->parent.package_uid)) {
            GST_ERROR_OBJECT (demux, "There already exists a different track "
                "with this track number and body sid but a different source "
                "or source track id -- ignoring");
            continue;
          }
          etrack = tmp;
          break;
        }
      }

      if (!etrack) {
        GstMXFDemuxEssenceTrack tmp;

        memset (&tmp, 0, sizeof (tmp));
        tmp.body_sid = edata->body_sid;
        tmp.index_sid = edata->index_sid;
        tmp.track_number = track->parent.track_number;
        tmp.track_id = track->parent.track_id;
        memcpy (&tmp.source_package_uid, &package->parent.package_uid, 32);

        if (demux->current_partition->partition.body_sid == edata->body_sid &&
            demux->current_partition->partition.body_offset == 0)
          tmp.position = 0;
        else
          tmp.position = -1;

        g_array_append_val (demux->essence_tracks, tmp);
        etrack =
            &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack,
            demux->essence_tracks->len - 1);
        new = TRUE;
      }

      etrack->source_package = NULL;
      etrack->source_track = NULL;

      if (!track->parent.sequence) {
        GST_WARNING_OBJECT (demux, "Source track has no sequence");
        goto next;
      }

      if (track->parent.n_descriptor == 0) {
        GST_WARNING_OBJECT (demux, "Source track has no descriptors");
        goto next;
      }

      if (track->parent.sequence->duration > etrack->duration)
        etrack->duration = track->parent.sequence->duration;

      g_free (etrack->mapping_data);
      etrack->mapping_data = NULL;
      etrack->handler = NULL;
      etrack->handle_func = NULL;
      if (etrack->tags)
        gst_tag_list_unref (etrack->tags);
      etrack->tags = NULL;

      etrack->handler = mxf_essence_element_handler_find (track);
      if (!etrack->handler) {
        gchar essence_container[48];
        gchar essence_compression[48];
        gchar *name;

        GST_WARNING_OBJECT (demux,
            "No essence element handler for track %u found", i);

        mxf_ul_to_string (&track->parent.descriptor[0]->essence_container,
            essence_container);

        if (track->parent.type == MXF_METADATA_TRACK_PICTURE_ESSENCE) {
          if (MXF_IS_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR (track->parent.
                  descriptor[0]))
            mxf_ul_to_string (&MXF_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR
                (track->parent.descriptor[0])->picture_essence_coding,
                essence_compression);

          name =
              g_strdup_printf ("video/x-mxf-%s-%s", essence_container,
              essence_compression);
        } else if (track->parent.type == MXF_METADATA_TRACK_SOUND_ESSENCE) {
          if (MXF_IS_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR (track->parent.
                  descriptor[0]))
            mxf_ul_to_string (&MXF_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR
                (track->parent.descriptor[0])->sound_essence_compression,
                essence_compression);

          name =
              g_strdup_printf ("audio/x-mxf-%s-%s", essence_container,
              essence_compression);
        } else if (track->parent.type == MXF_METADATA_TRACK_DATA_ESSENCE) {
          if (MXF_IS_METADATA_GENERIC_DATA_ESSENCE_DESCRIPTOR (track->parent.
                  descriptor[0]))
            mxf_ul_to_string (&MXF_METADATA_GENERIC_DATA_ESSENCE_DESCRIPTOR
                (track->parent.descriptor[0])->data_essence_coding,
                essence_compression);

          name =
              g_strdup_printf ("application/x-mxf-%s-%s", essence_container,
              essence_compression);
        } else {
          name = NULL;
          g_assert_not_reached ();
        }

        caps = gst_caps_new_empty_simple (name);
        g_free (name);
        etrack->intra_only = FALSE;
      } else {
        caps =
            etrack->handler->create_caps (track, &etrack->tags,
            &etrack->intra_only, &etrack->handle_func, &etrack->mapping_data);
      }

      GST_DEBUG_OBJECT (demux, "Created caps %" GST_PTR_FORMAT, caps);

      if (!caps && new) {
        GST_WARNING_OBJECT (demux, "No caps created, ignoring stream");
        g_free (etrack->mapping_data);
        etrack->mapping_data = NULL;
        if (etrack->tags)
          gst_tag_list_unref (etrack->tags);
        etrack->tags = NULL;
        goto next;
      } else if (!caps) {
        GST_WARNING_OBJECT (demux, "Couldn't create updated caps for stream");
      } else if (!etrack->caps || !gst_caps_is_equal (etrack->caps, caps)) {
        if (etrack->caps)
          gst_caps_unref (etrack->caps);
        etrack->caps = caps;
      } else {
        gst_caps_unref (caps);
        caps = NULL;
      }

      if (etrack->handler != NULL) {
        MXFEssenceWrapping track_wrapping;

        track_wrapping = etrack->handler->get_track_wrapping (track);
        if (track_wrapping == MXF_ESSENCE_WRAPPING_CLIP_WRAPPING) {
          GST_ELEMENT_ERROR (demux, STREAM, NOT_IMPLEMENTED, (NULL),
              ("Clip essence wrapping is not implemented yet."));
          return GST_FLOW_ERROR;
        } else if (track_wrapping == MXF_ESSENCE_WRAPPING_CUSTOM_WRAPPING) {
          GST_ELEMENT_ERROR (demux, STREAM, NOT_IMPLEMENTED, (NULL),
              ("Custom essence wrappings are not supported."));
          return GST_FLOW_ERROR;
        }
      }

      etrack->source_package = package;
      etrack->source_track = track;
      continue;

    next:
      if (new) {
        g_free (etrack->mapping_data);
        if (etrack->tags)
          gst_tag_list_unref (etrack->tags);
        if (etrack->caps)
          gst_caps_unref (etrack->caps);

        g_array_remove_index (demux->essence_tracks,
            demux->essence_tracks->len - 1);
      }
    }
  }

  if (demux->essence_tracks->len == 0) {
    GST_ERROR_OBJECT (demux, "No valid essence tracks in this file");
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *etrack =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

    if (!etrack->source_package || !etrack->source_track || !etrack->caps) {
      GST_ERROR_OBJECT (demux, "Failed to update essence track %u", i);
      return GST_FLOW_ERROR;
    }

  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_update_tracks (GstMXFDemux * demux)
{
  MXFMetadataGenericPackage *current_package = NULL;
  guint i, j, k;
  gboolean first_run;
  guint component_index;
  GstFlowReturn ret;
  GList *pads = NULL, *l;
  GstVideoTimeCode start_timecode = GST_VIDEO_TIME_CODE_INIT;

  g_rw_lock_writer_lock (&demux->metadata_lock);
  GST_DEBUG_OBJECT (demux, "Updating tracks");

  if ((ret = gst_mxf_demux_update_essence_tracks (demux)) != GST_FLOW_OK) {
    goto error;
  }

  current_package = gst_mxf_demux_choose_package (demux);

  if (!current_package) {
    GST_ERROR_OBJECT (demux, "Unable to find current package");
    ret = GST_FLOW_ERROR;
    goto error;
  } else if (!current_package->tracks) {
    GST_ERROR_OBJECT (demux, "Current package has no (resolved) tracks");
    ret = GST_FLOW_ERROR;
    goto error;
  } else if (!current_package->n_essence_tracks) {
    GST_ERROR_OBJECT (demux, "Current package has no essence tracks");
    ret = GST_FLOW_ERROR;
    goto error;
  }

  first_run = (demux->src->len == 0);

  /* For material packages, there must be one timecode track with one
   * continuous timecode. For source packages there might be multiple,
   * discontinuous timecode components.
   * TODO: Support multiple timecode components
   */
  for (i = 0; i < current_package->n_tracks; i++) {
    MXFMetadataTimelineTrack *track = NULL;
    MXFMetadataSequence *sequence = NULL;
    MXFMetadataTimecodeComponent *component = NULL;

    if (!current_package->tracks[i]) {
      GST_WARNING_OBJECT (demux, "Unresolved track");
      continue;
    }

    if (!MXF_IS_METADATA_TIMELINE_TRACK (current_package->tracks[i])) {
      GST_DEBUG_OBJECT (demux, "No timeline track");
      continue;
    }


    track = MXF_METADATA_TIMELINE_TRACK (current_package->tracks[i]);

    if (!track->parent.sequence)
      continue;
    sequence = track->parent.sequence;
    if (sequence->n_structural_components != 1 ||
        !sequence->structural_components[0]
        ||
        !MXF_IS_METADATA_TIMECODE_COMPONENT (sequence->structural_components
            [0]))
      continue;

    component =
        MXF_METADATA_TIMECODE_COMPONENT (sequence->structural_components[0]);

    /* Not a timecode track */
    if (track->parent.type && (track->parent.type & 0xf0) != 0x10)
      continue;

    /* Main timecode track must have id 1, all others must be 0 */
    if (track->parent.track_id != 1)
      continue;

    gst_video_time_code_init (&start_timecode, track->edit_rate.n,
        track->edit_rate.d, NULL, (component->drop_frame
            ?
            GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME
            : GST_VIDEO_TIME_CODE_FLAGS_NONE), 0, 0, 0, 0, 0);
    gst_video_time_code_add_frames (&start_timecode, track->origin);
    gst_video_time_code_add_frames (&start_timecode, component->start_timecode);
    break;
  }

  for (i = 0; i < current_package->n_tracks; i++) {
    MXFMetadataTimelineTrack *track = NULL;
    MXFMetadataSequence *sequence;
    MXFMetadataSourceClip *component = NULL;
    MXFMetadataSourcePackage *source_package = NULL;
    MXFMetadataTimelineTrack *source_track = NULL;
    GstMXFDemuxEssenceTrack *etrack = NULL;
    GstMXFDemuxPad *pad = NULL;
    GstCaps *pad_caps;

    GST_DEBUG_OBJECT (demux, "Handling track %u", i);

    if (!current_package->tracks[i]) {
      GST_WARNING_OBJECT (demux, "Unresolved track");
      continue;
    }

    if (!MXF_IS_METADATA_TIMELINE_TRACK (current_package->tracks[i])) {
      GST_DEBUG_OBJECT (demux, "No timeline track");
      continue;
    }

    track = MXF_METADATA_TIMELINE_TRACK (current_package->tracks[i]);

    if (!first_run) {
      /* Find pad from track_id */
      for (j = 0; j < demux->src->len; j++) {
        GstMXFDemuxPad *tmp = g_ptr_array_index (demux->src, j);

        if (tmp->track_id == track->parent.track_id) {
          pad = tmp;
          break;
        }
      }
    }

    if (pad)
      component_index = pad->current_component_index;
    else
      component_index = 0;

    if (!track->parent.sequence) {
      GST_WARNING_OBJECT (demux, "Track with no sequence");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    sequence = track->parent.sequence;

    if (MXF_IS_METADATA_SOURCE_PACKAGE (current_package)) {
      GST_DEBUG_OBJECT (demux, "Playing source package");

      component = NULL;
      source_package = MXF_METADATA_SOURCE_PACKAGE (current_package);
      source_track = track;
    } else if (sequence->structural_components
        &&
        MXF_IS_METADATA_SOURCE_CLIP (sequence->structural_components
            [component_index])) {
      GST_DEBUG_OBJECT (demux, "Playing material package");

      component =
          MXF_METADATA_SOURCE_CLIP (sequence->structural_components
          [component_index]);
      if (!component) {
        GST_WARNING_OBJECT (demux, "NULL component in non-source package");
        if (!pad) {
          continue;
        } else {
          ret = GST_FLOW_ERROR;
          goto error;
        }
      }

      if (component->source_package && component->source_package->top_level &&
          MXF_METADATA_GENERIC_PACKAGE (component->source_package)->tracks) {
        MXFMetadataGenericPackage *tmp_pkg =
            MXF_METADATA_GENERIC_PACKAGE (component->source_package);

        source_package = component->source_package;

        for (k = 0; k < tmp_pkg->n_tracks; k++) {
          MXFMetadataTrack *tmp = tmp_pkg->tracks[k];

          if (tmp->track_id == component->source_track_id) {
            source_track = MXF_METADATA_TIMELINE_TRACK (tmp);
            break;
          }
        }
      }
    }

    if (track->parent.type && (track->parent.type & 0xf0) != 0x30) {
      GST_DEBUG_OBJECT (demux, "No essence track");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    if (!source_package || track->parent.type == MXF_METADATA_TRACK_UNKNOWN
        || !source_track) {
      GST_WARNING_OBJECT (demux,
          "No source package or track type for track found");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    for (k = 0; k < demux->essence_tracks->len; k++) {
      GstMXFDemuxEssenceTrack *tmp =
          &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, k);

      if (tmp->source_package == source_package &&
          tmp->source_track == source_track) {
        etrack = tmp;
        break;
      }
    }

    if (!etrack) {
      GST_WARNING_OBJECT (demux, "No essence track for this track found");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    if (track->edit_rate.n <= 0 || track->edit_rate.d <= 0 ||
        source_track->edit_rate.n <= 0 || source_track->edit_rate.d <= 0) {
      GST_WARNING_OBJECT (demux, "Track has an invalid edit rate");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    if (MXF_IS_METADATA_MATERIAL_PACKAGE (current_package) && !component) {
      GST_WARNING_OBJECT (demux,
          "Playing material package but found no component for track");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    if (!source_package->descriptor) {
      GST_WARNING_OBJECT (demux, "Source package has no descriptors");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    if (!source_track->parent.descriptor) {
      GST_WARNING_OBJECT (demux, "No descriptor found for track");
      if (!pad) {
        continue;
      } else {
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }

    if (!pad && first_run) {
      GstPadTemplate *templ;
      gchar *pad_name;

      templ =
          gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (demux),
          "track_%u");
      pad_name = g_strdup_printf ("track_%u", track->parent.track_id);

      g_assert (templ != NULL);

      /* Create pad */
      pad = (GstMXFDemuxPad *) g_object_new (GST_TYPE_MXF_DEMUX_PAD,
          "name", pad_name, "direction", GST_PAD_SRC, "template", templ, NULL);
      pad->need_segment = TRUE;
      pad->eos = FALSE;
      g_free (pad_name);

      if (demux->tags)
        pad->tags = gst_tag_list_copy (demux->tags);
    }

    if (!pad) {
      GST_WARNING_OBJECT (demux,
          "Not the first pad addition run, ignoring new track");
      continue;
    }

    /* Update pad */
    pad->track_id = track->parent.track_id;

    pad->material_package = current_package;
    pad->material_track = track;

    pad->start_timecode = start_timecode;

    /* If we just added the pad initialize for the current component */
    if (first_run && MXF_IS_METADATA_MATERIAL_PACKAGE (current_package)) {
      pad->current_component_index = 0;
      pad->current_component_start = source_track->origin;
      pad->current_component_start_position = 0;

      if (component->parent.duration >= -1)
        pad->current_component_duration = component->parent.duration;
      else
        pad->current_component_duration = -1;

      if (track->edit_rate.n != source_track->edit_rate.n ||
          track->edit_rate.d != source_track->edit_rate.d) {
        pad->current_component_start +=
            gst_util_uint64_scale (component->start_position,
            source_track->edit_rate.n * track->edit_rate.d,
            source_track->edit_rate.d * track->edit_rate.n);

        if (pad->current_component_duration != -1)
          pad->current_component_duration =
              gst_util_uint64_scale (pad->current_component_duration,
              source_track->edit_rate.n * track->edit_rate.d,
              source_track->edit_rate.d * track->edit_rate.n);
      } else {
        pad->current_component_start += component->start_position;
      }
      pad->current_essence_track_position = pad->current_component_start;
    }

    /* NULL iff playing a source package */
    pad->current_component = component;

    pad->current_essence_track = etrack;

    if (etrack->tags) {
      if (pad->tags)
        gst_tag_list_insert (pad->tags, etrack->tags, GST_TAG_MERGE_REPLACE);
      else
        pad->tags = gst_tag_list_copy (etrack->tags);
    }

    pad_caps = gst_pad_get_current_caps (GST_PAD_CAST (pad));
    if (pad_caps && !gst_caps_is_equal (pad_caps, etrack->caps)) {
      gst_pad_set_caps (GST_PAD_CAST (pad), etrack->caps);
    } else if (!pad_caps) {
      GstEvent *event;
      gchar *stream_id;

      gst_pad_set_event_function (GST_PAD_CAST (pad),
          GST_DEBUG_FUNCPTR (gst_mxf_demux_src_event));

      gst_pad_set_query_function (GST_PAD_CAST (pad),
          GST_DEBUG_FUNCPTR (gst_mxf_demux_src_query));

      gst_pad_use_fixed_caps (GST_PAD_CAST (pad));
      gst_pad_set_active (GST_PAD_CAST (pad), TRUE);

      stream_id =
          gst_pad_create_stream_id_printf (GST_PAD_CAST (pad),
          GST_ELEMENT_CAST (demux), "%03u", pad->track_id);

      event =
          gst_pad_get_sticky_event (demux->sinkpad, GST_EVENT_STREAM_START, 0);
      if (event) {
        if (gst_event_parse_group_id (event, &demux->group_id))
          demux->have_group_id = TRUE;
        else
          demux->have_group_id = FALSE;
        gst_event_unref (event);
      } else if (!demux->have_group_id) {
        demux->have_group_id = TRUE;
        demux->group_id = gst_util_group_id_next ();
      }
      event = gst_event_new_stream_start (stream_id);
      if (demux->have_group_id)
        gst_event_set_group_id (event, demux->group_id);

      gst_pad_push_event (GST_PAD_CAST (pad), event);
      g_free (stream_id);

      gst_pad_set_caps (GST_PAD_CAST (pad), etrack->caps);

      pads = g_list_prepend (pads, gst_object_ref (pad));

      g_ptr_array_add (demux->src, pad);
      pad->discont = TRUE;
    }
    if (pad_caps)
      gst_caps_unref (pad_caps);
  }

  if (demux->src->len > 0) {
    for (i = 0; i < demux->src->len; i++) {
      GstMXFDemuxPad *pad = g_ptr_array_index (demux->src, i);

      if (!pad->material_track || !pad->material_package) {
        GST_ERROR_OBJECT (demux, "Unable to update existing pad");
        ret = GST_FLOW_ERROR;
        goto error;
      }
    }
  } else {
    GST_ERROR_OBJECT (demux, "Couldn't create any streams");
    ret = GST_FLOW_ERROR;
    goto error;
  }

  g_rw_lock_writer_unlock (&demux->metadata_lock);

  for (l = pads; l; l = l->next) {
    gst_flow_combiner_add_pad (demux->flowcombiner, l->data);
    gst_element_add_pad (GST_ELEMENT_CAST (demux), l->data);
  }
  g_list_free (pads);

  if (first_run)
    gst_element_no_more_pads (GST_ELEMENT_CAST (demux));

  return GST_FLOW_OK;

error:
  g_rw_lock_writer_unlock (&demux->metadata_lock);
  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  guint16 type;
  MXFMetadata *metadata = NULL, *old = NULL;
  GstMapInfo map;
  GstFlowReturn ret = GST_FLOW_OK;

  type = GST_READ_UINT16_BE (key->u + 13);

  GST_DEBUG_OBJECT (demux,
      "Handling metadata of size %" G_GSIZE_FORMAT " at offset %"
      G_GUINT64_FORMAT " of type 0x%04x", gst_buffer_get_size (buffer),
      demux->offset, type);

  if (G_UNLIKELY (!demux->current_partition)) {
    GST_ERROR_OBJECT (demux, "Partition pack doesn't exist");
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (!demux->current_partition->primer.mappings)) {
    GST_ERROR_OBJECT (demux, "Primer pack doesn't exists");
    return GST_FLOW_ERROR;
  }

  if (demux->current_partition->parsed_metadata) {
    GST_DEBUG_OBJECT (demux, "Metadata of this partition was already parsed");
    return GST_FLOW_OK;
  }

  if (gst_buffer_get_size (buffer) == 0)
    return GST_FLOW_OK;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  metadata =
      mxf_metadata_new (type, &demux->current_partition->primer, demux->offset,
      map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  if (!metadata) {
    GST_WARNING_OBJECT (demux,
        "Unknown or unhandled metadata of type 0x%04x", type);
    return GST_FLOW_OK;
  }

  old =
      g_hash_table_lookup (demux->metadata,
      &MXF_METADATA_BASE (metadata)->instance_uid);

  if (old && G_TYPE_FROM_INSTANCE (old) != G_TYPE_FROM_INSTANCE (metadata)) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif

    GST_DEBUG_OBJECT (demux,
        "Metadata with instance uid %s already exists and has different type '%s',"
        " expected '%s'",
        mxf_uuid_to_string (&MXF_METADATA_BASE (metadata)->instance_uid, str),
        g_type_name (G_TYPE_FROM_INSTANCE (old)),
        g_type_name (G_TYPE_FROM_INSTANCE (metadata)));
    g_object_unref (metadata);
    return GST_FLOW_ERROR;
  } else if (old
      && MXF_METADATA_BASE (old)->offset >=
      MXF_METADATA_BASE (metadata)->offset) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif

    GST_DEBUG_OBJECT (demux,
        "Metadata with instance uid %s already exists and is newer",
        mxf_uuid_to_string (&MXF_METADATA_BASE (metadata)->instance_uid, str));
    g_object_unref (metadata);
    return GST_FLOW_OK;
  }

  g_rw_lock_writer_lock (&demux->metadata_lock);
  demux->update_metadata = TRUE;

  if (MXF_IS_METADATA_PREFACE (metadata)) {
    demux->preface = MXF_METADATA_PREFACE (metadata);
  }

  gst_mxf_demux_reset_linked_metadata (demux);

  g_hash_table_replace (demux->metadata,
      &MXF_METADATA_BASE (metadata)->instance_uid, metadata);
  g_rw_lock_writer_unlock (&demux->metadata_lock);

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_descriptive_metadata (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  guint32 type;
  guint8 scheme;
  GstMapInfo map;
  GstFlowReturn ret = GST_FLOW_OK;
  MXFDescriptiveMetadata *m = NULL, *old = NULL;

  scheme = GST_READ_UINT8 (key->u + 12);
  type = GST_READ_UINT24_BE (key->u + 13);

  GST_DEBUG_OBJECT (demux,
      "Handling descriptive metadata of size %" G_GSIZE_FORMAT " at offset %"
      G_GUINT64_FORMAT " with scheme 0x%02x and type 0x%06x",
      gst_buffer_get_size (buffer), demux->offset, scheme, type);

  if (G_UNLIKELY (!demux->current_partition)) {
    GST_ERROR_OBJECT (demux, "Partition pack doesn't exist");
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (!demux->current_partition->primer.mappings)) {
    GST_ERROR_OBJECT (demux, "Primer pack doesn't exists");
    return GST_FLOW_ERROR;
  }

  if (demux->current_partition->parsed_metadata) {
    GST_DEBUG_OBJECT (demux, "Metadata of this partition was already parsed");
    return GST_FLOW_OK;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  m = mxf_descriptive_metadata_new (scheme, type,
      &demux->current_partition->primer, demux->offset, map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  if (!m) {
    GST_WARNING_OBJECT (demux,
        "Unknown or unhandled descriptive metadata of scheme 0x%02x and type 0x%06x",
        scheme, type);
    return GST_FLOW_OK;
  }

  old =
      g_hash_table_lookup (demux->metadata,
      &MXF_METADATA_BASE (m)->instance_uid);

  if (old && G_TYPE_FROM_INSTANCE (old) != G_TYPE_FROM_INSTANCE (m)) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif

    GST_DEBUG_OBJECT (demux,
        "Metadata with instance uid %s already exists and has different type '%s',"
        " expected '%s'",
        mxf_uuid_to_string (&MXF_METADATA_BASE (m)->instance_uid, str),
        g_type_name (G_TYPE_FROM_INSTANCE (old)),
        g_type_name (G_TYPE_FROM_INSTANCE (m)));
    g_object_unref (m);
    return GST_FLOW_ERROR;
  } else if (old
      && MXF_METADATA_BASE (old)->offset >= MXF_METADATA_BASE (m)->offset) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif

    GST_DEBUG_OBJECT (demux,
        "Metadata with instance uid %s already exists and is newer",
        mxf_uuid_to_string (&MXF_METADATA_BASE (m)->instance_uid, str));
    g_object_unref (m);
    return GST_FLOW_OK;
  }

  g_rw_lock_writer_lock (&demux->metadata_lock);

  demux->update_metadata = TRUE;
  gst_mxf_demux_reset_linked_metadata (demux);

  g_hash_table_replace (demux->metadata, &MXF_METADATA_BASE (m)->instance_uid,
      m);

  g_rw_lock_writer_unlock (&demux->metadata_lock);

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_generic_container_system_item (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (demux,
      "Handling generic container system item of size %" G_GSIZE_FORMAT
      " at offset %" G_GUINT64_FORMAT, gst_buffer_get_size (buffer),
      demux->offset);

  if (demux->current_partition->essence_container_offset == 0)
    demux->current_partition->essence_container_offset =
        demux->offset - demux->current_partition->partition.this_partition -
        demux->run_in;

  /* TODO: parse this */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_pad_set_component (GstMXFDemux * demux, GstMXFDemuxPad * pad,
    guint i)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *pad_caps;
  MXFMetadataSequence *sequence;
  guint k;
  MXFMetadataSourcePackage *source_package = NULL;
  MXFMetadataTimelineTrack *source_track = NULL;
  gboolean update = (pad->current_component_index != i);

  pad->current_component_index = i;

  sequence = pad->material_track->parent.sequence;

  if (pad->current_component_index >= sequence->n_structural_components) {
    GST_DEBUG_OBJECT (demux, "After last structural component");
    pad->current_component_index = sequence->n_structural_components - 1;
    ret = GST_FLOW_EOS;
  }

  GST_DEBUG_OBJECT (demux, "Switching to component %u",
      pad->current_component_index);

  pad->current_component =
      MXF_METADATA_SOURCE_CLIP (sequence->structural_components[pad->
          current_component_index]);
  if (pad->current_component == NULL) {
    GST_ERROR_OBJECT (demux, "No such structural component");
    return GST_FLOW_ERROR;
  }

  if (!pad->current_component->source_package
      || !pad->current_component->source_package->top_level
      || !MXF_METADATA_GENERIC_PACKAGE (pad->current_component->
          source_package)->tracks) {
    GST_ERROR_OBJECT (demux, "Invalid component");
    return GST_FLOW_ERROR;
  }

  source_package = pad->current_component->source_package;

  for (k = 0; k < source_package->parent.n_tracks; k++) {
    MXFMetadataTrack *tmp = source_package->parent.tracks[k];

    if (tmp->track_id == pad->current_component->source_track_id) {
      source_track = MXF_METADATA_TIMELINE_TRACK (tmp);
      break;
    }
  }

  if (!source_track) {
    GST_ERROR_OBJECT (demux, "No source track found");
    return GST_FLOW_ERROR;
  }

  pad->current_essence_track = NULL;

  for (k = 0; k < demux->essence_tracks->len; k++) {
    GstMXFDemuxEssenceTrack *tmp =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, k);

    if (tmp->source_package == source_package &&
        tmp->source_track == source_track) {
      pad->current_essence_track = tmp;
      break;
    }
  }

  if (!pad->current_essence_track) {
    GST_ERROR_OBJECT (demux, "No corresponding essence track found");
    return GST_FLOW_ERROR;
  }

  if (!source_package->descriptor) {
    GST_ERROR_OBJECT (demux, "Source package has no descriptors");
    return GST_FLOW_ERROR;
  }

  if (!source_track->parent.descriptor) {
    GST_ERROR_OBJECT (demux, "No descriptor found for track");
    return GST_FLOW_ERROR;
  }

  if (source_track->edit_rate.n <= 0 || source_track->edit_rate.d <= 0) {
    GST_ERROR_OBJECT (demux, "Source track has invalid edit rate");
    return GST_FLOW_ERROR;
  }

  pad->current_component_start_position = 0;
  for (k = 0; k < i; k++) {
    pad->current_component_start_position +=
        MXF_METADATA_SOURCE_CLIP (sequence->structural_components[k])->
        parent.duration;
  }

  if (pad->current_component->parent.duration >= -1)
    pad->current_component_duration = pad->current_component->parent.duration;
  else
    pad->current_component_duration = -1;

  if (pad->material_track->edit_rate.n != source_track->edit_rate.n ||
      pad->material_track->edit_rate.d != source_track->edit_rate.d) {
    pad->current_component_start +=
        gst_util_uint64_scale (pad->current_component->start_position,
        source_track->edit_rate.n * pad->material_track->edit_rate.d,
        source_track->edit_rate.d * pad->material_track->edit_rate.n);

    if (pad->current_component_duration != -1)
      pad->current_component_duration =
          gst_util_uint64_scale (pad->current_component_duration,
          source_track->edit_rate.n * pad->material_track->edit_rate.d,
          source_track->edit_rate.d * pad->material_track->edit_rate.n);
  } else {
    pad->current_component_start += pad->current_component->start_position;
  }
  pad->current_essence_track_position = pad->current_component_start;

  pad_caps = gst_pad_get_current_caps (GST_PAD_CAST (pad));
  if (!gst_caps_is_equal (pad_caps, pad->current_essence_track->caps)) {
    gst_pad_set_caps (GST_PAD_CAST (pad), pad->current_essence_track->caps);
  }
  gst_caps_unref (pad_caps);

  if (update) {
    if (pad->tags) {
      if (pad->current_essence_track->tags)
        gst_tag_list_insert (pad->tags, pad->current_essence_track->tags,
            GST_TAG_MERGE_REPLACE);
    } else {
      if (pad->current_essence_track->tags)
        pad->tags = gst_tag_list_copy (pad->current_essence_track->tags);
    }
  }

  if (ret == GST_FLOW_EOS) {
    pad->current_essence_track_position += pad->current_component_duration;
  }

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_generic_container_essence_element (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer, gboolean peek)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 track_number;
  guint i;
  GstBuffer *inbuf = NULL;
  GstBuffer *outbuf = NULL;
  GstMXFDemuxEssenceTrack *etrack = NULL;
  gboolean keyframe = TRUE;
  /* As in GstMXFDemuxIndex */
  guint64 pts = G_MAXUINT64, dts = G_MAXUINT64;

  GST_DEBUG_OBJECT (demux,
      "Handling generic container essence element of size %" G_GSIZE_FORMAT
      " at offset %" G_GUINT64_FORMAT, gst_buffer_get_size (buffer),
      demux->offset);

  GST_DEBUG_OBJECT (demux, "  type = 0x%02x", key->u[12]);
  GST_DEBUG_OBJECT (demux, "  essence element count = 0x%02x", key->u[13]);
  GST_DEBUG_OBJECT (demux, "  essence element type = 0x%02x", key->u[14]);
  GST_DEBUG_OBJECT (demux, "  essence element number = 0x%02x", key->u[15]);

  if (demux->current_partition->essence_container_offset == 0)
    demux->current_partition->essence_container_offset =
        demux->offset - demux->current_partition->partition.this_partition -
        demux->run_in;

  if (!demux->current_package) {
    GST_ERROR_OBJECT (demux, "No package selected yet");
    return GST_FLOW_ERROR;
  }

  if (demux->src->len == 0) {
    GST_ERROR_OBJECT (demux, "No streams created yet");
    return GST_FLOW_ERROR;
  }

  if (demux->essence_tracks->len == 0) {
    GST_ERROR_OBJECT (demux, "No essence streams found in the metadata");
    return GST_FLOW_ERROR;
  }

  track_number = GST_READ_UINT32_BE (&key->u[12]);

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *tmp =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

    if (tmp->body_sid == demux->current_partition->partition.body_sid &&
        (tmp->track_number == track_number || tmp->track_number == 0)) {
      etrack = tmp;
      break;
    }
  }

  if (!etrack) {
    GST_WARNING_OBJECT (demux,
        "No essence track for this essence element found");
    return GST_FLOW_OK;
  }

  if (etrack->position == -1) {
    GST_DEBUG_OBJECT (demux,
        "Unknown essence track position, looking into index");
    if (etrack->offsets) {
      for (i = 0; i < etrack->offsets->len; i++) {
        GstMXFDemuxIndex *idx =
            &g_array_index (etrack->offsets, GstMXFDemuxIndex, i);

        if (idx->initialized && idx->offset != 0
            && idx->offset == demux->offset - demux->run_in) {
          etrack->position = i;
          break;
        }
      }
    }

    if (etrack->position == -1) {
      GST_WARNING_OBJECT (demux, "Essence track position not in index");
      return GST_FLOW_OK;
    }
  }

  if (etrack->offsets && etrack->offsets->len > etrack->position) {
    GstMXFDemuxIndex *index =
        &g_array_index (etrack->offsets, GstMXFDemuxIndex, etrack->position);
    if (index->initialized && index->offset != 0)
      keyframe = index->keyframe;
    if (index->initialized && index->pts != G_MAXUINT64)
      pts = index->pts;
    if (index->initialized && index->dts != G_MAXUINT64)
      dts = index->dts;
  }

  /* Create subbuffer to be able to change metadata */
  inbuf =
      gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 0,
      gst_buffer_get_size (buffer));

  if (!keyframe)
    GST_BUFFER_FLAG_SET (inbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  if (etrack->handle_func) {
    /* Takes ownership of inbuf */
    ret =
        etrack->handle_func (key, inbuf, etrack->caps,
        etrack->source_track, etrack->mapping_data, &outbuf);
    inbuf = NULL;
  } else {
    outbuf = inbuf;
    inbuf = NULL;
    ret = GST_FLOW_OK;
  }

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (demux, "Failed to handle essence element");
    if (outbuf) {
      gst_buffer_unref (outbuf);
      outbuf = NULL;
    }
    return ret;
  }

  if (outbuf)
    keyframe = !GST_BUFFER_FLAG_IS_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  /* Prefer keyframe information from index tables over everything else */
  if (demux->index_tables) {
    GList *l;
    GstMXFDemuxIndexTable *index_table = NULL;

    for (l = demux->index_tables; l; l = l->next) {
      GstMXFDemuxIndexTable *tmp = l->data;

      if (tmp->body_sid == etrack->body_sid
          && tmp->index_sid == etrack->index_sid) {
        index_table = tmp;
        break;
      }
    }

    if (index_table && index_table->offsets->len > etrack->position) {
      GstMXFDemuxIndex *index =
          &g_array_index (index_table->offsets, GstMXFDemuxIndex,
          etrack->position);
      if (index->initialized && index->offset != 0) {
        keyframe = index->keyframe;

        if (outbuf) {
          if (keyframe)
            GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
          else
            GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
        }
      }

      if (index->initialized && index->pts != G_MAXUINT64)
        pts = index->pts;
      if (index->initialized && index->dts != G_MAXUINT64)
        dts = index->dts;
    }
  }

  if (!etrack->offsets)
    etrack->offsets = g_array_new (FALSE, TRUE, sizeof (GstMXFDemuxIndex));

  {
    if (etrack->offsets->len > etrack->position) {
      GstMXFDemuxIndex *index =
          &g_array_index (etrack->offsets, GstMXFDemuxIndex, etrack->position);

      index->offset = demux->offset - demux->run_in;
      index->initialized = TRUE;
      index->pts = pts;
      index->dts = dts;
      index->keyframe = keyframe;
    } else if (etrack->position < G_MAXINT) {
      GstMXFDemuxIndex index;

      index.offset = demux->offset - demux->run_in;
      index.initialized = TRUE;
      index.pts = pts;
      index.dts = dts;
      index.keyframe = keyframe;
      if (etrack->offsets->len < etrack->position)
        g_array_set_size (etrack->offsets, etrack->position + 1);
      g_array_insert_val (etrack->offsets, etrack->position, index);
    }
  }

  if (peek)
    goto out;

  if (!outbuf) {
    GST_DEBUG_OBJECT (demux, "No output buffer created");
    goto out;
  }

  inbuf = outbuf;
  outbuf = NULL;

  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *pad = g_ptr_array_index (demux->src, i);

    if (pad->current_essence_track != etrack)
      continue;

    if (pad->eos) {
      GST_DEBUG_OBJECT (demux, "Pad is already EOS");
      continue;
    }

    if (etrack->position != pad->current_essence_track_position) {
      GST_DEBUG_OBJECT (demux, "Not at current component's position");
      continue;
    }

    {
      GstMXFDemuxPad *earliest = gst_mxf_demux_get_earliest_pad (demux);

      if (earliest && earliest != pad && earliest->position < pad->position &&
          pad->position - earliest->position > demux->max_drift) {
        GST_DEBUG_OBJECT (demux, "Pad is too far ahead of time");
        continue;
      }
    }

    /* Create another subbuffer to have writable metadata */
    outbuf =
        gst_buffer_copy_region (inbuf, GST_BUFFER_COPY_ALL, 0,
        gst_buffer_get_size (inbuf));

    GST_BUFFER_DTS (outbuf) = pad->position;
    if (etrack->intra_only) {
      GST_BUFFER_PTS (outbuf) = pad->position;
    } else if (pts != G_MAXUINT64) {
      GST_BUFFER_PTS (outbuf) = gst_util_uint64_scale (pts * GST_SECOND,
          pad->current_essence_track->source_track->edit_rate.d,
          pad->current_essence_track->source_track->edit_rate.n);
      GST_BUFFER_PTS (outbuf) +=
          gst_util_uint64_scale (pad->current_component_start_position *
          GST_SECOND, pad->material_track->edit_rate.d,
          pad->material_track->edit_rate.n);
    } else {
      GST_BUFFER_PTS (outbuf) = GST_CLOCK_TIME_NONE;
    }

    GST_BUFFER_DURATION (outbuf) =
        gst_util_uint64_scale (GST_SECOND,
        pad->current_essence_track->source_track->edit_rate.d,
        pad->current_essence_track->source_track->edit_rate.n);
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_NONE;

    if (pad->material_track->parent.type == MXF_METADATA_TRACK_PICTURE_ESSENCE
        && pad->start_timecode.config.fps_n != 0
        && pad->start_timecode.config.fps_d != 0) {
      if (etrack->intra_only) {
        GstVideoTimeCode timecode = pad->start_timecode;

        gst_video_time_code_add_frames (&timecode,
            pad->current_material_track_position);
        gst_buffer_add_video_time_code_meta (outbuf, &timecode);
      } else if (pts != G_MAXUINT64) {
        GstVideoTimeCode timecode = pad->start_timecode;

        gst_video_time_code_add_frames (&timecode,
            pad->current_component_start_position);
        gst_video_time_code_add_frames (&timecode,
            gst_util_uint64_scale (pts,
                pad->material_track->edit_rate.n *
                pad->current_essence_track->source_track->edit_rate.d,
                pad->material_track->edit_rate.d *
                pad->current_essence_track->source_track->edit_rate.n));
        gst_buffer_add_video_time_code_meta (outbuf, &timecode);
      }

    }

    /* Update accumulated error and compensate */
    {
      guint64 abs_error =
          (GST_SECOND * pad->current_essence_track->source_track->edit_rate.d) %
          pad->current_essence_track->source_track->edit_rate.n;
      pad->position_accumulated_error +=
          ((gdouble) abs_error) /
          ((gdouble) pad->current_essence_track->source_track->edit_rate.n);
    }
    if (pad->position_accumulated_error >= 1.0) {
      GST_BUFFER_DURATION (outbuf) += 1;
      pad->position_accumulated_error -= 1.0;
    }

    if (pad->need_segment) {
      GstEvent *e;

      if (demux->close_seg_event)
        gst_pad_push_event (GST_PAD_CAST (pad),
            gst_event_ref (demux->close_seg_event));

      e = gst_event_new_segment (&demux->segment);
      gst_event_set_seqnum (e, demux->seqnum);
      gst_pad_push_event (GST_PAD_CAST (pad), e);
      pad->need_segment = FALSE;
    }

    if (pad->tags) {
      gst_pad_push_event (GST_PAD_CAST (pad), gst_event_new_tag (pad->tags));
      pad->tags = NULL;
    }

    pad->position += GST_BUFFER_DURATION (outbuf);
    pad->current_material_track_position++;

    GST_DEBUG_OBJECT (demux,
        "Pushing buffer of size %" G_GSIZE_FORMAT " for track %u: pts %"
        GST_TIME_FORMAT " dts %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT
        " position %" G_GUINT64_FORMAT, gst_buffer_get_size (outbuf),
        pad->material_track->parent.track_id,
        GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DTS (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
        pad->current_essence_track_position);

    if (pad->discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      pad->discont = FALSE;
    }

    ret = gst_pad_push (GST_PAD_CAST (pad), outbuf);
    outbuf = NULL;
    ret = gst_flow_combiner_update_flow (demux->flowcombiner, ret);
    GST_LOG_OBJECT (demux, "combined return %s", gst_flow_get_name (ret));

    if (pad->position > demux->segment.position)
      demux->segment.position = pad->position;

    if (ret != GST_FLOW_OK)
      goto out;

    pad->current_essence_track_position++;

    if (pad->current_component) {
      if (pad->current_component_duration > 0 &&
          pad->current_essence_track_position - pad->current_component_start
          >= pad->current_component_duration) {
        GST_DEBUG_OBJECT (demux, "Switching to next component");

        ret =
            gst_mxf_demux_pad_set_component (demux, pad,
            pad->current_component_index + 1);
        if (ret != GST_FLOW_OK && ret != GST_FLOW_EOS) {
          GST_ERROR_OBJECT (demux, "Switching component failed");
        }
      } else if (etrack->duration > 0
          && pad->current_essence_track_position >= etrack->duration) {
        GST_DEBUG_OBJECT (demux,
            "Current component position after end of essence track");
        ret = GST_FLOW_EOS;
      }
    } else if (etrack->duration > 0
        && pad->current_essence_track_position == etrack->duration) {
      GST_DEBUG_OBJECT (demux, "At the end of the essence track");
      ret = GST_FLOW_EOS;
    }

    if (ret == GST_FLOW_EOS) {
      GstEvent *e;

      GST_DEBUG_OBJECT (demux, "EOS for track");
      pad->eos = TRUE;
      e = gst_event_new_eos ();
      gst_event_set_seqnum (e, demux->seqnum);
      gst_pad_push_event (GST_PAD_CAST (pad), e);
      ret = GST_FLOW_OK;
    }

    if (ret != GST_FLOW_OK)
      goto out;
  }

out:
  if (inbuf)
    gst_buffer_unref (inbuf);

  if (outbuf)
    gst_buffer_unref (outbuf);

  etrack->position++;

  return ret;
}

static void
read_partition_header (GstMXFDemux * demux)
{
  GstBuffer *buf;
  MXFUL key;
  guint read;

  if (gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buf, &read)
      != GST_FLOW_OK)
    return;

  if (!mxf_is_partition_pack (&key)) {
    gst_buffer_unref (buf);
    return;
  }

  if (gst_mxf_demux_handle_partition_pack (demux, &key, buf) != GST_FLOW_OK) {
    gst_buffer_unref (buf);
    return;
  }
  demux->offset += read;
  gst_buffer_unref (buf);

  if (gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buf, &read)
      != GST_FLOW_OK)
    return;

  while (mxf_is_fill (&key)) {
    demux->offset += read;
    gst_buffer_unref (buf);
    if (gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buf, &read)
        != GST_FLOW_OK)
      return;
  }

  if (!mxf_is_index_table_segment (&key)
      && demux->current_partition->partition.header_byte_count) {
    gst_buffer_unref (buf);
    demux->offset += demux->current_partition->partition.header_byte_count;
    if (gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buf, &read)
        != GST_FLOW_OK)
      return;
  }

  while (mxf_is_fill (&key)) {
    demux->offset += read;
    gst_buffer_unref (buf);
    if (gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buf, &read)
        != GST_FLOW_OK)
      return;
  }

  if (demux->current_partition->partition.index_byte_count
      && mxf_is_index_table_segment (&key)) {
    guint64 index_end_offset =
        demux->offset + demux->current_partition->partition.index_byte_count;

    while (demux->offset < index_end_offset) {
      if (mxf_is_index_table_segment (&key)) {
        gst_mxf_demux_handle_index_table_segment (demux, &key, buf,
            demux->offset);
      }
      demux->offset += read;

      gst_buffer_unref (buf);
      if (gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buf,
              &read)
          != GST_FLOW_OK)
        return;
    }
  }

  while (mxf_is_fill (&key)) {
    demux->offset += read;
    gst_buffer_unref (buf);
    if (gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buf, &read)
        != GST_FLOW_OK)
      return;
  }

  if (mxf_is_generic_container_system_item (&key) ||
      mxf_is_generic_container_essence_element (&key) ||
      mxf_is_avid_essence_container_essence_element (&key)) {
    if (demux->current_partition->essence_container_offset == 0)
      demux->current_partition->essence_container_offset =
          demux->offset - demux->current_partition->partition.this_partition -
          demux->run_in;
  }

  gst_buffer_unref (buf);
}

static GstFlowReturn
gst_mxf_demux_handle_random_index_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  guint i;
  GList *l;
  GstMapInfo map;
  gboolean ret;

  GST_DEBUG_OBJECT (demux,
      "Handling random index pack of size %" G_GSIZE_FORMAT " at offset %"
      G_GUINT64_FORMAT, gst_buffer_get_size (buffer), demux->offset);

  if (demux->random_index_pack) {
    GST_DEBUG_OBJECT (demux, "Already parsed random index pack");
    return GST_FLOW_OK;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  ret =
      mxf_random_index_pack_parse (key, map.data, map.size,
      &demux->random_index_pack);
  gst_buffer_unmap (buffer, &map);

  if (!ret) {
    GST_ERROR_OBJECT (demux, "Parsing random index pack failed");
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < demux->random_index_pack->len; i++) {
    GstMXFDemuxPartition *p = NULL;
    MXFRandomIndexPackEntry *e =
        &g_array_index (demux->random_index_pack, MXFRandomIndexPackEntry, i);

    if (e->offset < demux->run_in) {
      GST_ERROR_OBJECT (demux, "Invalid random index pack entry");
      return GST_FLOW_ERROR;
    }

    for (l = demux->partitions; l; l = l->next) {
      GstMXFDemuxPartition *tmp = l->data;

      if (tmp->partition.this_partition + demux->run_in == e->offset) {
        p = tmp;
        break;
      }
    }

    if (!p) {
      p = g_new0 (GstMXFDemuxPartition, 1);
      p->partition.this_partition = e->offset - demux->run_in;
      p->partition.body_sid = e->body_sid;
      demux->partitions =
          g_list_insert_sorted (demux->partitions, p,
          (GCompareFunc) gst_mxf_demux_partition_compare);
    }
  }

  for (l = demux->partitions; l; l = l->next) {
    GstMXFDemuxPartition *a, *b;

    if (l->next == NULL)
      break;

    a = l->data;
    b = l->next->data;

    b->partition.prev_partition = a->partition.this_partition;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_index_table_segment (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer, guint64 offset)
{
  MXFIndexTableSegment *segment;
  GstMapInfo map;
  gboolean ret;

  GST_DEBUG_OBJECT (demux,
      "Handling index table segment of size %" G_GSIZE_FORMAT " at offset %"
      G_GUINT64_FORMAT, gst_buffer_get_size (buffer), offset);

  segment = g_new0 (MXFIndexTableSegment, 1);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  ret = mxf_index_table_segment_parse (key, segment, map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  if (!ret) {
    GST_ERROR_OBJECT (demux, "Parsing index table segment failed");
    g_free (segment);
    return GST_FLOW_ERROR;
  }

  demux->pending_index_table_segments =
      g_list_prepend (demux->pending_index_table_segments, segment);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_pull_klv_packet (GstMXFDemux * demux, guint64 offset, MXFUL * key,
    GstBuffer ** outbuf, guint * read)
{
  GstBuffer *buffer = NULL;
  const guint8 *data;
  guint64 data_offset = 0;
  guint64 length;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
#ifndef GST_DISABLE_GST_DEBUG
  gchar str[48];
#endif

  memset (key, 0, sizeof (MXFUL));

  /* Pull 16 byte key and first byte of BER encoded length */
  if ((ret =
          gst_mxf_demux_pull_range (demux, offset, 17, &buffer)) != GST_FLOW_OK)
    goto beach;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  memcpy (key, map.data, 16);

  GST_DEBUG_OBJECT (demux, "Got KLV packet with key %s", mxf_ul_to_string (key,
          str));

  /* Decode BER encoded packet length */
  if ((map.data[16] & 0x80) == 0) {
    length = map.data[16];
    data_offset = 17;
  } else {
    guint slen = map.data[16] & 0x7f;

    data_offset = 16 + 1 + slen;

    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
    buffer = NULL;

    /* Must be at most 8 according to SMPTE-379M 5.3.4 */
    if (slen > 8) {
      GST_ERROR_OBJECT (demux, "Invalid KLV packet length: %u", slen);
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    /* Now pull the length of the packet */
    if ((ret = gst_mxf_demux_pull_range (demux, offset + 17, slen,
                &buffer)) != GST_FLOW_OK)
      goto beach;

    gst_buffer_map (buffer, &map, GST_MAP_READ);

    data = map.data;
    length = 0;
    while (slen) {
      length = (length << 8) | *data;
      data++;
      slen--;
    }
  }

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);
  buffer = NULL;

  /* GStreamer's buffer sizes are stored in a guint so we
   * limit ourself to G_MAXUINT large buffers */
  if (length > G_MAXUINT) {
    GST_ERROR_OBJECT (demux,
        "Unsupported KLV packet length: %" G_GUINT64_FORMAT, length);
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  GST_DEBUG_OBJECT (demux, "KLV packet with key %s has length "
      "%" G_GUINT64_FORMAT, mxf_ul_to_string (key, str), length);

  /* Pull the complete KLV packet */
  if ((ret = gst_mxf_demux_pull_range (demux, offset + data_offset, length,
              &buffer)) != GST_FLOW_OK)
    goto beach;

  *outbuf = buffer;
  buffer = NULL;
  if (read)
    *read = data_offset + length;

beach:
  if (buffer)
    gst_buffer_unref (buffer);

  return ret;
}

static void
gst_mxf_demux_pull_random_index_pack (GstMXFDemux * demux)
{
  GstBuffer *buffer;
  gint64 filesize = -1;
  GstFormat fmt = GST_FORMAT_BYTES;
  guint32 pack_size;
  guint64 old_offset = demux->offset;
  MXFUL key;
  GstMapInfo map;
  GstFlowReturn flow_ret;

  if (!gst_pad_peer_query_duration (demux->sinkpad, fmt, &filesize) ||
      fmt != GST_FORMAT_BYTES || filesize == -1) {
    GST_DEBUG_OBJECT (demux, "Can't query upstream size");
    return;
  }

  g_assert (filesize > 4);

  buffer = NULL;
  if (gst_mxf_demux_pull_range (demux, filesize - 4, 4, &buffer) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Failed pulling last 4 bytes");
    return;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  pack_size = GST_READ_UINT32_BE (map.data);
  gst_buffer_unmap (buffer, &map);

  gst_buffer_unref (buffer);

  if (pack_size < 20) {
    GST_DEBUG_OBJECT (demux, "Too small pack size (%u bytes)", pack_size);
    return;
  } else if (pack_size > filesize - 20) {
    GST_DEBUG_OBJECT (demux, "Too large pack size (%u bytes)", pack_size);
    return;
  }

  buffer = NULL;
  if (gst_mxf_demux_pull_range (demux, filesize - pack_size, 16,
          &buffer) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Failed pulling random index pack key");
    return;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  memcpy (&key, map.data, 16);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  if (!mxf_is_random_index_pack (&key)) {
    GST_DEBUG_OBJECT (demux, "No random index pack");
    return;
  }

  demux->offset = filesize - pack_size;
  if (gst_mxf_demux_pull_klv_packet (demux, filesize - pack_size, &key,
          &buffer, NULL) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Failed pulling random index pack");
    return;
  }

  flow_ret = gst_mxf_demux_handle_random_index_pack (demux, &key, buffer);
  gst_buffer_unref (buffer);
  demux->offset = old_offset;

  if (flow_ret == GST_FLOW_OK && !demux->index_table_segments_collected) {
    collect_index_table_segments (demux);
    demux->index_table_segments_collected = TRUE;
  }
}

static void
gst_mxf_demux_parse_footer_metadata (GstMXFDemux * demux)
{
  guint64 old_offset = demux->offset;
  MXFUL key;
  GstBuffer *buffer = NULL;
  guint read = 0;
  GstFlowReturn flow = GST_FLOW_OK;
  GstMXFDemuxPartition *old_partition = demux->current_partition;

  demux->current_partition = NULL;

  gst_mxf_demux_reset_metadata (demux);

  if (demux->footer_partition_pack_offset != 0) {
    demux->offset = demux->run_in + demux->footer_partition_pack_offset;
  } else {
    MXFRandomIndexPackEntry *entry =
        &g_array_index (demux->random_index_pack, MXFRandomIndexPackEntry,
        demux->random_index_pack->len - 1);
    demux->offset = entry->offset;
  }

next_try:
  flow =
      gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
      &read);
  if (G_UNLIKELY (flow != GST_FLOW_OK))
    goto out;

  if (!mxf_is_partition_pack (&key))
    goto out;

  if (gst_mxf_demux_handle_partition_pack (demux, &key, buffer) != GST_FLOW_OK)
    goto out;

  demux->offset += read;
  gst_buffer_unref (buffer);
  buffer = NULL;

  if (demux->current_partition->partition.header_byte_count == 0) {
    if (demux->current_partition->partition.this_partition == 0)
      goto out;

    demux->offset =
        demux->run_in + demux->current_partition->partition.prev_partition;
    goto next_try;
  }

  while (TRUE) {
    flow =
        gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
        &read);
    if (G_UNLIKELY (flow != GST_FLOW_OK)) {
      if (!demux->current_partition->partition.prev_partition)
        goto out;
      demux->offset =
          demux->run_in + demux->current_partition->partition.prev_partition;
      goto next_try;
    }

    if (mxf_is_fill (&key)) {
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    } else if (mxf_is_primer_pack (&key)) {
      if (!demux->current_partition->primer.mappings) {
        if (gst_mxf_demux_handle_primer_pack (demux, &key,
                buffer) != GST_FLOW_OK) {
          demux->offset += read;
          gst_buffer_unref (buffer);
          buffer = NULL;
          if (!demux->current_partition->partition.prev_partition)
            goto out;
          demux->offset =
              demux->run_in +
              demux->current_partition->partition.prev_partition;
          goto next_try;
        }
      }
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
      break;
    } else {
      gst_buffer_unref (buffer);
      buffer = NULL;
      if (!demux->current_partition->partition.prev_partition)
        goto out;
      demux->offset =
          demux->run_in + demux->current_partition->partition.prev_partition;
      goto next_try;
    }
  }

  /* parse metadata */
  while (demux->offset <
      demux->run_in + demux->current_partition->primer.offset +
      demux->current_partition->partition.header_byte_count) {
    flow =
        gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
        &read);
    if (G_UNLIKELY (flow != GST_FLOW_OK)) {
      if (!demux->current_partition->partition.prev_partition)
        goto out;
      demux->offset =
          demux->run_in + demux->current_partition->partition.prev_partition;
      goto next_try;
    }

    if (mxf_is_metadata (&key)) {
      flow = gst_mxf_demux_handle_metadata (demux, &key, buffer);
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;

      if (G_UNLIKELY (flow != GST_FLOW_OK)) {
        gst_mxf_demux_reset_metadata (demux);
        if (!demux->current_partition->partition.prev_partition)
          goto out;
        demux->offset =
            demux->run_in + demux->current_partition->partition.prev_partition;
        goto next_try;
      }
    } else if (mxf_is_descriptive_metadata (&key)) {
      gst_mxf_demux_handle_descriptive_metadata (demux, &key, buffer);
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    } else if (mxf_is_fill (&key)) {
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    } else if (mxf_is_generic_container_system_item (&key) ||
        mxf_is_generic_container_essence_element (&key) ||
        mxf_is_avid_essence_container_essence_element (&key)) {
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
      break;
    } else {
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    }
  }

  /* resolve references etc */
  if (!demux->preface || gst_mxf_demux_resolve_references (demux) !=
      GST_FLOW_OK || gst_mxf_demux_update_tracks (demux) != GST_FLOW_OK) {
    demux->current_partition->parsed_metadata = TRUE;
    if (!demux->current_partition->partition.prev_partition)
      goto out;
    demux->offset =
        demux->run_in + demux->current_partition->partition.prev_partition;
    goto next_try;
  }

out:
  if (buffer)
    gst_buffer_unref (buffer);

  demux->offset = old_offset;
  demux->current_partition = old_partition;
}

static GstFlowReturn
gst_mxf_demux_handle_klv_packet (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer, gboolean peek)
{
#ifndef GST_DISABLE_GST_DEBUG
  gchar key_str[48];
#endif
  GstFlowReturn ret = GST_FLOW_OK;

  if (demux->update_metadata
      && demux->preface
      && (demux->offset >=
          demux->run_in + demux->current_partition->primer.offset +
          demux->current_partition->partition.header_byte_count ||
          mxf_is_generic_container_system_item (key) ||
          mxf_is_generic_container_essence_element (key) ||
          mxf_is_avid_essence_container_essence_element (key))) {
    demux->current_partition->parsed_metadata = TRUE;
    if ((ret = gst_mxf_demux_resolve_references (demux)) != GST_FLOW_OK ||
        (ret = gst_mxf_demux_update_tracks (demux)) != GST_FLOW_OK) {
      goto beach;
    }
  } else if (demux->metadata_resolved && demux->requested_package_string) {
    if ((ret = gst_mxf_demux_update_tracks (demux)) != GST_FLOW_OK) {
      goto beach;
    }
  }

  if (!mxf_is_mxf_packet (key)) {
    GST_WARNING_OBJECT (demux,
        "Skipping non-MXF packet of size %" G_GSIZE_FORMAT " at offset %"
        G_GUINT64_FORMAT ", key: %s", gst_buffer_get_size (buffer),
        demux->offset, mxf_ul_to_string (key, key_str));
  } else if (mxf_is_partition_pack (key)) {
    ret = gst_mxf_demux_handle_partition_pack (demux, key, buffer);

    /* If this partition contains the start of an essence container
     * set the positions of all essence streams to 0
     */
    if (ret == GST_FLOW_OK && demux->current_partition
        && demux->current_partition->partition.body_sid != 0
        && demux->current_partition->partition.body_offset == 0) {
      guint i;

      for (i = 0; i < demux->essence_tracks->len; i++) {
        GstMXFDemuxEssenceTrack *etrack =
            &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

        if (etrack->body_sid != demux->current_partition->partition.body_sid)
          continue;

        etrack->position = 0;
      }
    }
  } else if (mxf_is_primer_pack (key)) {
    ret = gst_mxf_demux_handle_primer_pack (demux, key, buffer);
  } else if (mxf_is_metadata (key)) {
    ret = gst_mxf_demux_handle_metadata (demux, key, buffer);
  } else if (mxf_is_descriptive_metadata (key)) {
    ret = gst_mxf_demux_handle_descriptive_metadata (demux, key, buffer);
  } else if (mxf_is_generic_container_system_item (key)) {
    ret =
        gst_mxf_demux_handle_generic_container_system_item (demux, key, buffer);
  } else if (mxf_is_generic_container_essence_element (key) ||
      mxf_is_avid_essence_container_essence_element (key)) {
    ret =
        gst_mxf_demux_handle_generic_container_essence_element (demux, key,
        buffer, peek);
  } else if (mxf_is_random_index_pack (key)) {
    ret = gst_mxf_demux_handle_random_index_pack (demux, key, buffer);

    if (ret == GST_FLOW_OK && demux->random_access
        && !demux->index_table_segments_collected) {
      collect_index_table_segments (demux);
      demux->index_table_segments_collected = TRUE;
    }
  } else if (mxf_is_index_table_segment (key)) {
    ret =
        gst_mxf_demux_handle_index_table_segment (demux, key, buffer,
        demux->offset);
  } else if (mxf_is_fill (key)) {
    GST_DEBUG_OBJECT (demux,
        "Skipping filler packet of size %" G_GSIZE_FORMAT " at offset %"
        G_GUINT64_FORMAT, gst_buffer_get_size (buffer), demux->offset);
  } else {
    GST_DEBUG_OBJECT (demux,
        "Skipping unknown packet of size %" G_GSIZE_FORMAT " at offset %"
        G_GUINT64_FORMAT ", key: %s", gst_buffer_get_size (buffer),
        demux->offset, mxf_ul_to_string (key, key_str));
  }

  /* In pull mode try to get the last metadata */
  if (mxf_is_partition_pack (key) && ret == GST_FLOW_OK
      && demux->pull_footer_metadata
      && demux->random_access && demux->current_partition
      && demux->current_partition->partition.type == MXF_PARTITION_PACK_HEADER
      && (!demux->current_partition->partition.closed
          || !demux->current_partition->partition.complete)
      && (demux->footer_partition_pack_offset != 0 || demux->random_index_pack)) {
    GST_DEBUG_OBJECT (demux,
        "Open or incomplete header partition, trying to get final metadata from the last partitions");
    gst_mxf_demux_parse_footer_metadata (demux);
    demux->pull_footer_metadata = FALSE;

    if (demux->current_partition->partition.body_sid != 0 &&
        demux->current_partition->partition.body_offset == 0) {
      guint i;
      for (i = 0; i < demux->essence_tracks->len; i++) {
        GstMXFDemuxEssenceTrack *etrack =
            &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

        if (etrack->body_sid != demux->current_partition->partition.body_sid)
          continue;

        etrack->position = 0;
      }
    }
  }

beach:
  return ret;
}

static void
gst_mxf_demux_set_partition_for_offset (GstMXFDemux * demux, guint64 offset)
{
  GList *l;

  /* This partition will already be parsed, otherwise
   * the position wouldn't be in the index */
  for (l = demux->partitions; l; l = l->next) {
    GstMXFDemuxPartition *p = l->data;

    if (p->partition.this_partition + demux->run_in <= offset)
      demux->current_partition = p;
  }
}

static guint64
find_offset (GArray * offsets, gint64 * position, gboolean keyframe)
{
  GstMXFDemuxIndex *idx;
  guint64 current_offset = -1;
  gint64 current_position = *position;

  if (!offsets || offsets->len <= *position)
    return -1;

  idx = &g_array_index (offsets, GstMXFDemuxIndex, *position);
  if (idx->offset != 0 && (!keyframe || idx->keyframe)) {
    current_offset = idx->offset;
  } else if (idx->offset != 0) {
    current_position--;
    while (current_position >= 0) {
      idx = &g_array_index (offsets, GstMXFDemuxIndex, current_position);
      if (idx->offset == 0) {
        break;
      } else if (!idx->keyframe) {
        current_position--;
        continue;
      } else {
        current_offset = idx->offset;
        break;
      }
    }
  }

  if (current_offset == -1)
    return -1;

  *position = current_position;
  return current_offset;
}

static guint64
find_closest_offset (GArray * offsets, gint64 * position, gboolean keyframe)
{
  GstMXFDemuxIndex *idx;
  gint64 current_position = *position;

  if (!offsets || offsets->len == 0)
    return -1;

  current_position = MIN (current_position, offsets->len - 1);

  idx = &g_array_index (offsets, GstMXFDemuxIndex, current_position);
  while (idx->offset == 0 || (keyframe && !idx->keyframe)) {
    current_position--;
    if (current_position < 0)
      break;
    idx = &g_array_index (offsets, GstMXFDemuxIndex, current_position);
  }

  if (idx->offset != 0 && (!keyframe || idx->keyframe)) {
    *position = current_position;
    return idx->offset;
  }

  return -1;
}

static guint64
gst_mxf_demux_find_essence_element (GstMXFDemux * demux,
    GstMXFDemuxEssenceTrack * etrack, gint64 * position, gboolean keyframe)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 old_offset = demux->offset;
  GstMXFDemuxPartition *old_partition = demux->current_partition;
  gint i;
  guint64 offset;
  gint64 requested_position = *position;
  GstMXFDemuxIndexTable *index_table = NULL;

  GST_DEBUG_OBJECT (demux, "Trying to find essence element %" G_GINT64_FORMAT
      " of track %u with body_sid %u (keyframe %d)", *position,
      etrack->track_number, etrack->body_sid, keyframe);

  if (demux->index_tables) {
    GList *l;

    for (l = demux->index_tables; l; l = l->next) {
      GstMXFDemuxIndexTable *tmp = l->data;

      if (tmp->body_sid == etrack->body_sid
          && tmp->index_sid == etrack->index_sid) {
        index_table = tmp;
        break;
      }
    }
  }

from_index:

  if (etrack->duration > 0 && *position >= etrack->duration) {
    GST_WARNING_OBJECT (demux, "Position after end of essence track");
    return -1;
  }

  /* First try to find an offset in our index */
  offset = find_offset (etrack->offsets, position, keyframe);
  if (offset != -1) {
    GST_DEBUG_OBJECT (demux,
        "Found edit unit %" G_GINT64_FORMAT " for %" G_GINT64_FORMAT
        " in generated index at offset %" G_GUINT64_FORMAT, *position,
        requested_position, offset);
    return offset;
  }

  GST_DEBUG_OBJECT (demux, "Not found in index");
  if (!demux->random_access) {
    offset = find_closest_offset (etrack->offsets, position, keyframe);
    if (offset != -1) {
      GST_DEBUG_OBJECT (demux,
          "Starting with edit unit %" G_GINT64_FORMAT " for %" G_GINT64_FORMAT
          " in generated index at offset %" G_GUINT64_FORMAT, *position,
          requested_position, offset);
      return offset;
    }

    if (index_table) {
      offset = find_closest_offset (index_table->offsets, position, keyframe);
      if (offset != -1) {
        GST_DEBUG_OBJECT (demux,
            "Starting with edit unit %" G_GINT64_FORMAT " for %" G_GINT64_FORMAT
            " in index at offset %" G_GUINT64_FORMAT, *position,
            requested_position, offset);
        return offset;
      }
    }
  } else if (demux->random_access) {
    gint64 index_start_position = *position;

    demux->offset = demux->run_in;

    offset =
        find_closest_offset (etrack->offsets, &index_start_position, FALSE);
    if (offset != -1) {
      demux->offset = offset + demux->run_in;
      GST_DEBUG_OBJECT (demux,
          "Starting with edit unit %" G_GINT64_FORMAT " for %" G_GINT64_FORMAT
          " in generated index at offset %" G_GUINT64_FORMAT,
          index_start_position, requested_position, offset);
    } else {
      index_start_position = -1;
    }

    if (index_table) {
      gint64 tmp_position = *position;

      offset = find_closest_offset (index_table->offsets, &tmp_position, TRUE);
      if (offset != -1 && tmp_position > index_start_position) {
        demux->offset = offset + demux->run_in;
        index_start_position = tmp_position;
        GST_DEBUG_OBJECT (demux,
            "Starting with edit unit %" G_GINT64_FORMAT " for %" G_GINT64_FORMAT
            " in index at offset %" G_GUINT64_FORMAT, index_start_position,
            requested_position, offset);
      }
    }

    gst_mxf_demux_set_partition_for_offset (demux, demux->offset);

    for (i = 0; i < demux->essence_tracks->len; i++) {
      GstMXFDemuxEssenceTrack *t =
          &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

      if (index_start_position != -1 && t == etrack)
        t->position = index_start_position;
      else
        t->position = (demux->offset == demux->run_in) ? 0 : -1;
    }

    /* Else peek at all essence elements and complete our
     * index until we find the requested element
     */
    while (ret == GST_FLOW_OK) {
      GstBuffer *buffer = NULL;
      MXFUL key;
      guint read = 0;

      ret =
          gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
          &read);

      if (ret == GST_FLOW_EOS) {
        for (i = 0; i < demux->essence_tracks->len; i++) {
          GstMXFDemuxEssenceTrack *t =
              &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack,
              i);

          if (t->position > 0)
            t->duration = t->position;
        }
        /* For the searched track this is really our position */
        etrack->duration = etrack->position;

        for (i = 0; i < demux->src->len; i++) {
          GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);

          if (!p->eos
              && p->current_essence_track_position >=
              p->current_essence_track->duration) {
            GstEvent *e;

            p->eos = TRUE;
            e = gst_event_new_eos ();
            gst_event_set_seqnum (e, demux->seqnum);
            gst_pad_push_event (GST_PAD_CAST (p), e);
          }
        }
      }

      if (G_UNLIKELY (ret != GST_FLOW_OK) && etrack->position <= *position) {
        demux->offset = old_offset;
        demux->current_partition = old_partition;
        break;
      } else if (G_UNLIKELY (ret == GST_FLOW_OK)) {
        ret = gst_mxf_demux_handle_klv_packet (demux, &key, buffer, TRUE);
        gst_buffer_unref (buffer);
      }

      /* If we found the position read it from the index again */
      if (((ret == GST_FLOW_OK && etrack->position == *position + 2) ||
              (ret == GST_FLOW_EOS && etrack->position == *position + 1))
          && etrack->offsets && etrack->offsets->len > *position
          && g_array_index (etrack->offsets, GstMXFDemuxIndex,
              *position).offset != 0) {
        GST_DEBUG_OBJECT (demux, "Found at offset %" G_GUINT64_FORMAT,
            demux->offset);
        demux->offset = old_offset;
        demux->current_partition = old_partition;
        goto from_index;
      }
      demux->offset += read;
    }
    demux->offset = old_offset;
    demux->current_partition = old_partition;

    GST_DEBUG_OBJECT (demux, "Not found in this file");
  }

  return -1;
}

static GstFlowReturn
gst_mxf_demux_pull_and_handle_klv_packet (GstMXFDemux * demux)
{
  GstBuffer *buffer = NULL;
  MXFUL key;
  GstFlowReturn ret = GST_FLOW_OK;
  guint read = 0;

  if (demux->src->len > 0) {
    if (!gst_mxf_demux_get_earliest_pad (demux)) {
      ret = GST_FLOW_EOS;
      GST_DEBUG_OBJECT (demux, "All tracks are EOS");
      goto beach;
    }
  }

  ret =
      gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
      &read);

  if (ret == GST_FLOW_EOS && demux->src->len > 0) {
    guint i;
    GstMXFDemuxPad *p = NULL;

    for (i = 0; i < demux->essence_tracks->len; i++) {
      GstMXFDemuxEssenceTrack *t =
          &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

      if (t->position > 0)
        t->duration = t->position;
    }

    for (i = 0; i < demux->src->len; i++) {
      GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);

      if (!p->eos
          && p->current_essence_track_position >=
          p->current_essence_track->duration) {
        GstEvent *e;

        p->eos = TRUE;
        e = gst_event_new_eos ();
        gst_event_set_seqnum (e, demux->seqnum);
        gst_pad_push_event (GST_PAD_CAST (p), e);
      }
    }

    while ((p = gst_mxf_demux_get_earliest_pad (demux))) {
      guint64 offset;
      gint64 position;

      position = p->current_essence_track_position;

      offset =
          gst_mxf_demux_find_essence_element (demux, p->current_essence_track,
          &position, FALSE);
      if (offset == -1) {
        GstEvent *e;

        GST_ERROR_OBJECT (demux, "Failed to find offset for essence track");
        p->eos = TRUE;
        e = gst_event_new_eos ();
        gst_event_set_seqnum (e, demux->seqnum);
        gst_pad_push_event (GST_PAD_CAST (p), e);
        continue;
      }

      demux->offset = offset + demux->run_in;
      gst_mxf_demux_set_partition_for_offset (demux, demux->offset);

      p->current_essence_track->position = position;

      ret = GST_FLOW_OK;
      goto beach;
    }
  }

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto beach;

  ret = gst_mxf_demux_handle_klv_packet (demux, &key, buffer, FALSE);
  demux->offset += read;

  if (ret == GST_FLOW_OK && demux->src->len > 0
      && demux->essence_tracks->len > 0) {
    GstMXFDemuxPad *earliest = NULL;
    /* We allow time drifts of at most 500ms */
    while ((earliest = gst_mxf_demux_get_earliest_pad (demux)) &&
        demux->segment.position - earliest->position > demux->max_drift) {
      guint64 offset;
      gint64 position;

      GST_WARNING_OBJECT (demux,
          "Found synchronization issue -- trying to solve");

      position = earliest->current_essence_track_position;

      /* FIXME: This can probably be improved by using the
       * offset of position-1 if it's in the same partition
       * or the start of the position otherwise.
       * This way we won't skip elements from the same essence
       * container as etrack->position
       */
      offset =
          gst_mxf_demux_find_essence_element (demux,
          earliest->current_essence_track, &position, FALSE);
      if (offset == -1) {
        GstEvent *e;

        GST_WARNING_OBJECT (demux,
            "Failed to find offset for late essence track");
        earliest->eos = TRUE;
        e = gst_event_new_eos ();
        gst_event_set_seqnum (e, demux->seqnum);
        gst_pad_push_event (GST_PAD_CAST (earliest), e);
        continue;
      }

      demux->offset = offset + demux->run_in;
      gst_mxf_demux_set_partition_for_offset (demux, demux->offset);

      earliest->current_essence_track->position = position;
      break;
    }
  }

beach:
  if (buffer)
    gst_buffer_unref (buffer);

  return ret;
}

static void
gst_mxf_demux_loop (GstPad * pad)
{
  GstMXFDemux *demux = NULL;
  GstFlowReturn flow = GST_FLOW_OK;
  GstMapInfo map;
  gboolean res;

  demux = GST_MXF_DEMUX (gst_pad_get_parent (pad));

  if (demux->run_in == -1) {
    /* Skip run-in, which is at most 64K and is finished
     * by a header partition pack */
    while (demux->offset < 64 * 1024) {
      GstBuffer *buffer = NULL;

      if ((flow =
              gst_mxf_demux_pull_range (demux, demux->offset, 16,
                  &buffer)) != GST_FLOW_OK)
        break;

      gst_buffer_map (buffer, &map, GST_MAP_READ);
      res = mxf_is_header_partition_pack ((const MXFUL *) map.data);
      gst_buffer_unmap (buffer, &map);

      if (res) {
        GST_DEBUG_OBJECT (demux,
            "Found header partition pack at offset %" G_GUINT64_FORMAT,
            demux->offset);
        demux->run_in = demux->offset;
        gst_buffer_unref (buffer);
        break;
      }

      demux->offset++;
      gst_buffer_unref (buffer);
    }

    if (G_UNLIKELY (flow != GST_FLOW_OK))
      goto pause;

    if (G_UNLIKELY (demux->run_in == -1)) {
      GST_ERROR_OBJECT (demux, "No valid header partition pack found");
      flow = GST_FLOW_ERROR;
      goto pause;
    }

    /* First of all pull&parse the random index pack at EOF */
    gst_mxf_demux_pull_random_index_pack (demux);
  }

  /* Now actually do something */
  flow = gst_mxf_demux_pull_and_handle_klv_packet (demux);

  /* pause if something went wrong */
  if (G_UNLIKELY (flow != GST_FLOW_OK))
    goto pause;

  /* check EOS condition */
  if ((demux->segment.flags & GST_SEEK_FLAG_SEGMENT) &&
      (demux->segment.stop != -1) &&
      (demux->segment.position >= demux->segment.stop)) {
    guint i;
    gboolean eos = TRUE;

    for (i = 0; i < demux->src->len; i++) {
      GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);

      if (!p->eos && p->position < demux->segment.stop) {
        eos = FALSE;
        break;
      }
    }

    if (eos) {
      flow = GST_FLOW_EOS;
      goto pause;
    }
  }

  gst_object_unref (demux);

  return;

pause:
  {
    const gchar *reason = gst_flow_get_name (flow);

    GST_LOG_OBJECT (demux, "pausing task, reason %s", reason);
    gst_pad_pause_task (pad);

    if (flow == GST_FLOW_EOS) {
      /* perform EOS logic */
      if (demux->src->len == 0) {
        GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE,
            ("This stream contains no data."),
            ("got eos and didn't find any streams"));
      } else if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        gint64 stop;
        GstMessage *m;
        GstEvent *e;

        /* for segment playback we need to post when (in stream time)
         * we stopped, this is either stop (when set) or the duration. */
        if ((stop = demux->segment.stop) == -1)
          stop = demux->segment.duration;

        GST_LOG_OBJECT (demux, "Sending segment done, at end of segment");
        m = gst_message_new_segment_done (GST_OBJECT_CAST (demux),
            GST_FORMAT_TIME, stop);
        gst_message_set_seqnum (m, demux->seqnum);
        gst_element_post_message (GST_ELEMENT_CAST (demux), m);
        e = gst_event_new_segment_done (GST_FORMAT_TIME, stop);
        gst_event_set_seqnum (e, demux->seqnum);
        gst_mxf_demux_push_src_event (demux, e);
      } else {
        GstEvent *e;

        /* normal playback, send EOS to all linked pads */
        GST_LOG_OBJECT (demux, "Sending EOS, at end of stream");
        e = gst_event_new_eos ();
        gst_event_set_seqnum (e, demux->seqnum);
        if (!gst_mxf_demux_push_src_event (demux, e)) {
          GST_WARNING_OBJECT (demux, "failed pushing EOS on streams");
        }
      }
    } else if (flow == GST_FLOW_NOT_LINKED || flow < GST_FLOW_EOS) {
      GstEvent *e;

      GST_ELEMENT_FLOW_ERROR (demux, flow);
      e = gst_event_new_eos ();
      gst_event_set_seqnum (e, demux->seqnum);
      gst_mxf_demux_push_src_event (demux, e);
    }
    gst_object_unref (demux);
    return;
  }
}

static GstFlowReturn
gst_mxf_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMXFDemux *demux = NULL;
  MXFUL key;
  const guint8 *data = NULL;
  guint64 length = 0;
  guint64 offset = 0;
  GstBuffer *buffer = NULL;
  gboolean res;
#ifndef GST_DISABLE_GST_DEBUG
  gchar str[48];
#endif

  demux = GST_MXF_DEMUX (parent);

  GST_LOG_OBJECT (demux,
      "received buffer of %" G_GSIZE_FORMAT " bytes at offset %"
      G_GUINT64_FORMAT, gst_buffer_get_size (inbuf), GST_BUFFER_OFFSET (inbuf));

  if (demux->src->len > 0) {
    if (!gst_mxf_demux_get_earliest_pad (demux)) {
      ret = GST_FLOW_EOS;
      GST_DEBUG_OBJECT (demux, "All tracks are EOS");
      return ret;
    }
  }

  if (G_UNLIKELY (GST_BUFFER_OFFSET (inbuf) == 0)) {
    GST_DEBUG_OBJECT (demux, "beginning of file, expect header");
    demux->run_in = -1;
    demux->offset = 0;
  }

  if (G_UNLIKELY (demux->offset == 0 && GST_BUFFER_OFFSET (inbuf) != 0)) {
    GST_DEBUG_OBJECT (demux, "offset was zero, synchronizing with buffer's");
    if (GST_BUFFER_OFFSET_IS_VALID (inbuf))
      demux->offset = GST_BUFFER_OFFSET (inbuf);
    gst_mxf_demux_set_partition_for_offset (demux, demux->offset);
  } else if (demux->current_partition == NULL) {
    gst_mxf_demux_set_partition_for_offset (demux, demux->offset);
  }

  gst_adapter_push (demux->adapter, inbuf);
  inbuf = NULL;

  while (ret == GST_FLOW_OK) {
    if (G_UNLIKELY (demux->flushing)) {
      GST_DEBUG_OBJECT (demux, "we are now flushing, exiting parser loop");
      ret = GST_FLOW_FLUSHING;
      break;
    }

    if (gst_adapter_available (demux->adapter) < 16)
      break;

    if (demux->run_in == -1) {
      /* Skip run-in, which is at most 64K and is finished
       * by a header partition pack */

      while (demux->offset < 64 * 1024
          && gst_adapter_available (demux->adapter) >= 16) {
        data = gst_adapter_map (demux->adapter, 16);
        res = mxf_is_header_partition_pack ((const MXFUL *) data);
        gst_adapter_unmap (demux->adapter);

        if (res) {
          GST_DEBUG_OBJECT (demux,
              "Found header partition pack at offset %" G_GUINT64_FORMAT,
              demux->offset);
          demux->run_in = demux->offset;
          break;
        }
        gst_adapter_flush (demux->adapter, 1);
        demux->offset++;
      }
    } else if (demux->offset < demux->run_in) {
      guint64 flush = MIN (gst_adapter_available (demux->adapter),
          demux->run_in - demux->offset);
      gst_adapter_flush (demux->adapter, flush);
      demux->offset += flush;
      continue;
    }

    /* Need more data */
    if (demux->run_in == -1 && demux->offset < 64 * 1024)
      break;

    if (G_UNLIKELY (demux->run_in == -1)) {
      GST_ERROR_OBJECT (demux, "No valid header partition pack found");
      ret = GST_FLOW_ERROR;
      break;
    }

    if (gst_adapter_available (demux->adapter) < 17)
      break;

    /* Now actually do something */
    memset (&key, 0, sizeof (MXFUL));

    /* Pull 16 byte key and first byte of BER encoded length */
    data = gst_adapter_map (demux->adapter, 17);

    memcpy (&key, data, 16);

    GST_DEBUG_OBJECT (demux, "Got KLV packet with key %s",
        mxf_ul_to_string (&key, str));

    /* Decode BER encoded packet length */
    if ((data[16] & 0x80) == 0) {
      length = data[16];
      offset = 17;
    } else {
      guint slen = data[16] & 0x7f;

      offset = 16 + 1 + slen;

      gst_adapter_unmap (demux->adapter);

      /* Must be at most 8 according to SMPTE-379M 5.3.4 and
       * GStreamer buffers can only have a 4 bytes length */
      if (slen > 8) {
        GST_ERROR_OBJECT (demux, "Invalid KLV packet length: %u", slen);
        ret = GST_FLOW_ERROR;
        break;
      }

      if (gst_adapter_available (demux->adapter) < 17 + slen)
        break;

      data = gst_adapter_map (demux->adapter, 17 + slen);
      data += 17;

      length = 0;
      while (slen) {
        length = (length << 8) | *data;
        data++;
        slen--;
      }
    }

    gst_adapter_unmap (demux->adapter);

    /* GStreamer's buffer sizes are stored in a guint so we
     * limit ourself to G_MAXUINT large buffers */
    if (length > G_MAXUINT) {
      GST_ERROR_OBJECT (demux,
          "Unsupported KLV packet length: %" G_GUINT64_FORMAT, length);
      ret = GST_FLOW_ERROR;
      break;
    }

    GST_DEBUG_OBJECT (demux, "KLV packet with key %s has length "
        "%" G_GUINT64_FORMAT, mxf_ul_to_string (&key, str), length);

    if (gst_adapter_available (demux->adapter) < offset + length)
      break;

    gst_adapter_flush (demux->adapter, offset);

    if (length > 0) {
      buffer = gst_adapter_take_buffer (demux->adapter, length);

      ret = gst_mxf_demux_handle_klv_packet (demux, &key, buffer, FALSE);
      gst_buffer_unref (buffer);
    }

    demux->offset += offset + length;
  }

  return ret;
}

static void
gst_mxf_demux_pad_set_position (GstMXFDemux * demux, GstMXFDemuxPad * p,
    GstClockTime start)
{
  guint i;
  guint64 sum = 0;
  MXFMetadataSourceClip *clip = NULL;

  if (!p->current_component) {
    p->current_essence_track_position =
        gst_util_uint64_scale (start, p->material_track->edit_rate.n,
        p->material_track->edit_rate.d * GST_SECOND);

    if (p->current_essence_track_position >= p->current_essence_track->duration
        && p->current_essence_track->duration > 0) {
      p->current_essence_track_position = p->current_essence_track->duration;
      p->position =
          gst_util_uint64_scale (p->current_essence_track->duration,
          p->material_track->edit_rate.d * GST_SECOND,
          p->material_track->edit_rate.n);
    } else {
      p->position = start;
    }
    p->position_accumulated_error = 0.0;
    p->current_material_track_position = p->current_essence_track_position;

    return;
  }

  for (i = 0; i < p->material_track->parent.sequence->n_structural_components;
      i++) {
    clip =
        MXF_METADATA_SOURCE_CLIP (p->material_track->parent.sequence->
        structural_components[i]);

    if (clip->parent.duration <= 0)
      break;

    sum += clip->parent.duration;

    if (gst_util_uint64_scale (sum, p->material_track->edit_rate.d * GST_SECOND,
            p->material_track->edit_rate.n) > start)
      break;
  }

  if (i == p->material_track->parent.sequence->n_structural_components) {
    p->position =
        gst_util_uint64_scale (sum, p->material_track->edit_rate.d * GST_SECOND,
        p->material_track->edit_rate.n);
    p->position_accumulated_error = 0.0;
    p->current_material_track_position = sum;

    gst_mxf_demux_pad_set_component (demux, p, i);
    return;
  }

  if (clip->parent.duration > 0)
    sum -= clip->parent.duration;

  start -=
      gst_util_uint64_scale (sum, p->material_track->edit_rate.d * GST_SECOND,
      p->material_track->edit_rate.n);

  gst_mxf_demux_pad_set_component (demux, p, i);

  {
    gint64 essence_offset = gst_util_uint64_scale (start,
        p->current_essence_track->source_track->edit_rate.n,
        p->current_essence_track->source_track->edit_rate.d * GST_SECOND);

    p->current_essence_track_position += essence_offset;

    p->position = gst_util_uint64_scale (sum,
        GST_SECOND * p->material_track->edit_rate.d,
        p->material_track->edit_rate.n) + gst_util_uint64_scale (essence_offset,
        GST_SECOND * p->current_essence_track->source_track->edit_rate.d,
        p->current_essence_track->source_track->edit_rate.n);
    p->position_accumulated_error = 0.0;
    p->current_material_track_position = sum + essence_offset;
  }

  if (p->current_essence_track_position >= p->current_essence_track->duration
      && p->current_essence_track->duration > 0) {
    p->current_essence_track_position = p->current_essence_track->duration;
    p->position =
        gst_util_uint64_scale (sum + p->current_component->parent.duration,
        p->material_track->edit_rate.d * GST_SECOND,
        p->material_track->edit_rate.n);
    p->position_accumulated_error = 0.0;
    p->current_material_track_position =
        sum + p->current_component->parent.duration;
  }
}

static gboolean
gst_mxf_demux_seek_push (GstMXFDemux * demux, GstEvent * event)
{
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gdouble rate;
  gboolean update, flush, keyframe;
  GstSegment seeksegment;
  guint i;
  guint32 seqnum;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &start_type, &start, &stop_type, &stop);
  seqnum = gst_event_get_seqnum (event);

  if (rate <= 0.0)
    goto wrong_rate;

  if (format != GST_FORMAT_TIME)
    goto wrong_format;

  flush = ! !(flags & GST_SEEK_FLAG_FLUSH);
  keyframe = ! !(flags & GST_SEEK_FLAG_KEY_UNIT);

  /* Work on a copy until we are sure the seek succeeded. */
  memcpy (&seeksegment, &demux->segment, sizeof (GstSegment));

  GST_DEBUG_OBJECT (demux, "segment before configure %" GST_SEGMENT_FORMAT,
      &demux->segment);

  /* Apply the seek to our segment */
  gst_segment_do_seek (&seeksegment, rate, format, flags,
      start_type, start, stop_type, stop, &update);

  GST_DEBUG_OBJECT (demux, "segment configured %" GST_SEGMENT_FORMAT,
      &seeksegment);

  if (flush || seeksegment.position != demux->segment.position) {
    gboolean ret;
    guint64 new_offset = -1;
    GstEvent *e;

    if (!demux->metadata_resolved || demux->update_metadata) {
      if (gst_mxf_demux_resolve_references (demux) != GST_FLOW_OK ||
          gst_mxf_demux_update_tracks (demux) != GST_FLOW_OK) {
        goto unresolved_metadata;
      }
    }

    /* Do the actual seeking */
    for (i = 0; i < demux->src->len; i++) {
      GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);
      gint64 position;
      guint64 off;

      /* Reset EOS flag on all pads */
      p->eos = FALSE;
      gst_mxf_demux_pad_set_position (demux, p, start);

      position = p->current_essence_track_position;
      off = gst_mxf_demux_find_essence_element (demux, p->current_essence_track,
          &position, keyframe);
      new_offset = MIN (off, new_offset);
      p->discont = TRUE;
    }

    if (new_offset == -1)
      goto no_new_offset;

    new_offset += demux->run_in;

    GST_DEBUG_OBJECT (demux, "generating an upstream seek at position %"
        G_GUINT64_FORMAT, new_offset);
    e = gst_event_new_seek (seeksegment.rate, GST_FORMAT_BYTES,
        seeksegment.flags | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET,
        new_offset, GST_SEEK_TYPE_NONE, 0);
    gst_event_set_seqnum (e, seqnum);
    ret = gst_pad_push_event (demux->sinkpad, e);

    if (G_UNLIKELY (!ret)) {
      goto seek_failed;
    }
  }

  /* Tell all the stream a new segment is needed */
  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);
    p->need_segment = TRUE;
  }

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *t =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);
    t->position = -1;
  }

  /* Ok seek succeeded, take the newly configured segment */
  memcpy (&demux->segment, &seeksegment, sizeof (GstSegment));

  return TRUE;

/* ERRORS */
wrong_format:
  {
    GST_WARNING_OBJECT (demux, "seeking only supported in TIME format");
    return gst_pad_push_event (demux->sinkpad, gst_event_ref (event));
  }
wrong_rate:
  {
    GST_WARNING_OBJECT (demux, "only rates > 0.0 are allowed");
    return FALSE;
  }
unresolved_metadata:
  {
    GST_WARNING_OBJECT (demux, "metadata can't be resolved");
    return gst_pad_push_event (demux->sinkpad, gst_event_ref (event));
  }
seek_failed:
  {
    GST_WARNING_OBJECT (demux, "upstream seek failed");
    return gst_pad_push_event (demux->sinkpad, gst_event_ref (event));
  }
no_new_offset:
  {
    GST_WARNING_OBJECT (demux, "can't find new offset");
    return gst_pad_push_event (demux->sinkpad, gst_event_ref (event));
  }
}

static void
collect_index_table_segments (GstMXFDemux * demux)
{
  GList *l;
  guint i;
  guint64 old_offset = demux->offset;
  GstMXFDemuxPartition *old_partition = demux->current_partition;

  if (!demux->random_index_pack)
    return;

  for (i = 0; i < demux->random_index_pack->len; i++) {
    MXFRandomIndexPackEntry *e =
        &g_array_index (demux->random_index_pack, MXFRandomIndexPackEntry, i);

    if (e->offset < demux->run_in) {
      GST_ERROR_OBJECT (demux, "Invalid random index pack entry");
      return;
    }

    demux->offset = e->offset;
    read_partition_header (demux);
  }

  demux->offset = old_offset;
  demux->current_partition = old_partition;

  for (l = demux->pending_index_table_segments; l; l = l->next) {
    MXFIndexTableSegment *segment = l->data;
    GstMXFDemuxIndexTable *t = NULL;
    GList *k;
    guint64 start, end;

    for (k = demux->index_tables; k; k = k->next) {
      GstMXFDemuxIndexTable *tmp = k->data;

      if (tmp->body_sid == segment->body_sid
          && tmp->index_sid == segment->index_sid) {
        t = tmp;
        break;
      }
    }

    if (!t) {
      t = g_new0 (GstMXFDemuxIndexTable, 1);
      t->body_sid = segment->body_sid;
      t->index_sid = segment->index_sid;
      t->offsets = g_array_new (FALSE, TRUE, sizeof (GstMXFDemuxIndex));
      demux->index_tables = g_list_prepend (demux->index_tables, t);
    }

    start = segment->index_start_position;
    end = start + segment->index_duration;
    if (end > G_MAXINT / sizeof (GstMXFDemuxIndex)) {
      demux->index_tables = g_list_remove (demux->index_tables, t);
      g_array_free (t->offsets, TRUE);
      g_free (t);
      continue;
    }

    if (t->offsets->len < end)
      g_array_set_size (t->offsets, end);

    for (i = 0; i < segment->n_index_entries && start + i < t->offsets->len;
        i++) {
      guint64 offset = segment->index_entries[i].stream_offset;
      GList *m;
      GstMXFDemuxPartition *offset_partition = NULL, *next_partition = NULL;

      for (m = demux->partitions; m; m = m->next) {
        GstMXFDemuxPartition *partition = m->data;

        if (!next_partition && offset_partition)
          next_partition = partition;

        if (partition->partition.body_sid != t->body_sid)
          continue;
        if (partition->partition.body_offset > offset)
          break;

        offset_partition = partition;
        next_partition = NULL;
      }

      if (offset_partition && offset >= offset_partition->partition.body_offset) {
        offset =
            offset_partition->partition.this_partition +
            offset_partition->essence_container_offset + (offset -
            offset_partition->partition.body_offset);

        if (next_partition
            && offset >= next_partition->partition.this_partition) {
          GST_ERROR_OBJECT (demux,
              "Invalid index table segment going into next unrelated partition");
        } else {
          GstMXFDemuxIndex *index;
          gint8 temporal_offset = segment->index_entries[i].temporal_offset;
          guint64 pts_i = G_MAXUINT64;

          if (temporal_offset > 0 ||
              (temporal_offset < 0 && start + i >= -(gint) temporal_offset)) {
            pts_i = start + i + temporal_offset;

            if (t->offsets->len < pts_i)
              g_array_set_size (t->offsets, pts_i + 1);

            index = &g_array_index (t->offsets, GstMXFDemuxIndex, pts_i);
            if (!index->initialized) {
              index->initialized = TRUE;
              index->offset = 0;
              index->pts = G_MAXUINT64;
              index->dts = G_MAXUINT64;
              index->keyframe = FALSE;
            }

            index->pts = start + i;
          }

          index = &g_array_index (t->offsets, GstMXFDemuxIndex, start + i);
          if (!index->initialized) {
            index->initialized = TRUE;
            index->offset = 0;
            index->pts = G_MAXUINT64;
            index->dts = G_MAXUINT64;
            index->keyframe = FALSE;
          }

          index->offset = offset;
          index->keyframe = ! !(segment->index_entries[i].flags & 0x80)
              || (segment->index_entries[i].key_frame_offset == 0);
          index->dts = pts_i;
        }
      }
    }
  }

  for (l = demux->pending_index_table_segments; l; l = l->next) {
    MXFIndexTableSegment *s = l->data;
    mxf_index_table_segment_reset (s);
    g_free (s);
  }
  g_list_free (demux->pending_index_table_segments);
  demux->pending_index_table_segments = NULL;
}

static gboolean
gst_mxf_demux_seek_pull (GstMXFDemux * demux, GstEvent * event)
{
  GstClockTime keyunit_ts;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gdouble rate;
  gboolean update, flush, keyframe;
  GstSegment seeksegment;
  guint i;
  gboolean ret = TRUE;
  guint32 seqnum;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &start_type, &start, &stop_type, &stop);
  seqnum = gst_event_get_seqnum (event);

  if (format != GST_FORMAT_TIME)
    goto wrong_format;

  if (rate <= 0.0)
    goto wrong_rate;

  flush = ! !(flags & GST_SEEK_FLAG_FLUSH);
  keyframe = ! !(flags & GST_SEEK_FLAG_KEY_UNIT);

  keyunit_ts = start;

  if (!demux->index_table_segments_collected) {
    collect_index_table_segments (demux);
    demux->index_table_segments_collected = TRUE;
  }

  if (flush) {
    GstEvent *e;

    /* Flush start up and downstream to make sure data flow and loops are
       idle */
    e = gst_event_new_flush_start ();
    gst_event_set_seqnum (e, seqnum);
    gst_mxf_demux_push_src_event (demux, gst_event_ref (e));
    gst_pad_push_event (demux->sinkpad, e);
  } else {
    /* Pause the pulling task */
    gst_pad_pause_task (demux->sinkpad);
  }

  /* Take the stream lock */
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  if (flush) {
    GstEvent *e;

    /* Stop flushing upstream we need to pull */
    e = gst_event_new_flush_stop (TRUE);
    gst_event_set_seqnum (e, seqnum);
    gst_pad_push_event (demux->sinkpad, e);
  }

  /* Work on a copy until we are sure the seek succeeded. */
  memcpy (&seeksegment, &demux->segment, sizeof (GstSegment));

  GST_DEBUG_OBJECT (demux, "segment before configure %" GST_SEGMENT_FORMAT,
      &demux->segment);

  /* Apply the seek to our segment */
  gst_segment_do_seek (&seeksegment, rate, format, flags,
      start_type, start, stop_type, stop, &update);

  GST_DEBUG_OBJECT (demux, "segment configured %" GST_SEGMENT_FORMAT,
      &seeksegment);

  if (flush || seeksegment.position != demux->segment.position) {
    guint64 new_offset = -1;

    if (!demux->metadata_resolved || demux->update_metadata) {
      if (gst_mxf_demux_resolve_references (demux) != GST_FLOW_OK ||
          gst_mxf_demux_update_tracks (demux) != GST_FLOW_OK) {
        goto unresolved_metadata;
      }
    }

    /* Do the actual seeking */
    for (i = 0; i < demux->src->len; i++) {
      MXFMetadataTrackType track_type = MXF_METADATA_TRACK_UNKNOWN;
      GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);
      gint64 position;
      guint64 off;

      if (p->material_track != NULL)
        track_type = p->material_track->parent.type;

      /* Reset EOS flag on all pads */
      p->eos = FALSE;
      gst_mxf_demux_pad_set_position (demux, p, start);

      /* we always want to send data starting with a key unit */
      position = p->current_essence_track_position;
      off =
          gst_mxf_demux_find_essence_element (demux, p->current_essence_track,
          &position, TRUE);
      if (off == -1) {
        GST_DEBUG_OBJECT (demux, "Unable to find offset for pad %s",
            GST_PAD_NAME (p));
        p->current_essence_track_position = p->current_essence_track->duration;
      } else {
        new_offset = MIN (off, new_offset);
        if (position != p->current_essence_track_position) {
          p->position -=
              gst_util_uint64_scale (p->current_essence_track_position -
              position,
              GST_SECOND * p->current_essence_track->source_track->edit_rate.d,
              p->current_essence_track->source_track->edit_rate.n);
          p->position_accumulated_error = 0.0;
          p->current_material_track_position -=
              gst_util_uint64_scale (p->current_essence_track_position -
              position,
              p->material_track->edit_rate.n *
              p->current_essence_track->source_track->edit_rate.d,
              p->material_track->edit_rate.d *
              p->current_essence_track->source_track->edit_rate.n);
        }
        p->current_essence_track_position = position;

        /* FIXME: what about DV + MPEG-TS container essence tracks? */
        if (track_type == MXF_METADATA_TRACK_PICTURE_ESSENCE) {
          keyunit_ts = MIN (p->position, keyunit_ts);
        }
      }
      p->discont = TRUE;
    }
    gst_flow_combiner_reset (demux->flowcombiner);
    if (new_offset == -1) {
      GST_WARNING_OBJECT (demux, "No new offset found");
      ret = FALSE;
    } else {
      demux->offset = new_offset + demux->run_in;
    }
    gst_mxf_demux_set_partition_for_offset (demux, demux->offset);
  }

  if (G_UNLIKELY (demux->close_seg_event)) {
    gst_event_unref (demux->close_seg_event);
    demux->close_seg_event = NULL;
  }

  if (flush) {
    GstEvent *e;

    /* Stop flushing, the sinks are at time 0 now */
    e = gst_event_new_flush_stop (TRUE);
    gst_event_set_seqnum (e, seqnum);
    gst_mxf_demux_push_src_event (demux, e);
  } else {
    GST_DEBUG_OBJECT (demux, "closing running segment %" GST_SEGMENT_FORMAT,
        &demux->segment);

    /* Close the current segment for a linear playback */
    demux->close_seg_event = gst_event_new_segment (&demux->segment);
    gst_event_set_seqnum (demux->close_seg_event, demux->seqnum);
  }

  if (keyframe && keyunit_ts != start) {
    GST_INFO_OBJECT (demux, "key unit seek, adjusting segment start to "
        "%" GST_TIME_FORMAT, GST_TIME_ARGS (keyunit_ts));
    gst_segment_do_seek (&seeksegment, rate, format, flags,
        start_type, keyunit_ts, stop_type, stop, &update);
  }

  /* Ok seek succeeded, take the newly configured segment */
  memcpy (&demux->segment, &seeksegment, sizeof (GstSegment));

  /* Notify about the start of a new segment */
  if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    GstMessage *m;

    m = gst_message_new_segment_start (GST_OBJECT (demux),
        demux->segment.format, demux->segment.position);
    gst_message_set_seqnum (m, seqnum);
    gst_element_post_message (GST_ELEMENT (demux), m);
  }

  /* Tell all the stream a new segment is needed */
  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);
    p->need_segment = TRUE;
  }

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *t =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);
    t->position = -1;
  }

  demux->seqnum = seqnum;

  gst_pad_start_task (demux->sinkpad,
      (GstTaskFunction) gst_mxf_demux_loop, demux->sinkpad, NULL);

  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return ret;

  /* ERRORS */
wrong_format:
  {
    GST_WARNING_OBJECT (demux, "seeking only supported in TIME format");
    return FALSE;
  }
wrong_rate:
  {
    GST_WARNING_OBJECT (demux, "only rates > 0.0 are allowed");
    return FALSE;
  }
unresolved_metadata:
  {
    gst_pad_start_task (demux->sinkpad,
        (GstTaskFunction) gst_mxf_demux_loop, demux->sinkpad, NULL);
    GST_PAD_STREAM_UNLOCK (demux->sinkpad);
    GST_WARNING_OBJECT (demux, "metadata can't be resolved");
    return FALSE;
  }
}

static gboolean
gst_mxf_demux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (parent);
  gboolean ret;

  GST_DEBUG_OBJECT (pad, "handling event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (demux->random_access)
        ret = gst_mxf_demux_seek_pull (demux, event);
      else
        ret = gst_mxf_demux_seek_push (demux, event);
      gst_event_unref (event);
      break;
    default:
      ret = gst_pad_push_event (demux->sinkpad, event);
      break;
  }

  return ret;
}

static gboolean
gst_mxf_demux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (parent);
  gboolean ret = FALSE;
  GstMXFDemuxPad *mxfpad = GST_MXF_DEMUX_PAD (pad);

  GST_DEBUG_OBJECT (pad, "handling query %s",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 pos;

      gst_query_parse_position (query, &format, NULL);
      if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT)
        goto error;

      pos =
          format ==
          GST_FORMAT_DEFAULT ? mxfpad->current_material_track_position :
          mxfpad->position;

      GST_DEBUG_OBJECT (pad,
          "Returning position %" G_GINT64_FORMAT " in format %s", pos,
          gst_format_get_name (format));

      gst_query_set_position (query, format, pos);
      ret = TRUE;

      break;
    }
    case GST_QUERY_DURATION:{
      gint64 duration;
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);
      if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT)
        goto error;

      g_rw_lock_reader_lock (&demux->metadata_lock);
      if (!mxfpad->material_track || !mxfpad->material_track->parent.sequence) {
        g_rw_lock_reader_unlock (&demux->metadata_lock);
        goto error;
      }

      duration = mxfpad->material_track->parent.sequence->duration;
      if (duration <= -1)
        duration = -1;

      if (duration != -1 && format == GST_FORMAT_TIME) {
        if (mxfpad->material_track->edit_rate.n == 0 ||
            mxfpad->material_track->edit_rate.d == 0) {
          g_rw_lock_reader_unlock (&demux->metadata_lock);
          goto error;
        }

        duration =
            gst_util_uint64_scale (duration,
            GST_SECOND * mxfpad->material_track->edit_rate.d,
            mxfpad->material_track->edit_rate.n);
      }
      g_rw_lock_reader_unlock (&demux->metadata_lock);

      GST_DEBUG_OBJECT (pad,
          "Returning duration %" G_GINT64_FORMAT " in format %s", duration,
          gst_format_get_name (format));

      gst_query_set_duration (query, format, duration);
      ret = TRUE;
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gint64 duration;

      ret = TRUE;
      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt != GST_FORMAT_TIME) {
        gst_query_set_seeking (query, fmt, FALSE, -1, -1);
        goto done;
      }

      if (!gst_pad_query_duration (pad, GST_FORMAT_TIME, &duration)) {
        gst_query_set_seeking (query, fmt, FALSE, -1, -1);
        goto done;
      }

      if (demux->random_access) {
        gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0, duration);
      } else {
        GstQuery *peerquery = gst_query_new_seeking (GST_FORMAT_BYTES);
        gboolean seekable;

        seekable = gst_pad_peer_query (demux->sinkpad, peerquery);
        if (seekable)
          gst_query_parse_seeking (peerquery, NULL, &seekable, NULL, NULL);
        if (seekable)
          gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0, duration);
        else
          gst_query_set_seeking (query, GST_FORMAT_TIME, FALSE, -1, -1);

        gst_query_unref (peerquery);
      }

      break;
    }
    case GST_QUERY_SEGMENT:{
      GstFormat format;
      gint64 start, stop;

      format = demux->segment.format;

      start =
          gst_segment_to_stream_time (&demux->segment, format,
          demux->segment.start);
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;
      else
        stop = gst_segment_to_stream_time (&demux->segment, format, stop);

      gst_query_set_segment (query, demux->segment.rate, format, start, stop);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

done:
  return ret;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (pad, "query failed");
    goto done;
  }
}

static gboolean
gst_mxf_demux_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  GstPadMode mode = GST_PAD_MODE_PUSH;

  query = gst_query_new_scheduling ();

  if (gst_pad_peer_query (sinkpad, query)) {
    if (gst_query_has_scheduling_mode_with_flags (query,
            GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE)) {
      GstSchedulingFlags flags;
      gst_query_parse_scheduling (query, &flags, NULL, NULL, NULL);
      if (!(flags & GST_SCHEDULING_FLAG_SEQUENTIAL))
        mode = GST_PAD_MODE_PULL;
    }
  }
  gst_query_unref (query);

  return gst_pad_activate_mode (sinkpad, mode, TRUE);
}

static gboolean
gst_mxf_demux_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstMXFDemux *demux;

  demux = GST_MXF_DEMUX (parent);

  if (mode == GST_PAD_MODE_PUSH) {
    demux->random_access = FALSE;
  } else {
    if (active) {
      demux->random_access = TRUE;
      return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_mxf_demux_loop,
          sinkpad, NULL);
    } else {
      demux->random_access = FALSE;
      return gst_pad_stop_task (sinkpad);
    }
  }

  return TRUE;
}

static gboolean
gst_mxf_demux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMXFDemux *demux;
  gboolean ret = FALSE;

  demux = GST_MXF_DEMUX (parent);

  GST_DEBUG_OBJECT (pad, "handling event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      demux->flushing = TRUE;
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (demux, "flushing queued data in the MXF demuxer");

      gst_adapter_clear (demux->adapter);
      demux->flushing = FALSE;
      demux->offset = 0;
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_EOS:{
      GstMXFDemuxPad *p = NULL;
      guint i;

      if (demux->src->len == 0) {
        GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE,
            ("This stream contains no data."),
            ("got eos and didn't find any streams"));
      }

      for (i = 0; i < demux->essence_tracks->len; i++) {
        GstMXFDemuxEssenceTrack *t =
            &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

        if (t->position > 0)
          t->duration = t->position;
      }

      for (i = 0; i < demux->src->len; i++) {
        GstMXFDemuxPad *p = g_ptr_array_index (demux->src, i);

        if (!p->eos
            && p->current_essence_track_position >=
            p->current_essence_track->duration) {
          p->eos = TRUE;
          gst_pad_push_event (GST_PAD_CAST (p), gst_event_new_eos ());
        }
      }

      while ((p = gst_mxf_demux_get_earliest_pad (demux))) {
        guint64 offset;
        gint64 position;

        position = p->current_essence_track_position;

        offset =
            gst_mxf_demux_find_essence_element (demux, p->current_essence_track,
            &position, FALSE);
        if (offset == -1) {
          GST_ERROR_OBJECT (demux, "Failed to find offset for essence track");
          p->eos = TRUE;
          gst_pad_push_event (GST_PAD_CAST (p), gst_event_new_eos ());
          continue;
        }

        if (gst_pad_push_event (demux->sinkpad,
                gst_event_new_seek (demux->segment.rate, GST_FORMAT_BYTES,
                    demux->segment.flags | GST_SEEK_FLAG_ACCURATE,
                    GST_SEEK_TYPE_SET, offset + demux->run_in,
                    GST_SEEK_TYPE_NONE, 0))) {

          for (i = 0; i < demux->essence_tracks->len; i++) {
            GstMXFDemuxEssenceTrack *etrack =
                &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack,
                i);
            etrack->position = -1;
          }
          ret = TRUE;
          goto out;
        } else {
          GST_WARNING_OBJECT (demux,
              "Seek to remaining part of the file failed");
          p->eos = TRUE;
          gst_pad_push_event (GST_PAD_CAST (p), gst_event_new_eos ());
          continue;
        }
      }

      /* and one more time for good measure apparently? */
      gst_pad_event_default (pad, parent, event);
      ret = (demux->src->len > 0);
      break;
    }
    case GST_EVENT_SEGMENT:{
      guint i;

      for (i = 0; i < demux->essence_tracks->len; i++) {
        GstMXFDemuxEssenceTrack *t =
            &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack,
            i);
        t->position = -1;
      }
      demux->current_partition = NULL;
      demux->seqnum = gst_event_get_seqnum (event);
      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

out:

  return ret;
}

static gboolean
gst_mxf_demux_query (GstElement * element, GstQuery * query)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (element);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (demux, "handling query %s",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 pos;

      gst_query_parse_position (query, &format, NULL);
      if (format != GST_FORMAT_TIME)
        goto error;

      pos = demux->segment.position;

      GST_DEBUG_OBJECT (demux,
          "Returning position %" G_GINT64_FORMAT " in format %s", pos,
          gst_format_get_name (format));

      gst_query_set_position (query, format, pos);
      ret = TRUE;

      break;
    }
    case GST_QUERY_DURATION:{
      gint64 duration = -1;
      GstFormat format;
      guint i;

      gst_query_parse_duration (query, &format, NULL);
      if (format != GST_FORMAT_TIME)
        goto error;

      if (demux->src->len == 0)
        goto done;

      g_rw_lock_reader_lock (&demux->metadata_lock);
      for (i = 0; i < demux->src->len; i++) {
        GstMXFDemuxPad *pad = g_ptr_array_index (demux->src, i);
        gint64 pdur = -1;

        if (!pad->material_track || !pad->material_track->parent.sequence)
          continue;

        pdur = pad->material_track->parent.sequence->duration;
        if (pad->material_track->edit_rate.n == 0 ||
            pad->material_track->edit_rate.d == 0 || pdur <= -1)
          continue;

        pdur =
            gst_util_uint64_scale (pdur,
            GST_SECOND * pad->material_track->edit_rate.d,
            pad->material_track->edit_rate.n);
        duration = MAX (duration, pdur);
      }
      g_rw_lock_reader_unlock (&demux->metadata_lock);

      if (duration == -1) {
        GST_DEBUG_OBJECT (demux, "No duration known (yet)");
        goto done;
      }

      GST_DEBUG_OBJECT (demux,
          "Returning duration %" G_GINT64_FORMAT " in format %s", duration,
          gst_format_get_name (format));

      gst_query_set_duration (query, format, duration);
      ret = TRUE;
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      ret = TRUE;
      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt != GST_FORMAT_TIME) {
        gst_query_set_seeking (query, fmt, FALSE, -1, -1);
        goto done;
      }

      if (demux->random_access) {
        gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0, -1);
      } else {
        GstQuery *peerquery = gst_query_new_seeking (GST_FORMAT_BYTES);
        gboolean seekable;

        seekable = gst_pad_peer_query (demux->sinkpad, peerquery);
        if (seekable)
          gst_query_parse_seeking (peerquery, NULL, &seekable, NULL, NULL);
        if (seekable)
          gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0, -1);
        else
          gst_query_set_seeking (query, GST_FORMAT_TIME, FALSE, -1, -1);
      }

      break;
    }
    case GST_QUERY_SEGMENT:{
      GstFormat format;
      gint64 start, stop;

      format = demux->segment.format;

      start =
          gst_segment_to_stream_time (&demux->segment, format,
          demux->segment.start);
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;
      else
        stop = gst_segment_to_stream_time (&demux->segment, format, stop);

      gst_query_set_segment (query, demux->segment.rate, format, start, stop);
      ret = TRUE;
      break;
    }
    default:
      /* else forward upstream */
      ret = gst_pad_peer_query (demux->sinkpad, query);
      break;
  }

done:
  return ret;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (demux, "query failed");
    goto done;
  }
}

static GstStateChangeReturn
gst_mxf_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      demux->seqnum = gst_util_seqnum_next ();
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mxf_demux_reset (demux);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_mxf_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (object);

  switch (prop_id) {
    case PROP_PACKAGE:
      g_free (demux->requested_package_string);
      demux->requested_package_string = g_value_dup_string (value);
      break;
    case PROP_MAX_DRIFT:
      demux->max_drift = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mxf_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (object);

  switch (prop_id) {
    case PROP_PACKAGE:
      g_value_set_string (value, demux->current_package_string);
      break;
    case PROP_MAX_DRIFT:
      g_value_set_uint64 (value, demux->max_drift);
      break;
    case PROP_STRUCTURE:{
      GstStructure *s;

      g_rw_lock_reader_lock (&demux->metadata_lock);
      if (demux->preface &&
          MXF_METADATA_BASE (demux->preface)->resolved ==
          MXF_METADATA_BASE_RESOLVE_STATE_SUCCESS)
        s = mxf_metadata_base_to_structure (MXF_METADATA_BASE (demux->preface));
      else
        s = NULL;

      gst_value_set_structure (value, s);

      if (s)
        gst_structure_free (s);

      g_rw_lock_reader_unlock (&demux->metadata_lock);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mxf_demux_finalize (GObject * object)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (object);

  gst_mxf_demux_reset (demux);

  if (demux->adapter) {
    g_object_unref (demux->adapter);
    demux->adapter = NULL;
  }

  if (demux->flowcombiner) {
    gst_flow_combiner_free (demux->flowcombiner);
    demux->flowcombiner = NULL;
  }

  if (demux->close_seg_event) {
    gst_event_unref (demux->close_seg_event);
    demux->close_seg_event = NULL;
  }

  g_free (demux->current_package_string);
  demux->current_package_string = NULL;
  g_free (demux->requested_package_string);
  demux->requested_package_string = NULL;

  g_ptr_array_free (demux->src, TRUE);
  demux->src = NULL;
  g_array_free (demux->essence_tracks, TRUE);
  demux->essence_tracks = NULL;

  g_hash_table_destroy (demux->metadata);

  g_rw_lock_clear (&demux->metadata_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mxf_demux_class_init (GstMXFDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (mxfdemux_debug, "mxfdemux", 0, "MXF demuxer");

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_mxf_demux_finalize;
  gobject_class->set_property = gst_mxf_demux_set_property;
  gobject_class->get_property = gst_mxf_demux_get_property;

  g_object_class_install_property (gobject_class, PROP_PACKAGE,
      g_param_spec_string ("package", "Package",
          "Material or Source package to use for playback", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_DRIFT,
      g_param_spec_uint64 ("max-drift", "Maximum drift",
          "Maximum number of nanoseconds by which tracks can differ",
          100 * GST_MSECOND, G_MAXUINT64, 500 * GST_MSECOND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STRUCTURE,
      g_param_spec_boxed ("structure", "Structure",
          "Structural metadata of the MXF file",
          GST_TYPE_STRUCTURE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mxf_demux_change_state);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_mxf_demux_query);

  gst_element_class_add_static_pad_template (gstelement_class,
      &mxf_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &mxf_src_template);
  gst_element_class_set_static_metadata (gstelement_class, "MXF Demuxer",
      "Codec/Demuxer", "Demux MXF files",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_mxf_demux_init (GstMXFDemux * demux)
{
  demux->sinkpad =
      gst_pad_new_from_static_template (&mxf_sink_template, "sink");

  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_sink_event));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_chain));
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_sink_activate));
  gst_pad_set_activatemode_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_sink_activate_mode));

  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->max_drift = 500 * GST_MSECOND;

  demux->adapter = gst_adapter_new ();
  demux->flowcombiner = gst_flow_combiner_new ();
  g_rw_lock_init (&demux->metadata_lock);

  demux->src = g_ptr_array_new ();
  demux->essence_tracks =
      g_array_new (FALSE, FALSE, sizeof (GstMXFDemuxEssenceTrack));

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);

  gst_mxf_demux_reset (demux);
}
