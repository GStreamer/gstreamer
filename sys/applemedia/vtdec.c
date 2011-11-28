/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
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

#include "vtdec.h"

#include "corevideobuffer.h"
#include "vtutil.h"

GST_DEBUG_CATEGORY (gst_vtdec_debug);
#define GST_CAT_DEFAULT (gst_vtdec_debug)

#define GST_VTDEC_CODEC_DETAILS_QDATA \
    g_quark_from_static_string ("vtdec-codec-details")

static GstElementClass *parent_class = NULL;

static GstStateChangeReturn gst_vtdec_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_vtdec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_vtdec_chain (GstPad * pad, GstBuffer * buf);

static CMFormatDescriptionRef gst_vtdec_create_format_description
    (GstVTDec * self);
static CMFormatDescriptionRef
gst_vtdec_create_format_description_from_codec_data (GstVTDec * self,
    GstBuffer * codec_data);
static VTDecompressionSessionRef gst_vtdec_create_session (GstVTDec * self,
    CMFormatDescriptionRef fmt_desc);
static void gst_vtdec_destroy_session (GstVTDec * self,
    VTDecompressionSessionRef * session);
static GstFlowReturn gst_vtdec_decode_buffer (GstVTDec * self, GstBuffer * buf);
static void gst_vtdec_enqueue_frame (void *data, gsize unk1, VTStatus result,
    gsize unk2, CVBufferRef cvbuf);

static CMSampleBufferRef gst_vtdec_sample_buffer_from (GstVTDec * self,
    GstBuffer * buf);

static void
gst_vtdec_base_init (GstVTDecClass * klass)
{
  const GstVTDecoderDetails *codec_details =
      GST_VTDEC_CLASS_GET_CODEC_DETAILS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  const int min_width = 1, max_width = G_MAXINT;
  const int min_height = 1, max_height = G_MAXINT;
  const int min_fps_n = 0, max_fps_n = G_MAXINT;
  const int min_fps_d = 1, max_fps_d = 1;
  GstPadTemplate *sink_template, *src_template;
  GstCaps *sink_caps;
  GstElementDetails details;

  details.longname = g_strdup_printf ("%s decoder", codec_details->name);
  details.klass = g_strdup_printf ("Codec/Decoder/Video");
  details.description = g_strdup_printf ("%s decoder", codec_details->name);

  gst_element_class_set_details_simple (element_class,
      details.longname, details.klass, details.description,
      "Ole André Vadla Ravnås <oravnas@cisco.com>");

  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  sink_caps = gst_caps_new_simple (codec_details->mimetype,
      "width", GST_TYPE_INT_RANGE, min_width, max_width,
      "height", GST_TYPE_INT_RANGE, min_height, max_height,
      "framerate", GST_TYPE_FRACTION_RANGE,
      min_fps_n, min_fps_d, max_fps_n, max_fps_d, NULL);
  if (codec_details->format_id == kVTFormatH264) {
    gst_structure_set (gst_caps_get_structure (sink_caps, 0),
        "stream-format", G_TYPE_STRING, "avc", NULL);
  }
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      sink_caps);
  gst_element_class_add_pad_template (element_class, sink_template);
  gst_object_unref (sink_template);

  src_template = gst_pad_template_new ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '1', '2'),
          "width", GST_TYPE_INT_RANGE, min_width, max_width,
          "height", GST_TYPE_INT_RANGE, min_height, max_height,
          "framerate", GST_TYPE_FRACTION_RANGE,
          min_fps_n, min_fps_d, max_fps_n, max_fps_d, NULL));
  gst_element_class_add_pad_template (element_class, src_template);
  gst_object_unref (src_template);
}

static void
gst_vtdec_class_init (GstVTDecClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_vtdec_change_state;
}

static void
gst_vtdec_init (GstVTDec * self)
{
  GstVTDecClass *klass = (GstVTDecClass *) G_OBJECT_GET_CLASS (self);
  GstElementClass *element_klass = GST_ELEMENT_CLASS (klass);
  GstElement *element = GST_ELEMENT (self);

  self->details = GST_VTDEC_CLASS_GET_CODEC_DETAILS (klass);

  self->sinkpad = gst_pad_new_from_template
      (gst_element_class_get_pad_template (element_klass, "sink"), "sink");
  gst_element_add_pad (element, self->sinkpad);
  gst_pad_set_setcaps_function (self->sinkpad, gst_vtdec_sink_setcaps);
  gst_pad_set_chain_function (self->sinkpad, gst_vtdec_chain);

  self->srcpad = gst_pad_new_from_template
      (gst_element_class_get_pad_template (element_klass, "src"), "src");
  gst_element_add_pad (element, self->srcpad);
}

static GstStateChangeReturn
gst_vtdec_change_state (GstElement * element, GstStateChange transition)
{
  GstVTDec *self = GST_VTDEC_CAST (element);
  GError *error = NULL;
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
    self->ctx = gst_core_media_ctx_new (GST_API_CORE_VIDEO | GST_API_CORE_MEDIA
        | GST_API_VIDEO_TOOLBOX, &error);
    if (error != NULL)
      goto api_error;

    self->cur_outbufs = g_ptr_array_new ();
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
    gst_vtdec_destroy_session (self, &self->session);

    self->ctx->cm->FigFormatDescriptionRelease (self->fmt_desc);
    self->fmt_desc = NULL;

    self->negotiated_width = self->negotiated_height = 0;
    self->negotiated_fps_n = self->negotiated_fps_d = 0;
    self->caps_width = self->caps_height = 0;
    self->caps_fps_n = self->caps_fps_d = 0;

    g_ptr_array_free (self->cur_outbufs, TRUE);
    self->cur_outbufs = NULL;

    g_object_unref (self->ctx);
    self->ctx = NULL;
  }

  return ret;

api_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("API error"),
        ("%s", error->message));
    g_clear_error (&error);
    return GST_STATE_CHANGE_FAILURE;
  }
}

static gboolean
gst_vtdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVTDec *self = GST_VTDEC_CAST (GST_PAD_PARENT (pad));
  GstStructure *structure;
  CMFormatDescriptionRef fmt_desc = NULL;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &self->negotiated_width))
    goto incomplete_caps;
  if (!gst_structure_get_int (structure, "height", &self->negotiated_height))
    goto incomplete_caps;
  gst_structure_get_fraction (structure, "framerate",
      &self->negotiated_fps_n, &self->negotiated_fps_d);

  /* FIXME */
  if (self->negotiated_fps_n == 0)
    self->negotiated_fps_n = 30;
  if (self->negotiated_fps_d == 0)
    self->negotiated_fps_d = 1;

  if (self->details->format_id == kVTFormatH264) {
    const GValue *codec_data_value;

    codec_data_value = gst_structure_get_value (structure, "codec_data");
    if (codec_data_value != NULL) {
      fmt_desc = gst_vtdec_create_format_description_from_codec_data (self,
          gst_value_get_buffer (codec_data_value));
    } else {
      GST_DEBUG_OBJECT (self, "no codec_data in caps, awaiting future setcaps");
    }
  } else {
    fmt_desc = gst_vtdec_create_format_description (self);
  }

  if (fmt_desc != NULL) {
    gst_vtdec_destroy_session (self, &self->session);
    self->ctx->cm->FigFormatDescriptionRelease (self->fmt_desc);

    self->fmt_desc = fmt_desc;
    self->session = gst_vtdec_create_session (self, fmt_desc);
    if (self->session == NULL)
      goto session_create_error;
  }

  return TRUE;

  /* ERRORS */
incomplete_caps:
  {
    self->negotiated_width = self->negotiated_height = -1;
    return TRUE;
  }
session_create_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to create session"), (NULL));
    return FALSE;
  }
}

static gboolean
gst_vtdec_is_negotiated (GstVTDec * self)
{
  return self->negotiated_width != 0;
}

static gboolean
gst_vtdec_negotiate_downstream (GstVTDec * self)
{
  gboolean result;
  GstCaps *caps;
  GstStructure *s;

  if (self->caps_width == self->negotiated_width &&
      self->caps_height == self->negotiated_height &&
      self->caps_fps_n == self->negotiated_fps_n &&
      self->caps_fps_d == self->negotiated_fps_d) {
    return TRUE;
  }

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (self->srcpad));
  s = gst_caps_get_structure (caps, 0);
  gst_structure_set (s,
      "width", G_TYPE_INT, self->negotiated_width,
      "height", G_TYPE_INT, self->negotiated_height,
      "framerate", GST_TYPE_FRACTION,
      self->negotiated_fps_n, self->negotiated_fps_d, NULL);
  result = gst_pad_set_caps (self->srcpad, caps);
  gst_caps_unref (caps);

  self->caps_width = self->negotiated_width;
  self->caps_height = self->negotiated_height;
  self->caps_fps_n = self->negotiated_fps_n;
  self->caps_fps_d = self->negotiated_fps_d;

  return result;
}

static GstFlowReturn
gst_vtdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstVTDec *self = GST_VTDEC_CAST (GST_PAD_PARENT (pad));

  if (!gst_vtdec_is_negotiated (self))
    goto not_negotiated;

  if (self->session == NULL || self->negotiated_width < 0)
    goto pending_caps;

  return gst_vtdec_decode_buffer (self, buf);

not_negotiated:
  GST_DEBUG_OBJECT (self, "chain called while not negotiated");
  gst_buffer_unref (buf);
  return GST_FLOW_NOT_NEGOTIATED;

pending_caps:
  gst_buffer_unref (buf);
  GST_DEBUG_OBJECT (self, "dropped buffer %p (waiting for complete caps)", buf);
  return GST_FLOW_OK;
}

static CMFormatDescriptionRef
gst_vtdec_create_format_description (GstVTDec * self)
{
  CMFormatDescriptionRef fmt_desc;
  OSStatus status;

  status = self->ctx->cm->CMVideoFormatDescriptionCreate (NULL,
      self->details->format_id, self->negotiated_width, self->negotiated_height,
      NULL, &fmt_desc);
  if (status == noErr)
    return fmt_desc;
  else
    return NULL;
}

static CMFormatDescriptionRef
gst_vtdec_create_format_description_from_codec_data (GstVTDec * self,
    GstBuffer * codec_data)
{
  CMFormatDescriptionRef fmt_desc;
  OSStatus status;

  status =
      self->ctx->cm->
      FigVideoFormatDescriptionCreateWithSampleDescriptionExtensionAtom (NULL,
      self->details->format_id, self->negotiated_width, self->negotiated_height,
      'avcC', GST_BUFFER_DATA (codec_data), GST_BUFFER_SIZE (codec_data),
      &fmt_desc);
  if (status == noErr)
    return fmt_desc;
  else
    return NULL;
}

static VTDecompressionSessionRef
gst_vtdec_create_session (GstVTDec * self, CMFormatDescriptionRef fmt_desc)
{
  VTDecompressionSessionRef session = NULL;
  GstCVApi *cv = self->ctx->cv;
  CFMutableDictionaryRef pb_attrs;
  VTDecompressionOutputCallback callback;
  VTStatus status;

  pb_attrs = CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_i32 (pb_attrs, *(cv->kCVPixelBufferPixelFormatTypeKey),
      kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
  gst_vtutil_dict_set_i32 (pb_attrs, *(cv->kCVPixelBufferWidthKey),
      self->negotiated_width);
  gst_vtutil_dict_set_i32 (pb_attrs, *(cv->kCVPixelBufferHeightKey),
      self->negotiated_height);
  gst_vtutil_dict_set_i32 (pb_attrs,
      *(cv->kCVPixelBufferBytesPerRowAlignmentKey), 2 * self->negotiated_width);

  callback.func = gst_vtdec_enqueue_frame;
  callback.data = self;

  status = self->ctx->vt->VTDecompressionSessionCreate (NULL, fmt_desc,
      NULL, pb_attrs, &callback, &session);
  GST_INFO_OBJECT (self, "VTDecompressionSessionCreate for %d x %d => %d",
      self->negotiated_width, self->negotiated_height, status);

  CFRelease (pb_attrs);

  return session;
}

static void
gst_vtdec_destroy_session (GstVTDec * self, VTDecompressionSessionRef * session)
{
  self->ctx->vt->VTDecompressionSessionInvalidate (*session);
  self->ctx->vt->VTDecompressionSessionRelease (*session);
  *session = NULL;
}

static GstFlowReturn
gst_vtdec_decode_buffer (GstVTDec * self, GstBuffer * buf)
{
  GstVTApi *vt = self->ctx->vt;
  CMSampleBufferRef sbuf;
  VTStatus status;
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;

  self->cur_inbuf = buf;
  sbuf = gst_vtdec_sample_buffer_from (self, buf);

  status = vt->VTDecompressionSessionDecodeFrame (self->session, sbuf, 0, 0, 0);
  if (status != 0) {
    GST_WARNING_OBJECT (self, "VTDecompressionSessionDecodeFrame returned %d",
        status);
  }

  status = vt->VTDecompressionSessionWaitForAsynchronousFrames (self->session);
  if (status != 0) {
    GST_WARNING_OBJECT (self,
        "VTDecompressionSessionWaitForAsynchronousFrames returned %d", status);
  }

  self->ctx->cm->FigSampleBufferRelease (sbuf);
  self->cur_inbuf = NULL;
  gst_buffer_unref (buf);

  if (self->cur_outbufs->len > 0) {
    if (!gst_vtdec_negotiate_downstream (self))
      ret = GST_FLOW_NOT_NEGOTIATED;
  }

  for (i = 0; i != self->cur_outbufs->len; i++) {
    GstBuffer *buf = g_ptr_array_index (self->cur_outbufs, i);

    if (ret == GST_FLOW_OK) {
      gst_buffer_set_caps (buf, GST_PAD_CAPS (self->srcpad));
      ret = gst_pad_push (self->srcpad, buf);
    } else {
      gst_buffer_unref (buf);
    }
  }
  g_ptr_array_set_size (self->cur_outbufs, 0);

  return ret;
}

static void
gst_vtdec_enqueue_frame (void *data, gsize unk1, VTStatus result, gsize unk2,
    CVBufferRef cvbuf)
{
  GstVTDec *self = GST_VTDEC_CAST (data);
  GstBuffer *buf;

  if (result != kVTSuccess)
    goto beach;

  buf = gst_core_video_buffer_new (self->ctx, cvbuf);
  gst_buffer_copy_metadata (buf, self->cur_inbuf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

  g_ptr_array_add (self->cur_outbufs, buf);

beach:
  return;
}

static CMSampleBufferRef
gst_vtdec_sample_buffer_from (GstVTDec * self, GstBuffer * buf)
{
  GstCMApi *cm = self->ctx->cm;
  OSStatus status;
  CMBlockBufferRef bbuf = NULL;
  CMSampleBufferRef sbuf = NULL;

  g_assert (self->fmt_desc != NULL);

  status = cm->CMBlockBufferCreateWithMemoryBlock (NULL,
      GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), kCFAllocatorNull, NULL,
      0, GST_BUFFER_SIZE (buf), FALSE, &bbuf);
  if (status != noErr)
    goto beach;

  status = cm->CMSampleBufferCreate (NULL, bbuf, TRUE, 0, 0, self->fmt_desc,
      1, 0, NULL, 0, NULL, &sbuf);
  if (status != noErr)
    goto beach;

beach:
  cm->FigBlockBufferRelease (bbuf);
  return sbuf;
}

static void
gst_vtdec_register (GstPlugin * plugin,
    const GstVTDecoderDetails * codec_details)
{
  GTypeInfo type_info = {
    sizeof (GstVTDecClass),
    (GBaseInitFunc) gst_vtdec_base_init,
    NULL,
    (GClassInitFunc) gst_vtdec_class_init,
    NULL,
    NULL,
    sizeof (GstVTDecClass),
    0,
    (GInstanceInitFunc) gst_vtdec_init,
  };
  gchar *type_name;
  GType type;
  gboolean result;

  type_name = g_strdup_printf ("vtdec_%s", codec_details->element_name);

  type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &type_info, 0);

  g_type_set_qdata (type, GST_VTDEC_CODEC_DETAILS_QDATA,
      (gpointer) codec_details);

  result = gst_element_register (plugin, type_name, GST_RANK_NONE, type);
  if (!result) {
    GST_ERROR_OBJECT (plugin, "failed to register element %s", type_name);
  }

  g_free (type_name);
}

static const GstVTDecoderDetails gst_vtdec_codecs[] = {
  {"H.264", "h264", "video/x-h264", kVTFormatH264},
  {"JPEG", "jpeg", "image/jpeg", kVTFormatJPEG}
};

void
gst_vtdec_register_elements (GstPlugin * plugin)
{
  guint i;

  GST_DEBUG_CATEGORY_INIT (gst_vtdec_debug, "vtdec",
      0, "Apple VideoToolbox Decoder Wrapper");

  for (i = 0; i != G_N_ELEMENTS (gst_vtdec_codecs); i++)
    gst_vtdec_register (plugin, &gst_vtdec_codecs[i]);
}
