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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-mxfdemux
 *
 * mxfdemux demuxes an MXF file into the different contained streams.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=/path/to/mxf ! mxfdemux ! audioconvert ! autoaudiosink
 * ]| This pipeline demuxes an MXF file and outputs one of the contained raw audio streams.
 * </refsect2>
 */

/* TODO:
 *   - Implement support for DMS-1 and descriptive metadata tracks
 *   - Differentiate UL and UUIDs, the former can define an object system
 *     (i.e. mxf_ul_is_a() and friends could be implemented), see SMPTE S336M.
 *     The latter are just 16 byte unique identifiers
 *   - Check everything for correctness vs. SMPTE S336M, some things can probably
 *     be generalized/simplified
 *   - Seeking support: IndexTableSegments and skip-to-position seeks, needs correct
 *     timestamp calculation, etc.
 *   - Handle timecode tracks correctly (where is this documented?)
 *   - Handle Generic container system items
 *   - Force synchronization of tracks. Packets that have the timestamp are not required
 *     to be stored at the same position in the essence stream, especially if tracks
 *     with different source packages (body sid) are used.
 *   - Implement correct support for clip-wrapped essence elements.
 *   - Add a "tracks" property to select the tracks that should be used from the
 *     selected package.
 *   - Post structural metadata and descriptive metadata trees as a message on the bus
 *     and send them downstream as event.
 *   - Multichannel audio needs channel layouts, define them (SMPTE S320M?).
 *   - Correctly handle the different rectangles and aspect-ratio for video
 *   - Add support for non-standard MXF used by Avid (bug #561922).
 *   - Fix frame layout stuff, i.e. interlaced/progressive
 *
 *   - Implement SMPTE D11 essence and the digital cinema/MXF specs
 *
 *   - Implement a muxer ;-)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mxfdemux.h"
#include "mxfparse.h"
#include "mxfmetadata.h"

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

G_DEFINE_TYPE (GstMXFDemuxPad, gst_mxf_demux_pad, GST_TYPE_PAD);

static void
gst_mxf_demux_pad_finalize (GObject * object)
{
  GstMXFDemuxPad *pad = GST_MXF_DEMUX_PAD (object);

  if (pad->tags) {
    gst_tag_list_free (pad->tags);
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
  pad->last_flow = GST_FLOW_OK;
  pad->last_stop = 0;
}

enum
{
  PROP_0,
  PROP_PACKAGE
};

static gboolean gst_mxf_demux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_mxf_demux_src_event (GstPad * pad, GstEvent * event);
static const GstQueryType *gst_mxf_demux_src_query_type (GstPad * pad);
static gboolean gst_mxf_demux_src_query (GstPad * pad, GstQuery * query);

GST_BOILERPLATE (GstMXFDemux, gst_mxf_demux, GstElement, GST_TYPE_ELEMENT);

static void
gst_mxf_demux_flush (GstMXFDemux * demux, gboolean discont)
{
  GST_DEBUG_OBJECT (demux, "flushing queued data in the MXF demuxer");

  gst_adapter_clear (demux->adapter);

  demux->flushing = FALSE;

  /* Only in push mode */
  if (!demux->random_access) {
    /* We reset the offset and will get one from first push */
    demux->offset = -1;
  }
}

static void
gst_mxf_demux_remove_pad (GstMXFDemuxPad * pad, GstMXFDemux * demux)
{
  gst_element_remove_pad (GST_ELEMENT (demux), GST_PAD (pad));
}

static void
gst_mxf_demux_remove_pads (GstMXFDemux * demux)
{
  if (demux->src) {
    g_ptr_array_foreach (demux->src, (GFunc) gst_mxf_demux_remove_pad, demux);
    g_ptr_array_foreach (demux->src, (GFunc) gst_object_unref, NULL);
    g_ptr_array_free (demux->src, TRUE);
    demux->src = NULL;
  }
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
  GST_DEBUG_OBJECT (demux, "Resetting MXF state");

  g_list_foreach (demux->partitions, (GFunc) gst_mxf_demux_partition_free,
      NULL);
  g_list_free (demux->partitions);
  demux->partitions = NULL;

  demux->current_partition = NULL;

  if (demux->essence_tracks) {
    guint i;

    for (i = 0; i < demux->essence_tracks->len; i++) {
      GstMXFDemuxEssenceTrack *t =
          &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

      g_free (t->offsets);
      g_free (t->mapping_data);

      if (t->tags)
        gst_tag_list_free (t->tags);

      if (t->caps)
        gst_caps_unref (t->caps);
    }
    g_array_free (demux->essence_tracks, TRUE);
    demux->essence_tracks = NULL;
  }
}

static void
gst_mxf_demux_reset_linked_metadata (GstMXFDemux * demux)
{
  guint i;

  if (demux->src) {
    for (i = 0; i < demux->src->len; i++) {
      GstMXFDemuxPad *pad = g_ptr_array_index (demux->src, i);

      pad->material_track = NULL;
      pad->material_package = NULL;
      pad->current_component = NULL;
    }
  }

  if (demux->essence_tracks) {
    for (i = 0; i < demux->essence_tracks->len; i++) {
      GstMXFDemuxEssenceTrack *track =
          &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

      track->source_package = NULL;
      track->source_track = NULL;
    }
  }

  demux->current_package = NULL;
}

static void
gst_mxf_demux_reset_metadata (GstMXFDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "Resetting metadata");

  demux->update_metadata = TRUE;
  demux->metadata_resolved = FALSE;

  gst_mxf_demux_reset_linked_metadata (demux);

  demux->preface = NULL;

  if (demux->metadata) {
    g_hash_table_destroy (demux->metadata);
    demux->metadata = NULL;
  }
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

  if (demux->new_seg_event) {
    gst_event_unref (demux->new_seg_event);
    demux->new_seg_event = NULL;
  }

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

  gst_mxf_demux_reset_mxf_state (demux);
  gst_mxf_demux_reset_metadata (demux);
}

static GstFlowReturn
gst_mxf_demux_combine_flows (GstMXFDemux * demux,
    GstMXFDemuxPad * pad, GstFlowReturn ret)
{
  guint i;

  /* store the value */
  pad->last_flow = ret;

  /* any other error that is not-linked can be returned right away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  g_assert (demux->src->len != 0);
  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *opad = g_ptr_array_index (demux->src, i);

    if (opad == NULL)
      continue;

    ret = opad->last_flow;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  GST_LOG_OBJECT (demux, "combined return %s", gst_flow_get_name (ret));
  return ret;
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

  if (G_UNLIKELY (*buffer && GST_BUFFER_SIZE (*buffer) != size)) {
    GST_WARNING_OBJECT (demux,
        "partial pull got %u when expecting %u from offset %" G_GUINT64_FORMAT,
        GST_BUFFER_SIZE (*buffer), size, offset);
    gst_buffer_unref (*buffer);
    ret = GST_FLOW_UNEXPECTED;
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

  if (!demux->src)
    return ret;

  for (i = 0; i < demux->src->len; i++) {
    GstPad *pad = GST_PAD (g_ptr_array_index (demux->src, i));

    ret |= gst_pad_push_event (pad, gst_event_ref (event));
  }

  gst_event_unref (event);

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_partition_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  MXFPartitionPack partition;
  GList *l;
  GstMXFDemuxPartition *p = NULL;

  GST_DEBUG_OBJECT (demux,
      "Handling partition pack of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  for (l = demux->partitions; l; l = l->next) {
    GstMXFDemuxPartition *tmp = l->data;

    if (tmp->partition.this_partition + demux->run_in == demux->offset &&
        tmp->partition.major_version == 0x0001) {
      GST_DEBUG_OBJECT (demux, "Partition already parsed");
      goto out;
    }
  }


  if (!mxf_partition_pack_parse (key, &partition,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
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
    demux->partitions = g_list_prepend (demux->partitions, p);
  }

out:
  demux->current_partition = p;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_primer_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (demux,
      "Handling primer pack of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (G_UNLIKELY (!demux->current_partition)) {
    GST_ERROR_OBJECT (demux, "Primer pack before partition pack");
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (demux->current_partition->primer.mappings)) {
    GST_DEBUG_OBJECT (demux, "Primer pack already exists");
    return GST_FLOW_OK;
  }

  if (!mxf_primer_pack_parse (key, &demux->current_partition->primer,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
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

  GST_DEBUG_OBJECT (demux, "Resolve metadata references");
  demux->update_metadata = FALSE;

  if (!demux->metadata) {
    GST_ERROR_OBJECT (demux, "No metadata yet");
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

  return ret;

error:
  demux->metadata_resolved = FALSE;

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
    MXFUMID umid;

    if (!mxf_umid_from_string (demux->requested_package_string, &umid)) {
      GST_ERROR_OBJECT (demux, "Invalid requested package");
    } else {
      if (memcmp (&umid, &demux->current_package_uid, 32) != 0) {
        gst_mxf_demux_remove_pads (demux);
        memcpy (&demux->current_package_uid, &umid, 32);
      }
    }
    g_free (demux->requested_package_string);
    demux->requested_package_string = NULL;
  }

  if (!mxf_umid_is_zero (&demux->current_package_uid))
    ret = gst_mxf_demux_find_package (demux, &demux->current_package_uid);
  if (ret && (MXF_IS_METADATA_MATERIAL_PACKAGE (ret)
          || (MXF_IS_METADATA_SOURCE_PACKAGE (ret)
              && MXF_METADATA_SOURCE_PACKAGE (ret)->top_level)))
    return ret;
  else if (ret)
    GST_WARNING_OBJECT (demux,
        "Current package is not a material package or top-level source package, choosing the first best");
  else if (!mxf_umid_is_zero (&demux->current_package_uid))
    GST_WARNING_OBJECT (demux,
        "Current package not found, choosing the first best");

  if (demux->preface->primary_package)
    ret = demux->preface->primary_package;
  if (ret && (MXF_IS_METADATA_MATERIAL_PACKAGE (ret)
          || (MXF_IS_METADATA_SOURCE_PACKAGE (ret)
              && MXF_METADATA_SOURCE_PACKAGE (ret)->top_level)))
    return ret;

  for (i = 0; i < demux->preface->content_storage->n_packages; i++) {
    if (demux->preface->content_storage->packages[i] &&
        MXF_IS_METADATA_MATERIAL_PACKAGE (demux->preface->
            content_storage->packages[i])) {
      ret =
          MXF_METADATA_GENERIC_PACKAGE (demux->preface->
          content_storage->packages[i]);
      break;
    }
  }

  if (!ret) {
    GST_ERROR_OBJECT (demux, "No material package");
    return NULL;
  }

  memcpy (&demux->current_package_uid, &ret->package_uid, 32);

  if (!ret)
    GST_ERROR_OBJECT (demux, "No suitable package found");

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

      if (demux->essence_tracks) {
        for (k = 0; k < demux->essence_tracks->len; k++) {
          GstMXFDemuxEssenceTrack *tmp =
              &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack,
              k);

          if (tmp->track_number == track->parent.track_number &&
              tmp->body_sid == edata->body_sid) {
            etrack = tmp;
            break;
          }
        }
      }

      if (!etrack) {
        GstMXFDemuxEssenceTrack tmp;

        memset (&tmp, 0, sizeof (tmp));
        tmp.body_sid = edata->body_sid;
        tmp.track_number = track->parent.track_number;

        if (!demux->essence_tracks)
          demux->essence_tracks =
              g_array_new (FALSE, FALSE, sizeof (GstMXFDemuxEssenceTrack));
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

      if (track->parent.sequence->duration > etrack->duration) {
        guint64 old_duration = etrack->duration;

        etrack->duration = track->parent.sequence->duration;

        if (etrack->offsets) {
          etrack->offsets =
              g_realloc (etrack->offsets,
              etrack->duration * sizeof (GstMXFDemuxIndex));
          memset (&etrack->offsets[old_duration - 1], 0,
              etrack->duration - old_duration);
        } else {
          etrack->offsets = g_new0 (GstMXFDemuxIndex, etrack->duration);
        }
      }

      g_free (etrack->mapping_data);
      etrack->mapping_data = NULL;
      etrack->handler = NULL;
      etrack->handle_func = NULL;
      if (etrack->tags)
        gst_tag_list_free (etrack->tags);
      etrack->tags = NULL;

      etrack->handler = mxf_essence_element_handler_find (track);
      if (!etrack->handler) {
        GST_WARNING_OBJECT (demux,
            "No essence element handler for track found");
        goto next;
      }

      caps =
          etrack->handler->create_caps (track, &etrack->tags,
          &etrack->handle_func, &etrack->mapping_data);

      GST_DEBUG_OBJECT (demux, "Created caps %" GST_PTR_FORMAT, caps);

      if (!caps && new) {
        GST_WARNING_OBJECT (demux, "No caps created, ignoring stream");
        g_free (etrack->mapping_data);
        if (etrack->tags)
          gst_tag_list_free (etrack->tags);
        g_array_remove_index (demux->essence_tracks,
            demux->essence_tracks->len - 1);
        goto next;
      } else if (!caps) {
        GST_WARNING_OBJECT (demux, "Couldn't create updated caps for stream");
      } else if (!etrack->caps || !gst_caps_is_equal (etrack->caps, caps)) {
        gst_caps_replace (&etrack->caps, caps);
      }

      etrack->source_package = package;
      etrack->source_track = track;
      continue;

    next:
      if (new) {
        g_free (etrack->mapping_data);
        if (etrack->tags)
          gst_tag_list_free (etrack->tags);
        if (etrack->caps)
          gst_caps_unref (etrack->caps);

        g_array_remove_index (demux->essence_tracks,
            demux->essence_tracks->len - 1);
      }
    }
  }

  if (!demux->essence_tracks || demux->essence_tracks->len == 0) {
    GST_ERROR_OBJECT (demux, "No valid essence tracks in this file");
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *etrack =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

    if (!etrack->source_package || !etrack->source_track || !etrack->caps) {
      GST_ERROR_OBJECT (demux, "Failed to update essence track");
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

  GST_DEBUG_OBJECT (demux, "Updating tracks");

  if ((ret = gst_mxf_demux_update_essence_tracks (demux))) {
    return ret;
  }

  current_package = gst_mxf_demux_choose_package (demux);

  if (!current_package) {
    GST_ERROR_OBJECT (demux, "Unable to find current package");
    return GST_FLOW_ERROR;
  } else if (!current_package->tracks) {
    GST_ERROR_OBJECT (demux, "Current package has no (resolved) tracks");
    return GST_FLOW_ERROR;
  } else if (!current_package->n_essence_tracks) {
    GST_ERROR_OBJECT (demux, "Current package has no essence tracks");
    return GST_FLOW_ERROR;
  }

  first_run = (demux->src == NULL);
  demux->current_package = current_package;

  for (i = 0; i < current_package->n_tracks; i++) {
    MXFMetadataTimelineTrack *track = NULL;
    MXFMetadataSequence *sequence;
    MXFMetadataSourceClip *component = NULL;
    MXFMetadataSourcePackage *source_package = NULL;
    MXFMetadataTimelineTrack *source_track = NULL;
    GstMXFDemuxEssenceTrack *etrack = NULL;
    GstMXFDemuxPad *pad = NULL;

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

    if (demux->src && demux->src->len > 0) {
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
      continue;
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
      continue;
    }

    if (!source_package || track->parent.type == MXF_METADATA_TRACK_UNKNOWN
        || !source_track) {
      GST_WARNING_OBJECT (demux,
          "No source package or track type for track found");
      continue;
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
      continue;
    }

    if (track->edit_rate.n <= 0 || track->edit_rate.d <= 0 ||
        source_track->edit_rate.n <= 0 || source_track->edit_rate.d <= 0) {
      GST_WARNING_OBJECT (demux, "Track has an invalid edit rate");
      continue;
    }

    if (MXF_IS_METADATA_MATERIAL_PACKAGE (current_package) && !component) {
      GST_WARNING_OBJECT (demux,
          "Playing material package but found no component for track");
      continue;
    }

    if (!source_package->descriptors) {
      GST_WARNING_OBJECT (demux, "Source package has no descriptors");
      continue;
    }

    if (!source_track->parent.descriptor) {
      GST_WARNING_OBJECT (demux, "No descriptor found for track");
      continue;
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

    /* If we just added the pad initialize for the current component */
    if (first_run && MXF_IS_METADATA_MATERIAL_PACKAGE (current_package)) {
      pad->current_component_index = 0;
      pad->current_component_position = 0;
      pad->current_component_start = source_track->origin;
      if (track->edit_rate.n != source_track->edit_rate.n ||
          track->edit_rate.n != source_track->edit_rate.n) {

        pad->current_component_start +=
            gst_util_uint64_scale (component->start_position,
            source_track->edit_rate.n * track->edit_rate.d,
            source_track->edit_rate.d * track->edit_rate.n);
      } else {
        pad->current_component_start += component->start_position;
      }
    }

    /* NULL iff playing a source package */
    pad->current_component = component;

    pad->current_essence_track = etrack;

    if (pad->tags)
      gst_tag_list_free (pad->tags);
    pad->tags = NULL;
    if (etrack->tags)
      pad->tags = gst_tag_list_copy (etrack->tags);

    if (GST_PAD_CAPS (pad)
        && !gst_caps_is_equal (GST_PAD_CAPS (pad), etrack->caps)) {
      gst_pad_set_caps (GST_PAD_CAST (pad), etrack->caps);
    } else if (!GST_PAD_CAPS (pad)) {
      gst_pad_set_caps (GST_PAD_CAST (pad), etrack->caps);

      gst_pad_set_event_function (GST_PAD_CAST (pad),
          GST_DEBUG_FUNCPTR (gst_mxf_demux_src_event));

      gst_pad_set_query_type_function (GST_PAD_CAST (pad),
          GST_DEBUG_FUNCPTR (gst_mxf_demux_src_query_type));
      gst_pad_set_query_function (GST_PAD_CAST (pad),
          GST_DEBUG_FUNCPTR (gst_mxf_demux_src_query));

      gst_pad_use_fixed_caps (GST_PAD_CAST (pad));
      gst_pad_set_active (GST_PAD_CAST (pad), TRUE);

      gst_element_add_pad (GST_ELEMENT_CAST (demux), gst_object_ref (pad));

      if (!demux->src)
        demux->src = g_ptr_array_new ();
      g_ptr_array_add (demux->src, pad);
      pad->discont = TRUE;
    }
  }

  gst_element_no_more_pads (GST_ELEMENT_CAST (demux));

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  guint16 type;
  MXFMetadata *metadata = NULL, *old = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  type = GST_READ_UINT16_BE (key->u + 13);

  GST_DEBUG_OBJECT (demux,
      "Handling metadata of size %u at offset %"
      G_GUINT64_FORMAT " of type 0x%04x", GST_BUFFER_SIZE (buffer),
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

  metadata =
      mxf_metadata_new (type, &demux->current_partition->primer, demux->offset,
      GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));

  if (!metadata) {
    GST_ERROR_OBJECT (demux, "Parsing metadata failed");
    return GST_FLOW_ERROR;
  }

  demux->update_metadata = TRUE;

  if (!demux->metadata)
    demux->metadata = mxf_metadata_hash_table_new ();

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
        mxf_ul_to_string (&MXF_METADATA_BASE (metadata)->instance_uid, str),
        g_type_name (G_TYPE_FROM_INSTANCE (old)),
        g_type_name (G_TYPE_FROM_INSTANCE (metadata)));
    gst_mini_object_unref (GST_MINI_OBJECT (metadata));
    return GST_FLOW_ERROR;
  } else if (old
      && MXF_METADATA_BASE (old)->offset >=
      MXF_METADATA_BASE (metadata)->offset) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif

    GST_DEBUG_OBJECT (demux,
        "Metadata with instance uid %s already exists and is newer",
        mxf_ul_to_string (&MXF_METADATA_BASE (metadata)->instance_uid, str));
    gst_mini_object_unref (GST_MINI_OBJECT (metadata));
    return GST_FLOW_OK;
  }

  if (MXF_IS_METADATA_PREFACE (metadata)) {
    demux->preface = MXF_METADATA_PREFACE (metadata);
  }

  gst_mxf_demux_reset_linked_metadata (demux);

  g_hash_table_replace (demux->metadata,
      &MXF_METADATA_BASE (metadata)->instance_uid, metadata);

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_descriptive_metadata (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  guint32 type;
  guint8 scheme;
  GstFlowReturn ret = GST_FLOW_OK;
  MXFDescriptiveMetadata *m = NULL, *old = NULL;

  scheme = GST_READ_UINT8 (key->u + 12);
  type = GST_READ_UINT24_BE (key->u + 13);

  GST_DEBUG_OBJECT (demux,
      "Handling descriptive metadata of size %u at offset %"
      G_GUINT64_FORMAT " with scheme 0x%02x and type 0x%06x",
      GST_BUFFER_SIZE (buffer), demux->offset, scheme, type);

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

  m = mxf_descriptive_metadata_new (scheme, type,
      &demux->current_partition->primer, demux->offset,
      GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));

  if (!m) {
    GST_WARNING_OBJECT (demux,
        "Unknown or unhandled descriptive metadata of scheme 0x%02x and type 0x%06x",
        scheme, type);
    return GST_FLOW_OK;
  }

  demux->update_metadata = TRUE;

  if (!demux->metadata)
    demux->metadata = mxf_metadata_hash_table_new ();

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
        mxf_ul_to_string (&MXF_METADATA_BASE (m)->instance_uid, str),
        g_type_name (G_TYPE_FROM_INSTANCE (old)),
        g_type_name (G_TYPE_FROM_INSTANCE (m)));
    gst_mini_object_unref (GST_MINI_OBJECT (m));
    return GST_FLOW_ERROR;
  } else if (old
      && MXF_METADATA_BASE (old)->offset >= MXF_METADATA_BASE (m)->offset) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar str[48];
#endif

    GST_DEBUG_OBJECT (demux,
        "Metadata with instance uid %s already exists and is newer",
        mxf_ul_to_string (&MXF_METADATA_BASE (m)->instance_uid, str));
    gst_mini_object_unref (GST_MINI_OBJECT (m));
    return GST_FLOW_OK;
  }

  gst_mxf_demux_reset_linked_metadata (demux);

  g_hash_table_replace (demux->metadata, &MXF_METADATA_BASE (m)->instance_uid,
      m);

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_generic_container_system_item (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (demux,
      "Handling generic container system item of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  /* TODO: parse this */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_pad_next_component (GstMXFDemux * demux, GstMXFDemuxPad * pad)
{
  MXFMetadataSequence *sequence;
  guint k;
  MXFMetadataSourcePackage *source_package = NULL;
  MXFMetadataTimelineTrack *source_track = NULL;

  pad->current_component_index++;

  sequence = pad->material_track->parent.sequence;

  if (pad->current_component_index >= sequence->n_structural_components) {
    GST_DEBUG_OBJECT (demux, "After last structural component");
    return GST_FLOW_UNEXPECTED;
  }

  GST_DEBUG_OBJECT (demux, "Switching to component %u",
      pad->current_component_index);

  pad->current_component =
      MXF_METADATA_SOURCE_CLIP (sequence->
      structural_components[pad->current_component_index]);
  if (pad->current_component == NULL) {
    GST_ERROR_OBJECT (demux, "No such structural component");
    return GST_FLOW_ERROR;
  }

  if (!pad->current_component->source_package
      || !pad->current_component->source_package->top_level
      || !MXF_METADATA_GENERIC_PACKAGE (pad->
          current_component->source_package)->tracks) {
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

  if (!source_package->descriptors) {
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

  pad->current_component_position = 0;
  pad->current_component_start = source_track->origin;
  if (pad->material_track->edit_rate.n != source_track->edit_rate.n ||
      pad->material_track->edit_rate.n != source_track->edit_rate.n) {

    pad->current_component_start +=
        gst_util_uint64_scale (pad->current_component->start_position,
        source_track->edit_rate.n * pad->material_track->edit_rate.d,
        source_track->edit_rate.d * pad->material_track->edit_rate.n);
  } else {
    pad->current_component_start += pad->current_component->start_position;
  }


  if (!gst_caps_is_equal (GST_PAD_CAPS (pad), pad->current_essence_track->caps)) {
    gst_pad_set_caps (GST_PAD_CAST (pad), pad->current_essence_track->caps);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_generic_container_essence_element (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 track_number;
  guint i;
  GstBuffer *inbuf;
  GstBuffer *outbuf = NULL;
  GstMXFDemuxPad *pad = NULL;
  GstMXFDemuxEssenceTrack *etrack = NULL;

  GST_DEBUG_OBJECT (demux,
      "Handling generic container essence element of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  GST_DEBUG_OBJECT (demux, "  type = 0x%02x", key->u[12]);
  GST_DEBUG_OBJECT (demux, "  essence element count = 0x%02x", key->u[13]);
  GST_DEBUG_OBJECT (demux, "  essence element type = 0x%02x", key->u[14]);
  GST_DEBUG_OBJECT (demux, "  essence element number = 0x%02x", key->u[15]);

  if (!demux->current_package) {
    GST_ERROR_OBJECT (demux, "No package selected yet");
    return GST_FLOW_ERROR;
  }

  if (!demux->src || demux->src->len == 0) {
    GST_ERROR_OBJECT (demux, "No streams created yet");
    return GST_FLOW_ERROR;
  }

  if (!demux->essence_tracks || demux->essence_tracks->len == 0) {
    GST_ERROR_OBJECT (demux, "No essence streams found in the metadata");
    return GST_FLOW_ERROR;
  }

  if (GST_BUFFER_SIZE (buffer) == 0) {
    GST_DEBUG_OBJECT (demux, "Zero sized essence element, ignoring");
    return GST_FLOW_OK;
  }

  track_number = GST_READ_UINT32_BE (&key->u[12]);

  for (i = 0; i < demux->essence_tracks->len; i++) {
    GstMXFDemuxEssenceTrack *tmp =
        &g_array_index (demux->essence_tracks, GstMXFDemuxEssenceTrack, i);

    if (tmp->body_sid == demux->current_partition->partition.body_sid &&
        tmp->track_number == track_number) {
      etrack = tmp;
      break;
    }
  }

  if (!etrack) {
    GST_WARNING_OBJECT (demux,
        "No essence track for this essence element found");
    return GST_FLOW_OK;
  }

  /* TODO update essence tracks offsets, position, etc... */

  for (i = 0; i < demux->src->len; i++) {
    GstMXFDemuxPad *tmp = g_ptr_array_index (demux->src, i);

    if (tmp->current_essence_track == etrack) {
      pad = tmp;
      break;
    }
  }

  if (!pad) {
    GST_DEBUG_OBJECT (demux, "No pad for essence track found");
    return GST_FLOW_OK;
  }

  if (pad->eos) {
    GST_DEBUG_OBJECT (demux, "Pad is already EOS");
    return GST_FLOW_OK;
  }

  if (pad->current_component &&
      pad->current_component_position < pad->current_component_start) {
    GST_DEBUG_OBJECT (demux, "Before current component's start position");
    pad->current_component_position++;
    return GST_FLOW_OK;
  }

  /* Create subbuffer to be able to change metadata */
  inbuf = gst_buffer_create_sub (buffer, 0, GST_BUFFER_SIZE (buffer));

  GST_BUFFER_TIMESTAMP (inbuf) = pad->last_stop;
  GST_BUFFER_DURATION (inbuf) =
      gst_util_uint64_scale (GST_SECOND, pad->material_track->edit_rate.d,
      pad->material_track->edit_rate.n);
  GST_BUFFER_OFFSET (inbuf) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (inbuf) = GST_BUFFER_OFFSET_NONE;
  gst_buffer_set_caps (inbuf, etrack->caps);

  if (etrack->handle_func) {
    /* Takes ownership of inbuf */
    ret =
        etrack->handle_func (key, inbuf, etrack->caps,
        etrack->source_track, pad->current_component, etrack->mapping_data,
        &outbuf);
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
  }

  if (pad->need_segment) {
    gst_pad_push_event (GST_PAD_CAST (pad),
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
    pad->need_segment = FALSE;
  }

  if (pad->tags) {
    gst_element_found_tags_for_pad (GST_ELEMENT_CAST (demux),
        GST_PAD_CAST (pad), pad->tags);
    pad->tags = NULL;
  }


  if (outbuf)
    pad->last_stop += GST_BUFFER_DURATION (outbuf);

  if (outbuf) {
    GST_DEBUG_OBJECT (demux,
        "Pushing buffer of size %u for track %u: timestamp %" GST_TIME_FORMAT
        " duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (outbuf),
        pad->material_track->parent.track_id,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    if (pad->discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      pad->discont = FALSE;
    }

    ret = gst_pad_push (GST_PAD_CAST (pad), outbuf);
    ret = gst_mxf_demux_combine_flows (demux, pad, ret);
  } else {
    GST_DEBUG_OBJECT (demux, "Dropping buffer for track %u",
        pad->material_track->parent.track_id);
  }

  if (ret != GST_FLOW_OK)
    return ret;

  if (pad->current_component) {
    pad->current_component_position++;
    if (pad->current_component->parent.duration != -1 &&
        pad->current_component_position - pad->current_component_start
        >= pad->current_component->parent.duration) {
      GST_DEBUG_OBJECT (demux, "Switching to next component");

      if ((ret = gst_mxf_demux_pad_next_component (demux, pad)) != GST_FLOW_OK) {
        if (ret == GST_FLOW_UNEXPECTED) {
          gboolean eos = TRUE;

          GST_DEBUG_OBJECT (demux, "EOS for track");
          pad->eos = TRUE;

          for (i = 0; i < demux->src->len; i++) {
            GstMXFDemuxPad *opad = g_ptr_array_index (demux->src, i);

            eos &= opad->eos;
          }

          if (eos) {
            GST_DEBUG_OBJECT (demux, "All tracks are EOS");
            return GST_FLOW_UNEXPECTED;
          } else {
            return GST_FLOW_OK;
          }
        } else {
          GST_ERROR_OBJECT (demux, "Switching component failed");
          return ret;
        }
      }
    }
  }

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_random_index_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  guint i;

  GST_DEBUG_OBJECT (demux,
      "Handling random index pack of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (demux->random_index_pack) {
    GST_DEBUG_OBJECT (demux, "Already parsed random index pack");
    return GST_FLOW_OK;
  }

  if (!mxf_random_index_pack_parse (key, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer), &demux->random_index_pack)) {
    GST_ERROR_OBJECT (demux, "Parsing random index pack failed");
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < demux->random_index_pack->len; i++) {
    GList *l;
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
      demux->partitions = g_list_prepend (demux->partitions, p);
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_index_table_segment (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  MXFIndexTableSegment *segment;

  GST_DEBUG_OBJECT (demux,
      "Handling index table segment of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!demux->current_partition->primer.mappings) {
    GST_WARNING_OBJECT (demux, "Invalid primer pack");
    return GST_FLOW_OK;
  }

  segment = g_new0 (MXFIndexTableSegment, 1);

  if (!mxf_index_table_segment_parse (key, segment,
          &demux->current_partition->primer, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {

    GST_ERROR_OBJECT (demux, "Parsing index table segment failed");
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

  memset (key, 0, sizeof (MXFUL));

  /* Pull 16 byte key and first byte of BER encoded length */
  if ((ret =
          gst_mxf_demux_pull_range (demux, offset, 17, &buffer)) != GST_FLOW_OK)
    goto beach;

  data = GST_BUFFER_DATA (buffer);

  memcpy (key, GST_BUFFER_DATA (buffer), 16);

  /* Decode BER encoded packet length */
  if ((data[16] & 0x80) == 0) {
    length = data[16];
    data_offset = 17;
  } else {
    guint slen = data[16] & 0x7f;

    data_offset = 16 + 1 + slen;

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
    data = GST_BUFFER_DATA (buffer);

    length = 0;
    while (slen) {
      length = (length << 8) | *data;
      data++;
      slen--;
    }
  }

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

void
gst_mxf_demux_pull_random_index_pack (GstMXFDemux * demux)
{
  GstBuffer *buffer;
  GstFlowReturn ret;
  gint64 filesize = -1;
  GstFormat fmt = GST_FORMAT_BYTES;
  guint32 pack_size;
  guint64 old_offset = demux->offset;
  MXFUL key;

  if (!gst_pad_query_peer_duration (demux->sinkpad, &fmt, &filesize) ||
      fmt != GST_FORMAT_BYTES || filesize == -1) {
    GST_DEBUG_OBJECT (demux, "Can't query upstream size");
    return;
  }

  g_assert (filesize > 4);

  if ((ret =
          gst_mxf_demux_pull_range (demux, filesize - 4, 4,
              &buffer)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Failed pulling last 4 bytes");
    return;
  }

  pack_size = GST_READ_UINT32_BE (GST_BUFFER_DATA (buffer));

  gst_buffer_unref (buffer);

  if (pack_size < 20) {
    GST_DEBUG_OBJECT (demux, "Too small pack size (%u bytes)", pack_size);
    return;
  } else if (pack_size > filesize - 20) {
    GST_DEBUG_OBJECT (demux, "Too large pack size (%u bytes)", pack_size);
    return;
  }

  if ((ret =
          gst_mxf_demux_pull_range (demux, filesize - pack_size, 16,
              &buffer)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Failed pulling random index pack key");
    return;
  }

  memcpy (&key, GST_BUFFER_DATA (buffer), 16);
  gst_buffer_unref (buffer);

  if (!mxf_is_random_index_pack (&key)) {
    GST_DEBUG_OBJECT (demux, "No random index pack");
    return;
  }

  demux->offset = filesize - pack_size;
  if ((ret =
          gst_mxf_demux_pull_klv_packet (demux, filesize - pack_size, &key,
              &buffer, NULL)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Failed pulling random index pack");
    return;
  }

  gst_mxf_demux_handle_random_index_pack (demux, &key, buffer);
  gst_buffer_unref (buffer);
  demux->offset = old_offset;
}

static void
gst_mxf_demux_parse_footer_metadata (GstMXFDemux * demux)
{
  guint64 old_offset = demux->offset;
  MXFUL key;
  GstBuffer *buffer = NULL;
  guint read = 0;
  GstFlowReturn ret = GST_FLOW_OK;
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
  ret =
      gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
      &read);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto out;

  if (!mxf_is_partition_pack (&key))
    goto out;

  if (gst_mxf_demux_handle_partition_pack (demux, &key, buffer) != GST_FLOW_OK)
    goto out;

  demux->offset += read;
  gst_buffer_unref (buffer);
  buffer = NULL;

  if (demux->current_partition->partition.header_byte_count == 0) {
    if (demux->current_partition->partition.prev_partition == 0
        || demux->current_partition->partition.this_partition == 0)
      goto out;

    demux->offset =
        demux->run_in + demux->current_partition->partition.this_partition -
        demux->current_partition->partition.prev_partition;
    goto next_try;
  }

  while (TRUE) {
    ret =
        gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
        &read);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      demux->offset =
          demux->run_in +
          demux->current_partition->partition.this_partition -
          demux->current_partition->partition.prev_partition;
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
          demux->offset =
              demux->run_in +
              demux->current_partition->partition.this_partition -
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
      demux->offset =
          demux->run_in +
          demux->current_partition->partition.this_partition -
          demux->current_partition->partition.prev_partition;
      goto next_try;
    }
  }

  /* parse metadata */
  while (demux->offset <
      demux->run_in + demux->current_partition->primer.offset +
      demux->current_partition->partition.header_byte_count) {
    ret =
        gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
        &read);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      demux->offset =
          demux->run_in +
          demux->current_partition->partition.this_partition -
          demux->current_partition->partition.prev_partition;
      goto next_try;
    }

    if (mxf_is_metadata (&key)) {
      ret = gst_mxf_demux_handle_metadata (demux, &key, buffer);
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;

      if (G_UNLIKELY (ret != GST_FLOW_OK)) {
        gst_mxf_demux_reset_metadata (demux);
        demux->offset =
            demux->run_in +
            demux->current_partition->partition.this_partition -
            demux->current_partition->partition.prev_partition;
        goto next_try;
      }
    } else if (mxf_is_descriptive_metadata (&key)) {
      ret = gst_mxf_demux_handle_descriptive_metadata (demux, &key, buffer);
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    } else if (mxf_is_fill (&key)) {
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    } else {
      demux->offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    }
  }

  /* resolve references etc */

  if (gst_mxf_demux_resolve_references (demux) !=
      GST_FLOW_OK || gst_mxf_demux_update_tracks (demux) != GST_FLOW_OK) {
    demux->current_partition->parsed_metadata = TRUE;
    demux->offset =
        demux->run_in + demux->current_partition->partition.this_partition -
        demux->current_partition->partition.prev_partition;
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
    GstBuffer * buffer)
{
#ifndef GST_DISABLE_GST_DEBUG
  gchar key_str[48];
#endif
  GstFlowReturn ret = GST_FLOW_OK;

  if (demux->update_metadata
      && demux->preface
      && demux->offset >=
      demux->run_in + demux->current_partition->primer.offset +
      demux->current_partition->partition.header_byte_count) {
    demux->current_partition->parsed_metadata = TRUE;
    if ((ret = gst_mxf_demux_resolve_references (demux)) != GST_FLOW_OK)
      goto beach;
    if ((ret = gst_mxf_demux_update_tracks (demux)) != GST_FLOW_OK)
      goto beach;
  } else if (demux->metadata_resolved && demux->requested_package_string) {
    if ((ret = gst_mxf_demux_update_tracks (demux)) != GST_FLOW_OK)
      goto beach;
  }

  if (!mxf_is_mxf_packet (key)) {
    GST_WARNING_OBJECT (demux,
        "Skipping non-MXF packet of size %u at offset %"
        G_GUINT64_FORMAT ", key: %s", GST_BUFFER_SIZE (buffer), demux->offset,
        mxf_ul_to_string (key, key_str));
  } else if (mxf_is_partition_pack (key)) {
    ret = gst_mxf_demux_handle_partition_pack (demux, key, buffer);
  } else if (mxf_is_primer_pack (key)) {
    ret = gst_mxf_demux_handle_primer_pack (demux, key, buffer);
  } else if (mxf_is_metadata (key)) {
    ret = gst_mxf_demux_handle_metadata (demux, key, buffer);
  } else if (mxf_is_descriptive_metadata (key)) {
    ret = gst_mxf_demux_handle_descriptive_metadata (demux, key, buffer);
  } else if (mxf_is_generic_container_system_item (key)) {
    ret =
        gst_mxf_demux_handle_generic_container_system_item (demux, key, buffer);
  } else if (mxf_is_generic_container_essence_element (key)) {
    ret =
        gst_mxf_demux_handle_generic_container_essence_element (demux, key,
        buffer);
  } else if (mxf_is_random_index_pack (key)) {
    ret = gst_mxf_demux_handle_random_index_pack (demux, key, buffer);
  } else if (mxf_is_index_table_segment (key)) {
    ret = gst_mxf_demux_handle_index_table_segment (demux, key, buffer);
  } else if (mxf_is_fill (key)) {
    GST_DEBUG_OBJECT (demux,
        "Skipping filler packet of size %u at offset %"
        G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);
  } else {
    GST_DEBUG_OBJECT (demux,
        "Skipping unknown packet of size %u at offset %"
        G_GUINT64_FORMAT ", key: %s", GST_BUFFER_SIZE (buffer), demux->offset,
        mxf_ul_to_string (key, key_str));
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
  }

beach:
  return ret;
}

static GstFlowReturn
gst_mxf_demux_pull_and_handle_klv_packet (GstMXFDemux * demux)
{
  GstBuffer *buffer = NULL;
  MXFUL key;
  GstFlowReturn ret = GST_FLOW_OK;
  guint read = 0;

  ret =
      gst_mxf_demux_pull_klv_packet (demux, demux->offset, &key, &buffer,
      &read);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto beach;

  ret = gst_mxf_demux_handle_klv_packet (demux, &key, buffer);

  demux->offset += read;

beach:
  if (buffer)
    gst_buffer_unref (buffer);

  return ret;
}

static void
gst_mxf_demux_loop (GstPad * pad)
{
  GstMXFDemux *demux = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  demux = GST_MXF_DEMUX (gst_pad_get_parent (pad));

  if (demux->run_in == -1) {
    /* Skip run-in, which is at most 64K and is finished
     * by a header partition pack */
    while (demux->offset < 64 * 1024) {
      GstBuffer *buffer;

      if ((ret =
              gst_mxf_demux_pull_range (demux, demux->offset, 16,
                  &buffer)) != GST_FLOW_OK)
        break;

      if (mxf_is_header_partition_pack ((const MXFUL *)
              GST_BUFFER_DATA (buffer))) {
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

    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto pause;

    if (G_UNLIKELY (demux->run_in == -1)) {
      GST_ERROR_OBJECT (demux, "No valid header partition pack found");
      ret = GST_FLOW_ERROR;
      goto pause;
    }

    /* First of all pull&parse the random index pack at EOF */
    gst_mxf_demux_pull_random_index_pack (demux);
  }

  /* Now actually do something */
  ret = gst_mxf_demux_pull_and_handle_klv_packet (demux);

  /* pause if something went wrong */
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pause;

  /* check EOS condition */
  if ((demux->segment.flags & GST_SEEK_FLAG_SEGMENT) &&
      (demux->segment.stop != -1) &&
      (demux->segment.last_stop >= demux->segment.stop)) {
    ret = GST_FLOW_UNEXPECTED;
    goto pause;
  }

  gst_object_unref (demux);

  return;

pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_LOG_OBJECT (demux, "pausing task, reason %s", reason);
    gst_pad_pause_task (pad);

    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */
        if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gint64 stop;

          /* for segment playback we need to post when (in stream time)
           * we stopped, this is either stop (when set) or the duration. */
          if ((stop = demux->segment.stop) == -1)
            stop = demux->segment.duration;

          GST_LOG_OBJECT (demux, "Sending segment done, at end of segment");
          gst_element_post_message (GST_ELEMENT_CAST (demux),
              gst_message_new_segment_done (GST_OBJECT_CAST (demux),
                  GST_FORMAT_TIME, stop));
        } else {
          /* normal playback, send EOS to all linked pads */
          GST_LOG_OBJECT (demux, "Sending EOS, at end of stream");
          if (!gst_mxf_demux_push_src_event (demux, gst_event_new_eos ())) {
            GST_WARNING_OBJECT (demux, "failed pushing EOS on streams");
          }
        }
      } else {
        GST_ELEMENT_ERROR (demux, STREAM, FAILED,
            ("Internal data stream error."),
            ("stream stopped, reason %s", reason));
        gst_mxf_demux_push_src_event (demux, gst_event_new_eos ());
      }
    }
    gst_object_unref (demux);
    return;
  }
}

static GstFlowReturn
gst_mxf_demux_chain (GstPad * pad, GstBuffer * inbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMXFDemux *demux = NULL;
  MXFUL key;
  const guint8 *data = NULL;
  guint64 length = 0;
  guint64 offset = 0;
  GstBuffer *buffer = NULL;

  demux = GST_MXF_DEMUX (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (demux, "received buffer of %u bytes at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (inbuf), GST_BUFFER_OFFSET (inbuf));

  if (G_UNLIKELY (GST_BUFFER_OFFSET (inbuf) == 0)) {
    GST_DEBUG_OBJECT (demux, "beginning of file, expect header");
    demux->run_in = -1;
    demux->offset = 0;
  }

  if (G_UNLIKELY (demux->offset == 0 && GST_BUFFER_OFFSET (inbuf) != 0)) {
    GST_DEBUG_OBJECT (demux, "offset was zero, synchronizing with buffer's");
    demux->offset = GST_BUFFER_OFFSET (inbuf);
  }

  gst_adapter_push (demux->adapter, inbuf);
  inbuf = NULL;

  while (ret == GST_FLOW_OK) {
    if (G_UNLIKELY (demux->flushing)) {
      GST_DEBUG_OBJECT (demux, "we are now flushing, exiting parser loop");
      ret = GST_FLOW_WRONG_STATE;
      break;
    }

    if (gst_adapter_available (demux->adapter) < 16)
      break;

    if (demux->run_in == -1) {
      /* Skip run-in, which is at most 64K and is finished
       * by a header partition pack */

      while (demux->offset < 64 * 1024
          && gst_adapter_available (demux->adapter) >= 16) {
        data = gst_adapter_peek (demux->adapter, 16);

        if (mxf_is_header_partition_pack ((const MXFUL *)
                data)) {
          GST_DEBUG_OBJECT (demux,
              "Found header partition pack at offset %" G_GUINT64_FORMAT,
              demux->offset);
          demux->run_in = demux->offset;
          break;
        }
        gst_adapter_flush (demux->adapter, 1);
      }
    } else if (demux->offset < demux->run_in) {
      gst_adapter_flush (demux->adapter,
          MIN (gst_adapter_available (demux->adapter),
              demux->run_in - demux->offset));
      continue;
    }

    if (G_UNLIKELY (ret != GST_FLOW_OK))
      break;

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
    data = gst_adapter_peek (demux->adapter, 17);

    memcpy (&key, data, 16);

    /* Decode BER encoded packet length */
    if ((data[16] & 0x80) == 0) {
      length = data[16];
      offset = 17;
    } else {
      guint slen = data[16] & 0x7f;

      offset = 16 + 1 + slen;

      /* Must be at most 8 according to SMPTE-379M 5.3.4 and
       * GStreamer buffers can only have a 4 bytes length */
      if (slen > 8) {
        GST_ERROR_OBJECT (demux, "Invalid KLV packet length: %u", slen);
        ret = GST_FLOW_ERROR;
        break;
      }

      if (gst_adapter_available (demux->adapter) < 17 + slen)
        break;

      data = gst_adapter_peek (demux->adapter, 17 + slen);
      data += 17;

      length = 0;
      while (slen) {
        length = (length << 8) | *data;
        data++;
        slen--;
      }
    }

    /* GStreamer's buffer sizes are stored in a guint so we
     * limit ourself to G_MAXUINT large buffers */
    if (length > G_MAXUINT) {
      GST_ERROR_OBJECT (demux,
          "Unsupported KLV packet length: %" G_GUINT64_FORMAT, length);
      ret = GST_FLOW_ERROR;
      break;
    }

    if (gst_adapter_available (demux->adapter) < offset + length)
      break;

    gst_adapter_flush (demux->adapter, offset);
    buffer = gst_adapter_take_buffer (demux->adapter, length);

    ret = gst_mxf_demux_handle_klv_packet (demux, &key, buffer);
    gst_buffer_unref (buffer);
  }

  gst_object_unref (demux);

  return ret;
}

static gboolean
gst_mxf_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (gst_pad_get_parent (pad));
  gboolean ret;

  GST_DEBUG_OBJECT (pad, "handling event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* TODO: handle this */
      gst_event_unref (event);
      ret = FALSE;
      break;
    default:
      ret = gst_pad_push_event (demux->sinkpad, event);
      break;
  }

  gst_object_unref (demux);

  return ret;
}

static const GstQueryType *
gst_mxf_demux_src_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return types;
}

static gboolean
gst_mxf_demux_src_query (GstPad * pad, GstQuery * query)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (gst_pad_get_parent (pad));
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

      pos = mxfpad->last_stop;
      if (format == GST_FORMAT_DEFAULT && pos != GST_CLOCK_TIME_NONE) {
        if (!mxfpad->material_track || mxfpad->material_track->edit_rate.n == 0
            || mxfpad->material_track->edit_rate.d == 0)
          goto error;

        pos =
            gst_util_uint64_scale (pos,
            mxfpad->material_track->edit_rate.n,
            mxfpad->material_track->edit_rate.d * GST_SECOND);
      }

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

      if (!mxfpad->material_track || !mxfpad->material_track->parent.sequence)
        goto error;

      duration = mxfpad->material_track->parent.sequence->duration;
      if (format == GST_FORMAT_TIME) {
        if (mxfpad->material_track->edit_rate.n == 0 ||
            mxfpad->material_track->edit_rate.d == 0)
          goto error;

        duration =
            gst_util_uint64_scale (duration,
            GST_SECOND * mxfpad->material_track->edit_rate.d,
            mxfpad->material_track->edit_rate.n);
      }

      GST_DEBUG_OBJECT (pad,
          "Returning duration %" G_GINT64_FORMAT " in format %s", duration,
          gst_format_get_name (format));

      gst_query_set_duration (query, format, duration);
      ret = TRUE;
      break;
    }
    default:
      /* else forward upstream */
      ret = gst_pad_peer_query (demux->sinkpad, query);
      break;
  }

done:
  gst_object_unref (demux);
  return ret;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (pad, "query failed");
    goto done;
  }
}

static gboolean
gst_mxf_demux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

static gboolean
gst_mxf_demux_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstMXFDemux *demux;

  demux = GST_MXF_DEMUX (gst_pad_get_parent (sinkpad));

  demux->random_access = FALSE;

  gst_object_unref (demux);

  return TRUE;
}

static gboolean
gst_mxf_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstMXFDemux *demux;

  demux = GST_MXF_DEMUX (gst_pad_get_parent (sinkpad));

  if (active) {
    demux->random_access = TRUE;
    gst_object_unref (demux);
    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_mxf_demux_loop,
        sinkpad);
  } else {
    demux->random_access = FALSE;
    gst_object_unref (demux);
    return gst_pad_stop_task (sinkpad);
  }
}

static gboolean
gst_mxf_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstMXFDemux *demux;
  gboolean ret = FALSE;

  demux = GST_MXF_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "handling event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      demux->flushing = TRUE;
      ret = gst_mxf_demux_push_src_event (demux, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_mxf_demux_flush (demux, TRUE);
      ret = gst_mxf_demux_push_src_event (demux, event);
      break;
    case GST_EVENT_EOS:
      if (!gst_mxf_demux_push_src_event (demux, event))
        GST_WARNING_OBJECT (pad, "failed pushing EOS on streams");
      ret = TRUE;
      break;
    case GST_EVENT_NEWSEGMENT:
      /* TODO: handle this */
      gst_event_unref (event);
      ret = FALSE;
      break;
    default:
      ret = gst_mxf_demux_push_src_event (demux, event);
      break;
  }

  gst_object_unref (demux);

  return ret;
}

static GstStateChangeReturn
gst_mxf_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mxf_demux_finalize (GObject * object)
{
  GstMXFDemux *demux = GST_MXF_DEMUX (object);

  if (demux->adapter) {
    g_object_unref (demux->adapter);
    demux->adapter = NULL;
  }

  if (demux->new_seg_event) {
    gst_event_unref (demux->new_seg_event);
    demux->new_seg_event = NULL;
  }

  if (demux->close_seg_event) {
    gst_event_unref (demux->close_seg_event);
    demux->close_seg_event = NULL;
  }

  g_free (demux->current_package_string);
  demux->current_package_string = NULL;
  g_free (demux->requested_package_string);
  demux->requested_package_string = NULL;

  gst_mxf_demux_remove_pads (demux);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mxf_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mxf_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mxf_src_template));
  gst_element_class_set_details_simple (element_class, "MXF Demuxer",
      "Codec/Demuxer",
      "Demux MXF files", "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_mxf_demux_class_init (GstMXFDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (mxfdemux_debug, "mxfdemux", 0, "MXF demuxer");

  gobject_class->finalize = gst_mxf_demux_finalize;
  gobject_class->set_property = gst_mxf_demux_set_property;
  gobject_class->get_property = gst_mxf_demux_get_property;

  g_object_class_install_property (gobject_class, PROP_PACKAGE,
      g_param_spec_string ("package", "Package",
          "Material or Source package to use for playback", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mxf_demux_change_state);
}

static void
gst_mxf_demux_init (GstMXFDemux * demux, GstMXFDemuxClass * g_class)
{
  demux->sinkpad =
      gst_pad_new_from_static_template (&mxf_sink_template, "sink");

  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_sink_event));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_chain));
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_sink_activate));
  gst_pad_set_activatepull_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_sink_activate_pull));
  gst_pad_set_activatepush_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mxf_demux_sink_activate_push));

  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->adapter = gst_adapter_new ();
  demux->src = g_ptr_array_new ();
  gst_segment_init (&demux->segment, GST_FORMAT_TIME);

  gst_mxf_demux_reset (demux);
}
