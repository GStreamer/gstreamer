/* GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-mxfmux
 *
 * mxfmux muxes different streams into an MXF file.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=/path/to/audio ! decodebin2 ! queue ! mxfmux name=m ! filesink location=file.mxf   filesrc location=/path/to/video ! decodebin2 ! queue ! m.
 * ]| This pipeline muxes an audio and video file into a single MXF file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "mxfmux.h"

#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

GST_DEBUG_CATEGORY_STATIC (mxfmux_debug);
#define GST_CAT_DEFAULT mxfmux_debug

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/mxf")
    );

enum
{
  PROP_0
};

#define gst_mxf_mux_parent_class parent_class
G_DEFINE_TYPE (GstMXFMux, gst_mxf_mux, GST_TYPE_ELEMENT);

static void gst_mxf_mux_finalize (GObject * object);
static void gst_mxf_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_mxf_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_mxf_mux_collected (GstCollectPads * pads,
    gpointer user_data);

static gboolean gst_mxf_mux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_mxf_mux_handle_sink_event (GstCollectPads * pads,
    GstCollectData * data, GstEvent * event, gpointer user_data);
static GstPad *gst_mxf_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_mxf_mux_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn
gst_mxf_mux_change_state (GstElement * element, GstStateChange transition);

static void gst_mxf_mux_reset (GstMXFMux * mux);

static GstFlowReturn
gst_mxf_mux_push (GstMXFMux * mux, GstBuffer * buf)
{
  guint size = gst_buffer_get_size (buf);
  GstFlowReturn ret;

  ret = gst_pad_push (mux->srcpad, buf);
  mux->offset += size;

  return ret;
}

static void
gst_mxf_mux_class_init (GstMXFMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  const GstPadTemplate **p;

  GST_DEBUG_CATEGORY_INIT (mxfmux_debug, "mxfmux", 0, "MXF muxer");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_mxf_mux_finalize;
  gobject_class->set_property = gst_mxf_mux_set_property;
  gobject_class->get_property = gst_mxf_mux_get_property;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_mxf_mux_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_mxf_mux_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_mxf_mux_release_pad);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_templ));

  p = mxf_essence_element_writer_get_pad_templates ();
  while (p && *p) {
    gst_element_class_add_pad_template (gstelement_class,
        (GstPadTemplate *) gst_object_ref (GST_OBJECT (*p)));
    p++;
  }

  gst_element_class_set_static_metadata (gstelement_class, "MXF muxer",
      "Codec/Muxer",
      "Muxes video/audio streams into a MXF stream",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_mxf_mux_init (GstMXFMux * mux)
{
  GstCaps *caps;

  mux->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (mux->srcpad, gst_mxf_mux_handle_src_event);
  caps = gst_caps_new_empty_simple ("application/mxf");
  gst_pad_set_caps (mux->srcpad, caps);
  gst_caps_unref (caps);
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_event_function (mux->collect,
      GST_DEBUG_FUNCPTR (gst_mxf_mux_handle_sink_event), mux);
  gst_collect_pads_set_function (mux->collect,
      GST_DEBUG_FUNCPTR (gst_mxf_mux_collected), mux);

  gst_mxf_mux_reset (mux);
}

static void
gst_mxf_mux_finalize (GObject * object)
{
  GstMXFMux *mux = GST_MXF_MUX (object);

  gst_mxf_mux_reset (mux);

  if (mux->metadata) {
    g_hash_table_destroy (mux->metadata);
    mux->metadata = NULL;
    g_list_free (mux->metadata_list);
    mux->metadata_list = NULL;
  }

  gst_object_unref (mux->collect);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mxf_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  //GstMXFMux *mux = GST_MXF_MUX (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mxf_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  //GstMXFMux *mux = GST_MXF_MUX (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mxf_mux_reset (GstMXFMux * mux)
{
  GSList *sl;

  while ((sl = mux->collect->data) != NULL) {
    GstMXFMuxPad *cpad = (GstMXFMuxPad *) sl->data;

    g_object_unref (cpad->adapter);
    g_free (cpad->mapping_data);

    gst_collect_pads_remove_pad (mux->collect, cpad->collect.pad);
  }

  mux->state = GST_MXF_MUX_STATE_HEADER;
  mux->n_pads = 0;

  if (mux->metadata) {
    g_hash_table_destroy (mux->metadata);
    mux->preface = NULL;
    g_list_free (mux->metadata_list);
    mux->metadata_list = NULL;
  }
  mux->metadata = mxf_metadata_hash_table_new ();

  mxf_partition_pack_reset (&mux->partition);
  mxf_primer_pack_reset (&mux->primer);
  memset (&mux->min_edit_rate, 0, sizeof (MXFFraction));
  mux->last_gc_timestamp = 0;
  mux->last_gc_position = 0;
  mux->offset = 0;
}

static gboolean
gst_mxf_mux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstEventType type;

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      /* disable seeking for now */
      return FALSE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_mxf_mux_event_caps (GstPad * pad, GstCaps * caps)
{
  GstMXFMux *mux = GST_MXF_MUX (gst_pad_get_parent (pad));
  GstMXFMuxPad *cpad = (GstMXFMuxPad *) gst_pad_get_element_private (pad);
  gboolean ret = TRUE;
  MXFUUID d_instance_uid = { {0,} };
  MXFMetadataFileDescriptor *old_descriptor = cpad->descriptor;
  GList *l;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  if (old_descriptor) {
    memcpy (&d_instance_uid, &MXF_METADATA_BASE (old_descriptor)->instance_uid,
        16);
    cpad->descriptor = NULL;
    g_free (cpad->mapping_data);
    cpad->mapping_data = NULL;
  }

  cpad->descriptor =
      cpad->writer->get_descriptor (GST_PAD_PAD_TEMPLATE (pad), caps,
      &cpad->write_func, &cpad->mapping_data);

  if (!cpad->descriptor) {
    GST_ERROR_OBJECT (mux,
        "Couldn't get descriptor for pad '%s' with caps %" GST_PTR_FORMAT,
        GST_PAD_NAME (pad), caps);
    gst_object_unref (mux);
    return FALSE;
  }

  if (mxf_uuid_is_zero (&d_instance_uid))
    mxf_uuid_init (&d_instance_uid, mux->metadata);

  memcpy (&MXF_METADATA_BASE (cpad->descriptor)->instance_uid, &d_instance_uid,
      16);

  if (old_descriptor) {
    for (l = mux->metadata_list; l; l = l->next) {
      MXFMetadataBase *tmp = l->data;

      if (mxf_uuid_is_equal (&d_instance_uid, &tmp->instance_uid)) {
        l->data = cpad->descriptor;
        break;
      }
    }
  } else {
    mux->metadata_list = g_list_prepend (mux->metadata_list, cpad->descriptor);
  }

  g_hash_table_replace (mux->metadata,
      &MXF_METADATA_BASE (cpad->descriptor)->instance_uid, cpad->descriptor);

  if (old_descriptor) {
    if (mux->preface && mux->preface->content_storage &&
        mux->preface->content_storage->packages) {
      guint i, j;

      for (i = 0; i < mux->preface->content_storage->n_packages; i++) {
        MXFMetadataSourcePackage *package;

        if (!MXF_IS_METADATA_SOURCE_PACKAGE (mux->preface->
                content_storage->packages[i]))
          continue;

        package =
            MXF_METADATA_SOURCE_PACKAGE (mux->preface->
            content_storage->packages[i]);

        if (!package->descriptor)
          continue;

        if (MXF_IS_METADATA_MULTIPLE_DESCRIPTOR (package->descriptor)) {
          MXFMetadataMultipleDescriptor *tmp =
              MXF_METADATA_MULTIPLE_DESCRIPTOR (package->descriptor);

          for (j = 0; j < tmp->n_sub_descriptors; j++) {
            if (tmp->sub_descriptors[j] ==
                MXF_METADATA_GENERIC_DESCRIPTOR (old_descriptor)) {
              tmp->sub_descriptors[j] =
                  MXF_METADATA_GENERIC_DESCRIPTOR (cpad->descriptor);
              memcpy (&tmp->sub_descriptors_uids[j], &d_instance_uid, 16);
            }
          }
        } else if (package->descriptor ==
            MXF_METADATA_GENERIC_DESCRIPTOR (old_descriptor)) {
          package->descriptor =
              MXF_METADATA_GENERIC_DESCRIPTOR (cpad->descriptor);
          memcpy (&package->descriptor_uid, &d_instance_uid, 16);
        }
      }
    }
  }

  gst_object_unref (mux);

  return ret;
}

static gboolean
gst_mxf_mux_handle_sink_event (GstCollectPads * pads, GstCollectData * data,
    GstEvent * event, gpointer user_data)
{
  GstCaps *caps;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      /* TODO: do something with the tags */
      break;
    case GST_EVENT_SEGMENT:
      /* We don't support SEGMENT events */
      ret = FALSE;
      gst_event_unref (event);
      break;
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);

      gst_mxf_mux_event_caps (data->pad, caps);

      gst_caps_unref (caps);
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  if (ret)
    ret = gst_collect_pads_event_default (pads, data, event, FALSE);

  return ret;
}

static char *
gst_mxf_mux_create_pad_name (GstPadTemplate * templ, guint id)
{
  GString *string;

  string = g_string_new (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));
  g_string_truncate (string, string->len - 2);
  g_string_append_printf (string, "%u", id);

  return g_string_free (string, FALSE);
}

static GstPad *
gst_mxf_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * pad_name, const GstCaps * caps)
{
  GstMXFMux *mux = GST_MXF_MUX (element);
  GstMXFMuxPad *cpad;
  GstPad *pad = NULL;
  guint pad_number;
  gchar *name = NULL;
  const MXFEssenceElementWriter *writer;

  if (mux->state != GST_MXF_MUX_STATE_HEADER) {
    GST_WARNING_OBJECT (mux, "Can't request pads after writing header");
    return NULL;
  }

  writer = mxf_essence_element_writer_find (templ);
  if (!writer) {
    GST_ERROR_OBJECT (mux, "Not our template");
    return NULL;
  }
  pad_number = g_atomic_int_add ((gint *) & mux->n_pads, 1);
  name = gst_mxf_mux_create_pad_name (templ, pad_number);

  GST_DEBUG_OBJECT (mux, "Creating pad '%s'", name);
  pad = gst_pad_new_from_template (templ, name);
  g_free (name);
  cpad = (GstMXFMuxPad *)
      gst_collect_pads_add_pad (mux->collect, pad, sizeof (GstMXFMuxPad), NULL,
      TRUE);
  cpad->last_timestamp = 0;
  cpad->adapter = gst_adapter_new ();
  cpad->writer = writer;

  gst_pad_use_fixed_caps (pad);
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (element, pad);

  return pad;
}

static void
gst_mxf_mux_release_pad (GstElement * element, GstPad * pad)
{
  /*GstMXFMux *mux = GST_MXF_MUX (GST_PAD_PARENT (pad));
     GstMXFMuxPad *cpad = (GstMXFMuxPad *) gst_pad_get_element_private (pad);

     g_object_unref (cpad->adapter);
     g_free (cpad->mapping_data);

     gst_collect_pads_remove_pad (mux->collect, pad);
     gst_element_remove_pad (element, pad); */
}

static GstFlowReturn
gst_mxf_mux_create_metadata (GstMXFMux * mux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GSList *l;
  GArray *tmp;

  GST_DEBUG_OBJECT (mux, "Creating MXF metadata");

  for (l = mux->collect->data; l; l = l->next) {
    GstMXFMuxPad *cpad = l->data;
    GstCaps *caps;

    if (!cpad || !cpad->descriptor)
      return GST_FLOW_ERROR;

    caps = gst_pad_get_current_caps (cpad->collect.pad);
    if (!caps)
      return GST_FLOW_ERROR;

    if (cpad->writer->update_descriptor)
      cpad->writer->update_descriptor (cpad->descriptor,
          caps, cpad->mapping_data, cpad->collect.buffer);
    gst_caps_unref (caps);
  }

  /* Preface */
  mux->preface =
      (MXFMetadataPreface *) g_object_new (MXF_TYPE_METADATA_PREFACE, NULL);
  mxf_uuid_init (&MXF_METADATA_BASE (mux->preface)->instance_uid,
      mux->metadata);
  g_hash_table_insert (mux->metadata,
      &MXF_METADATA_BASE (mux->preface)->instance_uid, mux->preface);
  mux->metadata_list = g_list_prepend (mux->metadata_list, mux->preface);

  mxf_timestamp_set_now (&mux->preface->last_modified_date);
  mux->preface->version = 258;
  mux->preface->object_model_version = 1;

  mxf_op_set_generalized (&mux->preface->operational_pattern, MXF_OP_1a, TRUE,
      TRUE, FALSE);

  tmp = g_array_new (FALSE, FALSE, sizeof (MXFUL));
  for (l = mux->collect->data; l; l = l->next) {
    GstMXFMuxPad *cpad = l->data;
    guint i;
    gboolean found = FALSE;

    if (!cpad || !cpad->descriptor ||
        mxf_ul_is_zero (&cpad->descriptor->essence_container))
      return GST_FLOW_ERROR;

    for (i = 0; i < tmp->len; i++) {
      if (mxf_ul_is_equal (&cpad->descriptor->essence_container,
              &g_array_index (tmp, MXFUL, i))) {
        found = TRUE;
        break;
      }
    }

    if (found)
      continue;

    g_array_append_val (tmp, cpad->descriptor->essence_container);
  }
  mux->preface->n_essence_containers = tmp->len;
  mux->preface->essence_containers = (MXFUL *) g_array_free (tmp, FALSE);

  /* This will later be used as UID for the material package */
  mxf_uuid_init (&mux->preface->primary_package_uid, mux->metadata);

  /* Identifications */
  {
    MXFMetadataIdentification *identification;
    static const guint8 gst_uid[] = {
      0xe5, 0xde, 0xcd, 0x04, 0x24, 0x90, 0x69, 0x18,
      0x8a, 0xc9, 0xb5, 0xd7, 0x02, 0x58, 0x46, 0x78
    };
    guint major, minor, micro, nano;

    mux->preface->n_identifications = 1;
    mux->preface->identifications = g_new0 (MXFMetadataIdentification *, 1);
    identification = mux->preface->identifications[0] =
        (MXFMetadataIdentification *)
        g_object_new (MXF_TYPE_METADATA_IDENTIFICATION, NULL);

    mxf_uuid_init (&MXF_METADATA_BASE (identification)->instance_uid,
        mux->metadata);
    g_hash_table_insert (mux->metadata,
        &MXF_METADATA_BASE (identification)->instance_uid, identification);
    mux->metadata_list = g_list_prepend (mux->metadata_list, identification);

    mxf_uuid_init (&identification->this_generation_uid, NULL);

    identification->company_name = g_strdup ("GStreamer");
    identification->product_name = g_strdup ("GStreamer Multimedia Framework");

    gst_version (&major, &minor, &micro, &nano);
    identification->product_version.major = major;
    identification->product_version.minor = minor;
    identification->product_version.patch = micro;
    identification->product_version.build = nano;
    identification->product_version.release =
        (nano == 0) ? 1 : (nano == 1) ? 2 : 4;

    identification->version_string =
        g_strdup_printf ("%u.%u.%u.%u", major, minor, micro, nano);
    memcpy (&identification->product_uid, &gst_uid, 16);

    memcpy (&identification->modification_date,
        &mux->preface->last_modified_date, sizeof (MXFTimestamp));
    memcpy (&identification->toolkit_version, &identification->product_version,
        sizeof (MXFProductVersion));

#ifdef HAVE_SYS_UTSNAME_H
    {
      struct utsname sys_details;

      if (uname (&sys_details) == 0) {
        identification->platform = g_strdup_printf ("%s %s %s",
            sys_details.sysname, sys_details.release, sys_details.machine);
      }
    }
#endif

#if defined(G_OS_WIN32)
    if (identification->platform == NULL)
      identification->platform = g_strdup ("Microsoft Windows");
#elif defined(G_OS_BEOS)
    if (identification->platform == NULL)
      identification->platform = g_strdup ("BEOS");
#elif defined(G_OS_UNIX)
    if (identification->platform == NULL)
      identification->platform = g_strdup ("Unix");
#endif
  }

  /* Content storage */
  {
    MXFMetadataContentStorage *cstorage;
    guint i;

    cstorage = mux->preface->content_storage = (MXFMetadataContentStorage *)
        g_object_new (MXF_TYPE_METADATA_CONTENT_STORAGE, NULL);
    mxf_uuid_init (&MXF_METADATA_BASE (cstorage)->instance_uid, mux->metadata);
    g_hash_table_insert (mux->metadata,
        &MXF_METADATA_BASE (cstorage)->instance_uid, cstorage);
    mux->metadata_list = g_list_prepend (mux->metadata_list, cstorage);

    cstorage->n_packages = 2;
    cstorage->packages = g_new0 (MXFMetadataGenericPackage *, 2);

    /* Source package */
    {
      MXFMetadataSourcePackage *p;

      cstorage->packages[1] = (MXFMetadataGenericPackage *)
          g_object_new (MXF_TYPE_METADATA_SOURCE_PACKAGE, NULL);
      mxf_uuid_init (&MXF_METADATA_BASE (cstorage->packages[1])->instance_uid,
          mux->metadata);
      g_hash_table_insert (mux->metadata,
          &MXF_METADATA_BASE (cstorage->packages[1])->instance_uid,
          cstorage->packages[1]);
      mux->metadata_list =
          g_list_prepend (mux->metadata_list, cstorage->packages[1]);
      p = (MXFMetadataSourcePackage *) cstorage->packages[1];

      mxf_umid_init (&p->parent.package_uid);
      p->parent.name = g_strdup ("Source package");
      memcpy (&p->parent.package_creation_date,
          &mux->preface->last_modified_date, sizeof (MXFTimestamp));
      memcpy (&p->parent.package_modified_date,
          &mux->preface->last_modified_date, sizeof (MXFTimestamp));

      p->parent.n_tracks = g_slist_length (mux->collect->data);
      p->parent.tracks = g_new0 (MXFMetadataTrack *, p->parent.n_tracks);

      if (p->parent.n_tracks > 1) {
        MXFMetadataMultipleDescriptor *d;

        p->descriptor = (MXFMetadataGenericDescriptor *)
            g_object_new (MXF_TYPE_METADATA_MULTIPLE_DESCRIPTOR, NULL);
        d = (MXFMetadataMultipleDescriptor *) p->descriptor;
        d->n_sub_descriptors = p->parent.n_tracks;
        d->sub_descriptors =
            g_new0 (MXFMetadataGenericDescriptor *, p->parent.n_tracks);

        mxf_uuid_init (&MXF_METADATA_BASE (d)->instance_uid, mux->metadata);
        g_hash_table_insert (mux->metadata,
            &MXF_METADATA_BASE (d)->instance_uid, d);
        mux->metadata_list = g_list_prepend (mux->metadata_list, d);
      }

      /* Tracks */
      {
        guint n = 0;

        /* Essence tracks */
        for (l = mux->collect->data; l; l = l->next) {
          GstMXFMuxPad *cpad = l->data;
          MXFMetadataTimelineTrack *track;
          MXFMetadataSequence *sequence;
          MXFMetadataSourceClip *clip;
          GstCaps *caps;

          p->parent.tracks[n] = (MXFMetadataTrack *)
              g_object_new (MXF_TYPE_METADATA_TIMELINE_TRACK, NULL);
          track = (MXFMetadataTimelineTrack *) p->parent.tracks[n];
          mxf_uuid_init (&MXF_METADATA_BASE (track)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (track)->instance_uid, track);
          mux->metadata_list = g_list_prepend (mux->metadata_list, track);

          caps = gst_pad_get_current_caps (cpad->collect.pad);
          track->parent.track_id = n + 1;
          track->parent.track_number =
              cpad->writer->get_track_number_template (cpad->descriptor,
              caps, cpad->mapping_data);

          cpad->writer->get_edit_rate (cpad->descriptor,
              caps, cpad->mapping_data,
              cpad->collect.buffer, p, track, &track->edit_rate);
          gst_caps_unref (caps);

          sequence = track->parent.sequence = (MXFMetadataSequence *)
              g_object_new (MXF_TYPE_METADATA_SEQUENCE, NULL);
          mxf_uuid_init (&MXF_METADATA_BASE (sequence)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (sequence)->instance_uid, sequence);
          mux->metadata_list = g_list_prepend (mux->metadata_list, sequence);

          memcpy (&sequence->data_definition, &cpad->writer->data_definition,
              16);

          sequence->n_structural_components = 1;
          sequence->structural_components =
              g_new0 (MXFMetadataStructuralComponent *, 1);

          clip = (MXFMetadataSourceClip *)
              g_object_new (MXF_TYPE_METADATA_SOURCE_CLIP, NULL);
          sequence->structural_components[0] =
              (MXFMetadataStructuralComponent *) clip;
          mxf_uuid_init (&MXF_METADATA_BASE (clip)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (clip)->instance_uid, clip);
          mux->metadata_list = g_list_prepend (mux->metadata_list, clip);

          memcpy (&clip->parent.data_definition, &sequence->data_definition,
              16);
          clip->start_position = 0;

          cpad->source_package = p;
          cpad->source_track = track;
          cpad->descriptor->linked_track_id = n + 1;
          if (p->parent.n_tracks == 1) {
            p->descriptor = (MXFMetadataGenericDescriptor *) cpad->descriptor;
          } else {
            MXF_METADATA_MULTIPLE_DESCRIPTOR (p->
                descriptor)->sub_descriptors[n] =
                (MXFMetadataGenericDescriptor *) cpad->descriptor;
          }

          n++;
        }
      }
    }

    /* Material package */
    {
      MXFMetadataMaterialPackage *p;
      MXFFraction min_edit_rate = { 0, 0 };
      gdouble min_edit_rate_d = G_MAXDOUBLE;

      cstorage->packages[0] = (MXFMetadataGenericPackage *)
          g_object_new (MXF_TYPE_METADATA_MATERIAL_PACKAGE, NULL);
      memcpy (&MXF_METADATA_BASE (cstorage->packages[0])->instance_uid,
          &mux->preface->primary_package_uid, 16);
      g_hash_table_insert (mux->metadata,
          &MXF_METADATA_BASE (cstorage->packages[0])->instance_uid,
          cstorage->packages[0]);
      mux->metadata_list =
          g_list_prepend (mux->metadata_list, cstorage->packages[0]);
      p = (MXFMetadataMaterialPackage *) cstorage->packages[0];

      mxf_umid_init (&p->package_uid);
      p->name = g_strdup ("Material package");
      memcpy (&p->package_creation_date, &mux->preface->last_modified_date,
          sizeof (MXFTimestamp));
      memcpy (&p->package_modified_date, &mux->preface->last_modified_date,
          sizeof (MXFTimestamp));

      p->n_tracks = g_slist_length (mux->collect->data) + 1;
      p->tracks = g_new0 (MXFMetadataTrack *, p->n_tracks);

      /* Tracks */
      {
        guint n;

        n = 1;
        /* Essence tracks */
        for (l = mux->collect->data; l; l = l->next) {
          GstMXFMuxPad *cpad = l->data;
          GstCaps *caps;
          MXFMetadataSourcePackage *source_package;
          MXFMetadataTimelineTrack *track, *source_track;
          MXFMetadataSequence *sequence;
          MXFMetadataSourceClip *clip;

          source_package = MXF_METADATA_SOURCE_PACKAGE (cstorage->packages[1]);
          source_track =
              MXF_METADATA_TIMELINE_TRACK (source_package->parent.tracks[n -
                  1]);

          p->tracks[n] = (MXFMetadataTrack *)
              g_object_new (MXF_TYPE_METADATA_TIMELINE_TRACK, NULL);
          track = (MXFMetadataTimelineTrack *) p->tracks[n];
          mxf_uuid_init (&MXF_METADATA_BASE (track)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (track)->instance_uid, track);
          mux->metadata_list = g_list_prepend (mux->metadata_list, track);

          track->parent.track_id = n + 1;
          track->parent.track_number = 0;

          caps = gst_pad_get_current_caps (cpad->collect.pad);
          cpad->writer->get_edit_rate (cpad->descriptor,
              caps, cpad->mapping_data,
              cpad->collect.buffer, source_package, source_track,
              &track->edit_rate);
          gst_caps_unref (caps);

          if (track->edit_rate.n != source_track->edit_rate.n ||
              track->edit_rate.d != source_track->edit_rate.d) {
            memcpy (&source_track->edit_rate, &track->edit_rate,
                sizeof (MXFFraction));
          }

          if (track->edit_rate.d <= 0 || track->edit_rate.n <= 0) {
            GST_ERROR_OBJECT (mux, "Invalid edit rate");
            return GST_FLOW_ERROR;
          }

          if (min_edit_rate_d >
              ((gdouble) track->edit_rate.n) / ((gdouble) track->edit_rate.d)) {
            min_edit_rate_d =
                ((gdouble) track->edit_rate.n) / ((gdouble) track->edit_rate.d);
            memcpy (&min_edit_rate, &track->edit_rate, sizeof (MXFFraction));
          }

          sequence = track->parent.sequence = (MXFMetadataSequence *)
              g_object_new (MXF_TYPE_METADATA_SEQUENCE, NULL);
          mxf_uuid_init (&MXF_METADATA_BASE (sequence)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (sequence)->instance_uid, sequence);
          mux->metadata_list = g_list_prepend (mux->metadata_list, sequence);

          memcpy (&sequence->data_definition, &cpad->writer->data_definition,
              16);
          sequence->n_structural_components = 1;
          sequence->structural_components =
              g_new0 (MXFMetadataStructuralComponent *, 1);

          clip = (MXFMetadataSourceClip *)
              g_object_new (MXF_TYPE_METADATA_SOURCE_CLIP, NULL);
          sequence->structural_components[0] =
              (MXFMetadataStructuralComponent *) clip;
          mxf_uuid_init (&MXF_METADATA_BASE (clip)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (clip)->instance_uid, clip);
          mux->metadata_list = g_list_prepend (mux->metadata_list, clip);

          memcpy (&clip->parent.data_definition, &sequence->data_definition,
              16);
          clip->start_position = 0;

          memcpy (&clip->source_package_id, &cstorage->packages[1]->package_uid,
              32);
          clip->source_track_id = n;

          n++;
        }

        n = 0;
        /* Timecode track */
        {
          MXFMetadataTimelineTrack *track;
          MXFMetadataSequence *sequence;
          MXFMetadataTimecodeComponent *component;

          p->tracks[n] = (MXFMetadataTrack *)
              g_object_new (MXF_TYPE_METADATA_TIMELINE_TRACK, NULL);
          track = (MXFMetadataTimelineTrack *) p->tracks[n];
          mxf_uuid_init (&MXF_METADATA_BASE (track)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (track)->instance_uid, track);
          mux->metadata_list = g_list_prepend (mux->metadata_list, track);

          track->parent.track_id = n + 1;
          track->parent.track_number = 0;
          track->parent.track_name = g_strdup ("Timecode track");
          /* FIXME: Is this correct? */
          memcpy (&track->edit_rate, &min_edit_rate, sizeof (MXFFraction));

          sequence = track->parent.sequence = (MXFMetadataSequence *)
              g_object_new (MXF_TYPE_METADATA_SEQUENCE, NULL);
          mxf_uuid_init (&MXF_METADATA_BASE (sequence)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (sequence)->instance_uid, sequence);
          mux->metadata_list = g_list_prepend (mux->metadata_list, sequence);

          memcpy (&sequence->data_definition,
              mxf_metadata_track_identifier_get
              (MXF_METADATA_TRACK_TIMECODE_12M_INACTIVE), 16);

          sequence->n_structural_components = 1;
          sequence->structural_components =
              g_new0 (MXFMetadataStructuralComponent *, 1);

          component = (MXFMetadataTimecodeComponent *)
              g_object_new (MXF_TYPE_METADATA_TIMECODE_COMPONENT, NULL);
          sequence->structural_components[0] =
              (MXFMetadataStructuralComponent *) component;
          mxf_uuid_init (&MXF_METADATA_BASE (component)->instance_uid,
              mux->metadata);
          g_hash_table_insert (mux->metadata,
              &MXF_METADATA_BASE (component)->instance_uid, component);
          mux->metadata_list = g_list_prepend (mux->metadata_list, component);

          memcpy (&component->parent.data_definition,
              &sequence->data_definition, 16);

          component->start_timecode = 0;
          if (track->edit_rate.d == 0)
            component->rounded_timecode_base = 1;
          else
            component->rounded_timecode_base =
                (((gdouble) track->edit_rate.n) /
                ((gdouble) track->edit_rate.d) + 0.5);
          /* TODO: drop frame */
        }

        memcpy (&mux->min_edit_rate, &min_edit_rate, sizeof (MXFFraction));
      }
    }

    for (i = 0; i < cstorage->packages[1]->n_tracks; i++) {
      MXFMetadataTrack *track = cstorage->packages[1]->tracks[i];
      guint j;
      guint32 templ;
      guint8 n_type, n;

      if ((track->track_number & 0x00ff00ff) != 0)
        continue;

      templ = track->track_number;
      n_type = 0;

      for (j = 0; j < cstorage->packages[1]->n_tracks; j++) {
        MXFMetadataTrack *tmp = cstorage->packages[1]->tracks[j];

        if (tmp->track_number == templ) {
          n_type++;
        }
      }

      n = 0;
      for (j = 0; j < cstorage->packages[1]->n_tracks; j++) {
        MXFMetadataTrack *tmp = cstorage->packages[1]->tracks[j];

        if (tmp->track_number == templ) {
          n++;
          tmp->track_number |= (n_type << 16) | (n);
        }
      }
    }

    cstorage->n_essence_container_data = 1;
    cstorage->essence_container_data =
        g_new0 (MXFMetadataEssenceContainerData *, 1);
    cstorage->essence_container_data[0] = (MXFMetadataEssenceContainerData *)
        g_object_new (MXF_TYPE_METADATA_ESSENCE_CONTAINER_DATA, NULL);
    mxf_uuid_init (&MXF_METADATA_BASE (cstorage->essence_container_data[0])->
        instance_uid, mux->metadata);
    g_hash_table_insert (mux->metadata,
        &MXF_METADATA_BASE (cstorage->essence_container_data[0])->instance_uid,
        cstorage->essence_container_data[0]);
    mux->metadata_list =
        g_list_prepend (mux->metadata_list,
        cstorage->essence_container_data[0]);

    cstorage->essence_container_data[0]->linked_package =
        MXF_METADATA_SOURCE_PACKAGE (cstorage->packages[1]);
    cstorage->essence_container_data[0]->index_sid = 0;
    cstorage->essence_container_data[0]->body_sid = 1;
  }

  /* Sort descriptors at the correct places */
  {
    GList *l;
    GList *descriptors = NULL;

    for (l = mux->metadata_list; l; l = l->next) {
      MXFMetadataBase *m = l->data;

      if (MXF_IS_METADATA_GENERIC_DESCRIPTOR (m)
          && !MXF_IS_METADATA_MULTIPLE_DESCRIPTOR (m)) {
        descriptors = l;
        l->prev->next = NULL;
        l->prev = NULL;
        break;
      }
    }

    g_assert (descriptors != NULL);

    for (l = mux->metadata_list; l; l = l->next) {
      MXFMetadataBase *m = l->data;
      GList *s;

      if (MXF_IS_METADATA_MULTIPLE_DESCRIPTOR (m) ||
          MXF_IS_METADATA_SOURCE_PACKAGE (m)) {
        s = l->prev;
        l->prev = g_list_last (descriptors);
        s->next = descriptors;
        descriptors->prev = s;
        l->prev->next = l;
        break;
      }
    }
  }

  mux->metadata_list = g_list_reverse (mux->metadata_list);

  return ret;
}

static GstFlowReturn
gst_mxf_mux_init_partition_pack (GstMXFMux * mux)
{
  GSList *l;
  guint i = 0;

  mxf_partition_pack_reset (&mux->partition);
  mux->partition.type = MXF_PARTITION_PACK_HEADER;
  mux->partition.closed = mux->partition.complete = FALSE;
  mux->partition.major_version = 0x0001;
  mux->partition.minor_version = 0x0002;
  mux->partition.kag_size = 0;
  mux->partition.this_partition = 0;
  mux->partition.prev_partition = 0;
  mux->partition.footer_partition = 0;
  mux->partition.header_byte_count = 0;
  mux->partition.index_byte_count = 0;
  mux->partition.index_sid = 0;
  mux->partition.body_offset = 0;
  mux->partition.body_sid = 0;

  memcpy (&mux->partition.operational_pattern,
      &mux->preface->operational_pattern, 16);

  mux->partition.n_essence_containers = g_slist_length (mux->collect->data);
  mux->partition.essence_containers =
      g_new0 (MXFUL, mux->partition.n_essence_containers);

  for (l = mux->collect->data; l; l = l->next) {
    GstMXFMuxPad *cpad = l->data;
    guint j;
    gboolean found = FALSE;

    for (j = 0; j <= i; j++) {
      if (mxf_ul_is_equal (&cpad->descriptor->essence_container,
              &mux->partition.essence_containers[j])) {
        found = TRUE;
        break;
      }
    }

    if (found)
      continue;

    memcpy (&mux->partition.essence_containers[i],
        &cpad->descriptor->essence_container, 16);
    i++;
  }
  mux->partition.n_essence_containers = i;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mxf_mux_write_header_metadata (GstMXFMux * mux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf;
  GList *buffers = NULL;
  GList *l;
  MXFMetadataBase *m;
  guint64 header_byte_count = 0;

  for (l = mux->metadata_list; l; l = l->next) {
    m = l->data;
    buf = mxf_metadata_base_to_buffer (m, &mux->primer);
    header_byte_count += gst_buffer_get_size (buf);
    buffers = g_list_prepend (buffers, buf);
  }

  buffers = g_list_reverse (buffers);
  buf = mxf_primer_pack_to_buffer (&mux->primer);
  header_byte_count += gst_buffer_get_size (buf);
  buffers = g_list_prepend (buffers, buf);

  mux->partition.header_byte_count = header_byte_count;
  buf = mxf_partition_pack_to_buffer (&mux->partition);
  if ((ret = gst_mxf_mux_push (mux, buf)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (mux, "Failed pushing partition: %s",
        gst_flow_get_name (ret));
    g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
    g_list_free (buffers);
    return ret;
  }

  for (l = buffers; l; l = l->next) {
    buf = l->data;
    l->data = NULL;
    if ((ret = gst_mxf_mux_push (mux, buf)) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (mux, "Failed pushing buffer: %s",
          gst_flow_get_name (ret));
      g_list_foreach (l, (GFunc) gst_mini_object_unref, NULL);
      g_list_free (buffers);
      return ret;
    }
  }

  g_list_free (buffers);

  return ret;
}

static const guint8 _gc_essence_element_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x02, 0x01, 0x00,
  0x0d, 0x01, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00
};

static GstFlowReturn
gst_mxf_mux_handle_buffer (GstMXFMux * mux, GstMXFMuxPad * cpad)
{
  GstBuffer *buf = NULL;
  GstBuffer *outbuf = NULL;
  GstBuffer *packet;
  GstMapInfo map;
  GstMapInfo readmap;
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 slen, ber[9];
  gboolean flush = ((cpad->collect.state & GST_COLLECT_PADS_STATE_EOS)
      && !cpad->have_complete_edit_unit && cpad->collect.buffer == NULL);

  if (cpad->have_complete_edit_unit) {
    GST_DEBUG_OBJECT (cpad->collect.pad,
        "Handling remaining buffer for track %u at position %" G_GINT64_FORMAT,
        cpad->source_track->parent.track_id, cpad->pos);
    buf = NULL;
  } else if (!flush) {
    buf = gst_collect_pads_pop (mux->collect, &cpad->collect);
  }

  if (buf) {
    GST_DEBUG_OBJECT (cpad->collect.pad,
        "Handling buffer of size %" G_GSIZE_FORMAT " for track %u at position %"
        G_GINT64_FORMAT, gst_buffer_get_size (buf),
        cpad->source_track->parent.track_id, cpad->pos);
  } else {
    flush = TRUE;
    GST_DEBUG_OBJECT (cpad->collect.pad,
        "Flushing for track %u at position %" G_GINT64_FORMAT,
        cpad->source_track->parent.track_id, cpad->pos);
  }

  ret = cpad->write_func (buf,
      cpad->mapping_data, cpad->adapter, &outbuf, flush);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_CUSTOM_SUCCESS) {
    GST_ERROR_OBJECT (cpad->collect.pad,
        "Failed handling buffer for track %u, reason %s",
        cpad->source_track->parent.track_id, gst_flow_get_name (ret));
    return ret;
  }

  if (ret == GST_FLOW_CUSTOM_SUCCESS) {
    cpad->have_complete_edit_unit = TRUE;
    ret = GST_FLOW_OK;
  } else {
    cpad->have_complete_edit_unit = FALSE;
  }

  buf = outbuf;
  if (buf == NULL)
    return ret;

  gst_buffer_map (buf, &readmap, GST_MAP_READ);
  slen = mxf_ber_encode_size (readmap.size, ber);
  packet = gst_buffer_new_and_alloc (16 + slen + readmap.size);
  gst_buffer_map (packet, &map, GST_MAP_WRITE);
  memcpy (map.data, _gc_essence_element_ul, 16);
  map.data[7] = cpad->descriptor->essence_container.u[7];
  GST_WRITE_UINT32_BE (map.data + 12, cpad->source_track->parent.track_number);
  memcpy (map.data + 16, ber, slen);
  memcpy (map.data + 16 + slen, readmap.data, readmap.size);
  gst_buffer_unmap (buf, &readmap);

  gst_buffer_unref (buf);

  GST_DEBUG_OBJECT (cpad->collect.pad,
      "Pushing buffer of size %" G_GSIZE_FORMAT " for track %u", map.size,
      cpad->source_track->parent.track_id);
  gst_buffer_unmap (packet, &map);

  if ((ret = gst_mxf_mux_push (mux, packet)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (cpad->collect.pad,
        "Failed pushing buffer for track %u, reason %s",
        cpad->source_track->parent.track_id, gst_flow_get_name (ret));
    return ret;
  }

  cpad->pos++;
  cpad->last_timestamp =
      gst_util_uint64_scale (GST_SECOND * cpad->pos,
      cpad->source_track->edit_rate.d, cpad->source_track->edit_rate.n);

  return ret;
}

static GstFlowReturn
gst_mxf_mux_write_body_partition (GstMXFMux * mux)
{
  GstBuffer *buf;

  mux->partition.type = MXF_PARTITION_PACK_BODY;
  mux->partition.this_partition = mux->offset;
  mux->partition.prev_partition = 0;
  mux->partition.footer_partition = 0;
  mux->partition.header_byte_count = 0;
  mux->partition.index_byte_count = 0;
  mux->partition.index_sid = 0;
  mux->partition.body_offset = 0;
  mux->partition.body_sid =
      mux->preface->content_storage->essence_container_data[0]->body_sid;

  buf = mxf_partition_pack_to_buffer (&mux->partition);
  return gst_mxf_mux_push (mux, buf);
}

static GstFlowReturn
gst_mxf_mux_handle_eos (GstMXFMux * mux)
{
  GSList *l;
  gboolean have_data = FALSE;
  GstBuffer *packet;

  do {
    GstMXFMuxPad *best = NULL;

    have_data = FALSE;

    for (l = mux->collect->data; l; l = l->next) {
      GstMXFMuxPad *cpad = l->data;
      GstClockTime next_gc_timestamp =
          gst_util_uint64_scale ((mux->last_gc_position + 1) * GST_SECOND,
          mux->min_edit_rate.d, mux->min_edit_rate.n);

      best = NULL;

      if (cpad->have_complete_edit_unit ||
          gst_adapter_available (cpad->adapter) > 0 || cpad->collect.buffer) {
        have_data = TRUE;
        if (cpad->last_timestamp < next_gc_timestamp) {
          best = cpad;
          break;
        }
      }

      if (have_data && !l->next) {
        mux->last_gc_position++;
        mux->last_gc_timestamp = next_gc_timestamp;
        best = NULL;
        break;
      }
    }

    if (best) {
      gst_mxf_mux_handle_buffer (mux, best);
      have_data = TRUE;
    }
  } while (have_data);

  mux->last_gc_position++;
  mux->last_gc_timestamp =
      gst_util_uint64_scale (mux->last_gc_position * GST_SECOND,
      mux->min_edit_rate.d, mux->min_edit_rate.n);

  /* Update essence track durations */
  for (l = mux->collect->data; l; l = l->next) {
    GstMXFMuxPad *cpad = l->data;
    guint i;

    /* Update durations */
    cpad->source_track->parent.sequence->duration = cpad->pos;
    MXF_METADATA_SOURCE_CLIP (cpad->source_track->parent.
        sequence->structural_components[0])->parent.duration = cpad->pos;
    for (i = 0; i < mux->preface->content_storage->packages[0]->n_tracks; i++) {
      MXFMetadataTimelineTrack *track;

      if (!MXF_IS_METADATA_TIMELINE_TRACK (mux->preface->
              content_storage->packages[0]->tracks[i])
          || !MXF_IS_METADATA_SOURCE_CLIP (mux->preface->
              content_storage->packages[0]->tracks[i]->sequence->
              structural_components[0]))
        continue;

      track =
          MXF_METADATA_TIMELINE_TRACK (mux->preface->
          content_storage->packages[0]->tracks[i]);
      if (MXF_METADATA_SOURCE_CLIP (track->parent.
              sequence->structural_components[0])->source_track_id ==
          cpad->source_track->parent.track_id) {
        track->parent.sequence->structural_components[0]->duration = cpad->pos;
        track->parent.sequence->duration = cpad->pos;
      }
    }
  }

  /* Update timecode track duration */
  {
    MXFMetadataTimelineTrack *track =
        MXF_METADATA_TIMELINE_TRACK (mux->preface->
        content_storage->packages[0]->tracks[0]);
    MXFMetadataSequence *sequence = track->parent.sequence;
    MXFMetadataTimecodeComponent *component =
        MXF_METADATA_TIMECODE_COMPONENT (sequence->structural_components[0]);

    sequence->duration = mux->last_gc_position;
    component->parent.duration = mux->last_gc_position;
  }

  {
    guint64 body_partition = mux->partition.this_partition;
    guint32 body_sid = mux->partition.body_sid;
    guint64 footer_partition = mux->offset;
    GArray *rip;
    GstFlowReturn ret;
    GstSegment segment;
    MXFRandomIndexPackEntry entry;

    mux->partition.type = MXF_PARTITION_PACK_FOOTER;
    mux->partition.closed = TRUE;
    mux->partition.complete = TRUE;
    mux->partition.this_partition = mux->offset;
    mux->partition.prev_partition = body_partition;
    mux->partition.footer_partition = mux->offset;
    mux->partition.header_byte_count = 0;
    mux->partition.index_byte_count = 0;
    mux->partition.index_sid = 0;
    mux->partition.body_offset = 0;
    mux->partition.body_sid = 0;

    gst_mxf_mux_write_header_metadata (mux);

    rip = g_array_sized_new (FALSE, FALSE, sizeof (MXFRandomIndexPackEntry), 3);
    entry.offset = 0;
    entry.body_sid = 0;
    g_array_append_val (rip, entry);
    entry.offset = body_partition;
    entry.body_sid = body_sid;
    g_array_append_val (rip, entry);
    entry.offset = footer_partition;
    entry.body_sid = 0;
    g_array_append_val (rip, entry);

    packet = mxf_random_index_pack_to_buffer (rip);
    if ((ret = gst_mxf_mux_push (mux, packet)) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (mux, "Failed pushing random index pack");
    }
    g_array_free (rip, TRUE);

    /* Rewrite header partition with updated values */
    gst_segment_init (&segment, GST_FORMAT_BYTES);
    if (gst_pad_push_event (mux->srcpad, gst_event_new_segment (&segment))) {
      mux->offset = 0;
      mux->partition.type = MXF_PARTITION_PACK_HEADER;
      mux->partition.closed = TRUE;
      mux->partition.complete = TRUE;
      mux->partition.this_partition = 0;
      mux->partition.prev_partition = footer_partition;
      mux->partition.footer_partition = footer_partition;
      mux->partition.header_byte_count = 0;
      mux->partition.index_byte_count = 0;
      mux->partition.index_sid = 0;
      mux->partition.body_offset = 0;
      mux->partition.body_sid = 0;

      ret = gst_mxf_mux_write_header_metadata (mux);
      if (ret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (mux, "Rewriting header partition failed");
        return ret;
      }
    } else {
      GST_WARNING_OBJECT (mux, "Can't rewrite header partition");
    }
  }

  return GST_FLOW_OK;
}

static gint
_sort_mux_pads (gconstpointer a, gconstpointer b)
{
  const GstMXFMuxPad *pa = a, *pb = b;
  MXFMetadataTrackType ta =
      mxf_metadata_track_identifier_parse (&pa->writer->data_definition);
  MXFMetadataTrackType tb =
      mxf_metadata_track_identifier_parse (&pb->writer->data_definition);

  if (ta != tb)
    return ta - tb;

  return pa->source_track->parent.track_number -
      pa->source_track->parent.track_number;
}

static GstFlowReturn
gst_mxf_mux_collected (GstCollectPads * pads, gpointer user_data)
{
  GstMXFMux *mux = GST_MXF_MUX (user_data);
  GstMXFMuxPad *best = NULL;
  GstFlowReturn ret;
  GstSegment segment;
  GSList *sl;
  gboolean eos = TRUE;

  if (mux->state == GST_MXF_MUX_STATE_ERROR) {
    GST_ERROR_OBJECT (mux, "Had an error before -- returning");
    return GST_FLOW_ERROR;
  } else if (mux->state == GST_MXF_MUX_STATE_EOS) {
    GST_WARNING_OBJECT (mux, "EOS");
    return GST_FLOW_EOS;
  }

  if (mux->state == GST_MXF_MUX_STATE_HEADER) {
    if (mux->collect->data == NULL) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("No input streams configured"));
      ret = GST_FLOW_ERROR;
      goto error;
    }

    gst_segment_init (&segment, GST_FORMAT_BYTES);
    if (gst_pad_push_event (mux->srcpad, gst_event_new_segment (&segment))) {
      if ((ret = gst_mxf_mux_create_metadata (mux)) != GST_FLOW_OK)
        goto error;

      if ((ret = gst_mxf_mux_init_partition_pack (mux)) != GST_FLOW_OK)
        goto error;

      ret = gst_mxf_mux_write_header_metadata (mux);
    } else {
      ret = GST_FLOW_ERROR;
    }

    if (ret != GST_FLOW_OK)
      goto error;

    /* Sort pads, we will always write in that order */
    mux->collect->data = g_slist_sort (mux->collect->data, _sort_mux_pads);

    /* Write body partition */
    ret = gst_mxf_mux_write_body_partition (mux);
    if (ret != GST_FLOW_OK)
      goto error;
    mux->state = GST_MXF_MUX_STATE_DATA;
  }

  g_return_val_if_fail (g_hash_table_size (mux->metadata) > 0, GST_FLOW_ERROR);

  do {
    for (sl = mux->collect->data; sl; sl = sl->next) {
      gboolean pad_eos;
      GstMXFMuxPad *cpad = sl->data;
      GstClockTime next_gc_timestamp =
          gst_util_uint64_scale ((mux->last_gc_position + 1) * GST_SECOND,
          mux->min_edit_rate.d, mux->min_edit_rate.n);

      pad_eos = cpad->collect.state & GST_COLLECT_PADS_STATE_EOS;
      if (!pad_eos)
        eos = FALSE;

      if ((!pad_eos || cpad->have_complete_edit_unit ||
              gst_adapter_available (cpad->adapter) > 0 || cpad->collect.buffer)
          && cpad->last_timestamp < next_gc_timestamp) {
        best = cpad;
        break;
      } else if (!eos && !sl->next) {
        mux->last_gc_position++;
        mux->last_gc_timestamp = next_gc_timestamp;
        eos = FALSE;
        best = NULL;
        break;
      }
    }
  } while (!eos && best == NULL);

  if (!eos && best) {
    ret = gst_mxf_mux_handle_buffer (mux, best);
    if (ret != GST_FLOW_OK)
      goto error;
  } else if (eos) {
    GST_DEBUG_OBJECT (mux, "Handling EOS");

    gst_mxf_mux_handle_eos (mux);
    gst_pad_push_event (mux->srcpad, gst_event_new_eos ());
    mux->state = GST_MXF_MUX_STATE_EOS;
    return GST_FLOW_EOS;
  }

  return GST_FLOW_OK;

error:
  {
    mux->state = GST_MXF_MUX_STATE_ERROR;
    gst_pad_push_event (mux->srcpad, gst_event_new_eos ());
    return ret;
  }
}

static GstStateChangeReturn
gst_mxf_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMXFMux *mux = GST_MXF_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (mux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (mux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mxf_mux_reset (mux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
