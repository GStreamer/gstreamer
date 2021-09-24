/*
 *  gstvaapicodec_objects.c - VA codec objects abstraction
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
#include "gstvaapicodec_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* ------------------------------------------------------------------------- */
/* --- Base Codec Object                                                 --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_CODEC_OBJECT_GET_CLASS(object) \
    gst_vaapi_codec_object_get_class(object)

const GstVaapiCodecObjectClass *
gst_vaapi_codec_object_get_class (GstVaapiCodecObject * object)
{
  return (const GstVaapiCodecObjectClass *)
      GST_VAAPI_MINI_OBJECT_GET_CLASS (object);
}

static gboolean
gst_vaapi_codec_object_create (GstVaapiCodecObject * object,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  const GstVaapiCodecObjectClass *klass;

  g_return_val_if_fail (args->param_size > 0, FALSE);

  if (GST_VAAPI_MINI_OBJECT_FLAG_IS_SET (object,
          GST_VAAPI_CODEC_OBJECT_FLAG_CONSTRUCTED))
    return TRUE;

  klass = GST_VAAPI_CODEC_OBJECT_GET_CLASS (object);
  if (!klass->create || !klass->create (object, args))
    return FALSE;

  GST_VAAPI_MINI_OBJECT_FLAG_SET (object,
      GST_VAAPI_CODEC_OBJECT_FLAG_CONSTRUCTED);
  return TRUE;
}

GstVaapiCodecObject *
gst_vaapi_codec_object_new_with_param_num (const GstVaapiCodecObjectClass *
    object_class, GstVaapiCodecBase * codec, gconstpointer param,
    guint param_size, guint param_num, gconstpointer data,
    guint data_size, guint flags)
{
  GstVaapiCodecObject *obj;
  GstVaapiCodecObjectConstructorArgs args;

  obj = (GstVaapiCodecObject *)
      gst_vaapi_mini_object_new0 (GST_VAAPI_MINI_OBJECT_CLASS (object_class));
  if (!obj)
    return NULL;

  obj = GST_VAAPI_CODEC_OBJECT (obj);
  obj->codec = codec;

  args.param = param;
  args.param_size = param_size;
  args.param_num = param_num;
  args.data = data;
  args.data_size = data_size;
  args.flags = flags;

  if (gst_vaapi_codec_object_create (obj, &args))
    return obj;

  gst_vaapi_codec_object_unref (obj);
  return NULL;
}

GstVaapiCodecObject *
gst_vaapi_codec_object_new (const GstVaapiCodecObjectClass * object_class,
    GstVaapiCodecBase * codec, gconstpointer param, guint param_size,
    gconstpointer data, guint data_size, guint flags)
{
  return gst_vaapi_codec_object_new_with_param_num (object_class, codec, param,
      param_size, 1, data, data_size, flags);
}

#define GET_DECODER(obj)    GST_VAAPI_DECODER_CAST((obj)->parent_instance.codec)
#define GET_VA_DISPLAY(obj) GET_DECODER(obj)->va_display
#define GET_VA_CONTEXT(obj) GET_DECODER(obj)->va_context

/* ------------------------------------------------------------------------- */
/* --- Inverse Quantization Matrices                                     --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiIqMatrix, gst_vaapi_iq_matrix);

void
gst_vaapi_iq_matrix_destroy (GstVaapiIqMatrix * iq_matrix)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (iq_matrix), &iq_matrix->param_id);
  iq_matrix->param = NULL;
}

gboolean
gst_vaapi_iq_matrix_create (GstVaapiIqMatrix * iq_matrix,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  iq_matrix->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (iq_matrix),
      GET_VA_CONTEXT (iq_matrix), VAIQMatrixBufferType,
      args->param_size, args->param, &iq_matrix->param_id, &iq_matrix->param);
}

GstVaapiIqMatrix *
gst_vaapi_iq_matrix_new (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiIqMatrixClass,
      GST_VAAPI_CODEC_BASE (decoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_IQ_MATRIX_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* --- VC-1 Bit Planes                                                   --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiBitPlane, gst_vaapi_bitplane);

void
gst_vaapi_bitplane_destroy (GstVaapiBitPlane * bitplane)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (bitplane), &bitplane->data_id);
  bitplane->data = NULL;
}

gboolean
gst_vaapi_bitplane_create (GstVaapiBitPlane * bitplane,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  bitplane->data_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (bitplane),
      GET_VA_CONTEXT (bitplane), VABitPlaneBufferType, args->param_size,
      args->param, &bitplane->data_id, (void **) &bitplane->data);
}


GstVaapiBitPlane *
gst_vaapi_bitplane_new (GstVaapiDecoder * decoder, guint8 * data,
    guint data_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiBitPlaneClass,
      GST_VAAPI_CODEC_BASE (decoder), data, data_size, NULL, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_BITPLANE_CAST (object);
}

/* ------------------------------------------------------------------------- */
/* --- JPEG Huffman Tables                                               --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiHuffmanTable, gst_vaapi_huffman_table);

void
gst_vaapi_huffman_table_destroy (GstVaapiHuffmanTable * huf_table)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (huf_table), &huf_table->param_id);
  huf_table->param = NULL;
}

gboolean
gst_vaapi_huffman_table_create (GstVaapiHuffmanTable * huf_table,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  huf_table->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (huf_table),
      GET_VA_CONTEXT (huf_table), VAHuffmanTableBufferType, args->param_size,
      args->param, &huf_table->param_id, (void **) &huf_table->param);
}

GstVaapiHuffmanTable *
gst_vaapi_huffman_table_new (GstVaapiDecoder * decoder,
    guint8 * data, guint data_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiHuffmanTableClass,
      GST_VAAPI_CODEC_BASE (decoder), data, data_size, NULL, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_HUFFMAN_TABLE_CAST (object);
}

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiProbabilityTable,
    gst_vaapi_probability_table);

void
gst_vaapi_probability_table_destroy (GstVaapiProbabilityTable * prob_table)
{
  vaapi_destroy_buffer (GET_VA_DISPLAY (prob_table), &prob_table->param_id);
  prob_table->param = NULL;
}

gboolean
gst_vaapi_probability_table_create (GstVaapiProbabilityTable * prob_table,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  prob_table->param_id = VA_INVALID_ID;
  return vaapi_create_buffer (GET_VA_DISPLAY (prob_table),
      GET_VA_CONTEXT (prob_table),
      VAProbabilityBufferType,
      args->param_size, args->param, &prob_table->param_id, &prob_table->param);
}

GstVaapiProbabilityTable *
gst_vaapi_probability_table_new (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size)
{
  GstVaapiCodecObject *object;

  object = gst_vaapi_codec_object_new (&GstVaapiProbabilityTableClass,
      GST_VAAPI_CODEC_BASE (decoder), param, param_size, NULL, 0, 0);
  if (!object)
    return NULL;
  return GST_VAAPI_PROBABILITY_TABLE_CAST (object);
}
