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


#include "gstudpsink.h"

#define UDP_DEFAULT_HOST	"localhost"
#define UDP_DEFAULT_PORT	4951

/* elementfactory information */
GstElementDetails gst_udpsink_details = {
  "UDP packet sender",
  "Transport/",
  "",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

/* UDPSink signals and args */
enum {
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  /* FILL ME */
};

static void		gst_udpsink_class_init		(GstUDPSink *klass);
static void		gst_udpsink_init		(GstUDPSink *udpsink);

static void		gst_udpsink_chain		(GstPad *pad,GstBuffer *buf);
static GstElementStateReturn gst_udpsink_change_state 	(GstElement *element);

static void 		gst_udpsink_set_property 	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 		gst_udpsink_get_property 	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);


static GstElementClass *parent_class = NULL;
//static guint gst_udpsink_signals[LAST_SIGNAL] = { 0 };

GType
gst_udpsink_get_type (void)
{
  static GType udpsink_type = 0;


  if (!udpsink_type) {
    static const GTypeInfo udpsink_info = {
      sizeof(GstUDPSinkClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_udpsink_class_init,
      NULL,
      NULL,
      sizeof(GstUDPSink),
      0,
      (GInstanceInitFunc)gst_udpsink_init,
      NULL
    };
    udpsink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstUDPSink", &udpsink_info, 0);
  }
  return udpsink_type;
}

static void
gst_udpsink_class_init (GstUDPSink *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST,
    g_param_spec_string ("host", "nost", "The host to send the packets to",
                         UDP_DEFAULT_HOST, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
    g_param_spec_int ("port", "port", "The port to send the packets to",
                       0, 32768, UDP_DEFAULT_PORT, G_PARAM_READWRITE)); 

  gobject_class->set_property = gst_udpsink_set_property;
  gobject_class->get_property = gst_udpsink_get_property;

  gstelement_class->change_state = gst_udpsink_change_state;
}


static void
gst_udpsink_newcaps (GstPad *pad, GstCaps *caps)
{
  GstUDPSink *udpsink;
  struct sockaddr_in serv_addr;
  struct hostent *serverhost;
  int fd;
  FILE *f;
#ifndef GST_DISABLE_LOADSAVE
  xmlDocPtr doc;
#endif

  udpsink = GST_UDPSINK (gst_pad_get_parent (pad));

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
     perror("socket");
     return;
  }
  memset(&serv_addr, 0, sizeof(serv_addr));
  /* its a name rather than an ipnum */
  serverhost = gethostbyname(udpsink->host);
  if (serverhost == (struct hostent *)0) {
    perror("gethostbyname");
    return;
  }
  memmove(&serv_addr.sin_addr,serverhost->h_addr, serverhost->h_length);

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(udpsink->port);

  if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
    g_printerr ("udpsink: connect to %s port %d failed: %s\n",
		udpsink->host, udpsink->port, sys_errlist[errno]);
    return;
  }
  f = fdopen (dup (fd), "wb");

#ifndef GST_DISABLE_LOADSAVE
  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "NewCaps", NULL);

  gst_caps_save_thyself (caps, doc->xmlRootNode);
  xmlDocDump(f, doc);
#endif

  fclose (f);
  close (fd);
}

static void
gst_udpsink_init (GstUDPSink *udpsink)
{
  /* create the sink and src pads */
  udpsink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (udpsink), udpsink->sinkpad);
  gst_pad_set_chain_function (udpsink->sinkpad, gst_udpsink_chain);
  gst_pad_set_newcaps_function (udpsink->sinkpad, gst_udpsink_newcaps);

  udpsink->host = g_strdup (UDP_DEFAULT_HOST);
  udpsink->port = UDP_DEFAULT_PORT;
}

static void
gst_udpsink_chain (GstPad *pad, GstBuffer *buf)
{
  GstUDPSink *udpsink;
  int tolen;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  udpsink = GST_UDPSINK (GST_OBJECT_PARENT (pad));

  tolen = sizeof(udpsink->theiraddr);

  if (sendto (udpsink->sock, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), 0, 
			  (struct sockaddr *) &udpsink->theiraddr, tolen) == -1)
  {
    perror("sending");
  } 

  gst_buffer_unref(buf);
}

static void
gst_udpsink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstUDPSink *udpsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_UDPSINK(object));
  udpsink = GST_UDPSINK(object);

  switch (prop_id) {
    case ARG_HOST:
      if (udpsink->host != NULL) g_free(udpsink->host);
      if (g_value_get_string (value) == NULL)
        udpsink->host = NULL;
      else
        udpsink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
        udpsink->port = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_udpsink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstUDPSink *udpsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_UDPSINK(object));
  udpsink = GST_UDPSINK(object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, udpsink->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, udpsink->port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


// create a socket for sending to remote machine
static gboolean
gst_udpsink_init_send (GstUDPSink *sink)
{
  struct hostent *he;

  bzero (&sink->theiraddr, sizeof (sink->theiraddr));
  sink->theiraddr.sin_family = AF_INET;         // host byte order
  sink->theiraddr.sin_port = htons (sink->port);     // short, network byte order
  if ((he = gethostbyname (sink->host)) == NULL) {
    perror("gethostbyname");
    return FALSE;
  }
  sink->theiraddr.sin_addr = *((struct in_addr *) he->h_addr);

  if ((sink->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket");
    return FALSE;
  }

  GST_FLAG_SET (sink, GST_UDPSINK_OPEN);

  return TRUE;
}

static void
gst_udpsink_close (GstUDPSink *sink)
{
  close (sink->sock);

  GST_FLAG_UNSET (sink, GST_UDPSINK_OPEN);
}

static GstElementStateReturn
gst_udpsink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_UDPSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_UDPSINK_OPEN))
      gst_udpsink_close (GST_UDPSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_UDPSINK_OPEN)) {
      if (!gst_udpsink_init_send (GST_UDPSINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

