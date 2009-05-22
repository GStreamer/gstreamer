/* GStreamer Sub-Picture Unit - VobSub/DVD handling
 * Copyright (C) 2009 Jan Schmidt <thaytan@noraisin.net>
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
#ifndef __GSTSPU_VOBSUB_H__
#define __GSTSPU_VOBSUB_H__

void gst_dvd_spu_handle_new_vobsub_buf (GstDVDSpu * dvdspu, SpuPacket * packet);
void gst_dvdspu_vobsub_execute_event (GstDVDSpu *dvdspu);

#endif
