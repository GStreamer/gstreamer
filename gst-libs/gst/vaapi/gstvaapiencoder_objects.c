/*
 *  gstvaapiencoder_objects.c - VA encoder objects abstraction
 *
 *  Copyright (C) 2013-2014 Intel Corporation
 *    Author: Wind Yuan <feng.yuan@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "sysdeps.h"
#include "gstvaapiencoder_objects.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapisurfaceproxy_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GET_ENCODER(obj)    GST_VAAPI_ENCODER_CAST((obj)->parent_instance.codec)
#define GET_VA_DISPLAY(obj) GET_ENCODER(obj)->va_display
#define GET_VA_CONTEXT(obj) GET_ENCODER(obj)->va_context

/* ------------------------------------------------------------------------- */
/* --- Encoder Packed Header                                             --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncPackedHeader,
    gst_vaapi_enc_packed_header);

void
gst_vaapi_enc_packed_header_destroy (GstVaapiEncPackedHeader * header)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (header), &header->param_id);
  vaapi_destroy_buffer (GET_VA_DISPLAY (header), &header->data_id);
  header->param = NULL;
  header->data = NULL;
}

gboolean
gst_vaapi_enc_packed_header_create (GstVaapiEncPackedHeader * header,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  gboolean success;

  header->param_id = VA_INVALID_ID;
  header->data_id = VA_INVALID_ID;

  success = vaapi_create_buffer (GET_VA_DISPLAY (header),
      GET_VA_CONTEXT (header),
      VAEncPackedHeaderParameterBufferType,
      args->param_size, args->param, &header->param_id, &header->param);
  if (!success)
    return FALSE;

  if (!args->data_size)
    return TRUE;

  success = vaapi_create_buffer (GET_VA_DISPLAY (header),
      GET_VA_CONTEXT (header),
      VAEncPackedHeaderDataBufferType,
      args->data_size, args->data, &header->data_id, &header->data);
  if (!success)
    return FALSE;
  return TRUE;
}

GstVaapiEncPackedHeader *
gst_vaapi_enc_packed_header_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size, gconstpointer data, guint data_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiEncPackedHeaderClass,
      GST_VAAPI_CODEC_BASE (encoder), param, param_size, data, data_size, 0);
  return GST_VAAPI_ENC_PACKED_HEADER (object);
}

gboolean
gst_vaapi_enc_packed_header_set_data (GstVaapiEncPackedHeader * header,
    gconstpointer data, guint data_size)
{
  gboolean success;

  vaapi_destroy_buffer (GET_VA_DISPLAY (header), &header->data_id);
  header->data = NULL;

  success = vaapi_create_buffer (GET_VA_DISPLAY (header),
      GET_VA_CONTEXT (header),
      VAEncPackedHeaderDataBufferType,
      data_size, data, &header->data_id, &header->data);
  if (!success)
    return FALSE;
  return TRUE;
}

/* ------------------------------------------------------------------------- */
/* --- Encoder Sequence                                                  --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncSequence, gst_vaapi_enc_sequence);

void
gst_vaapi_enc_sequence_destroy (GstVaapiEncSequence * sequence)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (sequence), &sequence->param_id);
  sequence->param = NULL;
}

gboolean
gst_vaapi_enc_sequence_create (GstVaapiEncSequence * sequence,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  gboolean success;

  sequence->param_id = VA_INVALID_ID;
  success = vaapi_create_buffer (GET_VA_DISPLAY (sequence),
      GET_VA_CONTEXT (sequence),
      VAEncSequenceParameterBufferType,
      args->param_size, args->param, &sequence->param_id, &sequence->param);
  if (!success)
    return FALSE;
  return TRUE;
}

GstVaapiEncSequence *
gst_vaapi_enc_sequence_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiEncSequenceClass,
      GST_VAAPI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  return GST_VAAPI_ENC_SEQUENCE (object);
}

/* ------------------------------------------------------------------------- */
/* --- Encoder Slice                                                     --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncSlice, gst_vaapi_enc_slice);

void
gst_vaapi_enc_slice_destroy (GstVaapiEncSlice * slice)
{
  if (slice->packed_headers) {
    g_ptr_array_unref (slice->packed_headers);
    slice->packed_headers = NULL;
  }

  vaapi_destroy_buffer (GET_VA_DISPLAY (slice), &slice->param_id);
  slice->param = NULL;
}

gboolean
gst_vaapi_enc_slice_create (GstVaapiEncSlice * slice,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  gboolean success;

  slice->param_id = VA_INVALID_ID;
  success = vaapi_create_buffer (GET_VA_DISPLAY (slice),
      GET_VA_CONTEXT (slice),
      VAEncSliceParameterBufferType,
      args->param_size, args->param, &slice->param_id, &slice->param);
  if (!success)
    return FALSE;

  slice->packed_headers = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_vaapi_mini_object_unref);
  if (!slice->packed_headers)
    return FALSE;

  return TRUE;
}

GstVaapiEncSlice *
gst_vaapi_enc_slice_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiEncSliceClass,
      GST_VAAPI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  return GST_VAAPI_ENC_SLICE (object);
}

/* ------------------------------------------------------------------------- */
/* --- Encoder Misc Parameter Buffer                                     --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncMiscParam, gst_vaapi_enc_misc_param);

void
gst_vaapi_enc_misc_param_destroy (GstVaapiEncMiscParam * misc)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (misc), &misc->param_id);
  misc->param = NULL;
  misc->data = NULL;
}

gboolean
gst_vaapi_enc_misc_param_create (GstVaapiEncMiscParam * misc,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  gboolean success;

  misc->param_id = VA_INVALID_ID;
  success = vaapi_create_buffer (GET_VA_DISPLAY (misc),
      GET_VA_CONTEXT (misc),
      VAEncMiscParameterBufferType,
      args->param_size, args->param, &misc->param_id, &misc->param);
  if (!success)
    return FALSE;
  return TRUE;
}

GstVaapiEncMiscParam *
gst_vaapi_enc_misc_param_new (GstVaapiEncoder * encoder,
    VAEncMiscParameterType type, guint data_size)
{
  GstVaapiCodecObject *object;
  GstVaapiEncMiscParam *misc;
  VAEncMiscParameterBuffer *va_misc;

  object = gst_vaapi_codec_object_new (&GstVaapiEncMiscParamClass,
      GST_VAAPI_CODEC_BASE (encoder),
      NULL, sizeof (VAEncMiscParameterBuffer) + data_size, NULL, 0, 0);
  if (!object)
    return NULL;

  misc = GST_VAAPI_ENC_MISC_PARAM (object);
  va_misc = misc->param;
  va_misc->type = type;
  misc->data = va_misc->data;
  return misc;
}

/* ------------------------------------------------------------------------- */
/* ---  Quantization Matrices                                            --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncQMatrix, gst_vaapi_enc_q_matrix);

void
gst_vaapi_enc_q_matrix_destroy (GstVaapiEncQMatrix * q_matrix)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (q_matrix), &q_matrix->param_id);
  q_matrix->param = NULL;
}

gboolean
gst_vaapi_enc_q_matrix_create (GstVaapiEncQMatrix * q_matrix,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  q_matrix->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (q_matrix),
      GET_VA_CONTEXT (q_matrix), VAQMatrixBufferType,
      args->param_size, args->param, &q_matrix->param_id, &q_matrix->param);
}

GstVaapiEncQMatrix *
gst_vaapi_enc_q_matrix_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiEncQMatrixClass,
      GST_VAAPI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_ENC_Q_MATRIX_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* --- JPEG Huffman Tables                                               --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncHuffmanTable,
    gst_vaapi_enc_huffman_table);

void
gst_vaapi_enc_huffman_table_destroy (GstVaapiEncHuffmanTable * huf_table)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (huf_table), &huf_table->param_id);
  huf_table->param = NULL;
}

gboolean
gst_vaapi_enc_huffman_table_create (GstVaapiEncHuffmanTable * huf_table,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  huf_table->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (huf_table),
      GET_VA_CONTEXT (huf_table), VAHuffmanTableBufferType, args->param_size,
      args->param, &huf_table->param_id, (void **) &huf_table->param);
}

GstVaapiEncHuffmanTable *
gst_vaapi_enc_huffman_table_new (GstVaapiEncoder * encoder,
    guint8 * data, guint data_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiEncHuffmanTableClass,
      GST_VAAPI_CODEC_BASE (encoder), data, data_size, NULL, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_ENC_HUFFMAN_TABLE_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* --- Encoder Picture                                                   --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncPicture, gst_vaapi_enc_picture);

void
gst_vaapi_enc_picture_destroy (GstVaapiEncPicture * picture)
{
  if (picture->packed_headers) {
    g_ptr_array_unref (picture->packed_headers);
    picture->packed_headers = NULL;
  }
  if (picture->misc_params) {
    g_ptr_array_unref (picture->misc_params);
    picture->misc_params = NULL;
  }
  if (picture->slices) {
    g_ptr_array_unref (picture->slices);
    picture->slices = NULL;
  }

  gst_vaapi_codec_object_replace (&picture->q_matrix, NULL);
  gst_vaapi_codec_object_replace (&picture->huf_table, NULL);

  gst_vaapi_codec_object_replace (&picture->sequence, NULL);

  gst_vaapi_surface_proxy_replace (&picture->proxy, NULL);
  picture->surface_id = VA_INVALID_ID;
  picture->surface = NULL;

  vaapi_destroy_buffer (GET_VA_DISPLAY (picture), &picture->param_id);
  picture->param = NULL;

  if (picture->frame) {
    gst_video_codec_frame_unref (picture->frame);
    picture->frame = NULL;
  }
}

gboolean
gst_vaapi_enc_picture_create (GstVaapiEncPicture * picture,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  GstVideoCodecFrame *const frame = (GstVideoCodecFrame *) args->data;
  gboolean success;

  picture->proxy = gst_video_codec_frame_get_user_data (frame);
  if (!gst_vaapi_surface_proxy_ref (picture->proxy))
    return FALSE;

  picture->surface = GST_VAAPI_SURFACE_PROXY_SURFACE (picture->proxy);
  if (!picture->surface)
    return FALSE;

  picture->surface_id = GST_VAAPI_SURFACE_ID (picture->surface);
  if (picture->surface_id == VA_INVALID_ID)
    return FALSE;

  picture->type = GST_VAAPI_PICTURE_TYPE_NONE;
  picture->pts = GST_CLOCK_TIME_NONE;
  picture->frame_num = 0;
  picture->poc = 0;

  picture->param_id = VA_INVALID_ID;
  picture->param_size = args->param_size;
  success = vaapi_create_buffer (GET_VA_DISPLAY (picture),
      GET_VA_CONTEXT (picture),
      VAEncPictureParameterBufferType,
      args->param_size, args->param, &picture->param_id, &picture->param);
  if (!success)
    return FALSE;
  picture->param_size = args->param_size;

  picture->packed_headers = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_vaapi_mini_object_unref);
  if (!picture->packed_headers)
    return FALSE;

  picture->misc_params = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_vaapi_mini_object_unref);
  if (!picture->misc_params)
    return FALSE;

  picture->slices = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_vaapi_mini_object_unref);
  if (!picture->slices)
    return FALSE;

  picture->frame = gst_video_codec_frame_ref (frame);
  return TRUE;
}

GstVaapiEncPicture *
gst_vaapi_enc_picture_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size, GstVideoCodecFrame * frame)
{
  GstVaapiCodecObject *object;

  g_return_val_if_fail (frame != NULL, NULL);

  object = gst_vaapi_codec_object_new (&GstVaapiEncPictureClass,
      GST_VAAPI_CODEC_BASE (encoder), param, param_size, frame, 0, 0);
  return GST_VAAPI_ENC_PICTURE (object);
}

void
gst_vaapi_enc_picture_set_sequence (GstVaapiEncPicture * picture,
    GstVaapiEncSequence * sequence)
{
  g_return_if_fail (picture != NULL);
  g_return_if_fail (sequence != NULL);

  gst_vaapi_codec_object_replace (&picture->sequence, sequence);
}

void
gst_vaapi_enc_picture_add_packed_header (GstVaapiEncPicture * picture,
    GstVaapiEncPackedHeader * header)
{
  g_return_if_fail (picture != NULL);
  g_return_if_fail (header != NULL);

  g_ptr_array_add (picture->packed_headers,
      gst_vaapi_codec_object_ref (header));
}

void
gst_vaapi_enc_picture_add_misc_param (GstVaapiEncPicture * picture,
    GstVaapiEncMiscParam * misc)
{
  g_return_if_fail (picture != NULL);
  g_return_if_fail (misc != NULL);

  g_ptr_array_add (picture->misc_params, gst_vaapi_codec_object_ref (misc));
}

void
gst_vaapi_enc_picture_add_slice (GstVaapiEncPicture * picture,
    GstVaapiEncSlice * slice)
{
  g_return_if_fail (picture != NULL);
  g_return_if_fail (slice != NULL);

  g_ptr_array_add (picture->slices, gst_vaapi_codec_object_ref (slice));
}

void
gst_vaapi_enc_slice_add_packed_header (GstVaapiEncSlice * slice,
    GstVaapiEncPackedHeader * header)
{
  g_return_if_fail (slice != NULL);
  g_return_if_fail (header != NULL);

  g_ptr_array_add (slice->packed_headers, gst_vaapi_codec_object_ref (header));
}

static gboolean
do_encode (VADisplay dpy, VAContextID ctx, VABufferID * buf_id, void **buf_ptr)
{
  VAStatus status;

  vaapi_unmap_buffer (dpy, *buf_id, buf_ptr);

  status = vaRenderPicture (dpy, ctx, buf_id, 1);
  if (!vaapi_check_status (status, "vaRenderPicture()"))
    return FALSE;

  /* XXX: vaRenderPicture() is meant to destroy the VA buffer implicitly */
  vaapi_destroy_buffer (dpy, buf_id);
  return TRUE;
}

gboolean
gst_vaapi_enc_picture_encode (GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence;
  GstVaapiEncQMatrix *q_matrix;
  GstVaapiEncHuffmanTable *huf_table;
  VADisplay va_display;
  VAContextID va_context;
  VAStatus status;
  guint i;

  g_return_val_if_fail (picture != NULL, FALSE);
  g_return_val_if_fail (picture->surface_id != VA_INVALID_SURFACE, FALSE);

  va_display = GET_VA_DISPLAY (picture);
  va_context = GET_VA_CONTEXT (picture);

  GST_DEBUG ("encode picture 0x%08x", picture->surface_id);

  status = vaBeginPicture (va_display, va_context, picture->surface_id);
  if (!vaapi_check_status (status, "vaBeginPicture()"))
    return FALSE;

  /* Submit Sequence parameter */
  sequence = picture->sequence;
  if (sequence && !do_encode (va_display, va_context,
          &sequence->param_id, &sequence->param))
    return FALSE;

  /* Submit Quantization matrix */
  q_matrix = picture->q_matrix;
  if (q_matrix && !do_encode (va_display, va_context,
          &q_matrix->param_id, &q_matrix->param))
    return FALSE;

  /* Submit huffman table */
  huf_table = picture->huf_table;
  if (huf_table && !do_encode (va_display, va_context,
          &huf_table->param_id, (void **) &huf_table->param))
    return FALSE;

  /* Submit Packed Headers */
  for (i = 0; i < picture->packed_headers->len; i++) {
    GstVaapiEncPackedHeader *const header =
        g_ptr_array_index (picture->packed_headers, i);
    if (!do_encode (va_display, va_context,
            &header->param_id, &header->param) ||
        !do_encode (va_display, va_context, &header->data_id, &header->data))
      return FALSE;
  }

  /* Submit Picture parameter */
  if (!do_encode (va_display, va_context, &picture->param_id, &picture->param))
    return FALSE;

  /* Submit Misc Params */
  for (i = 0; i < picture->misc_params->len; i++) {
    GstVaapiEncMiscParam *const misc =
        g_ptr_array_index (picture->misc_params, i);
    if (!do_encode (va_display, va_context, &misc->param_id, &misc->param))
      return FALSE;
  }

  /* Submit Slice parameters */
  for (i = 0; i < picture->slices->len; i++) {
    GstVaapiEncSlice *const slice = g_ptr_array_index (picture->slices, i);
    guint j;

    /* Submit packed_slice_header and packed_raw_data */
    for (j = 0; j < slice->packed_headers->len; j++) {
      GstVaapiEncPackedHeader *const header =
          g_ptr_array_index (slice->packed_headers, j);
      if (!do_encode (va_display, va_context,
              &header->param_id, &header->param) ||
          !do_encode (va_display, va_context, &header->data_id, &header->data))
        return FALSE;
    }
    if (!do_encode (va_display, va_context, &slice->param_id, &slice->param))
      return FALSE;
  }

  status = vaEndPicture (va_display, va_context);
  if (!vaapi_check_status (status, "vaEndPicture()"))
    return FALSE;
  return TRUE;
}
