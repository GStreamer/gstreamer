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

#include "gstudpsrc.h"
#include <unistd.h>

#define UDP_DEFAULT_PORT		4951
#define UDP_DEFAULT_MULTICAST_GROUP	"0.0.0.0"

/* elementfactory information */
static GstElementDetails gst_udpsrc_details = GST_ELEMENT_DETAILS (
  "UDP packet receiver",
  "Source/Network",
  "Receive data over the network via UDP",
  "Wim Taymans <wim.taymans@chello.be>"
);

/* UDPSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_PORT,
  ARG_CONTROL,
  ARG_MULTICAST_GROUP
  /* FILL ME */
};

#define GST_TYPE_UDPSRC_CONTROL	(gst_udpsrc_control_get_type())
static GType
gst_udpsrc_control_get_type(void) {
  static GType udpsrc_control_type = 0;
  static GEnumValue udpsrc_control[] = {
    {CONTROL_NONE, "1", "none"},
    {CONTROL_UDP, "2", "udp"},
    {CONTROL_TCP, "3", "tcp"},
    {CONTROL_ZERO, NULL, NULL},
  };
  if (!udpsrc_control_type) {
    udpsrc_control_type = g_enum_register_static("GstUDPSrcControl", udpsrc_control);
  }
  return udpsrc_control_type;
}

static void		gst_udpsrc_base_init		(gpointer g_class);
static void		gst_udpsrc_class_init		(GstUDPSrc *klass);
static void		gst_udpsrc_init			(GstUDPSrc *udpsrc);

static GstData*	gst_udpsrc_get			(GstPad *pad);
static GstElementStateReturn
			gst_udpsrc_change_state 	(GstElement *element);

static void 		gst_udpsrc_set_property 	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 		gst_udpsrc_get_property 	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);
static void 		gst_udpsrc_set_clock 		(GstElement *element, GstClock *clock);

static GstElementClass *parent_class = NULL;
/*static guint gst_udpsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_udpsrc_get_type (void)
{
  static GType udpsrc_type = 0;

  if (!udpsrc_type) {
    static const GTypeInfo udpsrc_info = {
      sizeof(GstUDPSrcClass),
      gst_udpsrc_base_init,
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
gst_udpsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_udpsrc_details);
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
  g_object_class_install_property (gobject_class, ARG_CONTROL,
    g_param_spec_enum ("control", "control", "The type of control",
                       GST_TYPE_UDPSRC_CONTROL, CONTROL_UDP, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MULTICAST_GROUP,
    g_param_spec_string ("multicast_group", "multicast_group", 
		         "The Address of multicast group to join",
                         UDP_DEFAULT_MULTICAST_GROUP, G_PARAM_READWRITE));

  gobject_class->set_property = gst_udpsrc_set_property;
  gobject_class->get_property = gst_udpsrc_get_property;

  gstelement_class->change_state = gst_udpsrc_change_state;
  gstelement_class->set_clock = gst_udpsrc_set_clock;
}

static void
gst_udpsrc_set_clock (GstElement *element, GstClock *clock)
{
  GstUDPSrc *udpsrc;
	      
  udpsrc = GST_UDPSRC (element);

  udpsrc->clock = clock;
}

static void
gst_udpsrc_init (GstUDPSrc *udpsrc)
{
  /* create the src and src pads */
  udpsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (udpsrc), udpsrc->srcpad);
  gst_pad_set_get_function (udpsrc->srcpad, gst_udpsrc_get);

  udpsrc->port = UDP_DEFAULT_PORT;
  udpsrc->control = CONTROL_UDP;
  udpsrc->clock = NULL;
  udpsrc->sock = -1;
  udpsrc->control_sock = -1;
  udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);

  udpsrc->first_buf = TRUE;
}

static GstData*
gst_udpsrc_get (GstPad *pad)
{
  GstUDPSrc *udpsrc;
  GstBuffer *outbuf;
  struct sockaddr_in tmpaddr;
  socklen_t len;
  gint numbytes;
  fd_set read_fds;
  guint max_sock;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  udpsrc = GST_UDPSRC (GST_OBJECT_PARENT (pad));

  FD_ZERO (&read_fds);
  FD_SET (udpsrc->sock, &read_fds);
  if (udpsrc->control != CONTROL_NONE) {
     FD_SET (udpsrc->control_sock, &read_fds);
  }
  max_sock = MAX(udpsrc->sock, udpsrc->control_sock);

  if (select (max_sock+1, &read_fds, NULL, NULL, NULL) > 0) {
    if ((udpsrc->control_sock != -1) &&
        FD_ISSET (udpsrc->control_sock, &read_fds)) {
#ifndef GST_DISABLE_LOADSAVE
      guchar *buf;
      int ret;
      int fdread;
      struct sockaddr addr;
      xmlDocPtr doc;
      GstCaps *caps;

      buf = g_malloc (1024*10);

      switch (udpsrc->control) {
    	case CONTROL_TCP:
      	    len = sizeof (struct sockaddr);
      	    fdread = accept (udpsrc->control_sock, &addr, &len);
      	    if (fdread < 0) {
		perror ("accept");
      	    }
      
      	    ret = read (fdread, buf, 1024*10);
	    break;
    	case CONTROL_UDP:
      	    len = sizeof (struct sockaddr);
      	    ret = recvfrom (udpsrc->control_sock, buf, 1024*10, 0, (struct sockaddr *)&tmpaddr, &len);
      	    if (ret < 0) {
		perror ("recvfrom");
      	    }
	    break;
    	case CONTROL_NONE:
	default:
	    g_free (buf);
      	    return NULL;
	    break;
      }

      buf[ret] = '\0';
      doc = xmlParseMemory(buf, ret);
      caps = gst_caps_load_thyself(doc->xmlRootNode);
      
      /* foward the connect, we don't signal back the result here... */
      gst_pad_try_set_caps (udpsrc->srcpad, caps);

#endif
      g_free (buf);
      outbuf = NULL;
    }
    else {
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = g_malloc (24000);
      GST_BUFFER_SIZE (outbuf) = 24000;

      if (udpsrc->first_buf) {
  	if (udpsrc->clock) {
	   GstClockTime current_time;
      	   GstEvent *discont;

	   current_time = gst_clock_get_time (udpsrc->clock);
      	   
	   GST_BUFFER_TIMESTAMP (outbuf) = current_time;

      	   discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
				current_time, NULL);

      	   gst_pad_push (udpsrc->srcpad, GST_DATA (discont));
	}

	udpsrc->first_buf = FALSE;
      }
      
      else {
      	GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
      }

      len = sizeof (struct sockaddr);
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
  return GST_DATA (outbuf);
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
    case ARG_MULTICAST_GROUP:
      g_free(udpsrc->multi_group);
      
      if (g_value_get_string (value) == NULL)
        udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
      else
        udpsrc->multi_group = g_strdup (g_value_get_string (value));
      
      break;
    case ARG_CONTROL:
        udpsrc->control = g_value_get_enum (value);
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
    case ARG_MULTICAST_GROUP:
      g_value_set_string (value, udpsrc->multi_group);
      break;
    case ARG_CONTROL:
      g_value_set_enum (value, udpsrc->control);
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
  guint bc_val;
  gint reuse=1;
  bzero (&src->myaddr, sizeof (src->myaddr));
  src->myaddr.sin_family = AF_INET;           /* host byte order */
  src->myaddr.sin_port = htons (src->port);   /* short, network byte order */
  src->myaddr.sin_addr.s_addr = INADDR_ANY;

  if ((src->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket");
    return FALSE;
  }
  
  if (setsockopt(src->sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
   perror("setsockopt");
   return FALSE;
  }

  if (bind (src->sock, (struct sockaddr *) &src->myaddr, sizeof (src->myaddr)) == -1)  {
    perror("bind");
    return FALSE;
  }

  if (inet_aton (src->multi_group, &(src->multi_addr.imr_multiaddr))) {
    if (src->multi_addr.imr_multiaddr.s_addr) {
      src->multi_addr.imr_interface.s_addr = INADDR_ANY;
      setsockopt (src->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &src->multi_addr, sizeof(src->multi_addr));
    }
  }

  bc_val = 1;
  setsockopt (src->sock, SOL_SOCKET, SO_BROADCAST, &bc_val, sizeof (bc_val));
  src->myaddr.sin_port = htons (src->port+1);
  
  switch (src->control) {
    case CONTROL_TCP:
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
  
	break;
    case CONTROL_UDP:
  	if ((src->control_sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
    	   perror("socket");
    	   return FALSE;
  	}

  	if (bind (src->control_sock, (struct sockaddr *) &src->myaddr, sizeof (src->myaddr)) == -1) 
	{
    	    perror("control_bind");
    	    return FALSE;
  	}
	/* We can only do broadcast in udp */
  	bc_val = 1;
  	setsockopt (src->control_sock, SOL_SOCKET, SO_BROADCAST, &bc_val, sizeof (bc_val));
	break;
    case CONTROL_NONE:
        GST_FLAG_SET (src, GST_UDPSRC_OPEN);
  	return TRUE;
	break;
    default:
    	return FALSE;
	break;
  }

  GST_FLAG_SET (src, GST_UDPSRC_OPEN);
  
  return TRUE;
}

static void
gst_udpsrc_close (GstUDPSrc *src)
{
  if (src->sock != -1) {
    close (src->sock);
    src->sock = -1;
  }
  if (src->control_sock != -1) {
    close (src->control_sock);
    src->control_sock = -1;
  }

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

