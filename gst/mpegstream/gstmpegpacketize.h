/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __MPEGPACKETIZE_H__
#define __MPEGPACKETIZE_H__


#include <config.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct _GstMPEGPacketize GstMPEGPacketize;

#define GST_MPEG_PACKETIZE_ID(pack) 		((pack)->id)
#define GST_MPEG_PACKETIZE_IS_MPEG2(pack) 	((pack)->MPEG2)


struct _GstMPEGPacketize {
  /* current parse state */
  guchar id;

  GstPad *pad;
  GstByteStream *bs;

  gboolean MPEG2;
};

GstMPEGPacketize* 	gst_mpeg_packetize_new 		(GstPad *pad);
void		 	gst_mpeg_packetize_destroy 	(GstMPEGPacketize *packetize);

GstData* 		gst_mpeg_packetize_read 	(GstMPEGPacketize *packetize);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __MPEGPACKETIZE_H__ */
