/*
 *  gstvaapicodec_objects.c - VA codec objects abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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
#include <string.h>
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

G_DEFINE_TYPE(GstVaapiCodecObject, gst_vaapi_codec_object, GST_TYPE_MINI_OBJECT)

static void
gst_vaapi_codec_object_finalize(GstMiniObject *object)
{
    GstVaapiCodecObject * const obj = GST_VAAPI_CODEC_OBJECT(object);

    obj->codec = NULL;
}

static void
gst_vaapi_codec_object_init(GstVaapiCodecObject *obj)
{
    obj->codec = NULL;
}

static gboolean
gst_vaapi_codec_object_create(
    GstVaapiCodecObject                      *obj,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    obj->codec = args->codec;
    return TRUE;
}

static void
gst_vaapi_codec_object_class_init(GstVaapiCodecObjectClass *klass)
{
    GstMiniObjectClass * const object_class = GST_MINI_OBJECT_CLASS(klass);

    object_class->finalize = gst_vaapi_codec_object_finalize;
    klass->construct       = gst_vaapi_codec_object_create;
}

GstVaapiCodecObject *
gst_vaapi_codec_object_new(
    GType              type,
    GstVaapiCodecBase *codec,
    gconstpointer      param,
    guint              param_size,
    gconstpointer      data,
    guint              data_size
)
{
    GstMiniObject *obj;
    GstVaapiCodecObject *va_obj;
    GstVaapiCodecObjectConstructorArgs args;

    obj = gst_mini_object_new(type);
    if (!obj)
        return NULL;

    va_obj = GST_VAAPI_CODEC_OBJECT(obj);
    args.codec      = codec;
    args.param      = param;
    args.param_size = param_size;
    args.data       = data;
    args.data_size  = data_size;
    args.flags      = 0;
    if (gst_vaapi_codec_object_construct(va_obj, &args))
        return va_obj;

    gst_mini_object_unref(obj);
    return NULL;
}

gboolean
gst_vaapi_codec_object_construct(
    GstVaapiCodecObject                      *obj,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    GstVaapiCodecObjectClass *klass;

    g_return_val_if_fail(GST_VAAPI_CODEC_OBJECT(obj), FALSE);
    g_return_val_if_fail(args->codec != NULL, FALSE);
    g_return_val_if_fail(args->param_size > 0, FALSE);

    if (GST_MINI_OBJECT_FLAG_IS_SET(obj, GST_VAAPI_CODEC_OBJECT_FLAG_CONSTRUCTED))
        return TRUE;

    klass = GST_VAAPI_CODEC_OBJECT_GET_CLASS(obj);
    if (!klass || !klass->construct || !klass->construct(obj, args))
        return FALSE;

    GST_MINI_OBJECT_FLAG_SET(obj, GST_VAAPI_CODEC_OBJECT_FLAG_CONSTRUCTED);
    return TRUE;
}

#define GET_DECODER(obj)    GST_VAAPI_DECODER_CAST((obj)->parent_instance.codec)
#define GET_CONTEXT(obj)    GET_DECODER(obj)->priv->context
#define GET_VA_DISPLAY(obj) GET_DECODER(obj)->priv->va_display
#define GET_VA_CONTEXT(obj) GET_DECODER(obj)->priv->va_context

/* ------------------------------------------------------------------------- */
/* --- Inverse Quantization Matrices                                     --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiIqMatrix,
                            gst_vaapi_iq_matrix,
                            GST_VAAPI_TYPE_CODEC_OBJECT)

static void
gst_vaapi_iq_matrix_destroy(GstVaapiIqMatrix *iq_matrix)
{
    vaapi_destroy_buffer(GET_VA_DISPLAY(iq_matrix), &iq_matrix->param_id);
    iq_matrix->param = NULL;
}

static gboolean
gst_vaapi_iq_matrix_create(
    GstVaapiIqMatrix                         *iq_matrix,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return vaapi_create_buffer(GET_VA_DISPLAY(iq_matrix),
                               GET_VA_CONTEXT(iq_matrix),
                               VAIQMatrixBufferType,
                               args->param_size,
                               args->param,
                               &iq_matrix->param_id,
                               &iq_matrix->param);
}

static void
gst_vaapi_iq_matrix_init(GstVaapiIqMatrix *iq_matrix)
{
    iq_matrix->param    = NULL;
    iq_matrix->param_id = VA_INVALID_ID;
}

GstVaapiIqMatrix *
gst_vaapi_iq_matrix_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size
)
{
    GstVaapiCodecObject *object;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    object = gst_vaapi_codec_object_new(
        GST_VAAPI_TYPE_IQ_MATRIX,
        GST_VAAPI_CODEC_BASE(decoder),
        param, param_size,
        NULL, 0
    );
    if (!object)
        return NULL;
    return GST_VAAPI_IQ_MATRIX_CAST(object);
}

/* ------------------------------------------------------------------------- */
/* --- VC-1 Bit Planes                                                   --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiBitPlane,
                            gst_vaapi_bitplane,
                            GST_VAAPI_TYPE_CODEC_OBJECT)

static void
gst_vaapi_bitplane_destroy(GstVaapiBitPlane *bitplane)
{
    vaapi_destroy_buffer(GET_VA_DISPLAY(bitplane), &bitplane->data_id);
    bitplane->data = NULL;
}

static gboolean
gst_vaapi_bitplane_create(
    GstVaapiBitPlane                         *bitplane,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return vaapi_create_buffer(GET_VA_DISPLAY(bitplane),
                               GET_VA_CONTEXT(bitplane),
                               VABitPlaneBufferType,
                               args->param_size,
                               args->param,
                               &bitplane->data_id,
                               (void **)&bitplane->data);
}

static void
gst_vaapi_bitplane_init(GstVaapiBitPlane *bitplane)
{
    bitplane->data      = NULL;
    bitplane->data_id   = VA_INVALID_ID;
}

GstVaapiBitPlane *
gst_vaapi_bitplane_new(GstVaapiDecoder *decoder, guint8 *data, guint data_size)
{
    GstVaapiCodecObject *object;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    object = gst_vaapi_codec_object_new(
        GST_VAAPI_TYPE_BITPLANE,
        GST_VAAPI_CODEC_BASE(decoder),
        data, data_size,
        NULL, 0
    );
    if (!object)
        return NULL;
    return GST_VAAPI_BITPLANE_CAST(object);
}

/* ------------------------------------------------------------------------- */
/* --- JPEG Huffman Tables                                               --- */
/* ------------------------------------------------------------------------- */

#if USE_JPEG_DECODER
GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiHuffmanTable,
                            gst_vaapi_huffman_table,
                            GST_VAAPI_TYPE_CODEC_OBJECT)

static void
gst_vaapi_huffman_table_destroy(GstVaapiHuffmanTable *huf_table)
{
    vaapi_destroy_buffer(GET_VA_DISPLAY(huf_table), &huf_table->param_id);
    huf_table->param = NULL;
}

static gboolean
gst_vaapi_huffman_table_create(
    GstVaapiHuffmanTable                     *huf_table,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return vaapi_create_buffer(GET_VA_DISPLAY(huf_table),
                               GET_VA_CONTEXT(huf_table),
                               VAHuffmanTableBufferType,
                               args->param_size,
                               args->param,
                               &huf_table->param_id,
                               (void **)&huf_table->param);
}

static void
gst_vaapi_huffman_table_init(GstVaapiHuffmanTable *huf_table)
{
    huf_table->param    = NULL;
    huf_table->param_id = VA_INVALID_ID;
}

GstVaapiHuffmanTable *
gst_vaapi_huffman_table_new(
    GstVaapiDecoder *decoder,
    guint8          *data,
    guint            data_size
)
{
    GstVaapiCodecObject *object;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    object = gst_vaapi_codec_object_new(
        GST_VAAPI_TYPE_HUFFMAN_TABLE,
        GST_VAAPI_CODEC_BASE(decoder),
        data, data_size,
        NULL, 0
    );
    if (!object)
        return NULL;
    return GST_VAAPI_HUFFMAN_TABLE_CAST(object);
}
#endif
