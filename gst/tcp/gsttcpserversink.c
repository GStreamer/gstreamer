/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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
#include <gst/gst-i18n-plugin.h>

#include <sys/ioctl.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "gsttcpserversink.h"
#include "gsttcp-marshal.h"

#define TCP_DEFAULT_HOST	"127.0.0.1"
#define TCP_DEFAULT_PORT	4953
#define TCP_BACKLOG		5

#define CONTROL_RESTART		'R'     /* restart the select call */
#define CONTROL_STOP		'S'     /* stop the select call */
#define SEND_COMMAND(sink, command) 		\
G_STMT_START {					\
  unsigned char c; c = command;			\
  write (sink->control_sock[1], &c, 1);		\
} G_STMT_END

#define READ_COMMAND(sink, command) 		\
G_STMT_START {					\
  read(sink->control_sock[0], &command, 1);	\
} G_STMT_END

/* elementfactory information */
static GstElementDetails gst_tcpserversink_details =
GST_ELEMENT_DETAILS ("TCP Server sink",
    "Sink/Network",
    "Send data as a server over the network via TCP",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

GST_DEBUG_CATEGORY (tcpserversink_debug);
#define GST_CAT_DEFAULT (tcpserversink_debug)

typedef struct
{
  int fd;
  int bufpos;                   /* position of this client in the global queue */

  GList *sending;               /* the buffers we need to send */
  int bufoffset;                /* offset in the first buffer */

  gboolean caps_sent;
  gboolean streamheader_sent;
}
GstTCPClient;

/* TCPServerSink signals and args */
enum
{
  SIGNAL_CLIENT_ADDED,
  SIGNAL_CLIENT_REMOVED,
  LAST_SIGNAL
};

#define DEFAULT_BUFFERS_MAX		25
#define DEFAULT_BUFFERS_SOFT_MAX	20

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  ARG_PROTOCOL,
  ARG_BUFFERS_MAX,
  ARG_BUFFERS_SOFT_MAX,
};

static void gst_tcpserversink_base_init (gpointer g_class);
static void gst_tcpserversink_class_init (GstTCPServerSink * klass);
static void gst_tcpserversink_init (GstTCPServerSink * tcpserversink);

static void gst_tcpserversink_set_clock (GstElement * element,
    GstClock * clock);

static void gst_tcpserversink_chain (GstPad * pad, GstData * _data);
static GstElementStateReturn gst_tcpserversink_change_state (GstElement *
    element);

static void gst_tcpserversink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcpserversink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstElementClass *parent_class = NULL;

static guint gst_tcpserversink_signals[LAST_SIGNAL] = { 0 };

GType
gst_tcpserversink_get_type (void)
{
  static GType tcpserversink_type = 0;


  if (!tcpserversink_type) {
    static const GTypeInfo tcpserversink_info = {
      sizeof (GstTCPServerSinkClass),
      gst_tcpserversink_base_init,
      NULL,
      (GClassInitFunc) gst_tcpserversink_class_init,
      NULL,
      NULL,
      sizeof (GstTCPServerSink),
      0,
      (GInstanceInitFunc) gst_tcpserversink_init,
      NULL
    };

    tcpserversink_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstTCPServerSink",
        &tcpserversink_info, 0);
  }
  return tcpserversink_type;
}

static void
gst_tcpserversink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_tcpserversink_details);
}

static void
gst_tcpserversink_class_init (GstTCPServerSink * klass)
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
  g_object_class_install_property (gobject_class, ARG_PROTOCOL,
      g_param_spec_enum ("protocol", "Protocol", "The protocol to wrap data in",
          GST_TYPE_TCP_PROTOCOL_TYPE, GST_TCP_PROTOCOL_TYPE_NONE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFERS_MAX,
      g_param_spec_int ("buffers-max", "Buffers max",
          "max number of buffers to queue (0 = no limit)", 0, G_MAXINT,
          DEFAULT_BUFFERS_MAX, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFERS_SOFT_MAX,
      g_param_spec_int ("buffers-soft-max", "Buffers soft max",
          "Recover client when going over this limit (0 = no limit)", 0,
          G_MAXINT, DEFAULT_BUFFERS_SOFT_MAX, G_PARAM_READWRITE));

  gst_tcpserversink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstTCPServerSinkClass, client_added),
      NULL, NULL, gst_tcp_marshal_VOID__STRING_UINT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_UINT);
  gst_tcpserversink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstTCPServerSinkClass,
          client_removed), NULL, NULL, gst_tcp_marshal_VOID__STRING_UINT,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

  gobject_class->set_property = gst_tcpserversink_set_property;
  gobject_class->get_property = gst_tcpserversink_get_property;

  gstelement_class->change_state = gst_tcpserversink_change_state;
  gstelement_class->set_clock = gst_tcpserversink_set_clock;

  GST_DEBUG_CATEGORY_INIT (tcpserversink_debug, "tcpserversink", 0, "TCP sink");
}

static void
gst_tcpserversink_set_clock (GstElement * element, GstClock * clock)
{
  GstTCPServerSink *tcpserversink;

  tcpserversink = GST_TCPSERVERSINK (element);

  tcpserversink->clock = clock;
}

static void
gst_tcpserversink_init (GstTCPServerSink * this)
{
  /* create the sink pad */
  this->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (this), this->sinkpad);
  gst_pad_set_chain_function (this->sinkpad, gst_tcpserversink_chain);

  this->server_port = TCP_DEFAULT_PORT;
  /* should support as minimum 576 for IPV4 and 1500 for IPV6 */
  /* this->mtu = 1500; */

  this->server_sock_fd = -1;
  GST_FLAG_UNSET (this, GST_TCPSERVERSINK_OPEN);

  this->protocol = GST_TCP_PROTOCOL_TYPE_NONE;
  this->clock = NULL;

  this->clients = NULL;

  this->bufqueue = g_array_new (FALSE, TRUE, sizeof (GstBuffer *));
  this->queuelock = g_mutex_new ();
  this->queuecond = g_cond_new ();
  this->buffers_max = DEFAULT_BUFFERS_MAX;
  this->buffers_soft_max = DEFAULT_BUFFERS_SOFT_MAX;

  this->clientslock = g_mutex_new ();

}

static void
gst_tcpserversink_debug_fdset (GstTCPServerSink * sink, fd_set * testfds)
{
  int fd;

  for (fd = 0; fd < FD_SETSIZE; fd++) {
    if (FD_ISSET (fd, testfds)) {
      GST_LOG_OBJECT (sink, "fd %d", fd);
    }
  }
}

/* handle a read request on the server,
 * which indicates a new client connection */
static gboolean
gst_tcpserversink_handle_server_read (GstTCPServerSink * sink)
{
  /* new client */
  int client_sock_fd;
  struct sockaddr_in client_address;
  int client_address_len;
  GstTCPClient *client;

  client_sock_fd =
      accept (sink->server_sock_fd, (struct sockaddr *) &client_address,
      &client_address_len);
  if (client_sock_fd == -1) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Could not accept client on server socket: %s", g_strerror (errno)));
    return FALSE;
  }

  /* create client datastructure */
  client = g_new0 (GstTCPClient, 1);
  client->fd = client_sock_fd;
  client->bufpos = -1;
  client->bufoffset = 0;
  client->sending = NULL;

  g_mutex_lock (sink->clientslock);
  sink->clients = g_list_prepend (sink->clients, client);
  g_mutex_unlock (sink->clientslock);

  /* we always read from a client */
  FD_SET (client_sock_fd, &sink->readfds);

  /* set the socket to non blocking */
  fcntl (client_sock_fd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (sink, "added new client ip %s with fd %d",
      inet_ntoa (client_address.sin_addr), client_sock_fd);
  g_signal_emit (G_OBJECT (sink),
      gst_tcpserversink_signals[SIGNAL_CLIENT_ADDED], 0,
      inet_ntoa (client_address.sin_addr), client_sock_fd);

  return TRUE;
}

static void
gst_tcpserversink_client_remove (GstTCPServerSink * sink, GstTCPClient * client)
{
  int fd = client->fd;

  /* FIXME: if we keep track of ip we can log it here and signal */
  GST_DEBUG_OBJECT (sink, "removing client on fd %d", fd);
  if (close (fd) != 0) {
    GST_DEBUG_OBJECT (sink, "error closing fd %d: %s", fd, g_strerror (errno));
  }
  FD_CLR (fd, &sink->readfds);
  FD_CLR (fd, &sink->writefds);

  sink->clients = g_list_remove (sink->clients, client);

  g_free (client);

  g_signal_emit (G_OBJECT (sink),
      gst_tcpserversink_signals[SIGNAL_CLIENT_REMOVED], 0, NULL, fd);
}

/* handle a read on a client fd,
 * which either indicates a close or should be ignored
 * returns FALSE if the client has been closed. */
static gboolean
gst_tcpserversink_handle_client_read (GstTCPServerSink * sink,
    GstTCPClient * client)
{
  int nread, fd;

  fd = client->fd;

  GST_LOG_OBJECT (sink, "select reports client read on fd %d", fd);

  ioctl (fd, FIONREAD, &nread);
  if (nread == 0) {
    /* client sent close, so remove it */
    GST_DEBUG_OBJECT (sink, "client asked for close, removing on fd %d", fd);
    return FALSE;
  } else {
    /* FIXME: we should probably just Read 'n' Drop */
    g_warning ("Don't know what to do with %d bytes to read", nread);
  }
  return TRUE;
}

static gboolean
gst_tcpserversink_client_queue_data (GstTCPServerSink * sink,
    GstTCPClient * client, gchar * data, gint len)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = data;
  GST_BUFFER_SIZE (buf) = len;

  GST_DEBUG_OBJECT (sink, "Queueing data of length %d for fd %d",
      len, client->fd);
  client->sending = g_list_append (client->sending, buf);

  return TRUE;
}

static gboolean
gst_tcpserversink_client_queue_caps (GstTCPServerSink * sink,
    GstTCPClient * client, const GstCaps * caps)
{
  guint8 *header;
  guint8 *payload;
  guint length;
  gchar *string;

  string = gst_caps_to_string (caps);
  GST_DEBUG_OBJECT (sink, "Queueing caps %s for fd %d through GDP", string,
      client->fd);
  g_free (string);

  if (!gst_dp_packet_from_caps (caps, 0, &length, &header, &payload)) {
    GST_DEBUG_OBJECT (sink, "Could not create GDP packet from caps");
    return FALSE;
  }
  gst_tcpserversink_client_queue_data (sink, client, header, length);

  length = gst_dp_header_payload_length (header);
  gst_tcpserversink_client_queue_data (sink, client, payload, length);

  return TRUE;
}

static gboolean
gst_tcpserversink_client_queue_buffer (GstTCPServerSink * sink,
    GstTCPClient * client, GstBuffer * buffer)
{
  if (sink->protocol == GST_TCP_PROTOCOL_TYPE_GDP) {
    guint8 *header;
    guint len;

    if (!gst_dp_header_from_buffer (buffer, 0, &len, &header)) {
      GST_DEBUG_OBJECT (sink,
          "could not create header, removing client on fd %d", client->fd);
      return FALSE;
    }
    gst_tcpserversink_client_queue_data (sink, client, header, len);
  }

  gst_buffer_ref (buffer);
  client->sending = g_list_append (client->sending, buffer);

  return TRUE;
}


/* handle a write on a client,
 * which indicates a read request from a client.
 *
 * The strategy is as follows, for each client we maintain a queue of GstBuffers
 * that contain the raw bytes we need to send to the client. In the case of the
 * GDP protocol, we create buffers out of the header bytes so that we can only focus
 * on sending buffers.
 *
 * We first check to see if we need to send caps (in GDP) and streamheaders. If so,
 * we queue them. 
 *
 * Then we run into the main loop that tries to send as many buffers as possible. It
 * will first exhaust the client->sending queue and if the queue is empty, it will
 * pick a buffer from the global queue.
 * 
 * Sending the Buffers from the client->sending queue is basically writing the bytes
 * to the socket and maintaining a count of the bytes that were sent. When the buffer
 * is completely sent, it is removed from the client->sending queue and we try to pick
 * a new buffer for sending.
 *
 * When the sending returns a partial buffer we stop sending more data as the next send
 * operation could block.
 */
static gboolean
gst_tcpserversink_handle_client_write (GstTCPServerSink * sink,
    GstTCPClient * client)
{
  int fd = client->fd;
  gboolean more;
  gboolean res;

  /* when using GDP, first check if we have queued caps yet */
  if (sink->protocol == GST_TCP_PROTOCOL_TYPE_GDP) {
    if (!client->caps_sent) {
      const GstCaps *caps = GST_PAD_CAPS (GST_PAD_PEER (sink->sinkpad));

      /* queue caps for sending */
      res = gst_tcpserversink_client_queue_caps (sink, client, caps);
      if (!res) {
        GST_DEBUG_OBJECT (sink, "Failed queueing caps, removing client");
        return FALSE;
      }
      client->caps_sent = TRUE;
    }
  }
  /* if we have streamheader buffers, and haven't sent them to this client
   * yet, send them out one by one */
  if (!client->streamheader_sent) {
    if (sink->streamheader) {
      GList *l;

      for (l = sink->streamheader; l; l = l->next) {
        /* queue stream headers for sending */
        res =
            gst_tcpserversink_client_queue_buffer (sink, client,
            GST_BUFFER (l->data));
        if (!res) {
          GST_DEBUG_OBJECT (sink,
              "Failed queueing streamheader, removing client");
          return FALSE;
        }
      }
    }
    client->streamheader_sent = TRUE;
  }

  more = TRUE;
  do {
    gint maxsize;

    if (!client->sending) {
      /* client is not working on a buffer */
      if (client->bufpos == -1) {
        /* client is too fast, remove from write queue until new buffer is
         * available */
        FD_CLR (fd, &sink->writefds);
        return TRUE;
      } else {
        /* client can pick a buffer from the global queue */
        GstBuffer *buf;

        /* grab buffer and ref, we need to ref since it could be unreffed in
         * another thread when we unlock the queuelock */
        g_mutex_lock (sink->queuelock);
        buf = g_array_index (sink->bufqueue, GstBuffer *, client->bufpos);
        client->bufpos--;
        gst_buffer_ref (buf);
        g_mutex_unlock (sink->queuelock);

        gst_tcpserversink_client_queue_buffer (sink, client, buf);
        /* it is safe to unref now as queueing a buffer will ref it */
        gst_buffer_unref (buf);
        /* need to start from the first byte for this new buffer */
        client->bufoffset = 0;
      }
    }

    /* see if we need to send something */
    if (client->sending) {
      ssize_t wrote;
      GstBuffer *head;

      /* pick first buffer from list */
      head = GST_BUFFER (client->sending->data);
      maxsize = GST_BUFFER_SIZE (head) - client->bufoffset;

      /* try to write the complete buffer */
      wrote =
          send (fd, GST_BUFFER_DATA (head) + client->bufoffset, maxsize,
          MSG_NOSIGNAL);
      if (wrote < 0) {
        /* hmm error.. */
        if (errno == EAGAIN) {
          /* nothing serious, resource was unavailable, try again later */
          more = FALSE;
        } else {
          GST_DEBUG_OBJECT (sink, "could not write, removing client on fd %d",
              fd);
          return FALSE;
        }
      } else if (wrote < maxsize) {
        /* partial write means that the client cannot read more and we should
         * stop sending more */
        GST_DEBUG_OBJECT (sink, "partial write on %d of %d bytes", fd, wrote);
        client->bufoffset += wrote;
        more = FALSE;
      } else {
        /* complete buffer was written, we can proceed to the next one */
        client->sending = g_list_remove (client->sending, head);
        gst_buffer_unref (head);
        /* make sure we start from byte 0 for the next buffer */
        client->bufoffset = 0;
      }
    }
  } while (more);

  return TRUE;
}

/* Queue a buffer on the global queue. 
 *
 * This functions adds the buffer to the front of a GArray. It removes the
 * tail buffer if the max queue size is exceeded. Unreffing the buffer that
 * is queued. Note that unreffing the buffer is not a problem as clients who
 * started writing out this buffer will still have a reference to it in the
 * client->sending queue.
 *
 * After adding the buffer, we update all client positions in the queue. If
 * a client moves of the soft max, we start the recovery procedure for this
 * slow client. If it goes over the hard max, it is put into the slow list
 * and removed.
 *
 * Special care is taken of clients that were waiting for a new buffer (they
 * had a position of -1) because they can proceed after adding this new buffer.
 * This is done by adding the client back into the write fd_set and signalling
 * the select thread that the fd_set changed.
 *
 */
static void
gst_tcpserversink_queue_buffer (GstTCPServerSink * sink, GstBuffer * buf)
{
  GList *clients;
  gint queuelen;
  GList *slow = NULL;
  gboolean need_signal = FALSE;

  g_mutex_lock (sink->queuelock);
  /* add buffer to queue */
  g_array_prepend_val (sink->bufqueue, buf);
  queuelen = sink->bufqueue->len;
  if (queuelen > sink->buffers_max) {
    GstBuffer *old;

    /* queue exceeded max size */
    queuelen--;
    old = g_array_index (sink->bufqueue, GstBuffer *, queuelen);
    sink->bufqueue = g_array_remove_index (sink->bufqueue, queuelen);

    /* unref tail buffer */
    gst_buffer_unref (old);
  }
  g_mutex_unlock (sink->queuelock);

  /* then loop over the clients and update the positions */
  g_mutex_lock (sink->clientslock);
  for (clients = sink->clients; clients; clients = g_list_next (clients)) {
    GstTCPClient *client;

    client = (GstTCPClient *) clients->data;

    client->bufpos++;
    GST_LOG_OBJECT (sink, "client %p with fd %d at position %d",
        client, client->fd, client->bufpos);
    if (client->bufpos >= sink->buffers_soft_max) {
      if (client->bufpos == sink->buffers_soft_max) {
        g_warning ("client %p with fd %d is lagging...", client, client->fd);
      }
      GST_LOG_OBJECT (sink, "client %p with fd %d is lagging",
          client, client->fd);
    }
    if (client->bufpos >= queuelen) {
      /* remove client */
      GST_LOG_OBJECT (sink, "client %p with fd %d is too slow, removing",
          client, client->fd);
      g_warning ("client %p with fd %d too slow, removing", client, client->fd);
      FD_CLR (client->fd, &sink->readfds);
      FD_CLR (client->fd, &sink->writefds);
      slow = g_list_prepend (slow, client);
      /* cannot send data to this client anymore. need to signal the select thread that
       * the fd_set changed */
      need_signal = TRUE;
    } else if (client->bufpos == 0) {
      /* can send data to this client now. need to signal the select thread that
       * the fd_set changed */
      FD_SET (client->fd, &sink->writefds);
      need_signal = TRUE;
    }
  }
  /* remove crap clients */
  for (clients = slow; clients; clients = g_list_next (clients)) {
    GstTCPClient *client;

    client = (GstTCPClient *) slow->data;

    gst_tcpserversink_client_remove (sink, client);
  }
  g_list_free (slow);
  g_mutex_unlock (sink->clientslock);

  /* and send a signal to thread if fd_set changed */
  if (need_signal) {
    SEND_COMMAND (sink, CONTROL_RESTART);
  }
}

/* Handle the clients. Basically does a blocking select for one
 * of the client fds to become read or writable. We also have a 
 * filedescriptor to receive commands on that we need to check.
 *
 * After going out of the select call, we read and write to all
 * clients that can do so. Badly behaving clients are put on a
 * garbage list and removed.
 */
static void
gst_tcpserversink_handle_clients (GstTCPServerSink * sink)
{
  int result;
  fd_set testreadfds, testwritefds;
  GList *clients, *error = NULL;
  gboolean try_again;

  do {
    try_again = FALSE;

    /* check for:
     * - server socket input (ie, new client connections)
     * - client socket input (ie, clients saying goodbye)
     * - client socket output (ie, client reads)          */
    testwritefds = sink->writefds;
    testreadfds = sink->readfds;
    FD_SET (sink->server_sock_fd, &testreadfds);
    FD_SET (sink->control_sock[0], &testreadfds);

    GST_LOG_OBJECT (sink, "doing select on server + client fds for reads");
    gst_tcpserversink_debug_fdset (sink, &testreadfds);
    GST_LOG_OBJECT (sink, "doing select on client fds for writes");
    gst_tcpserversink_debug_fdset (sink, &testwritefds);

    result =
        select (FD_SETSIZE, &testreadfds, &testwritefds, (fd_set *) 0, NULL);

    /* < 0 is an error, 0 just means a timeout happened */
    if (result < 0) {
      GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
          ("select failed: %s", g_strerror (errno)));
      return;
    }

    GST_LOG_OBJECT (sink, "%d sockets had action", result);
    GST_LOG_OBJECT (sink, "done select on server/client fds for reads");
    gst_tcpserversink_debug_fdset (sink, &testreadfds);
    GST_LOG_OBJECT (sink, "done select on client fds for writes");
    gst_tcpserversink_debug_fdset (sink, &testwritefds);

    if (FD_ISSET (sink->control_sock[0], &testreadfds)) {
      gchar command;

      READ_COMMAND (sink, command);

      switch (command) {
        case CONTROL_RESTART:
          /* need to restart the select call as the fd_set changed */
          try_again = TRUE;
          break;
        case CONTROL_STOP:
          /* stop this function */
          return;
        default:
          g_warning ("tcpserversink: unknown control message received");
          break;
      }
    }
  } while (try_again);

  if (FD_ISSET (sink->server_sock_fd, &testreadfds)) {
    /* handle new client connection on server socket */
    if (!gst_tcpserversink_handle_server_read (sink)) {
      GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
          ("client connection failed: %s", g_strerror (errno)));
      return;
    }
  }

  /* Check the reads */
  g_mutex_lock (sink->clientslock);
  for (clients = sink->clients; clients; clients = g_list_next (clients)) {
    GstTCPClient *client;
    int fd;

    client = (GstTCPClient *) clients->data;
    fd = client->fd;

    if (FD_ISSET (fd, &testreadfds)) {
      /* handle client read */
      if (!gst_tcpserversink_handle_client_read (sink, client)) {
        error = g_list_prepend (error, client);
        continue;
      }
    }
    if (FD_ISSET (fd, &testwritefds)) {
      /* handle client write */
      if (!gst_tcpserversink_handle_client_write (sink, client)) {
        error = g_list_prepend (error, client);
        continue;
      }
    }
  }
  /* remove crappy clients */
  for (clients = error; clients; clients = g_list_next (clients)) {
    GstTCPClient *client;

    client = (GstTCPClient *) error->data;

    GST_LOG_OBJECT (sink, "removing client %p with fd %d with errors", client,
        client->fd);
    gst_tcpserversink_client_remove (sink, client);
  }
  g_list_free (error);
  g_mutex_unlock (sink->clientslock);
}

static gpointer
gst_tcpserversink_thread (GstTCPServerSink * sink)
{
  while (sink->running) {
    gst_tcpserversink_handle_clients (sink);
  }
  return NULL;
}

static void
gst_tcpserversink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstTCPServerSink *sink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  sink = GST_TCPSERVERSINK (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_TCPSERVERSINK_OPEN));

  if (GST_IS_EVENT (buf)) {
    g_warning ("FIXME: handle events");
    return;
  }

  /* if the incoming buffer is marked as IN CAPS, then we assume for now
   * it's a streamheader that needs to be sent to each new client, so we
   * put it on our internal list of streamheader buffers.
   * After that we return, since we only send these out when we get
   * non IN_CAPS buffers so we properly keep track of clients that got
   * streamheaders. */
  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_IN_CAPS)) {
    GST_DEBUG_OBJECT (sink,
        "appending IN_CAPS buffer with length %d to streamheader",
        GST_BUFFER_SIZE (buf));
    sink->streamheader = g_list_append (sink->streamheader, buf);
    return;
  }

  /* queue the buffer */
  gst_tcpserversink_queue_buffer (sink, buf);

  sink->data_written += GST_BUFFER_SIZE (buf);
}

static void
gst_tcpserversink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPServerSink *tcpserversink;

  g_return_if_fail (GST_IS_TCPSERVERSINK (object));
  tcpserversink = GST_TCPSERVERSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_free (tcpserversink->host);
      tcpserversink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      tcpserversink->server_port = g_value_get_int (value);
      break;
    case ARG_PROTOCOL:
      tcpserversink->protocol = g_value_get_enum (value);
      break;
    case ARG_BUFFERS_MAX:
      tcpserversink->buffers_max = g_value_get_int (value);
      break;
    case ARG_BUFFERS_SOFT_MAX:
      tcpserversink->buffers_soft_max = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tcpserversink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTCPServerSink *tcpserversink;

  g_return_if_fail (GST_IS_TCPSERVERSINK (object));
  tcpserversink = GST_TCPSERVERSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, tcpserversink->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, tcpserversink->server_port);
      break;
    case ARG_PROTOCOL:
      g_value_set_enum (value, tcpserversink->protocol);
      break;
    case ARG_BUFFERS_MAX:
      g_value_set_int (value, tcpserversink->buffers_max);
      break;
    case ARG_BUFFERS_SOFT_MAX:
      g_value_set_int (value, tcpserversink->buffers_soft_max);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_tcpserversink_init_send (GstTCPServerSink * this)
{
  int ret;

  /* create sending server socket */
  if ((this->server_sock_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_WRITE, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
  GST_DEBUG_OBJECT (this, "opened sending server socket with fd %d",
      this->server_sock_fd);

  /* make address reusable */
  if (setsockopt (this->server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &ret,
          sizeof (int)) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
  /* keep connection alive; avoids SIGPIPE during write */
  if (setsockopt (this->server_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &ret,
          sizeof (int)) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }

  /* name the socket */
  memset (&this->server_sin, 0, sizeof (this->server_sin));
  this->server_sin.sin_family = AF_INET;        /* network socket */
  this->server_sin.sin_port = htons (this->server_port);        /* on port */
  this->server_sin.sin_addr.s_addr = htonl (INADDR_ANY);        /* for hosts */

  /* bind it */
  GST_DEBUG_OBJECT (this, "binding server socket to address");
  ret = bind (this->server_sock_fd, (struct sockaddr *) &this->server_sin,
      sizeof (this->server_sin));

  if (ret) {
    switch (errno) {
      default:
        GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
            ("bind failed: %s", g_strerror (errno)));
        return FALSE;
        break;
    }
  }

  /* set the server socket to nonblocking */
  fcntl (this->server_sock_fd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (this, "listening on server socket %d with queue of %d",
      this->server_sock_fd, TCP_BACKLOG);
  if (listen (this->server_sock_fd, TCP_BACKLOG) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("Could not listen on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
  GST_DEBUG_OBJECT (this,
      "listened on server socket %d, returning from connection setup",
      this->server_sock_fd);

  FD_ZERO (&this->readfds);
  FD_ZERO (&this->writefds);
  FD_SET (this->server_sock_fd, &this->readfds);

  if (socketpair (PF_UNIX, SOCK_STREAM, 0, this->control_sock) < 0) {
    perror ("creating socket pair");
  }

  this->running = TRUE;
  this->thread = g_thread_create ((GThreadFunc) gst_tcpserversink_thread,
      this, TRUE, NULL);

  GST_FLAG_SET (this, GST_TCPSERVERSINK_OPEN);
  this->streamheader = NULL;

  this->data_written = 0;

  return TRUE;
}

static void
gst_tcpserversink_close (GstTCPServerSink * this)
{
  this->running = FALSE;

  SEND_COMMAND (this, CONTROL_STOP);

  g_thread_join (this->thread);

  close (this->control_sock[0]);
  close (this->control_sock[1]);

  if (this->server_sock_fd != -1) {
    close (this->server_sock_fd);
    this->server_sock_fd = -1;
  }

  if (this->streamheader) {
    GList *l;

    for (l = this->streamheader; l; l = l->next) {
      gst_buffer_unref (l->data);
    }
    g_list_free (this->streamheader);
  }
  GST_FLAG_UNSET (this, GST_TCPSERVERSINK_OPEN);
}

static GstElementStateReturn
gst_tcpserversink_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_TCPSERVERSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_TCPSERVERSINK_OPEN))
      gst_tcpserversink_close (GST_TCPSERVERSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_TCPSERVERSINK_OPEN)) {
      if (!gst_tcpserversink_init_send (GST_TCPSERVERSINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
