/*
 *  gstvaapiparser_frame.c - VA parser frame
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

/**
 * SECTION:gstvaapiparser_frame
 * @short_description: VA decoder frame
 */

#include "sysdeps.h"
#include "gstvaapiparser_frame.h"

static inline const GstVaapiMiniObjectClass *
gst_vaapi_parser_frame_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiParserFrameClass = {
    sizeof (GstVaapiParserFrame),
    (GDestroyNotify) gst_vaapi_parser_frame_free
  };
  return &GstVaapiParserFrameClass;
}

static inline gboolean
alloc_units (GArray ** units_ptr, guint size)
{
  GArray *units;

  units = g_array_sized_new (FALSE, FALSE, sizeof (GstVaapiDecoderUnit), size);
  *units_ptr = units;
  return units != NULL;
}

static inline void
free_units (GArray ** units_ptr)
{
  GArray *const units = *units_ptr;
  guint i;

  if (units) {
    for (i = 0; i < units->len; i++) {
      GstVaapiDecoderUnit *const unit =
          &g_array_index (units, GstVaapiDecoderUnit, i);
      gst_vaapi_decoder_unit_clear (unit);
    }
    g_array_unref (units);
    *units_ptr = NULL;
  }
}

/**
 * gst_vaapi_parser_frame_new:
 * @width: frame width in pixels
 * @height: frame height in pixels
 *
 * Creates a new #GstVaapiParserFrame object.
 *
 * Returns: The newly allocated #GstVaapiParserFrame
 */
GstVaapiParserFrame *
gst_vaapi_parser_frame_new (guint width, guint height)
{
  GstVaapiParserFrame *frame;
  guint num_slices;

  frame = (GstVaapiParserFrame *)
      gst_vaapi_mini_object_new (gst_vaapi_parser_frame_class ());
  if (!frame)
    return NULL;

  if (!height)
    height = 1088;
  num_slices = (height + 15) / 16;

  if (!alloc_units (&frame->pre_units, 16))
    goto error;
  if (!alloc_units (&frame->units, num_slices))
    goto error;
  if (!alloc_units (&frame->post_units, 1))
    goto error;
  frame->output_offset = 0;
  return frame;

  /* ERRORS */
error:
  {
    gst_vaapi_parser_frame_unref (frame);
    return NULL;
  }
}

/**
 * gst_vaapi_parser_frame_free:
 * @frame: a #GstVaapiParserFrame
 *
 * Deallocates any internal resources bound to the supplied decoder
 * @frame.
 *
 * @note This is an internal function used to implement lightweight
 * sub-classes.
 */
void
gst_vaapi_parser_frame_free (GstVaapiParserFrame * frame)
{
  free_units (&frame->units);
  free_units (&frame->pre_units);
  free_units (&frame->post_units);
}

/**
 * gst_vaapi_parser_frame_append_unit:
 * @frame: a #GstVaapiParserFrame
 * @unit: a #GstVaapiDecoderUnit
 *
 * Appends unit to the @frame.
 */
void
gst_vaapi_parser_frame_append_unit (GstVaapiParserFrame * frame,
    GstVaapiDecoderUnit * unit)
{
  GArray **unit_array_ptr;

  unit->offset = frame->output_offset;
  frame->output_offset += unit->size;

  if (GST_VAAPI_DECODER_UNIT_IS_SLICE (unit))
    unit_array_ptr = &frame->units;
  else if (GST_VAAPI_DECODER_UNIT_IS_FRAME_END (unit))
    unit_array_ptr = &frame->post_units;
  else
    unit_array_ptr = &frame->pre_units;
  g_array_append_val (*unit_array_ptr, *unit);
}
