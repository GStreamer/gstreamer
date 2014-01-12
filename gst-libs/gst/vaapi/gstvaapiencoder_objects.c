/*
 *  gstvaapiencoder_objects.c - VA encoder objects abstraction
 *
 *  Copyright (C) 2013 Intel Corporation
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
#include "glibcompat.h"

#include "gstvaapiencoder_objects.h"
#include "gstvaapiencoder.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapisurfaceproxy_priv.h"
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
gst_vaapi_enc_packed_header_destroy (GstVaapiEncPackedHeader * packed_header)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (packed_header),
      &packed_header->param_id);
  vaapi_destroy_buffer (GET_VA_DISPLAY (packed_header),
      &packed_header->data_id);
  packed_header->param = NULL;
  packed_header->data = NULL;
}

gboolean
gst_vaapi_enc_packed_header_create (GstVaapiEncPackedHeader * packed_header,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  gboolean success;

  packed_header->param_id = VA_INVALID_ID;
  packed_header->param = NULL;
  packed_header->data_id = VA_INVALID_ID;
  packed_header->data = NULL;
  success = vaapi_create_buffer (GET_VA_DISPLAY (packed_header),
      GET_VA_CONTEXT (packed_header),
      VAEncPackedHeaderParameterBufferType,
      args->param_size,
      args->param, &packed_header->param_id, &packed_header->param);
  if (!success)
    return FALSE;

  if (!args->data_size)
    return TRUE;

  success = vaapi_create_buffer (GET_VA_DISPLAY (packed_header),
      GET_VA_CONTEXT (packed_header),
      VAEncPackedHeaderDataBufferType,
      args->data_size,
      args->data, &packed_header->data_id, &packed_header->data);
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
gst_vaapi_enc_packed_header_set_data (GstVaapiEncPackedHeader * packed_header,
    gconstpointer data, guint data_size)
{
  gboolean success;

  g_assert (packed_header->data_id == VA_INVALID_ID);
  if (packed_header->data_id != VA_INVALID_ID) {
    vaapi_destroy_buffer (GET_VA_DISPLAY (packed_header),
        &packed_header->data_id);
    packed_header->data = NULL;
  }
  success = vaapi_create_buffer (GET_VA_DISPLAY (packed_header),
      GET_VA_CONTEXT (packed_header),
      VAEncPackedHeaderDataBufferType,
      data_size, data, &packed_header->data_id, &packed_header->data);
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
  return GST_VAAPI_ENC_SEQUENCE_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* --- Encoder Slice                                                     --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncSlice, gst_vaapi_enc_slice);

void
gst_vaapi_enc_slice_destroy (GstVaapiEncSlice * slice)
{
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

  return TRUE;
}

GstVaapiEncSlice *
gst_vaapi_enc_slice_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiEncSliceClass,
      GST_VAAPI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  return GST_VAAPI_ENC_SLICE_CAST (object);
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
  misc->impl = NULL;
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
    VAEncMiscParameterType type, guint total_size)
{
  GstVaapiCodecObject *object;
  GstVaapiEncMiscParam *misc_obj;
  VAEncMiscParameterBuffer *misc;

  object = gst_vaapi_codec_object_new (&GstVaapiEncMiscParamClass,
      GST_VAAPI_CODEC_BASE (encoder), NULL, total_size, NULL, 0, 0);
  if (!object)
    return NULL;

  misc_obj = GST_VAAPI_ENC_MISC_PARAM_CAST (object);
  misc = misc_obj->param;
  misc->type = type;
  misc_obj->impl = misc->data;
  g_assert (misc_obj->impl);
  return misc_obj;
}

/* ------------------------------------------------------------------------- */
/* --- Encoder Picture                                                   --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiEncPicture, gst_vaapi_enc_picture);

static void
destroy_vaapi_obj_cb (gpointer data, gpointer user_data)
{
  GstVaapiMiniObject *const object = data;

  gst_vaapi_mini_object_unref (object);
}

void
gst_vaapi_enc_picture_destroy (GstVaapiEncPicture * picture)
{
  if (picture->packed_headers) {
    g_ptr_array_foreach (picture->packed_headers, destroy_vaapi_obj_cb, NULL);
    g_ptr_array_free (picture->packed_headers, TRUE);
    picture->packed_headers = NULL;
  }
  if (picture->misc_buffers) {
    g_ptr_array_foreach (picture->misc_buffers, destroy_vaapi_obj_cb, NULL);
    g_ptr_array_free (picture->misc_buffers, TRUE);
    picture->misc_buffers = NULL;
  }
  if (picture->slices) {
    g_ptr_array_foreach (picture->slices, destroy_vaapi_obj_cb, NULL);
    g_ptr_array_free (picture->slices, TRUE);
    picture->slices = NULL;
  }
  gst_vaapi_mini_object_replace (
      (GstVaapiMiniObject **) (&picture->sequence), NULL);

  gst_vaapi_surface_proxy_replace (&picture->proxy, NULL);
  picture->surface_id = VA_INVALID_ID;
  picture->surface = NULL;

  vaapi_destroy_buffer (GET_VA_DISPLAY (picture), &picture->param_id);
  picture->param = NULL;
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

  g_return_val_if_fail (frame != NULL, FALSE);

  picture->proxy = gst_video_codec_frame_get_user_data (frame);
  if (!gst_vaapi_surface_proxy_ref (picture->proxy))
    return FALSE;

  picture->surface = GST_VAAPI_SURFACE_PROXY_SURFACE (picture->proxy);
  if (!picture->surface)
    return FALSE;

  picture->surface_id = GST_VAAPI_OBJECT_ID (picture->surface);
  if (picture->surface_id == VA_INVALID_ID)
    return FALSE;

  picture->sequence = NULL;
  picture->type = GST_VAAPI_PICTURE_TYPE_NONE;
  picture->pts = GST_CLOCK_TIME_NONE;
  picture->frame_num = 0;
  picture->poc = 0;

  picture->param_id = VA_INVALID_ID;
  picture->param_size = args->param_size;
  picture->param = NULL;
  success = vaapi_create_buffer (GET_VA_DISPLAY (picture),
      GET_VA_CONTEXT (picture),
      VAEncPictureParameterBufferType,
      args->param_size, args->param, &picture->param_id, &picture->param);
  if (!success)
    return FALSE;
  picture->param_size = args->param_size;

  picture->packed_headers = g_ptr_array_new ();
  picture->misc_buffers = g_ptr_array_new ();
  picture->slices = g_ptr_array_new ();

  g_assert (picture->packed_headers && picture->misc_buffers
      && picture->slices);
  if (!picture->packed_headers || !picture->misc_buffers || !picture->slices)
    return FALSE;

  picture->frame = gst_video_codec_frame_ref (frame);

  return TRUE;
}

GstVaapiEncPicture *
gst_vaapi_enc_picture_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size, GstVideoCodecFrame * frame)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiEncPictureClass,
      GST_VAAPI_CODEC_BASE (encoder), param, param_size, frame, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_ENC_PICTURE_CAST (object);
}

void
gst_vaapi_enc_picture_set_sequence (GstVaapiEncPicture * picture,
    GstVaapiEncSequence * sequence)
{
  g_return_if_fail (GST_VAAPI_IS_ENC_PICTURE (picture));
  g_return_if_fail (GST_VAAPI_IS_ENC_SEQUENCE (sequence));

  g_assert (sequence);
  gst_vaapi_mini_object_replace (
      (GstVaapiMiniObject **) (&picture->sequence),
      GST_VAAPI_MINI_OBJECT (sequence));
}

void
gst_vaapi_enc_picture_add_packed_header (GstVaapiEncPicture * picture,
    GstVaapiEncPackedHeader * header)
{
  g_return_if_fail (GST_VAAPI_IS_ENC_PICTURE (picture));
  g_return_if_fail (GST_VAAPI_IS_ENC_PACKED_HEADER (header));

  g_assert (picture->packed_headers);
  g_ptr_array_add (picture->packed_headers,
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (header)));
}

void
gst_vaapi_enc_picture_add_misc_buffer (GstVaapiEncPicture * picture,
    GstVaapiEncMiscParam * misc)
{
  g_return_if_fail (GST_VAAPI_IS_ENC_PICTURE (picture));
  g_return_if_fail (GST_VAAPI_IS_ENC_MISC_PARAM (misc));

  g_assert (picture->misc_buffers);
  g_ptr_array_add (picture->misc_buffers,
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (misc)));
}

void
gst_vaapi_enc_picture_add_slice (GstVaapiEncPicture * picture,
    GstVaapiEncSlice * slice)
{
  g_return_if_fail (GST_VAAPI_IS_ENC_PICTURE (picture));
  g_return_if_fail (GST_VAAPI_IS_ENC_SLICE (slice));

  g_ptr_array_add (picture->slices,
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (slice)));
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
  GstVaapiEncPackedHeader *packed_header;
  GstVaapiEncMiscParam *misc;
  GstVaapiEncSlice *slice;
  VADisplay va_display;
  VAContextID va_context;
  VAStatus status;
  guint i;

  g_return_val_if_fail (GST_VAAPI_IS_ENC_PICTURE (picture), FALSE);
  g_return_val_if_fail (picture->surface_id != VA_INVALID_SURFACE, FALSE);

  va_display = GET_VA_DISPLAY (picture);
  va_context = GET_VA_CONTEXT (picture);

  GST_DEBUG ("encode picture 0x%08x", picture->surface_id);

  status = vaBeginPicture (va_display, va_context, picture->surface_id);
  if (!vaapi_check_status (status, "vaBeginPicture()"))
    return FALSE;

  /* encode sequence parameter */
  sequence = picture->sequence;
  if (sequence) {
    if (!do_encode (va_display, va_context,
            &sequence->param_id, &sequence->param))
      return FALSE;
  }

  /* encode picture parameter */
  if (!do_encode (va_display, va_context, &picture->param_id, &picture->param))
    return FALSE;

  /* encode packed headers  */
  for (i = 0; i < picture->packed_headers->len; i++) {
    packed_header = g_ptr_array_index (picture->packed_headers, i);
    if (!do_encode (va_display, va_context,
            &packed_header->param_id, &packed_header->param) ||
        !do_encode (va_display, va_context,
            &packed_header->data_id, &packed_header->data))
      return FALSE;
  }

  /* encode misc buffers  */
  for (i = 0; i < picture->misc_buffers->len; i++) {
    misc = g_ptr_array_index (picture->misc_buffers, i);
    if (!do_encode (va_display, va_context, &misc->param_id, &misc->param))
      return FALSE;
  }

  /* encode slice parameters */
  for (i = 0; i < picture->slices->len; i++) {
    slice = g_ptr_array_index (picture->slices, i);
    if (!do_encode (va_display, va_context, &slice->param_id, &slice->param))
      return FALSE;
  }

  status = vaEndPicture (va_display, va_context);
  if (!vaapi_check_status (status, "vaEndPicture()"))
    return FALSE;
  return TRUE;
}
