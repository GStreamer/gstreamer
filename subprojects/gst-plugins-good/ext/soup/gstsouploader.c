/* GStreamer
 * Copyright (C) 2021 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsouploader.h"
#include <gmodule.h>

#ifdef HAVE_RTLD_NOLOAD
#include <dlfcn.h>
#endif

#ifdef G_OS_WIN32
#include <windows.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#define GST_WINAPI_ONLY_APP
#endif
#endif /* G_OS_WIN32 */

#ifdef BUILDING_ADAPTIVEDEMUX2
GST_DEBUG_CATEGORY (gst_adaptivedemux_soup_debug);
#define GST_CAT_DEFAULT gst_adaptivedemux_soup_debug
#else
GST_DEBUG_CATEGORY (gst_soup_debug);
#define GST_CAT_DEFAULT gst_soup_debug
#endif


#ifndef STATIC_SOUP

/* G_OS_WIN32 is handled separately below */
#ifdef __APPLE__
#define LIBSOUP_3_SONAME "libsoup-3.0.0.dylib"
#define LIBSOUP_2_SONAME "libsoup-2.4.1.dylib"
#else
#define LIBSOUP_3_SONAME "libsoup-3.0.so.0"
#define LIBSOUP_2_SONAME "libsoup-2.4.so.1"
#endif


#define LOAD_SYMBOL(name) G_STMT_START {                                \
    if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &G_PASTE (vtable->_, name))) { \
      GST_ERROR ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), g_module_name (module), g_module_error()); \
      goto error;                                                       \
    }                                                                   \
  } G_STMT_END;

#define LOAD_VERSIONED_SYMBOL(version, name) G_STMT_START {             \
  if (!g_module_symbol(module, G_STRINGIFY(name), (gpointer *)&G_PASTE(vtable->_, G_PASTE(name, G_PASTE(_, version))))) { \
    GST_WARNING ("Failed to load '%s' from %s, %s", G_STRINGIFY(name),  \
                g_module_name(module), g_module_error());               \
    goto error;                                                         \
  }                                                                     \
  } G_STMT_END;

typedef struct _GstSoupVTable
{
  gboolean loaded;
  guint lib_version;

  /* *INDENT-OFF* */

  /* Symbols present only in libsoup 3 */
#if GLIB_CHECK_VERSION(2, 66, 0)
  GUri *(*_soup_message_get_uri_3)(SoupMessage * msg);
#endif
  SoupLogger *(*_soup_logger_new_3) (SoupLoggerLogLevel level);
  SoupMessageHeaders *(*_soup_message_get_request_headers_3) (SoupMessage * msg);
  SoupMessageHeaders *(*_soup_message_get_response_headers_3) (SoupMessage * msg);
  void (*_soup_message_set_request_body_from_bytes_3) (SoupMessage * msg,
    const char * content_type, GBytes * data);
  const char *(*_soup_message_get_reason_phrase_3) (SoupMessage * msg);
  SoupStatus (*_soup_message_get_status_3) (SoupMessage * msg);

  /* Symbols present only in libsoup 2 */
  SoupLogger *(*_soup_logger_new_2) (SoupLoggerLogLevel, int);
  SoupURI *(*_soup_uri_new_2) (const char *);
  SoupURI *(*_soup_message_get_uri_2) (SoupMessage *);
  char *(*_soup_uri_to_string_2) (SoupURI *, gboolean);
  void (*_soup_message_body_append_2) (SoupMessageBody *, SoupMemoryUse,
    gconstpointer, gsize);
  void (*_soup_uri_free_2) (SoupURI *);
  void (*_soup_session_cancel_message_2) (SoupSession *, SoupMessage *, guint);

  /* Symbols present in libsoup 2 and libsoup 3 */
  GType (*_soup_content_decoder_get_type) (void);
  GType (*_soup_cookie_jar_get_type) (void);
  guint (*_soup_get_major_version) (void);
  guint (*_soup_get_minor_version) (void);
  guint (*_soup_get_micro_version) (void);
  GType (*_soup_logger_log_level_get_type) (void);
  void (*_soup_logger_set_printer) (SoupLogger * logger, SoupLoggerPrinter printer, gpointer user_data,
    GDestroyNotify destroy_notify);
  void (*_soup_message_disable_feature) (SoupMessage * message, GType feature_type);
  void (*_soup_message_headers_append) (SoupMessageHeaders * hdrs, const char * name,
    const char * value);
  void (*_soup_message_headers_foreach) (SoupMessageHeaders * hdrs,
    SoupMessageHeadersForeachFunc callback, gpointer user_data);
  goffset (*_soup_message_headers_get_content_length) (SoupMessageHeaders * hdrs);
  const char *(*_soup_message_headers_get_content_type) (SoupMessageHeaders * hdrs,
    GHashTable ** value);
  gboolean (*_soup_message_headers_get_content_range) (SoupMessageHeaders *hdrs, goffset *start,
    goffset *end, goffset *total_length);
  void (*_soup_message_headers_set_range) (SoupMessageHeaders *hdrs, goffset start, goffset end);
  SoupEncoding (*_soup_message_headers_get_encoding) (SoupMessageHeaders * hdrs);
  const char *(*_soup_message_headers_get_one) (SoupMessageHeaders * hdrs,
    const char * name);
  void (*_soup_message_headers_remove) (SoupMessageHeaders * hdrs, const char * name);
  SoupMessage *(*_soup_message_new) (const char * method, const char * location);
  void (*_soup_message_set_flags) (SoupMessage * msg, SoupMessageFlags flags);
  void (*_soup_session_abort) (SoupSession * session);
  void (*_soup_session_add_feature) (SoupSession * session, SoupSessionFeature * feature);
  void (*_soup_session_add_feature_by_type) (SoupSession * session, GType feature_type);
  GType (*_soup_session_get_type) (void);

  void (*_soup_auth_authenticate) (SoupAuth * auth, const char *username,
    const char *password);
  const char *(*_soup_message_get_method_3) (SoupMessage * msg);
  GInputStream *(*_soup_session_send_async_2) (SoupSession * session, SoupMessage * msg,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer user_data);
  GInputStream *(*_soup_session_send_async_3) (SoupSession * session, SoupMessage * msg,
    int io_priority, GCancellable * cancellable, GAsyncReadyCallback callback, gpointer user_data);
  GInputStream *(*_soup_session_send_finish) (SoupSession * session,
    GAsyncResult * result, GError ** error);
  GInputStream *(*_soup_session_send) (SoupSession * session, SoupMessage * msg,
    GCancellable * cancellable, GError ** error);
  SoupCookie* (*_soup_cookie_parse) (const char* header, GUri* origin);
  void (*_soup_cookies_to_request) (GSList* cookies, SoupMessage* msg);
  void (*_soup_cookies_free) (GSList *cookies);

  /* *INDENT-ON* */
} GstSoupVTable;

static GstSoupVTable gst_soup_vtable = { 0, };

gboolean
gst_soup_load_library (void)
{
  GModule *module;
  GstSoupVTable *vtable;
  const gchar *libsoup_sonames[5] = { 0 };
  guint len = 0;

  if (gst_soup_vtable.loaded)
    return TRUE;

  g_assert (g_module_supported ());

#ifdef BUILDING_ADAPTIVEDEMUX2
  GST_DEBUG_CATEGORY_INIT (gst_adaptivedemux_soup_debug, "adaptivedemux2-soup",
      0, "adaptivedemux2-soup");
#else
  GST_DEBUG_CATEGORY_INIT (gst_soup_debug, "soup", 0, "soup");
#endif

#ifdef HAVE_RTLD_NOLOAD
  {
    gpointer handle = NULL;

    /* In order to avoid causing conflicts we detect if libsoup 2 or 3 is loaded already.
     * If so use that. Otherwise we will try to load our own version to use preferring 3. */

    if ((handle = dlopen (LIBSOUP_3_SONAME, RTLD_NOW | RTLD_NOLOAD))) {
      libsoup_sonames[0] = LIBSOUP_3_SONAME;
      GST_DEBUG ("LibSoup 3 found");
    } else if ((handle = dlopen (LIBSOUP_2_SONAME, RTLD_NOW | RTLD_NOLOAD))) {
      libsoup_sonames[0] = LIBSOUP_2_SONAME;
      GST_DEBUG ("LibSoup 2 found");
    } else {
      GST_DEBUG ("Trying all libsoups");
      libsoup_sonames[0] = LIBSOUP_3_SONAME;
      libsoup_sonames[1] = LIBSOUP_2_SONAME;
    }

    g_clear_pointer (&handle, dlclose);
  }
#else /* !HAVE_RTLD_NOLOAD */

#ifdef G_OS_WIN32

#define LIBSOUP2_MSVC_DLL "soup-2.4-1.dll"
#define LIBSOUP3_MSVC_DLL "soup-3.0-0.dll"
#define LIBSOUP2_MINGW_DLL "libsoup-2.4-1.dll"
#define LIBSOUP3_MINGW_DLL "libsoup-3.0-0.dll"

  {
#ifdef _MSC_VER
    const char *candidates[5] = { LIBSOUP3_MSVC_DLL, LIBSOUP2_MSVC_DLL,
      LIBSOUP3_MINGW_DLL, LIBSOUP2_MINGW_DLL, 0
    };
#else
    const char *candidates[5] = { LIBSOUP3_MINGW_DLL, LIBSOUP2_MINGW_DLL,
      LIBSOUP3_MSVC_DLL, LIBSOUP2_MSVC_DLL, 0
    };
#endif /* _MSC_VER */

    guint len = g_strv_length ((gchar **) candidates);
#if !GST_WINAPI_ONLY_APP
    for (guint i = 0; i < len; i++) {
      HMODULE phModule;
      BOOL loaded =
          GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          candidates[i], &phModule);
      if (loaded) {
        GST_DEBUG ("%s is resident. Using it.", candidates[i]);
        libsoup_sonames[0] = candidates[i];
        break;
      }
    }
#endif
    if (libsoup_sonames[0] == NULL) {
      GST_DEBUG ("No resident libsoup, trying them all");
      for (guint i = 0; i < len; i++) {
        libsoup_sonames[i] = candidates[i];
      }
    }
  }
#else /* !G_OS_WIN32 */
  libsoup_sonames[0] = LIBSOUP_3_SONAME;
  libsoup_sonames[1] = LIBSOUP_2_SONAME;
#endif /* G_OS_WIN32 */

#endif /* HAVE_RTLD_NOLOAD */

  vtable = &gst_soup_vtable;
  len = g_strv_length ((gchar **) libsoup_sonames);

  for (guint i = 0; i < len; i++) {
    module =
        g_module_open (libsoup_sonames[i],
        G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (module) {
      GST_DEBUG ("Loaded %s", g_module_name (module));
      if (g_strstr_len (libsoup_sonames[i], -1, "soup-2")) {
        vtable->lib_version = 2;
        LOAD_VERSIONED_SYMBOL (2, soup_logger_new);
        LOAD_VERSIONED_SYMBOL (2, soup_message_body_append);
        LOAD_VERSIONED_SYMBOL (2, soup_uri_free);
        LOAD_VERSIONED_SYMBOL (2, soup_uri_new);
        LOAD_VERSIONED_SYMBOL (2, soup_uri_to_string);
        LOAD_VERSIONED_SYMBOL (2, soup_message_get_uri);
        LOAD_VERSIONED_SYMBOL (2, soup_session_cancel_message);
        LOAD_VERSIONED_SYMBOL (2, soup_session_send_async);
      } else {
        vtable->lib_version = 3;
        LOAD_VERSIONED_SYMBOL (3, soup_logger_new);
        LOAD_VERSIONED_SYMBOL (3, soup_message_get_request_headers);
        LOAD_VERSIONED_SYMBOL (3, soup_message_get_response_headers);
        LOAD_VERSIONED_SYMBOL (3, soup_message_set_request_body_from_bytes);
#if GLIB_CHECK_VERSION(2, 66, 0)
        LOAD_VERSIONED_SYMBOL (3, soup_message_get_uri);
#endif
        LOAD_VERSIONED_SYMBOL (3, soup_message_get_method);
        LOAD_VERSIONED_SYMBOL (3, soup_message_get_reason_phrase);
        LOAD_VERSIONED_SYMBOL (3, soup_message_get_status);
        LOAD_VERSIONED_SYMBOL (3, soup_session_send_async);
      }

      LOAD_SYMBOL (soup_auth_authenticate);
      LOAD_SYMBOL (soup_content_decoder_get_type);
      LOAD_SYMBOL (soup_cookie_jar_get_type);
      LOAD_SYMBOL (soup_get_major_version);
      LOAD_SYMBOL (soup_get_micro_version);
      LOAD_SYMBOL (soup_get_minor_version);
      LOAD_SYMBOL (soup_logger_log_level_get_type);
      LOAD_SYMBOL (soup_logger_set_printer);
      LOAD_SYMBOL (soup_message_disable_feature);
      LOAD_SYMBOL (soup_message_headers_append);
      LOAD_SYMBOL (soup_message_headers_foreach);
      LOAD_SYMBOL (soup_message_headers_get_content_length);
      LOAD_SYMBOL (soup_message_headers_get_content_type);
      LOAD_SYMBOL (soup_message_headers_get_content_range);
      LOAD_SYMBOL (soup_message_headers_set_range);
      LOAD_SYMBOL (soup_message_headers_get_encoding);
      LOAD_SYMBOL (soup_message_headers_get_one);
      LOAD_SYMBOL (soup_message_headers_remove);
      LOAD_SYMBOL (soup_message_new);
      LOAD_SYMBOL (soup_message_set_flags);
      LOAD_SYMBOL (soup_session_abort);
      LOAD_SYMBOL (soup_session_add_feature);
      LOAD_SYMBOL (soup_session_add_feature_by_type);
      LOAD_SYMBOL (soup_session_get_type);
      LOAD_SYMBOL (soup_session_send);
      LOAD_SYMBOL (soup_session_send_finish);
      LOAD_SYMBOL (soup_cookie_parse);
      LOAD_SYMBOL (soup_cookies_to_request);
      LOAD_SYMBOL (soup_cookies_free);

      vtable->loaded = TRUE;
      goto beach;

    error:
      GST_DEBUG ("Failed to find all libsoup symbols");
      g_clear_pointer (&module, g_module_close);
      continue;
    } else {
      GST_DEBUG ("Module %s not found", libsoup_sonames[i]);
      continue;
    }
  beach:
    break;
  }

  return vtable->loaded;
}

#endif /* !STATIC_SOUP */

guint
gst_soup_loader_get_api_version (void)
{
#ifdef STATIC_SOUP
  return STATIC_SOUP;
#else
  return gst_soup_vtable.lib_version;
#endif
}

SoupSession *
_soup_session_new_with_options (const char *optname1, ...)
{
  SoupSession *session;
  va_list ap;

  va_start (ap, optname1);
  session =
      (SoupSession *) g_object_new_valist (_soup_session_get_type (), optname1,
      ap);
  va_end (ap);
  return session;
}

SoupLogger *
_soup_logger_new (SoupLoggerLogLevel level)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  return soup_logger_new (level, -1);
#elif STATIC_SOUP == 3
  return soup_logger_new (level);
#endif
#else
  if (gst_soup_vtable.lib_version == 2) {
    g_assert (gst_soup_vtable._soup_logger_new_2 != NULL);
    return gst_soup_vtable._soup_logger_new_2 (level, -1);
  }
  g_assert (gst_soup_vtable._soup_logger_new_3 != NULL);
  return gst_soup_vtable._soup_logger_new_3 (level);
#endif
}

void
_soup_logger_set_printer (SoupLogger * logger, SoupLoggerPrinter printer,
    gpointer printer_data, GDestroyNotify destroy)
{
#ifdef STATIC_SOUP
  soup_logger_set_printer (logger, printer, printer_data, destroy);
#else
  g_assert (gst_soup_vtable._soup_logger_set_printer != NULL);
  gst_soup_vtable._soup_logger_set_printer (logger, printer, printer_data,
      destroy);
#endif
}

void
_soup_session_add_feature (SoupSession * session, SoupSessionFeature * feature)
{
#ifdef STATIC_SOUP
  soup_session_add_feature (session, feature);
#else
  g_assert (gst_soup_vtable._soup_session_add_feature != NULL);
  gst_soup_vtable._soup_session_add_feature (session, feature);
#endif
}

GstSoupUri *
gst_soup_uri_new (const char *uri_string)
{
  GstSoupUri *uri = g_new0 (GstSoupUri, 1);
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  uri->soup_uri = soup_uri_new (uri_string);
#else
  uri->uri = g_uri_parse (uri_string, SOUP_HTTP_URI_FLAGS, NULL);
#endif
#else
  if (gst_soup_vtable.lib_version == 2) {
    g_assert (gst_soup_vtable._soup_uri_new_2 != NULL);
    uri->soup_uri = gst_soup_vtable._soup_uri_new_2 (uri_string);
  } else {
#if GLIB_CHECK_VERSION(2, 66, 0)
    uri->uri = g_uri_parse (uri_string, SOUP_HTTP_URI_FLAGS, NULL);
#endif
  }
#endif
  return uri;
}

void
gst_soup_uri_free (GstSoupUri * uri)
{
#if (defined(STATIC_SOUP) && STATIC_SOUP == 3) || (!defined(STATIC_SOUP) && GLIB_CHECK_VERSION(2, 66, 0))
  if (uri->uri) {
    g_uri_unref (uri->uri);
  }
#endif

#if defined(STATIC_SOUP)
#if STATIC_SOUP == 2
  if (uri->soup_uri) {
    soup_uri_free (uri->soup_uri);
  }
#endif
#else /* !STATIC_SOUP */
  if (uri->soup_uri) {
    g_assert (gst_soup_vtable._soup_uri_free_2 != NULL);
    gst_soup_vtable._soup_uri_free_2 (uri->soup_uri);
  }
#endif /* STATIC_SOUP */
  g_free (uri);
}

char *
gst_soup_uri_to_string (GstSoupUri * uri)
{
#if (defined(STATIC_SOUP) && STATIC_SOUP == 3) || (!defined(STATIC_SOUP) && GLIB_CHECK_VERSION(2, 66, 0))
  if (uri->uri) {
    return g_uri_to_string_partial (uri->uri, G_URI_HIDE_PASSWORD);
  }
#endif

#if defined(STATIC_SOUP)
#if STATIC_SOUP == 2
  if (uri->soup_uri) {
    return soup_uri_to_string (uri->soup_uri, FALSE);
  }
#endif
#else /* !STATIC_SOUP */
  if (uri->soup_uri) {
    g_assert (gst_soup_vtable._soup_uri_to_string_2 != NULL);
    return gst_soup_vtable._soup_uri_to_string_2 (uri->soup_uri, FALSE);
  }
#endif /* STATIC_SOUP */

  g_assert_not_reached ();
  return NULL;
}

char *
gst_soup_message_uri_to_string (SoupMessage * msg)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  SoupURI *uri = NULL;
  uri = soup_message_get_uri (msg);
  return soup_uri_to_string (uri, FALSE);
#elif STATIC_SOUP == 3
  GUri *uri = NULL;
  uri = soup_message_get_uri (msg);
  return g_uri_to_string_partial (uri, G_URI_HIDE_PASSWORD);
#endif
#else
  if (gst_soup_vtable.lib_version == 2) {
    SoupURI *uri = NULL;
    g_assert (gst_soup_vtable._soup_message_get_uri_2 != NULL);
    uri = gst_soup_vtable._soup_message_get_uri_2 (msg);
    return gst_soup_vtable._soup_uri_to_string_2 (uri, FALSE);
  } else {
#if GLIB_CHECK_VERSION(2, 66, 0)
    GUri *uri = NULL;
    g_assert (gst_soup_vtable._soup_message_get_uri_3 != NULL);
    uri = gst_soup_vtable._soup_message_get_uri_3 (msg);
    return g_uri_to_string_partial (uri, G_URI_HIDE_PASSWORD);
#endif
  }
#endif
  /*
   * If we reach this, it means the plugin was built for old glib, but somehow
   * we managed to load libsoup3, which requires a very recent glib. As this
   * is a contradiction, we can assert, I guess?
   */
  g_assert_not_reached ();
  return NULL;
}

guint
_soup_get_major_version (void)
{
#ifdef STATIC_SOUP
  return soup_get_major_version ();
#else
  g_assert (gst_soup_vtable._soup_get_major_version != NULL);
  return gst_soup_vtable._soup_get_major_version ();
#endif
}

guint
_soup_get_minor_version (void)
{
#ifdef STATIC_SOUP
  return soup_get_minor_version ();
#else
  g_assert (gst_soup_vtable._soup_get_minor_version != NULL);
  return gst_soup_vtable._soup_get_minor_version ();
#endif
}

guint
_soup_get_micro_version (void)
{
#ifdef STATIC_SOUP
  return soup_get_micro_version ();
#else
  g_assert (gst_soup_vtable._soup_get_micro_version != NULL);
  return gst_soup_vtable._soup_get_micro_version ();
#endif
}

void
_soup_message_set_request_body_from_bytes (SoupMessage * msg,
    const char *content_type, GBytes * bytes)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  gsize size;
  gconstpointer data = g_bytes_get_data (bytes, &size);
  soup_message_body_append (msg->request_body, SOUP_MEMORY_COPY, data, size);
#elif STATIC_SOUP == 3
  soup_message_set_request_body_from_bytes (msg, content_type, bytes);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_assert (gst_soup_vtable._soup_message_set_request_body_from_bytes_3 !=
        NULL);
    gst_soup_vtable._soup_message_set_request_body_from_bytes_3 (msg,
        content_type, bytes);
  } else {
    gsize size;
    gconstpointer data = g_bytes_get_data (bytes, &size);
    SoupMessage2 *msg2 = (SoupMessage2 *) msg;
    g_assert (gst_soup_vtable._soup_message_body_append_2 != NULL);
    gst_soup_vtable._soup_message_body_append_2 (msg2->request_body,
        SOUP_MEMORY_COPY, data, size);
  }
#endif
}

GType
_soup_session_get_type (void)
{
#ifdef STATIC_SOUP
  return soup_session_get_type ();
#else
  g_assert (gst_soup_vtable._soup_session_get_type != NULL);
  return gst_soup_vtable._soup_session_get_type ();
#endif
}

GType
_soup_logger_log_level_get_type (void)
{
#ifdef STATIC_SOUP
  return soup_logger_log_level_get_type ();
#else
  g_assert (gst_soup_vtable._soup_logger_log_level_get_type != NULL);
  return gst_soup_vtable._soup_logger_log_level_get_type ();
#endif
}

GType
_soup_content_decoder_get_type (void)
{
#ifdef STATIC_SOUP
  return soup_content_decoder_get_type ();
#else
  g_assert (gst_soup_vtable._soup_content_decoder_get_type != NULL);
  return gst_soup_vtable._soup_content_decoder_get_type ();
#endif
}

GType
_soup_cookie_jar_get_type (void)
{
#ifdef STATIC_SOUP
  return soup_cookie_jar_get_type ();
#else
  g_assert (gst_soup_vtable._soup_cookie_jar_get_type != NULL);
  return gst_soup_vtable._soup_cookie_jar_get_type ();
#endif
}

void
_soup_session_abort (SoupSession * session)
{
#ifdef STATIC_SOUP
  soup_session_abort (session);
#else
  g_assert (gst_soup_vtable._soup_session_abort != NULL);
  gst_soup_vtable._soup_session_abort (session);
#endif
}

SoupMessage *
_soup_message_new (const char *method, const char *uri_string)
{
#ifdef STATIC_SOUP
  return soup_message_new (method, uri_string);
#else
  g_assert (gst_soup_vtable._soup_message_new != NULL);
  return gst_soup_vtable._soup_message_new (method, uri_string);
#endif
}

SoupMessageHeaders *
_soup_message_get_request_headers (SoupMessage * msg)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  return msg->request_headers;
#elif STATIC_SOUP == 3
  return soup_message_get_request_headers (msg);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_assert (gst_soup_vtable._soup_message_get_request_headers_3 != NULL);
    return gst_soup_vtable._soup_message_get_request_headers_3 (msg);
  } else {
    SoupMessage2 *msg2 = (SoupMessage2 *) msg;
    return msg2->request_headers;
  }
#endif
}

SoupMessageHeaders *
_soup_message_get_response_headers (SoupMessage * msg)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  return msg->response_headers;
#elif STATIC_SOUP == 3
  return soup_message_get_response_headers (msg);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_assert (gst_soup_vtable._soup_message_get_response_headers_3 != NULL);
    return gst_soup_vtable._soup_message_get_response_headers_3 (msg);
  } else {
    SoupMessage2 *msg2 = (SoupMessage2 *) msg;
    return msg2->response_headers;
  }
#endif
}

void
_soup_message_headers_remove (SoupMessageHeaders * hdrs, const char *name)
{
#ifdef STATIC_SOUP
  soup_message_headers_remove (hdrs, name);
#else
  g_assert (gst_soup_vtable._soup_message_headers_remove != NULL);
  gst_soup_vtable._soup_message_headers_remove (hdrs, name);
#endif
}

void
_soup_message_headers_append (SoupMessageHeaders * hdrs, const char *name,
    const char *value)
{
#ifdef STATIC_SOUP
  soup_message_headers_append (hdrs, name, value);
#else
  g_assert (gst_soup_vtable._soup_message_headers_append != NULL);
  gst_soup_vtable._soup_message_headers_append (hdrs, name, value);
#endif
}

void
_soup_message_set_flags (SoupMessage * msg, SoupMessageFlags flags)
{
#ifdef STATIC_SOUP
  soup_message_set_flags (msg, flags);
#else
  g_assert (gst_soup_vtable._soup_message_set_flags != NULL);
  gst_soup_vtable._soup_message_set_flags (msg, flags);
#endif
}

void
_soup_session_add_feature_by_type (SoupSession * session, GType feature_type)
{
#ifdef STATIC_SOUP
  soup_session_add_feature_by_type (session, feature_type);
#else
  g_assert (gst_soup_vtable._soup_session_add_feature_by_type != NULL);
  gst_soup_vtable._soup_session_add_feature_by_type (session, feature_type);
#endif
}

void
_soup_message_headers_foreach (SoupMessageHeaders * hdrs,
    SoupMessageHeadersForeachFunc func, gpointer user_data)
{
#ifdef STATIC_SOUP
  soup_message_headers_foreach (hdrs, func, user_data);
#else
  g_assert (gst_soup_vtable._soup_message_headers_foreach != NULL);
  gst_soup_vtable._soup_message_headers_foreach (hdrs, func, user_data);
#endif
}

SoupEncoding
_soup_message_headers_get_encoding (SoupMessageHeaders * hdrs)
{
#ifdef STATIC_SOUP
  return soup_message_headers_get_encoding (hdrs);
#else
  g_assert (gst_soup_vtable._soup_message_headers_get_encoding != NULL);
  return gst_soup_vtable._soup_message_headers_get_encoding (hdrs);
#endif
}

goffset
_soup_message_headers_get_content_length (SoupMessageHeaders * hdrs)
{
#ifdef STATIC_SOUP
  return soup_message_headers_get_content_length (hdrs);
#else
  g_assert (gst_soup_vtable._soup_message_headers_get_content_length != NULL);
  return gst_soup_vtable._soup_message_headers_get_content_length (hdrs);
#endif
}

SoupStatus
_soup_message_get_status (SoupMessage * msg)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  return msg->status_code;
#elif STATIC_SOUP == 3
  return soup_message_get_status (msg);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_assert (gst_soup_vtable._soup_message_get_status_3 != NULL);
    return gst_soup_vtable._soup_message_get_status_3 (msg);
  } else {
    SoupMessage2 *msg2 = (SoupMessage2 *) msg;
    return msg2->status_code;
  }
#endif
}

const char *
_soup_message_get_reason_phrase (SoupMessage * msg)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  return msg->reason_phrase;
#elif STATIC_SOUP == 3
  return soup_message_get_reason_phrase (msg);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_assert (gst_soup_vtable._soup_message_get_reason_phrase_3 != NULL);
    return gst_soup_vtable._soup_message_get_reason_phrase_3 (msg);
  } else {
    SoupMessage2 *msg2 = (SoupMessage2 *) msg;
    return msg2->reason_phrase;
  }
#endif
}

const char *
_soup_message_headers_get_one (SoupMessageHeaders * hdrs, const char *name)
{
#ifdef STATIC_SOUP
  return soup_message_headers_get_one (hdrs, name);
#else
  g_assert (gst_soup_vtable._soup_message_headers_get_one != NULL);
  return gst_soup_vtable._soup_message_headers_get_one (hdrs, name);
#endif
}

void
_soup_message_disable_feature (SoupMessage * msg, GType feature_type)
{
#ifdef STATIC_SOUP
  soup_message_disable_feature (msg, feature_type);
#else
  g_assert (gst_soup_vtable._soup_message_disable_feature != NULL);
  gst_soup_vtable._soup_message_disable_feature (msg, feature_type);
#endif
}

const char *
_soup_message_headers_get_content_type (SoupMessageHeaders * hdrs,
    GHashTable ** params)
{
#ifdef STATIC_SOUP
  return soup_message_headers_get_content_type (hdrs, params);
#else
  g_assert (gst_soup_vtable._soup_message_headers_get_content_type != NULL);
  return gst_soup_vtable._soup_message_headers_get_content_type (hdrs, params);
#endif
}

gboolean
_soup_message_headers_get_content_range (SoupMessageHeaders * hdrs,
    goffset * start, goffset * end, goffset * total_length)
{
#ifdef STATIC_SOUP
  return soup_message_headers_get_content_range (hdrs, start, end,
      total_length);
#else
  g_assert (gst_soup_vtable._soup_message_headers_get_content_range != NULL);
  return gst_soup_vtable._soup_message_headers_get_content_range (hdrs, start,
      end, total_length);
#endif
}

void
_soup_message_headers_set_range (SoupMessageHeaders * hdrs, goffset start,
    goffset end)
{
#ifdef STATIC_SOUP
  soup_message_headers_set_range (hdrs, start, end);
#else
  g_assert (gst_soup_vtable._soup_message_headers_set_range != NULL);
  gst_soup_vtable._soup_message_headers_set_range (hdrs, start, end);
#endif
}

void
_soup_auth_authenticate (SoupAuth * auth, const char *username,
    const char *password)
{
#ifdef STATIC_SOUP
  soup_auth_authenticate (auth, username, password);
#else
  g_assert (gst_soup_vtable._soup_auth_authenticate != NULL);
  gst_soup_vtable._soup_auth_authenticate (auth, username, password);
#endif
}

const char *
_soup_message_get_method (SoupMessage * msg)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  return msg->method;
#elif STATIC_SOUP == 3
  return soup_message_get_method (msg);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_assert (gst_soup_vtable._soup_message_get_method_3 != NULL);
    return gst_soup_vtable._soup_message_get_method_3 (msg);
  } else {
    SoupMessage2 *msg2 = (SoupMessage2 *) msg;
    return msg2->method;
  }
#endif
}

void
_soup_session_send_async (SoupSession * session, SoupMessage * msg,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 2
  soup_session_send_async (session, msg, cancellable, callback, user_data);
#else
  soup_session_send_async (session, msg, G_PRIORITY_DEFAULT, cancellable,
      callback, user_data);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_assert (gst_soup_vtable._soup_session_send_async_3 != NULL);
    gst_soup_vtable._soup_session_send_async_3 (session, msg,
        G_PRIORITY_DEFAULT, cancellable, callback, user_data);
  } else {
    g_assert (gst_soup_vtable._soup_session_send_async_2 != NULL);
    gst_soup_vtable._soup_session_send_async_2 (session, msg,
        cancellable, callback, user_data);
  }
#endif
}

GInputStream *
_soup_session_send_finish (SoupSession * session,
    GAsyncResult * result, GError ** error)
{
#ifdef STATIC_SOUP
  return soup_session_send_finish (session, result, error);
#else
  g_assert (gst_soup_vtable._soup_session_send_finish != NULL);
  return gst_soup_vtable._soup_session_send_finish (session, result, error);
#endif
}

GInputStream *
_soup_session_send (SoupSession * session, SoupMessage * msg,
    GCancellable * cancellable, GError ** error)
{
#ifdef STATIC_SOUP
  return soup_session_send (session, msg, cancellable, error);
#else
  g_assert (gst_soup_vtable._soup_session_send != NULL);
  return gst_soup_vtable._soup_session_send (session, msg, cancellable, error);
#endif
}

void
gst_soup_session_cancel_message (SoupSession * session, SoupMessage * msg,
    GCancellable * cancellable)
{
#ifdef STATIC_SOUP
#if STATIC_SOUP == 3
  g_cancellable_cancel (cancellable);
#else
  soup_session_cancel_message (session, msg, SOUP_STATUS_CANCELLED);
#endif
#else
  if (gst_soup_vtable.lib_version == 3) {
    g_cancellable_cancel (cancellable);
  } else {
    g_assert (gst_soup_vtable._soup_session_cancel_message_2 != NULL);
    gst_soup_vtable._soup_session_cancel_message_2 (session, msg,
        SOUP_STATUS_CANCELLED);
  }
#endif
}

SoupCookie *
_soup_cookie_parse (const char *header)
{
#ifdef STATIC_SOUP
  return soup_cookie_parse (header, NULL);
#else
  g_assert (gst_soup_vtable._soup_cookie_parse != NULL);
  return gst_soup_vtable._soup_cookie_parse (header, NULL);
#endif
}


void
_soup_cookies_to_request (GSList * cookies, SoupMessage * msg)
{
#ifdef STATIC_SOUP
  soup_cookies_to_request (cookies, msg);
#else
  g_assert (gst_soup_vtable._soup_cookies_to_request != NULL);
  gst_soup_vtable._soup_cookies_to_request (cookies, msg);
#endif
}

void
_soup_cookies_free (GSList * cookies)
{
#ifdef STATIC_SOUP
  soup_cookies_free (cookies);
#else
  g_assert (gst_soup_vtable._soup_cookies_free != NULL);
  gst_soup_vtable._soup_cookies_free (cookies);
#endif
}
