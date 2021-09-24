/*
 *  gstvaapicodec_objects.h - VA codec objects abstraction
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

#ifndef GST_VAAPI_CODEC_COMMON_H
#define GST_VAAPI_CODEC_COMMON_H

#include <gst/vaapi/gstvaapiminiobject.h>
#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

typedef gpointer                                GstVaapiCodecBase;
typedef struct _GstVaapiCodecObject             GstVaapiCodecObject;
typedef struct _GstVaapiCodecObjectClass        GstVaapiCodecObjectClass;
typedef struct _GstVaapiIqMatrix                GstVaapiIqMatrix;
typedef struct _GstVaapiBitPlane                GstVaapiBitPlane;
typedef struct _GstVaapiHuffmanTable            GstVaapiHuffmanTable;
typedef struct _GstVaapiProbabilityTable        GstVaapiProbabilityTable;

/* ------------------------------------------------------------------------- */
/* --- Base Codec Object                                                 --- */
/* ------------------------------------------------------------------------- */

/* XXX: remove when a common base class for decoder and encoder is available */
#define GST_VAAPI_CODEC_BASE(obj) \
  ((GstVaapiCodecBase *) (obj))

#define GST_VAAPI_CODEC_OBJECT(obj) \
  ((GstVaapiCodecObject *) (obj))

enum
{
  GST_VAAPI_CODEC_OBJECT_FLAG_CONSTRUCTED = (1 << 0),
  GST_VAAPI_CODEC_OBJECT_FLAG_LAST        = (1 << 1)
};

typedef struct
{
  gconstpointer param;
  guint param_size;
  guint param_num;
  gconstpointer data;
  guint data_size;
  guint flags;
} GstVaapiCodecObjectConstructorArgs;

typedef gboolean
(*GstVaapiCodecObjectCreateFunc)(GstVaapiCodecObject * object,
    const GstVaapiCodecObjectConstructorArgs * args);

typedef GDestroyNotify GstVaapiCodecObjectDestroyFunc;

/**
 * GstVaapiCodecObject:
 *
 * A #GstVaapiMiniObject holding the base codec object data
 */
struct _GstVaapiCodecObject
{
  /*< private >*/
  GstVaapiMiniObject parent_instance;
  GstVaapiCodecBase *codec;
};

/**
 * GstVaapiCodecObjectClass:
 *
 * The #GstVaapiCodecObject base class.
 */
struct _GstVaapiCodecObjectClass
{
  /*< private >*/
  GstVaapiMiniObjectClass parent_class;

  GstVaapiCodecObjectCreateFunc create;
};

G_GNUC_INTERNAL
const GstVaapiCodecObjectClass *
gst_vaapi_codec_object_get_class (GstVaapiCodecObject * object) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiCodecObject *
gst_vaapi_codec_object_new (const GstVaapiCodecObjectClass * object_class,
    GstVaapiCodecBase * codec, gconstpointer param, guint param_size,
    gconstpointer data, guint data_size, guint flags);

G_GNUC_INTERNAL
GstVaapiCodecObject *
gst_vaapi_codec_object_new_with_param_num (const GstVaapiCodecObjectClass *
    object_class, GstVaapiCodecBase * codec, gconstpointer param,
    guint param_size, guint param_num, gconstpointer data,
    guint data_size, guint flags);

#define gst_vaapi_codec_object_ref(object) \
  ((gpointer) gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (object)))

#define gst_vaapi_codec_object_unref(object) \
  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (object))

#define gst_vaapi_codec_object_replace(old_object_ptr, new_object) \
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (old_object_ptr), \
      GST_VAAPI_MINI_OBJECT (new_object))

/* ------------------------------------------------------------------------- */
/* --- Inverse Quantization Matrices                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_IQ_MATRIX_CAST(obj) \
  ((GstVaapiIqMatrix *) (obj))

/**
 * GstVaapiIqMatrix:
 *
 * A #GstVaapiCodecObject holding an inverse quantization matrix parameter.
 */
struct _GstVaapiIqMatrix
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  VABufferID param_id;

  /*< public >*/
  gpointer param;
};

G_GNUC_INTERNAL
GstVaapiIqMatrix *
gst_vaapi_iq_matrix_new (GstVaapiDecoder * decoder, gconstpointer param,
    guint param_size);

/* ------------------------------------------------------------------------- */
/* --- VC-1 Bit Planes                                                   --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_BITPLANE_CAST(obj) \
  ((GstVaapiBitPlane *) (obj))

/**
 * GstVaapiBitPlane:
 *
 * A #GstVaapiCodecObject holding a VC-1 bit plane parameter.
 */
struct _GstVaapiBitPlane
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  VABufferID data_id;

  /*< public >*/
  guint8 *data;
};

G_GNUC_INTERNAL
GstVaapiBitPlane *
gst_vaapi_bitplane_new (GstVaapiDecoder * decoder, guint8 * data,
    guint data_size);

/* ------------------------------------------------------------------------- */
/* --- JPEG Huffman Tables                                               --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_HUFFMAN_TABLE_CAST(obj) \
  ((GstVaapiHuffmanTable *) (obj))

/**
 * GstVaapiHuffmanTable:
 *
 * A #GstVaapiCodecObject holding huffman table.
 */
struct _GstVaapiHuffmanTable
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  VABufferID param_id;

  /*< public >*/
  gpointer param;
};

G_GNUC_INTERNAL
GstVaapiHuffmanTable *
gst_vaapi_huffman_table_new (GstVaapiDecoder * decoder, guint8 * data,
    guint data_size);

/* ------------------------------------------------------------------------- */
/* ---                   Probability (Update) Table                      --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_PROBABILITY_TABLE_CAST(obj) \
    ((GstVaapiProbabilityTable *)(obj))

/**
 * GstVaapiProbabilityTable:
 *
 * A #GstVaapiCodecObject holding an Probability (Update) Table for RAC decoding
 */
struct _GstVaapiProbabilityTable
{
  /*< private > */
  GstVaapiCodecObject parent_instance;
  VABufferID param_id;

  /*< public > */
  gpointer param;
};

G_GNUC_INTERNAL
GstVaapiProbabilityTable *
gst_vaapi_probability_table_new (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size);

/* ------------------------------------------------------------------------- */
/* --- Helpers to create codec-dependent objects                         --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_CODEC_DEFINE_TYPE(type, prefix)                       \
G_GNUC_INTERNAL                                                         \
void                                                                    \
G_PASTE (prefix, _destroy) (type *);                                    \
                                                                        \
G_GNUC_INTERNAL                                                         \
gboolean                                                                \
G_PASTE (prefix, _create) (type *,                                      \
    const GstVaapiCodecObjectConstructorArgs * args);                   \
                                                                        \
static const GstVaapiCodecObjectClass G_PASTE (type, Class) = {         \
  .parent_class = {                                                     \
    .size = sizeof (type),                                              \
    .finalize = (GstVaapiCodecObjectDestroyFunc)                        \
        G_PASTE (prefix, _destroy)                                      \
  },                                                                    \
  .create = (GstVaapiCodecObjectCreateFunc)                             \
      G_PASTE (prefix, _create),                                        \
}

#define GST_VAAPI_IQ_MATRIX_NEW(codec, decoder)                         \
  gst_vaapi_iq_matrix_new (GST_VAAPI_DECODER_CAST (decoder),            \
      NULL, sizeof (G_PASTE (VAIQMatrixBuffer, codec)))

#define GST_VAAPI_BITPLANE_NEW(decoder, size) \
  gst_vaapi_bitplane_new (GST_VAAPI_DECODER_CAST (decoder), NULL, size)

#define GST_VAAPI_HUFFMAN_TABLE_NEW(codec, decoder)                     \
  gst_vaapi_huffman_table_new (GST_VAAPI_DECODER_CAST (decoder),        \
      NULL, sizeof (G_PASTE (VAHuffmanTableBuffer, codec)))

#define GST_VAAPI_PROBABILITY_TABLE_NEW(codec, decoder)                 \
  gst_vaapi_probability_table_new (GST_VAAPI_DECODER_CAST (decoder),    \
      NULL, sizeof (G_PASTE (VAProbabilityDataBuffer, codec)))

G_END_DECLS

#endif /* GST_VAAPI_CODEC_OBJECTS_H */
