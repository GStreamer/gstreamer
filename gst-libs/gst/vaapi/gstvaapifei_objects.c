/*
 *  gstvaapifei_objects.c - VA FEI objects abstraction
 *
 *  Copyright (C) 2017-2018 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
#include "gstvaapiencoder_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapifei_objects.h"
#include "gstvaapifei_objects_priv.h"
#define DEBUG 1
#include "gstvaapidebug.h"

/* ------------------------------------------------------------------------- */
/* --- Base Codec Object                                                 --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_FEI_CODEC_OBJECT_GET_CLASS(object) \
    gst_vaapi_fei_codec_object_get_class(object)

const GstVaapiFeiCodecObjectClass *
gst_vaapi_fei_codec_object_get_class (GstVaapiFeiCodecObject * object)
{
  return (const GstVaapiFeiCodecObjectClass *)
      GST_VAAPI_MINI_OBJECT_GET_CLASS (object);
}

static gboolean
gst_vaapi_fei_codec_object_create (GstVaapiFeiCodecObject * object,
    const GstVaapiFeiCodecObjectConstructorArgs * args)
{
  const GstVaapiFeiCodecObjectClass *klass;

  g_return_val_if_fail (args->param_size > 0, FALSE);

  if (GST_VAAPI_MINI_OBJECT_FLAG_IS_SET (object,
          GST_VAAPI_FEI_CODEC_OBJECT_FLAG_CONSTRUCTED))
    return TRUE;

  klass = GST_VAAPI_FEI_CODEC_OBJECT_GET_CLASS (object);
  if (!klass->create || !klass->create (object, args))
    return FALSE;

  GST_VAAPI_MINI_OBJECT_FLAG_SET (object,
      GST_VAAPI_FEI_CODEC_OBJECT_FLAG_CONSTRUCTED);
  return TRUE;
}

GstVaapiFeiCodecObject *
gst_vaapi_fei_codec_object_new (const GstVaapiFeiCodecObjectClass *
    object_class, GstVaapiFeiCodecBase * codec, gconstpointer param,
    guint param_size, gconstpointer data, guint data_size, guint flags)
{
  GstVaapiFeiCodecObject *obj;
  GstVaapiFeiCodecObjectConstructorArgs args;

  obj = (GstVaapiFeiCodecObject *)
      gst_vaapi_mini_object_new0 (GST_VAAPI_MINI_OBJECT_CLASS (object_class));
  if (!obj)
    return NULL;

  obj = GST_VAAPI_FEI_CODEC_OBJECT (obj);
  obj->codec = codec;

  args.param = param;
  args.param_size = param_size;
  args.data = data;
  args.data_size = data_size;
  args.flags = flags;

  if (gst_vaapi_fei_codec_object_create (obj, &args))
    return obj;

  gst_vaapi_fei_codec_object_unref (obj);
  return NULL;
}

GstVaapiFeiCodecObject *
gst_vaapi_fei_codec_object_ref (GstVaapiFeiCodecObject * object)
{
  return ((GstVaapiFeiCodecObject *)
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (object)));
}

void
gst_vaapi_fei_codec_object_unref (GstVaapiFeiCodecObject * object)
{
  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (object));
}

void
gst_vaapi_fei_codec_object_replace (GstVaapiFeiCodecObject ** old_object_ptr,
    GstVaapiFeiCodecObject * new_object)
{
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (old_object_ptr),
      GST_VAAPI_MINI_OBJECT (new_object));
}

/* FeiFixme: map_unlocked and map_lock could be needed */
gboolean
gst_vaapi_fei_codec_object_map (GstVaapiFeiCodecObject * object,
    gpointer * data, guint * size)
{
  g_return_val_if_fail (object != NULL, FALSE);

  /*FeiFixme: explicit map if not yet mapped */
  *data = object->param;
  *size = object->param_size;

  return TRUE;
}

void
gst_vaapi_fei_codec_object_unmap (GstVaapiFeiCodecObject * object)
{
  g_return_if_fail (object != NULL);
  vaapi_unmap_buffer (GST_VAAPI_ENCODER_CAST (object->codec)->va_display,
      object->param_id, &object->param);
}

#define GET_ENCODER(obj)    GST_VAAPI_ENCODER_CAST((obj)->parent_instance.codec)
#define GET_VA_DISPLAY(obj) GET_ENCODER(obj)->va_display
#define GET_VA_CONTEXT(obj) GET_ENCODER(obj)->va_context

/* ------------------------------------------------------------------------- */
/* ---  FEI Mb Code buffer                                               --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_FEI_CODEC_DEFINE_TYPE (GstVaapiEncFeiMbCode,
    gst_vaapi_enc_fei_mb_code);

void
gst_vaapi_enc_fei_mb_code_destroy (GstVaapiEncFeiMbCode * fei_mb_code)
{
  GstVaapiFeiCodecObject *object = &fei_mb_code->parent_instance;
  vaapi_destroy_buffer (GET_VA_DISPLAY (fei_mb_code), &object->param_id);
  object->param = NULL;
}

gboolean
gst_vaapi_enc_fei_mb_code_create (GstVaapiEncFeiMbCode *
    fei_mb_code, const GstVaapiFeiCodecObjectConstructorArgs * args)
{
  GstVaapiFeiCodecObject *object = &fei_mb_code->parent_instance;
  object->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (fei_mb_code),
      GET_VA_CONTEXT (fei_mb_code),
      (VABufferType) VAEncFEIMBCodeBufferType, args->param_size,
      args->param, &object->param_id, &object->param);
}

GstVaapiEncFeiMbCode *
gst_vaapi_enc_fei_mb_code_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiFeiCodecObject *object;

  object = gst_vaapi_fei_codec_object_new (&GstVaapiEncFeiMbCodeClass,
      GST_VAAPI_FEI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  object->param_size = param_size;
  return GST_VAAPI_ENC_FEI_MB_CODE_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* ---  FEI MV buffer                                                    --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_FEI_CODEC_DEFINE_TYPE (GstVaapiEncFeiMv, gst_vaapi_enc_fei_mv);

void
gst_vaapi_enc_fei_mv_destroy (GstVaapiEncFeiMv * fei_mv)
{
  GstVaapiFeiCodecObject *object = &fei_mv->parent_instance;
  vaapi_destroy_buffer (GET_VA_DISPLAY (fei_mv), &object->param_id);
  object->param = NULL;
}

gboolean
gst_vaapi_enc_fei_mv_create (GstVaapiEncFeiMv *
    fei_mv, const GstVaapiFeiCodecObjectConstructorArgs * args)
{
  GstVaapiFeiCodecObject *object = &fei_mv->parent_instance;
  object->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (fei_mv),
      GET_VA_CONTEXT (fei_mv),
      (VABufferType) VAEncFEIMVBufferType, args->param_size,
      args->param, &object->param_id, &object->param);
}

GstVaapiEncFeiMv *
gst_vaapi_enc_fei_mv_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiFeiCodecObject *object;

  object = gst_vaapi_fei_codec_object_new (&GstVaapiEncFeiMvClass,
      GST_VAAPI_FEI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  object->param_size = param_size;
  return GST_VAAPI_ENC_FEI_NEW_MV_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* ---  FEI Mv predictor buffer                                          --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_FEI_CODEC_DEFINE_TYPE (GstVaapiEncFeiMvPredictor,
    gst_vaapi_enc_fei_mv_predictor);

void
gst_vaapi_enc_fei_mv_predictor_destroy (GstVaapiEncFeiMvPredictor *
    fei_mv_predictor)
{
  GstVaapiFeiCodecObject *object = &fei_mv_predictor->parent_instance;
  vaapi_destroy_buffer (GET_VA_DISPLAY (fei_mv_predictor), &object->param_id);
  object->param = NULL;
}

gboolean
gst_vaapi_enc_fei_mv_predictor_create (GstVaapiEncFeiMvPredictor *
    fei_mv_predictor, const GstVaapiFeiCodecObjectConstructorArgs * args)
{
  GstVaapiFeiCodecObject *object = &fei_mv_predictor->parent_instance;
  object->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (fei_mv_predictor),
      GET_VA_CONTEXT (fei_mv_predictor),
      (VABufferType) VAEncFEIMVPredictorBufferType, args->param_size,
      args->param, &object->param_id, &object->param);
}

GstVaapiEncFeiMvPredictor *
gst_vaapi_enc_fei_mv_predictor_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiFeiCodecObject *object;

  object = gst_vaapi_fei_codec_object_new (&GstVaapiEncFeiMvPredictorClass,
      GST_VAAPI_FEI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  object->param_size = param_size;
  return GST_VAAPI_ENC_FEI_NEW_MV_PREDICTOR_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* ---  FEI Mb Control  buffer                                           --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_FEI_CODEC_DEFINE_TYPE (GstVaapiEncFeiMbControl,
    gst_vaapi_enc_fei_mb_control);

void
gst_vaapi_enc_fei_mb_control_destroy (GstVaapiEncFeiMbControl * fei_mb_control)
{
  GstVaapiFeiCodecObject *object = &fei_mb_control->parent_instance;
  vaapi_destroy_buffer (GET_VA_DISPLAY (fei_mb_control), &object->param_id);
  object->param = NULL;
}

gboolean
gst_vaapi_enc_fei_mb_control_create (GstVaapiEncFeiMbControl *
    fei_mb_control, const GstVaapiFeiCodecObjectConstructorArgs * args)
{
  GstVaapiFeiCodecObject *object = &fei_mb_control->parent_instance;
  object->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (fei_mb_control),
      GET_VA_CONTEXT (fei_mb_control),
      (VABufferType) VAEncFEIMBControlBufferType, args->param_size,
      args->param, &object->param_id, &object->param);
}

GstVaapiEncFeiMbControl *
gst_vaapi_enc_fei_mb_control_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiFeiCodecObject *object;

  object = gst_vaapi_fei_codec_object_new (&GstVaapiEncFeiMbControlClass,
      GST_VAAPI_FEI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  object->param_size = param_size;
  return GST_VAAPI_ENC_FEI_NEW_MB_CONTROL_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* ---  FEI qp buffer                                                    --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_FEI_CODEC_DEFINE_TYPE (GstVaapiEncFeiQp, gst_vaapi_enc_fei_qp);

void
gst_vaapi_enc_fei_qp_destroy (GstVaapiEncFeiQp * fei_qp)
{
  GstVaapiFeiCodecObject *object = &fei_qp->parent_instance;
  vaapi_destroy_buffer (GET_VA_DISPLAY (fei_qp), &object->param_id);
  object->param = NULL;
}

gboolean
gst_vaapi_enc_fei_qp_create (GstVaapiEncFeiQp * fei_qp,
    const GstVaapiFeiCodecObjectConstructorArgs * args)
{
  GstVaapiFeiCodecObject *object = &fei_qp->parent_instance;
  object->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (fei_qp),
      GET_VA_CONTEXT (fei_qp),
      (VABufferType) VAEncQPBufferType, args->param_size,
      args->param, &object->param_id, &object->param);
}

GstVaapiEncFeiQp *
gst_vaapi_enc_fei_qp_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiFeiCodecObject *object;

  object = gst_vaapi_fei_codec_object_new (&GstVaapiEncFeiQpClass,
      GST_VAAPI_FEI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  object->param_size = param_size;
  return GST_VAAPI_ENC_FEI_NEW_QP_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* ---  FEI Distortion buffer                                            --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_FEI_CODEC_DEFINE_TYPE (GstVaapiEncFeiDistortion,
    gst_vaapi_enc_fei_distortion);

void
gst_vaapi_enc_fei_distortion_destroy (GstVaapiEncFeiDistortion * fei_dist)
{
  GstVaapiFeiCodecObject *object = &fei_dist->parent_instance;
  vaapi_destroy_buffer (GET_VA_DISPLAY (fei_dist), &object->param_id);
  object->param = NULL;
}

gboolean
gst_vaapi_enc_fei_distortion_create (GstVaapiEncFeiDistortion * fei_dist,
    const GstVaapiFeiCodecObjectConstructorArgs * args)
{
  GstVaapiFeiCodecObject *object = &fei_dist->parent_instance;
  object->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (fei_dist),
      GET_VA_CONTEXT (fei_dist),
      (VABufferType) VAEncFEIDistortionBufferType, args->param_size,
      args->param, &object->param_id, &object->param);
}

GstVaapiEncFeiDistortion *
gst_vaapi_enc_fei_distortion_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size)
{
  GstVaapiFeiCodecObject *object;

  object = gst_vaapi_fei_codec_object_new (&GstVaapiEncFeiDistortionClass,
      GST_VAAPI_FEI_CODEC_BASE (encoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  object->param_size = param_size;
  return GST_VAAPI_ENC_FEI_NEW_DISTORTION_CAST (object);
}
