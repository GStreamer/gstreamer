/*************************************************************************
*  Generating a MPEG/SYSTEMS						 *
*  MULTIPLEXED VIDEO/AUDIO STREAM					 *
*  from two MPEG source streams						 *
*  Christoph Moar							 *
*  SIEMENS CORPORATE RESEARCH AND DEVELOPMENT ST SN 11 / T SN 6		 *
*  (C) 1994 1995							 *
**************************************************************************
*  Restrictions apply. Will not support the whole MPEG/SYSTEM Standard.  *
*  Basically, will generate Constrained System Parameter Files.		 *
*  Mixes only one audio and/or one video stream. Might be expanded.	 *
*************************************************************************/

/*************************************************************************
*  mplex - MPEG/SYSTEMS multiplexer					 *
*  Copyright (C) 1994 1995 Christoph Moar				 *
*  Siemens ZFE ST SN 11 / T SN 6					 *
*									 *
*  moar@informatik.tu-muenchen.de 					 *
*       (Christoph Moar)			 			 *
*									 *
*  This program is free software; you can redistribute it and/or modify	 *
*  it under the terms of the GNU General Public License as published by	 *
*  the Free Software Foundation; either version 2 of the License, or	 *
*  (at your option) any later version.					 *
*									 *
*  This program is distributed in the hope that it will be useful,	 *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of	 *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	 *
*  GNU General Public License for more details.				 *
*									 *
*  You should have received a copy of the GNU General Public License	 *
*  along with this program; if not, write to the Free Software		 *
*  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		 *
*************************************************************************/


#ifndef __MAIN_H__
#define __MAIN_H__

#include <glib.h>

#define PACK_START		0x000001ba
#define SYS_HEADER_START	0x000001bb
#define ISO11172_END		0x000001b9
#define PACKET_START		0x000001

#define CLOCKS			90000.0		/* System Clock Hertz	*/

#define AFTER_PACKET_LENGTH	15		/* No of non-data-bytes	*/
						/* following the packet	*/
						/* length field		*/
#define LAST_SCR_BYTE_IN_PACK	9		/* No of bytes in pack	*/
						/* preceding, and 	*/
						/* including, the SCR	*/

/* The following values for sys_header_length & size are only valid for */
/* System streams consisting of two basic streams. When wrapping around */
/* the system layer on a single video or a single audio stream, those   */
/* values get decreased by 3.                                           */

#define SYS_HEADER_LENGTH	12		/* length of Sys Header	*/
						/* after start code and	*/
						/* length field		*/

#define SYS_HEADER_SIZE		18		/* incl. start code and	*/
						/* length field		*/
#define PACK_HEADER_SIZE	12

#define PACKET_HEADER_SIZE	6

#define MAX_SECTOR_SIZE		0x20000		/* Max Sektor Groesse	*/

#define STREAMS_VIDEO           1
#define STREAMS_AUDIO           2
#define STREAMS_BOTH            3

#define AUDIO_STREAMS		0xb8		/* Marker Audio Streams	*/
#define VIDEO_STREAMS		0xb9		/* Marker Video Streams	*/
#define AUDIO_STR_0		0xc0		/* Marker Audio Stream0	*/
#define VIDEO_STR_0		0xe0		/* Marker Video Stream0	*/
#define PADDING_STR		0xbe		/* Marker Padding Stream*/

#define ZERO_STUFFING_BYTE	0
#define STUFFING_BYTE		0xff
#define RESERVED_BYTE		0xff
#define TIMESTAMPS_NO		0		/* Flag NO timestamps	*/
#define TIMESTAMPS_PTS		1		/* Flag PTS timestamp	*/
#define TIMESTAMPS_PTS_DTS	2		/* Flag BOTH timestamps	*/

#define MARKER_SCR		2		/* Marker SCR		*/
#define MARKER_JUST_PTS		2		/* Marker only PTS	*/
#define MARKER_PTS		3		/* Marker PTS		*/
#define MARKER_DTS		1		/* Marker DTS		*/
#define MARKER_NO_TIMESTAMPS	0x0f		/* Marker NO timestamps	*/

#define STATUS_AUDIO_END	0		/* Statusmessage A end	*/
#define STATUS_VIDEO_END	1		/* Statusmessage V end	*/
#define STATUS_AUDIO_TIME_OUT	2		/* Statusmessage A out	*/
#define STATUS_VIDEO_TIME_OUT	3		/* Statusmessage V out	*/

/*************************************************************************
    Typ- und Strukturdefinitionen
*************************************************************************/

typedef struct sector_struc	/* A sector, can contain pack, sys header	*/
				/* and packet.		*/
{   unsigned char  buf [MAX_SECTOR_SIZE] ;
    unsigned int   length_of_sector  ;
    unsigned int   length_of_packet_data ;
    guint64 TS                ;
} Sector_struc;

typedef struct pack_struc	/* Pack Info				*/
{   unsigned char  buf [PACK_HEADER_SIZE];
    guint64 SCR;
} Pack_struc;

typedef struct sys_header_struc	/* System Header Info			*/
{   unsigned char  buf [SYS_HEADER_SIZE];
} Sys_header_struc;

/*************************************************************************
    Funktionsprototypen, keine Argumente, K&R Style
*************************************************************************/

/* systems.c */
void create_sector (Sector_struc     *sector, Pack_struc       *pack, Sys_header_struc *sys_header,
	unsigned int packet_size, unsigned char *inputbuffer, unsigned char type, unsigned char buffer_scale,
	unsigned int buffer_size, unsigned char buffers, guint64 PTS, guint64 DTS,
	unsigned char timestamps, unsigned int  which_streams);

void create_pack (Pack_struc *pack, guint64 SCR, unsigned int mux_rate);

void create_sys_header (Sys_header_struc *sys_header, unsigned int rate_bound, unsigned char audio_bound,
	unsigned char fixed, unsigned char CSPS, unsigned char audio_lock, unsigned char video_lock,
	unsigned char video_bound, unsigned char stream1, unsigned char buffer1_scale, unsigned int buffer1_size,
	unsigned char stream2, unsigned char buffer2_scale, unsigned int buffer2_size, unsigned int which_streams);

#endif
