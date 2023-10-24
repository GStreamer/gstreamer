/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include <windows.h>
#include <gst/gst_private.h>
#include <gst/gstconfig.h>
#include <gst/gstutils.h>
#include <gst/gstpluginloader.h>
#include <gst/gstregistrychunks.h>
#include <gst/gstregistrybinary.h>

#include <string.h>
#include <malloc.h>

extern HMODULE _priv_gst_dll_handle;

/* IMPORTANT: Bump the version number if the plugin loader packet protocol
 * changes. Changes in the binary registry format itself are handled by
 * bumping the GST_MAGIC_BINARY_VERSION_STR
 */
static const guint32 loader_protocol_version = 3;

#define GST_CAT_DEFAULT GST_CAT_PLUGIN_LOADING

#define BUF_INIT_SIZE 512
#define BUF_GROW_EXTRA 512
#define BUF_MAX_SIZE (32 * 1024 * 1024)

#define HEADER_SIZE 16
/* 4 magic hex bytes to mark each packet */
#define HEADER_MAGIC 0xbefec0ae
#define ALIGNMENT   (sizeof (void *))

#ifdef _MSC_VER
#define GST_PLUGIN_LOADER_ARCH TARGET_CPU "-msvc"
#else
#define GST_PLUGIN_LOADER_ARCH TARGET_CPU "-mingw"
#endif

#define GST_PLUGIN_LOADER_ARCH_LEN 64

#define GST_PLUGIN_LOADER_VERSION_INFO_SIZE \
  (sizeof (guint32) + GST_MAGIC_BINARY_VERSION_LEN + GST_PLUGIN_LOADER_ARCH_LEN)

static ULONG global_pipe_index = 0;

#define SET_LAST_ERROR_AND_RETURN(l) G_STMT_START { \
  gchar *_err; \
  (l)->last_err = GetLastError(); \
  _err = g_win32_error_message ((l)->last_err); \
  GST_WARNING ("Operation failed with 0x%x (%s)", (l)->last_err, \
      GST_STR_NULL (_err)); \
  g_free (_err); \
  if ((l)->last_err == ERROR_SUCCESS) \
    (l)->last_err = ERROR_OPERATION_ABORTED; \
  SetEvent ((l)->cancellable); \
  return; \
} G_STMT_END

#define SET_ERROR_AND_RETURN(l,e) G_STMT_START { \
  (l)->last_err = e; \
  if (e != ERROR_SUCCESS) { \
    gchar *_err = g_win32_error_message ((l)->last_err); \
    GST_WARNING ("Operation failed with 0x%x (%s)", (l)->last_err, \
        GST_STR_NULL (_err)); \
    g_free (_err); \
  } \
  SetEvent ((l)->cancellable); \
  return; \
} G_STMT_END

static GstPluginLoader *gst_plugin_loader_new (GstRegistry * registry);
static gboolean gst_plugin_loader_free (GstPluginLoader * loader);
static gboolean gst_plugin_loader_load (GstPluginLoader * loader,
    const gchar * filename, off_t file_size, time_t file_mtime);

/* functions used in GstRegistry scanning */
const GstPluginLoaderFuncs _priv_gst_plugin_loader_funcs = {
  gst_plugin_loader_new, gst_plugin_loader_free, gst_plugin_loader_load
};

typedef enum
{
  PACKET_VERSION = (1 << 0),
  PACKET_LOAD_PLUGIN = (1 << 1),
  PACKET_PLUGIN_DETAILS = (1 << 2),
  PACKET_EXIT = (1 << 3),
} PacketType;

typedef struct
{
  guint32 seq_num;
  gchar *filename;
  off_t file_size;
  time_t file_mtime;
} PendingPluginEntry;

static void
pending_plugin_entry_free (PendingPluginEntry * entry)
{
  g_free (entry->filename);
  g_free (entry);
}

typedef struct
{
  guint32 type;
  guint32 seq_num;
  guint32 payload_size;
  guint32 magic;
} PacketHeader;

G_STATIC_ASSERT (sizeof (PacketHeader) == HEADER_SIZE);

/* Base struct both for server and client */
typedef struct
{
  OVERLAPPED overlap;
  HANDLE cancellable;

  gboolean is_client;
  PacketType expected_pkt;

  HANDLE pipe;
  guint last_err;

  PacketHeader rx_header;
  guint8 *rx_buf;
  guint rx_buf_size;

  PacketHeader tx_header;
  guint8 *tx_buf;
  guint tx_buf_size;

  /* loader-protocol-version: 4 bytes
   * binary chunk format: 64 bytes
   * architecture: 64 bytes */
  guint8 version_info[GST_PLUGIN_LOADER_VERSION_INFO_SIZE];
  gboolean apc_called;
} Win32PluginLoader;

struct _GstPluginLoader
{
  Win32PluginLoader parent;

  GstRegistry *registry;
  gchar *pipe_prefix;

  wchar_t *env_string;

  PROCESS_INFORMATION child_info;
  LARGE_INTEGER frequency;

  gboolean got_plugin_detail;
  gboolean client_running;
  guint seq_num;

  GQueue pending_plugins;
};

static void
win32_plugin_loader_init (Win32PluginLoader * self, gboolean is_client)
{
  memset (self, 0, sizeof (Win32PluginLoader));
  self->cancellable = CreateEventA (NULL, TRUE, FALSE, NULL);
  GST_WRITE_UINT32_BE (self->version_info, loader_protocol_version);
  strcpy ((char *) self->version_info + sizeof (guint32),
      GST_MAGIC_BINARY_VERSION_STR);
  strcpy ((char *) self->version_info + sizeof (guint32) +
      GST_MAGIC_BINARY_VERSION_LEN, GST_PLUGIN_LOADER_ARCH);

  self->is_client = is_client;
  self->pipe = INVALID_HANDLE_VALUE;
  self->last_err = ERROR_SUCCESS;

  self->rx_buf_size = self->tx_buf_size = BUF_INIT_SIZE;
  self->rx_buf = _aligned_malloc (BUF_INIT_SIZE, ALIGNMENT);
  self->tx_buf = _aligned_malloc (BUF_INIT_SIZE, ALIGNMENT);
}

static void
win32_plugin_loader_clear (Win32PluginLoader * self)
{
  if (self->pipe != INVALID_HANDLE_VALUE)
    CloseHandle (self->pipe);
  if (self->cancellable)
    CloseHandle (self->cancellable);
  if (self->overlap.hEvent)
    CloseHandle (self->overlap.hEvent);

  _aligned_free (self->tx_buf);
  _aligned_free (self->rx_buf);
}

static gboolean
win32_plugin_loader_resize (Win32PluginLoader * self, gboolean is_tx,
    guint size)
{
  guint new_size;

  if (size > BUF_MAX_SIZE) {
    GST_WARNING ("Too large size %u", size);
    return FALSE;
  }

  if (is_tx) {
    if (self->tx_buf_size <= size) {
      new_size = size + BUF_GROW_EXTRA;
      GST_LOG ("Resizing TX buffer %u -> %u", self->tx_buf_size, new_size);
      self->tx_buf_size = new_size;
      self->tx_buf = _aligned_realloc (self->tx_buf,
          self->tx_buf_size, ALIGNMENT);
    }
  } else {
    if (self->rx_buf_size <= size) {
      new_size = size + BUF_GROW_EXTRA;
      GST_LOG ("Resizing RX buffer %u -> %u", self->rx_buf_size, new_size);
      self->rx_buf_size = new_size;
      self->rx_buf = _aligned_realloc (self->rx_buf,
          self->rx_buf_size, ALIGNMENT);
    }
  }

  return TRUE;
}

static void win32_plugin_loader_write_packet_async (Win32PluginLoader * self,
    guint type, guint seq_num, const guint8 * payload, guint payload_size);
static void win32_plugin_loader_read_header_async (Win32PluginLoader * self);
static gboolean win32_plugin_loader_run (Win32PluginLoader * self,
    DWORD timeout);

static void
gst_plugin_loader_create_blacklist (GstPluginLoader * self,
    PendingPluginEntry * entry)
{
  GstPlugin *plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  plugin->filename = g_strdup (entry->filename);
  plugin->file_mtime = entry->file_mtime;
  plugin->file_size = entry->file_size;
  GST_OBJECT_FLAG_SET (plugin, GST_PLUGIN_FLAG_BLACKLISTED);

  plugin->basename = g_path_get_basename (plugin->filename);
  plugin->desc.name = g_intern_string (plugin->basename);
  plugin->desc.description = "Plugin for blacklisted file";
  plugin->desc.version = "0.0.0";
  plugin->desc.license = "BLACKLIST";
  plugin->desc.source = plugin->desc.license;
  plugin->desc.package = plugin->desc.license;
  plugin->desc.origin = plugin->desc.license;

  GST_DEBUG ("Adding blacklist plugin '%s'", plugin->desc.name);
  gst_registry_add_plugin (self->registry, plugin);
}

static gboolean
gst_plugin_loader_try_helper (GstPluginLoader * self, gchar * location)
{
  Win32PluginLoader *loader = (Win32PluginLoader *) self;
  gchar *cmd = NULL;
  gchar *err = NULL;
  gint last_err;
  gunichar2 *wcmd = NULL;
  STARTUPINFOW si;
  BOOL ret;
  DWORD n_bytes;
  DWORD wait_ret;
  gchar *pipe_name = NULL;
  HANDLE waitables[2];
  LARGE_INTEGER now;
  LONGLONG timeout;

  memset (&si, 0, sizeof (STARTUPINFOW));
  si.cb = sizeof (STARTUPINFOW);

  pipe_name = g_strdup_printf ("%s.%u", self->pipe_prefix,
      (guint) InterlockedIncrement ((LONG *) & global_pipe_index));
  cmd = g_strdup_printf ("%s -l %s %s", location, _gst_executable_path,
      pipe_name);
  wcmd = g_utf8_to_utf16 (cmd, -1, NULL, NULL, NULL);
  if (!wcmd) {
    GST_WARNING ("Couldn't build cmd string");
    goto error;
  }

  loader->pipe = CreateNamedPipeA (pipe_name,
      FILE_FLAG_FIRST_PIPE_INSTANCE | PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      1, BUF_INIT_SIZE, BUF_INIT_SIZE, 5000, NULL);

  if (loader->pipe == INVALID_HANDLE_VALUE) {
    last_err = GetLastError ();
    err = g_win32_error_message (last_err);
    GST_WARNING ("CreateNamedPipeA failed with 0x%x (%s)",
        last_err, GST_STR_NULL (err));
    goto error;
  }

  loader->overlap.Internal = 0;
  loader->overlap.InternalHigh = 0;
  loader->overlap.Offset = 0;
  loader->overlap.OffsetHigh = 0;
  loader->apc_called = FALSE;

  /* Async pipe should return zero */
  if (ConnectNamedPipe (loader->pipe, &loader->overlap)) {
    last_err = GetLastError ();
    err = g_win32_error_message (last_err);
    GST_ERROR ("ConnectNamedPipe failed with 0x%x (%s)",
        last_err, GST_STR_NULL (err));
    goto error;
  }

  /* We didn't create child process yet. So GetLastError should return
   * ERROR_IO_PENDING. Otherwise there's some error or unexpected process is
   * trying to connect to our pipe */
  last_err = GetLastError ();
  if (last_err != ERROR_IO_PENDING) {
    err = g_win32_error_message (last_err);
    GST_ERROR ("ConnectNamedPipe failed with 0x%x (%s)",
        last_err, GST_STR_NULL (err));
    goto error;
  }

  GST_LOG ("Trying to spawn gst-plugin-scanner helper at %s, command %s",
      location, cmd);
  ret = CreateProcessW (NULL, (WCHAR *) wcmd, NULL, NULL, FALSE,
      CREATE_UNICODE_ENVIRONMENT, (LPVOID) self->env_string, NULL, &si,
      &self->child_info);

  if (!ret) {
    last_err = GetLastError ();
    err = g_win32_error_message (last_err);
    GST_ERROR ("Spawning gst-plugin-scanner helper failed with 0x%x (%s)",
        last_err, GST_STR_NULL (err));
    goto error;
  }

  ret = QueryPerformanceCounter (&now);
  g_assert (ret);

  /* 10 seconds timeout */
  timeout = now.QuadPart + 10 * self->frequency.QuadPart;

  /* Wait for client connection */
  waitables[0] = loader->overlap.hEvent;
  waitables[1] = self->child_info.hProcess;
  do {
    wait_ret = WaitForMultipleObjectsEx (2, waitables, FALSE, 5000, TRUE);
    switch (wait_ret) {
      case WAIT_OBJECT_0:
        ret = GetOverlappedResult (loader->pipe,
            &loader->overlap, &n_bytes, FALSE);
        if (!ret) {
          last_err = GetLastError ();
          err = g_win32_error_message (last_err);
          GST_ERROR ("GetOverlappedResult failed with 0x%x (%s)",
              last_err, GST_STR_NULL (err));
          goto kill_child;
        }
        break;
      case WAIT_OBJECT_0 + 1:
        GST_ERROR ("Child process got terminated");
        goto kill_child;
      case WAIT_IO_COMPLETION:
        ret = QueryPerformanceCounter (&now);
        g_assert (ret);

        if (now.QuadPart > timeout) {
          GST_ERROR ("Connection takes too long, give up");
          goto kill_child;
        }

        if (loader->apc_called) {
          GST_WARNING
              ("Unexpected our APC called while waiting for client connection");
        } else {
          GST_DEBUG ("WAIT_IO_COMPLETION, waiting again");
        }
        break;
      case WAIT_TIMEOUT:
        GST_ERROR ("WaitForMultipleObjectsEx timeout");
        goto kill_child;
      default:
        last_err = GetLastError ();
        err = g_win32_error_message (last_err);
        GST_ERROR
            ("Unexpected WaitForMultipleObjectsEx return 0x%x, with 0x%x (%s)",
            (guint) wait_ret, last_err, GST_STR_NULL (err));
        goto kill_child;
    }
  } while (wait_ret == WAIT_IO_COMPLETION);

  /* Do version check */
  loader->expected_pkt = PACKET_VERSION;
  win32_plugin_loader_write_packet_async (loader, PACKET_VERSION, 0, NULL, 0);
  if (!win32_plugin_loader_run (loader, 10000)) {
    GST_ERROR ("Version check failed");
    goto kill_child;
  }

  GST_LOG ("Child pid %u is running now", (guint) self->child_info.dwProcessId);

  self->client_running = TRUE;

  g_free (cmd);
  g_free (wcmd);
  g_free (err);
  g_free (pipe_name);

  return TRUE;

kill_child:
  TerminateProcess (self->child_info.hProcess, 0);
  CloseHandle (self->child_info.hProcess);
  CloseHandle (self->child_info.hThread);
  memset (&self->child_info, 0, sizeof (PROCESS_INFORMATION));

  goto error;

error:
  if (loader->pipe != INVALID_HANDLE_VALUE)
    CloseHandle (loader->pipe);
  loader->pipe = INVALID_HANDLE_VALUE;
  g_free (cmd);
  g_free (wcmd);
  g_free (err);
  g_free (pipe_name);

  return FALSE;
}

static gboolean
gst_plugin_loader_spawn (GstPluginLoader * loader)
{
  const gchar *env;
  char *helper_bin;
  gboolean res = FALSE;

  if (loader->client_running)
    return TRUE;

  /* Find the gst-plugin-scanner */
  env = g_getenv ("GST_PLUGIN_SCANNER_1_0");
  if (!env)
    env = g_getenv ("GST_PLUGIN_SCANNER");

  if (env && *env != '\0') {
    /* use the env-var if it is set */
    GST_LOG ("Trying GST_PLUGIN_SCANNER env var: %s", env);
    helper_bin = g_strdup (env);
    res = gst_plugin_loader_try_helper (loader, helper_bin);
    g_free (helper_bin);
  } else {
    char *relocated_libgstreamer;

    /* use the installed version */
    GST_LOG ("Trying installed plugin scanner");

#define MAX_PATH_DEPTH 64

    relocated_libgstreamer = priv_gst_get_relocated_libgstreamer ();
    if (relocated_libgstreamer) {
      int plugin_subdir_depth = priv_gst_count_directories (GST_PLUGIN_SUBDIR);

      GST_DEBUG ("found libgstreamer-" GST_API_VERSION " library "
          "at %s", relocated_libgstreamer);

      if (plugin_subdir_depth < MAX_PATH_DEPTH) {
        const char *filenamev[MAX_PATH_DEPTH + 5];
        int i = 0, j;

        filenamev[i++] = relocated_libgstreamer;
        for (j = 0; j < plugin_subdir_depth; j++)
          filenamev[i++] = "..";
        filenamev[i++] = GST_PLUGIN_SCANNER_SUBDIR;
        filenamev[i++] = "gstreamer-" GST_API_VERSION;
        filenamev[i++] = "gst-plugin-scanner.exe";
        filenamev[i++] = NULL;
        g_assert (i <= MAX_PATH_DEPTH + 5);

        GST_DEBUG ("constructing path to system plugin scanner using "
            "plugin dir: \'%s\', plugin scanner dir: \'%s\'",
            GST_PLUGIN_SUBDIR, GST_PLUGIN_SCANNER_SUBDIR);

        helper_bin = g_build_filenamev ((char **) filenamev);
      } else {
        GST_WARNING ("GST_PLUGIN_SUBDIR: \'%s\' has too many path segments",
            GST_PLUGIN_SUBDIR);
        helper_bin = g_strdup (GST_PLUGIN_SCANNER_INSTALLED);
      }
    } else {
      helper_bin = g_strdup (GST_PLUGIN_SCANNER_INSTALLED);
    }

#undef MAX_PATH_DEPTH

    GST_DEBUG ("using system plugin scanner at %s", helper_bin);

    res = gst_plugin_loader_try_helper (loader, helper_bin);
    g_free (helper_bin);
    g_free (relocated_libgstreamer);
  }

  if (!res)
    GST_INFO ("No gst-plugin-scanner available, or not working");

  return loader->client_running;
}

static VOID WINAPI
win32_plugin_loader_write_payload_finish (DWORD error_code, DWORD n_bytes,
    LPOVERLAPPED overlapped)
{
  Win32PluginLoader *self = (Win32PluginLoader *) overlapped;
  PacketHeader *header = &self->tx_header;

  self->apc_called = TRUE;

  if (error_code != ERROR_SUCCESS)
    SET_ERROR_AND_RETURN (self, error_code);

  if (n_bytes != header->payload_size) {
    GST_WARNING ("Unexpected sent byte size %u", (guint) n_bytes);
    SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
  }

  GST_LOG ("Payload (%u) sent for type %u", header->payload_size, header->type);
  win32_plugin_loader_read_header_async (self);
}

static VOID WINAPI
win32_plugin_loader_write_header_finish (DWORD error_code, DWORD n_bytes,
    LPOVERLAPPED overlapped)
{
  Win32PluginLoader *self = (Win32PluginLoader *) overlapped;
  PacketHeader *header = &self->tx_header;

  self->apc_called = TRUE;

  if (error_code != ERROR_SUCCESS)
    SET_ERROR_AND_RETURN (self, error_code);

  if (n_bytes != HEADER_SIZE) {
    GST_WARNING ("Unexpected header byte size received %d", (guint) n_bytes);
    SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
  }

  GST_LOG ("Header type %u sent", header->type);
  if (self->tx_header.payload_size) {
    GST_LOG ("Sending payload %u", self->tx_header.payload_size);
    if (!WriteFileEx (self->pipe, self->tx_buf + HEADER_SIZE,
            self->tx_header.payload_size, (OVERLAPPED *) self,
            win32_plugin_loader_write_payload_finish)) {
      SET_LAST_ERROR_AND_RETURN (self);
    }
  } else {
    /* This is our final message */
    if (self->is_client && header->type == PACKET_EXIT)
      SET_ERROR_AND_RETURN (self, ERROR_SUCCESS);

    win32_plugin_loader_read_header_async (self);
  }
}

static void
win32_plugin_loader_write_packet_async (Win32PluginLoader * self, guint type,
    guint seq_num, const guint8 * payload, guint payload_size)
{
  PacketHeader *header = &self->tx_header;

  if (!win32_plugin_loader_resize (self, TRUE, HEADER_SIZE + payload_size))
    SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);

  header->type = type;
  header->seq_num = seq_num;
  header->payload_size = payload_size;
  header->magic = HEADER_MAGIC;

  GST_LOG ("Sending header - type %d, seq_num %d, payload_size %d, magic 0x%x",
      header->type, header->seq_num, header->payload_size, header->magic);

  memcpy (self->tx_buf, &self->tx_header, sizeof (PacketHeader));
  if (payload && payload_size)
    memcpy (self->tx_buf + HEADER_SIZE, payload, payload_size);

  if (!WriteFileEx (self->pipe, self->tx_buf, HEADER_SIZE,
          (OVERLAPPED *) self, win32_plugin_loader_write_header_finish)) {
    SET_LAST_ERROR_AND_RETURN (self);
  }
}

static void
win32_plugin_loader_client_load (Win32PluginLoader * self,
    const gchar * file_name, guint seq_num)
{
  GstPlugin *plugin;
  GList *chunks = NULL;

  GST_DEBUG ("Plugin scanner loading file %s, seq-num %u", file_name, seq_num);

  plugin = gst_plugin_load_file (file_name, NULL);
  if (plugin) {
    GList *iter;
    guint offset;
    guint i;

    GST_LOG ("Plugin %s loaded", file_name);
    if (!_priv_gst_registry_chunks_save_plugin (&chunks, gst_registry_get (),
            plugin)) {
      GST_LOG ("Saving plugin %s failed", file_name);
      gst_object_unref (plugin);
      win32_plugin_loader_write_packet_async (self, PACKET_PLUGIN_DETAILS,
          seq_num, NULL, 0);
      return;
    }

    offset = HEADER_SIZE;

    for (iter = chunks, i = 0; iter; iter = g_list_next (iter), i++) {
      GstRegistryChunk *c = (GstRegistryChunk *) iter->data;
      guint padsize = 0;

      if (c->align && (offset % ALIGNMENT) != 0)
        padsize = ALIGNMENT - (offset % ALIGNMENT);

      GST_LOG ("Plugin %s chunk %d, size %d, offset %d, padding size %d",
          file_name, i, c->size, offset, padsize);

      if (!win32_plugin_loader_resize (self, TRUE, offset + padsize + c->size)) {
        g_list_free_full (chunks,
            (GDestroyNotify) _priv_gst_registry_chunk_free);
        gst_object_unref (plugin);
        SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
      }

      if (padsize)
        memset (self->tx_buf + offset, 0, padsize);

      memcpy (self->tx_buf + offset + padsize, c->data, c->size);
      offset += padsize + c->size;
    }

    if (chunks)
      g_list_free_full (chunks, (GDestroyNotify) _priv_gst_registry_chunk_free);

    gst_object_unref (plugin);

    self->tx_header.type = PACKET_PLUGIN_DETAILS;
    self->tx_header.seq_num = seq_num;
    self->tx_header.payload_size = offset - HEADER_SIZE;
    self->tx_header.magic = HEADER_MAGIC;

    memcpy (self->tx_buf, &self->tx_header, sizeof (PacketHeader));
    if (!WriteFileEx (self->pipe, self->tx_buf, HEADER_SIZE,
            (OVERLAPPED *) self, win32_plugin_loader_write_header_finish)) {
      SET_LAST_ERROR_AND_RETURN (self);
    }
  } else {
    win32_plugin_loader_write_packet_async (self,
        PACKET_PLUGIN_DETAILS, seq_num, NULL, 0);
  }
}

static void
win32_plugin_loader_process_packet (Win32PluginLoader * self)
{
  PacketHeader *header = &self->rx_header;
  gchar *payload = (gchar *) self->rx_buf + HEADER_SIZE;

  GST_LOG ("Processing packet - type %u, seq-num %u, payload-size %u",
      header->type, header->seq_num, header->payload_size);

  if ((header->type & self->expected_pkt) == 0) {
    GST_WARNING ("Unexpected packet type %u", header->type);
    goto error;
  }

  switch (header->type) {
    case PACKET_VERSION:
      if (self->is_client) {
        self->expected_pkt = PACKET_LOAD_PLUGIN | PACKET_EXIT;
        GST_LOG ("Got version packet from server, responding");
        win32_plugin_loader_write_packet_async (self, PACKET_VERSION,
            header->seq_num, self->version_info,
            GST_PLUGIN_LOADER_VERSION_INFO_SIZE);
      } else {
        guint32 client_ver;
        gchar *binary_reg_ver;
        gchar *arch_ver;

        GST_LOG ("Got version packet from client");
        if (header->payload_size < GST_PLUGIN_LOADER_VERSION_INFO_SIZE) {
          GST_WARNING ("Too small size of version pkt");
          goto error;
        }

        client_ver = GST_READ_UINT32_BE (self->rx_buf + HEADER_SIZE);
        if (client_ver != loader_protocol_version) {
          GST_WARNING ("Different protocol version %d (ours %d)",
              client_ver, loader_protocol_version);
          goto error;
        }

        binary_reg_ver = (gchar *)
            (self->rx_buf + HEADER_SIZE + sizeof (guint32));
        if (strncmp (binary_reg_ver, GST_MAGIC_BINARY_VERSION_STR,
                GST_MAGIC_BINARY_VERSION_LEN) != 0) {
          GST_WARNING ("Different binary chunk format");
          goto error;
        }

        arch_ver = binary_reg_ver + GST_MAGIC_BINARY_VERSION_LEN;
        if (strncmp (arch_ver, GST_PLUGIN_LOADER_ARCH,
                GST_PLUGIN_LOADER_ARCH_LEN) != 0) {
          GST_WARNING ("Different architecture");
          goto error;
        }

        GST_LOG ("Version packet handled");
        SET_ERROR_AND_RETURN (self, ERROR_SUCCESS);
      }
      return;
    case PACKET_LOAD_PLUGIN:
      if (self->is_client) {
        win32_plugin_loader_client_load (self, payload, header->seq_num);
      } else {
        /* Something went wrong, server shouldn't receive this pkt */
        SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
      }
      return;
    case PACKET_PLUGIN_DETAILS:
      if (self->is_client) {
        /* Something went wrong, client shouldn't receive this pkt */
        SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
      } else {
        GstPluginLoader *server = (GstPluginLoader *) self;
        PendingPluginEntry *entry = NULL;

        /* remove outdated ones */
        while (!g_queue_is_empty (&server->pending_plugins)) {
          PendingPluginEntry *pending =
              g_queue_peek_head (&server->pending_plugins);
          if (pending->seq_num > header->seq_num) {
            break;
          } else if (pending->seq_num == header->seq_num) {
            entry = pending;
            break;
          } else {
            /* Remove old entry */
            g_queue_pop_head (&server->pending_plugins);
            pending_plugin_entry_free (pending);
          }
        }

        if (header->payload_size > 0) {
          GstPlugin *new_plugin = NULL;
          if (!_priv_gst_registry_chunks_load_plugin (server->registry,
                  &payload, payload + header->payload_size, &new_plugin)) {
            /* Got garbage from the child, so fail and trigger replay of plugins */
            GST_ERROR ("Problems loading plugin details with seqnum %u",
                header->seq_num);
            goto error;
          }

          GST_OBJECT_FLAG_UNSET (new_plugin, GST_PLUGIN_FLAG_CACHED);

          GST_LOG ("Marking plugin %p as registered as %s", new_plugin,
              new_plugin->filename);
          new_plugin->registered = TRUE;
          server->got_plugin_detail = TRUE;
        } else if (entry) {
          gst_plugin_loader_create_blacklist (server, entry);
          server->got_plugin_detail = TRUE;
        }

        /* Done with this entry, pop from pending list */
        if (entry) {
          g_queue_pop_head (&server->pending_plugins);
          pending_plugin_entry_free (entry);
        }

        SET_ERROR_AND_RETURN (self, ERROR_SUCCESS);
      }
      return;
    case PACKET_EXIT:
      if (self->is_client) {
        GST_LOG ("Replying EXIT packet");
        win32_plugin_loader_write_packet_async (self,
            PACKET_EXIT, header->seq_num, NULL, 0);
      } else {
        GST_LOG ("Got EXIT packet from child");
        SET_ERROR_AND_RETURN (self, ERROR_SUCCESS);
      }
      return;
    default:
      break;
  }

  /* Unexpected pkt type */
  GST_WARNING ("Unexpected packet type %d", header->type);

error:
  SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
}

static VOID WINAPI
win32_plugin_loader_read_payload_finish (DWORD error_code, DWORD n_bytes,
    LPOVERLAPPED overlapped)
{
  Win32PluginLoader *self = (Win32PluginLoader *) overlapped;
  PacketHeader *header = &self->rx_header;

  self->apc_called = TRUE;

  if (error_code != ERROR_SUCCESS)
    SET_ERROR_AND_RETURN (self, error_code);

  if (n_bytes != header->payload_size) {
    GST_WARNING ("Unexpected payload size %u", (guint) n_bytes);
    SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
  }

  GST_LOG ("Received payload size %u", header->payload_size);
  win32_plugin_loader_process_packet (self);
}

static VOID WINAPI
win32_plugin_loader_read_header_finish (DWORD error_code, DWORD n_bytes,
    LPOVERLAPPED overlapped)
{
  Win32PluginLoader *self = (Win32PluginLoader *) overlapped;
  PacketHeader *header = &self->rx_header;

  self->apc_called = TRUE;

  if (error_code != ERROR_SUCCESS)
    SET_ERROR_AND_RETURN (self, error_code);

  if (n_bytes != HEADER_SIZE) {
    GST_WARNING ("Unexpected header byte size received %d", (guint) n_bytes);
    goto error;
  }

  /* Validates pkt header */
  memcpy (header, self->rx_buf, sizeof (PacketHeader));
  if (header->payload_size + HEADER_SIZE > BUF_MAX_SIZE) {
    GST_WARNING ("Received excessively large packet");
    goto error;
  }

  GST_LOG ("Received header - type %d, seq-num %d, payload-size %d, magic 0x%x",
      header->type, header->seq_num, header->payload_size, header->magic);

  if (header->magic != HEADER_MAGIC) {
    GST_WARNING ("Invalid packet (bad magic number) received");
    goto error;
  }

  /* Reads remaining payload if any */
  if (header->payload_size > 0) {
    GST_LOG ("Reading payload size %u", header->payload_size);
    if (!win32_plugin_loader_resize (self,
            FALSE, HEADER_SIZE + header->payload_size)) {
      goto error;
    }

    if (!ReadFileEx (self->pipe, self->rx_buf + HEADER_SIZE,
            header->payload_size, (OVERLAPPED *) self,
            win32_plugin_loader_read_payload_finish)) {
      SET_LAST_ERROR_AND_RETURN (self);
    }
  } else {
    /* Or this is header only pkt */
    win32_plugin_loader_process_packet (self);
  }

  return;

error:
  SET_ERROR_AND_RETURN (self, ERROR_BAD_FORMAT);
}

static void
win32_plugin_loader_read_header_async (Win32PluginLoader * self)
{
  if (!ReadFileEx (self->pipe, self->rx_buf, HEADER_SIZE,
          (OVERLAPPED *) self, win32_plugin_loader_read_header_finish)) {
    SET_LAST_ERROR_AND_RETURN (self);
  }

  self->last_err = ERROR_SUCCESS;
}

static gboolean
win32_plugin_loader_run (Win32PluginLoader * self, DWORD timeout_ms)
{
  gboolean ret = FALSE;

  do {
    DWORD wait_ret =
        WaitForSingleObjectEx (self->cancellable, timeout_ms, TRUE);

    switch (wait_ret) {
      case WAIT_OBJECT_0:
        if (self->last_err != ERROR_SUCCESS) {
          GST_DEBUG ("Operation cancelled");
        } else {
          GST_LOG ("Operation finished");
          ret = TRUE;
        }
        goto out;
      case WAIT_IO_COMPLETION:
        /* do nothing */
        break;
      default:
        /* timeout or unexpected wake up */
        GST_WARNING ("Unexpected wait return 0x%x", (guint) wait_ret);
        goto out;
    }
  } while (TRUE);

out:
  CancelIoEx (self->pipe, &self->overlap);
  ResetEvent (self->cancellable);

  return ret;
}

static gboolean
gst_plugin_loader_server_load (GstPluginLoader * self,
    PendingPluginEntry * entry)
{
  Win32PluginLoader *loader = (Win32PluginLoader *) self;

  GST_DEBUG ("Synchronously loading plugin file %s", entry->filename);

  loader->last_err = ERROR_SUCCESS;
  loader->expected_pkt = PACKET_PLUGIN_DETAILS;
  win32_plugin_loader_write_packet_async (loader, PACKET_LOAD_PLUGIN,
      entry->seq_num, (guint8 *) entry->filename, strlen (entry->filename) + 1);
  if (loader->last_err != ERROR_SUCCESS) {
    ResetEvent (loader->cancellable);
    return FALSE;
  }

  return win32_plugin_loader_run (loader, 60000);
}

/* *INDENT-OFF* */
static gboolean
is_path_env_string (wchar_t *str)
{
  if (wcslen (str) <= wcslen (L"PATH="))
    return FALSE;

  /* Env variable is case-insensitive */
  if ((str[0] == L'P' || str[0] == L'p') &&
      (str[1] == L'A' || str[1] == L'a') &&
      (str[2] == L'T' || str[2] == L't') &&
      (str[3] == L'H' || str[3] == L'h') && str[4] == L'=') {
    return TRUE;
  }

  return FALSE;
}
/* *INDENT-ON* */

static GstPluginLoader *
gst_plugin_loader_new (GstRegistry * registry)
{
  GstPluginLoader *self;
  Win32PluginLoader *loader;
  wchar_t *env_str;
  size_t origin_len;
  guint i;
  wchar_t lib_dir[MAX_PATH];
  wchar_t *origin_path = NULL;
  BOOL ret;

  if (!registry)
    return NULL;

  self = g_new0 (GstPluginLoader, 1);
  loader = (Win32PluginLoader *) self;

  win32_plugin_loader_init (loader, FALSE);

  loader->overlap.hEvent = CreateEventA (NULL, TRUE, TRUE, NULL);
  self->pipe_prefix = g_strdup_printf ("\\\\.\\pipe\\gst.plugin.loader.%u",
      (guint) GetCurrentProcessId ());

  g_queue_init (&self->pending_plugins);
  self->registry = gst_object_ref (registry);

  env_str = GetEnvironmentStringsW ();
  /* Count original env string length */
  for (i = 0, origin_len = 0; env_str[origin_len]; i++) {
    if (!origin_path) {
      if (is_path_env_string (&env_str[origin_len]))
        origin_path = _wcsdup (&env_str[origin_len]);
    }

    origin_len += wcslen (&env_str[origin_len]) + 1;
  }

  /* Environment string is terminated with additional L'\0' */
  origin_len++;

  if (GetModuleFileNameW (_priv_gst_dll_handle, lib_dir, MAX_PATH)) {
    wchar_t *new_env_string, *pos;
    size_t new_len;
    size_t lib_dir_len;
    wchar_t *sep = wcsrchr (lib_dir, L'\\');
    if (sep)
      *sep = L'\0';

    lib_dir_len = wcslen (lib_dir);

    /* +1 for L';' seperator */
    new_len = origin_len + lib_dir_len + 1;

    new_env_string = calloc (1, sizeof (wchar_t) * new_len);

    pos = new_env_string;
    /* Copy every env except for PATH */
    for (i = 0, origin_len = 0; env_str[origin_len]; i++) {
      size_t len = wcslen (&env_str[origin_len]);
      if (!is_path_env_string (&env_str[origin_len])) {
        wcscpy (pos, &env_str[origin_len]);
        pos += len + 1;
      }

      origin_len += len + 1;
    }

    /* Then copy PATH env */
    wcscpy (pos, L"PATH=");
    pos += wcslen (L"PATH=");
    wcscpy (pos, lib_dir);
    pos += lib_dir_len;
    *pos = L';';
    if (origin_path)
      wcscpy (pos + 1, origin_path + wcslen (L"PATH="));

    self->env_string = new_env_string;
  }

  free (origin_path);
  FreeEnvironmentStringsW (env_str);

  ret = QueryPerformanceFrequency (&self->frequency);
  /* Must not return zero */
  g_assert (ret);

  return self;
}

static void
gst_plugin_loader_cleanup_child (GstPluginLoader * self)
{
  Win32PluginLoader *loader;
  DWORD ret;

  if (!self->client_running)
    return;

  loader = (Win32PluginLoader *) self;

  if (loader->pipe != INVALID_HANDLE_VALUE) {
    GST_LOG ("Disconnecting pipe");
    DisconnectNamedPipe (loader->pipe);
    CloseHandle (loader->pipe);
    loader->pipe = INVALID_HANDLE_VALUE;
  }

  GST_LOG ("Waiting for child term");
  ret = WaitForSingleObject (self->child_info.hProcess, 1000);
  GST_LOG ("Wait return 0x%x", (guint) ret);

  CloseHandle (self->child_info.hProcess);
  CloseHandle (self->child_info.hThread);
  memset (&self->child_info, 0, sizeof (PROCESS_INFORMATION));

  self->client_running = FALSE;
}

static gboolean
gst_plugin_loader_retry_pending (GstPluginLoader * self)
{
  if (g_queue_is_empty (&self->pending_plugins))
    return TRUE;

  if (!gst_plugin_loader_spawn (self))
    return FALSE;

  while (!g_queue_is_empty (&self->pending_plugins)) {
    PendingPluginEntry *pending = g_queue_peek_head (&self->pending_plugins);

    GST_LOG ("Retrying plugin %s", pending->filename);

    if (!gst_plugin_loader_server_load (self, pending)) {
      GST_ERROR ("Loading plugin %s failed", pending->filename);
      gst_plugin_loader_create_blacklist (self, pending);
      self->got_plugin_detail = TRUE;
      pending_plugin_entry_free (pending);
      g_queue_pop_head (&self->pending_plugins);
      gst_plugin_loader_cleanup_child (self);

      if (!gst_plugin_loader_spawn (self))
        return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_plugin_loader_load (GstPluginLoader * self, const gchar * filename,
    off_t file_size, time_t file_mtime)
{
  PendingPluginEntry *entry;

  GST_LOG ("Loading new plugin");

  if (!self || !filename)
    return FALSE;

  if (!gst_plugin_loader_spawn (self))
    return FALSE;

  /* Send a packet to the child requesting that it load the given file */
  GST_LOG ("Sending file %s to child. tag %u", filename, self->seq_num);

  entry = g_new0 (PendingPluginEntry, 1);
  entry->filename = g_strdup (filename);
  entry->file_size = file_size;
  entry->file_mtime = file_mtime;
  entry->seq_num = self->seq_num++;

  g_queue_push_tail (&self->pending_plugins, entry);
  if (!gst_plugin_loader_server_load (self, entry)) {
    GST_WARNING ("Loading plugin %s failed", filename);
    gst_plugin_loader_cleanup_child (self);

    if (!gst_plugin_loader_retry_pending (self)) {
      gst_plugin_loader_cleanup_child (self);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_plugin_loader_free (GstPluginLoader * self)
{
  gboolean got_plugin_detail;
  Win32PluginLoader *loader;

  GST_LOG ("Freeing %p", self);

  if (!self)
    return FALSE;

  loader = (Win32PluginLoader *) self;

  gst_plugin_loader_retry_pending (self);
  if (self->client_running) {
    loader->expected_pkt = PACKET_EXIT;
    GST_LOG ("Sending EXIT packet to client");

    win32_plugin_loader_write_packet_async (loader, PACKET_EXIT, 0, NULL, 0);
    win32_plugin_loader_run (loader, 10000);
  }

  gst_plugin_loader_cleanup_child (self);

  got_plugin_detail = self->got_plugin_detail;
  win32_plugin_loader_clear (loader);
  g_free (self->pipe_prefix);
  gst_clear_object (&self->registry);
  g_queue_clear_full (&self->pending_plugins,
      (GDestroyNotify) pending_plugin_entry_free);

  free (self->env_string);
  g_free (self);

  return got_plugin_detail;
}

static HANDLE
gst_plugin_loader_client_create_file (LPCWSTR pipe_name)
{
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
  CREATEFILE2_EXTENDED_PARAMETERS params;
  memset (&params, 0, sizeof (CREATEFILE2_EXTENDED_PARAMETERS));
  params.dwSize = sizeof (CREATEFILE2_EXTENDED_PARAMETERS);
  params.dwFileFlags = FILE_FLAG_OVERLAPPED;
  params.dwSecurityQosFlags = SECURITY_IMPERSONATION;

  return CreateFile2 (pipe_name,
      GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, &params);
#else
  return CreateFileW (pipe_name,
      GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED, NULL);
#endif
}

/* child process routine */
gboolean
_gst_plugin_loader_client_run (const gchar * pipe_name)
{
  gboolean ret = FALSE;
  Win32PluginLoader loader;
  DWORD pipe_mode = PIPE_READMODE_MESSAGE;
  gchar *err = NULL;
  LPWSTR pipe_name_wide;

  pipe_name_wide = (LPWSTR) g_utf8_to_utf16 (pipe_name, -1, NULL, NULL, NULL);
  if (!pipe_name_wide) {
    GST_ERROR ("Couldn't convert %s to wide string", pipe_name);
    return FALSE;
  }

  win32_plugin_loader_init (&loader, TRUE);

  GST_DEBUG ("Connecting pipe %s", pipe_name);

  /* Connect to server's named pipe */
  loader.pipe = gst_plugin_loader_client_create_file (pipe_name_wide);
  loader.last_err = GetLastError ();
  if (loader.pipe == INVALID_HANDLE_VALUE) {
    /* Server should be pending (waiting for connection) state already,
     * but do retry if it's not the case */
    if (loader.last_err == ERROR_PIPE_BUSY) {
      if (WaitNamedPipeW (pipe_name_wide, 5000))
        loader.pipe = gst_plugin_loader_client_create_file (pipe_name_wide);

      loader.last_err = GetLastError ();
    }

    if (loader.pipe == INVALID_HANDLE_VALUE) {
      err = g_win32_error_message (loader.last_err);
      GST_ERROR ("CreateFileA failed with 0x%x (%s)",
          loader.last_err, GST_STR_NULL (err));
      goto out;
    }
  }

  /* We use message mode */
  if (!SetNamedPipeHandleState (loader.pipe, &pipe_mode, NULL, NULL)) {
    loader.last_err = GetLastError ();
    err = g_win32_error_message (loader.last_err);
    GST_ERROR ("SetNamedPipeHandleState failed with 0x%x (%s)",
        loader.last_err, err);
    goto out;
  }

  GST_DEBUG ("Plugin scanner child running. Waiting for instructions");
  /* version packet should be the first packet */
  loader.expected_pkt = PACKET_VERSION;

  /* Setup initial read callback */
  win32_plugin_loader_read_header_async (&loader);
  if (loader.last_err != ERROR_SUCCESS)
    goto out;

  ret = win32_plugin_loader_run (&loader, 60000);

out:
  g_free (err);
  g_free (pipe_name_wide);
  win32_plugin_loader_clear (&loader);

  return ret;
}
