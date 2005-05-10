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

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* elementfactory information */
static GstElementDetails gst_udpsrc_details =
GST_ELEMENT_DETAILS ("UDP packet receiver",
    "Source/Network",
    "Receive data over the network via UDP",
    "Wim Taymans <wim.taymans@chello.be>");

/* UDPSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PORT,
  ARG_MULTICAST_GROUP
      /* FILL ME */
};

static void gst_udpsrc_base_init (gpointer g_class);
static void gst_udpsrc_class_init (GstUDPSrc * klass);
static void gst_udpsrc_init (GstUDPSrc * udpsrc);

static void gst_udpsrc_loop (GstPad * pad);
static GstElementStateReturn gst_udpsrc_change_state (GstElement * element);
static gboolean gst_udpsrc_activate (GstPad * pad, GstActivateMode mode);

static void gst_udpsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_udpsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_udpsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_udpsrc_get_type (void)
{
  static GType udpsrc_type = 0;

  if (!udpsrc_type) {
    static const GTypeInfo udpsrc_info = {
      sizeof (GstUDPSrcClass),
      gst_udpsrc_base_init,
      NULL,
      (GClassInitFunc) gst_udpsrc_class_init,
      NULL,
      NULL,
      sizeof (GstUDPSrc),
      0,
      (GInstanceInitFunc) gst_udpsrc_init,
      NULL
    };

    udpsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstUDPSrc", &udpsrc_info, 0);
  }
  return udpsrc_type;
}

static void
gst_udpsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_udpsrc_details);
}

static void
gst_udpsrc_class_init (GstUDPSrc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_udpsrc_set_property;
  gobject_class->get_property = gst_udpsrc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "port", "The port to receive the packets from",
          0, 32768, UDP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MULTICAST_GROUP,
      g_param_spec_string ("multicast_group", "multicast_group",
          "The Address of multicast group to join",
          UDP_DEFAULT_MULTICAST_GROUP, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_udpsrc_change_state;
}

static void
gst_udpsrc_init (GstUDPSrc * udpsrc)
{
  /* create the src and src pads */
  udpsrc->srcpad = gst_pad_new_from_template
      (gst_static_pad_template_get (&src_template), "src");
  gst_pad_set_activate_function (udpsrc->srcpad, gst_udpsrc_activate);
  gst_pad_set_loop_function (udpsrc->srcpad, gst_udpsrc_loop);
  gst_element_add_pad (GST_ELEMENT (udpsrc), udpsrc->srcpad);

  udpsrc->port = UDP_DEFAULT_PORT;
  udpsrc->sock = -1;
  udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
}

static void
gst_udpsrc_loop (GstPad * pad)
{
  GstUDPSrc *udpsrc;
  GstBuffer *outbuf;
  struct sockaddr_in tmpaddr;
  socklen_t len;
  gint numbytes;
  fd_set read_fds;
  guint max_sock;

  udpsrc = GST_UDPSRC (GST_OBJECT_PARENT (pad));

  FD_ZERO (&read_fds);
  FD_SET (udpsrc->sock, &read_fds);
  max_sock = udpsrc->sock;

  if (select (max_sock + 1, &read_fds, NULL, NULL, NULL) < 0)
    goto select_error;

  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) = g_malloc (24000);
  GST_BUFFER_SIZE (outbuf) = 24000;

  len = sizeof (struct sockaddr);
  if ((numbytes = recvfrom (udpsrc->sock, GST_BUFFER_DATA (outbuf),
              GST_BUFFER_SIZE (outbuf), 0, (struct sockaddr *) &tmpaddr,
              &len)) == -1)
    goto receive_error;

  GST_BUFFER_SIZE (outbuf) = numbytes;
  gst_pad_push (udpsrc->srcpad, outbuf);

  return;

select_error:
  {
    GST_DEBUG ("got select error");
    return;
  }
receive_error:
  {
    gst_buffer_unref (outbuf);
    GST_DEBUG ("got receive error");
    return;
  }
}

static void
gst_udpsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (object);

  switch (prop_id) {
    case ARG_PORT:
      udpsrc->port = g_value_get_int (value);
      break;
    case ARG_MULTICAST_GROUP:
      g_free (udpsrc->multi_group);

      if (g_value_get_string (value) == NULL)
        udpsrc->multi_group = g_strdup (UDP_DEFAULT_MULTICAST_GROUP);
      else
        udpsrc->multi_group = g_strdup (g_value_get_string (value));

      break;
    default:
      break;
  }
}

static void
gst_udpsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (object);

  switch (prop_id) {
    case ARG_PORT:
      g_value_set_int (value, udpsrc->port);
      break;
    case ARG_MULTICAST_GROUP:
      g_value_set_string (value, udpsrc->multi_group);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_udpsrc_init_receive (GstUDPSrc * src)
{
  guint bc_val;
  gint reuse = 1;

  memset (&src->myaddr, 0, sizeof (src->myaddr));
  src->myaddr.sin_family = AF_INET;     /* host byte order */
  src->myaddr.sin_port = htons (src->port);     /* short, network byte order */
  src->myaddr.sin_addr.s_addr = INADDR_ANY;

  if ((src->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror ("socket");
    return FALSE;
  }

  if (setsockopt (src->sock, SOL_SOCKET, SO_REUSEADDR, &reuse,
          sizeof (reuse)) == -1) {
    perror ("setsockopt");
    return FALSE;
  }

  if (bind (src->sock, (struct sockaddr *) &src->myaddr,
          sizeof (src->myaddr)) == -1) {
    perror ("bind");
    return FALSE;
  }

  if (inet_aton (src->multi_group, &(src->multi_addr.imr_multiaddr))) {
    if (src->multi_addr.imr_multiaddr.s_addr) {
      src->multi_addr.imr_interface.s_addr = INADDR_ANY;
      setsockopt (src->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &src->multi_addr,
          sizeof (src->multi_addr));
    }
  }

  bc_val = 1;
  setsockopt (src->sock, SOL_SOCKET, SO_BROADCAST, &bc_val, sizeof (bc_val));
  src->myaddr.sin_port = htons (src->port + 1);

  return TRUE;
}

static void
gst_udpsrc_close (GstUDPSrc * src)
{
  if (src->sock != -1) {
    close (src->sock);
    src->sock = -1;
  }
}

static gboolean
gst_udpsrc_activate (GstPad * pad, GstActivateMode mode)
{
  gboolean result;
  GstUDPSrc *udpsrc;

  udpsrc = GST_UDPSRC (GST_OBJECT_PARENT (pad));

  switch (mode) {
    case GST_ACTIVATE_PUSH:
      /* if we have a scheduler we can start the task */
      if (GST_ELEMENT_SCHEDULER (udpsrc)) {
        GST_STREAM_LOCK (pad);
        GST_RPAD_TASK (pad) =
            gst_scheduler_create_task (GST_ELEMENT_SCHEDULER (udpsrc),
            (GstTaskFunction) gst_udpsrc_loop, pad);

        gst_task_start (GST_RPAD_TASK (pad));
        GST_STREAM_UNLOCK (pad);
        result = TRUE;
      }
      break;
    case GST_ACTIVATE_PULL:
      result = FALSE;
      break;
    case GST_ACTIVATE_NONE:
      /* step 1, unblock clock sync (if any) */

      /* step 2, make sure streaming finishes */
      GST_STREAM_LOCK (pad);
      /* step 3, stop the task */
      if (GST_RPAD_TASK (pad)) {
        gst_task_stop (GST_RPAD_TASK (pad));
        gst_object_unref (GST_OBJECT (GST_RPAD_TASK (pad)));
        GST_RPAD_TASK (pad) = NULL;
      }
      GST_STREAM_UNLOCK (pad);

      result = TRUE;
      break;
  }
  return result;
}

static GstElementStateReturn
gst_udpsrc_change_state (GstElement * element)
{
  GstElementStateReturn ret;
  GstUDPSrc *src;
  gint transition;

  src = GST_UDPSRC (element);

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_READY_TO_PAUSED:
      if (!gst_udpsrc_init_receive (src))
        goto no_init;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      gst_udpsrc_close (src);
      break;
    default:
      break;
  }

  return ret;

no_init:
  {
    return GST_STATE_FAILURE;
  }
}
