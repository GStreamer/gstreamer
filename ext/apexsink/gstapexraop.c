/* GStreamer - Remote Audio Access Protocol (RAOP) as used in Apple iTunes to stream music to the Airport Express (ApEx) -
 *
 * RAOP is based on the Real Time Streaming Protocol (RTSP) but with an extra challenge-response RSA based authentication step.
 * This interface accepts RAW PCM data and set it as AES encrypted ALAC while performing emission.
 *
 * Copyright (C) 2008 Jérémie Bernard [GRemi] <gremimail@gmail.com>
 *
 * gstapexraop.c
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstapexraop.h"

/* private constants */
#define GST_APEX_RAOP_VOLUME_MIN 	-144
#define GST_APEX_RAOP_VOLUME_MAX  	 0

#define GST_APEX_RAOP_HDR_DEFAULT_LENGTH 1024
#define GST_APEX_RAOP_SDP_DEFAULT_LENGTH 2048

const static gchar GST_APEX_RAOP_RSA_PUBLIC_MOD[] =
    "59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUtwC"
    "5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDR"
    "KSKv6kDqnw4UwPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuB"
    "OitnZ/bDzPHrTOZz0Dew0uowxf/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJ"
    "Q+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/UAaHqn9JdsBWLUEpVviYnh"
    "imNVvYFZeCXg/IdTQ+x4IRdiXNv5hEew==";

const static gchar GST_APEX_RAOP_RSA_PUBLIC_EXP[] = "AQAB";

const static gchar GST_APEX_RAOP_USER_AGENT[] =
    "iTunes/4.6 (Macintosh; U; PPC Mac OS X 10.3)";

const static guchar GST_APEX_RAOP_FRAME_HEADER[] = {    // Used by gen. 1
  0x24, 0x00, 0x00, 0x00,
  0xF0, 0xFF, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

const static int GST_APEX_RAOP_FRAME_HEADER_SIZE = 16;  // Used by gen. 1
const static int GST_APEX_RTP_FRAME_HEADER_SIZE = 12;   // Used by gen. 2

const static int GST_APEX_RAOP_ALAC_HEADER_SIZE = 3;

/* string extra utility */
static gint
g_strdel (gchar * str, gchar rc)
{
  int i = 0, j = 0, len, num = 0;
  len = strlen (str);
  while (i < len) {
    if (str[i] == rc) {
      for (j = i; j < len; j++)
        str[j] = str[j + 1];
      len--;
      num++;
    } else {
      i++;
    }
  }
  return num;
}

/* socket utilities */
static int
gst_apexraop_send (int desc, void *data, size_t len)
{
  int total = 0, bytesleft = len, n = 0;

  while (total < len) {
    n = send (desc, ((const char *) data) + total, bytesleft, 0);
    if (n == -1)
      break;
    total += n;
    bytesleft -= n;
  }

  return n == -1 ? -1 : total;
}

static int
gst_apexraop_recv (int desc, void *data, size_t len)
{
  memset (data, 0, len);
  return recv (desc, data, len, 0);
}

/* public opaque handle resolution */
typedef struct
{
  guchar aes_ky[AES_BLOCK_SIZE];        /* AES random key */
  guchar aes_iv[AES_BLOCK_SIZE];        /* AES random initial vector */

  guchar url_abspath[16];       /* header url random absolute path addon, ANNOUNCE id */
  gint cseq;                    /* header rtsp inc cseq */
  guchar cid[24];               /* header client instance id */
  gchar *session;               /* header raop negotiated session id, once SETUP performed */
  gchar *ua;                    /* header user agent */

  GstApExJackType jack_type;    /* APEX connected jack type, once ANNOUNCE performed */
  GstApExJackStatus jack_status;        /* APEX connected jack status, once ANNOUNCE performed */

  GstApExGeneration generation; /* Different devices accept different audio streams */
  GstApExTransportProtocol transport_protocol;  /* For media stream, not RAOP/RTSP */

  gchar *host;                  /* APEX target ip */
  guint ctrl_port;              /* APEX target control port */
  guint data_port;              /* APEX negotiated data port, once SETUP performed */

  int ctrl_sd;                  /* control socket */
  struct sockaddr_in ctrl_sd_in;

  int data_sd;                  /* data socket */
  struct sockaddr_in data_sd_in;

  short rtp_seq_num;            /* RTP sequence number, used by gen. 2 */
  int rtp_timestamp;            /* RTP timestamp,       used by gen. 2 */
}
_GstApExRAOP;

/* raop apex struct allocation */
GstApExRAOP *
gst_apexraop_new (const gchar * host,
    const guint16 port,
    const GstApExGeneration generation,
    const GstApExTransportProtocol transport_protocol)
{
  _GstApExRAOP *apexraop;

  apexraop = (_GstApExRAOP *) g_malloc0 (sizeof (_GstApExRAOP));

  apexraop->host = g_strdup (host);
  apexraop->ctrl_port = port;
  apexraop->ua = g_strdup (GST_APEX_RAOP_USER_AGENT);
  apexraop->jack_type = GST_APEX_JACK_TYPE_UNDEFINED;
  apexraop->jack_status = GST_APEX_JACK_STATUS_DISCONNECTED;
  apexraop->generation = generation;
  apexraop->transport_protocol = transport_protocol;
  apexraop->rtp_seq_num = 0;
  apexraop->rtp_timestamp = 0;

  return (GstApExRAOP *) apexraop;
}

/* raop apex struct freeing */
void
gst_apexraop_free (GstApExRAOP * con)
{
  _GstApExRAOP *conn;
  conn = (_GstApExRAOP *) con;

  g_free (conn->host);
  g_free (conn->session);
  g_free (conn->ua);
  g_free (conn);
}

/* host affectation */
void
gst_apexraop_set_host (GstApExRAOP * con, const gchar * host)
{
  _GstApExRAOP *conn;
  conn = (_GstApExRAOP *) con;

  g_free (conn->host);
  conn->host = g_strdup (host);
}

/* host reader */
gchar *
gst_apexraop_get_host (GstApExRAOP * con)
{
  _GstApExRAOP *conn;
  conn = (_GstApExRAOP *) con;

  return g_strdup (conn->host);
}

/* control port affectation */
void
gst_apexraop_set_port (GstApExRAOP * con, const guint16 port)
{
  _GstApExRAOP *conn;
  conn = (_GstApExRAOP *) con;

  conn->ctrl_port = port;
}

/* control port reader */
guint16
gst_apexraop_get_port (GstApExRAOP * con)
{
  _GstApExRAOP *conn;
  conn = (_GstApExRAOP *) con;

  return conn->ctrl_port;
}

/* user agent affectation */
void
gst_apexraop_set_useragent (GstApExRAOP * con, const gchar * useragent)
{
  _GstApExRAOP *conn;
  conn = (_GstApExRAOP *) con;

  g_free (conn->ua);
  conn->ua = g_strdup (useragent);
}

/* user agent reader */
gchar *
gst_apexraop_get_useragent (GstApExRAOP * con)
{
  _GstApExRAOP *conn;
  conn = (_GstApExRAOP *) con;

  return g_strdup (conn->ua);
}

/* raop apex connection sequence */
GstRTSPStatusCode
gst_apexraop_connect (GstApExRAOP * con)
{
  gchar *ac, *ky, *iv, *s, inaddr[INET_ADDRSTRLEN],
      creq[GST_APEX_RAOP_SDP_DEFAULT_LENGTH],
      hreq[GST_APEX_RAOP_HDR_DEFAULT_LENGTH], *req;
  RSA *rsa;
  guchar *mod, *exp, rsakey[512];
  union gst_randbytes
  {
    struct asvals
    {
      gulong url_key;
      guint64 conn_id;
      guchar challenge[16];
    } v;
    guchar buf[4 + 8 + 16];
  } randbuf;
  gsize size;
  struct sockaddr_in ioaddr;
  socklen_t iolen;
  GstRTSPStatusCode res;
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  if ((conn->ctrl_sd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    return GST_RTSP_STS_DESTINATION_UNREACHABLE;

  conn->ctrl_sd_in.sin_family = AF_INET;
  conn->ctrl_sd_in.sin_port = htons (conn->ctrl_port);

  if (!inet_aton (conn->host, &conn->ctrl_sd_in.sin_addr)) {
    struct hostent *hp = (struct hostent *) gethostbyname (conn->host);
    if (hp == NULL)
      return GST_RTSP_STS_DESTINATION_UNREACHABLE;
    memcpy (&conn->ctrl_sd_in.sin_addr, hp->h_addr, hp->h_length);
  }

  if (connect (conn->ctrl_sd, (struct sockaddr *) &conn->ctrl_sd_in,
          sizeof (conn->ctrl_sd_in)) < 0)
    return GST_RTSP_STS_DESTINATION_UNREACHABLE;

  RAND_bytes (randbuf.buf, sizeof (randbuf));
  sprintf ((gchar *) conn->url_abspath, "%lu", randbuf.v.url_key);
  sprintf ((char *) conn->cid, "%16" G_GINT64_MODIFIER "x", randbuf.v.conn_id);

  RAND_bytes (conn->aes_ky, AES_BLOCK_SIZE);
  RAND_bytes (conn->aes_iv, AES_BLOCK_SIZE);

  rsa = RSA_new ();
  mod = g_base64_decode (GST_APEX_RAOP_RSA_PUBLIC_MOD, &size);
  rsa->n = BN_bin2bn (mod, size, NULL);
  exp = g_base64_decode (GST_APEX_RAOP_RSA_PUBLIC_EXP, &size);
  rsa->e = BN_bin2bn (exp, size, NULL);
  size =
      RSA_public_encrypt (AES_BLOCK_SIZE, conn->aes_ky, rsakey, rsa,
      RSA_PKCS1_OAEP_PADDING);

  ky = g_base64_encode (rsakey, size);
  iv = g_base64_encode (conn->aes_iv, AES_BLOCK_SIZE);
  g_strdel (ky, '=');
  g_strdel (iv, '=');

  iolen = sizeof (struct sockaddr);
  getsockname (conn->ctrl_sd, (struct sockaddr *) &ioaddr, &iolen);
  inet_ntop (AF_INET, &(ioaddr.sin_addr), inaddr, INET_ADDRSTRLEN);

  ac = g_base64_encode (randbuf.v.challenge, 16);
  g_strdel (ac, '=');

  sprintf (creq,
      "v=0\r\n"
      "o=iTunes %s 0 IN IP4 %s\r\n"
      "s=iTunes\r\n"
      "c=IN IP4 %s\r\n"
      "t=0 0\r\n"
      "m=audio 0 RTP/AVP 96\r\n"
      "a=rtpmap:96 AppleLossless\r\n"
      "a=fmtp:96 %d 0 %d 40 10 14 %d 255 0 0 %d\r\n"
      "a=rsaaeskey:%s\r\n"
      "a=aesiv:%s\r\n",
      conn->url_abspath,
      inaddr,
      conn->host,
      conn->generation == GST_APEX_GENERATION_ONE
      ? GST_APEX_RAOP_V1_SAMPLES_PER_FRAME
      : GST_APEX_RAOP_V2_SAMPLES_PER_FRAME,
      GST_APEX_RAOP_BYTES_PER_CHANNEL * 8,
      GST_APEX_RAOP_CHANNELS, GST_APEX_RAOP_BITRATE, ky, iv);

  sprintf (hreq,
      "ANNOUNCE rtsp://%s/%s RTSP/1.0\r\n"
      "CSeq: %d\r\n"
      "Client-Instance: %s\r\n"
      "User-Agent: %s\r\n"
      "Content-Type: application/sdp\r\n"
      "Content-Length: %u\r\n"
      "Apple-Challenge: %s\r\n",
      conn->host,
      conn->url_abspath, ++conn->cseq, conn->cid, conn->ua,
      (guint) strlen (creq), ac);

  RSA_free (rsa);
  g_free (ky);
  g_free (iv);
  g_free (ac);
  g_free (mod);
  g_free (exp);

  req = g_strconcat (hreq, "\r\n", creq, NULL);

  if (gst_apexraop_send (conn->ctrl_sd, req, strlen (req)) <= 0) {
    g_free (req);
    return GST_RTSP_STS_GONE;
  }

  g_free (req);

  if (gst_apexraop_recv (conn->ctrl_sd, hreq,
          GST_APEX_RAOP_HDR_DEFAULT_LENGTH) <= 0)
    return GST_RTSP_STS_GONE;

  {
    int tmp;
    sscanf (hreq, "%*s %d", &tmp);
    res = (GstRTSPStatusCode) tmp;
  }

  if (res != GST_RTSP_STS_OK)
    return res;

  s = g_strrstr (hreq, "Audio-Jack-Status");

  if (s != NULL) {
    gchar status[128];
    sscanf (s, "%*s %s", status);

    if (strcmp (status, "connected;") == 0)
      conn->jack_status = GST_APEX_JACK_STATUS_CONNECTED;
    else if (strcmp (status, "disconnected;") == 0)
      conn->jack_status = GST_APEX_JACK_STATUS_DISCONNECTED;
    else
      conn->jack_status = GST_APEX_JACK_STATUS_UNDEFINED;

    s = g_strrstr (s, "type=");

    if (s != NULL) {
      strtok (s, "=");
      s = strtok (NULL, "\n");

      if (strcmp (s, "analog"))
        conn->jack_type = GST_APEX_JACK_TYPE_ANALOG;
      else if (strcmp (s, "digital"))
        conn->jack_type = GST_APEX_JACK_TYPE_DIGITAL;
      else
        conn->jack_type = GST_APEX_JACK_TYPE_UNDEFINED;
    }
  }

  sprintf (hreq,
      "SETUP rtsp://%s/%s RTSP/1.0\r\n"
      "CSeq: %d\r\n"
      "Client-Instance: %s\r\n"
      "User-Agent: %s\r\n"
      "Transport: RTP/AVP/TCP;unicast;interleaved=0-1;mode=record\r\n"
      "\r\n", conn->host, conn->url_abspath, ++conn->cseq, conn->cid, conn->ua);

  if (gst_apexraop_send (conn->ctrl_sd, hreq, strlen (hreq)) <= 0)
    return GST_RTSP_STS_GONE;

  if (gst_apexraop_recv (conn->ctrl_sd, hreq,
          GST_APEX_RAOP_HDR_DEFAULT_LENGTH) <= 0)
    return GST_RTSP_STS_GONE;

  {
    int tmp;
    sscanf (hreq, "%*s %d", &tmp);
    res = (GstRTSPStatusCode) tmp;
  }

  if (res != GST_RTSP_STS_OK)
    return res;

  s = g_strrstr (hreq, "Session");

  if (s != NULL) {
    gchar session[128];
    sscanf (s, "%*s %s", session);
    conn->session = g_strdup (session);
  } else
    return GST_RTSP_STS_PRECONDITION_FAILED;

  s = g_strrstr (hreq, "server_port");
  if (s != NULL) {
    sscanf (s, "server_port=%d", &conn->data_port);
  } else
    return GST_RTSP_STS_PRECONDITION_FAILED;

  sprintf (hreq,
      "RECORD rtsp://%s/%s RTSP/1.0\r\n"
      "CSeq: %d\r\n"
      "Client-Instance: %s\r\n"
      "User-Agent: %s\r\n"
      "Session: %s\r\n"
      "Range: npt=0-\r\n"
      "RTP-Info: seq=0;rtptime=0\r\n"
      "\r\n",
      conn->host,
      conn->url_abspath, ++conn->cseq, conn->cid, conn->ua, conn->session);

  if (gst_apexraop_send (conn->ctrl_sd, hreq, strlen (hreq)) <= 0)
    return GST_RTSP_STS_GONE;

  if (gst_apexraop_recv (conn->ctrl_sd, hreq,
          GST_APEX_RAOP_HDR_DEFAULT_LENGTH) <= 0)
    return GST_RTSP_STS_GONE;

  {
    int tmp;
    sscanf (hreq, "%*s %d", &tmp);
    res = (GstRTSPStatusCode) tmp;
  }

  if (res != GST_RTSP_STS_OK)
    return res;

  if (conn->transport_protocol == GST_APEX_TCP) {
    if ((conn->data_sd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
      return GST_RTSP_STS_DESTINATION_UNREACHABLE;
  } else if (conn->transport_protocol == GST_APEX_UDP) {
    if ((conn->data_sd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
      return GST_RTSP_STS_DESTINATION_UNREACHABLE;
  } else
    return GST_RTSP_STS_METHOD_NOT_ALLOWED;

  conn->data_sd_in.sin_family = AF_INET;
  conn->data_sd_in.sin_port = htons (conn->data_port);

  memcpy (&conn->data_sd_in.sin_addr, &conn->ctrl_sd_in.sin_addr,
      sizeof (conn->ctrl_sd_in.sin_addr));

  if (connect (conn->data_sd, (struct sockaddr *) &conn->data_sd_in,
          sizeof (conn->data_sd_in)) < 0)
    return GST_RTSP_STS_DESTINATION_UNREACHABLE;

  return res;
}

/* raop apex jack type access */
GstApExJackType
gst_apexraop_get_jacktype (GstApExRAOP * con)
{
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  if (!conn)
    return GST_APEX_JACK_TYPE_UNDEFINED;

  return conn->jack_type;
}

/* raop apex jack status access */
GstApExJackStatus
gst_apexraop_get_jackstatus (GstApExRAOP * con)
{
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  if (!conn)
    return GST_APEX_JACK_STATUS_UNDEFINED;

  return conn->jack_status;
}

/* raop apex generation access */
GstApExGeneration
gst_apexraop_get_generation (GstApExRAOP * con)
{
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  if (!conn)
    return GST_APEX_GENERATION_ONE;

  return conn->generation;
}

/* raop apex transport protocol access */
GstApExTransportProtocol
gst_apexraop_get_transport_protocol (GstApExRAOP * con)
{
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  if (!conn)
    return GST_APEX_TCP;

  return conn->transport_protocol;
}

/* raop apex sockets close */
void
gst_apexraop_close (GstApExRAOP * con)
{
  gchar hreq[GST_APEX_RAOP_HDR_DEFAULT_LENGTH];
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  sprintf (hreq,
      "TEARDOWN rtsp://%s/%s RTSP/1.0\r\n"
      "CSeq: %d\r\n"
      "Client-Instance: %s\r\n"
      "User-Agent: %s\r\n"
      "Session: %s\r\n"
      "\r\n",
      conn->host,
      conn->url_abspath, ++conn->cseq, conn->cid, conn->ua, conn->session);

  gst_apexraop_send (conn->ctrl_sd, hreq, strlen (hreq));
  gst_apexraop_recv (conn->ctrl_sd, hreq, GST_APEX_RAOP_HDR_DEFAULT_LENGTH);

  if (conn->ctrl_sd != 0)
    close (conn->ctrl_sd);
  if (conn->data_sd != 0)
    close (conn->data_sd);
}

/* raop apex volume set */
GstRTSPStatusCode
gst_apexraop_set_volume (GstApExRAOP * con, const guint volume)
{
  gint v;
  gchar creq[GST_APEX_RAOP_SDP_DEFAULT_LENGTH],
      hreq[GST_APEX_RAOP_HDR_DEFAULT_LENGTH], *req, vol[128];
  GstRTSPStatusCode res;
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  v = GST_APEX_RAOP_VOLUME_MIN + (GST_APEX_RAOP_VOLUME_MAX -
      GST_APEX_RAOP_VOLUME_MIN) * volume / 100.;
  sprintf (vol, "volume: %d.000000\r\n", v);

  sprintf (creq, "%s\r\n", vol);

  sprintf (hreq,
      "SET_PARAMETER rtsp://%s/%s RTSP/1.0\r\n"
      "CSeq: %d\r\n"
      "Client-Instance: %s\r\n"
      "User-Agent: %s\r\n"
      "Session: %s\r\n"
      "Content-Type: text/parameters\r\n"
      "Content-Length: %u\r\n",
      conn->host,
      conn->url_abspath,
      ++conn->cseq, conn->cid, conn->ua, conn->session, (guint) strlen (creq)
      );

  req = g_strconcat (hreq, "\r\n", creq, NULL);

  if (gst_apexraop_send (conn->ctrl_sd, req, strlen (req)) <= 0) {
    g_free (req);
    return GST_RTSP_STS_GONE;
  }

  g_free (req);

  if (gst_apexraop_recv (conn->ctrl_sd, hreq,
          GST_APEX_RAOP_HDR_DEFAULT_LENGTH) <= 0)
    return GST_RTSP_STS_GONE;

  {
    int tmp;
    sscanf (hreq, "%*s %d", &tmp);
    res = (GstRTSPStatusCode) tmp;
  }

  return res;
}

/* raop apex raw data alac encapsulation, encryption and emission, http://wiki.multimedia.cx/index.php?title=Apple_Lossless_Audio_Coding */
static void inline
gst_apexraop_write_bits (guchar * buffer, int data, int numbits,
    int *bit_offset, int *byte_offset)
{
  const static guchar masks[] =
      { 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };

  if (((*bit_offset) != 0) && (((*bit_offset) + numbits) > 8)) {
    gint numwritebits;
    guchar bitstowrite;

    numwritebits = 8 - (*bit_offset);
    bitstowrite =
        (guchar) ((data >> (numbits - numwritebits)) << (8 - (*bit_offset) -
            numwritebits));
    buffer[(*byte_offset)] |= bitstowrite;
    numbits -= numwritebits;
    (*bit_offset) = 0;
    (*byte_offset)++;
  }

  while (numbits >= 8) {
    guchar bitstowrite;

    bitstowrite = (guchar) ((data >> (numbits - 8)) & 0xFF);
    buffer[(*byte_offset)] |= bitstowrite;
    numbits -= 8;
    (*bit_offset) = 0;
    (*byte_offset)++;
  }

  if (numbits > 0) {
    guchar bitstowrite;
    bitstowrite =
        (guchar) ((data & masks[numbits]) << (8 - (*bit_offset) - numbits));
    buffer[(*byte_offset)] |= bitstowrite;
    (*bit_offset) += numbits;
    if ((*bit_offset) == 8) {
      (*byte_offset)++;
      (*bit_offset) = 0;
    }
  }
}

guint
gst_apexraop_write (GstApExRAOP * con, gpointer rawdata, guint length)
{
  guchar *buffer, *frame_data;
  gushort len;
  gint bit_offset, byte_offset, i, out_len, res;
  EVP_CIPHER_CTX aes_ctx;
  _GstApExRAOP *conn = (_GstApExRAOP *) con;
  const int frame_header_size = conn->generation == GST_APEX_GENERATION_ONE
      ? GST_APEX_RAOP_FRAME_HEADER_SIZE : GST_APEX_RTP_FRAME_HEADER_SIZE;

  buffer =
      (guchar *) g_malloc0 (frame_header_size +
      GST_APEX_RAOP_ALAC_HEADER_SIZE + length);

  if (conn->generation == GST_APEX_GENERATION_ONE) {
    g_assert (frame_header_size == GST_APEX_RAOP_FRAME_HEADER_SIZE);
    memcpy (buffer, GST_APEX_RAOP_FRAME_HEADER, frame_header_size);

    len = length + frame_header_size + GST_APEX_RAOP_ALAC_HEADER_SIZE - 4;

    buffer[2] = len >> 8;
    buffer[3] = len & 0xff;
  } else {
    /* Gen. 2 uses RTP-like header (RFC 3550). */
    short network_seq_num;
    int network_timestamp, unknown_const;
    static gboolean first = TRUE;

    buffer[0] = 0x80;
    if (first) {
      buffer[1] = 0xe0;
      first = FALSE;
    } else
      buffer[1] = 0x60;

    network_seq_num = htons (conn->rtp_seq_num++);
    memcpy (buffer + 2, &network_seq_num, 2);

    network_timestamp = htons (conn->rtp_timestamp);
    memcpy (buffer + 4, &network_timestamp, 4);
    conn->rtp_timestamp += GST_APEX_RAOP_V2_SAMPLES_PER_FRAME;

    unknown_const = 0xdeadbeef;
    memcpy (buffer + 8, &unknown_const, 4);
  }

  bit_offset = 0;
  byte_offset = 0;
  frame_data = buffer + frame_header_size;

  gst_apexraop_write_bits (frame_data, 1, 3, &bit_offset, &byte_offset);        /* channels, 0 mono, 1 stereo */
  gst_apexraop_write_bits (frame_data, 0, 4, &bit_offset, &byte_offset);        /* unknown */
  gst_apexraop_write_bits (frame_data, 0, 8, &bit_offset, &byte_offset);        /* unknown (12 bits) */
  gst_apexraop_write_bits (frame_data, 0, 4, &bit_offset, &byte_offset);
  gst_apexraop_write_bits (frame_data, 0, 1, &bit_offset, &byte_offset);        /* has size flag */
  gst_apexraop_write_bits (frame_data, 0, 2, &bit_offset, &byte_offset);        /* unknown */
  gst_apexraop_write_bits (frame_data, 1, 1, &bit_offset, &byte_offset);        /* no compression flag */

  for (i = 0; i < length; i += 2) {
    gst_apexraop_write_bits (frame_data, ((guchar *) rawdata)[i + 1], 8,
        &bit_offset, &byte_offset);
    gst_apexraop_write_bits (frame_data, ((guchar *) rawdata)[i], 8,
        &bit_offset, &byte_offset);
  }

  EVP_CIPHER_CTX_init (&aes_ctx);
  EVP_CipherInit_ex (&aes_ctx, EVP_aes_128_cbc (), NULL, conn->aes_ky,
      conn->aes_iv, AES_ENCRYPT);
  EVP_CipherUpdate (&aes_ctx, frame_data, &out_len, frame_data, /*( */
      GST_APEX_RAOP_ALAC_HEADER_SIZE +
      length /*) / AES_BLOCK_SIZE * AES_BLOCK_SIZE */ );
  EVP_CIPHER_CTX_cleanup (&aes_ctx);

  res =
      gst_apexraop_send (conn->data_sd, buffer,
      frame_header_size + GST_APEX_RAOP_ALAC_HEADER_SIZE + length);

  g_free (buffer);

  return (guint) ((res >=
          (frame_header_size +
              GST_APEX_RAOP_ALAC_HEADER_SIZE)) ? (res -
          frame_header_size - GST_APEX_RAOP_ALAC_HEADER_SIZE) : 0);
}

/* raop apex buffer flush */
GstRTSPStatusCode
gst_apexraop_flush (GstApExRAOP * con)
{
  gchar hreq[GST_APEX_RAOP_HDR_DEFAULT_LENGTH];
  GstRTSPStatusCode res;
  _GstApExRAOP *conn;

  conn = (_GstApExRAOP *) con;

  sprintf (hreq,
      "FLUSH rtsp://%s/%s RTSP/1.0\r\n"
      "CSeq: %d\r\n"
      "Client-Instance: %s\r\n"
      "User-Agent: %s\r\n"
      "Session: %s\r\n"
      "RTP-Info: seq=%d;rtptime=%d\r\n"
      "\r\n",
      conn->host,
      conn->url_abspath,
      ++conn->cseq,
      conn->cid,
      conn->ua, conn->session, conn->rtp_seq_num, conn->rtp_timestamp);

  if (gst_apexraop_send (conn->ctrl_sd, hreq, strlen (hreq)) <= 0)
    return GST_RTSP_STS_GONE;

  if (gst_apexraop_recv (conn->ctrl_sd, hreq,
          GST_APEX_RAOP_HDR_DEFAULT_LENGTH) <= 0)
    return GST_RTSP_STS_GONE;

  {
    int tmp;
    sscanf (hreq, "%*s %d", &tmp);
    res = (GstRTSPStatusCode) tmp;
  }

  return res;
}
