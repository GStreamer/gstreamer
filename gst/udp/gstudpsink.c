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
#include "gstudpsink.h"

#define UDP_DEFAULT_HOST	"localhost"
#define UDP_DEFAULT_PORT	4951
#define UDP_DEFAULT_CONTROL	1

/* elementfactory information */
static GstElementDetails gst_udpsink_details = GST_ELEMENT_DETAILS (
  "UDP packet sender",
  "Sink/Network",
  "Send data over the network via UDP",
  "Wim Taymans <wim.taymans@chello.be>"
);

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
  ARG_CONTROL,
  ARG_MTU
  /* FILL ME */
};

#define GST_TYPE_UDPSINK_CONTROL	(gst_udpsink_control_get_type())
static GType
gst_udpsink_control_get_type(void) {
  static GType udpsink_control_type = 0;
  static GEnumValue udpsink_control[] = {
    {CONTROL_NONE, "1", "none"},
    {CONTROL_UDP, "2", "udp"},
    {CONTROL_TCP, "3", "tcp"},
    {CONTROL_ZERO, NULL, NULL},
  };
  if (!udpsink_control_type) {
    udpsink_control_type = g_enum_register_static("GstUDPSinkControl", udpsink_control);
  }
  return udpsink_control_type;
}

static void		gst_udpsink_base_init		(gpointer g_class);
static void		gst_udpsink_class_init		(GstUDPSink *klass);
static void		gst_udpsink_init		(GstUDPSink *udpsink);

static void 		gst_udpsink_set_clock 		(GstElement *element, GstClock *clock);

static void		gst_udpsink_chain		(GstPad *pad,GstData *_data);
static GstElementStateReturn gst_udpsink_change_state 	(GstElement *element);

static void 		gst_udpsink_set_property 	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 		gst_udpsink_get_property 	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);


static GstElementClass *parent_class = NULL;
/*static guint gst_udpsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_udpsink_get_type (void)
{
  static GType udpsink_type = 0;

  if (!udpsink_type) {
    static const GTypeInfo udpsink_info = {
      sizeof(GstUDPSinkClass),
      gst_udpsink_base_init,
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
gst_udpsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_udpsink_details);
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
    g_param_spec_string ("host", "host", 
			 "The host/IP/Multicast group to send the packets to",
                         UDP_DEFAULT_HOST, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
    g_param_spec_int ("port", "port", "The port to send the packets to",
                       0, 32768, UDP_DEFAULT_PORT, G_PARAM_READWRITE)); 
  g_object_class_install_property (gobject_class, ARG_CONTROL,
    g_param_spec_enum ("control", "control", "The type of control",
                       GST_TYPE_UDPSINK_CONTROL, CONTROL_UDP, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MTU, 
		  g_param_spec_int ("mtu", "mtu", "maximun transmit unit", G_MININT, G_MAXINT, 
			  0, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_udpsink_set_property;
  gobject_class->get_property = gst_udpsink_get_property;

  gstelement_class->change_state = gst_udpsink_change_state;
  gstelement_class->set_clock = gst_udpsink_set_clock;
}


static GstPadLinkReturn
gst_udpsink_sink_link (GstPad *pad, const GstCaps *caps)
{
  GstUDPSink *udpsink;
  struct sockaddr_in serv_addr;
  struct hostent *serverhost;
  int fd;
  FILE *f;
  guint bc_val;
#ifndef GST_DISABLE_LOADSAVE
  xmlDocPtr doc;
  xmlChar *buf;
  int buf_size;

  udpsink = GST_UDPSINK (gst_pad_get_parent (pad));
  
  memset(&serv_addr, 0, sizeof(serv_addr));
  
  /* its a name rather than an ipnum */
  serverhost = gethostbyname(udpsink->host);
  if (serverhost == (struct hostent *)0) {
	perror("gethostbyname");
    	return GST_PAD_LINK_REFUSED;
  }
  
  memmove(&serv_addr.sin_addr,serverhost->h_addr, serverhost->h_length);

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(udpsink->port+1);
  	    
  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "NewCaps", NULL);

  gst_caps_save_thyself (caps, doc->xmlRootNode);

  switch (udpsink->control) {
    case CONTROL_UDP:
  	    if ((fd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    		perror("socket");
    	    	return GST_PAD_LINK_REFUSED;
  	    }

	    /* We can only do broadcast in udp */
	    bc_val = 1;
	    setsockopt (fd,SOL_SOCKET, SO_BROADCAST, &bc_val, sizeof (bc_val));
  	    
	    xmlDocDumpMemory(doc, &buf, &buf_size);

	    if (sendto (fd, buf, buf_size, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
  	    {
		perror("sending");
    	    	return GST_PAD_LINK_REFUSED;
  	    } 
	    close (fd);
	    break;
    case CONTROL_TCP:
  	    if ((fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    		perror("socket");
    	    	return GST_PAD_LINK_REFUSED;
  	    }
  
  	    if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
    		g_printerr ("udpsink: connect to %s port %d failed: %s\n",
			     udpsink->host, udpsink->port, g_strerror(errno));
    		return GST_PAD_LINK_REFUSED;
  	    }
  
	    f = fdopen (dup (fd), "wb");

  	    xmlDocDump(f, doc);
  	    fclose (f);
	    close (fd);
	    break;
    case CONTROL_NONE:
    	    return GST_PAD_LINK_OK;
	    break;
    default:
    	    return GST_PAD_LINK_REFUSED;
	    break;
  }
#endif
  
  return GST_PAD_LINK_OK;
}

static void
gst_udpsink_set_clock (GstElement *element, GstClock *clock)
{
  GstUDPSink *udpsink;
	      
  udpsink = GST_UDPSINK (element);

  udpsink->clock = clock;
}

static void
gst_udpsink_init (GstUDPSink *udpsink)
{
  /* create the sink and src pads */
  udpsink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (udpsink), udpsink->sinkpad);
  gst_pad_set_chain_function (udpsink->sinkpad, gst_udpsink_chain);
  gst_pad_set_link_function (udpsink->sinkpad, gst_udpsink_sink_link);

  udpsink->host = g_strdup (UDP_DEFAULT_HOST);
  udpsink->port = UDP_DEFAULT_PORT;
  udpsink->control = CONTROL_UDP;
  udpsink->mtu = 1024;
  
  udpsink->clock = NULL;
}

static void
gst_udpsink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstUDPSink *udpsink;
  guint tolen, i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  udpsink = GST_UDPSINK (GST_OBJECT_PARENT (pad));
  
  if (udpsink->clock && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_element_wait (GST_ELEMENT (udpsink), GST_BUFFER_TIMESTAMP (buf));
  }
  
  tolen = sizeof(udpsink->theiraddr);
  
 /*
  if (sendto (udpsink->sock, GST_BUFFER_DATA (buf), 
	 GST_BUFFER_SIZE (buf), 0, (struct sockaddr *) &udpsink->theiraddr, 
	 tolen) == -1) {
    		perror("sending");
  } 
*/
  /* MTU */ 
  for (i = 0; i < GST_BUFFER_SIZE (buf); i += udpsink->mtu) {
    if (GST_BUFFER_SIZE (buf) - i > udpsink->mtu) {
  	if (sendto (udpsink->sock, GST_BUFFER_DATA (buf) + i, 
	    udpsink->mtu, 0, (struct sockaddr *) &udpsink->theiraddr, 
	    tolen) == -1) {
    		perror("sending");
  	} 
    }
    else {
  	if (sendto (udpsink->sock, GST_BUFFER_DATA (buf) + i, 
	    GST_BUFFER_SIZE (buf) -i, 0, 
	    (struct sockaddr *) &udpsink->theiraddr, tolen) == -1) {
    		perror("sending");
  	} 
    }
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
    case ARG_CONTROL:
        udpsink->control = g_value_get_enum (value);
      break;
    case ARG_MTU:
      udpsink->mtu = g_value_get_int (value);
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
    case ARG_CONTROL:
      g_value_set_enum (value, udpsink->control);
      break;
    case ARG_MTU:
      g_value_set_int (value, udpsink->mtu);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_udpsink_init_send (GstUDPSink *sink)
{
  struct hostent *he;
  struct in_addr addr;
  guint bc_val;

  bzero (&sink->theiraddr, sizeof (sink->theiraddr));
  sink->theiraddr.sin_family = AF_INET;         /* host byte order */
  sink->theiraddr.sin_port = htons (sink->port); /* short, network byte order */

  /* if its an IP address */
  if (inet_aton (sink->host, &addr)) {
    /* check if its a multicast address */
    if ((ntohl (addr.s_addr) & 0xe0000000) == 0xe0000000) {
	sink->multi_addr.imr_multiaddr.s_addr = addr.s_addr;
	sink->multi_addr.imr_interface.s_addr = INADDR_ANY;
       
	sink->theiraddr.sin_addr = sink->multi_addr.imr_multiaddr;	
  
	/* Joining the multicast group */
	setsockopt (sink->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &sink->multi_addr, sizeof(sink->multi_addr));
    }
 
    else {
       sink->theiraddr.sin_addr = 
		*((struct in_addr *) &addr);	
    }
  }
 
  /* we dont need to lookup for localhost */
  else if (strcmp (sink->host, UDP_DEFAULT_HOST) == 0 && 
	   inet_aton ("127.0.0.1", &addr)) {
       sink->theiraddr.sin_addr = 
		*((struct in_addr *) &addr);
  }

  /* if its a hostname */
  else if ((he = gethostbyname (sink->host))) {
    sink->theiraddr.sin_addr = *((struct in_addr *) he->h_addr);
  }
  
  else {
     perror("hostname lookup error?");
     return FALSE;
  }

  if ((sink->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
     perror("socket");
     return FALSE;
  }

  bc_val = 1;
  setsockopt (sink->sock, SOL_SOCKET, SO_BROADCAST, &bc_val, sizeof (bc_val));
  
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

