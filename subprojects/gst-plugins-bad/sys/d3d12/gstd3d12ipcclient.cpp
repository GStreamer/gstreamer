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

#include "gstd3d12ipcclient.h"
#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
static std::mutex gc_pool_lock;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_ipc_client_debug);
#define GST_CAT_DEFAULT gst_d3d12_ipc_client_debug

static GThreadPool *gc_thread_pool = nullptr;

void
gst_d3d12_ipc_client_deinit (void)
{
  std::lock_guard < std::mutex > lk (gc_pool_lock);
  if (gc_thread_pool) {
    g_thread_pool_free (gc_thread_pool, FALSE, TRUE);
    gc_thread_pool = nullptr;
  }
}

/**
 * GstD3D12IpcIOMode:
 *
 * Texture import mode
 *
 * Since: 1.26
 */
GType
gst_d3d12_ipc_io_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue io_modes[] = {
    /**
     * GstD3D12IpcIOMode::copy:
     *
     * Copy remote texture to newly allocated texture
     *
     * Since: 1.26
     */
    {GST_D3D12_IPC_IO_COPY, "Copy remote texture", "copy"},

    /**
     * GstD3D12IpcIOMode::import:
     *
     * Import remote texture to without any allocation/copy
     *
     * Since: 1.26
     */
    {GST_D3D12_IPC_IO_IMPORT, "Import remote texture", "import"},
    {0, nullptr, nullptr}
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12IpcIOMode", io_modes);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

/* *INDENT-OFF* */
struct GstD3D12IpcClientConn : public OVERLAPPED
{
  GstD3D12IpcClientConn (GstD3D12IpcClient * client, HANDLE pipe_handle)
      : client (client), pipe (pipe_handle)
  {
    OVERLAPPED *parent = static_cast<OVERLAPPED *> (this);
    parent->Internal = 0;
    parent->InternalHigh = 0;
    parent->Offset = 0;
    parent->OffsetHigh = 0;

    client_msg.resize (GST_D3D12_IPC_PKT_HEADER_SIZE);
    server_msg.resize (GST_D3D12_IPC_PKT_HEADER_SIZE);
  }

  ~GstD3D12IpcClientConn ()
  {
    if (pipe != INVALID_HANDLE_VALUE) {
      CancelIo (pipe);
      CloseHandle (pipe);
    }
  }

  GstD3D12IpcClient *client;

  HANDLE pipe = INVALID_HANDLE_VALUE;

  GstD3D12IpcPktType type;
  std::vector<guint8> client_msg;
  std::vector<guint8> server_msg;
};

struct GstD3D12IpcImportData
{
  ~GstD3D12IpcImportData ()
  {
    GST_LOG_OBJECT (client, "Release handle \"%p\"", server_handle);
    gst_object_unref (client);
  }

  GstD3D12IpcClient *client;
  ComPtr<ID3D12Resource> texture;
  GstD3D12IpcMemLayout layout;
  HANDLE server_handle = nullptr;
};

struct GstD3D12IpcReleaseData
{
  GstD3D12IpcClient *self;
  std::shared_ptr<GstD3D12IpcImportData> imported;
};

struct GstD3D12IpcClientPrivate
{
  GstD3D12IpcClientPrivate ()
  {
    wakeup_event = CreateEvent (nullptr, FALSE, FALSE, nullptr);
    cancellable = CreateEvent (nullptr, TRUE, FALSE, nullptr);

    shutdown = false;
    io_pending = true;
  }

  ~GstD3D12IpcClientPrivate ()
  {
    gst_clear_caps (&caps);
    if (pool) {
      gst_buffer_pool_set_active (pool, FALSE);
      gst_object_unref (pool);
    }

    gst_clear_object (&device);

    CloseHandle (wakeup_event);
    CloseHandle (cancellable);
    if (server_process)
      CloseHandle (server_process);
  }

  std::string address;
  GstD3D12IpcIOMode io_mode = GST_D3D12_IPC_IO_COPY;
  GstClockTime timeout;
  HANDLE wakeup_event;
  HANDLE cancellable;
  HANDLE server_process = nullptr;
  std::mutex lock;
  std::condition_variable cond;
  GstD3D12Device *device = nullptr;
  GstCaps *caps = nullptr;
  GstBufferPool *pool = nullptr;
  GstVideoInfo info;
  bool server_eos = false;
  bool flushing = false;
  bool aborted = false;
  bool sent_fin = false;
  std::atomic<bool> shutdown;
  std::atomic<bool> io_pending;
  GThread *loop_thread = nullptr;
  std::queue <GstSample *> samples;
  std::shared_ptr<GstD3D12IpcClientConn> conn;
  std::queue<HANDLE> unused_data;
  std::vector<std::weak_ptr<GstD3D12IpcImportData>> imported;
  ComPtr<ID3D12Fence> server_fence;
};
/* *INDENT-ON* */

struct _GstD3D12IpcClient
{
  GstObject parent;

  GstD3D12IpcClientPrivate *priv;
};

static void gst_d3d12_ipc_client_dispose (GObject * object);
static void gst_d3d12_ipc_client_finalize (GObject * object);
static void gst_d3d12_ipc_client_continue (GstD3D12IpcClient * self);
static void gst_d3d12_ipc_client_send_msg (GstD3D12IpcClient * self);

#define gst_d3d12_ipc_client_parent_class parent_class
G_DEFINE_TYPE (GstD3D12IpcClient, gst_d3d12_ipc_client, GST_TYPE_OBJECT);

static void
gst_d3d12_ipc_client_class_init (GstD3D12IpcClientClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_d3d12_ipc_client_dispose;
  object_class->finalize = gst_d3d12_ipc_client_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_ipc_client_debug, "d3d12ipcclient",
      0, "d3d12ipcclient");
}

static void
gst_d3d12_ipc_client_init (GstD3D12IpcClient * self)
{
  self->priv = new GstD3D12IpcClientPrivate ();
}

static void
gst_d3d12_ipc_client_dispose (GObject * object)
{
  auto self = GST_D3D12_IPC_CLIENT (object);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "dispose");

  SetEvent (priv->cancellable);

  g_clear_pointer (&priv->loop_thread, g_thread_join);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d12_ipc_client_finalize (GObject * object)
{
  auto self = GST_D3D12_IPC_CLIENT (object);

  GST_DEBUG_OBJECT (self, "finalize");

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_ipc_client_abort (GstD3D12IpcClient * self)
{
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  priv->aborted = true;
  priv->cond.notify_all ();
}

static bool
gst_d3d12_client_update_caps (GstD3D12IpcClient * self, GstCaps * caps)
{
  auto priv = self->priv;

  if (!caps)
    return true;

  gst_clear_caps (&priv->caps);
  priv->caps = caps;

  if (priv->pool) {
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_clear_object (&priv->pool);
  }

  if (!gst_video_info_from_caps (&priv->info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps");
    return false;
  }

  if (priv->io_mode == GST_D3D12_IPC_IO_COPY) {
    priv->pool = gst_d3d12_buffer_pool_new (priv->device);
    auto config = gst_buffer_pool_get_config (priv->pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    gst_buffer_pool_config_set_params (config, priv->caps,
        GST_VIDEO_INFO_SIZE (&priv->info), 0, 0);

    auto params = gst_d3d12_allocation_params_new (priv->device, &priv->info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_HEAP_FLAG_NONE);

    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);

    if (!gst_buffer_pool_set_config (priv->pool, config)) {
      GST_ERROR_OBJECT (self, "Couldn't set pool config");
      gst_clear_object (&priv->pool);
      return false;
    }

    if (!gst_buffer_pool_set_active (priv->pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Couldn't active pool");
      gst_clear_object (&priv->pool);
      return false;
    }
  }

  return true;
}

static bool
gst_d3d12_ipc_client_config_data (GstD3D12IpcClient * self)
{
  auto priv = self->priv;
  gint64 prev_luid, luid;
  GstCaps *caps = nullptr;
  auto conn = priv->conn;
  DWORD server_pid;
  HANDLE server_fence_handle;
  std::lock_guard < std::mutex > lk (priv->lock);

  g_object_get (priv->device, "adapter-luid", &prev_luid, nullptr);

  if (!gst_d3d12_ipc_pkt_parse_config (conn->server_msg,
          server_pid, luid, server_fence_handle, &caps)) {
    GST_ERROR_OBJECT (self, "Couldn't parse CONFIG-DATA");
    return false;
  }

  if (priv->server_process) {
    GST_WARNING_OBJECT (self, "Have server process handle already");
    CloseHandle (priv->server_process);
  }

  priv->server_process = OpenProcess (PROCESS_DUP_HANDLE, FALSE, server_pid);
  if (!priv->server_process) {
    guint last_err = GetLastError ();
    auto err = gst_d3d12_ipc_win32_error_to_string (last_err);
    GST_ERROR_OBJECT (self, "Couldn't open server process, 0x%x (%s)",
        last_err, err.c_str ());
    return false;
  }

  if (prev_luid != luid) {
    auto device = gst_d3d12_device_new_for_adapter_luid (luid);
    if (!device) {
      GST_ERROR_OBJECT (self, "Couldn't create device");
      return false;
    }

    gst_object_unref (priv->device);
    priv->device = device;
  }

  if (!gst_d3d12_client_update_caps (self, caps))
    return false;

  HANDLE client_fence_handle;
  if (!DuplicateHandle (priv->server_process, server_fence_handle,
          GetCurrentProcess (), &client_fence_handle, 0, FALSE,
          DUPLICATE_SAME_ACCESS)) {
    guint last_err = GetLastError ();
    auto err = gst_d3d12_ipc_win32_error_to_string (last_err);
    GST_ERROR_OBJECT (self, "Couldn't duplicate handle, 0x%x (%s)",
        last_err, err.c_str ());
    return false;
  }

  priv->server_fence = nullptr;
  auto device = gst_d3d12_device_get_device_handle (priv->device);
  auto hr = device->OpenSharedHandle (client_fence_handle,
      IID_PPV_ARGS (&priv->server_fence));
  CloseHandle (client_fence_handle);
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't open server fence");
    return false;
  }

  priv->cond.notify_all ();

  return true;
}

static void
gst_d3d12_ipc_client_release_imported_data (GstD3D12IpcReleaseData * data)
{
  auto self = data->self;
  auto priv = self->priv;
  HANDLE server_handle = data->imported->server_handle;

  GST_LOG_OBJECT (self, "Releasing data \"%p\"", server_handle);

  data->imported = nullptr;

  priv->lock.lock ();
  priv->unused_data.push (server_handle);
  priv->lock.unlock ();

  SetEvent (priv->wakeup_event);

  gst_object_unref (data->self);

  delete data;
}

static bool
gst_d3d12_ipc_client_have_data (GstD3D12IpcClient * self)
{
  auto priv = self->priv;
  GstBuffer *buffer = nullptr;
  GstSample *sample;
  GstClockTime pts;
  GstCaps *caps = nullptr;
  GstD3D12IpcMemLayout layout;
  std::shared_ptr < GstD3D12IpcImportData > import_data;
  std::unique_lock < std::mutex > lk (priv->lock);
  HANDLE server_handle = nullptr;
  HANDLE client_handle = nullptr;
  auto conn = priv->conn;
  ComPtr < ID3D12Resource > texture;
  HRESULT hr;
  guint64 fence_val = 0;

  if (!gst_d3d12_ipc_pkt_parse_have_data (conn->server_msg,
          pts, layout, server_handle, fence_val, &caps)) {
    GST_ERROR_OBJECT (self, "Couldn't parse HAVE-DATA packet");
    return false;
  }

  if (!gst_d3d12_client_update_caps (self, caps))
    return false;

  if (!DuplicateHandle (priv->server_process, server_handle,
          GetCurrentProcess (), &client_handle, 0, FALSE,
          DUPLICATE_SAME_ACCESS)) {
    guint last_err = GetLastError ();
    auto err = gst_d3d12_ipc_win32_error_to_string (last_err);
    GST_ERROR_OBJECT (self, "Couldn't duplicate handle, 0x%x (%s)",
        last_err, err.c_str ());
    return false;
  }

  GST_LOG_OBJECT (self, "Importing server handle %p", server_handle);

  auto device = gst_d3d12_device_get_device_handle (priv->device);
  hr = device->OpenSharedHandle (client_handle, IID_PPV_ARGS (&texture));
  CloseHandle (client_handle);

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't open resource");
    return false;
  }

  import_data = std::make_shared < GstD3D12IpcImportData > ();
  import_data->client = (GstD3D12IpcClient *) gst_object_ref (self);
  import_data->texture = texture;
  import_data->layout = layout;
  import_data->server_handle = server_handle;

  if (priv->io_mode == GST_D3D12_IPC_IO_COPY) {
    gst_buffer_pool_acquire_buffer (priv->pool, &buffer, nullptr);
    if (!buffer) {
      GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
      return false;
    }

    auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (buffer, 0);
    auto num_planes = gst_d3d12_memory_get_plane_count (dmem);
    auto resource = gst_d3d12_memory_get_resource_handle (dmem);
    std::vector < GstD3D12CopyTextureRegionArgs > copy_args;
    D3D12_BOX src_box[4];

    for (guint i = 0; i < num_planes; i++) {
      GstD3D12CopyTextureRegionArgs args = { };
      D3D12_RECT dst_rect;

      gst_d3d12_memory_get_plane_rectangle (dmem, i, &dst_rect);

      args.src = CD3DX12_TEXTURE_COPY_LOCATION (texture.Get (), i);
      args.dst = CD3DX12_TEXTURE_COPY_LOCATION (resource, i);

      src_box[i].front = 0;
      src_box[i].back = 1;
      src_box[i].left = 0;
      src_box[i].top = 0;
      src_box[i].right = dst_rect.right;
      src_box[i].bottom = dst_rect.bottom;

      args.src_box = &src_box[i];
      copy_args.push_back (args);
    }

    auto queue = gst_d3d12_device_get_command_queue (priv->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto completed = priv->server_fence->GetCompletedValue ();
    if (completed < fence_val) {
      gst_d3d12_command_queue_execute_wait (queue, priv->server_fence.Get (),
          fence_val);
    }

    lk.unlock ();

    guint64 copy_fence_val;
    gst_d3d12_device_copy_texture_region (priv->device, copy_args.size (),
        copy_args.data (), nullptr, 0, nullptr, nullptr,
        D3D12_COMMAND_LIST_TYPE_DIRECT, &copy_fence_val);

    auto data = new GstD3D12IpcReleaseData ();
    data->self = (GstD3D12IpcClient *) gst_object_ref (self);
    data->imported = import_data;

    gst_d3d12_command_queue_set_notify (queue, copy_fence_val, data,
        (GDestroyNotify) gst_d3d12_ipc_client_release_imported_data);

    gst_d3d12_buffer_set_fence (buffer,
        gst_d3d12_device_get_fence_handle (priv->device,
            D3D12_COMMAND_LIST_TYPE_DIRECT), copy_fence_val, FALSE);

    lk.lock ();
  } else {
    gint stride[GST_VIDEO_MAX_PLANES];
    gsize offset[GST_VIDEO_MAX_PLANES];

    for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      stride[i] = import_data->layout.pitch;
      offset[i] = import_data->layout.offset[i];
    }

    auto data = new GstD3D12IpcReleaseData ();
    data->self = (GstD3D12IpcClient *) gst_object_ref (self);
    data->imported = import_data;

    auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, priv->device,
        texture.Get (), 0, data,
        (GDestroyNotify) gst_d3d12_ipc_client_release_imported_data);

    gst_d3d12_memory_set_fence (GST_D3D12_MEMORY_CAST (mem),
        priv->server_fence.Get (), fence_val, FALSE);

    GST_MINI_OBJECT_FLAG_SET (mem, GST_MEMORY_FLAG_READONLY);

    buffer = gst_buffer_new ();
    gst_buffer_append_memory (buffer, mem);

    gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (&priv->info), GST_VIDEO_INFO_WIDTH (&priv->info),
        GST_VIDEO_INFO_HEIGHT (&priv->info),
        GST_VIDEO_INFO_N_PLANES (&priv->info), offset, stride);

    priv->imported.push_back (import_data);
  }

  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;

  sample = gst_sample_new (buffer, priv->caps, nullptr, nullptr);
  gst_buffer_unref (buffer);

  /* Drops too old samples */
  std::queue < GstSample * >drop_queue;
  while (priv->samples.size () > 2) {
    drop_queue.push (priv->samples.front ());
    priv->samples.pop ();
  }

  priv->samples.push (sample);
  priv->cond.notify_all ();
  lk.unlock ();

  import_data = nullptr;
  while (!drop_queue.empty ()) {
    auto old = drop_queue.front ();
    gst_sample_unref (old);
    drop_queue.pop ();
  }

  return true;
}

static void
gst_d3d12_ipc_client_wait_msg_finish (GstD3D12IpcClient * client)
{
  auto priv = client->priv;
  GstD3D12IpcPacketHeader header;
  auto conn = priv->conn;

  if (!gst_d3d12_ipc_pkt_identify (conn->server_msg, header)) {
    GST_ERROR_OBJECT (client, "Broken header");
    gst_d3d12_ipc_client_abort (client);
    return;
  }

  switch (header.type) {
    case GstD3D12IpcPktType::CONFIG:
      GST_LOG_OBJECT (client, "Got CONFIG");
      if (!gst_d3d12_ipc_client_config_data (client)) {
        gst_d3d12_ipc_client_abort (client);
        return;
      }

      gst_d3d12_ipc_client_continue (client);
      break;
    case GstD3D12IpcPktType::HAVE_DATA:
      GST_LOG_OBJECT (client, "Got HAVE-DATA");
      if (!gst_d3d12_ipc_client_have_data (client)) {
        gst_d3d12_ipc_client_abort (client);
        return;
      }

      GST_LOG_OBJECT (client, "Sending READ-DONE");
      gst_d3d12_ipc_pkt_build_read_done (conn->client_msg);
      conn->type = GstD3D12IpcPktType::READ_DONE;
      gst_d3d12_ipc_client_send_msg (client);
      break;
    case GstD3D12IpcPktType::EOS:
      GST_DEBUG_OBJECT (client, "Got EOS");
      priv->server_eos = true;
      priv->lock.lock ();
      priv->cond.notify_all ();
      priv->lock.unlock ();
      gst_d3d12_ipc_client_continue (client);
      break;
    default:
      GST_WARNING_OBJECT (client, "Unexpected packet type");
      gst_d3d12_ipc_client_abort (client);
      break;
  }
}

static void WINAPI
gst_d3d12_ipc_client_payload_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  auto conn = static_cast < GstD3D12IpcClientConn * >(overlap);
  auto self = conn->client;

  if (error_code != ERROR_SUCCESS) {
    auto err = gst_d3d12_ipc_win32_error_to_string (error_code);
    GST_WARNING_OBJECT (self, "ReadFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err.c_str ());
    gst_d3d12_ipc_client_abort (self);
  }

  gst_d3d12_ipc_client_wait_msg_finish (self);
}

static void WINAPI
gst_d3d12_ipc_client_win32_wait_header_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  auto conn = static_cast < GstD3D12IpcClientConn * >(overlap);
  auto self = conn->client;
  GstD3D12IpcPacketHeader header;

  if (error_code != ERROR_SUCCESS) {
    auto err = gst_d3d12_ipc_win32_error_to_string (error_code);
    GST_WARNING_OBJECT (self, "ReadFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err.c_str ());
    gst_d3d12_ipc_client_abort (self);
    return;
  }

  if (!gst_d3d12_ipc_pkt_identify (conn->server_msg, header)) {
    GST_ERROR_OBJECT (self, "Broken header");
    gst_d3d12_ipc_client_abort (self);
    return;
  }

  if (header.payload_size == 0) {
    gst_d3d12_ipc_client_wait_msg_finish (self);
    return;
  }

  GST_LOG_OBJECT (self, "Reading payload");

  if (!ReadFileEx (conn->pipe, &conn->server_msg[0] +
          GST_D3D12_IPC_PKT_HEADER_SIZE, header.payload_size, conn,
          gst_d3d12_ipc_client_payload_finish)) {
    guint last_err = GetLastError ();
    auto err = gst_d3d12_ipc_win32_error_to_string (last_err);
    GST_WARNING_OBJECT (self, "ReadFileEx failed with 0x%x (%s)",
        last_err, err.c_str ());
    gst_d3d12_ipc_client_abort (self);
  }
}

static void
gst_d3d12_ipc_client_wait_msg (GstD3D12IpcClient * self)
{
  auto priv = self->priv;
  auto conn = priv->conn;
  priv->io_pending = true;

  if (!ReadFileEx (conn->pipe, &conn->server_msg[0],
          GST_D3D12_IPC_PKT_HEADER_SIZE, conn.get (),
          gst_d3d12_ipc_client_win32_wait_header_finish)) {
    guint last_err = GetLastError ();
    auto err = gst_d3d12_ipc_win32_error_to_string (last_err);
    GST_WARNING_OBJECT (self, "ReadFileEx failed with 0x%x (%s)",
        last_err, err.c_str ());
    gst_d3d12_ipc_client_abort (self);
  }
}

static void WINAPI
gst_cuda_ipc_client_win32_send_msg_finish (DWORD error_code, DWORD size,
    OVERLAPPED * overlap)
{
  auto conn = static_cast < GstD3D12IpcClientConn * >(overlap);
  auto self = conn->client;

  if (error_code != ERROR_SUCCESS) {
    auto err = gst_d3d12_ipc_win32_error_to_string (error_code);
    GST_WARNING_OBJECT (self, "WriteFileEx callback failed with 0x%x (%s)",
        (guint) error_code, err.c_str ());
    gst_d3d12_ipc_client_abort (self);
    return;
  }

  switch (conn->type) {
    case GstD3D12IpcPktType::NEED_DATA:
      GST_LOG_OBJECT (self, "Sent NEED-DATA");
      gst_d3d12_ipc_client_wait_msg (self);
      break;
    case GstD3D12IpcPktType::READ_DONE:
      GST_LOG_OBJECT (self, "Sent READ-DONE");
      gst_d3d12_ipc_client_continue (self);
      break;
    case GstD3D12IpcPktType::RELEASE_DATA:
      GST_LOG_OBJECT (self, "Sent RELEASE-DATA");
      gst_d3d12_ipc_client_continue (self);
      break;
    case GstD3D12IpcPktType::FIN:
      GST_DEBUG_OBJECT (self, "Sent FIN");
      gst_d3d12_ipc_client_abort (self);
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected msg type");
      gst_d3d12_ipc_client_abort (self);
      break;
  }
}

static void
gst_d3d12_ipc_client_send_msg (GstD3D12IpcClient * self)
{
  auto priv = self->priv;
  auto conn = priv->conn;

  priv->io_pending = true;

  if (!WriteFileEx (conn->pipe, &conn->client_msg[0],
          conn->client_msg.size (), conn.get (),
          gst_cuda_ipc_client_win32_send_msg_finish)) {
    guint last_err = GetLastError ();
    auto err = gst_d3d12_ipc_win32_error_to_string (last_err);
    GST_WARNING_OBJECT (self, "WriteFileEx failed with 0x%x (%s)",
        last_err, err.c_str ());
    gst_d3d12_ipc_client_abort (self);
  }
}

static void
gst_d3d12_ipc_client_run_gc (GstD3D12IpcClient * self)
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
gst_d3d12_ipc_client_continue (GstD3D12IpcClient * self)
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

    gst_d3d12_ipc_pkt_build_release_data (conn->client_msg, server_handle);
    conn->type = GstD3D12IpcPktType::RELEASE_DATA;
    lk.unlock ();

    gst_d3d12_ipc_client_send_msg (self);
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
    gst_d3d12_ipc_client_run_gc (self);

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
        gst_d3d12_ipc_pkt_build_fin (conn->client_msg);
        conn->type = GstD3D12IpcPktType::FIN;

        GST_DEBUG_OBJECT (self, "Sending FIN");
        gst_d3d12_ipc_client_send_msg (self);
        return;
      }
    } else {
      priv->io_pending = false;
    }
    return;
  }

  lk.unlock ();

  gst_d3d12_ipc_pkt_build_need_data (conn->client_msg);
  conn->type = GstD3D12IpcPktType::NEED_DATA;

  GST_LOG_OBJECT (self, "Sending NEED-DATA");
  gst_d3d12_ipc_client_send_msg (self);
}

static gpointer
gst_d3d12_ipc_client_loop_thread_func (GstD3D12IpcClient * self)
{
  auto priv = self->priv;
  DWORD mode = PIPE_READMODE_MESSAGE;
  guint wait_ret;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  GstClockTime start_time = gst_util_get_timestamp ();
  HANDLE waitables[] = { priv->cancellable, priv->wakeup_event };
  std::wstring address = gst_d3d12_ipc_string_to_wstring (priv->address);

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
    pipe = CreateFile2 (address.c_str (), GENERIC_READ | GENERIC_WRITE, 0,
        OPEN_EXISTING, &params);
#else
    pipe = CreateFileW (address.c_str (),
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
    auto err = gst_d3d12_ipc_win32_error_to_string (last_err);
    GST_WARNING_OBJECT (self, "SetNamedPipeHandleState failed with 0x%x (%s)",
        last_err, err.c_str ());

    CloseHandle (pipe);
    priv->aborted = true;
    priv->cond.notify_all ();
    goto out;
  }

  priv->conn = std::make_shared < GstD3D12IpcClientConn > (self, pipe);
  priv->cond.notify_all ();
  lk.unlock ();

  gst_d3d12_ipc_client_wait_msg (self);

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
          gst_d3d12_ipc_client_continue (self);
        break;
      default:
        GST_WARNING ("Unexpected wait return 0x%x", wait_ret);
        gst_d3d12_ipc_client_abort (self);
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

  GST_DEBUG_OBJECT (self, "Exit loop thread");

  return nullptr;
}

GstFlowReturn
gst_d3d12_ipc_client_run (GstD3D12IpcClient * client)
{
  g_return_val_if_fail (GST_IS_D3D12_IPC_CLIENT (client), GST_FLOW_ERROR);

  auto priv = client->priv;
  std::unique_lock < std::mutex > lk (priv->lock);
  if (!priv->loop_thread) {
    priv->loop_thread = g_thread_new ("d3d12-ipc-client",
        (GThreadFunc) gst_d3d12_ipc_client_loop_thread_func, client);

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
gst_d3d12_ipc_client_get_caps (GstD3D12IpcClient * client)
{
  GstCaps *caps = nullptr;

  g_return_val_if_fail (GST_IS_D3D12_IPC_CLIENT (client), nullptr);

  auto priv = client->priv;

  if (gst_d3d12_ipc_client_run (client) != GST_FLOW_OK)
    return nullptr;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->caps)
    caps = gst_caps_ref (priv->caps);

  return caps;
}

static void
gst_d3d12_ipc_client_stop_async (GstD3D12IpcClient * client, gpointer user_data)
{
  auto priv = client->priv;

  GST_DEBUG_OBJECT (client, "Stopping");
  std::unique_lock < std::mutex > lk (priv->lock);
  while (!priv->aborted)
    priv->cond.wait (lk);
  lk.unlock ();

  SetEvent (priv->cancellable);
  g_clear_pointer (&priv->loop_thread, g_thread_join);

  GST_DEBUG_OBJECT (client, "Stopped");

  gst_object_unref (client);
}

static void
gst_d3d12_ipc_client_push_stop_async (GstD3D12IpcClient * client)
{
  std::lock_guard < std::mutex > lk (gc_pool_lock);
  if (!gc_thread_pool) {
    gc_thread_pool = g_thread_pool_new ((GFunc) gst_d3d12_ipc_client_stop_async,
        nullptr, -1, FALSE, nullptr);
  }

  g_thread_pool_push (gc_thread_pool, gst_object_ref (client), nullptr);
}

void
gst_d3d12_ipc_client_stop (GstD3D12IpcClient * client)
{
  g_return_if_fail (GST_IS_D3D12_IPC_CLIENT (client));

  auto priv = client->priv;

  GST_DEBUG_OBJECT (client, "Stopping");
  priv->shutdown = true;
  SetEvent (priv->wakeup_event);

  if (priv->io_mode == GST_D3D12_IPC_IO_COPY) {
    std::unique_lock < std::mutex > lk (priv->lock);
    while (!priv->aborted)
      priv->cond.wait (lk);
    lk.unlock ();

    GST_DEBUG_OBJECT (client, "Terminating");

    SetEvent (priv->cancellable);

    g_clear_pointer (&priv->loop_thread, g_thread_join);

    GST_DEBUG_OBJECT (client, "Stopped");
  } else {
    /* We don't know when imported memory gets released */
    gst_d3d12_ipc_client_push_stop_async (client);
  }
}

void
gst_d3d12_ipc_client_set_flushing (GstD3D12IpcClient * client, bool flushing)
{
  g_return_if_fail (GST_IS_D3D12_IPC_CLIENT (client));

  auto priv = client->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->flushing = flushing;
  priv->cond.notify_all ();
}

GstFlowReturn
gst_d3d12_ipc_client_get_sample (GstD3D12IpcClient * client,
    GstSample ** sample)
{
  g_return_val_if_fail (GST_IS_D3D12_IPC_CLIENT (client), GST_FLOW_ERROR);
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

GstD3D12IpcClient *
gst_d3d12_ipc_client_new (const std::string & address, GstD3D12Device * device,
    GstD3D12IpcIOMode io_mode, guint timeout)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto self = (GstD3D12IpcClient *)
      g_object_new (GST_TYPE_D3D12_IPC_CLIENT, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->address = address;
  priv->timeout = timeout * GST_SECOND;
  priv->io_mode = io_mode;
  priv->device = (GstD3D12Device *) gst_object_ref (device);

  return self;
}
