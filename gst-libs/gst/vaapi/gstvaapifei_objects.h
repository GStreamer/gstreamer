/*
 *  gstvaapifei_objects.h - VA FEI objects abstraction
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

#ifndef GST_VAAPI_FEI_OBJECTS_H
#define GST_VAAPI_FEI_OBJECTS_H

G_BEGIN_DECLS

#define GST_VAAPI_FEI_CODEC_OBJECT(obj) \
    ((GstVaapiFeiCodecObject *) (obj))

typedef struct _GstVaapiFeiCodecObject GstVaapiFeiCodecObject;

typedef struct _GstVaapiEncFeiMbCode GstVaapiEncFeiMbCode;
typedef struct _GstVaapiEncFeiMv GstVaapiEncFeiMv;
typedef struct _GstVaapiEncFeiMvPredictor GstVaapiEncFeiMvPredictor;
typedef struct _GstVaapiEncFeiMbControl GstVaapiEncFeiMbControl;
typedef struct _GstVaapiEncFeiQp GstVaapiEncFeiQp;
typedef struct _GstVaapiEncFeiDistortion GstVaapiEncFeiDistortion;

struct _GstVaapiEncoder;

/* -----------------       Base Codec Object    ---------------------------- */
/* ------------------------------------------------------------------------- */

GstVaapiFeiCodecObject *
gst_vaapi_fei_codec_object_ref (GstVaapiFeiCodecObject *object);

void
gst_vaapi_fei_codec_object_unref (GstVaapiFeiCodecObject *object);

void
gst_vaapi_fei_codec_object_replace (GstVaapiFeiCodecObject **old_object_ptr,
                                    GstVaapiFeiCodecObject *new_object);

gboolean
gst_vaapi_fei_codec_object_map (GstVaapiFeiCodecObject *object,
                               gpointer *data, guint *size);

void
gst_vaapi_fei_codec_object_unmap (GstVaapiFeiCodecObject *object);

/* ------------------------------------------------------------------------- */
/* ---  MB Code buffer                                                   --- */
/* ------------------------------------------------------------------------- */

GstVaapiEncFeiMbCode *
gst_vaapi_enc_fei_mb_code_new (struct _GstVaapiEncoder * encoder, gconstpointer param,
        guint param_size);

/* ------------------------------------------------------------------------- */
/* ---  MV Buffer                                                        --- */
/* ------------------------------------------------------------------------- */

GstVaapiEncFeiMv *
gst_vaapi_enc_fei_mv_new (struct _GstVaapiEncoder * encoder, gconstpointer param,
        guint param_size);

/* ------------------------------------------------------------------------- */
/* ---  MV Predictor Buffer                                              --- */
/* ------------------------------------------------------------------------- */

GstVaapiEncFeiMvPredictor *
gst_vaapi_enc_fei_mv_predictor_new (struct _GstVaapiEncoder * encoder, gconstpointer param,
        guint param_size);

/* ------------------------------------------------------------------------- */
/* ---  MB Control Buffer                                                --- */
/* ------------------------------------------------------------------------- */

GstVaapiEncFeiMbControl *
gst_vaapi_enc_fei_mb_control_new (struct _GstVaapiEncoder * encoder, gconstpointer param,
        guint param_size);

/* ------------------------------------------------------------------------- */
/* ---  QP Buffer                                                        --- */
/* ------------------------------------------------------------------------- */

GstVaapiEncFeiQp *
gst_vaapi_enc_fei_qp_new (struct _GstVaapiEncoder * encoder, gconstpointer param,
        guint param_size);

/* ------------------------------------------------------------------------- */
/* ---  Distortion Buffer                                                --- */
/* ------------------------------------------------------------------------- */

GstVaapiEncFeiDistortion *
gst_vaapi_enc_fei_distortion_new (struct _GstVaapiEncoder * encoder, gconstpointer param,
        guint param_size);

G_END_DECLS

#endif /* GST_VAAPI_FEI_OBJECTS_H */
