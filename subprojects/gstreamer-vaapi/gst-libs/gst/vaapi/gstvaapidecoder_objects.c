/*
 *  gstvaapidecoder_objects.c - VA decoder objects helpers
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
#include <gst/vaapi/gstvaapicontext.h>
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapisurfaceproxy_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GET_DECODER(obj)    GST_VAAPI_DECODER_CAST((obj)->parent_instance.codec)
#define GET_CONTEXT(obj)    GET_DECODER(obj)->context
#define GET_VA_DISPLAY(obj) GET_DECODER(obj)->va_display
#define GET_VA_CONTEXT(obj) GET_DECODER(obj)->va_context

static inline void
gst_video_codec_frame_clear (GstVideoCodecFrame ** frame_ptr)
{
  if (!*frame_ptr)
    return;
  gst_video_codec_frame_unref (*frame_ptr);
  *frame_ptr = NULL;
}

/* ------------------------------------------------------------------------- */
/* --- Pictures                                                          --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiPicture, gst_vaapi_picture);

enum
{
  GST_VAAPI_CREATE_PICTURE_FLAG_CLONE = 1 << 0,
  GST_VAAPI_CREATE_PICTURE_FLAG_FIELD = 1 << 1,
};

void
gst_vaapi_picture_destroy (GstVaapiPicture * picture)
{
  if (picture->slices) {
    g_ptr_array_unref (picture->slices);
    picture->slices = NULL;
  }

  gst_vaapi_codec_object_replace (&picture->iq_matrix, NULL);
  gst_vaapi_codec_object_replace (&picture->huf_table, NULL);
  gst_vaapi_codec_object_replace (&picture->bitplane, NULL);
  gst_vaapi_codec_object_replace (&picture->prob_table, NULL);

  if (picture->proxy) {
    gst_vaapi_surface_proxy_unref (picture->proxy);
    picture->proxy = NULL;
  }
  picture->surface_id = VA_INVALID_ID;
  picture->surface = NULL;

  vaapi_destroy_buffer (GET_VA_DISPLAY (picture), &picture->param_id);
  picture->param = NULL;

  gst_video_codec_frame_clear (&picture->frame);
  gst_vaapi_picture_replace (&picture->parent_picture, NULL);
}

gboolean
gst_vaapi_picture_create (GstVaapiPicture * picture,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  gboolean success;

  picture->param_id = VA_INVALID_ID;

  if (args->flags & GST_VAAPI_CREATE_PICTURE_FLAG_CLONE) {
    GstVaapiPicture *const parent_picture = GST_VAAPI_PICTURE (args->data);

    picture->parent_picture = gst_vaapi_picture_ref (parent_picture);

    picture->proxy = gst_vaapi_surface_proxy_ref (parent_picture->proxy);
    picture->type = parent_picture->type;
    picture->pts = parent_picture->pts;
    picture->poc = parent_picture->poc;
    picture->voc = parent_picture->voc;
    picture->view_id = parent_picture->view_id;

    // Copy all picture flags but "output"
    GST_VAAPI_PICTURE_FLAG_SET (picture,
        GST_VAAPI_PICTURE_FLAGS (parent_picture) &
        (GST_VAAPI_PICTURE_FLAG_SKIPPED |
            GST_VAAPI_PICTURE_FLAG_REFERENCE |
            GST_VAAPI_PICTURE_FLAG_INTERLACED |
            GST_VAAPI_PICTURE_FLAG_FF | GST_VAAPI_PICTURE_FLAG_TFF |
            GST_VAAPI_PICTURE_FLAG_ONEFIELD |
            GST_VAAPI_PICTURE_FLAG_RFF | GST_VAAPI_PICTURE_FLAG_MVC));

    // Propagate "corrupted" flag while not presuming that the second
    // field is itself corrupted if the first one was marked as such
    if (GST_VAAPI_PICTURE_IS_CORRUPTED (parent_picture) &&
        !(args->flags & GST_VAAPI_CREATE_PICTURE_FLAG_FIELD))
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_CORRUPTED);

    picture->structure = parent_picture->structure;
    if ((args->flags & GST_VAAPI_CREATE_PICTURE_FLAG_FIELD) &&
        GST_VAAPI_PICTURE_IS_INTERLACED (picture)) {
      switch (picture->structure) {
        case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
          picture->structure = GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD;
          break;
        case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
          picture->structure = GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD;
          break;
      }
      GST_VAAPI_PICTURE_FLAG_UNSET (picture, GST_VAAPI_PICTURE_FLAG_FF);
    }

    if (parent_picture->has_crop_rect) {
      picture->has_crop_rect = TRUE;
      picture->crop_rect = parent_picture->crop_rect;
    }
  } else {
    picture->type = GST_VAAPI_PICTURE_TYPE_NONE;
    picture->pts = GST_CLOCK_TIME_NONE;

    picture->proxy =
        gst_vaapi_context_get_surface_proxy (GET_CONTEXT (picture));
    if (!picture->proxy)
      return FALSE;

    picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_FF);
  }
  picture->surface = GST_VAAPI_SURFACE_PROXY_SURFACE (picture->proxy);
  picture->surface_id = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (picture->proxy);

  success = vaapi_create_buffer (GET_VA_DISPLAY (picture),
      GET_VA_CONTEXT (picture), VAPictureParameterBufferType,
      args->param_size, args->param, &picture->param_id, &picture->param);
  if (!success)
    return FALSE;
  picture->param_size = args->param_size;

  picture->slices = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_vaapi_mini_object_unref);
  if (!picture->slices)
    return FALSE;

  picture->frame =
      gst_video_codec_frame_ref (GST_VAAPI_DECODER_CODEC_FRAME (GET_DECODER
          (picture)));
  return TRUE;
}

GstVaapiPicture *
gst_vaapi_picture_new (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiPictureClass,
      GST_VAAPI_CODEC_BASE (decoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_PICTURE_CAST (object);
}

GstVaapiPicture *
gst_vaapi_picture_new_field (GstVaapiPicture * picture)
{
  GstVaapiDecoder *const decoder = GET_DECODER (picture);
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (gst_vaapi_codec_object_get_class
      (&picture->parent_instance), GST_VAAPI_CODEC_BASE (decoder), NULL,
      picture->param_size, picture, 0,
      (GST_VAAPI_CREATE_PICTURE_FLAG_CLONE |
          GST_VAAPI_CREATE_PICTURE_FLAG_FIELD));
  if (!object)
    return NULL;
  return GST_VAAPI_PICTURE_CAST (object);
}

GstVaapiPicture *
gst_vaapi_picture_new_clone (GstVaapiPicture * picture)
{
  GstVaapiDecoder *const decoder = GET_DECODER (picture);
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (gst_vaapi_codec_object_get_class
      (&picture->parent_instance), GST_VAAPI_CODEC_BASE (decoder), NULL,
      picture->param_size, picture, 0, GST_VAAPI_CREATE_PICTURE_FLAG_CLONE);
  if (!object)
    return NULL;
  return GST_VAAPI_PICTURE_CAST (object);
}

void
gst_vaapi_picture_add_slice (GstVaapiPicture * picture, GstVaapiSlice * slice)
{
  g_return_if_fail (GST_VAAPI_IS_PICTURE (picture));
  g_return_if_fail (GST_VAAPI_IS_SLICE (slice));

  g_ptr_array_add (picture->slices, slice);
}

static gboolean
do_decode (VADisplay dpy, VAContextID ctx, VABufferID * buf_id, void **buf_ptr)
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
gst_vaapi_picture_decode_with_surface_id (GstVaapiPicture * picture,
    VASurfaceID surface_id)
{
  GstVaapiIqMatrix *iq_matrix;
  GstVaapiBitPlane *bitplane;
  GstVaapiHuffmanTable *huf_table;
  GstVaapiProbabilityTable *prob_table;
  VADisplay va_display;
  VAContextID va_context;
  VAStatus status;
  guint i;

  g_return_val_if_fail (GST_VAAPI_IS_PICTURE (picture), FALSE);
  g_return_val_if_fail (surface_id != VA_INVALID_SURFACE, FALSE);

  va_display = GET_VA_DISPLAY (picture);
  va_context = GET_VA_CONTEXT (picture);

  GST_DEBUG ("decode picture 0x%08x", surface_id);

  status = vaBeginPicture (va_display, va_context, surface_id);
  if (!vaapi_check_status (status, "vaBeginPicture()"))
    return FALSE;

  if (!do_decode (va_display, va_context, &picture->param_id, &picture->param))
    return FALSE;

  iq_matrix = picture->iq_matrix;
  if (iq_matrix && !do_decode (va_display, va_context,
          &iq_matrix->param_id, &iq_matrix->param))
    return FALSE;

  bitplane = picture->bitplane;
  if (bitplane && !do_decode (va_display, va_context,
          &bitplane->data_id, (void **) &bitplane->data))
    return FALSE;

  huf_table = picture->huf_table;
  if (huf_table && !do_decode (va_display, va_context,
          &huf_table->param_id, (void **) &huf_table->param))
    return FALSE;

  prob_table = picture->prob_table;
  if (prob_table && !do_decode (va_display, va_context,
          &prob_table->param_id, (void **) &prob_table->param))
    return FALSE;

  for (i = 0; i < picture->slices->len; i++) {
    GstVaapiSlice *const slice = g_ptr_array_index (picture->slices, i);
    VABufferID va_buffers[2];

    huf_table = slice->huf_table;
    if (huf_table && !do_decode (va_display, va_context,
            &huf_table->param_id, (void **) &huf_table->param))
      return FALSE;

    vaapi_unmap_buffer (va_display, slice->param_id, NULL);
    va_buffers[0] = slice->param_id;
    va_buffers[1] = slice->data_id;

    status = vaRenderPicture (va_display, va_context, va_buffers, 2);
    if (!vaapi_check_status (status, "vaRenderPicture()"))
      return FALSE;
  }

  status = vaEndPicture (va_display, va_context);

  for (i = 0; i < picture->slices->len; i++) {
    GstVaapiSlice *const slice = g_ptr_array_index (picture->slices, i);

    vaapi_destroy_buffer (va_display, &slice->param_id);
    vaapi_destroy_buffer (va_display, &slice->data_id);
  }

  if (!vaapi_check_status (status, "vaEndPicture()"))
    return FALSE;
  return TRUE;
}

gboolean
gst_vaapi_picture_decode (GstVaapiPicture * picture)
{
  g_return_val_if_fail (GST_VAAPI_IS_PICTURE (picture), FALSE);

  return gst_vaapi_picture_decode_with_surface_id (picture,
      picture->surface_id);
}

/* Mark picture as output for internal purposes only. Don't push frame out */
static void
do_output_internal (GstVaapiPicture * picture)
{
  if (GST_VAAPI_PICTURE_IS_OUTPUT (picture))
    return;

  gst_video_codec_frame_clear (&picture->frame);
  GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_OUTPUT);
}

static gboolean
do_output (GstVaapiPicture * picture)
{
  GstVideoCodecFrame *const out_frame = picture->frame;
  GstVaapiSurfaceProxy *proxy;
  guint flags = 0;

  if (GST_VAAPI_PICTURE_IS_OUTPUT (picture))
    return TRUE;

  if (!picture->proxy)
    return FALSE;

  proxy = gst_vaapi_surface_proxy_ref (picture->proxy);

  if (picture->has_crop_rect)
    gst_vaapi_surface_proxy_set_crop_rect (proxy, &picture->crop_rect);

  gst_video_codec_frame_set_user_data (out_frame,
      proxy, (GDestroyNotify) gst_vaapi_mini_object_unref);

  out_frame->pts = picture->pts;

  if (GST_VAAPI_PICTURE_IS_SKIPPED (picture))
    GST_VIDEO_CODEC_FRAME_FLAG_SET (out_frame,
        GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

  if (GST_VAAPI_PICTURE_IS_CORRUPTED (picture))
    flags |= GST_VAAPI_SURFACE_PROXY_FLAG_CORRUPTED;

  if (GST_VAAPI_PICTURE_IS_MVC (picture)) {
    if (picture->voc == 0)
      flags |= GST_VAAPI_SURFACE_PROXY_FLAG_FFB;
    GST_VAAPI_SURFACE_PROXY_VIEW_ID (proxy) = picture->view_id;
  }

  if (GST_VAAPI_PICTURE_IS_INTERLACED (picture)) {
    flags |= GST_VAAPI_SURFACE_PROXY_FLAG_INTERLACED;
    if (GST_VAAPI_PICTURE_IS_TFF (picture))
      flags |= GST_VAAPI_SURFACE_PROXY_FLAG_TFF;
    if (GST_VAAPI_PICTURE_IS_RFF (picture))
      flags |= GST_VAAPI_SURFACE_PROXY_FLAG_RFF;
    if (GST_VAAPI_PICTURE_IS_ONEFIELD (picture))
      flags |= GST_VAAPI_SURFACE_PROXY_FLAG_ONEFIELD;
  }
  GST_VAAPI_SURFACE_PROXY_FLAG_SET (proxy, flags);

  gst_vaapi_decoder_push_frame (GET_DECODER (picture), out_frame);
  gst_video_codec_frame_clear (&picture->frame);

  GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_OUTPUT);
  return TRUE;
}

gboolean
gst_vaapi_picture_output (GstVaapiPicture * picture)
{
  g_return_val_if_fail (GST_VAAPI_IS_PICTURE (picture), FALSE);

  if (G_UNLIKELY (picture->parent_picture)) {
    /* Emit the first field to GstVideoDecoder so that to release
       the underlying GstVideoCodecFrame. However, mark this
       picture as skipped so that to not display it */
    GstVaapiPicture *const parent_picture = picture->parent_picture;
    do {
      if (!GST_VAAPI_PICTURE_IS_INTERLACED (parent_picture))
        break;
      if (!GST_VAAPI_PICTURE_IS_FIRST_FIELD (parent_picture))
        break;
      if (parent_picture->frame == picture->frame)
        do_output_internal (parent_picture);
      else {
        GST_VAAPI_PICTURE_FLAG_SET (parent_picture,
            GST_VAAPI_PICTURE_FLAG_SKIPPED);
        if (!do_output (parent_picture))
          return FALSE;
      }
    } while (0);
  }
  return do_output (picture);
}

void
gst_vaapi_picture_set_crop_rect (GstVaapiPicture * picture,
    const GstVaapiRectangle * crop_rect)
{
  g_return_if_fail (GST_VAAPI_IS_PICTURE (picture));

  picture->has_crop_rect = crop_rect != NULL;
  if (picture->has_crop_rect)
    picture->crop_rect = *crop_rect;
}

/* ------------------------------------------------------------------------- */
/* --- Slices                                                            --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiSlice, gst_vaapi_slice);

void
gst_vaapi_slice_destroy (GstVaapiSlice * slice)
{
  VADisplay const va_display = GET_VA_DISPLAY (slice);

  gst_vaapi_codec_object_replace (&slice->huf_table, NULL);

  vaapi_destroy_buffer (va_display, &slice->data_id);
  vaapi_destroy_buffer (va_display, &slice->param_id);
  slice->param = NULL;
}

gboolean
gst_vaapi_slice_create (GstVaapiSlice * slice,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  VASliceParameterBufferBase *slice_param;
  gboolean success;

  slice->param_id = VA_INVALID_ID;
  slice->data_id = VA_INVALID_ID;

  success = vaapi_create_buffer (GET_VA_DISPLAY (slice), GET_VA_CONTEXT (slice),
      VASliceDataBufferType, args->data_size, args->data, &slice->data_id,
      NULL);
  if (!success)
    return FALSE;

  g_assert (args->param_num >= 1);
  success = vaapi_create_n_elements_buffer (GET_VA_DISPLAY (slice),
      GET_VA_CONTEXT (slice), VASliceParameterBufferType, args->param_size,
      args->param, &slice->param_id, &slice->param, args->param_num);
  if (!success)
    return FALSE;

  slice_param = slice->param;
  slice_param->slice_data_size = args->data_size;
  slice_param->slice_data_offset = 0;
  slice_param->slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
  return TRUE;
}

GstVaapiSlice *
gst_vaapi_slice_new (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size, const guchar * data, guint data_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiSliceClass,
      GST_VAAPI_CODEC_BASE (decoder), param, param_size, data, data_size, 0);
  return GST_VAAPI_SLICE_CAST (object);
}

GstVaapiSlice *
gst_vaapi_slice_new_n_params (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size, guint param_num, const guchar * data,
    guint data_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new_with_param_num (&GstVaapiSliceClass,
      GST_VAAPI_CODEC_BASE (decoder), param, param_size, param_num, data,
      data_size, 0);
  return GST_VAAPI_SLICE_CAST (object);
}
