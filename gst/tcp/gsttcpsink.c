/* GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gsttcpsink.h"

#define TCP_DEFAULT_HOST	"localhost"
#define TCP_DEFAULT_PORT	4953

/* elementfactory information */
static GstElementDetails gst_tcpsink_details =
GST_ELEMENT_DETAILS ("TCP packet sender",
    "Sink/Network",
    "Send data over the network via TCP",
    "Zeeshan Ali <zak147@yahoo.com>");

/* TCPSink signals and args */
enum
{
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  ARG_CONTROL,
  ARG_MTU
      /* FILL ME */
};

#define GST_TYPE_TCPSINK_CONTROL	(gst_tcpsink_control_get_type())
static GType
gst_tcpsink_control_get_type (void)
{
  static GType tcpsink_control_type = 0;
  static GEnumValue tcpsink_control[] = {
    {CONTROL_NONE, "1", "none"},
    {CONTROL_TCP, "2", "tcp"},
    {CONTROL_ZERO, NULL, NULL}
  };

  if (!tcpsink_control_type) {
    tcpsink_control_type =
        g_enum_register_static ("GstTCPSinkControl", tcpsink_control);
  }
  return tcpsink_control_type;
}

static void gst_tcpsink_base_init (gpointer g_class);
static void gst_tcpsink_class_init (GstTCPSink * klass);
static void gst_tcpsink_init (GstTCPSink * tcpsink);

static void gst_tcpsink_set_clock (GstElement * element, GstClock * clock);

static void gst_tcpsink_chain (GstPad * pad, GstData * _data);
static GstElementStateReturn gst_tcpsink_change_state (GstElement * element);

static void gst_tcpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstElementClass *parent_class = NULL;

/*static guint gst_tcpsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_tcpsink_get_type (void)
{
  static GType tcpsink_type = 0;


  if (!tcpsink_type) {
    static const GTypeInfo tcpsink_info = {
      sizeof (GstTCPSinkClass),
      gst_tcpsink_base_init,
      NULL,
      (GClassInitFunc) gst_tcpsink_class_init,
      NULL,
      NULL,
      sizeof (GstTCPSink),
      0,
      (GInstanceInitFunc) gst_tcpsink_init,
      NULL
    };

    tcpsink_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstTCPSink", &tcpsink_info,
        0);
  }
  return tcpsink_type;
}

static void
gst_tcpsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_tcpsink_details);
}

static void
gst_tcpsink_class_init (GstTCPSink * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST,
      g_param_spec_string ("host", "host", "The host/IP to send the packets to",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "port", "The port to send the packets to",
          0, 32768, TCP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CONTROL,
      g_param_spec_enum ("control", "control", "The type of control",
          GST_TYPE_TCPSINK_CONTROL, CONTROL_TCP, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MTU, g_param_spec_int ("mtu", "mtu", "mtu", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));   /* CHECKME */
  gobject_class->set_property = gst_tcpsink_set_property;
  gobject_class->get_property = gst_tcpsink_get_property;

  gstelement_class->change_state = gst_tcpsink_change_state;
  gstelement_class->set_clock = gst_tcpsink_set_clock;
}


static GstPadLinkReturn
gst_tcpsink_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstTCPSink *tcpsink;
  struct sockaddr_in serv_addr;
  struct in_addr addr;
  struct hostent *he;
  int fd;
  FILE *f;

#ifndef GST_DISABLE_LOADSAVE
  xmlDocPtr doc;

  tcpsink = GST_TCPSINK (gst_pad_get_parent (pad));

  switch (tcpsink->control) {
    case CONTROL_TCP:
      memset (&serv_addr, 0, sizeof (serv_addr));

      /* if its an IP address */
      if (inet_aton (tcpsink->host, &addr)) {
        memmove (&(serv_addr.sin_addr), &addr, sizeof (struct in_addr));
      }

      /* we dont need to lookup for localhost */
      else if (strcmp (tcpsink->host, TCP_DEFAULT_HOST) == 0) {
        if (inet_aton ("127.0.0.1", &addr)) {
          memmove (&(serv_addr.sin_addr), &addr, sizeof (struct in_addr));
        }
      }

      /* if its a hostname */
      else if ((he = gethostbyname (tcpsink->host))) {
        memmove (&(serv_addr.sin_addr), he->h_addr, he->h_length);
      }

      else {
        perror ("hostname lookup error?");
        return GST_PAD_LINK_REFUSED;
      }

      serv_addr.sin_family = AF_INET;
      serv_addr.sin_port = htons (tcpsink->port + 1);

      doc = xmlNewDoc ("1.0");
      doc->xmlRootNode = xmlNewDocNode (doc, NULL, "NewCaps", NULL);

      gst_caps_save_thyself (caps, doc->xmlRootNode);

      if ((fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        perror ("socket");
        return GST_PAD_LINK_REFUSED;
      }

      if (connect (fd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) != 0) {
        g_printerr ("tcpsink: connect to %s port %d failed: %s\n",
            tcpsink->host, tcpsink->port + 1, g_strerror (errno));
        return GST_PAD_LINK_REFUSED;
      }

      f = fdopen (dup (fd), "wb");

      xmlDocDump (f, doc);
      fclose (f);
      close (fd);
#endif
      break;
    case CONTROL_NONE:
      return GST_PAD_LINK_OK;
      break;
    default:
      return GST_PAD_LINK_REFUSED;
      break;
  }

  return GST_PAD_LINK_OK;
}

static void
gst_tcpsink_set_clock (GstElement * element, GstClock * clock)
{
  GstTCPSink *tcpsink;

  tcpsink = GST_TCPSINK (element);

  tcpsink->clock = clock;
}

static void
gst_tcpsink_init (GstTCPSink * tcpsink)
{
  /* create the sink and src pads */
  tcpsink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (tcpsink), tcpsink->sinkpad);
  gst_pad_set_chain_function (tcpsink->sinkpad, gst_tcpsink_chain);
  gst_pad_set_link_function (tcpsink->sinkpad, gst_tcpsink_sink_link);

  tcpsink->host = g_strdup (TCP_DEFAULT_HOST);
  tcpsink->port = TCP_DEFAULT_PORT;
  tcpsink->control = CONTROL_TCP;
  /* should support as minimum 576 for IPV4 and 1500 for IPV6 */
  tcpsink->mtu = 1500;

  tcpsink->clock = NULL;
}

static void
gst_tcpsink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstTCPSink *tcpsink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  tcpsink = GST_TCPSINK (GST_OBJECT_PARENT (pad));

  if (tcpsink->clock && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_element_wait (GST_ELEMENT (tcpsink), GST_BUFFER_TIMESTAMP (buf));
  }

  if (write (tcpsink->sock, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf)) <= 0) {
    perror ("write");
  }

  gst_buffer_unref (buf);
}

static void
gst_tcpsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstTCPSink *tcpsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TCPSINK (object));
  tcpsink = GST_TCPSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      if (tcpsink->host != NULL)
        g_free (tcpsink->host);
      if (g_value_get_string (value) == NULL)
        tcpsink->host = NULL;
      else
        tcpsink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      tcpsink->port = g_value_get_int (value);
      break;
    case ARG_CONTROL:
      tcpsink->control = g_value_get_enum (value);
      break;
    case ARG_MTU:
      tcpsink->mtu = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_tcpsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTCPSink *tcpsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TCPSINK (object));
  tcpsink = GST_TCPSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, tcpsink->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, tcpsink->port);
      break;
    case ARG_CONTROL:
      g_value_set_enum (value, tcpsink->control);
      break;
    case ARG_MTU:
      g_value_set_int (value, tcpsink->mtu);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_tcpsink_init_send (GstTCPSink * sink)
{
  struct hostent *he;
  struct in_addr addr;

  memset (&sink->theiraddr, 0, sizeof (sink->theiraddr));
  sink->theiraddr.sin_family = AF_INET; /* host byte order */
  sink->theiraddr.sin_port = htons (sink->port);        /* short, network byte order */

  /* if its an IP address */
  if (inet_aton (sink->host, &addr)) {
    memmove (&(sink->theiraddr.sin_addr), &addr, sizeof (struct in_addr));
  }

  /* we dont need to lookup for localhost */
  else if (strcmp (sink->host, TCP_DEFAULT_HOST) == 0) {
    if (inet_aton ("127.0.0.1", &addr)) {
      memmove (&(sink->theiraddr.sin_addr), &addr, sizeof (struct in_addr));
    }
  }

  /* if its a hostname */
  else if ((he = gethostbyname (sink->host))) {
    memmove (&(sink->theiraddr.sin_addr), he->h_addr, he->h_length);
  }

  else {
    perror ("hostname lookup error?");
    return FALSE;
  }

  if ((sink->sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    perror ("socket");
    return FALSE;
  }

  if (connect (sink->sock, (struct sockaddr *) &(sink->theiraddr),
          sizeof (sink->theiraddr)) != 0) {
    perror ("stream connect");
    return FALSE;
  }

  GST_FLAG_SET (sink, GST_TCPSINK_OPEN);

  return TRUE;
}

static void
gst_tcpsink_close (GstTCPSink * sink)
{
  close (sink->sock);

  GST_FLAG_UNSET (sink, GST_TCPSINK_OPEN);
}

static GstElementStateReturn
gst_tcpsink_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_TCPSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_TCPSINK_OPEN))
      gst_tcpsink_close (GST_TCPSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_TCPSINK_OPEN)) {
      if (!gst_tcpsink_init_send (GST_TCPSINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
