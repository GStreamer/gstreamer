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


#ifndef __GST_RTP_COMMON_H__
#define __GST_RTP_COMMON_H__

#define RTP_VERSION 2

typedef enum
{
/* Audio: */
  PAYLOAD_PCMU = 0,		/* ITU-T G.711. mu-law audio (RFC 3551) */ 
  PAYLOAD_GSM = 3,
  PAYLOAD_PCMA = 8,		/* ITU-T G.711 A-law audio (RFC 3551) */
  PAYLOAD_L16_STEREO = 10,
  PAYLOAD_L16_MONO = 11,
  PAYLOAD_MPA = 14,		/* Audio MPEG 1-3 */
  PAYLOAD_G723_63 = 16,		/* Not standard */
  PAYLOAD_G723_53 = 17,		/* Not standard */
  PAYLOAD_TS48 = 18,		/* Not standard */
  PAYLOAD_TS41 = 19,		/* Not standard */
  PAYLOAD_G728 = 20,		/* Not standard */
  PAYLOAD_G729 = 21,		/* Not standard */

/* Video: */
  PAYLOAD_MPV = 32,		/* Video MPEG 1 & 2 */

/* BOTH */
  PAYLOAD_BMPEG = 34		/* Not Standard */
}
rtp_payload_t;

#endif
