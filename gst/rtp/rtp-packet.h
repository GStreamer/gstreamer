/*
  Gnome-o-Phone - A program for internet telephony
  Copyright (C) 1999  Roland Dreier
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
  
  $Id$
*/

#ifndef _RTP_PACKET_H
#define _RTP_PACKET_H 1

#include <sys/types.h>
#include <glib.h>

#ifdef __sun
#include <sys/uio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RTP_VERSION 2
#define RTP_HEADER_LEN 12
#define RTP_MTU 2048

typedef struct Rtp_Header *Rtp_Header;

struct Rtp_Packet_Struct {
  gpointer data;
  guint data_len;
};

struct Rtp_Header {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned int csrc_count:4;    /* CSRC count */
  unsigned int extension:1;     /* header extension flag */
  unsigned int padding:1;       /* padding flag */
  unsigned int version:2;       /* protocol version */
  unsigned int payload_type:7;  /* payload type */
  unsigned int marker:1;        /* marker bit */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned int version:2;       /* protocol version */
  unsigned int padding:1;       /* padding flag */
  unsigned int extension:1;     /* header extension flag */
  unsigned int csrc_count:4;    /* CSRC count */
  unsigned int marker:1;        /* marker bit */
  unsigned int payload_type:7;  /* payload type */
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint16 seq;                  /* sequence number */
  guint32 timestamp;            /* timestamp */
  guint32 ssrc;                 /* synchronization source */
  guint32 csrc[1];              /* optional CSRC list */
};

typedef struct Rtp_Packet_Struct *Rtp_Packet;

Rtp_Packet rtp_packet_new_take_data(gpointer data, guint data_len);
Rtp_Packet rtp_packet_new_copy_data(gpointer data, guint data_len);
Rtp_Packet rtp_packet_new_allocate(guint payload_len,
                                   guint pad_len, guint csrc_count);
void rtp_packet_free(Rtp_Packet packet);
//Rtp_Packet rtp_packet_read(int fd, struct sockaddr *fromaddr, socklen_t *fromlen);
//void rtp_packet_send(Rtp_Packet packet, int fd, struct sockaddr *toaddr, socklen_t tolen);
guint8 rtp_packet_get_version(Rtp_Packet packet);
void rtp_packet_set_version(Rtp_Packet packet, guint8 version);
guint8 rtp_packet_get_padding(Rtp_Packet packet);
void rtp_packet_set_padding(Rtp_Packet packet, guint8 padding);
guint8 rtp_packet_get_csrc_count(Rtp_Packet packet);
guint8 rtp_packet_get_extension(Rtp_Packet packet);
void rtp_packet_set_extension(Rtp_Packet packet, guint8 extension);
void rtp_packet_set_csrc_count(Rtp_Packet packet, guint8 csrc_count);
guint8 rtp_packet_get_marker(Rtp_Packet packet);
void rtp_packet_set_marker(Rtp_Packet packet, guint8 marker);
guint8 rtp_packet_get_payload_type(Rtp_Packet packet);
void rtp_packet_set_payload_type(Rtp_Packet packet, guint8 payload_type);
guint16 rtp_packet_get_seq(Rtp_Packet packet);
void rtp_packet_set_seq(Rtp_Packet packet, guint16 seq);
guint32 rtp_packet_get_timestamp(Rtp_Packet packet);
void rtp_packet_set_timestamp(Rtp_Packet packet, guint32 timestamp);
guint32 rtp_packet_get_ssrc(Rtp_Packet packet);
void rtp_packet_set_ssrc(Rtp_Packet packet, guint32 ssrc);
guint rtp_packet_get_payload_len(Rtp_Packet packet);
gpointer rtp_packet_get_payload(Rtp_Packet packet);
guint rtp_packet_get_packet_len(Rtp_Packet packet);

#ifdef __cplusplus
}
#endif

#endif /* rtp-packet.h */
