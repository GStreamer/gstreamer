/*
 * GStreamer gstreamer-onnxinference
 * Copyright (C) 2026 Nirbheek Chauhan <nirbheek@centricular.com>
 *
 * gstonnx-utils.c
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

#include "gstonnx-utils.h"
#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (onnx_inference_debug);
#define GST_CAT_DEFAULT onnx_inference_debug

#ifdef G_OS_WIN32
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Case-insensitive compare, ignoring trailing backslashes */
static int
path_equal (const wchar_t *a, const wchar_t *b)
{
  size_t la = wcslen (a), lb = wcslen (b);
  while (la > 1 && a[la - 1] == L'\\')
    --la;
  while (lb > 1 && b[lb - 1] == L'\\')
    --lb;
  return la == lb && _wcsnicmp (a, b, la) == 0;
}

static wchar_t *
expand_dup (const char *s)
{
  wchar_t *in = (wchar_t *) g_utf8_to_utf16 (s, -1, NULL, NULL, NULL);
  DWORD cap;

  if (!in)
    return NULL;

  cap = ExpandEnvironmentStringsW (in, NULL, 0);
  if (!cap)
    return in;

  for (;;) {
    wchar_t *out = g_new (wchar_t, cap);
    DWORD n = ExpandEnvironmentStringsW (in, out, cap);

    if (!n) {
      g_free (out);
      return in;
    }

    if (n <= cap) {
      g_free (in);
      return out;
    }

    g_free (out);
    cap = n;
  }
}

/* Find onnxruntime.dll in PATH and skip System32 */
static HMODULE
find_onnxrt_from_path (void)
{
  wchar_t sys32[MAX_PATH];
  UINT sl = GetSystemDirectoryW (sys32, G_N_ELEMENTS (sys32));
  HMODULE module = NULL;

  g_assert (sl != 0 && sl < MAX_PATH);
  for (const char *p = g_getenv ("PATH"); p && *p;) {
    char *sep = strchr (p, ';');
    const char *next = sep ? sep + 1 : NULL;
    size_t len = sep ? (size_t) (sep - p) : strlen (p);
    /* Skip empty entries */
    if (!len) {
      p = next;
      continue;
    }

    char *tok = g_strndup (p, len);
    wchar_t *e = expand_dup (tok);
    g_free (tok);

    if (!e || path_equal (e, sys32)) {
      g_free (e);
      p = next;
      continue;
    }

    size_t path_len = wcslen (e);
    wchar_t *onnxrt_library =
        g_new (wchar_t, path_len + G_N_ELEMENTS (L"\\onnxruntime.dll"));
    wcscpy (onnxrt_library, e);
    wcscpy (onnxrt_library + path_len, L"\\onnxruntime.dll");
    g_free (e);

    GST_INFO ("Trying to load ONNX-RT '%S'", onnxrt_library);
    module = LoadLibraryW (onnxrt_library);
    g_free (onnxrt_library);
    if (module)
      break;

    p = next;
  }

  return module;
}
#endif /* G_OS_WIN32 */

#if defined(__linux__) && defined(__GLIBC__)
/*
 * Match versions like: '23.2', '27' etc. These are the minor/micro versions
 * at the end of fully-versioned libonnxruntime libs like:
 * libonnxruntime.so.1.23.2
 * libonnxruntime.so.1.27
 */
static gboolean
get_onnxrt_version (const char *version, guint * minor, guint * micro)
{
  guint a, b = 0;
  char *end = NULL;

  errno = 0;
  a = g_ascii_strtoull (version, &end, 10);
  if (end == version || errno)
    return FALSE;

  if (*end == '\0')
    goto out;
  if (*end != '.')
    return FALSE;

  version = end + 1;
  b = g_ascii_strtoull (version, &end, 10);
  if (end == version || errno || *end != '\0')
    return FALSE;

out:
  *minor = a;
  *micro = b;
  return TRUE;
}

static gpointer
find_versioned_onnxrt (void)
{
  static const char prefix[] = "libonnxruntime.so.1.";
  Dl_serinfo size = { 0, };
  Dl_serinfo *info = NULL;
  gpointer self_handle = NULL;
  gpointer ret = NULL;

  self_handle = dlopen (NULL, RTLD_NOW | RTLD_LOCAL);
  if (!self_handle)
    return NULL;

  if (dlinfo (self_handle, RTLD_DI_SERINFOSIZE, &size) < 0)
    goto done;

  info = g_malloc (size.dls_size);
  info->dls_size = size.dls_size;
  info->dls_cnt = size.dls_cnt;

  if (dlinfo (self_handle, RTLD_DI_SERINFOSIZE, info) < 0 ||
      dlinfo (self_handle, RTLD_DI_SERINFO, info) < 0)
    goto done;

  for (guint i = 0; i < info->dls_cnt; i++) {
    const char *search_path = *info->dls_serpath[i].dls_name ?
        info->dls_serpath[i].dls_name : ".";
    GDir *dir = g_dir_open (search_path, 0, NULL);
    guint best_minor = 0, best_micro = 0;
    const char *name;

    if (!dir)
      continue;

    /* list directory and find the latest libonnxruntime */
    while ((name = g_dir_read_name (dir))) {
      if (!g_str_has_prefix (name, prefix))
        continue;

      const char *version = name + sizeof (prefix) - 1;
      guint minor, micro;
      if (!get_onnxrt_version (version, &minor, &micro))
        continue;

      if (minor > best_minor || (minor == best_minor && micro > best_micro)) {
        best_minor = minor;
        best_micro = micro;
      }
    }
    g_dir_close (dir);

    /* if the library can be loaded, use it */
    if (best_minor > 0) {
      char *path = g_strdup_printf ("%s/%s%u.%u", search_path, prefix,
          best_minor, best_micro);
      GST_INFO ("Trying to load ONNX-RT '%s'", path);
      ret = dlopen (path, GST_DLOPEN_OPTS);
      g_free (path);
      if (ret)
        break;
    }
  }

done:
  g_free (info);
  dlclose (self_handle);
  return ret;
}
#endif /* __linux__ && __GLIBC__ */

gpointer
gst_onnx_find_onnxrt (void)
{
#ifdef G_OS_WIN32
  HMODULE onnxruntime_module = NULL;
#else
  gpointer onnxruntime_module = NULL;
#endif
  const char *onnxrt_path = g_getenv ("GST_ONNXRT_LIBRARY");

#ifdef G_OS_WIN32
  if (onnxrt_path) {
    wchar_t *onnxrt_path_w =
        g_utf8_to_utf16 (onnxrt_path, -1, NULL, NULL, NULL);
    GST_INFO ("Trying to load GST_ONNXRT_LIBRARY='%s'", onnxrt_path);
    onnxruntime_module = LoadLibraryW (onnxrt_path_w);
    g_free (onnxrt_path_w);
  }

  /*
   * On Windows, we have to do a manual search in PATH for an onnxruntime.dll
   * that the user wants us to use, lest we always just pick up the ancient
   * onnxruntime.dll in System32
   */
  if (!onnxruntime_module)
    onnxruntime_module = find_onnxrt_from_path ();
#else
  if (onnxrt_path) {
    GST_INFO ("Trying to load GST_ONNXRT_LIBRARY='%s'", onnxrt_path);
    onnxruntime_module = dlopen (onnxrt_path, GST_DLOPEN_OPTS);
  }
#if defined(__GLIBC__)
  /*
   * On Linux, we might only have the fully-versioned library, so have to hunt
   * for it. We use dlinfo() which is glibc-specific. People using other libcs
   * must use the env var.
   */
  if (!onnxruntime_module)
    onnxruntime_module = find_versioned_onnxrt ();
#endif
#endif

  return onnxruntime_module;
}
