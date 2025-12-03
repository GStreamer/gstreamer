/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32ipcserver.h"
#include "gstwin32ipcprotocol.h"
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <deque>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_server_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_server_debug

#define CONN_BUFFER_SIZE 1024

/* *INDENT-OFF* */
struct GstWin32IpcServerData
{
  explicit GstWin32IpcServerData (GstWin32IpcMmf * m, UINT64 pts,
      GstCaps * c, GByteArray * mtd, UINT64 s,
      void * u, GDestroyNotify n)
  {
    mmf = gst_win32_ipc_mmf_ref (m);
    size = gst_win32_ipc_mmf_get_size (m);
    handle = gst_win32_ipc_mmf_get_handle (m);
    timestamp = pts;
    caps = gst_caps_ref (c);
    if (mtd && mtd->len) {
      meta.resize (mtd->len);
      memcpy (meta.data (), mtd->data, mtd->len);
    }
    seq_num = s;
    user_data = u;
    notify = n;
  }

  ~GstWin32IpcServerData()
  {
    if (mmf)
      gst_win32_ipc_mmf_unref (mmf);

    gst_clear_caps (&caps);

    if (notify)
      notify (user_data);
  }

  GstWin32IpcMmf *mmf;
  HANDLE handle;
  GstCaps *caps;
  std::vector<UINT8> meta;
  UINT32 size;
  UINT64 seq_num;
  UINT64 timestamp;
  void *user_data;
  GDestroyNotify notify;
};

struct GstWin32IpcServerConn : public OVERLAPPED
{
  GstWin32IpcServerConn (HANDLE pipe_handle) : pipe (pipe_handle)
  {
    OVERLAPPED *parent = static_cast<OVERLAPPED *> (this);
    parent->Internal = 0;
    parent->InternalHigh = 0;
    parent->Offset = 0;
    parent->OffsetHigh = 0;

    client_msg.resize (CONN_BUFFER_SIZE);
    server_msg.resize (CONN_BUFFER_SIZE);
  }

  ~GstWin32IpcServerConn()
  {
    if (pipe != INVALID_HANDLE_VALUE) {
      CancelIo (pipe);
      DisconnectNamedPipe (pipe);
      CloseHandle (pipe);
    }
  }

  GstWin32IpcServer *server;

  HANDLE pipe;

  GstWin32IpcPktType type;
  std::vector<UINT8> client_msg;
  std::vector<UINT8> server_msg;
  std::shared_ptr<GstWin32IpcServerData> data;
  std::vector<std::shared_ptr<GstWin32IpcServerData>> peer_handles;
  GstCaps *caps = nullptr;
  std::string caps_string;

  guint64 seq_num = 0;
  guint id;
  bool eos = false;
  bool pending_have_data = false;
  bool configured = false;
};

struct GstWin32IpcServerPrivate
{
  GstWin32IpcServerPrivate ()
  {
    cancellable = CreateEvent (nullptr, TRUE, FALSE, nullptr);
    wakeup_event = CreateEvent (nullptr, FALSE, FALSE, nullptr);
  }

  ~GstWin32IpcServerPrivate ()
  {
    CloseHandle (cancellable);
    CloseHandle (wakeup_event);
  }

  std::mutex lock;
  std::condition_variable cond;
  guint64 seq_num = 0;
  guint next_conn_id = 0;
  std::unordered_map<guint, std::shared_ptr<GstWin32IpcServerConn>> conn_map;
  GThread *loop_thread = nullptr;
  std::atomic<bool> shutdown = { false };
  std::atomic<bool> aborted = { false };
  std::deque<std::shared_ptr<GstWin32IpcServerData>> data_queue;
  std::string address;
  HANDLE cancellable;
  HANDLE wakeup_event;
  DWORD pid;
  bool flushing = false;
  std::atomic<guint64> max_buffers = { 0 };
  std::atomic<GstWin32IpcLeakyType> leaky = { GST_WIN32_IPC_LEAKY_DOWNSTREAM };
};
/* *INDENT-ON* */

struct _GstWin32IpcServer
{
  GstObject parent;

  GstWin32IpcServerPrivate *priv;
};

static void gst_win32_ipc_server_dispose (GObject * object);
static void gst_win32_ipc_server_finalize (GObject * object);
static void gst_win32_ipc_server_on_idle (GstWin32IpcServer * self);
static void gst_win32_ipc_server_send_msg (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn);
static void gst_win32_ipc_server_wait_msg (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn);

#define gst_win32_ipc_server_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcServer, gst_win32_ipc_server, GST_TYPE_OBJECT);

static void
gst_win32_ipc_server_class_init (GstWin32IpcServerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_win32_ipc_server_finalize;
  object_class->dispose = gst_win32_ipc_server_dispose;

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_server_debug, "win32ipcserver",
      0, "win32ipcserver");
}

static void
gst_win32_ipc_server_init (GstWin32IpcServer * self)
{
  self->priv = new GstWin32IpcServerPrivate ();
  self->priv->pid = GetCurrentProcessId ();
}

static void
gst_win32_ipc_server_dispose (GObject * object)
{
  auto self = GST_WIN32_IPC_SERVER (object);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "dispose");

  SetEvent (priv->cancellable);

  g_clear_pointer (&priv->loop_thread, g_thread_join);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_win32_ipc_server_finalize (GObject * object)
{
  auto self = GST_WIN32_IPC_SERVER (object);

  GST_DEBUG_OBJECT (self, "finalize");

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static HANDLE
gst_win32_ipc_server_create_pipe (GstWin32IpcServer * self,
    OVERLAPPED * overlap, bool &io_pending)
{
  auto priv = self->priv;
  HANDLE pipe = CreateNamedPipeA (priv->address.c_str (),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      PIPE_UNLIMITED_INSTANCES, CONN_BUFFER_SIZE, CONN_BUFFER_SIZE, 5000,
      nullptr);

  if (pipe == INVALID_HANDLE_VALUE) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_ERROR_OBJECT (self, "CreateNamedPipeA failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    return INVALID_HANDLE_VALUE;
  }

  if (ConnectNamedPipe (pipe, overlap)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_ERROR_OBJECT (self, "ConnectNamedPipe failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    return INVALID_HANDLE_VALUE;
  }

  io_pending = false;
  guint last_err = GetLastError ();

  switch (last_err) {
    case ERROR_IO_PENDING:
      io_pending = true;
      break;
    case ERROR_PIPE_CONNECTED:
      SetEvent (overlap->hEvent);
      break;
    default:
    {
      auto err = g_win32_error_message (last_err);
      GST_ERROR_OBJECT (self, "ConnectNamedPipe failed with 0x%x (%s)",
          last_err, err);
      g_free (err);
      CloseHandle (pipe);
      return INVALID_HANDLE_VALUE;
    }
  }

  return pipe;
}

static void
gst_win32_ipc_server_close_connection (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Closing conn-id %u", conn->id);

  priv->conn_map.erase (conn->id);

  if (priv->conn_map.empty ()) {
    GST_DEBUG_OBJECT (self, "All connection were closed");
    if (priv->shutdown) {
      SetEvent (priv->cancellable);
    } else {
      /* Run idle func to flush buffer queue if needed */
      SetEvent (priv->wakeup_event);
    }
  }
}

static void
gst_win32_ipc_server_have_data (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn)
{
  if (!conn->data) {
    GST_ERROR_OBJECT (self, "Have no data to send, conn-id: %u", conn->id);
    gst_win32_ipc_server_close_connection (self, conn);
    return;
  }

  auto & data = conn->data;

  conn->pending_have_data = false;
  conn->seq_num = data->seq_num + 1;

  gchar *caps_str = nullptr;
  if (!conn->caps || !gst_caps_is_equal (conn->caps, data->caps)) {
    gst_caps_replace (&conn->caps, data->caps);
    caps_str = gst_caps_to_string (data->caps);
    conn->caps_string = caps_str;
  }

  GST_LOG_OBJECT (self, "Sending HAVE-DATA with handle \"%p\", conn-id :%u",
      conn->data->handle, conn->id);

  auto ret = gst_win32_ipc_pkt_build_have_data (conn->server_msg, data->size,
      data->timestamp, data->handle, caps_str, data->meta);
  g_free (caps_str);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Couldn't build HAVE-DATA pkt, conn-id: %u",
        conn->id);
    gst_win32_ipc_server_close_connection (self, conn);
    return;
  }

  conn->type = GstWin32IpcPktType::HAVE_DATA;
  gst_win32_ipc_server_send_msg (self, conn);
}

static bool
gst_win32_ipc_server_on_release_data (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn)
{
  bool found = false;
  HANDLE handle = nullptr;

  if (!gst_win32_ipc_pkt_parse_release_data (conn->client_msg, handle)) {
    GST_ERROR_OBJECT (self, "Couldn't parse RELEASE-DATA, conn-id: %u",
        conn->id);
    return false;
  }

  GST_LOG_OBJECT (self, "RELEASE-DATA \"%p\", conn-id: %u", handle, conn->id);

  for (auto it = conn->peer_handles.begin (); it != conn->peer_handles.end ();
      it++) {
    auto other = (*it)->handle;
    if (handle == other) {
      found = true;
      conn->peer_handles.erase (it);
      break;
    }
  }

  if (!found) {
    GST_WARNING_OBJECT (self,
        "Unexpected name to remove, conn-id: %u", conn->id);
    return false;
  }

  GST_LOG_OBJECT (self, "Client is holding %" G_GSIZE_FORMAT " handles",
      conn->peer_handles.size ());

  return true;
}

static void
gst_win32_ipc_server_wait_msg_finish (GstWin32IpcServer * server,
    GstWin32IpcServerConn * conn)
{
  GstWin32IpcPktHdr header;

  if (!gst_win32_ipc_pkt_identify (conn->client_msg, header)) {
    GST_ERROR_OBJECT (server, "Broken header, conn-id: %u", conn->id);
    gst_win32_ipc_server_close_connection (server, conn);
    return;
  }

  switch (header.type) {
    case GstWin32IpcPktType::NEED_DATA:
      GST_LOG_OBJECT (server, "NEED-DATA, conn-id: %u", conn->id);
      if (!conn->data) {
        GST_LOG_OBJECT (server, "Wait for available data, conn-id: %u",
            conn->id);
        conn->pending_have_data = true;
        gst_win32_ipc_server_on_idle (server);
        return;
      }
      gst_win32_ipc_server_have_data (server, conn);
      break;
    case GstWin32IpcPktType::READ_DONE:
      GST_LOG_OBJECT (server, "READ-DONE, conn-id: %u", conn->id);

      if (!conn->data) {
        GST_ERROR_OBJECT (server, "Unexpected READ-DATA, conn-id: %u",
            conn->id);
        gst_win32_ipc_server_close_connection (server, conn);
        return;
      }

      conn->peer_handles.push_back (conn->data);
      conn->data = nullptr;
      gst_win32_ipc_server_wait_msg (server, conn);
      break;
    case GstWin32IpcPktType::RELEASE_DATA:
      GST_LOG_OBJECT (server, "RELEASE-DATA, conn-id: %u", conn->id);
      if (!gst_win32_ipc_server_on_release_data (server, conn))
        gst_win32_ipc_server_close_connection (server, conn);
      else
        gst_win32_ipc_server_wait_msg (server, conn);
      break;
    case GstWin32IpcPktType::FIN:
      GST_DEBUG_OBJECT (server, "FIN, conn-id %u", conn->id);
      gst_win32_ipc_server_close_connection (server, conn);
      break;
    default:
      GST_ERROR_OBJECT (server, "Unexpected packet, conn-id: %u", conn->id);
      gst_win32_ipc_server_close_connection (server, conn);
      break;
  }
}

static void WINAPI
gst_win32_ipc_server_payload_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  GstWin32IpcServerConn *conn =
      static_cast < GstWin32IpcServerConn * >(overlap);
  auto self = conn->server;

  if (error_code != ERROR_SUCCESS) {
    auto err = g_win32_error_message (error_code);
    GST_WARNING_OBJECT (self, "ReadFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err);
    g_free (err);
    gst_win32_ipc_server_close_connection (self, conn);
    return;
  }

  gst_win32_ipc_server_wait_msg_finish (self, conn);
}

static void WINAPI
gst_win32_ipc_server_wait_msg_header_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  GstWin32IpcServerConn *conn =
      static_cast < GstWin32IpcServerConn * >(overlap);
  auto self = conn->server;
  GstWin32IpcPktHdr hdr;

  if (error_code != ERROR_SUCCESS) {
    auto err = g_win32_error_message (error_code);
    GST_WARNING_OBJECT (self, "ReadFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err);
    g_free (err);
    gst_win32_ipc_server_close_connection (self, conn);
    return;
  }

  if (!gst_win32_ipc_pkt_identify (conn->client_msg, hdr)) {
    GST_ERROR_OBJECT (self, "Broken header");
    gst_win32_ipc_server_close_connection (self, conn);
    return;
  }

  if (hdr.payload_size == 0) {
    gst_win32_ipc_server_wait_msg_finish (conn->server, conn);
    return;
  }

  GST_LOG_OBJECT (self, "Reading payload");

  if (!ReadFileEx (conn->pipe, conn->client_msg.data () +
          sizeof (GstWin32IpcPktHdr), hdr.payload_size, conn,
          gst_win32_ipc_server_payload_finish)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_WARNING_OBJECT (self, "ReadFileEx failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    gst_win32_ipc_server_close_connection (self, conn);
  }
}

static void
gst_win32_ipc_server_wait_msg (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn)
{
  if (!ReadFileEx (conn->pipe, conn->client_msg.data (),
          sizeof (GstWin32IpcPktHdr), conn,
          gst_win32_ipc_server_wait_msg_header_finish)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_WARNING_OBJECT (self, "ReadFileEx failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    gst_win32_ipc_server_close_connection (self, conn);
  }
}

static void
gst_win32_ipc_server_eos (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn)
{
  gst_win32_ipc_pkt_build_eos (conn->server_msg);
  conn->eos = true;
  conn->type = GstWin32IpcPktType::EOS;

  gst_win32_ipc_server_send_msg (self, conn);
}

static void
gst_win32_ipc_server_config_data (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn)
{
  auto priv = self->priv;

  if (conn->data) {
    auto & data = conn->data;
    if (!conn->caps || !gst_caps_is_equal (conn->caps, data->caps)) {
      gst_caps_replace (&conn->caps, data->caps);
      auto caps_str = gst_caps_to_string (data->caps);
      conn->caps_string = caps_str;
      g_free (caps_str);
    }
  }

  gst_win32_ipc_pkt_build_config (conn->server_msg,
      priv->pid, conn->caps_string);
  conn->type = GstWin32IpcPktType::CONFIG;

  GST_LOG_OBJECT (self, "Sending CONFIG, conn-id %u", conn->id);
  gst_win32_ipc_server_send_msg (self, conn);
}

/* *INDENT-OFF* */
static void
gst_win32_ipc_server_on_idle (GstWin32IpcServer * self)
{
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "idle");

  if (priv->shutdown) {
    GST_DEBUG_OBJECT (self, "We are stopping");

    if (priv->conn_map.empty ()) {
      GST_DEBUG_OBJECT (self, "All connections were closed");
      SetEvent (priv->cancellable);
      return;
    }

    std::vector < std::shared_ptr < GstWin32IpcServerConn >> to_send_eos;
    for (auto it : priv->conn_map) {
      auto conn = it.second;
      if (conn->eos || !conn->pending_have_data)
        continue;

      to_send_eos.push_back (conn);
    }

    for (auto it : to_send_eos) {
      GST_DEBUG_OBJECT (self, "Sending EOS to conn-id: %u", it->id);
      gst_win32_ipc_server_eos (self, it.get ());
    }

    GST_DEBUG_OBJECT (self, "Have %" G_GSIZE_FORMAT " alive connections",
        priv->conn_map.size());

    size_t num_closed = 0;
    for (auto it : priv->conn_map) {
      auto conn = it.second;
      GST_DEBUG_OBJECT (self, "conn-id %u"
          " peer handle size %" G_GSIZE_FORMAT, conn->id,
          conn->peer_handles.size ());

      /* Cannot erase conn since it's still referenced.
       * Manually close connection */
      if (conn->peer_handles.empty ()) {
        if (conn->pipe != INVALID_HANDLE_VALUE) {
          CancelIo (conn->pipe);
          DisconnectNamedPipe (conn->pipe);
          CloseHandle (conn->pipe);
          conn->pipe = INVALID_HANDLE_VALUE;
        }

        num_closed++;
      }
    }

    if (priv->conn_map.size () == num_closed) {
      GST_DEBUG_OBJECT (self, "All connections were closed");
      SetEvent (priv->cancellable);
    }

    return;
  }

  if (priv->conn_map.empty ()) {
    GST_LOG_OBJECT (self, "Have no connection");
    if (priv->max_buffers > 0) {
      std::lock_guard < std::mutex > lk (priv->lock);
      if (!priv->data_queue.empty ()) {
        GST_LOG_OBJECT (self, "Dropping %u queued buffers",
            (guint) priv->data_queue.size ());
        priv->data_queue.clear ();
      }

      priv->cond.notify_all ();
    }

    return;
  }

  std::vector < std::shared_ptr < GstWin32IpcServerConn >> to_config_data;
  std::vector < std::shared_ptr < GstWin32IpcServerConn >> to_send_have_data;
  guint64 base_seq = 0;

  {
    std::unique_lock < std::mutex > lk (priv->lock);
    if (priv->data_queue.empty ())
      return;

    base_seq = priv->data_queue.front ()->seq_num;

    for (auto it : priv->conn_map) {
      auto conn = it.second;
      if (!conn->configured) {
        conn->configured = true;
        conn->data = priv->data_queue.front ();
        to_config_data.push_back (conn);
      } else if (conn->pending_have_data) {
        auto next_seq = conn->seq_num;

        if (next_seq < base_seq) {
          GST_WARNING_OBJECT (self, "conn-id: %u next_seq < base_seq, resync",
              conn->id);
          next_seq = base_seq;
        }

        auto offset = (size_t) (next_seq - base_seq);
        if (offset < priv->data_queue.size ()) {
          conn->data = priv->data_queue[offset];
          to_send_have_data.push_back (conn);
        }
      }
    }
  }

  for (auto it: to_config_data)
    gst_win32_ipc_server_config_data (self, it.get ());

  for (auto it: to_send_have_data)
    gst_win32_ipc_server_have_data (self, it.get ());

  /* Drop fully consumed buffer from queue */
  {
    std::unique_lock<std::mutex> lk (priv->lock);

    if (!priv->data_queue.empty ()) {
      guint64 min_seq = G_MAXUINT64;

      for (auto it : priv->conn_map) {
        auto conn = it.second;
        if (conn->seq_num < min_seq)
          min_seq = conn->seq_num;
      }

      while (!priv->data_queue.empty () &&
          priv->data_queue.front ()->seq_num < min_seq) {
        priv->data_queue.pop_front ();
      }

      priv->cond.notify_all ();
    }
  }
}
/* *INDENT-ON* */

static void WINAPI
gst_win32_ipc_server_send_msg_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  GstWin32IpcServerConn *conn =
      static_cast < GstWin32IpcServerConn * >(overlap);
  auto self = conn->server;

  if (error_code != ERROR_SUCCESS) {
    auto err = g_win32_error_message (error_code);
    GST_WARNING_OBJECT (self, "WriteFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err);
    g_free (err);
    gst_win32_ipc_server_close_connection (self, conn);
    return;
  }

  GST_LOG_OBJECT (self, "Sent message");

  switch (conn->type) {
    case GstWin32IpcPktType::CONFIG:
      GST_DEBUG_OBJECT (self, "Sent CONFIG-DATA, conn-id %u", conn->id);
      gst_win32_ipc_server_wait_msg (self, conn);
      break;
    case GstWin32IpcPktType::HAVE_DATA:
      GST_LOG_OBJECT (self, "Sent HAVE-DATA, conn-id %u", conn->id);
      gst_win32_ipc_server_wait_msg (self, conn);
      break;
    case GstWin32IpcPktType::EOS:
      GST_DEBUG_OBJECT (self, "Sent EOS, conn-id %u", conn->id);
      gst_win32_ipc_server_wait_msg (self, conn);
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected msg type");
      gst_win32_ipc_server_close_connection (self, conn);
      break;
  }
}

static void
gst_win32_ipc_server_send_msg (GstWin32IpcServer * self,
    GstWin32IpcServerConn * conn)
{
  GST_LOG_OBJECT (self, "Sending message");

  if (!WriteFileEx (conn->pipe, conn->server_msg.data (),
          conn->server_msg.size (), conn,
          gst_win32_ipc_server_send_msg_finish)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_WARNING_OBJECT (self, "WriteFileEx failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    gst_win32_ipc_server_close_connection (self, conn);
  }
}

static void
gst_win32_ipc_server_on_incoming_connection (GstWin32IpcServer * self,
    std::shared_ptr < GstWin32IpcServerConn > conn)
{
  auto priv = self->priv;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    conn->server = self;
    conn->id = priv->next_conn_id;
    priv->next_conn_id++;

    conn->data = nullptr;
    if (!priv->data_queue.empty ())
      conn->data = priv->data_queue.front ();
  }

  GST_DEBUG_OBJECT (self, "New connection, conn-id: %u", conn->id);

  /* *INDENT-OFF* */
  priv->conn_map.insert ({conn->id, conn});
  /* *INDENT-ON* */

  if (conn->data) {
    conn->configured = true;
    gst_win32_ipc_server_config_data (self, conn.get ());
  } else {
    GST_DEBUG_OBJECT (self, "Have no config data yet, waiting for data");
  }
}

static gpointer
gst_win32_ipc_server_loop_thread_func (GstWin32IpcServer * self)
{
  auto priv = self->priv;
  bool io_pending = false;
  guint wait_ret;
  HANDLE pipe;
  OVERLAPPED overlap;
  HANDLE waitables[3];

  GST_DEBUG_OBJECT (self, "Entering loop");

  memset (&overlap, 0, sizeof (OVERLAPPED));

  overlap.hEvent = CreateEvent (nullptr, TRUE, TRUE, nullptr);
  pipe = gst_win32_ipc_server_create_pipe (self, &overlap, io_pending);
  if (pipe == INVALID_HANDLE_VALUE) {
    CloseHandle (overlap.hEvent);
    priv->aborted = true;
    goto out;
  }

  waitables[0] = overlap.hEvent;
  waitables[1] = priv->wakeup_event;
  waitables[2] = priv->cancellable;

  do {
    wait_ret = WaitForMultipleObjectsEx (G_N_ELEMENTS (waitables), waitables,
        FALSE, INFINITE, TRUE);

    if (wait_ret == WAIT_OBJECT_0 + 2) {
      GST_DEBUG_OBJECT (self, "Operation cancelled");
      goto out;
    }

    switch (wait_ret) {
      case WAIT_OBJECT_0:
      {
        DWORD n_bytes;

        if (io_pending
            && !GetOverlappedResult (pipe, &overlap, &n_bytes, FALSE)) {
          guint last_err = GetLastError ();
          auto err = g_win32_error_message (last_err);
          GST_WARNING_OBJECT (self, "GetOverlappedResult failed with 0x%x (%s)",
              last_err, err);
          g_free (err);
          CloseHandle (pipe);
          pipe = INVALID_HANDLE_VALUE;
          break;
        }

        auto conn = std::make_shared < GstWin32IpcServerConn > (pipe);
        conn->server = self;
        pipe = INVALID_HANDLE_VALUE;
        gst_win32_ipc_server_on_incoming_connection (self, conn);

        pipe = gst_win32_ipc_server_create_pipe (self, &overlap, io_pending);
        break;
      }
      case WAIT_IO_COMPLETION:
        break;
      case WAIT_OBJECT_0 + 1:
        gst_win32_ipc_server_on_idle (self);
        break;
      default:
      {
        guint last_err = GetLastError ();
        auto err = g_win32_error_message (last_err);
        GST_ERROR_OBJECT (self,
            "WaitForMultipleObjectsEx return 0x%x, last error 0x%x (%s)",
            wait_ret, last_err, err);
        g_free (err);
        priv->aborted = true;
        goto out;
      }
    }
  } while (true);

out:
  if (pipe != INVALID_HANDLE_VALUE) {
    CancelIo (pipe);
    DisconnectNamedPipe (pipe);
    CloseHandle (pipe);
  }

  CloseHandle (overlap.hEvent);

  priv->conn_map.clear ();

  GST_DEBUG_OBJECT (self, "Exit loop thread");

  return nullptr;
}

GstFlowReturn
gst_win32_ipc_server_send_data (GstWin32IpcServer * server,
    GstWin32IpcMmf * mmf, GstClockTime pts, GstCaps * caps,
    GByteArray * meta, gpointer user_data, GDestroyNotify notify)
{
  GstWin32IpcServerPrivate *priv;

  g_return_val_if_fail (GST_IS_WIN32_IPC_SERVER (server), GST_FLOW_ERROR);

  priv = server->priv;

  GST_LOG_OBJECT (server, "Sending data");

  {
    std::unique_lock < std::mutex > lk (priv->lock);
    if (priv->aborted) {
      GST_DEBUG_OBJECT (server, "Was aborted");
      if (notify)
        notify (user_data);

      return GST_FLOW_ERROR;
    }

    if (priv->max_buffers > 0) {
      if (priv->leaky == GST_WIN32_IPC_LEAKY_NONE) {
        if (priv->data_queue.size () >= priv->max_buffers) {
          GST_DEBUG_OBJECT (server, "Waiting for free space");
          priv->cond.wait (lk,[&] {
                auto max = priv->max_buffers.load ();
                return priv->aborted || priv->flushing || max == 0 ||
                priv->data_queue.size () < priv->max_buffers;
              }
          );
        }

        if (priv->aborted) {
          GST_DEBUG_OBJECT (server, "Aborted while waiting for free slot");
          if (notify)
            notify (user_data);

          return GST_FLOW_ERROR;
        } else if (priv->flushing) {
          GST_DEBUG_OBJECT (server, "We are flushing");
          if (notify)
            notify (user_data);

          return GST_FLOW_FLUSHING;
        }
      } else {
        if (priv->data_queue.size () >= priv->max_buffers) {
          if (priv->leaky == GST_WIN32_IPC_LEAKY_DOWNSTREAM) {
            auto dropped = priv->data_queue.front ();
            priv->data_queue.pop_front ();
            GST_DEBUG_OBJECT (server,
                "Queue full, dropping oldest seq=%" G_GUINT64_FORMAT,
                dropped->seq_num);
          } else {
            GST_DEBUG_OBJECT (server, "Queue full, dropping current buffer");
            if (notify)
              notify (user_data);

            return GST_FLOW_OK;
          }
        }
      }
    }

    auto data = std::make_shared < GstWin32IpcServerData > (mmf, pts, caps,
        meta, priv->seq_num, user_data, notify);
    GST_DEBUG_OBJECT (server, "Enqueue data, seq-num %" G_GUINT64_FORMAT,
        priv->seq_num);

    priv->seq_num++;
    priv->data_queue.push_back (data);
  }

  SetEvent (priv->wakeup_event);

  return GST_FLOW_OK;
}

void
gst_win32_ipc_server_stop (GstWin32IpcServer * server)
{
  GstWin32IpcServerPrivate *priv;

  g_return_if_fail (GST_IS_WIN32_IPC_SERVER (server));

  priv = server->priv;

  GST_DEBUG_OBJECT (server, "Stopping");
  priv->shutdown = true;
  SetEvent (priv->wakeup_event);

  g_clear_pointer (&priv->loop_thread, g_thread_join);

  GST_DEBUG_OBJECT (server, "Stopped");
}

void
gst_win32_ipc_server_set_flushing (GstWin32IpcServer * server,
    gboolean flushing)
{
  auto priv = server->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->flushing = flushing;
  priv->cond.notify_all ();
}

void
gst_win32_ipc_server_set_max_buffers (GstWin32IpcServer * server,
    guint64 max_buffers)
{
  auto priv = server->priv;
  bool updated = false;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (priv->max_buffers != max_buffers) {
      updated = true;
      priv->max_buffers = max_buffers;
      priv->cond.notify_all ();
    }
  }

  if (updated)
    SetEvent (priv->wakeup_event);
}

void
gst_win32_ipc_server_set_leaky (GstWin32IpcServer * server,
    GstWin32IpcLeakyType leaky)
{
  auto priv = server->priv;
  bool updated = false;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (priv->leaky != leaky) {
      updated = true;
      priv->leaky = leaky;
      priv->cond.notify_all ();
    }
  }

  if (updated)
    SetEvent (priv->wakeup_event);
}

guint64
gst_win32_ipc_server_get_current_level_buffers (GstWin32IpcServer * server)
{
  auto priv = server->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  return priv->data_queue.size ();
}

GstWin32IpcServer *
gst_win32_ipc_server_new (const std::string & address,
    guint64 max_buffers, GstWin32IpcLeakyType leaky)
{
  auto self = (GstWin32IpcServer *)
      g_object_new (GST_TYPE_WIN32_IPC_SERVER, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->address = address;
  priv->max_buffers = max_buffers;
  priv->leaky = leaky;

  priv->loop_thread = g_thread_new ("win32-ipc-server",
      (GThreadFunc) gst_win32_ipc_server_loop_thread_func, self);

  return self;
}
