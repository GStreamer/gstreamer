/*
 *  gstvaapiencoder_objects.h - VA encoder objects abstraction
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
typedef struct _GstVaapiCodedBuffer GstVaapiCodedBuffer;
typedef struct _GstVaapiEncPackedHeader GstVaapiEncPackedHeader;

/* ------------------------------------------------------------------------- */
/* --- Encoder Packed Header                                             --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_PACKED_HEADER_CAST(obj) \
    ((GstVaapiEncPackedHeader *)(obj))

#define GST_VAAPI_ENC_PACKED_HEADER(obj)      \
    GST_VAAPI_ENC_PACKED_HEADER_CAST(obj)

#define GST_VAAPI_IS_ENC_PACKED_HEADER(obj)   \
    (GST_VAAPI_ENC_PACKED_HEADER(obj) != NULL)

/**
 * GstVaapiEncPackedHeader:
 *
 * A #GstVaapiCodecObject holding a encoder packed header
 * parameter/data parameter.
 */
struct _GstVaapiEncPackedHeader
{
  /*< private > */
  GstVaapiCodecObject parent_instance;

  /*< public > */
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
gst_vaapi_enc_packed_header_set_data (GstVaapiEncPackedHeader * packed_header,
    gconstpointer data, guint data_size);

/* ------------------------------------------------------------------------- */
/* --- Encoder Sequence                                                  --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_SEQUENCE_CAST(obj) \
    ((GstVaapiEncSequence *)(obj))

#define GST_VAAPI_ENC_SEQUENCE(obj)      \
    GST_VAAPI_ENC_SEQUENCE_CAST(obj)

#define GST_VAAPI_IS_ENC_SEQUENCE(obj)   \
    (GST_VAAPI_ENC_SEQUENCE(obj) != NULL)

/**
 * GstVaapiEncSequence:
 *
 * A #GstVaapiCodecObject holding a encoder sequence parameter.
 */
struct _GstVaapiEncSequence
{
  /*< private > */
  GstVaapiCodecObject parent_instance;

  /*< public > */
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

#define GST_VAAPI_ENC_SLICE_CAST(obj) \
    ((GstVaapiEncSlice *)(obj))

#define GST_VAAPI_ENC_SLICE(obj)      \
    GST_VAAPI_ENC_SLICE_CAST(obj)

#define GST_VAAPI_IS_ENC_SLICE(obj)   \
    (GST_VAAPI_ENC_SLICE(obj) != NULL)

/**
 * GstVaapiEncSlice:
 *
 * A #GstVaapiCodecObject holding a encoder slice parameter.
 */
struct _GstVaapiEncSlice
{
  /*< private > */
  GstVaapiCodecObject parent_instance;

  /*< public > */
  VABufferID param_id;
  gpointer param;
};

G_GNUC_INTERNAL
void
gst_vaapi_enc_slice_destroy (GstVaapiEncSlice * slice);

G_GNUC_INTERNAL
gboolean
gst_vaapi_enc_slice_create (GstVaapiEncSlice * slice,
    const GstVaapiCodecObjectConstructorArgs * args);

G_GNUC_INTERNAL
GstVaapiEncSlice *
gst_vaapi_enc_slice_new (GstVaapiEncoder * encoder,
    gconstpointer param, guint param_size);

/* ------------------------------------------------------------------------- */
/* --- Encoder Misc Parameter Buffer                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_MISC_PARAM_CAST(obj) \
    ((GstVaapiEncMiscParam *)(obj))

#define GST_VAAPI_ENC_MISC_PARAM(obj)      \
    GST_VAAPI_ENC_MISC_PARAM_CAST(obj)

#define GST_VAAPI_IS_ENC_MISC_PARAM(obj)   \
    (GST_VAAPI_ENC_MISC_PARAM(obj) != NULL)

/**
 * GstVaapiEncMiscParam:
 *
 * A #GstVaapiCodecObject holding a encoder misc parameter.
 */
struct _GstVaapiEncMiscParam
{
  /*< private > */
  GstVaapiCodecObject parent_instance;
  gpointer param;

  /*< public > */
  VABufferID param_id;
  gpointer impl;
};

G_GNUC_INTERNAL
GstVaapiEncMiscParam *
gst_vaapi_enc_misc_param_new (GstVaapiEncoder * encoder,
    VAEncMiscParameterType type, guint total_size);

/* ------------------------------------------------------------------------- */
/* --- Encoder Picture                                                   --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_PICTURE_CAST(obj) \
    ((GstVaapiEncPicture *)(obj))

#define GST_VAAPI_ENC_PICTURE(obj) \
    GST_VAAPI_ENC_PICTURE_CAST(obj)

#define GST_VAAPI_IS_ENC_PICTURE(obj) \
    (GST_VAAPI_ENC_PICTURE(obj) != NULL)

typedef enum
{
  GST_VAAPI_ENC_PICTURE_FLAG_IDR    = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 0),
  GST_VAAPI_ENC_PICTURE_FLAG_LAST   = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 1),
} GstVaapiEncPictureFlags;

#define GST_VAAPI_ENC_PICTURE_FLAGS         GST_VAAPI_MINI_OBJECT_FLAGS
#define GST_VAAPI_ENC_PICTURE_FLAG_IS_SET   GST_VAAPI_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_ENC_PICTURE_FLAG_SET      GST_VAAPI_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_ENC_PICTURE_FLAG_UNSET    GST_VAAPI_MINI_OBJECT_FLAG_UNSET

#define GST_VAAPI_ENC_PICTURE_IS_IDR(picture) \
    GST_VAAPI_ENC_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_ENC_PICTURE_FLAG_IDR)

#define GST_VAAPI_ENC_PICTURE_GET_FRAME(picture) \
    (picture)->frame

/**
 * GstVaapiEncPicture:
 *
 * A #GstVaapiCodecObject holding a picture parameter.
 */
struct _GstVaapiEncPicture
{
  /*< private > */
  GstVaapiCodecObject parent_instance;
  GstVideoCodecFrame *frame;
  GstVaapiSurfaceProxy *proxy;
  GstVaapiSurface *surface;
  GstVaapiEncSequence *sequence;
  /*< private >, picture packed header */
  GPtrArray *packed_headers;
  GPtrArray *misc_buffers;
  GPtrArray *slices;
  VABufferID param_id;
  guint param_size;

  /*< public > */
  GstVaapiPictureType type;
  VASurfaceID surface_id;
  gpointer param;
  GstClockTime pts;
  guint frame_num;
  guint poc;
};

G_GNUC_INTERNAL
void
gst_vaapi_enc_picture_destroy (GstVaapiEncPicture * picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_enc_picture_create (GstVaapiEncPicture * picture,
    const GstVaapiCodecObjectConstructorArgs * args);

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
gst_vaapi_enc_picture_add_misc_buffer (GstVaapiEncPicture * picture,
    GstVaapiEncMiscParam * misc);

G_GNUC_INTERNAL
void
gst_vaapi_enc_picture_add_slice (GstVaapiEncPicture * picture,
    GstVaapiEncSlice * slice);

G_GNUC_INTERNAL
gboolean
gst_vaapi_enc_picture_encode (GstVaapiEncPicture * picture);

static inline gpointer
gst_vaapi_enc_picture_ref (gpointer ptr)
{
  return gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (ptr));
}

static inline void
gst_vaapi_enc_picture_unref (gpointer ptr)
{
  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (ptr));
}

#define gst_vaapi_enc_picture_replace(old_picture_p, new_picture)       \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_picture_p), \
        (GstVaapiMiniObject *)(new_picture))

/* GST_VAAPI_CODED_BUFFER_NEW  */
#define GST_VAAPI_CODED_BUFFER_NEW(encoder, size)                       \
        gst_vaapi_coded_buffer_new(GST_VAAPI_ENCODER_CAST(encoder),     \
                                NULL, size)

/* GST_VAAPI_ENC_SEQUENCE_NEW */
#define GST_VAAPI_ENC_SEQUENCE_NEW(codec, encoder)                      \
        gst_vaapi_enc_sequence_new(GST_VAAPI_ENCODER_CAST(encoder),     \
                  NULL, sizeof(VAEncSequenceParameterBuffer##codec))

/* GST_VAAPI_ENC_SLICE_NEW */
#define GST_VAAPI_ENC_SLICE_NEW(codec, encoder)                         \
        gst_vaapi_enc_slice_new(GST_VAAPI_ENCODER_CAST(encoder),        \
                   NULL, sizeof(VAEncSliceParameterBuffer##codec))

/* GST_VAAPI_ENC_MISC_PARAM_NEW */
#define GST_VAAPI_ENC_MISC_PARAM_NEW(type, encoder)                     \
        gst_vaapi_enc_misc_param_new(GST_VAAPI_ENCODER_CAST(encoder),   \
           VAEncMiscParameterType##type,                                \
         (sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameter##type)))

/* GST_VAAPI_ENC_PICTURE_NEW  */
#define GST_VAAPI_ENC_PICTURE_NEW(codec, encoder, frame)                \
        gst_vaapi_enc_picture_new(GST_VAAPI_ENCODER_CAST(encoder),      \
            NULL, sizeof(VAEncPictureParameterBuffer##codec), frame)

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_OBJECTS_H */
