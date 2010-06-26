/* 
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/gst.h>

#include "gstvdpdevice.h"
#include "gstvdpvideobuffer.h"
#include "gstvdpvideosrcpad.h"
#include "gstvdpoutputbuffer.h"
#include "gstvdpoutputsrcpad.h"
#include "gstvdpdecoder.h"

#include "gstvdp.h"

GST_DEBUG_CATEGORY (gst_vdp_debug);

void
gst_vdp_init (void)
{
  /* do this so debug categories get created */
  gst_vdp_device_get_type ();
  gst_vdp_output_buffer_get_type ();
  gst_vdp_video_buffer_get_type ();
  gst_vdp_video_src_pad_get_type ();
  gst_vdp_output_src_pad_get_type ();
  gst_vdp_decoder_get_type ();

  GST_DEBUG_CATEGORY_INIT (gst_vdp_debug, "vdp", 0, "GstVdp debug category");
}
