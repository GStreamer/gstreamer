/*
 *  gstvaapifei_objects_priv.h - VA FEI objects abstraction (priv definitions)
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

#ifndef GST_VAAPI_FEI_OBJECTS_PRIV_H
#define GST_VAAPI_FEI_OBJECTS_PRIV_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapifei_objects.h>
#include <gst/vaapi/gstvaapiencoder_objects.h>
#include <va/va.h>
G_BEGIN_DECLS

typedef gpointer                                   GstVaapiFeiCodecBase;
typedef struct _GstVaapiFeiCodecObjectClass        GstVaapiFeiCodecObjectClass;

#define GST_VAAPI_FEI_CODEC_BASE(obj) \
  ((GstVaapiFeiCodecBase *) (obj))

enum
{
  GST_VAAPI_FEI_CODEC_OBJECT_FLAG_CONSTRUCTED = (1 << 0),
  GST_VAAPI_FEI_CODEC_OBJECT_FLAG_LAST        = (1 << 1)
};

typedef struct
{
  gconstpointer param;
  guint param_size;
  gconstpointer data;
  guint data_size;
  guint flags;
} GstVaapiFeiCodecObjectConstructorArgs;

typedef gboolean
(*GstVaapiFeiCodecObjectCreateFunc)(GstVaapiFeiCodecObject * object,
    const GstVaapiFeiCodecObjectConstructorArgs * args);

typedef GDestroyNotify GstVaapiFeiCodecObjectDestroyFunc;

/**
 * GstVaapiFeiCodecObject:
 *
 * A #GstVaapiMiniObject holding the base codec object data
 */
struct _GstVaapiFeiCodecObject
{
  /*< private >*/
  GstVaapiMiniObject parent_instance;
  GstVaapiFeiCodecBase *codec;
  VABufferID param_id;
  gpointer param;
  guint param_size;
};

/**
 * GstVaapiFeiCodecObjectClass:
 *
 * The #GstVaapiFeiCodecObject base class.
 */
struct _GstVaapiFeiCodecObjectClass
{
  /*< private >*/
  GstVaapiMiniObjectClass parent_class;

  GstVaapiFeiCodecObjectCreateFunc create;
};

G_GNUC_INTERNAL
const GstVaapiFeiCodecObjectClass *
gst_vaapi_fei_codec_object_get_class (GstVaapiFeiCodecObject * object) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiFeiCodecObject *
gst_vaapi_fei_codec_object_new (const GstVaapiFeiCodecObjectClass * object_class,
    GstVaapiFeiCodecBase * codec, gconstpointer param, guint param_size,
    gconstpointer data, guint data_size, guint flags);


/* ------------------------------------------------------------------------- */
/* ---  MB Code Buffer                                              --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_FEI_MB_CODE_CAST(obj) \
  ((GstVaapiEncFeiMbCode *) (obj))
/**
 * GstVaapiEncFeiMbCode:
 *
 * A #GstVaapiFeiCodecObject holding a mb code buffer.
 */
struct _GstVaapiEncFeiMbCode
{
  /*< private >*/
  GstVaapiFeiCodecObject parent_instance;
};

/* ------------------------------------------------------------------------- */
/* ---  MV Buffer                                                        --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_FEI_NEW_MV_CAST(obj) \
  ((GstVaapiEncFeiMv *) (obj))
/**
 * GstVaapiEncFeiMv:
 *
 * A #GstVaapiFeiCodecObject holding a mv buffer.
 */
struct _GstVaapiEncFeiMv
{
  /*< private >*/
  GstVaapiFeiCodecObject parent_instance;
};

/* ------------------------------------------------------------------------- */
/* ---  MV Predictor Buffer                                              --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_FEI_NEW_MV_PREDICTOR_CAST(obj) \
  ((GstVaapiEncFeiMvPredictor *) (obj))
/**
 * GstVaapiEncFeiMvPredictor:
 *
 * A #GstVaapiFeiCodecObject holding a mv predictor buffer.
 */
struct _GstVaapiEncFeiMvPredictor
{
  /*< private >*/
  GstVaapiFeiCodecObject parent_instance;
};


/* ------------------------------------------------------------------------- */
/* ---  MB Control Buffer                                                --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_FEI_NEW_MB_CONTROL_CAST(obj) \
  ((GstVaapiEncFeiMbControl *) (obj))
/**
 * GstVaapiEncFeiMbControl:
 *
 * A #GstVaapiFeiCodecObject holding a mb control buffer.
 */
struct _GstVaapiEncFeiMbControl
{
  /*< private >*/
  GstVaapiFeiCodecObject parent_instance;
};


/* ------------------------------------------------------------------------- */
/* ---  QP Buffer                                                        --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_FEI_NEW_QP_CAST(obj) \
  ((GstVaapiEncFeiQp *) (obj))
/**
 * GstVaapiEncFeiQp:
 *
 * A #GstVaapiFeiCodecObject holding a qp buffer.
 */
struct _GstVaapiEncFeiQp
{
  /*< private >*/
  GstVaapiFeiCodecObject parent_instance;
};


/* ------------------------------------------------------------------------- */
/* ---  Distortion Buffer                                                --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENC_FEI_NEW_DISTORTION_CAST(obj) \
  ((GstVaapiEncFeiDistortion *) (obj))
/**
 * GstVaapiEncFeiDistortion:
 *
 * A #GstVaapiFeiCodecObject holding a distortion buffer.
 */
struct _GstVaapiEncFeiDistortion
{
  /*< private >*/
  GstVaapiFeiCodecObject parent_instance;
};


/* ------------------------------------------------------------------------- */
/* --- Helpers to create fei objects                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_FEI_CODEC_DEFINE_TYPE(type, prefix)                   \
G_GNUC_INTERNAL                                                         \
void                                                                    \
G_PASTE (prefix, _destroy) (type *);                                    \
                                                                        \
G_GNUC_INTERNAL                                                         \
gboolean                                                                \
G_PASTE (prefix, _create) (type *,                                      \
    const GstVaapiFeiCodecObjectConstructorArgs * args);                \
                                                                        \
static const GstVaapiFeiCodecObjectClass G_PASTE (type, Class) = {      \
  .parent_class = {                                                     \
    .size = sizeof (type),                                              \
    .finalize = (GstVaapiFeiCodecObjectDestroyFunc)                     \
        G_PASTE (prefix, _destroy)                                      \
  },                                                                    \
  .create = (GstVaapiFeiCodecObjectCreateFunc)                          \
      G_PASTE (prefix, _create),                                        \
}

/* GstVaapiEncFeiMiscParam */
#define GST_VAAPI_ENC_FEI_MISC_PARAM_NEW(codec, encoder)                \
  gst_vaapi_enc_misc_param_new (GST_VAAPI_ENCODER_CAST (encoder),       \
      VAEncMiscParameterTypeFEIFrameControl,                            \
      sizeof (G_PASTE (VAEncMiscParameterFEIFrameControl, codec)))

G_END_DECLS

#endif /* GST_VAAPI_FEI_OBJECTS_H */
