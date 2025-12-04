/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include "gstwin32ipcclient.h"
#include "gstwin32ipcprotocol.h"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_client_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_client_debug

#define CONN_BUFFER_SIZE 1024

/* *INDENT-OFF* */
struct GstWin32IpcClientConn : public OVERLAPPED
{
  GstWin32IpcClientConn (GstWin32IpcClient * client, HANDLE pipe_handle)
      : client (client), pipe (pipe_handle)
  {
    OVERLAPPED *parent = static_cast<OVERLAPPED *> (this);
    parent->Internal = 0;
    parent->InternalHigh = 0;
    parent->Offset = 0;
    parent->OffsetHigh = 0;

    client_msg.resize (CONN_BUFFER_SIZE);
    server_msg.resize (CONN_BUFFER_SIZE);
  }

  ~GstWin32IpcClientConn ()
  {
    if (pipe != INVALID_HANDLE_VALUE) {
      CancelIo (pipe);
      CloseHandle (pipe);
    }
  }

  GstWin32IpcClient *client;

  HANDLE pipe = INVALID_HANDLE_VALUE;

  GstWin32IpcPktType type;
  std::vector<guint8> client_msg;
  std::vector<guint8> server_msg;
};

struct GstWin32IpcImportData
{
  ~GstWin32IpcImportData ()
  {
    GST_LOG_OBJECT (client, "Release handle \"%p\"", server_handle);
    gst_object_unref (client);
    if (mmf)
      gst_win32_ipc_mmf_unref (mmf);
  }

  GstWin32IpcClient *client;
  HANDLE server_handle = nullptr;
  GstWin32IpcMmf *mmf = nullptr;
};

struct GstWin32IpcReleaseData
{
  GstWin32IpcClient *self;
  std::shared_ptr<GstWin32IpcImportData> imported;
};

struct GstWin32IpcClientPrivate
{
  GstWin32IpcClientPrivate ()
  {
    wakeup_event = CreateEvent (nullptr, FALSE, FALSE, nullptr);
    cancellable = CreateEvent (nullptr, TRUE, FALSE, nullptr);

    shutdown = false;
    io_pending = true;
  }

  ~GstWin32IpcClientPrivate ()
  {
    gst_clear_caps (&caps);
    CloseHandle (wakeup_event);
    CloseHandle (cancellable);
    if (server_process)
      CloseHandle (server_process);
  }

  std::string address;
  GstClockTime timeout;
  HANDLE wakeup_event;
  HANDLE cancellable;
  HANDLE server_process = nullptr;
  std::mutex lock;
  std::condition_variable cond;
  GstCaps *caps = nullptr;
  std::string caps_string;
  bool server_eos = false;
  bool flushing = false;
  bool aborted = false;
  bool sent_fin = false;
  std::atomic<bool> shutdown = { false };
  std::atomic<bool> io_pending = { false };
  GThread *loop_thread = nullptr;
  std::queue <GstSample *> samples;
  std::shared_ptr<GstWin32IpcClientConn> conn;
  std::queue<HANDLE> unused_data;
  std::vector<std::weak_ptr<GstWin32IpcImportData>> imported;

  std::atomic<guint64> max_buffers = { 0 };
  std::atomic<GstWin32IpcLeakyType> leaky { GST_WIN32_IPC_LEAKY_DOWNSTREAM };
};
/* *INDENT-ON* */

struct _GstWin32IpcClient
{
  GstObject parent;

  GstWin32IpcClientPrivate *priv;
};

static void gst_win32_ipc_client_dispose (GObject * object);
static void gst_win32_ipc_client_finalize (GObject * object);
static void gst_win32_ipc_client_continue (GstWin32IpcClient * self);
static void gst_win32_ipc_client_send_msg (GstWin32IpcClient * self);

#define gst_win32_ipc_client_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcClient, gst_win32_ipc_client, GST_TYPE_OBJECT);

static void
gst_win32_ipc_client_class_init (GstWin32IpcClientClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_win32_ipc_client_dispose;
  object_class->finalize = gst_win32_ipc_client_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_client_debug, "win32ipcclient",
      0, "win32ipcclient");
}

static void
gst_win32_ipc_client_init (GstWin32IpcClient * self)
{
  self->priv = new GstWin32IpcClientPrivate ();
}

static void
gst_win32_ipc_client_dispose (GObject * object)
{
  auto self = GST_WIN32_IPC_CLIENT (object);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "dispose");

  SetEvent (priv->cancellable);

  g_clear_pointer (&priv->loop_thread, g_thread_join);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_win32_ipc_client_finalize (GObject * object)
{
  auto self = GST_WIN32_IPC_CLIENT (object);

  GST_DEBUG_OBJECT (self, "finalize");

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_win32_ipc_client_abort (GstWin32IpcClient * self)
{
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  priv->aborted = true;
  priv->cond.notify_all ();
}

static bool
gst_win32_ipc_client_config_data (GstWin32IpcClient * self)
{
  auto priv = self->priv;
  auto conn = priv->conn;
  std::string caps_string;
  DWORD server_pid;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (!gst_win32_ipc_pkt_parse_config (conn->server_msg,
          server_pid, caps_string)) {
    GST_ERROR_OBJECT (self, "Couldn't parse CONFIG-DATA");
    return false;
  }

  if (caps_string.empty ()) {
    GST_ERROR_OBJECT (self, "Empty caps");
    return false;
  }

  priv->caps_string = caps_string;

  gst_clear_caps (&priv->caps);
  priv->caps = gst_caps_from_string (caps_string.c_str ());
  if (!priv->caps) {
    GST_ERROR_OBJECT (self, "Invalid caps string \"%s\"", caps_string.c_str ());
    return false;
  }

  if (priv->server_process) {
    GST_WARNING_OBJECT (self, "Have server process handle already");
    CloseHandle (priv->server_process);
  }

  priv->server_process = OpenProcess (PROCESS_DUP_HANDLE, FALSE, server_pid);
  if (!priv->server_process) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_ERROR_OBJECT (self, "Couldn't open server process, 0x%x (%s)",
        last_err, err);
    g_free (err);
    return false;
  }

  priv->cond.notify_all ();

  return true;
}

static void
gst_win32_ipc_client_release_imported_data (GstWin32IpcReleaseData * data)
{
  auto self = data->self;
  auto priv = self->priv;
  HANDLE server_handle = data->imported->server_handle;

  GST_LOG_OBJECT (self, "Releasing data \"%p\"", server_handle);

  data->imported = nullptr;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    priv->unused_data.push (server_handle);
  }

  SetEvent (priv->wakeup_event);

  gst_object_unref (data->self);

  delete data;
}

static bool
gst_win32_ipc_client_have_data (GstWin32IpcClient * self)
{
  auto priv = self->priv;
  GstBuffer *buffer = nullptr;
  SIZE_T size;
  std::string caps_string;
  GstClockTime pts;
  GstClockTime dts;
  GstClockTime dur;
  UINT buf_flags = 0;
  std::shared_ptr < GstWin32IpcImportData > import_data;
  HANDLE server_handle = nullptr;
  HANDLE client_handle = nullptr;
  std::vector < UINT8 > meta;
  auto conn = priv->conn;

  std::unique_lock < std::mutex > lk (priv->lock);

  if (!gst_win32_ipc_pkt_parse_have_data (conn->server_msg, size,
          pts, dts, dur, buf_flags, server_handle, caps_string, meta)) {
    GST_ERROR_OBJECT (self, "Couldn't parse HAVE-DATA packet");
    return false;
  }

  if (!caps_string.empty () && caps_string != priv->caps_string) {
    auto new_caps = gst_caps_from_string (caps_string.c_str ());
    if (!new_caps) {
      GST_ERROR_OBJECT (self, "Invalid caps string \"%s\"",
          caps_string.c_str ());
      return false;
    }

    gst_caps_unref (priv->caps);
    priv->caps = new_caps;
  }

  if (!DuplicateHandle (priv->server_process, server_handle,
          GetCurrentProcess (), &client_handle, 0, FALSE,
          DUPLICATE_SAME_ACCESS)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_ERROR_OBJECT (self, "Couldn't duplicate handle, 0x%x (%s)",
        last_err, err);
    g_free (err);
    return false;
  }

  GST_LOG_OBJECT (self, "Importing server handle %p", server_handle);

  auto mmf = gst_win32_ipc_mmf_open (size, client_handle);
  if (!mmf) {
    GST_ERROR_OBJECT (self, "Couldn't open resource");
    return false;
  }

  import_data = std::make_shared < GstWin32IpcImportData > ();
  import_data->client = (GstWin32IpcClient *) gst_object_ref (self);
  import_data->server_handle = server_handle;
  import_data->mmf = mmf;

  {
    auto data = new GstWin32IpcReleaseData ();
    data->self = (GstWin32IpcClient *) gst_object_ref (self);
    data->imported = import_data;

    auto mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
        gst_win32_ipc_mmf_get_raw (mmf), size, 0, size, data,
        (GDestroyNotify) gst_win32_ipc_client_release_imported_data);

    buffer = gst_buffer_new ();
    gst_buffer_append_memory (buffer, mem);

    while (!meta.empty ()) {
      guint32 consumed = 0;
      if (!gst_meta_deserialize (buffer, meta.data (), meta.size (),
              &consumed) || consumed == 0) {
        break;
      }

      meta.erase (meta.begin (), meta.begin () + consumed);
    }

    priv->imported.push_back (import_data);
  }

  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_DTS (buffer) = dts;
  GST_BUFFER_DURATION (buffer) = dur;
  GST_BUFFER_FLAG_SET (buffer, buf_flags);

  auto sample = gst_sample_new (buffer, priv->caps, nullptr, nullptr);
  gst_buffer_unref (buffer);

  std::queue < GstSample * >drop_queue;
  bool drop_current = false;

  if (priv->max_buffers > 0) {
    if (priv->leaky == GST_WIN32_IPC_LEAKY_NONE) {
      if (priv->samples.size () >= priv->max_buffers) {
        GST_DEBUG_OBJECT (self, "Waiting for free space");
        priv->cond.wait (lk,[&] {
              auto max = priv->max_buffers.load ();
              return priv->aborted || priv->flushing || priv->shutdown ||
              priv->leaky != GST_WIN32_IPC_LEAKY_NONE || max == 0 ||
              priv->samples.size () < max;
            }
        );
      }

      if (priv->aborted) {
        GST_DEBUG_OBJECT (self, "Aborted while waiting for free slot");
        lk.unlock ();

        gst_sample_unref (sample);
        return false;
      } else if (priv->flushing || priv->shutdown) {
        GST_DEBUG_OBJECT (self, "Flushing while waiting for free slot");
        lk.unlock ();

        gst_sample_unref (sample);
        return true;
      }
    } else if (priv->leaky == GST_WIN32_IPC_LEAKY_DOWNSTREAM) {
      while (priv->samples.size () >= priv->max_buffers) {
        drop_queue.push (priv->samples.front ());
        priv->samples.pop ();
      }
    } else {
      if (priv->samples.size () >= priv->max_buffers) {
        GST_DEBUG_OBJECT (self, "Queue full, dropping current sample");
        drop_current = true;
      }
    }
  }

  if (!drop_current) {
    priv->samples.push (sample);
    priv->cond.notify_all ();
  }

  lk.unlock ();

  import_data = nullptr;
  while (!drop_queue.empty ()) {
    auto old = drop_queue.front ();
    gst_sample_unref (old);
    drop_queue.pop ();
  }

  if (drop_current)
    gst_sample_unref (sample);

  return true;
}

static void
gst_win32_ipc_client_wait_msg_finish (GstWin32IpcClient * client)
{
  auto priv = client->priv;
  auto conn = priv->conn;
  GstWin32IpcPktHdr hdr;

  if (!gst_win32_ipc_pkt_identify (conn->server_msg, hdr)) {
    GST_ERROR_OBJECT (client, "Broken header");
    gst_win32_ipc_client_abort (client);
    return;
  }

  switch (hdr.type) {
    case GstWin32IpcPktType::CONFIG:
      GST_LOG_OBJECT (client, "Got CONFIG");
      if (!gst_win32_ipc_client_config_data (client)) {
        gst_win32_ipc_client_abort (client);
        return;
      }

      gst_win32_ipc_client_continue (client);
      break;
    case GstWin32IpcPktType::HAVE_DATA:
      GST_LOG_OBJECT (client, "Got HAVE-DATA");
      if (!gst_win32_ipc_client_have_data (client)) {
        gst_win32_ipc_client_abort (client);
        return;
      }

      GST_LOG_OBJECT (client, "Sending READ-DONE");
      gst_win32_ipc_pkt_build_read_done (conn->client_msg);
      conn->type = GstWin32IpcPktType::READ_DONE;
      gst_win32_ipc_client_send_msg (client);
      break;
    case GstWin32IpcPktType::EOS:
      GST_DEBUG_OBJECT (client, "Got EOS");
      priv->server_eos = true;
      priv->lock.lock ();
      priv->cond.notify_all ();
      priv->lock.unlock ();
      gst_win32_ipc_client_continue (client);
      break;
    default:
      GST_WARNING_OBJECT (client, "Unexpected packet type");
      gst_win32_ipc_client_abort (client);
      break;
  }
}

static void WINAPI
gst_win32_ipc_client_payload_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  auto conn = static_cast < GstWin32IpcClientConn * >(overlap);
  auto self = conn->client;

  if (error_code != ERROR_SUCCESS) {
    auto err = g_win32_error_message (error_code);
    GST_WARNING_OBJECT (self, "ReadFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err);
    g_free (err);
    gst_win32_ipc_client_abort (self);
  }

  gst_win32_ipc_client_wait_msg_finish (self);
}

static void WINAPI
gst_win32_ipc_client_win32_wait_header_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  auto conn = static_cast < GstWin32IpcClientConn * >(overlap);
  auto self = conn->client;
  GstWin32IpcPktHdr hdr;

  if (error_code != ERROR_SUCCESS) {
    auto err = g_win32_error_message (error_code);
    GST_WARNING_OBJECT (self, "ReadFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err);
    g_free (err);
    gst_win32_ipc_client_abort (self);
    return;
  }

  if (!gst_win32_ipc_pkt_identify (conn->server_msg, hdr)) {
    GST_ERROR_OBJECT (self, "Broken header");
    gst_win32_ipc_client_abort (self);
    return;
  }

  if (hdr.payload_size == 0) {
    gst_win32_ipc_client_wait_msg_finish (self);
    return;
  }

  GST_LOG_OBJECT (self, "Reading payload");

  if (!ReadFileEx (conn->pipe, conn->server_msg.data () +
          sizeof (GstWin32IpcPktHdr), hdr.payload_size, conn,
          gst_win32_ipc_client_payload_finish)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_WARNING_OBJECT (self, "ReadFileEx failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    gst_win32_ipc_client_abort (self);
  }
}

static void
gst_win32_ipc_client_wait_msg (GstWin32IpcClient * self)
{
  auto priv = self->priv;
  auto conn = priv->conn;
  priv->io_pending = true;

  if (!ReadFileEx (conn->pipe, conn->server_msg.data (),
          sizeof (GstWin32IpcPktHdr), conn.get (),
          gst_win32_ipc_client_win32_wait_header_finish)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_WARNING_OBJECT (self, "ReadFileEx failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    gst_win32_ipc_client_abort (self);
  }
}

static void WINAPI
gst_win32_ipc_client_send_msg_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  auto conn = static_cast < GstWin32IpcClientConn * >(overlap);
  auto self = conn->client;

  if (error_code != ERROR_SUCCESS) {
    auto err = g_win32_error_message (error_code);
    GST_WARNING_OBJECT (self, "WriteFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err);
    g_free (err);
    gst_win32_ipc_client_abort (self);
    return;
  }

  switch (conn->type) {
    case GstWin32IpcPktType::NEED_DATA:
      GST_LOG_OBJECT (self, "Sent NEED-DATA");
      gst_win32_ipc_client_wait_msg (self);
      break;
    case GstWin32IpcPktType::READ_DONE:
      GST_LOG_OBJECT (self, "Sent READ-DONE");
      gst_win32_ipc_client_continue (self);
      break;
    case GstWin32IpcPktType::RELEASE_DATA:
      GST_LOG_OBJECT (self, "Sent RELEASE-DATA");
      gst_win32_ipc_client_continue (self);
      break;
    case GstWin32IpcPktType::FIN:
      GST_DEBUG_OBJECT (self, "Sent FIN");
      gst_win32_ipc_client_abort (self);
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected msg type");
      gst_win32_ipc_client_abort (self);
      break;
  }
}

static void
gst_win32_ipc_client_send_msg (GstWin32IpcClient * self)
{
  auto priv = self->priv;
  auto conn = priv->conn;

  priv->io_pending = true;

  if (!WriteFileEx (conn->pipe, conn->client_msg.data (),
          conn->client_msg.size (), conn.get (),
          gst_win32_ipc_client_send_msg_finish)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_WARNING_OBJECT (self, "WriteFileEx failed with 0x%x (%s)",
        last_err, err);
    g_free (err);
    gst_win32_ipc_client_abort (self);
  }
}

static void
gst_win32_ipc_client_run_gc (GstWin32IpcClient * self)
{
  auto priv = self->priv;

  for (auto it = priv->imported.begin (); it != priv->imported.end ();) {
    auto data = it->lock ();
    if (!data) {
      it = priv->imported.erase (it);
    } else {
      it++;
    }
  }
}

static void
gst_win32_ipc_client_continue (GstWin32IpcClient * self)
{
  auto priv = self->priv;
  std::unique_lock < std::mutex > lk (priv->lock);
  auto conn = priv->conn;

  if (!conn) {
    GST_WARNING_OBJECT (self, "No connection was made");
    priv->aborted = true;
    priv->cond.notify_all ();
    return;
  }

  if (priv->aborted) {
    priv->cond.notify_all ();
    GST_DEBUG_OBJECT (self, "Operation was aborted");
    return;
  }

  if (!priv->unused_data.empty ()) {
    HANDLE server_handle = priv->unused_data.front ();
    priv->unused_data.pop ();

    GST_LOG_OBJECT (self, "Sending RELEASE-DATA %p", server_handle);

    gst_win32_ipc_pkt_build_release_data (conn->client_msg, server_handle);
    conn->type = GstWin32IpcPktType::RELEASE_DATA;
    lk.unlock ();

    gst_win32_ipc_client_send_msg (self);
    return;
  }

  if (priv->shutdown) {
    auto drop_queue = priv->samples;
    while (!priv->samples.empty ())
      priv->samples.pop ();
    lk.unlock ();

    while (!drop_queue.empty ()) {
      auto sample = drop_queue.front ();
      gst_sample_unref (sample);
      drop_queue.pop ();
    }
    lk.lock ();
  }

  if (priv->server_eos || priv->shutdown) {
    gst_win32_ipc_client_run_gc (self);

    GST_DEBUG_OBJECT (self, "Remaining imported memory %" G_GSIZE_FORMAT,
        priv->imported.size ());

    if (priv->imported.empty ()) {
      GST_DEBUG_OBJECT (self, "Drained");
      if (priv->sent_fin) {
        priv->aborted = true;
        priv->cond.notify_all ();
      } else {
        lk.unlock ();

        priv->sent_fin = true;
        gst_win32_ipc_pkt_build_fin (conn->client_msg);
        conn->type = GstWin32IpcPktType::FIN;

        GST_DEBUG_OBJECT (self, "Sending FIN");
        gst_win32_ipc_client_send_msg (self);
        return;
      }
    } else {
      priv->io_pending = false;
    }
    return;
  }

  lk.unlock ();

  gst_win32_ipc_pkt_build_need_data (conn->client_msg);
  conn->type = GstWin32IpcPktType::NEED_DATA;

  GST_LOG_OBJECT (self, "Sending NEED-DATA");
  gst_win32_ipc_client_send_msg (self);
}

static gpointer
gst_win32_ipc_client_loop_thread_func (GstWin32IpcClient * self)
{
  auto priv = self->priv;
  DWORD mode = PIPE_READMODE_MESSAGE;
  guint wait_ret;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  auto start_time = gst_util_get_timestamp ();
  HANDLE waitables[] = { priv->cancellable, priv->wakeup_event };
  auto address = (wchar_t *) g_utf8_to_utf16 (priv->address.c_str (),
      -1, nullptr, nullptr, nullptr);

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
  CREATEFILE2_EXTENDED_PARAMETERS params;
  memset (&params, 0, sizeof (CREATEFILE2_EXTENDED_PARAMETERS));
  params.dwSize = sizeof (CREATEFILE2_EXTENDED_PARAMETERS);
  params.dwFileAttributes = 0;
  params.dwFileFlags = FILE_FLAG_OVERLAPPED;
  params.dwSecurityQosFlags = SECURITY_IMPERSONATION;
#endif

  GST_DEBUG_OBJECT (self, "Starting loop thread");

  std::unique_lock < std::mutex > lk (priv->lock);
  do {
    GstClockTime diff;

    if (priv->flushing) {
      GST_DEBUG_OBJECT (self, "We are flushing");
      priv->aborted = true;
      priv->cond.notify_all ();
      goto out;
    }
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
    pipe = CreateFile2 (address, GENERIC_READ | GENERIC_WRITE, 0,
        OPEN_EXISTING, &params);
#else
    pipe = CreateFileW (address,
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
#endif

    if (pipe != INVALID_HANDLE_VALUE)
      break;

    if (priv->timeout > 0) {
      diff = gst_util_get_timestamp () - start_time;
      if (diff > priv->timeout) {
        GST_WARNING_OBJECT (self, "Timeout");
        priv->aborted = true;
        priv->cond.notify_all ();
        goto out;
      }
    }

    /* Retry per 100ms */
    GST_DEBUG_OBJECT (self, "Sleep for next retry");
    priv->cond.wait_for (lk, std::chrono::milliseconds (100));
  } while (true);

  if (!SetNamedPipeHandleState (pipe, &mode, nullptr, nullptr)) {
    guint last_err = GetLastError ();
    auto err = g_win32_error_message (last_err);
    GST_WARNING_OBJECT (self, "SetNamedPipeHandleState failed with 0x%x (%s)",
        last_err, err);
    g_free (err);

    CloseHandle (pipe);
    priv->aborted = true;
    priv->cond.notify_all ();
    goto out;
  }

  priv->conn = std::make_shared < GstWin32IpcClientConn > (self, pipe);
  priv->cond.notify_all ();
  lk.unlock ();

  gst_win32_ipc_client_wait_msg (self);

  do {
    /* Enters alertable thread state and wait for I/O completion event
     * or cancellable event */
    wait_ret = WaitForMultipleObjectsEx (G_N_ELEMENTS (waitables), waitables,
        FALSE, INFINITE, TRUE);
    if (wait_ret == WAIT_OBJECT_0) {
      GST_DEBUG ("Operation cancelled");
      goto out;
    }

    switch (wait_ret) {
      case WAIT_IO_COMPLETION:
        break;
      case WAIT_OBJECT_0 + 1:
        if (!priv->io_pending)
          gst_win32_ipc_client_continue (self);
        break;
      default:
        GST_WARNING ("Unexpected wait return 0x%x", wait_ret);
        gst_win32_ipc_client_abort (self);
        goto out;
    }
  } while (true);

out:
  while (!priv->samples.empty ()) {
    auto sample = priv->samples.front ();
    gst_sample_unref (sample);
    priv->samples.pop ();
  }

  priv->conn = nullptr;
  g_free (address);

  GST_DEBUG_OBJECT (self, "Exit loop thread");

  return nullptr;
}

GstFlowReturn
gst_win32_ipc_client_run (GstWin32IpcClient * client)
{
  g_return_val_if_fail (GST_IS_WIN32_IPC_CLIENT (client), GST_FLOW_ERROR);

  auto priv = client->priv;
  std::unique_lock < std::mutex > lk (priv->lock);
  if (!priv->loop_thread) {
    priv->loop_thread = g_thread_new ("win32-ipc-client",
        (GThreadFunc) gst_win32_ipc_client_loop_thread_func, client);

    while (!priv->caps && !priv->aborted && !priv->flushing)
      priv->cond.wait (lk);
  }

  if (priv->flushing) {
    GST_DEBUG_OBJECT (client, "We are flushing");
    return GST_FLOW_FLUSHING;
  } else if (priv->aborted || !priv->caps) {
    GST_DEBUG_OBJECT (client, "Aborted");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

GstCaps *
gst_win32_ipc_client_get_caps (GstWin32IpcClient * client)
{
  GstCaps *caps = nullptr;

  g_return_val_if_fail (GST_IS_WIN32_IPC_CLIENT (client), nullptr);

  auto priv = client->priv;

  if (gst_win32_ipc_client_run (client) != GST_FLOW_OK)
    return nullptr;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->caps)
    caps = gst_caps_ref (priv->caps);

  return caps;
}

static void
gst_win32_ipc_client_stop_async (GstWin32IpcClient * client, gpointer user_data)
{
  auto priv = client->priv;

  GST_DEBUG_OBJECT (client, "Stopping");

  SetEvent (priv->cancellable);
  g_clear_pointer (&priv->loop_thread, g_thread_join);

  GST_DEBUG_OBJECT (client, "Stopped");
}

void
gst_win32_ipc_client_stop (GstWin32IpcClient * client)
{
  g_return_if_fail (GST_IS_WIN32_IPC_CLIENT (client));

  auto priv = client->priv;

  GST_DEBUG_OBJECT (client, "Stopping");

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    priv->shutdown = true;
    priv->cond.notify_all ();
  }

  SetEvent (priv->wakeup_event);

  /* We don't know when imported memory gets released */
  gst_object_call_async (GST_OBJECT (client),
      (GstObjectCallAsyncFunc) gst_win32_ipc_client_stop_async, nullptr);
}

void
gst_win32_ipc_client_set_flushing (GstWin32IpcClient * client, bool flushing)
{
  g_return_if_fail (GST_IS_WIN32_IPC_CLIENT (client));

  auto priv = client->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->flushing = flushing;
  priv->cond.notify_all ();
}

GstFlowReturn
gst_win32_ipc_client_get_sample (GstWin32IpcClient * client,
    GstSample ** sample)
{
  g_return_val_if_fail (GST_IS_WIN32_IPC_CLIENT (client), GST_FLOW_ERROR);
  g_return_val_if_fail (sample, GST_FLOW_ERROR);

  auto priv = client->priv;

  GST_LOG_OBJECT (client, "Waiting for sample");
  std::unique_lock < std::mutex > lk (priv->lock);
  while (!priv->flushing && !priv->aborted && !priv->server_eos &&
      priv->samples.empty ()) {
    priv->cond.wait (lk);
  }

  if (!priv->samples.empty ()) {
    *sample = priv->samples.front ();
    priv->samples.pop ();

    priv->cond.notify_all ();

    GST_LOG_OBJECT (client, "Have sample");
    return GST_FLOW_OK;
  }

  if (priv->flushing) {
    GST_DEBUG_OBJECT (client, "Flushing");
    return GST_FLOW_FLUSHING;
  }

  GST_DEBUG_OBJECT (client, "EOS");

  return GST_FLOW_EOS;
}

void
gst_win32_ipc_client_set_leaky (GstWin32IpcClient * client,
    GstWin32IpcLeakyType leaky)
{
  auto priv = client->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->leaky != leaky) {
    priv->leaky = leaky;
    priv->cond.notify_all ();
  }
}

void
gst_win32_ipc_client_set_max_buffers (GstWin32IpcClient * client,
    guint64 max_buffers)
{
  auto priv = client->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->max_buffers != max_buffers) {
    priv->max_buffers = max_buffers;
    priv->cond.notify_all ();
  }
}

guint64
gst_win32_ipc_client_get_current_level_buffers (GstWin32IpcClient * client)
{
  auto priv = client->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  return priv->samples.size ();
}

GstWin32IpcClient *
gst_win32_ipc_client_new (const std::string & address, guint timeout,
    guint64 max_buffers, GstWin32IpcLeakyType leaky)
{
  auto self = (GstWin32IpcClient *)
      g_object_new (GST_TYPE_WIN32_IPC_CLIENT, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->address = address;
  priv->timeout = timeout * GST_SECOND;
  priv->max_buffers = max_buffers;
  priv->leaky = leaky;

  return self;
}
