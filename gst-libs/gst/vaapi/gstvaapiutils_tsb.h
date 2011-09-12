/*
 *  gstvaapiutils_tsb.h - Timestamp buffer store
 *
 *  Copyright (C) 2011 Intel Corporation
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

#ifndef GST_VAAPI_UTILS_TSB_H
#define GST_VAAPI_UTILS_TSB_H

#include "config.h"
#include <gst/gstbuffer.h>

typedef struct _GstVaapiTSB GstVaapiTSB;

GstVaapiTSB *
gst_vaapi_tsb_new()
    attribute_hidden;

void
gst_vaapi_tsb_destroy(GstVaapiTSB *tsb)
    attribute_hidden;

gboolean
gst_vaapi_tsb_push(GstVaapiTSB *tsb, GstBuffer *buffer)
    attribute_hidden;

void
gst_vaapi_tsb_pop(GstVaapiTSB *tsb, gsize size)
    attribute_hidden;

GstBuffer *
gst_vaapi_tsb_peek(GstVaapiTSB *tsb)
    attribute_hidden;

GstClockTime
gst_vaapi_tsb_get_timestamp(GstVaapiTSB *tsb)
    attribute_hidden;

gsize
gst_vaapi_tsb_get_size(GstVaapiTSB *tsb)
    attribute_hidden;

#endif /* GST_VAAPI_UTILS_TSB_H */
