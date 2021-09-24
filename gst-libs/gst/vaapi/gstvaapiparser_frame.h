/*
 *  gstvaapiparser_frame.h - VA parser frame
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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

#ifndef GST_VAAPI_PARSER_FRAME_H
#define GST_VAAPI_PARSER_FRAME_H

#include <gst/vaapi/gstvaapiminiobject.h>
#include <gst/vaapi/gstvaapidecoder_unit.h>

G_BEGIN_DECLS

typedef struct _GstVaapiParserFrame             GstVaapiParserFrame;

#define GST_VAAPI_PARSER_FRAME(frame) \
    ((GstVaapiParserFrame *)(frame))

#define GST_VAAPI_IS_PARSER_FRAME(frame) \
    (GST_VAAPI_PARSER_FRAME(frame) != NULL)

/**
 * GstVaapiParserFrame:
 * @output_offset: current offset to the reconstructed #GstBuffer for
 *    this #GstVideoCodecFrame. This is used to initialize the decoder
 *    unit offset
 * @units: list of #GstVaapiDecoderUnit objects (slice data)
 * @pre_units: list of units to decode before GstVaapiDecoder:start_frame()
 * @post_units: list of units to decode after GstVaapiDecoder:end_frame()
 *
 * An extension to #GstVideoCodecFrame with #GstVaapiDecoder specific
 * information. Decoder frames are usually attached to codec frames as
 * the user_data anchor point.
 */
struct _GstVaapiParserFrame {
    /*< private >*/
    GstVaapiMiniObject  parent_instance;

    guint               output_offset;
    GArray             *units;
    GArray             *pre_units;
    GArray             *post_units;
};

G_GNUC_INTERNAL
GstVaapiParserFrame *
gst_vaapi_parser_frame_new(guint width, guint height);

G_GNUC_INTERNAL
void
gst_vaapi_parser_frame_free(GstVaapiParserFrame *frame);

G_GNUC_INTERNAL
void
gst_vaapi_parser_frame_append_unit(GstVaapiParserFrame *frame,
    GstVaapiDecoderUnit *unit);

static inline GstVaapiParserFrame *
gst_vaapi_parser_frame_ref (GstVaapiParserFrame * frame)
{
  return (GstVaapiParserFrame *)
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (frame));
}

static inline void
gst_vaapi_parser_frame_unref (GstVaapiParserFrame * frame)
{
  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (frame));
}

static inline void
gst_vaapi_parser_frame_replace(GstVaapiParserFrame * old_frame_p,
    GstVaapiParserFrame * new_frame)
{
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) old_frame_p,
      (GstVaapiMiniObject *) new_frame);
}

G_END_DECLS

#endif /* GST_VAAPI_PARSER_FRAME_H */
