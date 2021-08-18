/* GStreamer RTP header extension unit tests
 * Copyright (C) 2020 Matthew Waters <matthew@centricular.com>
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
 * You should have received a copy of the GNU Library General
 * Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include "gst/gstcaps.h"
#include "gst/gstvalue.h"
#include "gst/rtp/gstrtphdrext.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/check.h>
#include <gst/rtp/rtp.h>

/* GstRTPDummyHdrExt shared between payloading and depayloading tests */

#define GST_TYPE_RTP_DUMMY_HDR_EXT \
  (gst_rtp_dummy_hdr_ext_get_type())
#define GST_RTP_DUMMY_HDR_EXT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_DUMMY_HDR_EXT,GstRTPDummyHdrExt))
#define GST_RTP_DUMMY_HDR_EXT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_DUMMY_HDR_EXT,GstRTPDummyHdrExtClass))
#define GST_IS_RTP_DUMMY_HDR_EXT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_DUMMY_HDR_EXT))
#define GST_IS_RTP_DUMMY_HDR_EXT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_DUMMY_HDR_EXT))

#define DUMMY_HDR_EXT_URI "gst:test:uri"

typedef struct _GstRTPDummyHdrExt GstRTPDummyHdrExt;
typedef struct _GstRTPDummyHdrExtClass GstRTPDummyHdrExtClass;

struct _GstRTPDummyHdrExt
{
  GstRTPHeaderExtension payload;

  GstRTPHeaderExtensionFlags supported_flags;
  guint read_count;
  guint write_count;
  guint set_attributes_count;
  guint caps_field_value;

  gchar *direction;
  gchar *attributes;
};

struct _GstRTPDummyHdrExtClass
{
  GstRTPHeaderExtensionClass parent_class;
};

GType gst_rtp_dummy_hdr_ext_get_type (void);

G_DEFINE_TYPE (GstRTPDummyHdrExt, gst_rtp_dummy_hdr_ext,
    GST_TYPE_RTP_HEADER_EXTENSION);

static GstRTPHeaderExtensionFlags
gst_rtp_dummy_hdr_ext_get_supported_flags (GstRTPHeaderExtension * ext);
static gsize gst_rtp_dummy_hdr_ext_get_max_size (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta);
static gssize gst_rtp_dummy_hdr_ext_write (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size);
static gboolean gst_rtp_dummy_hdr_ext_read (GstRTPHeaderExtension * ext,
    GstRTPHeaderExtensionFlags read_flags, const guint8 * data, gsize size,
    GstBuffer * buffer);
static gboolean
gst_rtp_dummy_hdr_ext_set_caps_from_attributes (GstRTPHeaderExtension * ext,
    GstCaps * caps);
static gboolean
gst_rtp_dummy_hdr_ext_set_attributes_from_caps (GstRTPHeaderExtension * ext,
    const GstCaps * caps);
static gboolean
gst_rtp_dummy_hdr_ext_update_non_rtp_src_caps (GstRTPHeaderExtension * ext,
    GstCaps * caps);

static void gst_rtp_dummy_hdr_ext_finalize (GObject * object);

static void
gst_rtp_dummy_hdr_ext_class_init (GstRTPDummyHdrExtClass * klass)
{
  GstRTPHeaderExtensionClass *gstrtpheaderextension_class;
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstrtpheaderextension_class = GST_RTP_HEADER_EXTENSION_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gobject_class = G_OBJECT_CLASS (klass);

  gstrtpheaderextension_class->get_supported_flags =
      gst_rtp_dummy_hdr_ext_get_supported_flags;
  gstrtpheaderextension_class->get_max_size =
      gst_rtp_dummy_hdr_ext_get_max_size;
  gstrtpheaderextension_class->write = gst_rtp_dummy_hdr_ext_write;
  gstrtpheaderextension_class->read = gst_rtp_dummy_hdr_ext_read;
  gstrtpheaderextension_class->set_attributes_from_caps =
      gst_rtp_dummy_hdr_ext_set_attributes_from_caps;
  gstrtpheaderextension_class->set_caps_from_attributes =
      gst_rtp_dummy_hdr_ext_set_caps_from_attributes;
  gstrtpheaderextension_class->update_non_rtp_src_caps =
      gst_rtp_dummy_hdr_ext_update_non_rtp_src_caps;

  gobject_class->finalize = gst_rtp_dummy_hdr_ext_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "Dummy Test RTP Header Extension", GST_RTP_HDREXT_ELEMENT_CLASS,
      "Dummy Test RTP Header Extension", "Author <email@example.com>");
  gst_rtp_header_extension_class_set_uri (gstrtpheaderextension_class,
      DUMMY_HDR_EXT_URI);
}

static void
gst_rtp_dummy_hdr_ext_init (GstRTPDummyHdrExt * dummy)
{
  dummy->supported_flags =
      GST_RTP_HEADER_EXTENSION_ONE_BYTE | GST_RTP_HEADER_EXTENSION_TWO_BYTE;
}

static void
gst_rtp_dummy_hdr_ext_finalize (GObject * object)
{
  GstRTPDummyHdrExt *dummy = GST_RTP_DUMMY_HDR_EXT (object);

  g_free (dummy->attributes);
  dummy->attributes = NULL;
  g_free (dummy->direction);
  dummy->direction = NULL;

  G_OBJECT_CLASS (gst_rtp_dummy_hdr_ext_parent_class)->finalize (object);
}

static GstRTPHeaderExtension *
rtp_dummy_hdr_ext_new (void)
{
  return g_object_new (GST_TYPE_RTP_DUMMY_HDR_EXT, NULL);
}

static GstRTPHeaderExtensionFlags
gst_rtp_dummy_hdr_ext_get_supported_flags (GstRTPHeaderExtension * ext)
{
  GstRTPDummyHdrExt *dummy = GST_RTP_DUMMY_HDR_EXT (ext);

  return dummy->supported_flags;
}

static gsize
gst_rtp_dummy_hdr_ext_get_max_size (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta)
{
  return 1;
}

#define TEST_DATA_BYTE 0x9d

static gssize
gst_rtp_dummy_hdr_ext_write (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size)
{
  GstRTPDummyHdrExt *dummy = GST_RTP_DUMMY_HDR_EXT (ext);

  g_assert (size >= gst_rtp_dummy_hdr_ext_get_max_size (ext, NULL));

  data[0] = TEST_DATA_BYTE;

  dummy->write_count++;

  return 1;
}

static gboolean
gst_rtp_dummy_hdr_ext_read (GstRTPHeaderExtension * ext,
    GstRTPHeaderExtensionFlags read_flags, const guint8 * data,
    gsize size, GstBuffer * buffer)
{
  GstRTPDummyHdrExt *dummy = GST_RTP_DUMMY_HDR_EXT (ext);

  fail_unless_equals_int (data[0], TEST_DATA_BYTE);

  dummy->read_count++;

  if (dummy->read_count % 5 == 1) {
    /* Every fifth buffer triggers caps change. */
    gst_rtp_header_extension_set_wants_update_non_rtp_src_caps (ext, TRUE);
  }

  return TRUE;
}

static gboolean
gst_rtp_dummy_hdr_ext_set_caps_from_attributes (GstRTPHeaderExtension * ext,
    GstCaps * caps)
{
  GstRTPDummyHdrExt *dummy = GST_RTP_DUMMY_HDR_EXT (ext);
  gchar *field_name = gst_rtp_header_extension_get_sdp_caps_field_name (ext);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  if (!field_name)
    return FALSE;

  if (dummy->attributes || dummy->direction) {
    GValue arr = G_VALUE_INIT;
    GValue val = G_VALUE_INIT;

    g_value_init (&arr, GST_TYPE_ARRAY);
    g_value_init (&val, G_TYPE_STRING);

    /* direction */
    g_value_set_string (&val, dummy->direction);
    gst_value_array_append_value (&arr, &val);

    /* uri */
    g_value_set_string (&val, gst_rtp_header_extension_get_uri (ext));
    gst_value_array_append_value (&arr, &val);

    /* attributes */
    g_value_set_string (&val, dummy->attributes);
    gst_value_array_append_value (&arr, &val);

    gst_structure_set_value (s, field_name, &arr);

    g_value_unset (&val);
    g_value_unset (&arr);
  } else {
    gst_structure_set (s, field_name, G_TYPE_STRING,
        gst_rtp_header_extension_get_uri (ext), NULL);
  }

  g_free (field_name);
  return TRUE;
}

static gboolean
gst_rtp_dummy_hdr_ext_set_attributes_from_caps (GstRTPHeaderExtension * ext,
    const GstCaps * caps)
{
  GstRTPDummyHdrExt *dummy = GST_RTP_DUMMY_HDR_EXT (ext);
  gchar *field_name = gst_rtp_header_extension_get_sdp_caps_field_name (ext);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gchar *new_attrs = NULL, *new_direction = NULL;
  const gchar *ext_uri;
  const GValue *arr;

  dummy->set_attributes_count++;

  if (!field_name)
    return FALSE;

  if ((ext_uri = gst_structure_get_string (s, field_name))) {
    if (g_strcmp0 (ext_uri, gst_rtp_header_extension_get_uri (ext)) != 0) {
      /* incompatible extension uri for this instance */
      goto error;
    }
  } else if ((arr = gst_structure_get_value (s, field_name))
      && GST_VALUE_HOLDS_ARRAY (arr)
      && gst_value_array_get_size (arr) == 3) {
    const GValue *val;

    val = gst_value_array_get_value (arr, 1);
    if (!G_VALUE_HOLDS_STRING (val))
      goto error;
    if (g_strcmp0 (g_value_get_string (val),
            gst_rtp_header_extension_get_uri (ext)) != 0)
      goto error;

    val = gst_value_array_get_value (arr, 0);
    if (!G_VALUE_HOLDS_STRING (val))
      goto error;
    new_direction = g_value_dup_string (val);

    val = gst_value_array_get_value (arr, 2);
    if (!G_VALUE_HOLDS_STRING (val))
      goto error;
    new_attrs = g_value_dup_string (val);
  } else {
    /* unknown caps format */
    goto error;
  }

  g_free (dummy->attributes);
  dummy->attributes = new_attrs;
  g_free (dummy->direction);
  dummy->direction = new_direction;

  g_free (field_name);
  return TRUE;

error:
  g_free (field_name);
  g_free (new_direction);
  g_free (new_attrs);
  return FALSE;
}

static gboolean
gst_rtp_dummy_hdr_ext_update_non_rtp_src_caps (GstRTPHeaderExtension * ext,
    GstCaps * caps)
{
  GstRTPDummyHdrExt *dummy = GST_RTP_DUMMY_HDR_EXT (ext);

  gst_caps_set_simple (caps, "dummy-hdrext-val", G_TYPE_UINT,
      ++dummy->caps_field_value, NULL);

  return TRUE;
}
