/*
 *  gstvaapiencoder_objects.h - VA encoder objects abstraction
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

#ifndef GST_VAAPI_ENCODER_OBJECTS_H
#define GST_VAAPI_ENCODER_OBJECTS_H

#include <gst/vaapi/gstvaapicodec_objects.h>
#include <gst/vaapi/gstvaapidecoder_objects.h>
#include <gst/vaapi/gstvaapiencoder.h>

G_BEGIN_DECLS

typedef struct _GstVaapiEncPicture GstVaapiEncPicture;
typedef struct _GstVaapiEncSequence GstVaapiEncSequence;
typedef struct _GstVaapiEncMiscParam GstVaapiEncMiscParam;
typedef struct _GstVaapiEncSlice GstVaapiEncSlice;
typedef struct _GstVaapiEncQMatrix GstVaapiEncQMatrix;
typedef struct _GstVaapiEncHuffmanTable GstVaapiEncHuffmanTable;
typedef struct _GstVaapiEncPackedHeader GstVaapiEncPackedHeader;

/* ------------------------------------------------------------------------- */
/* --- Encoder Packed Header                                             --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_PACKED_HEADER(obj) \
  ((GstVaapiEncPackedHeader *) (obj))

/**
 * GstVaapiEncPackedHeader:
 *
 * A #GstVaapiCodecObject holding a packed header (param/data) for the
 * encoder.
 */
struct _GstVaapiEncPackedHeader
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;

  /*< public >*/
  VABufferID param_id;
  gpointer param;
  VABufferID data_id;
  gpointer data;
};

G_GNUC_INTERNAL
GstVaapiEncPackedHeader *
gst_vaapi_enc_packed_header_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size, gconstpointer data, guint data_size);

G_GNUC_INTERNAL
gboolean
gst_vaapi_enc_packed_header_set_data (GstVaapiEncPackedHeader * header,
    gconstpointer data, guint data_size);

/* ------------------------------------------------------------------------- */
/* --- Encoder Sequence                                                  --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_SEQUENCE(obj) \
  ((GstVaapiEncSequence *) (obj))

/**
 * GstVaapiEncSequence:
 *
 * A #GstVaapiCodecObject holding a sequence parameter for encoding.
 */
struct _GstVaapiEncSequence
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;

  /*< public >*/
  VABufferID param_id;
  gpointer param;
};

G_GNUC_INTERNAL
GstVaapiEncSequence *
gst_vaapi_enc_sequence_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size);

/* ------------------------------------------------------------------------- */
/* --- Encoder Slice                                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_SLICE(obj) \
  ((GstVaapiEncSlice *) (obj))

/**
 * GstVaapiEncSlice:
 *
 * A #GstVaapiCodecObject holding a slice parameter used for encoding.
 */
struct _GstVaapiEncSlice
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;

  /*< public >*/
  VABufferID param_id;
  gpointer param;
  GPtrArray *packed_headers;
};

G_GNUC_INTERNAL
GstVaapiEncSlice *
gst_vaapi_enc_slice_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size);

/* ------------------------------------------------------------------------- */
/* --- Encoder Misc Parameter Buffer                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_MISC_PARAM(obj) \
  ((GstVaapiEncMiscParam *) (obj))

/**
 * GstVaapiEncMiscParam:
 *
 * A #GstVaapiCodecObject holding a misc parameter and associated data
 * used for controlling the encoder dynamically.
 */
struct _GstVaapiEncMiscParam
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  gpointer param;

  /*< public >*/
  VABufferID param_id;
  gpointer data;
};

G_GNUC_INTERNAL
GstVaapiEncMiscParam *
gst_vaapi_enc_misc_param_new (GstVaapiEncoder * encoder,
    VAEncMiscParameterType type, guint data_size);

/* ------------------------------------------------------------------------- */
/* ---  Quantization Matrices                                            --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_Q_MATRIX_CAST(obj) \
  ((GstVaapiEncQMatrix *) (obj))

/**
 * GstVaapiEncQMatrix:
 *
 * A #GstVaapiCodecObject holding a quantization matrix parameter.
 */
struct _GstVaapiEncQMatrix
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  VABufferID param_id;

  /*< public >*/
  gpointer param;
};

G_GNUC_INTERNAL
GstVaapiEncQMatrix *
gst_vaapi_enc_q_matrix_new (GstVaapiEncoder * encoder, gconstpointer param,
    guint param_size);

/* ------------------------------------------------------------------------- */
/* --- JPEG Huffman Tables                                               --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_HUFFMAN_TABLE_CAST(obj) \
  ((GstVaapiEncHuffmanTable *) (obj))

/**
 * GstVaapiEncHuffmanTable:
 *
 * A #GstVaapiCodecObject holding huffman table.
 */
struct _GstVaapiEncHuffmanTable
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  VABufferID param_id;

  /*< public >*/
  gpointer param;
};

G_GNUC_INTERNAL
GstVaapiEncHuffmanTable *
gst_vaapi_enc_huffman_table_new (GstVaapiEncoder * encoder, guint8 * data,
    guint data_size);

/* ------------------------------------------------------------------------- */
/* --- Encoder Picture                                                   --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_PICTURE(obj) \
  ((GstVaapiEncPicture *) (obj))

typedef enum
{
  GST_VAAPI_ENC_PICTURE_FLAG_IDR          = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 0),
  GST_VAAPI_ENC_PICTURE_FLAG_REFERENCE    = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 1),
  GST_VAAPI_ENC_PICTURE_FLAG_LAST         = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 2),
} GstVaapiEncPictureFlags;

#define GST_VAAPI_ENC_PICTURE_FLAGS         GST_VAAPI_MINI_OBJECT_FLAGS
#define GST_VAAPI_ENC_PICTURE_FLAG_IS_SET   GST_VAAPI_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_ENC_PICTURE_FLAG_SET      GST_VAAPI_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_ENC_PICTURE_FLAG_UNSET    GST_VAAPI_MINI_OBJECT_FLAG_UNSET

#define GST_VAAPI_ENC_PICTURE_IS_IDR(picture) \
    GST_VAAPI_ENC_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_ENC_PICTURE_FLAG_IDR)

#define GST_VAAPI_ENC_PICTURE_IS_REFRENCE(picture) \
    GST_VAAPI_ENC_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_ENC_PICTURE_FLAG_REFERENCE)

/**
 * GstVaapiEncPicture:
 *
 * A #GstVaapiCodecObject holding a picture parameter for encoding.
 */
struct _GstVaapiEncPicture
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  GstVideoCodecFrame *frame;
  GstVaapiSurfaceProxy *proxy;
  GstVaapiSurface *surface;
  VABufferID param_id;
  guint param_size;

  /* Additional data to pass down */
  GstVaapiEncSequence *sequence;
  GPtrArray *packed_headers;
  GPtrArray *misc_params;

  /*< public >*/
  GstVaapiPictureType type;
  VASurfaceID surface_id;
  gpointer param;
  GPtrArray *slices;
  GstVaapiEncQMatrix *q_matrix;
  GstVaapiEncHuffmanTable *huf_table;
  GstClockTime pts;
  guint frame_num;
  guint poc;
  guint temporal_id;
  gboolean has_roi;
};

G_GNUC_INTERNAL
GstVaapiEncPicture *
gst_vaapi_enc_picture_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size, GstVideoCodecFrame * frame);

G_GNUC_INTERNAL
void
gst_vaapi_enc_picture_set_sequence (GstVaapiEncPicture * picture,
    GstVaapiEncSequence * sequence);

G_GNUC_INTERNAL
void
gst_vaapi_enc_picture_add_packed_header (GstVaapiEncPicture * picture,
    GstVaapiEncPackedHeader * header);

G_GNUC_INTERNAL
void
gst_vaapi_enc_picture_add_misc_param (GstVaapiEncPicture * picture,
    GstVaapiEncMiscParam * misc);

G_GNUC_INTERNAL
void
gst_vaapi_enc_picture_add_slice (GstVaapiEncPicture * picture,
    GstVaapiEncSlice * slice);

G_GNUC_INTERNAL
void
gst_vaapi_enc_slice_add_packed_header (GstVaapiEncSlice *slice,
    GstVaapiEncPackedHeader * header);

G_GNUC_INTERNAL
gboolean
gst_vaapi_enc_picture_encode (GstVaapiEncPicture * picture);

#define gst_vaapi_enc_picture_ref(picture) \
  gst_vaapi_codec_object_ref (picture)
#define gst_vaapi_enc_picture_unref(picture) \
  gst_vaapi_codec_object_unref (picture)
#define gst_vaapi_enc_picture_replace(old_picture_ptr, new_picture) \
  gst_vaapi_codec_object_replace (old_picture_ptr, new_picture)

/* ------------------------------------------------------------------------- */
/* --- Helpers to create codec-dependent objects                         --- */
/* ------------------------------------------------------------------------- */

/* GstVaapiEncSequence */
#define GST_VAAPI_ENC_SEQUENCE_NEW(codec, encoder)                      \
  gst_vaapi_enc_sequence_new (GST_VAAPI_ENCODER_CAST (encoder),         \
      NULL, sizeof (G_PASTE (VAEncSequenceParameterBuffer, codec)))

/* GstVaapiEncMiscParam */
#define GST_VAAPI_ENC_MISC_PARAM_NEW(type, encoder)                     \
  gst_vaapi_enc_misc_param_new (GST_VAAPI_ENCODER_CAST (encoder),       \
      G_PASTE (VAEncMiscParameterType, type),                           \
      sizeof (G_PASTE (VAEncMiscParameter, type)))

/* GstVaapiEncQualityLevelMiscParam */
#define GST_VAAPI_ENC_QUALITY_LEVEL_MISC_PARAM_NEW(encoder)             \
  gst_vaapi_enc_misc_param_new (GST_VAAPI_ENCODER_CAST (encoder),       \
      VAEncMiscParameterTypeQualityLevel,                               \
      sizeof (VAEncMiscParameterBufferQualityLevel))

/* GstVaapiEncQuantizationMiscParam */
#define GST_VAAPI_ENC_QUANTIZATION_MISC_PARAM_NEW(encoder)              \
  gst_vaapi_enc_misc_param_new (GST_VAAPI_ENCODER_CAST (encoder),       \
      VAEncMiscParameterTypeQuantization,                               \
      sizeof (VAEncMiscParameterQuantization))

/* GstVaapiEncPicture  */
#define GST_VAAPI_ENC_PICTURE_NEW(codec, encoder, frame)                \
  gst_vaapi_enc_picture_new (GST_VAAPI_ENCODER_CAST (encoder),          \
      NULL, sizeof (G_PASTE (VAEncPictureParameterBuffer, codec)), frame)

/* GstVaapiEncSlice */
#define GST_VAAPI_ENC_SLICE_NEW(codec, encoder)                         \
  gst_vaapi_enc_slice_new (GST_VAAPI_ENCODER_CAST (encoder),            \
      NULL, sizeof(G_PASTE (VAEncSliceParameterBuffer, codec)))

/* GstVaapiEncQuantMatrix */
#define GST_VAAPI_ENC_Q_MATRIX_NEW(codec, encoder)                      \
  gst_vaapi_enc_q_matrix_new (GST_VAAPI_ENCODER_CAST (encoder),         \
      NULL, sizeof (G_PASTE (VAQMatrixBuffer, codec)))

/* GstVaapiEncHuffmanTable */
#define GST_VAAPI_ENC_HUFFMAN_TABLE_NEW(codec, encoder)                 \
  gst_vaapi_enc_huffman_table_new (GST_VAAPI_ENCODER_CAST (encoder),    \
      NULL, sizeof (G_PASTE (VAHuffmanTableBuffer, codec)))

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_OBJECTS_H */
