/* GStreamer
 * Copyright (C) 2019 Igalia S.L
 * Copyright (C) 2019 Metrological
 *   Author: Charlie Turner <cturner@igalia.com>
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
#  include "config.h"
#endif

#include "gst-validate-mockdecryptor.h"

#define CLEARKEY_SYSTEM_ID "78f32170-d883-11e0-9572-0800200c9a66"
#define WIDEVINE_SYSTEM_ID "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"

GST_DEBUG_CATEGORY_STATIC (gst_mockdecryptor_debug);
#define GST_CAT_DEFAULT gst_mockdecryptor_debug

static GstStaticPadTemplate gst_mockdecryptor_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("application/x-cenc, original-media-type=(string)video/x-h264, "
        GST_PROTECTION_SYSTEM_ID_CAPS_FIELD "=(string)" WIDEVINE_SYSTEM_ID "; "
        "application/x-cenc, original-media-type=(string)audio/mpeg, "
        GST_PROTECTION_SYSTEM_ID_CAPS_FIELD "=(string)" WIDEVINE_SYSTEM_ID)
    );

static GstStaticPadTemplate gst_mockdecryptor_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/webm; "
        "audio/webm; "
        "video/mp4; " "audio/mp4; " "audio/mpeg; " "video/x-h264"));

#define _mockdecryptor_do_init \
    GST_DEBUG_CATEGORY_INIT (gst_mockdecryptor_debug, GST_MOCKDECRYPTOR_NAME, 0, "mock decryptor element");
#define gst_mockdecryptor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMockDecryptor, gst_mockdecryptor,
    GST_TYPE_BASE_TRANSFORM, _mockdecryptor_do_init);

static GstCaps *gst_mockdecryptor_transform_caps (GstBaseTransform *,
    GstPadDirection, GstCaps *, GstCaps *);
static GstFlowReturn gst_mockdecryptor_transform_in_place (GstBaseTransform *,
    GstBuffer *);

static void
gst_mockdecryptor_class_init (GstMockDecryptorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_mockdecryptor_transform_in_place);
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_mockdecryptor_transform_caps);
  base_transform_class->transform_ip_on_passthrough = FALSE;

  gst_element_class_add_static_pad_template (element_class,
      &gst_mockdecryptor_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_mockdecryptor_src_template);

  gst_element_class_set_metadata (element_class,
      "Mock decryptor element for unit tests",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Use in unit tests", "Charlie Turner <cturner@igalia.com>");
}

static void
gst_mockdecryptor_init (GstMockDecryptor * klass)
{
}

static GstCaps *
gst_mockdecryptor_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *transformed_caps = NULL;
  guint incoming_caps_size, size;
  gint duplicate;
  guint i;

  if (direction == GST_PAD_UNKNOWN)
    return NULL;

  GST_DEBUG_OBJECT (base,
      "direction: %s, caps: %" GST_PTR_FORMAT " filter: %" GST_PTR_FORMAT,
      (direction == GST_PAD_SRC) ? "src" : "sink", caps, filter);

  transformed_caps = gst_caps_new_empty ();

  incoming_caps_size = gst_caps_get_size (caps);
  for (i = 0; i < incoming_caps_size; ++i) {
    GstStructure *incoming_structure = gst_caps_get_structure (caps, i);
    GstStructure *outgoing_structure = NULL;
    guint index;

    if (direction == GST_PAD_SINK) {
      if (!gst_structure_has_field (incoming_structure, "original-media-type"))
        continue;

      outgoing_structure = gst_structure_copy (incoming_structure);
      gst_structure_set_name (outgoing_structure,
          gst_structure_get_string (outgoing_structure, "original-media-type"));

      gst_structure_remove_fields (outgoing_structure, "protection-system",
          "original-media-type", "encryption-algorithm", "encoding-scope",
          "cipher-mode", NULL);
    } else {
      outgoing_structure = gst_structure_copy (incoming_structure);

      /* Filter out the video related fields from the up-stream caps,
       * because they are not relevant to the input caps of this element and
       * can cause caps negotiation failures with adaptive bitrate streams.
       */
      gst_structure_remove_fields (outgoing_structure, "base-profile",
          "codec_data", "height", "framerate", "level", "pixel-aspect-ratio",
          "profile", "rate", "width", NULL);

      gst_structure_set (outgoing_structure,
          "protection-system", G_TYPE_STRING, WIDEVINE_SYSTEM_ID,
          "original-media-type", G_TYPE_STRING,
          gst_structure_get_name (incoming_structure), NULL);

      gst_structure_set_name (outgoing_structure, "application/x-cenc");
    }

    duplicate = FALSE;
    size = gst_caps_get_size (transformed_caps);

    for (index = 0; !duplicate && index < size; ++index) {
      GstStructure *structure =
          gst_caps_get_structure (transformed_caps, index);
      if (gst_structure_is_equal (structure, outgoing_structure))
        duplicate = TRUE;
    }

    if (!duplicate)
      gst_caps_append_structure (transformed_caps, outgoing_structure);
    else
      gst_structure_free (outgoing_structure);
  }

  if (filter) {
    GstCaps *intersection;

    GST_DEBUG_OBJECT (base, "Using filter caps %" GST_PTR_FORMAT, filter);
    intersection =
        gst_caps_intersect_full (transformed_caps, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_replace (&transformed_caps, intersection);
  }

  GST_DEBUG_OBJECT (base, "returning %" GST_PTR_FORMAT, transformed_caps);
  return transformed_caps;
}

static GstFlowReturn
gst_mockdecryptor_transform_in_place (GstBaseTransform * base,
    GstBuffer * buffer)
{
  /* We are a mock decryptor, just pass the encrypted buffers through... */
  return GST_FLOW_OK;
}
