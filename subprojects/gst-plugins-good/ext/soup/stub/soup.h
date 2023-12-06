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

#ifndef __GST_SOUP_STUB_H__
#define __GST_SOUP_STUB_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	SOUP_LOGGER_LOG_NONE,
	SOUP_LOGGER_LOG_MINIMAL,
	SOUP_LOGGER_LOG_HEADERS,
	SOUP_LOGGER_LOG_BODY
} SoupLoggerLogLevel;

typedef enum {
  SOUP_MEMORY_STATIC,
  SOUP_MEMORY_TAKE,
  SOUP_MEMORY_COPY,
  SOUP_MEMORY_TEMPORARY
} SoupMemoryUse;

typedef enum {
  SOUP_MESSAGE_NO_REDIRECT = (1 << 1),
  /* Removed from libsoup2. In libsoup3 this enum value is allocated to
     SOUP_MESSAGE_IDEMPOTENT which we don't use in GStreamer. */
  SOUP_MESSAGE_OVERWRITE_CHUNKS = (1 << 3),
} SoupMessageFlags;

typedef enum {
  SOUP_ENCODING_UNRECOGNIZED,
  SOUP_ENCODING_NONE,
  SOUP_ENCODING_CONTENT_LENGTH,
  SOUP_ENCODING_EOF,
  SOUP_ENCODING_CHUNKED,
  SOUP_ENCODING_BYTERANGES
} SoupEncoding;

typedef enum {
  SOUP_STATUS_NONE,

  /* Transport Errors */
  SOUP_STATUS_CANCELLED = 1,
  SOUP_STATUS_CANT_RESOLVE,
  SOUP_STATUS_CANT_RESOLVE_PROXY,
  SOUP_STATUS_CANT_CONNECT,
  SOUP_STATUS_CANT_CONNECT_PROXY,
  SOUP_STATUS_SSL_FAILED,
  SOUP_STATUS_IO_ERROR,
  SOUP_STATUS_MALFORMED,
  SOUP_STATUS_TRY_AGAIN,
  SOUP_STATUS_TOO_MANY_REDIRECTS,
  SOUP_STATUS_TLS_FAILED,

  SOUP_STATUS_CONTINUE = 100,
  SOUP_STATUS_SWITCHING_PROTOCOLS = 101,
  SOUP_STATUS_PROCESSING = 102, /* WebDAV */

  SOUP_STATUS_OK = 200,
  SOUP_STATUS_CREATED = 201,
  SOUP_STATUS_ACCEPTED = 202,
  SOUP_STATUS_NON_AUTHORITATIVE = 203,
  SOUP_STATUS_NO_CONTENT = 204,
  SOUP_STATUS_RESET_CONTENT = 205,
  SOUP_STATUS_PARTIAL_CONTENT = 206,
  SOUP_STATUS_MULTI_STATUS = 207, /* WebDAV */

  SOUP_STATUS_MULTIPLE_CHOICES = 300,
  SOUP_STATUS_MOVED_PERMANENTLY = 301,
  SOUP_STATUS_FOUND = 302,
  SOUP_STATUS_MOVED_TEMPORARILY = 302, /* RFC 2068 */
  SOUP_STATUS_SEE_OTHER = 303,
  SOUP_STATUS_NOT_MODIFIED = 304,
  SOUP_STATUS_USE_PROXY = 305,
  SOUP_STATUS_NOT_APPEARING_IN_THIS_PROTOCOL = 306, /* (reserved) */
  SOUP_STATUS_TEMPORARY_REDIRECT = 307,
  SOUP_STATUS_PERMANENT_REDIRECT = 308,

  SOUP_STATUS_BAD_REQUEST = 400,
  SOUP_STATUS_UNAUTHORIZED = 401,
  SOUP_STATUS_PAYMENT_REQUIRED = 402, /* (reserved) */
  SOUP_STATUS_FORBIDDEN = 403,
  SOUP_STATUS_NOT_FOUND = 404,
  SOUP_STATUS_METHOD_NOT_ALLOWED = 405,
  SOUP_STATUS_NOT_ACCEPTABLE = 406,
  SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED = 407,
  SOUP_STATUS_PROXY_UNAUTHORIZED = SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED,
  SOUP_STATUS_REQUEST_TIMEOUT = 408,
  SOUP_STATUS_CONFLICT = 409,
  SOUP_STATUS_GONE = 410,
  SOUP_STATUS_LENGTH_REQUIRED = 411,
  SOUP_STATUS_PRECONDITION_FAILED = 412,
  SOUP_STATUS_REQUEST_ENTITY_TOO_LARGE = 413,
  SOUP_STATUS_REQUEST_URI_TOO_LONG = 414,
  SOUP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415,
  SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE = 416,
  SOUP_STATUS_INVALID_RANGE = SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE,
  SOUP_STATUS_EXPECTATION_FAILED = 417,
  SOUP_STATUS_MISDIRECTED_REQUEST = 421,  /* HTTP/2 */
  SOUP_STATUS_UNPROCESSABLE_ENTITY = 422, /* WebDAV */
  SOUP_STATUS_LOCKED = 423,               /* WebDAV */
  SOUP_STATUS_FAILED_DEPENDENCY = 424,    /* WebDAV */

  SOUP_STATUS_INTERNAL_SERVER_ERROR = 500,
  SOUP_STATUS_NOT_IMPLEMENTED = 501,
  SOUP_STATUS_BAD_GATEWAY = 502,
  SOUP_STATUS_SERVICE_UNAVAILABLE = 503,
  SOUP_STATUS_GATEWAY_TIMEOUT = 504,
  SOUP_STATUS_HTTP_VERSION_NOT_SUPPORTED = 505,
  SOUP_STATUS_INSUFFICIENT_STORAGE = 507, /* WebDAV search */
  SOUP_STATUS_NOT_EXTENDED = 510          /* RFC 2774 */
} SoupStatus;

#define SOUP_STATUS_IS_SUCCESSFUL(status) ((status) >= 200 && (status) < 300)
#define SOUP_STATUS_IS_REDIRECTION(status) ((status) >= 300 && (status) < 400)
#define SOUP_STATUS_IS_CLIENT_ERROR(status) ((status) >= 400 && (status) < 500)
#define SOUP_STATUS_IS_SERVER_ERROR(status) ((status) >= 500 && (status) < 600)
#define SOUP_STATUS_IS_TRANSPORT_ERROR(status) ((status) > 0 && (status) < 100)

typedef gpointer SoupSession;
typedef gpointer SoupMessage;
typedef gpointer SoupLogger;
typedef gpointer SoupSessionFeature;
typedef gpointer SoupURI;
typedef gpointer SoupMessageBody;
typedef gpointer SoupMessageHeaders;
typedef gpointer SoupAuth;

typedef struct _SoupMessage2 {
  GObject parent;

  /*< public >*/
  const char *method;

  guint status_code;
  char *reason_phrase;

  SoupMessageBody *request_body;
  SoupMessageHeaders *request_headers;

  SoupMessageBody *response_body;
  SoupMessageHeaders *response_headers;
} SoupMessage2;

typedef void (*SoupLoggerPrinter)(SoupLogger *logger,
                                                   SoupLoggerLogLevel level,
                                                   char direction,
                                                   const char *data,
                                                   gpointer user_data);

#if GLIB_CHECK_VERSION(2, 68, 0)
#define SOUP_HTTP_URI_FLAGS                                                    \
  (G_URI_FLAGS_HAS_PASSWORD | G_URI_FLAGS_ENCODED_PATH |                       \
   G_URI_FLAGS_ENCODED_QUERY | G_URI_FLAGS_ENCODED_FRAGMENT |                  \
   G_URI_FLAGS_SCHEME_NORMALIZE)
#else
#define SOUP_HTTP_URI_FLAGS                                                    \
  (G_URI_FLAGS_HAS_PASSWORD | G_URI_FLAGS_ENCODED_PATH |                       \
   G_URI_FLAGS_ENCODED_QUERY | G_URI_FLAGS_ENCODED_FRAGMENT)
#endif

typedef void (*SoupMessageHeadersForeachFunc)(const char *name,
                                              const char *value,
                                              gpointer user_data);

/* Do not use these variables directly; use the macros above, which
 * ensure that they get initialized properly.
 */

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
static gpointer _SOUP_METHOD_OPTIONS;
static gpointer _SOUP_METHOD_GET;
static gpointer _SOUP_METHOD_HEAD;
static gpointer _SOUP_METHOD_POST;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define _SOUP_ATOMIC_INTERN_STRING(variable, value)                            \
  ((const char *)(g_atomic_pointer_get(&(variable))                            \
                      ? (variable)                                             \
                      : (g_atomic_pointer_set(                                 \
                             &(variable),                                      \
                             (gpointer)g_intern_static_string(value)),         \
                         (variable))))

#define _SOUP_INTERN_METHOD(method)                                            \
  (_SOUP_ATOMIC_INTERN_STRING(_SOUP_METHOD_##method, #method))

#define SOUP_METHOD_OPTIONS _SOUP_INTERN_METHOD(OPTIONS)
#define SOUP_METHOD_GET _SOUP_INTERN_METHOD(GET)
#define SOUP_METHOD_HEAD _SOUP_INTERN_METHOD(HEAD)
#define SOUP_METHOD_POST _SOUP_INTERN_METHOD(POST)

typedef struct _SoupCookie SoupCookie;

G_END_DECLS

#endif /* __GST_SOUP_STUB_H__ */
