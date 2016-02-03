/*
 *  gstvaapidecoder_unit.c - VA decoder units
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
 * SECTION:gstvaapidecoder_unit
 * @short_description: Decoder unit
 */

#include "sysdeps.h"
#include "gstvaapidecoder_unit.h"

/**
 * gst_vaapi_decoder_unit_init:
 * @unit: a #GstVaapiDecoderUnit
 *
 * Initializes internal resources bound to the supplied decoder @unit.
 *
 * @note This is an internal function used to implement lightweight
 * sub-classes.
 */
void
gst_vaapi_decoder_unit_init (GstVaapiDecoderUnit * unit)
{
  unit->flags = 0;
  unit->size = 0;
  unit->offset = 0;

  unit->parsed_info = NULL;
  unit->parsed_info_destroy_notify = NULL;
}

/**
 * gst_vaapi_decoder_unit_clear:
 * @unit: a #GstVaapiDecoderUnit
 *
 * Deallocates any internal resources bound to the supplied decoder
 * @unit.
 *
 * @note This is an internal function used to implement lightweight
 * sub-classes.
 */
void
gst_vaapi_decoder_unit_clear (GstVaapiDecoderUnit * unit)
{
  gst_vaapi_decoder_unit_set_parsed_info (unit, NULL, NULL);
}

/**
 * gst_vaapi_decoder_unit_set_parsed_info:
 * @unit: a #GstVaapiDecoderUnit
 * @parsed_info: parser info
 * @destroy_notify: (closure parsed_info): a #GDestroyNotify
 *
 * Sets @parsed_info on the object and the #GDestroyNotify that will be
 * called when the data is freed.
 *
 * If some @parsed_info was previously set, then the former @destroy_notify
 * function will be called before the @parsed_info is replaced.
 */
void
gst_vaapi_decoder_unit_set_parsed_info (GstVaapiDecoderUnit * unit,
    gpointer parsed_info, GDestroyNotify destroy_notify)
{
  g_return_if_fail (GST_VAAPI_IS_DECODER_UNIT (unit));

  if (unit->parsed_info && unit->parsed_info_destroy_notify)
    unit->parsed_info_destroy_notify (unit->parsed_info);
  unit->parsed_info = parsed_info;
  unit->parsed_info_destroy_notify = destroy_notify;
}
