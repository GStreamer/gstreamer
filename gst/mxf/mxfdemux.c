/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* TODO:
 *   - start at correct position of the component, switch components
 *   - RandomIndex / IndexSegment support
 *   - timecode tracks
 *   - descriptive metadata
 *   - generic container system items
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mxfdemux.h"
#include "mxfparse.h"
#include "mxfaes-bwf.h"
#include "mxfmpeg.h"

#include <string.h>

static GstStaticPadTemplate mxf_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/mxf")
    );

static GstStaticPadTemplate audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate data_src_template =
GST_STATIC_PAD_TEMPLATE ("data_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (mxfdemux_debug);
#define GST_CAT_DEFAULT mxfdemux_debug

#define GST_TYPE_MXF_PAD (gst_mxf_pad_get_type())
#define GST_MXF_PAD(pad) (G_TYPE_CHECK_INSTANCE_CAST((pad),GST_TYPE_MXF_PAD,GstMXFPad))
#define GST_MXF_PAD_CAST(pad) ((GstMXFPad *) pad)
#define GST_IS_MXF_PAD(pad) (G_TYPE_CHECK_INSTANCE_TYPE((pad),GST_TYPE_MXF_PAD))

typedef struct
{
  GstPad parent;

  guint32 track_id;
  MXFMetadataTrackType track_type;
  gboolean need_segment;

  guint64 essence_element_count;
  MXFEssenceElementHandler handle_essence_element;
  gpointer mapping_data;

  GstTagList *tags;

  guint current_material_component;
  MXFMetadataGenericPackage *material_package;
  MXFMetadataTrack *material_track;
  MXFMetadataStructuralComponent *component;

  MXFMetadataGenericPackage *source_package;
  MXFMetadataTrack *source_track;

  GstCaps *caps;
} GstMXFPad;

typedef struct
{
  GstPadClass parent;

} GstMXFPadClass;

G_DEFINE_TYPE (GstMXFPad, gst_mxf_pad, GST_TYPE_PAD);

static void
gst_mxf_pad_finalize (GObject * object)
{
  GstMXFPad *pad = GST_MXF_PAD (object);

  gst_caps_replace (&pad->caps, NULL);

  g_free (pad->mapping_data);
  pad->mapping_data = NULL;

  if (pad->tags) {
    gst_tag_list_free (pad->tags);
    pad->tags = NULL;
  }

  G_OBJECT_CLASS (gst_mxf_pad_parent_class)->finalize (object);
}

static void
gst_mxf_pad_class_init (GstMXFPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_mxf_pad_finalize;
}

static void
gst_mxf_pad_init (GstMXFPad * pad)
{

}

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
    demux->offset = 0;
  }
}

static void
gst_mxf_demux_remove_pad (GstMXFPad * pad, GstMXFDemux * demux)
{
  gst_element_remove_pad (GST_ELEMENT (demux), GST_PAD (pad));
}

static void
gst_mxf_demux_reset_mxf_state (GstMXFDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "Resetting MXF state");
  mxf_partition_pack_reset (&demux->partition);
  mxf_primer_pack_reset (&demux->primer);
}

static void
gst_mxf_demux_reset_metadata (GstMXFDemux * demux)
{
  guint i;

  GST_DEBUG_OBJECT (demux, "Resetting metadata");

  demux->update_metadata = TRUE;
  demux->final_metadata = FALSE;

  demux->current_package = NULL;

  mxf_metadata_preface_reset (&demux->preface);

  if (demux->identification) {
    for (i = 0; i < demux->identification->len; i++)
      mxf_metadata_identification_reset (&g_array_index (demux->identification,
              MXFMetadataIdentification, i));
    g_array_free (demux->identification, TRUE);
    demux->identification = NULL;
  }

  mxf_metadata_content_storage_reset (&demux->content_storage);

  if (demux->essence_container_data) {
    for (i = 0; i < demux->essence_container_data->len; i++)
      mxf_metadata_essence_container_data_reset (&g_array_index
          (demux->essence_container_data, MXFMetadataEssenceContainerData, i));
    g_array_free (demux->essence_container_data, TRUE);
    demux->essence_container_data = NULL;
  }

  if (demux->material_package) {
    for (i = 0; i < demux->material_package->len; i++)
      mxf_metadata_generic_package_reset (&g_array_index
          (demux->material_package, MXFMetadataGenericPackage, i));
    g_array_free (demux->material_package, TRUE);
    demux->material_package = NULL;
  }

  if (demux->source_package) {
    for (i = 0; i < demux->source_package->len; i++)
      mxf_metadata_generic_package_reset (&g_array_index (demux->source_package,
              MXFMetadataGenericPackage, i));
    g_array_free (demux->source_package, TRUE);
    demux->source_package = NULL;
  }

  if (demux->package) {
    g_ptr_array_free (demux->package, TRUE);
    demux->package = NULL;
  }

  if (demux->track) {
    for (i = 0; i < demux->track->len; i++)
      mxf_metadata_track_reset (&g_array_index (demux->track,
              MXFMetadataTrack, i));
    g_array_free (demux->track, TRUE);
    demux->track = NULL;
  }

  if (demux->sequence) {
    for (i = 0; i < demux->sequence->len; i++)
      mxf_metadata_sequence_reset (&g_array_index (demux->sequence,
              MXFMetadataSequence, i));
    g_array_free (demux->sequence, TRUE);
    demux->sequence = NULL;
  }

  if (demux->structural_component) {
    for (i = 0; i < demux->structural_component->len; i++)
      mxf_metadata_structural_component_reset (&g_array_index
          (demux->structural_component, MXFMetadataStructuralComponent, i));
    g_array_free (demux->structural_component, TRUE);
    demux->structural_component = NULL;
  }

  if (demux->generic_descriptor) {
    for (i = 0; i < demux->generic_descriptor->len; i++)
      mxf_metadata_generic_descriptor_reset (&g_array_index
          (demux->generic_descriptor, MXFMetadataGenericDescriptor, i));
    g_array_free (demux->generic_descriptor, TRUE);
    demux->generic_descriptor = NULL;
  }

  if (demux->file_descriptor) {
    for (i = 0; i < demux->file_descriptor->len; i++)
      mxf_metadata_file_descriptor_reset (&g_array_index
          (demux->file_descriptor, MXFMetadataFileDescriptor, i));
    g_array_free (demux->file_descriptor, TRUE);
    demux->file_descriptor = NULL;
  }

  if (demux->multiple_descriptor) {
    for (i = 0; i < demux->multiple_descriptor->len; i++)
      mxf_metadata_multiple_descriptor_reset (&g_array_index
          (demux->multiple_descriptor, MXFMetadataMultipleDescriptor, i));
    g_array_free (demux->multiple_descriptor, TRUE);
    demux->multiple_descriptor = NULL;
  }

  if (demux->generic_picture_essence_descriptor) {
    for (i = 0; i < demux->generic_picture_essence_descriptor->len; i++)
      mxf_metadata_generic_picture_essence_descriptor_reset (&g_array_index
          (demux->generic_picture_essence_descriptor,
              MXFMetadataGenericPictureEssenceDescriptor, i));
    g_array_free (demux->generic_picture_essence_descriptor, TRUE);
    demux->generic_picture_essence_descriptor = NULL;
  }

  if (demux->cdci_picture_essence_descriptor) {
    for (i = 0; i < demux->cdci_picture_essence_descriptor->len; i++)
      mxf_metadata_cdci_picture_essence_descriptor_reset (&g_array_index
          (demux->cdci_picture_essence_descriptor,
              MXFMetadataCDCIPictureEssenceDescriptor, i));
    g_array_free (demux->cdci_picture_essence_descriptor, TRUE);
    demux->cdci_picture_essence_descriptor = NULL;
  }

  if (demux->mpeg_video_descriptor) {
    for (i = 0; i < demux->mpeg_video_descriptor->len; i++)
      mxf_metadata_mpeg_video_descriptor_reset (&g_array_index
          (demux->mpeg_video_descriptor, MXFMetadataMPEGVideoDescriptor, i));
    g_array_free (demux->mpeg_video_descriptor, TRUE);
    demux->mpeg_video_descriptor = NULL;
  }

  if (demux->generic_sound_essence_descriptor) {
    for (i = 0; i < demux->generic_sound_essence_descriptor->len; i++)
      mxf_metadata_generic_sound_essence_descriptor_reset (&g_array_index
          (demux->generic_sound_essence_descriptor,
              MXFMetadataGenericSoundEssenceDescriptor, i));
    g_array_free (demux->generic_sound_essence_descriptor, TRUE);
    demux->generic_sound_essence_descriptor = NULL;
  }

  if (demux->wave_audio_essence_descriptor) {
    for (i = 0; i < demux->wave_audio_essence_descriptor->len; i++)
      mxf_metadata_wave_audio_essence_descriptor_reset (&g_array_index
          (demux->wave_audio_essence_descriptor,
              MXFMetadataWaveAudioEssenceDescriptor, i));
    g_array_free (demux->wave_audio_essence_descriptor, TRUE);
    demux->wave_audio_essence_descriptor = NULL;
  }

  if (demux->descriptor) {
    g_ptr_array_free (demux->descriptor, TRUE);
    demux->descriptor = NULL;
  }

  if (demux->locator) {
    for (i = 0; i < demux->locator->len; i++)
      mxf_metadata_locator_reset (&g_array_index (demux->locator,
              MXFMetadataLocator, i));
    g_array_free (demux->locator, TRUE);
    demux->locator = NULL;
  }
}

static void
gst_mxf_demux_reset (GstMXFDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "cleaning up MXF demuxer");

  demux->flushing = FALSE;

  demux->header_partition_pack_offset = 0;
  demux->footer_partition_pack_offset = 0;
  demux->offset = 0;

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

  if (demux->src) {
    g_ptr_array_foreach (demux->src, (GFunc) gst_mxf_demux_remove_pad, demux);
    g_ptr_array_foreach (demux->src, (GFunc) gst_object_unref, NULL);
    g_ptr_array_free (demux->src, TRUE);
    demux->src = NULL;
  }

  gst_mxf_demux_reset_mxf_state (demux);
  gst_mxf_demux_reset_metadata (demux);
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
  gint i;

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
  if (demux->partition.valid) {
    mxf_partition_pack_reset (&demux->partition);
    mxf_primer_pack_reset (&demux->primer);
  }

  GST_DEBUG_OBJECT (demux,
      "Handling partition pack of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_partition_pack_parse (key, &demux->partition,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing partition pack failed");
    return GST_FLOW_ERROR;
  }

  if (demux->partition.type == MXF_PARTITION_PACK_HEADER)
    demux->footer_partition_pack_offset = demux->partition.footer_partition;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_primer_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (demux,
      "Handling primer pack of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (G_UNLIKELY (!demux->partition.valid)) {
    GST_ERROR_OBJECT (demux, "Primer pack before partition pack");
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (demux->primer.valid)) {
    GST_ERROR_OBJECT (demux, "Primer pack already exists");
    return GST_FLOW_ERROR;
  }

  if (!mxf_primer_pack_parse (key, &demux->primer,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing primer pack failed");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_preface (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  MXFMetadataPreface preface;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata preface of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (demux->final_metadata) {
    GST_DEBUG_OBJECT (demux, "Metadata is already final, skipping");
    return GST_FLOW_OK;
  }

  if (!mxf_metadata_preface_parse (key, &preface, &demux->primer,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata preface failed");
    return GST_FLOW_ERROR;
  }

  if (mxf_timestamp_is_unknown (&demux->preface.last_modified_date)
      || (!mxf_timestamp_is_unknown (&preface.last_modified_date)
          && mxf_timestamp_compare (&demux->preface.last_modified_date,
              &preface.last_modified_date) < 0)) {
    GST_DEBUG_OBJECT (demux,
        "Timestamp of new preface is newer than old, updating metadata");
    gst_mxf_demux_reset_metadata (demux);
    memcpy (&demux->preface, &preface, sizeof (MXFMetadataPreface));
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_identification (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  MXFMetadataIdentification identification;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata identification of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_identification_parse (key, &identification,
          &demux->primer, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata identification failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->identification)
    demux->identification =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataIdentification));

  g_array_append_val (demux->identification, identification);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_content_storage (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (demux,
      "Handling metadata content storage of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_content_storage_parse (key,
          &demux->content_storage, &demux->primer, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata content storage failed");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_essence_container_data (GstMXFDemux *
    demux, const MXFUL * key, GstBuffer * buffer)
{
  MXFMetadataEssenceContainerData essence_container_data;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata essence container data of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_essence_container_data_parse (key,
          &essence_container_data, &demux->primer, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata essence container data failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->essence_container_data)
    demux->essence_container_data =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataEssenceContainerData));

  g_array_append_val (demux->essence_container_data, essence_container_data);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_material_package (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  MXFMetadataGenericPackage material_package;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata material package of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_generic_package_parse (key, &material_package,
          &demux->primer, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata material package failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->material_package)
    demux->material_package =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataMaterialPackage));

  g_array_append_val (demux->material_package, material_package);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_source_package (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  MXFMetadataGenericPackage source_package;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata source package of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_generic_package_parse (key, &source_package,
          &demux->primer, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata source package failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->source_package)
    demux->source_package =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataSourcePackage));

  g_array_append_val (demux->source_package, source_package);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_track (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  MXFMetadataTrack track;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata track of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_track_parse (key, &track, &demux->primer,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata track timecode failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->track)
    demux->track = g_array_new (FALSE, FALSE, sizeof (MXFMetadataTrack));

  g_array_append_val (demux->track, track);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_sequence (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  MXFMetadataSequence sequence;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata sequence of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_sequence_parse (key, &sequence,
          &demux->primer, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata sequence failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->sequence)
    demux->sequence = g_array_new (FALSE, FALSE, sizeof (MXFMetadataSequence));

  g_array_append_val (demux->sequence, sequence);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_structural_component (GstMXFDemux * demux,
    const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataStructuralComponent component;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata structural component of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!mxf_metadata_structural_component_parse (key, &component,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata structural component failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->structural_component)
    demux->structural_component =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataStructuralComponent));

  g_array_append_val (demux->structural_component, component);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_generic_descriptor (GstMXFDemux * demux,
    const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataGenericDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata generic descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_generic_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata generic descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->generic_descriptor)
    demux->generic_descriptor =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataGenericDescriptor));

  g_array_append_val (demux->generic_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_file_descriptor (GstMXFDemux * demux,
    const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataFileDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata file descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_file_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata file descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->file_descriptor)
    demux->file_descriptor =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataFileDescriptor));

  g_array_append_val (demux->file_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_multiple_descriptor (GstMXFDemux * demux,
    const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataMultipleDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata multiple descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_multiple_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata multiple descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->multiple_descriptor)
    demux->multiple_descriptor =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataMultipleDescriptor));

  g_array_append_val (demux->multiple_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_generic_picture_essence_descriptor (GstMXFDemux *
    demux, const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataGenericPictureEssenceDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata generic picture essence descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_generic_picture_essence_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux,
        "Parsing metadata generic picture essence descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->generic_picture_essence_descriptor)
    demux->generic_picture_essence_descriptor =
        g_array_new (FALSE, FALSE,
        sizeof (MXFMetadataGenericPictureEssenceDescriptor));

  g_array_append_val (demux->generic_picture_essence_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_cdci_picture_essence_descriptor (GstMXFDemux *
    demux, const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataCDCIPictureEssenceDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata CDCI picture essence descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_cdci_picture_essence_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux,
        "Parsing metadata CDCI picture essence descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->cdci_picture_essence_descriptor)
    demux->cdci_picture_essence_descriptor =
        g_array_new (FALSE, FALSE,
        sizeof (MXFMetadataCDCIPictureEssenceDescriptor));

  g_array_append_val (demux->cdci_picture_essence_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_mpeg_video_descriptor (GstMXFDemux * demux,
    const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataMPEGVideoDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata MPEG video descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_mpeg_video_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata MPEG video descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->mpeg_video_descriptor)
    demux->mpeg_video_descriptor =
        g_array_new (FALSE, FALSE, sizeof (MXFMetadataMPEGVideoDescriptor));

  g_array_append_val (demux->mpeg_video_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_generic_sound_essence_descriptor (GstMXFDemux *
    demux, const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataGenericSoundEssenceDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata generic sound essence descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_generic_sound_essence_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux,
        "Parsing metadata generic sound essence descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->generic_sound_essence_descriptor)
    demux->generic_sound_essence_descriptor =
        g_array_new (FALSE, FALSE,
        sizeof (MXFMetadataGenericSoundEssenceDescriptor));

  g_array_append_val (demux->generic_sound_essence_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_wave_audio_essence_descriptor (GstMXFDemux *
    demux, const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataWaveAudioEssenceDescriptor descriptor;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata wave sound essence descriptor of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_wave_audio_essence_descriptor_parse (key, &descriptor,
          &demux->primer, type, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux,
        "Parsing metadata wave sound essence descriptor failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->wave_audio_essence_descriptor)
    demux->wave_audio_essence_descriptor =
        g_array_new (FALSE, FALSE,
        sizeof (MXFMetadataWaveAudioEssenceDescriptor));

  g_array_append_val (demux->wave_audio_essence_descriptor, descriptor);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata_locator (GstMXFDemux * demux,
    const MXFUL * key, guint16 type, GstBuffer * buffer)
{
  MXFMetadataLocator locator;

  GST_DEBUG_OBJECT (demux,
      "Handling metadata locator of size %u"
      " at offset %" G_GUINT64_FORMAT " with type 0x%04d",
      GST_BUFFER_SIZE (buffer), demux->offset, type);

  if (!mxf_metadata_locator_parse (key, &locator, &demux->primer,
          type, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
    GST_ERROR_OBJECT (demux, "Parsing metadata locator failed");
    return GST_FLOW_ERROR;
  }

  if (!demux->locator)
    demux->locator = g_array_new (FALSE, FALSE, sizeof (MXFMetadataLocator));

  g_array_append_val (demux->locator, locator);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_header_metadata_resolve_references (GstMXFDemux * demux)
{
  guint i, j, k;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (demux, "Resolve metadata references");
  demux->update_metadata = FALSE;
  if (demux->partition.closed && demux->partition.complete)
    demux->final_metadata = TRUE;

  /* Fill in demux->descriptor */
  demux->descriptor = g_ptr_array_new ();
  if (demux->generic_descriptor) {
    for (i = 0; i < demux->generic_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->generic_descriptor,
              MXFMetadataGenericDescriptor, i));
    }
  }
  if (demux->file_descriptor) {
    for (i = 0; i < demux->file_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->file_descriptor, MXFMetadataFileDescriptor,
              i));
    }
  }
  if (demux->generic_picture_essence_descriptor) {
    for (i = 0; i < demux->generic_picture_essence_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->generic_picture_essence_descriptor,
              MXFMetadataGenericPictureEssenceDescriptor, i));
    }
  }
  if (demux->cdci_picture_essence_descriptor) {
    for (i = 0; i < demux->cdci_picture_essence_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->cdci_picture_essence_descriptor,
              MXFMetadataCDCIPictureEssenceDescriptor, i));
    }
  }
  if (demux->mpeg_video_descriptor) {
    for (i = 0; i < demux->mpeg_video_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->mpeg_video_descriptor,
              MXFMetadataMPEGVideoDescriptor, i));
    }
  }
  if (demux->generic_sound_essence_descriptor) {
    for (i = 0; i < demux->generic_sound_essence_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->generic_sound_essence_descriptor,
              MXFMetadataGenericSoundEssenceDescriptor, i));
    }
  }
  if (demux->wave_audio_essence_descriptor) {
    for (i = 0; i < demux->wave_audio_essence_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->wave_audio_essence_descriptor,
              MXFMetadataWaveAudioEssenceDescriptor, i));
    }
  }
  if (demux->multiple_descriptor) {
    for (i = 0; i < demux->multiple_descriptor->len; i++) {
      g_ptr_array_add (demux->descriptor,
          &g_array_index (demux->multiple_descriptor,
              MXFMetadataMultipleDescriptor, i));
    }
  }

  /* Fill in demux->package */
  demux->package = g_ptr_array_new ();
  if (demux->material_package) {
    for (i = 0; i < demux->material_package->len; i++) {
      g_ptr_array_add (demux->package, &g_array_index (demux->material_package,
              MXFMetadataGenericPackage, i));
    }
  }
  if (demux->source_package) {
    for (i = 0; i < demux->source_package->len; i++) {
      g_ptr_array_add (demux->package, &g_array_index (demux->source_package,
              MXFMetadataGenericPackage, i));
    }
  }

  /* Multiple descriptor */
  if (demux->multiple_descriptor && demux->descriptor) {
    for (i = 0; i < demux->multiple_descriptor->len; i++) {
      MXFMetadataMultipleDescriptor *d =
          &g_array_index (demux->multiple_descriptor,
          MXFMetadataMultipleDescriptor, i);

      d->sub_descriptors =
          g_new0 (MXFMetadataGenericDescriptor *, d->n_sub_descriptors);
      for (j = 0; j < d->n_sub_descriptors; j++) {
        for (k = 0; k < demux->descriptor->len; k++) {
          MXFMetadataGenericDescriptor *e =
              g_ptr_array_index (demux->descriptor, k);

          if (mxf_ul_is_equal (&d->sub_descriptors_uids[j], &e->instance_uid)) {
            d->sub_descriptors[j] = e;
            break;
          }
        }
      }
    }
  }

  /* See SMPTE 377M 8.4 */

  /* Preface */
  if (demux->package) {
    for (i = 0; i < demux->package->len; i++) {
      MXFMetadataGenericPackage *package =
          g_ptr_array_index (demux->package, i);

      if (mxf_ul_is_equal (&demux->preface.primary_package_uid,
              &package->instance_uid)) {
        demux->preface.primary_package = package;
        break;
      }
    }
  }

  demux->preface.identifications =
      g_new0 (MXFMetadataIdentification *, demux->preface.n_identifications);
  if (demux->identification) {
    for (i = 0; i < demux->identification->len; i++) {
      MXFMetadataIdentification *identification =
          &g_array_index (demux->identification,
          MXFMetadataIdentification, i);

      for (j = 0; j < demux->preface.n_identifications; j++) {
        if (mxf_ul_is_equal (&demux->preface.identifications_uids[j],
                &identification->instance_uid)) {
          demux->preface.identifications[j] = identification;
          break;
        }
      }
    }
  }

  if (!mxf_ul_is_equal (&demux->preface.content_storage_uid,
          &demux->content_storage.instance_uid)) {
    GST_ERROR_OBJECT (demux,
        "Preface content storage UID not equal to actual content storage instance uid");
    ret = GST_FLOW_ERROR;
    goto error;
  }
  demux->preface.content_storage = &demux->content_storage;

  /* TODO: dm_schemes */

  /* Content storage */
  demux->content_storage.packages =
      g_new0 (MXFMetadataGenericPackage *, demux->content_storage.n_packages);
  if (demux->package) {
    for (i = 0; i < demux->package->len; i++) {
      MXFMetadataGenericPackage *package =
          g_ptr_array_index (demux->package, i);

      for (j = 0; j < demux->content_storage.n_packages; j++) {
        if (mxf_ul_is_equal (&demux->content_storage.packages_uids[j],
                &package->instance_uid)) {
          demux->content_storage.packages[j] = package;
          break;
        }
      }
    }
  }

  demux->content_storage.essence_container_data =
      g_new0 (MXFMetadataEssenceContainerData *,
      demux->content_storage.n_essence_container_data);
  if (demux->essence_container_data) {
    for (i = 0; i < demux->essence_container_data->len; i++) {
      MXFMetadataEssenceContainerData *data =
          &g_array_index (demux->essence_container_data,
          MXFMetadataEssenceContainerData, i);

      for (j = 0; j < demux->content_storage.n_essence_container_data; j++) {
        if (mxf_ul_is_equal (&demux->content_storage.
                essence_container_data_uids[j], &data->instance_uid)) {
          demux->content_storage.essence_container_data[j] = data;
          break;
        }
      }
    }
  }

  /* Essence container data */
  if (demux->package && demux->essence_container_data) {
    for (i = 0; i < demux->package->len; i++) {
      MXFMetadataGenericPackage *package =
          g_ptr_array_index (demux->package, i);

      for (j = 0; j < demux->essence_container_data->len; j++) {
        MXFMetadataEssenceContainerData *data =
            &g_array_index (demux->essence_container_data,
            MXFMetadataEssenceContainerData, j);

        if (mxf_umid_is_equal (&data->linked_package_uid,
                &package->package_uid)) {
          data->linked_package = package;
          break;
        }
      }
    }
  }

  /* Generic package */
  if (demux->package) {
    for (i = 0; i < demux->package->len; i++) {
      MXFMetadataGenericPackage *package =
          g_ptr_array_index (demux->package, i);

      package->tracks = g_new0 (MXFMetadataTrack *, package->n_tracks);
      if (demux->track) {
        for (j = 0; j < package->n_tracks; j++) {
          for (k = 0; k < demux->track->len; k++) {
            MXFMetadataTrack *track =
                &g_array_index (demux->track, MXFMetadataTrack, k);

            if (mxf_ul_is_equal (&package->tracks_uids[j],
                    &track->instance_uid)) {
              package->tracks[j] = track;
              break;
            }
          }
        }
      }

      /* Resolve descriptors */
      if (package->n_descriptors > 0 && demux->descriptor) {
        MXFMetadataGenericDescriptor *d = NULL;

        for (j = 0; j < demux->descriptor->len; j++) {
          MXFMetadataGenericDescriptor *descriptor =
              g_ptr_array_index (demux->descriptor, j);

          if (mxf_ul_is_equal (&package->descriptors_uid,
                  &descriptor->instance_uid)) {
            d = descriptor;
            break;
          }
        }

        if (d && d->type == MXF_METADATA_MULTIPLE_DESCRIPTOR) {
          MXFMetadataMultipleDescriptor *e =
              (MXFMetadataMultipleDescriptor *) d;

          package->n_descriptors = e->n_sub_descriptors;
          package->descriptors =
              g_new0 (MXFMetadataGenericDescriptor *, e->n_sub_descriptors);

          /* These are already resolved */
          for (j = 0; j < e->n_sub_descriptors; j++)
            package->descriptors[j] = e->sub_descriptors[j];
        } else {
          package->n_descriptors = 1;
          package->descriptors = g_new0 (MXFMetadataGenericDescriptor *, 1);
          package->descriptors[0] = d;
        }
      }

      if (package->tracks && package->descriptors) {
        for (j = 0; j < package->n_tracks; j++) {
          MXFMetadataTrack *track = package->tracks[j];
          guint n_descriptor = 0;

          if (!track)
            continue;

          for (k = 0; k < package->n_descriptors; k++) {
            MXFMetadataGenericDescriptor *d = package->descriptors[k];
            MXFMetadataFileDescriptor *e;

            if (!d || !d->is_file_descriptor)
              continue;

            e = (MXFMetadataFileDescriptor *) d;

            if (e->linked_track_id == track->track_id)
              n_descriptor++;
          }

          track->n_descriptor = n_descriptor;
          track->descriptor =
              g_new0 (MXFMetadataFileDescriptor *, n_descriptor);
          n_descriptor = 0;

          for (k = 0; k < package->n_descriptors; k++) {
            MXFMetadataGenericDescriptor *d = package->descriptors[k];
            MXFMetadataFileDescriptor *e;

            if (!d || !d->is_file_descriptor)
              continue;

            e = (MXFMetadataFileDescriptor *) d;

            if (e->linked_track_id == track->track_id)
              track->descriptor[n_descriptor++] = e;
          }
        }
      }
    }
  }

  /* Tracks */
  if (demux->track && demux->sequence) {
    for (i = 0; i < demux->track->len; i++) {
      MXFMetadataTrack *track =
          &g_array_index (demux->track, MXFMetadataTrack, i);

      for (j = 0; j < demux->sequence->len; j++) {
        MXFMetadataSequence *sequence =
            &g_array_index (demux->sequence, MXFMetadataSequence,
            i);

        if (mxf_ul_is_equal (&track->sequence_uid, &sequence->instance_uid)) {
          track->sequence = sequence;
          break;
        }
      }
    }
  }

  /* Sequences */
  if (demux->sequence) {
    for (i = 0; i < demux->sequence->len; i++) {
      MXFMetadataSequence *sequence =
          &g_array_index (demux->sequence, MXFMetadataSequence, i);

      sequence->structural_components =
          g_new0 (MXFMetadataStructuralComponent *,
          sequence->n_structural_components);

      if (demux->structural_component) {
        for (j = 0; j < sequence->n_structural_components; j++) {
          for (k = 0; k < demux->structural_component->len; k++) {
            MXFMetadataStructuralComponent *component =
                &g_array_index (demux->structural_component,
                MXFMetadataStructuralComponent, k);

            if (mxf_ul_is_equal (&sequence->structural_components_uids[j],
                    &component->instance_uid)) {
              sequence->structural_components[j] = component;
              break;
            }
          }
        }
      }
    }
  }

  /* Source clips */
  if (demux->structural_component && demux->source_package) {
    for (i = 0; i < demux->structural_component->len; i++) {
      MXFMetadataStructuralComponent *component =
          &g_array_index (demux->structural_component,
          MXFMetadataStructuralComponent, i);

      if (component->type != MXF_METADATA_SOURCE_CLIP)
        continue;

      for (j = 0; j < demux->source_package->len; j++) {
        MXFMetadataGenericPackage *package =
            &g_array_index (demux->source_package, MXFMetadataGenericPackage,
            j);

        if (mxf_umid_is_equal (&component->source_clip.source_package_id,
                &package->package_uid)) {
          component->source_clip.source_package = package;
          break;
        }
      }
    }
  }

  /* Generic descriptors */
  if (demux->descriptor) {
    for (i = 0; i < demux->descriptor->len; i++) {
      MXFMetadataGenericDescriptor *descriptor =
          g_ptr_array_index (demux->descriptor, i);

      descriptor->locators =
          g_new0 (MXFMetadataLocator *, descriptor->n_locators);

      if (demux->locator) {
        for (j = 0; j < descriptor->n_locators; j++) {
          for (k = 0; k < demux->locator->len; k++) {
            MXFMetadataLocator *locator =
                &g_array_index (demux->locator, MXFMetadataLocator,
                k);

            if (mxf_ul_is_equal (&descriptor->locators_uids[j],
                    &locator->instance_uid)) {
              descriptor->locators[j] = locator;
              break;
            }
          }
        }
      }
    }
  }

  /* Mark packages as material, top-level source and source */
  if (demux->material_package) {
    for (i = 0; i < demux->material_package->len; i++) {
      MXFMetadataGenericPackage *package =
          &g_array_index (demux->material_package, MXFMetadataGenericPackage,
          i);

      package->type = MXF_METADATA_GENERIC_PACKAGE_MATERIAL;

      for (j = 0; j < package->n_tracks; j++) {
        MXFMetadataTrack *track = package->tracks[j];
        MXFMetadataSequence *sequence;

        if (!track || !track->sequence)
          continue;

        sequence = track->sequence;

        for (k = 0; k < sequence->n_structural_components; k++) {
          MXFMetadataStructuralComponent *component =
              sequence->structural_components[k];

          if (!component || component->type != MXF_METADATA_SOURCE_CLIP
              || !component->source_clip.source_package)
            continue;

          component->source_clip.source_package->type =
              MXF_METADATA_GENERIC_PACKAGE_TOP_LEVEL_SOURCE;
        }
      }
    }
  }

  /* Store, for every package, the number of timestamp, metadata, essence and other tracks */
  if (demux->package) {
    for (i = 0; i < demux->package->len; i++) {
      MXFMetadataGenericPackage *package =
          g_ptr_array_index (demux->package, i);

      if (!package->tracks || package->n_tracks == 0)
        continue;

      for (j = 0; j < package->n_tracks; j++) {
        MXFMetadataTrack *track = package->tracks[j];
        MXFMetadataSequence *sequence;
        MXFMetadataTrackType type = MXF_METADATA_TRACK_UNKNOWN;

        if (!track || !track->sequence)
          continue;

        sequence = track->sequence;

        if (mxf_ul_is_zero (&sequence->data_definition)
            && sequence->structural_components) {
          for (k = 0; k < sequence->n_structural_components; k++) {
            MXFMetadataStructuralComponent *component =
                sequence->structural_components[k];
            if (!component || mxf_ul_is_zero (&component->data_definition))
              continue;

            type =
                mxf_metadata_track_identifier_parse
                (&component->data_definition);
            break;
          }
        } else {
          type =
              mxf_metadata_track_identifier_parse (&sequence->data_definition);
        }

        if (type == MXF_METADATA_TRACK_UNKNOWN)
          continue;
        else if ((type & 0xf0) == 0x10)
          package->n_timecode_tracks++;
        else if ((type & 0xf0) == 0x20)
          package->n_metadata_tracks++;
        else if ((type & 0xf0) == 0x30)
          package->n_essence_tracks++;
        else if ((type & 0xf0) == 0x40)
          package->n_other_tracks++;
      }
    }
  }

  return ret;

error:
  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_header_metadata_update_streams (GstMXFDemux * demux)
{
  MXFMetadataGenericPackage *current_package = NULL;
  guint i, j, k;
  gboolean first_run;

  GST_DEBUG_OBJECT (demux, "Updating streams");

choose_package:
  /* If no package was selected, select the first material package */
  if (mxf_umid_is_zero (&demux->current_package_uid)
      && !demux->material_package) {
    GST_ERROR_OBJECT (demux, "No material package");
    return GST_FLOW_ERROR;
  } else if (mxf_umid_is_zero (&demux->current_package_uid)) {
    MXFMetadataGenericPackage *p =
        (MXFMetadataGenericPackage *) demux->material_package->data;
    memcpy (&demux->current_package_uid, &p->package_uid, 32);
    current_package = p;
    if (demux->src) {
      for (i = 0; i < demux->src->len; i++) {
        GstMXFPad *pad = g_ptr_array_index (demux->src, i);
        gst_element_remove_pad (GST_ELEMENT_CAST (demux), GST_PAD_CAST (pad));
        gst_object_unref (pad);
      }
      g_ptr_array_free (demux->src, TRUE);
      demux->src = NULL;
    }
  }

  if (!current_package && demux->package) {
    for (i = 0; i < demux->package->len; i++) {
      MXFMetadataGenericPackage *p = g_ptr_array_index (demux->package, i);

      if (mxf_umid_is_equal (&p->package_uid, &demux->current_package_uid)) {
        current_package = p;
        break;
      }
    }
  }

  if (!current_package) {
    GST_WARNING_OBJECT (demux,
        "Selected package not found in header metadata, choosing the first best");
    memset (&demux->current_package_uid, 0, sizeof (MXFUMID));
    goto choose_package;
  }

  if (current_package->type == MXF_METADATA_GENERIC_PACKAGE_SOURCE) {
    GST_WARNING_OBJECT (demux,
        "Selected package is not a material package or top-level source package, choosing the first best");
    memset (&demux->current_package_uid, 0, sizeof (MXFUMID));
    goto choose_package;
  }

  if (!current_package->tracks) {
    GST_ERROR_OBJECT (demux, "Current package has no (resolved) tracks");
    return GST_FLOW_ERROR;
  } else if (!current_package->n_essence_tracks) {
    GST_ERROR_OBJECT (demux, "Current package has no essence tracks");
    return GST_FLOW_ERROR;
  }

  first_run = (demux->src == NULL);
  demux->current_package = current_package;

  for (i = 0; i < current_package->n_tracks; i++) {
    MXFMetadataTrack *track = current_package->tracks[i];
    MXFMetadataTrackType track_type = MXF_METADATA_TRACK_UNKNOWN;
    MXFMetadataSequence *sequence;
    MXFMetadataStructuralComponent *component = NULL;
    MXFMetadataGenericPackage *source_package = NULL;
    MXFMetadataTrack *source_track = NULL;
    GstMXFPad *pad = NULL;
    GstCaps *caps = NULL;

    GST_DEBUG_OBJECT (demux, "Handling track %d", i);

    if (!track) {
      GST_WARNING_OBJECT (demux, "Unresolved track");
      continue;
    } else if (!track->sequence) {
      GST_WARNING_OBJECT (demux, "Track with no sequence");
      continue;
    }

    sequence = track->sequence;

    if (current_package->type == MXF_METADATA_GENERIC_PACKAGE_TOP_LEVEL_SOURCE) {
      source_package = current_package;
      source_track = track;
    }

    track_type =
        mxf_metadata_track_identifier_parse (&sequence->data_definition);

    /* TODO: handle multiple components, see SMPTE 377M page 37 */
    if (sequence->structural_components && sequence->structural_components[0]) {
      component = sequence->structural_components[0];

      if (track_type == MXF_METADATA_TRACK_UNKNOWN)
        track_type =
            mxf_metadata_track_identifier_parse (&component->data_definition);

      if (!source_package && component->type == MXF_METADATA_SOURCE_CLIP
          && component->source_clip.source_package
          && component->source_clip.source_package->type ==
          MXF_METADATA_GENERIC_PACKAGE_TOP_LEVEL_SOURCE
          && component->source_clip.source_package->tracks) {
        source_package = component->source_clip.source_package;

        for (k = 0; k < source_package->n_tracks; k++) {
          MXFMetadataTrack *tmp = source_package->tracks[k];

          if (tmp->track_id == component->source_clip.source_track_id) {
            source_track = tmp;
            break;
          }
        }
      }
    }

    if (track_type && (track_type & 0xf0) != 0x30) {
      GST_DEBUG_OBJECT (demux, "No essence track");
      continue;
    }

    if (!source_package || track_type == MXF_METADATA_TRACK_UNKNOWN
        || !source_track || !component) {
      GST_WARNING_OBJECT (demux,
          "No source package or track type for track found");
      continue;
    }

    if (!source_package->descriptors) {
      GST_WARNING_OBJECT (demux, "Source package has no descriptors");
      continue;
    }

    if (!source_track->descriptor) {
      GST_WARNING_OBJECT (demux, "No descriptor found for track");
      continue;
    }

    if (demux->src && demux->src->len > 0) {
      /* Find pad from track_id */
      for (j = 0; j < demux->src->len; j++) {
        GstMXFPad *tmp = g_ptr_array_index (demux->src, j);

        if (tmp->track_id == track->track_id) {
          pad = tmp;
          break;
        }
      }
    }

    if (!pad && first_run) {
      GstPadTemplate *templ;
      gchar *pad_name;

      switch (track_type) {
        case MXF_METADATA_TRACK_PICTURE_ESSENCE:
          templ =
              gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (demux),
              "video_%d");
          pad_name = g_strdup_printf ("video_%d", source_track->track_id);
          break;
        case MXF_METADATA_TRACK_SOUND_ESSENCE:
          templ =
              gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (demux),
              "audio_%d");
          pad_name = g_strdup_printf ("audio_%d", source_track->track_id);
          break;
        case MXF_METADATA_TRACK_DATA_ESSENCE:
          templ =
              gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (demux),
              "data_%d");
          pad_name = g_strdup_printf ("data_%d", source_track->track_id);
          break;
        default:
          g_assert_not_reached ();
          break;
      }

      g_assert (templ != NULL);

      /* Create pad */
      pad = (GstMXFPad *) g_object_new (GST_TYPE_MXF_PAD,
          "name", pad_name, "direction", GST_PAD_SRC, "template", templ, NULL);
      pad->need_segment = TRUE;
      g_free (pad_name);
    }

    if (!pad) {
      GST_WARNING_OBJECT (demux,
          "Not the first pad addition run, ignoring new track");
      continue;
    }

    /* Update pad */
    pad->track_id = track->track_id;
    pad->track_type = track_type;

    pad->material_package = current_package;
    pad->material_track = track;
    pad->current_material_component = 0;
    pad->component = component;

    pad->source_package = source_package;
    pad->source_track = source_track;

    pad->handle_essence_element = NULL;
    g_free (pad->mapping_data);
    pad->mapping_data = NULL;

    switch (track_type) {
      case MXF_METADATA_TRACK_PICTURE_ESSENCE:
        if (mxf_is_mpeg_video_essence_track (source_track))
          caps =
              mxf_mpeg_video_create_caps (source_package, source_track,
              &pad->tags, &pad->handle_essence_element, &pad->mapping_data);
        break;
      case MXF_METADATA_TRACK_SOUND_ESSENCE:
        if (mxf_is_aes_bwf_essence_track (source_track))
          caps =
              mxf_aes_bwf_create_caps (source_package, source_track, &pad->tags,
              &pad->handle_essence_element, &pad->mapping_data);
        break;
      case MXF_METADATA_TRACK_DATA_ESSENCE:
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    if (!caps) {
      GST_WARNING_OBJECT (demux, "No caps created, ignoring stream");
      gst_object_unref (pad);
      continue;
    }

    GST_DEBUG_OBJECT (demux, "Created caps %" GST_PTR_FORMAT, caps);

    if (pad->caps && !gst_caps_is_equal (pad->caps, caps)) {
      gst_pad_set_caps (GST_PAD_CAST (pad), caps);
      gst_caps_replace (&pad->caps, gst_caps_ref (caps));
    } else if (!pad->caps) {
      gst_pad_set_caps (GST_PAD_CAST (pad), caps);
      pad->caps = gst_caps_ref (caps);

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
    }
    gst_caps_unref (caps);
  }

  gst_element_no_more_pads (GST_ELEMENT_CAST (demux));

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_metadata (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  guint16 type;
  GstFlowReturn ret = GST_FLOW_OK;

  type = GST_READ_UINT16_BE (key->u + 13);

  GST_DEBUG_OBJECT (demux,
      "Handling metadata of size %u at offset %"
      G_GUINT64_FORMAT " of type 0x%04d", GST_BUFFER_SIZE (buffer),
      demux->offset, type);

  if (G_UNLIKELY (!demux->partition.valid)) {
    GST_ERROR_OBJECT (demux, "Partition pack doesn't exist");
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (!demux->primer.valid)) {
    GST_ERROR_OBJECT (demux, "Primer pack doesn't exists");
    return GST_FLOW_ERROR;
  }

  if (type != MXF_METADATA_PREFACE && !demux->update_metadata) {
    GST_DEBUG_OBJECT (demux,
        "Skipping parsing of metadata because it's older than what we have");
    return GST_FLOW_OK;
  }

  /* Make writable as the parsing of descriptors sets already read local tags to 0x0000 */
  if (!gst_buffer_is_writable (buffer))
    buffer = gst_buffer_copy (buffer);
  else
    buffer = gst_buffer_ref (buffer);

  switch (type) {
    case MXF_METADATA_PREFACE:
      ret = gst_mxf_demux_handle_metadata_preface (demux, key, buffer);
      break;
    case MXF_METADATA_IDENTIFICATION:
      ret = gst_mxf_demux_handle_metadata_identification (demux, key, buffer);
      break;
    case MXF_METADATA_CONTENT_STORAGE:
      ret = gst_mxf_demux_handle_metadata_content_storage (demux, key, buffer);
      break;
    case MXF_METADATA_ESSENCE_CONTAINER_DATA:
      ret =
          gst_mxf_demux_handle_metadata_essence_container_data (demux,
          key, buffer);
      break;
    case MXF_METADATA_MATERIAL_PACKAGE:
      ret = gst_mxf_demux_handle_metadata_material_package (demux, key, buffer);
      break;
    case MXF_METADATA_SOURCE_PACKAGE:
      ret = gst_mxf_demux_handle_metadata_source_package (demux, key, buffer);
      break;
    case MXF_METADATA_TRACK:
      ret = gst_mxf_demux_handle_metadata_track (demux, key, buffer);
      break;
    case MXF_METADATA_SEQUENCE:
      ret = gst_mxf_demux_handle_metadata_sequence (demux, key, buffer);
      break;
    case MXF_METADATA_TIMECODE_COMPONENT:
    case MXF_METADATA_SOURCE_CLIP:
      ret =
          gst_mxf_demux_handle_metadata_structural_component (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_RGBA_PICTURE_ESSENCE_DESCRIPTOR:
    case MXF_METADATA_GENERIC_DATA_ESSENCE_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_generic_descriptor (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_FILE_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_file_descriptor (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_generic_picture_essence_descriptor
          (demux, key, type, buffer);
      break;
    case MXF_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_cdci_picture_essence_descriptor (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_MPEG_VIDEO_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_mpeg_video_descriptor (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_generic_sound_essence_descriptor (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_MULTIPLE_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_multiple_descriptor (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR:
      ret =
          gst_mxf_demux_handle_metadata_wave_audio_essence_descriptor (demux,
          key, type, buffer);
      break;
    case MXF_METADATA_NETWORK_LOCATOR:
    case MXF_METADATA_TEXT_LOCATOR:
      ret = gst_mxf_demux_handle_metadata_locator (demux, key, type, buffer);
      break;
    default:
      GST_WARNING_OBJECT (demux,
          "Unknown or unhandled metadata type 0x%04x", type);
      break;
  }

  gst_buffer_unref (buffer);

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
gst_mxf_demux_handle_generic_container_essence_element (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 track_number;
  guint i;
  GstMXFPad *pad = NULL;
  GstBuffer *inbuf;
  GstBuffer *outbuf = NULL;

  GST_DEBUG_OBJECT (demux,
      "Handling generic container essence element of size %u"
      " at offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  if (!demux->current_package) {
    GST_ERROR_OBJECT (demux, "No package selected yet");
    return GST_FLOW_ERROR;
  }

  if (!demux->src || demux->src->len == 0) {
    GST_ERROR_OBJECT (demux, "No streams created yet");
    return GST_FLOW_ERROR;
  }

  track_number = GST_READ_UINT32_BE (&key->u[12]);

  for (i = 0; i < demux->src->len; i++) {
    GstMXFPad *p = g_ptr_array_index (demux->src, i);

    if (p->source_track->track_number == track_number ||
        (p->source_track->track_number == 0 &&
            demux->src->len == 1 &&
            demux->current_package->n_essence_tracks == 1)) {
      pad = p;
      break;
    }
  }

  if (!pad) {
    GST_WARNING_OBJECT (demux, "No corresponding pad found");
    return GST_FLOW_OK;
  }

  /* TODO: Use a better start value */
  if (pad->need_segment) {
    gst_pad_push_event (GST_PAD_CAST (pad),
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
    pad->need_segment = FALSE;
  }

  /* Create subbuffer to be able to change metadata */
  inbuf = gst_buffer_create_sub (buffer, 0, GST_BUFFER_SIZE (buffer));

  GST_BUFFER_TIMESTAMP (inbuf) =
      gst_util_uint64_scale (pad->essence_element_count +
      pad->material_track->origin,
      GST_SECOND * pad->material_track->edit_rate.d,
      pad->material_track->edit_rate.n);
  GST_BUFFER_DURATION (inbuf) =
      gst_util_uint64_scale (GST_SECOND, pad->material_track->edit_rate.d,
      pad->material_track->edit_rate.n);
  GST_BUFFER_OFFSET (inbuf) = pad->essence_element_count;
  GST_BUFFER_OFFSET_END (inbuf) = GST_BUFFER_OFFSET_NONE;
  gst_buffer_set_caps (inbuf, pad->caps);

  if (pad->handle_essence_element) {
    /* Takes ownership of inbuf */
    ret =
        pad->handle_essence_element (key, inbuf, pad->caps, pad->source_package,
        pad->source_track, pad->component, pad->mapping_data, &outbuf);
    inbuf = NULL;
  } else {
    outbuf = inbuf;
    inbuf = NULL;
    ret = GST_FLOW_OK;
  }

  pad->essence_element_count++;

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (demux, "Failed to handle essence element");
    if (outbuf) {
      gst_buffer_unref (outbuf);
      outbuf = NULL;
    }
  }

  if (outbuf) {
    ret = gst_pad_push (GST_PAD_CAST (pad), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_mxf_demux_handle_random_index_pack (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (demux,
      "Handling random index pack of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  /* TODO: Parse this */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_handle_index_table_segment (GstMXFDemux * demux,
    const MXFUL * key, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (demux,
      "Handling index table segment of size %u at offset %"
      G_GUINT64_FORMAT, GST_BUFFER_SIZE (buffer), demux->offset);

  /* TODO: Parse this */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_demux_pull_klv_packet (GstMXFDemux * demux, guint64 offset, MXFUL * key,
    GstBuffer ** outbuf, guint * read)
{
  GstBuffer *buffer = NULL;
  const guint8 *data;
  guint64 data_offset = 0;
  guint length;
  gchar key_str[48];
  GstFlowReturn ret = GST_FLOW_OK;

  memset (key, 0, sizeof (MXFUL));

  /* Pull 16 byte key and first byte of BER encoded length */
  if ((ret =
          gst_mxf_demux_pull_range (demux, offset, 17, &buffer)) != GST_FLOW_OK)
    goto beach;

  data = GST_BUFFER_DATA (buffer);

  if (!mxf_is_mxf_packet ((const MXFUL *) data)) {
    GST_ERROR_OBJECT (demux, "Not an MXF packet, skipping. Key: %s",
        mxf_ul_to_string ((const MXFUL *) data, key_str));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  memcpy (key, GST_BUFFER_DATA (buffer), 16);

  /* Decode BER encoded packet length */
  if ((data[16] & 0x80) == 0) {
    length = data[16];
    data_offset = 17;
  } else {
    guint slen = data[16] & 0x7f;

    data_offset = 16 + 1 + slen;

    gst_buffer_unref (buffer);

    /* Must be at most 8 according to SMPTE-379M 5.3.4 and
     * GStreamer buffers can only have a 4 bytes length */
    if (slen > 8) {
      GST_ERROR_OBJECT (demux, "Invalid KLV packet length: %u", slen);
      ret = GST_FLOW_ERROR;
      goto beach;
    } else if (slen > 4) {
      GST_ERROR_OBJECT (demux, "Unsupported KLV packet length: %u", slen);
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
gst_mxf_demux_parse_footer_metadata (GstMXFDemux * demux)
{
  MXFPartitionPack partition;
  MXFPrimerPack primer;
  guint64 offset, old_offset = demux->offset;
  MXFUL key;
  GstBuffer *buffer = NULL;
  guint read = 0;
  GstFlowReturn ret = GST_FLOW_OK;

  memcpy (&partition, &demux->partition, sizeof (MXFPartitionPack));
  memcpy (&primer, &demux->primer, sizeof (MXFPrimerPack));
  memset (&demux->partition, 0, sizeof (MXFPartitionPack));
  memset (&demux->primer, 0, sizeof (MXFPrimerPack));

  gst_mxf_demux_reset_metadata (demux);
  offset =
      demux->header_partition_pack_offset + demux->footer_partition_pack_offset;

next_try:
  mxf_partition_pack_reset (&demux->partition);
  mxf_primer_pack_reset (&demux->primer);

  ret = gst_mxf_demux_pull_klv_packet (demux, offset, &key, &buffer, &read);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto out;

  if (!mxf_is_partition_pack (&key))
    goto out;

  if (!mxf_partition_pack_parse (&key, &demux->partition,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer)))
    goto out;

  offset += read;
  gst_buffer_unref (buffer);
  buffer = NULL;

  if (demux->partition.header_byte_count == 0) {
    if (demux->partition.prev_partition == 0
        || demux->partition.this_partition == 0)
      goto out;

    offset =
        demux->header_partition_pack_offset + demux->partition.prev_partition;
    goto next_try;
  }

  while (TRUE) {
    ret = gst_mxf_demux_pull_klv_packet (demux, offset, &key, &buffer, &read);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      offset =
          demux->header_partition_pack_offset + demux->partition.prev_partition;
      goto next_try;
    }

    if (mxf_is_fill (&key)) {
      offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    } else if (mxf_is_primer_pack (&key)) {
      if (!mxf_primer_pack_parse (&key, &demux->primer,
              GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer))) {
        offset += read;
        gst_buffer_unref (buffer);
        buffer = NULL;
        offset =
            demux->header_partition_pack_offset +
            demux->partition.prev_partition;
        goto next_try;
      }
      offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
      break;
    } else {
      gst_buffer_unref (buffer);
      buffer = NULL;
      offset =
          demux->header_partition_pack_offset + demux->partition.prev_partition;
      goto next_try;
    }
  }

  /* parse metadata */
  while (TRUE) {
    ret = gst_mxf_demux_pull_klv_packet (demux, offset, &key, &buffer, &read);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      offset =
          demux->header_partition_pack_offset + demux->partition.prev_partition;
      goto next_try;
    }

    if (mxf_is_metadata (&key)) {
      ret = gst_mxf_demux_handle_metadata (demux, &key, buffer);
      offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;

      if (G_UNLIKELY (ret != GST_FLOW_OK)) {
        gst_mxf_demux_reset_metadata (demux);
        offset =
            demux->header_partition_pack_offset +
            demux->partition.prev_partition;
        goto next_try;
      }
    } else if (mxf_is_fill (&key)) {
      offset += read;
      gst_buffer_unref (buffer);
      buffer = NULL;
    } else {
      break;
    }
  }

  /* resolve references etc */

  if (gst_mxf_demux_handle_header_metadata_resolve_references (demux) !=
      GST_FLOW_OK
      || gst_mxf_demux_handle_header_metadata_update_streams (demux) !=
      GST_FLOW_OK) {
    offset =
        demux->header_partition_pack_offset + demux->partition.prev_partition;
    goto next_try;
  }

  demux->final_metadata = TRUE;

out:
  if (buffer)
    gst_buffer_unref (buffer);

  mxf_partition_pack_reset (&demux->partition);
  mxf_primer_pack_reset (&demux->primer);
  memcpy (&demux->partition, &partition, sizeof (MXFPartitionPack));
  memcpy (&demux->primer, &primer, sizeof (MXFPrimerPack));

  demux->offset = old_offset;
}

static GstFlowReturn
gst_mxf_demux_handle_klv_packet (GstMXFDemux * demux, const MXFUL * key,
    GstBuffer * buffer)
{
  gchar key_str[48];
  GstFlowReturn ret = GST_FLOW_OK;

  /* In pull mode try to get the last metadata */
  if (!demux->final_metadata && demux->random_access && demux->partition.valid
      && demux->partition.type == MXF_PARTITION_PACK_HEADER
      && (!demux->partition.closed || !demux->partition.complete)
      && demux->footer_partition_pack_offset != 0) {
    GST_DEBUG_OBJECT (demux,
        "Open or incomplete header partition, trying to get final metadata from the last partitions");
    gst_mxf_demux_parse_footer_metadata (demux);
  }

  /* TODO: - Pull random index pack from footer partition?
   *       - Pull all partitions for parsing all index segments and having a complete index
   *         as first thing. This also will make it possible to use the latest header
   *         metadata if it's not in the footer partition
   */

  if (demux->update_metadata
      && !mxf_timestamp_is_unknown (&demux->preface.last_modified_date)
      && !mxf_is_metadata (key)
      && !mxf_is_fill (key)) {
    if ((ret =
            gst_mxf_demux_handle_header_metadata_resolve_references (demux)) !=
        GST_FLOW_OK)
      goto beach;
    if ((ret =
            gst_mxf_demux_handle_header_metadata_update_streams (demux)) !=
        GST_FLOW_OK)
      goto beach;
  }

  if (mxf_is_partition_pack (key)) {
    ret = gst_mxf_demux_handle_partition_pack (demux, key, buffer);
  } else if (mxf_is_primer_pack (key)) {
    ret = gst_mxf_demux_handle_primer_pack (demux, key, buffer);
  } else if (mxf_is_metadata (key)) {
    ret = gst_mxf_demux_handle_metadata (demux, key, buffer);
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
        demux->header_partition_pack_offset = demux->offset;
        gst_buffer_unref (buffer);
        break;
      }

      demux->offset++;
      gst_buffer_unref (buffer);
    }
  }

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pause;

  if (G_UNLIKELY (demux->run_in == -1)) {
    GST_ERROR_OBJECT (demux, "No valid header partition pack found");
    ret = GST_FLOW_ERROR;
    goto pause;
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
  guint length = 0;
  guint64 offset = 0;
  GstBuffer *buffer = NULL;
  gchar key_str[48];

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
          demux->header_partition_pack_offset = demux->offset;
          break;
        }
        gst_adapter_flush (demux->adapter, 1);
      }
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

    if (!mxf_is_mxf_packet ((const MXFUL *) data)) {
      GST_ERROR_OBJECT (demux, "Not an MXF packet, skipping. Key: %s",
          mxf_ul_to_string ((const MXFUL *) data, key_str));
      ret = GST_FLOW_ERROR;
      break;
    }

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
      } else if (slen > 4) {
        GST_ERROR_OBJECT (demux, "Unsupported KLV packet length: %u", slen);
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
  GstMXFPad *mxfpad = GST_MXF_PAD (pad);

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

      pos = mxfpad->essence_element_count;
      if (format == GST_FORMAT_TIME) {
        if (!mxfpad->material_track || mxfpad->material_track->edit_rate.n == 0
            || mxfpad->material_track->edit_rate.d == 0)
          goto error;

        pos =
            gst_util_uint64_scale (pos + mxfpad->material_track->origin,
            GST_SECOND * mxfpad->material_track->edit_rate.d,
            mxfpad->material_track->edit_rate.n);
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

      if (!mxfpad->material_track || !mxfpad->material_track->sequence)
        goto error;

      duration = mxfpad->material_track->sequence->duration;
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

  if (demux->src) {
    g_ptr_array_foreach (demux->src, (GFunc) gst_mxf_demux_remove_pad, demux);
    g_ptr_array_foreach (demux->src, (GFunc) gst_object_unref, NULL);
    g_ptr_array_free (demux->src, TRUE);
    demux->src = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mxf_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mxf_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&data_src_template));
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

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_mxf_demux_finalize);

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
