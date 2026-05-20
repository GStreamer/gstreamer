/* GStreamer
 * Copyright (C) 2025 Fluendo S.A.
 *   Author: Diego Nieto <dnieto@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gsth274.h>

G_BEGIN_DECLS

/**
 * GST_VIDEO_DSC_INITIALIZATION_META_API_TYPE:
 * 
 * Since: 1.30
 */
#define GST_VIDEO_DSC_INITIALIZATION_META_API_TYPE \
    (gst_video_dsc_initialization_meta_api_get_type())

/**
 * GST_VIDEO_DSC_INITIALIZATION_META_INFO:
 * 
 * Since: 1.30
 */
#define GST_VIDEO_DSC_INITIALIZATION_META_INFO \
    (gst_video_dsc_initialization_meta_get_info())
typedef struct _GstVideoDSCInitializationMeta
    GstVideoDSCInitializationMeta;

/**
 * GstVideoDSCInitializationMeta:
 * @meta: parent #GstMeta
 * @dsc_initialization: DSC Initialization data structure
 *
 * Metadata for Digitally Signed Content Initialization SEI message.
 *
 * Since: 1.30
 */
struct _GstVideoDSCInitializationMeta
{
  GstMeta meta;

  GstH274DigitallySignedContentInitialization dsc_initialization;
};

/**
 * gst_video_dsc_initialization_meta_api_get_type:
 * 
 * Since: 1.30
 */
GST_VIDEO_API
GType gst_video_dsc_initialization_meta_api_get_type (void);

/**
 * gst_video_dsc_initialization_meta_get_info:
 * 
 * Since: 1.30
 */
GST_VIDEO_API
const GstMetaInfo * gst_video_dsc_initialization_meta_get_info (void);

/**
 * gst_buffer_get_video_dsc_initialization_meta:
 * @b: a #GstBuffer
 * 
 * Returns: (transfer none) (nullable): the #GstVideoDSCInitializationMeta
 *   on the buffer or %NULL if not present.
 * 
 * Since: 1.30
 */
#define gst_buffer_get_video_dsc_initialization_meta(b) \
    ((GstVideoDSCInitializationMeta *)gst_buffer_get_meta((b), \
        GST_VIDEO_DSC_INITIALIZATION_META_API_TYPE))

/**
 * gst_buffer_add_video_dsc_initialization_meta:
 * @buffer: a #GstBuffer
 * @dsc_initialization: DSC Initialization data
 *
 * Attaches GstVideoDSCInitializationMeta metadata to @buffer.
 *
 * Returns: (transfer none): the #GstVideoDSCInitializationMeta on @buffer.
 *
 * Since: 1.30
 */
GST_VIDEO_API
GstVideoDSCInitializationMeta * gst_buffer_add_video_dsc_initialization_meta (GstBuffer *
    buffer, const GstH274DigitallySignedContentInitialization * dsc_initialization);

/**
 * GST_VIDEO_DSC_SELECTION_META_API_TYPE:
 * 
 * Since: 1.30
 */
#define GST_VIDEO_DSC_SELECTION_META_API_TYPE \
    (gst_video_dsc_selection_meta_api_get_type())

/**
 * GST_VIDEO_DSC_SELECTION_META_INFO:
 * 
 * Since: 1.30
 */
#define GST_VIDEO_DSC_SELECTION_META_INFO \
    (gst_video_dsc_selection_meta_get_info())

typedef struct _GstVideoDSCSelectionMeta GstVideoDSCSelectionMeta;

/**
 * GstVideoDSCSelectionMeta:
 * @meta: parent #GstMeta
 * @dsc_selection: DSC Selection data structure
 * 
 * Metadata for Digitally Signed Content Selection SEI message.
 *
 * Since: 1.30
 */
struct _GstVideoDSCSelectionMeta
{
  GstMeta meta;
  
  GstH274DigitallySignedContentSelection dsc_selection;
};

/**
 * gst_video_dsc_selection_meta_api_get_type:
 * 
 * Since: 1.30
 */
GST_VIDEO_API
GType gst_video_dsc_selection_meta_api_get_type (void);

/**
 * gst_video_dsc_selection_meta_get_info:
 * 
 * Since: 1.30
 */
GST_VIDEO_API
const GstMetaInfo * gst_video_dsc_selection_meta_get_info (void);

/**
 * gst_buffer_get_video_dsc_selection_meta:
 * @b: a #GstBuffer
 * 
 * Returns: (transfer none) (nullable): the #GstVideoDSCSelectionMeta
 *  on the buffer or %NULL if not present.
 * 
 * Since: 1.30
 */
#define gst_buffer_get_video_dsc_selection_meta(b) \
    ((GstVideoDSCSelectionMeta *)gst_buffer_get_meta((b), \
        GST_VIDEO_DSC_SELECTION_META_API_TYPE))

/**
 * gst_buffer_add_video_dsc_selection_meta:
 * @buffer: a #GstBuffer
 * @dsc_selection: DSC Selection data
 *
 * Attaches GstVideoDSCSelectionMeta metadata to @buffer.
 *
 * Returns: (transfer none): the #GstVideoDSCSelectionMeta on @buffer.
 *
 * Since: 1.30
 */
GST_VIDEO_API
GstVideoDSCSelectionMeta * gst_buffer_add_video_dsc_selection_meta (GstBuffer * buffer,
    const GstH274DigitallySignedContentSelection * dsc_selection);


/**
 * GST_VIDEO_DSC_VERIFICATION_META_API_TYPE:
 * 
 * Since: 1.30
 */
#define GST_VIDEO_DSC_VERIFICATION_META_API_TYPE \
    (gst_video_dsc_verification_meta_api_get_type())

/**
 * GST_VIDEO_DSC_VERIFICATION_META_INFO:
 * 
 * Since: 1.30
 */
#define GST_VIDEO_DSC_VERIFICATION_META_INFO \
    (gst_video_dsc_verification_meta_get_info())

typedef struct _GstVideoDSCVerificationMeta GstVideoDSCVerificationMeta;

/**
 * GstVideoDSCVerificationMeta:
 * @meta: parent #GstMeta
 * @dsc_verification: DSC Verification data structure
 * 
 * Metadata for Digitally Signed Content Verification SEI message.
 *
 * Since: 1.30
 */
struct _GstVideoDSCVerificationMeta
{
  GstMeta meta;

  GstH274DigitallySignedContentVerification dsc_verification;
};

/**
 * gst_video_dsc_verification_meta_api_get_type:
 * 
 * Since: 1.30
 */
GST_VIDEO_API
GType gst_video_dsc_verification_meta_api_get_type (void);

/**
 * gst_video_dsc_verification_meta_get_info:
 * 
 * Since: 1.30
 */
GST_VIDEO_API
const GstMetaInfo * gst_video_dsc_verification_meta_get_info (void);

/**
 * gst_buffer_get_video_dsc_verification_meta:
 * @b: a #GstBuffer
 * 
 * Returns: (transfer none) (nullable): the #GstVideoDSCVerificationMeta
 *   on the buffer or %NULL if not present.
 * 
 * Since: 1.30
 */
#define gst_buffer_get_video_dsc_verification_meta(b) \
    ((GstVideoDSCVerificationMeta *)gst_buffer_get_meta((b), \
        GST_VIDEO_DSC_VERIFICATION_META_API_TYPE))

/**
 * gst_buffer_add_video_dsc_verification_meta:
 * @buffer: a #GstBuffer
 * @dsc_verification: DSC Verification data
 *
 * Attaches GstVideoDSCVerificationMeta metadata to @buffer.
 *
 * Returns: (transfer none): the #GstVideoDSCVerificationMeta on @buffer.
 *
 * Since: 1.30
 */
GST_VIDEO_API
GstVideoDSCVerificationMeta * gst_buffer_add_video_dsc_verification_meta (GstBuffer *
    buffer, const GstH274DigitallySignedContentVerification * dsc_verification);

G_END_DECLS
