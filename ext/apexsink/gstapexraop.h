/* GStreamer - Remote Audio Access Protocol (RAOP) as used in Apple iTunes to stream music to the Airport Express (ApEx) -
 *
 * RAOP is based on the Real Time Streaming Protocol (RTSP) but with an extra challenge-response RSA based authentication step.
 * This interface accepts RAW PCM data and set it as AES encrypted ALAC while performing emission.
 *
 * Copyright (C) 2008 Jérémie Bernard [GRemi] <gremimail@gmail.com>
 *
 * gstapexraop.h
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
 *
 */

#ifndef __GST_APEXRAOP_H__
#define __GST_APEXRAOP_H__

#include <gst/gst.h>
#include <gst/rtsp/gstrtspdefs.h>

#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/aes.h>
#include <openssl/evp.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netdb.h>

#include <arpa/inet.h>

G_BEGIN_DECLS

/* raop fixed parameters */
#define GST_APEX_RAOP_BITRATE				44100
#define GST_APEX_RAOP_V1_SAMPLES_PER_FRAME		4096
#define GST_APEX_RAOP_V2_SAMPLES_PER_FRAME		352
#define GST_APEX_RAOP_BYTES_PER_CHANNEL			2
#define GST_APEX_RAOP_CHANNELS				2
#define GST_APEX_RAOP_BYTES_PER_SAMPLE			(GST_APEX_RAOP_CHANNELS * GST_APEX_RAOP_BYTES_PER_CHANNEL)

/* gst associated caps fields specification */
#define GST_APEX_RAOP_INPUT_TYPE			"audio/x-raw-int"
#define GST_APEX_RAOP_INPUT_WIDTH			"16"
#define GST_APEX_RAOP_INPUT_DEPTH	            	GST_APEX_RAOP_INPUT_WIDTH
#define GST_APEX_RAOP_INPUT_ENDIAN			"LITTLE_ENDIAN"
#define GST_APEX_RAOP_INPUT_CHANNELS            	"2"
#define GST_APEX_RAOP_INPUT_BIT_RATE			"44100"
#define GST_APEX_RAOP_INPUT_SIGNED			"TRUE"

typedef enum
{
  GST_APEX_JACK_TYPE_UNDEFINED = 0,
  GST_APEX_JACK_TYPE_ANALOG,
  GST_APEX_JACK_TYPE_DIGITAL,
}
GstApExJackType;

typedef enum
{
  GST_APEX_JACK_STATUS_UNDEFINED = 0,
  GST_APEX_JACK_STATUS_DISCONNECTED,
  GST_APEX_JACK_STATUS_CONNECTED,
}
GstApExJackStatus;

typedef enum
{
  GST_APEX_GENERATION_ONE = 1,
  GST_APEX_GENERATION_TWO,
}
GstApExGeneration;

typedef enum
{
  GST_APEX_TCP = 0,
  GST_APEX_UDP,
}
GstApExTransportProtocol;

/* raop context handle */
typedef struct
{
} GstApExRAOP;

/* host might be null and port might be 0 while instanciating */
GstApExRAOP *gst_apexraop_new (const gchar * host,
                               const guint16 port,
			       const GstApExGeneration generation,
			       const GstApExTransportProtocol transport_protocol);
void gst_apexraop_free (GstApExRAOP * conn);

/* must not be connected yet while setting the host target */
void gst_apexraop_set_host (GstApExRAOP * conn, const gchar * host);
gchar *gst_apexraop_get_host (GstApExRAOP * conn);

/* must not be connected yet while setting the port target */
void gst_apexraop_set_port (GstApExRAOP * conn, const guint16 port);
guint16 gst_apexraop_get_port (GstApExRAOP * conn);

/* optional affectation, default iTunes user agent internaly used */
void gst_apexraop_set_useragent (GstApExRAOP * conn, const gchar * useragent);
gchar *gst_apexraop_get_useragent (GstApExRAOP * conn);

/* once allocation and configuration performed, manages the raop ANNOUNCE, SETUP and RECORD sequences, 
 * open both ctrl and data channels */
GstRTSPStatusCode gst_apexraop_connect (GstApExRAOP * conn);

/* close the currently used session, manages raop TEARDOWN sequence and closes the used sockets */
void gst_apexraop_close (GstApExRAOP * conn);

/* once connected, set the apex target volume, manages SET_PARAMETER sequence */
GstRTSPStatusCode gst_apexraop_set_volume (GstApExRAOP * conn,
    const guint volume);

/* write raw samples typed as defined by the fixed raop parameters, flush the apex buffer */
guint gst_apexraop_write (GstApExRAOP * conn, gpointer rawdata, guint length);
GstRTSPStatusCode gst_apexraop_flush (GstApExRAOP * conn);

/* retrieve the connected apex jack type and status */
GstApExJackType gst_apexraop_get_jacktype (GstApExRAOP * conn);
GstApExJackStatus gst_apexraop_get_jackstatus (GstApExRAOP * conn);

/* retrieve the generation */
GstApExGeneration gst_apexraop_get_generation (GstApExRAOP * conn);

/* retrieve the transport protocol */
GstApExTransportProtocol gst_apexraop_get_transport_protocol (GstApExRAOP * conn);

G_END_DECLS

#endif

