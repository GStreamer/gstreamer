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


#include "gstudpsrc.h"

#define UDP_DEFAULT_PORT	4951

/* elementfactory information */
GstElementDetails gst_udpsrc_details = {
  "UDP packet receiver",
  "Transport/",
  "",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

/* UDPSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_PORT,
  /* FILL ME */
};

static void		gst_udpsrc_class_init		(GstUDPSrc *klass);
static void		gst_udpsrc_init			(GstUDPSrc *udpsrc);

static GstBuffer*	gst_udpsrc_get			(GstPad *pad);
static GstElementStateReturn
			gst_udpsrc_change_state 	(GstElement *element);

static void 		gst_udpsrc_set_property 	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 		gst_udpsrc_get_property 	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_udpsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_udpsrc_get_type (void)
{
  static GType udpsrc_type = 0;


  if (!udpsrc_type) {
    static const GTypeInfo udpsrc_info = {
      sizeof(GstUDPSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_udpsrc_class_init,
      NULL,
      NULL,
      sizeof(GstUDPSrc),
      0,
      (GInstanceInitFunc)gst_udpsrc_init,
      NULL
    };
    udpsrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstUDPSrc", &udpsrc_info, 0);
  }
  return udpsrc_type;
}

static void
gst_udpsrc_class_init (GstUDPSrc *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
    g_param_spec_int ("port", "port", "The port to receive the packets from",
                       0, 32768, UDP_DEFAULT_PORT, G_PARAM_READWRITE)); 

  gobject_class->set_property = gst_udpsrc_set_property;
  gobject_class->get_property = gst_udpsrc_get_property;

  gstelement_class->change_state = gst_udpsrc_change_state;
}


static void
gst_udpsrc_init (GstUDPSrc *udpsrc)
{
  /* create the src and src pads */
  udpsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (udpsrc), udpsrc->srcpad);
  gst_pad_set_get_function (udpsrc->srcpad, gst_udpsrc_get);

  udpsrc->port = UDP_DEFAULT_PORT;
}

static GstBuffer*
gst_udpsrc_get (GstPad *pad)
{
  GstUDPSrc *udpsrc;
  GstBuffer *outbuf;
  struct sockaddr_in tmpaddr;
  int len, numbytes;
  fd_set read_fds;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  udpsrc = GST_UDPSRC (GST_OBJECT_PARENT (pad));

  FD_ZERO (&read_fds);
  FD_SET (udpsrc->control_sock, &read_fds);
  FD_SET (udpsrc->sock, &read_fds);

  if (select (udpsrc->control_sock+1, &read_fds, NULL, NULL, NULL) > 0) {
    if (FD_ISSET (udpsrc->control_sock, &read_fds)) {
#ifndef GST_DISABLE_LOADSAVE
      guchar *buf;
      int ret;
      int fdread;
      struct sockaddr addr;
      socklen_t len;
      xmlDocPtr doc;
      GstCaps *caps;

      buf = g_malloc (1024*10);

      len = sizeof (struct sockaddr);
      fdread = accept (udpsrc->control_sock, &addr, &len);
      if (fdread < 0) {
	perror ("accept");
      }
      
      ret = read (fdread, buf, 1024*10);
      if (ret < 0) {
	perror ("read");
      }
      buf[ret] = '\0';
      doc = xmlParseMemory(buf, ret);
      caps = gst_caps_load_thyself(doc->xmlRootNode);
      
      gst_pad_try_set_caps (udpsrc->srcpad, caps);

#endif
      outbuf = NULL;
    }
    else {
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = g_malloc (24000);
      GST_BUFFER_SIZE (outbuf) = 24000;

      numbytes = recvfrom (udpsrc->sock, GST_BUFFER_DATA (outbuf),
		  GST_BUFFER_SIZE (outbuf), 0, (struct sockaddr *)&tmpaddr, &len);

      if (numbytes != -1) {
        GST_BUFFER_SIZE (outbuf) = numbytes;
      }
      else {
	perror ("recvfrom");
        gst_buffer_unref (outbuf);
        outbuf = NULL;
      }
  
    }
  }
  else {
    perror ("select");
    outbuf = NULL;
  }
  return outbuf;
}


static void
gst_udpsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstUDPSrc *udpsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_UDPSRC(object));
  udpsrc = GST_UDPSRC(object);

  switch (prop_id) {
    case ARG_PORT:
        udpsrc->port = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_udpsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstUDPSrc *udpsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_UDPSRC(object));
  udpsrc = GST_UDPSRC(object);

  switch (prop_id) {
    case ARG_PORT:
      g_value_set_int (value, udpsrc->port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_udpsrc_init_receive (GstUDPSrc *src)
{
  bzero (&src->myaddr, sizeof (src->myaddr));
  src->myaddr.sin_family = AF_INET;         /* host byte order */
  src->myaddr.sin_port = htons (src->port);     /* short, network byte order */
  src->myaddr.sin_addr.s_addr = INADDR_ANY;

  if ((src->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket");
    return FALSE;
  }

  if (bind (src->sock, (struct sockaddr *) &src->myaddr, sizeof (src->myaddr)) == -1) {
    perror("bind");
    return FALSE;
  }

  if ((src->control_sock = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("control_socket");
    return FALSE;
  }

  if (bind (src->control_sock, (struct sockaddr *) &src->myaddr, sizeof (src->myaddr)) == -1) {
    perror("control_bind");
    return FALSE;
  }
  if (listen (src->control_sock, 5) == -1) {
    perror("listen");
    return FALSE;
  }
  fcntl (src->control_sock, F_SETFL, O_NONBLOCK);

  GST_FLAG_SET (src, GST_UDPSRC_OPEN);

  return TRUE;
}

static void
gst_udpsrc_close (GstUDPSrc *src)
{
  close (src->sock);

  GST_FLAG_UNSET (src, GST_UDPSRC_OPEN);
}

static GstElementStateReturn
gst_udpsrc_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_UDPSRC (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_UDPSRC_OPEN))
      gst_udpsrc_close (GST_UDPSRC (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_UDPSRC_OPEN)) {
      if (!gst_udpsrc_init_receive (GST_UDPSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

