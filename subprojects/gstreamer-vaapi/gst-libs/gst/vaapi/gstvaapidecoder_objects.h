/*
 *  gstvaapidecoder_objects.h - VA decoder objects
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

#ifndef GST_VAAPI_DECODER_OBJECTS_H
#define GST_VAAPI_DECODER_OBJECTS_H

#include <gst/vaapi/gstvaapicodec_objects.h>

G_BEGIN_DECLS

typedef struct _GstVaapiPicture         GstVaapiPicture;
typedef struct _GstVaapiSlice           GstVaapiSlice;

/* ------------------------------------------------------------------------- */
/* --- Pictures                                                          --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_PICTURE_CAST(obj) \
  ((GstVaapiPicture *) (obj))

#define GST_VAAPI_PICTURE(obj) \
  GST_VAAPI_PICTURE_CAST (obj)

#define GST_VAAPI_IS_PICTURE(obj) \
  (GST_VAAPI_PICTURE (obj) != NULL)

typedef enum
{
  GST_VAAPI_PICTURE_TYPE_NONE = 0,      // Undefined
  GST_VAAPI_PICTURE_TYPE_I,             // Intra
  GST_VAAPI_PICTURE_TYPE_P,             // Predicted
  GST_VAAPI_PICTURE_TYPE_B,             // Bi-directional predicted
  GST_VAAPI_PICTURE_TYPE_S,             // S(GMC)-VOP (MPEG-4)
  GST_VAAPI_PICTURE_TYPE_SI,            // Switching Intra
  GST_VAAPI_PICTURE_TYPE_SP,            // Switching Predicted
  GST_VAAPI_PICTURE_TYPE_BI,            // BI type (VC-1)
} GstVaapiPictureType;

/**
 * GstVaapiPictureFlags:
 * @GST_VAAPI_PICTURE_FLAG_SKIPPED: skipped frame
 * @GST_VAAPI_PICTURE_FLAG_REFERENCE: reference frame
 * @GST_VAAPI_PICTURE_FLAG_OUTPUT: frame was output
 * @GST_VAAPI_PICTURE_FLAG_INTERLACED: interlaced frame
 * @GST_VAAPI_PICTURE_FLAG_FF: first-field
 * @GST_VAAPI_PICTURE_FLAG_TFF: top-field-first
 * @GST_VAAPI_PICTURE_FLAG_ONEFIELD: only one field is valid
 * @GST_VAAPI_PICTURE_FLAG_MVC: multiview component
 * @GST_VAAPI_PICTURE_FLAG_RFF: repeat-first-field
 * @GST_VAAPI_PICTURE_FLAG_CORRUPTED: picture was reconstructed from
 *   corrupted references
 * @GST_VAAPI_PICTURE_FLAG_LAST: first flag that can be used by subclasses
 *
 * Enum values used for #GstVaapiPicture flags.
 */
typedef enum
{
  GST_VAAPI_PICTURE_FLAG_SKIPPED    = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 0),
  GST_VAAPI_PICTURE_FLAG_REFERENCE  = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 1),
  GST_VAAPI_PICTURE_FLAG_OUTPUT     = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 2),
  GST_VAAPI_PICTURE_FLAG_INTERLACED = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 3),
  GST_VAAPI_PICTURE_FLAG_FF         = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 4),
  GST_VAAPI_PICTURE_FLAG_TFF        = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 5),
  GST_VAAPI_PICTURE_FLAG_ONEFIELD   = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 6),
  GST_VAAPI_PICTURE_FLAG_MVC        = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 7),
  GST_VAAPI_PICTURE_FLAG_RFF        = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 8),
  GST_VAAPI_PICTURE_FLAG_CORRUPTED  = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 9),
  GST_VAAPI_PICTURE_FLAG_LAST       = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 10),
} GstVaapiPictureFlags;

#define GST_VAAPI_PICTURE_FLAGS         GST_VAAPI_MINI_OBJECT_FLAGS
#define GST_VAAPI_PICTURE_FLAG_IS_SET   GST_VAAPI_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_PICTURE_FLAG_SET      GST_VAAPI_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_PICTURE_FLAG_UNSET    GST_VAAPI_MINI_OBJECT_FLAG_UNSET

#define GST_VAAPI_PICTURE_IS_SKIPPED(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED)

#define GST_VAAPI_PICTURE_IS_REFERENCE(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_REFERENCE)

#define GST_VAAPI_PICTURE_IS_OUTPUT(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_OUTPUT)

#define GST_VAAPI_PICTURE_IS_INTERLACED(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_INTERLACED)

#define GST_VAAPI_PICTURE_IS_FIRST_FIELD(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_FF)

#define GST_VAAPI_PICTURE_IS_TFF(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_TFF)

#define GST_VAAPI_PICTURE_IS_RFF(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_RFF)

#define GST_VAAPI_PICTURE_IS_ONEFIELD(picture) \
  GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_ONEFIELD)

#define GST_VAAPI_PICTURE_IS_FRAME(picture) \
  (GST_VAAPI_PICTURE (picture)->structure == GST_VAAPI_PICTURE_STRUCTURE_FRAME)

#define GST_VAAPI_PICTURE_IS_COMPLETE(picture)          \
  (GST_VAAPI_PICTURE_IS_FRAME (picture) ||              \
   GST_VAAPI_PICTURE_IS_ONEFIELD (picture) ||           \
   !GST_VAAPI_PICTURE_IS_FIRST_FIELD (picture))

#define GST_VAAPI_PICTURE_IS_MVC(picture) \
  (GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_MVC))

#define GST_VAAPI_PICTURE_IS_CORRUPTED(picture) \
  (GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_CORRUPTED))

/**
 * GstVaapiPicture:
 *
 * A #GstVaapiCodecObject holding a picture parameter.
 */
struct _GstVaapiPicture
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;
  GstVaapiPicture *parent_picture;
  GstVideoCodecFrame *frame;
  GstVaapiSurface *surface;
  GstVaapiSurfaceProxy *proxy;
  VABufferID param_id;
  guint param_size;

  /*< public >*/
  GstVaapiPictureType type;
  VASurfaceID surface_id;
  gpointer param;
  GPtrArray *slices;
  GstVaapiIqMatrix *iq_matrix;
  GstVaapiHuffmanTable *huf_table;
  GstVaapiBitPlane *bitplane;
  GstVaapiProbabilityTable *prob_table;
  GstClockTime pts;
  gint32 poc;
  guint16 voc;
  guint16 view_id;
  guint structure;
  GstVaapiRectangle crop_rect;
  guint has_crop_rect:1;
};

G_GNUC_INTERNAL
void
gst_vaapi_picture_destroy (GstVaapiPicture * picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_picture_create (GstVaapiPicture * picture,
    const GstVaapiCodecObjectConstructorArgs * args);

G_GNUC_INTERNAL
GstVaapiPicture *
gst_vaapi_picture_new (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size);

G_GNUC_INTERNAL
GstVaapiPicture *
gst_vaapi_picture_new_field (GstVaapiPicture * picture);

G_GNUC_INTERNAL
GstVaapiPicture *
gst_vaapi_picture_new_clone (GstVaapiPicture * picture);

G_GNUC_INTERNAL
void
gst_vaapi_picture_add_slice (GstVaapiPicture * picture, GstVaapiSlice * slice);

G_GNUC_INTERNAL
gboolean
gst_vaapi_picture_decode (GstVaapiPicture * picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_picture_decode_with_surface_id (GstVaapiPicture * picture,
    VASurfaceID surface_id);

G_GNUC_INTERNAL
gboolean
gst_vaapi_picture_output (GstVaapiPicture * picture);

G_GNUC_INTERNAL
void
gst_vaapi_picture_set_crop_rect (GstVaapiPicture * picture,
    const GstVaapiRectangle * crop_rect);

#define gst_vaapi_picture_ref(picture) \
  gst_vaapi_codec_object_ref (picture)

#define gst_vaapi_picture_unref(picture) \
  gst_vaapi_codec_object_unref (picture)

#define gst_vaapi_picture_replace(old_picture_ptr, new_picture) \
  gst_vaapi_codec_object_replace (old_picture_ptr, new_picture)

/* ------------------------------------------------------------------------- */
/* --- Slices                                                            --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_SLICE_CAST(obj) \
  ((GstVaapiSlice *) (obj))

#define GST_VAAPI_SLICE(obj) \
  GST_VAAPI_SLICE_CAST (obj)

#define GST_VAAPI_IS_SLICE(obj) \
  (GST_VAAPI_SLICE (obj) != NULL)

/**
 * GstVaapiSlice:
 *
 * A #GstVaapiCodecObject holding a slice parameter.
 */
struct _GstVaapiSlice
{
  /*< private >*/
  GstVaapiCodecObject parent_instance;

  /*< public >*/
  VABufferID param_id;
  VABufferID data_id;
  gpointer param;

  /* Per-slice overrides */
  GstVaapiHuffmanTable *huf_table;
};

G_GNUC_INTERNAL
void
gst_vaapi_slice_destroy (GstVaapiSlice * slice);

G_GNUC_INTERNAL
gboolean
gst_vaapi_slice_create (GstVaapiSlice * slice,
    const GstVaapiCodecObjectConstructorArgs * args);

G_GNUC_INTERNAL
GstVaapiSlice *
gst_vaapi_slice_new (GstVaapiDecoder * decoder, gconstpointer param,
    guint param_size, const guchar * data, guint data_size);

G_GNUC_INTERNAL
GstVaapiSlice *
gst_vaapi_slice_new_n_params (GstVaapiDecoder * decoder,
    gconstpointer param, guint param_size, guint param_num, const guchar * data,
    guint data_size);

/* ------------------------------------------------------------------------- */
/* --- Helpers to create codec-dependent objects                         --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_PICTURE_NEW(codec, decoder)                   \
  gst_vaapi_picture_new (GST_VAAPI_DECODER_CAST (decoder),      \
      NULL, sizeof (G_PASTE (VAPictureParameterBuffer, codec)))

#define GST_VAAPI_SLICE_NEW(codec, decoder, buf, buf_size)      \
  gst_vaapi_slice_new (GST_VAAPI_DECODER_CAST (decoder),        \
      NULL, sizeof (G_PASTE (VASliceParameterBuffer, codec)),   \
      buf, buf_size)

#define GST_VAAPI_SLICE_NEW_N_PARAMS(codec, decoder, buf, buf_size, n) \
  gst_vaapi_slice_new_n_params (GST_VAAPI_DECODER_CAST (decoder),    \
      NULL, sizeof (G_PASTE (VASliceParameterBuffer, codec)), n,     \
      buf, buf_size)

G_END_DECLS

#endif /* GST_VAAPI_DECODER_OBJECTS_H */
