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

#include "gsttcpsrc.h"
#include <unistd.h>

#define TCP_DEFAULT_PORT		4953

/* elementfactory information */
static GstElementDetails gst_tcpsrc_details =
GST_ELEMENT_DETAILS ("TCP packet receiver",
    "Source/Network",
    "Receive data over the network via TCP",
    "Zeeshan Ali <zak147@yahoo.com>");

/* TCPSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PORT,
  ARG_CONTROL,
/*  ARG_SOCKET_OPTIONS,*/
  /* FILL ME */
};

#define GST_TYPE_TCPSRC_CONTROL	(gst_tcpsrc_control_get_type())
static GType
gst_tcpsrc_control_get_type (void)
{
  static GType tcpsrc_control_type = 0;
  static GEnumValue tcpsrc_control[] = {
    {CONTROL_NONE, "1", "none"},
    {CONTROL_TCP, "2", "tcp"},
    {CONTROL_ZERO, NULL, NULL}
  };
  if (!tcpsrc_control_type) {
    tcpsrc_control_type =
	g_enum_register_static ("GstTCPSrcControl", tcpsrc_control);
  }
  return tcpsrc_control_type;
}

static void gst_tcpsrc_base_init (gpointer g_class);
static void gst_tcpsrc_class_init (GstTCPSrc * klass);
static void gst_tcpsrc_init (GstTCPSrc * tcpsrc);

static GstData *gst_tcpsrc_get (GstPad * pad);
static GstElementStateReturn gst_tcpsrc_change_state (GstElement * element);

static void gst_tcpsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcpsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_tcpsrc_set_clock (GstElement * element, GstClock * clock);

static GstElementClass *parent_class = NULL;

/*static guint gst_tcpsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_tcpsrc_get_type (void)
{
  static GType tcpsrc_type = 0;


  if (!tcpsrc_type) {
    static const GTypeInfo tcpsrc_info = {
      sizeof (GstTCPSrcClass),
      gst_tcpsrc_base_init,
      NULL,
      (GClassInitFunc) gst_tcpsrc_class_init,
      NULL,
      NULL,
      sizeof (GstTCPSrc),
      0,
      (GInstanceInitFunc) gst_tcpsrc_init,
      NULL
    };
    tcpsrc_type =
	g_type_register_static (GST_TYPE_ELEMENT, "GstTCPSrc", &tcpsrc_info, 0);
  }
  return tcpsrc_type;
}

static void
gst_tcpsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_tcpsrc_details);
}

static void
gst_tcpsrc_class_init (GstTCPSrc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "port", "The port to receive the packets from",
	  0, 32768, TCP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CONTROL,
      g_param_spec_enum ("control", "control", "The type of control",
	  GST_TYPE_TCPSRC_CONTROL, CONTROL_TCP, G_PARAM_READWRITE));
/*
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SOCKET_OPTIONS,
    g_param_spec_boolean ("socketop", "socketop", "Enable or disable socket options REUSEADDR and KEEPALIVE",
                        FALSE, G_PARAM_READWRITE));
*/
  gobject_class->set_property = gst_tcpsrc_set_property;
  gobject_class->get_property = gst_tcpsrc_get_property;

  gstelement_class->change_state = gst_tcpsrc_change_state;
  gstelement_class->set_clock = gst_tcpsrc_set_clock;
}

static void
gst_tcpsrc_set_clock (GstElement * element, GstClock * clock)
{
  GstTCPSrc *tcpsrc;

  tcpsrc = GST_TCPSRC (element);

  tcpsrc->clock = clock;
}

static void
gst_tcpsrc_init (GstTCPSrc * tcpsrc)
{
  /* create the src and src pads */
  tcpsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (tcpsrc), tcpsrc->srcpad);
  gst_pad_set_get_function (tcpsrc->srcpad, gst_tcpsrc_get);

  tcpsrc->port = TCP_DEFAULT_PORT;
  tcpsrc->control = CONTROL_TCP;
  tcpsrc->clock = NULL;
  tcpsrc->sock = -1;
  tcpsrc->control_sock = -1;
  tcpsrc->client_sock = -1;
  /*tcpsrc->socket_options = FALSE; */

  GST_FLAG_UNSET (tcpsrc, GST_TCPSRC_OPEN);
  GST_FLAG_SET (tcpsrc, GST_TCPSRC_1ST_BUF);
  GST_FLAG_UNSET (tcpsrc, GST_TCPSRC_CONNECTED);
}

static GstData *
gst_tcpsrc_get (GstPad * pad)
{
  GstTCPSrc *tcpsrc;
  GstBuffer *outbuf;
  socklen_t len;
  gint numbytes;
  fd_set read_fds;
  guint max_sock;
  int ret, client_sock;
  struct sockaddr client_addr;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  tcpsrc = GST_TCPSRC (GST_OBJECT_PARENT (pad));

  FD_ZERO (&read_fds);
  FD_SET (tcpsrc->sock, &read_fds);

  max_sock = tcpsrc->sock;

  if (tcpsrc->control_sock >= 0) {
    FD_SET (tcpsrc->control_sock, &read_fds);
    max_sock = MAX (tcpsrc->sock, tcpsrc->control_sock);
  }

  /* Add to FD_SET client socket, when connection has been established */
  if (tcpsrc->client_sock >= 0) {
    FD_SET (tcpsrc->client_sock, &read_fds);
    max_sock = MAX (tcpsrc->client_sock, max_sock);
  }


  if (select (max_sock + 1, &read_fds, NULL, NULL, NULL) > 0) {
    if ((tcpsrc->control_sock != -1)
	&& FD_ISSET (tcpsrc->control_sock, &read_fds)) {
      guchar *buf = NULL;
      xmlDocPtr doc;
      GstCaps *caps;

      switch (tcpsrc->control) {
	case CONTROL_TCP:

#ifndef GST_DISABLE_LOADSAVE
	  buf = g_malloc (1024 * 10);

	  len = sizeof (struct sockaddr);
	  client_sock = accept (tcpsrc->control_sock, &client_addr, &len);

	  if (client_sock <= 0) {
	    perror ("control_sock accept");
	  }

	  else if ((ret = read (client_sock, buf, 1024 * 10)) <= 0) {
	    perror ("control_sock read");
	  }

	  else {
	    buf[ret] = '\0';
	    doc = xmlParseMemory (buf, ret);
	    caps = gst_caps_load_thyself (doc->xmlRootNode);

	    /* foward the connect, we don't signal back the result here... */
	    gst_pad_try_set_caps (tcpsrc->srcpad, caps);
	  }

	  g_free (buf);
#endif
	  break;
	case CONTROL_NONE:
	default:
	  g_free (buf);
	  return NULL;
	  break;
      }

      outbuf = NULL;
    } else {
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = g_malloc (24000);
      GST_BUFFER_SIZE (outbuf) = 24000;

      if (GST_FLAG_IS_SET (tcpsrc, GST_TCPSRC_1ST_BUF)) {
	if (tcpsrc->clock) {
	  GstClockTime current_time;
	  GstEvent *discont;

	  current_time = gst_clock_get_time (tcpsrc->clock);

	  GST_BUFFER_TIMESTAMP (outbuf) = current_time;

	  discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
	      current_time, NULL);

	  gst_pad_push (tcpsrc->srcpad, GST_DATA (discont));
	}

	GST_FLAG_UNSET (tcpsrc, GST_TCPSRC_1ST_BUF);
      }

      else {
	GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
      }

      if (!GST_FLAG_IS_SET (tcpsrc, GST_TCPSRC_CONNECTED)) {
	tcpsrc->client_sock = accept (tcpsrc->sock, &client_addr, &len);

	if (tcpsrc->client_sock <= 0) {
	  perror ("accept");
	}

	else {
	  GST_FLAG_SET (tcpsrc, GST_TCPSRC_CONNECTED);
	}
      }

      numbytes =
	  read (tcpsrc->client_sock, GST_BUFFER_DATA (outbuf),
	  GST_BUFFER_SIZE (outbuf));

      if (numbytes > 0) {
	GST_BUFFER_SIZE (outbuf) = numbytes;
      }

      else {
	if (numbytes == -1) {
	  perror ("read");
	} else
	  g_print ("End of Stream reached\n");
	gst_buffer_unref (outbuf);
	outbuf = NULL;
	close (tcpsrc->client_sock);
	tcpsrc->client_sock = -1;
	GST_FLAG_UNSET (tcpsrc, GST_TCPSRC_CONNECTED);
      }
    }
  }

  else {
    perror ("select");
    outbuf = NULL;
  }

  return GST_DATA (outbuf);
}


static void
gst_tcpsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstTCPSrc *tcpsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TCPSRC (object));
  tcpsrc = GST_TCPSRC (object);

  switch (prop_id) {
    case ARG_PORT:
      tcpsrc->port = g_value_get_int (value);
      break;
    case ARG_CONTROL:
      tcpsrc->control = g_value_get_enum (value);
      break;
/*    case ARG_SOCKET_OPTIONS:
	tcpsrc->socket_options = g_value_get_boolean(value);	
      break;	*/
    default:
      break;
  }
}

static void
gst_tcpsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTCPSrc *tcpsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TCPSRC (object));
  tcpsrc = GST_TCPSRC (object);

  switch (prop_id) {
    case ARG_PORT:
      g_value_set_int (value, tcpsrc->port);
      break;
    case ARG_CONTROL:
      g_value_set_enum (value, tcpsrc->control);
      break;
/*    case ARG_SOCKET_OPTIONS:
      g_value_set_boolean(value,tcpsrc->socket_options);
      break;*/
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_tcpsrc_init_receive (GstTCPSrc * src)
{
  guint val = 0;

  memset (&src->myaddr, 0, sizeof (src->myaddr));
  src->myaddr.sin_family = AF_INET;	/* host byte order */
  src->myaddr.sin_port = htons (src->port);	/* short, network byte order */
  src->myaddr.sin_addr.s_addr = INADDR_ANY;

  if ((src->sock = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
    perror ("stream_socket");
    return FALSE;
  }

/*  if (src->socket_options)
  {*/
  g_print ("Socket Options SO_REUSEADDR, SO_KEEPALIVE\n");
  /* Sock Options */
  val = 1;
  /* allow local address reuse */
  if (setsockopt (src->sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof (int)) < 0)
    perror ("setsockopt()");
  val = 1;
  /* periodically test if connection still alive */
  if (setsockopt (src->sock, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof (int)) < 0)
    perror ("setsockopt()");
  /* Sock Options */
/*  } */

  if (bind (src->sock, (struct sockaddr *) &src->myaddr,
	  sizeof (src->myaddr)) == -1) {
    perror ("stream_sock bind");
    return FALSE;
  }

  if (listen (src->sock, 5) == -1) {
    perror ("stream_sock listen");
    return FALSE;
  }

  fcntl (src->sock, F_SETFL, O_NONBLOCK);

  switch (src->control) {
    case CONTROL_TCP:
      if ((src->control_sock = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
	perror ("control_socket");
	return FALSE;
      }

      src->myaddr.sin_port = htons (src->port + 1);
      if (bind (src->control_sock, (struct sockaddr *) &src->myaddr,
	      sizeof (src->myaddr)) == -1) {
	perror ("control bind");
	return FALSE;
      }

      if (listen (src->control_sock, 5) == -1) {
	perror ("control listen");
	return FALSE;
      }

      fcntl (src->control_sock, F_SETFL, O_NONBLOCK);
    case CONTROL_NONE:
      GST_FLAG_SET (src, GST_TCPSRC_OPEN);
      return TRUE;
      break;
    default:
      return FALSE;
      break;
  }

  GST_FLAG_SET (src, GST_TCPSRC_OPEN);

  return TRUE;
}

static void
gst_tcpsrc_close (GstTCPSrc * src)
{
  if (src->sock != -1) {
    close (src->sock);
    src->sock = -1;
  }
  if (src->control_sock != -1) {
    close (src->control_sock);
    src->control_sock = -1;
  }
  if (src->client_sock != -1) {
    close (src->client_sock);
    src->client_sock = -1;
  }

  GST_FLAG_UNSET (src, GST_TCPSRC_OPEN);
}

static GstElementStateReturn
gst_tcpsrc_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_TCPSRC (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_TCPSRC_OPEN))
      gst_tcpsrc_close (GST_TCPSRC (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_TCPSRC_OPEN)) {
      if (!gst_tcpsrc_init_receive (GST_TCPSRC (element)))
	return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
