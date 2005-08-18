/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#include "rtspdefs.h"

const gchar *rtsp_methods[] = {
  "DESCRIBE",
  "ANNOUNCE",
  "GET_PARAMETER",
  "OPTIONS",
  "PAUSE",
  "PLAY",
  "RECORD",
  "REDIRECT",
  "SETUP",
  "SET_PARAMETER",
  "TEARDOWN",
  NULL
};

const gchar *rtsp_headers[] = {
  "Accept",                     /* Accept               R      opt.      entity */
  "Accept-Encoding",            /* Accept-Encoding      R      opt.      entity */
  "Accept-Language",            /* Accept-Language      R      opt.      all */
  "Allow",                      /* Allow                r      opt.      all */
  "Authorization",              /* Authorization        R      opt.      all */
  "Bandwidth",                  /* Bandwidth            R      opt.      all */
  "Blocksize",                  /* Blocksize            R      opt.      all but OPTIONS, TEARDOWN */
  "Cache-Control",              /* Cache-Control        g      opt.      SETUP */
  "Conference",                 /* Conference           R      opt.      SETUP */
  "Connection",                 /* Connection           g      req.      all */
  "Content-Base",               /* Content-Base         e      opt.      entity */
  "Content-Encoding",           /* Content-Encoding     e      req.      SET_PARAMETER, DESCRIBE, ANNOUNCE */
  "Content-Language",           /* Content-Language     e      req.      DESCRIBE, ANNOUNCE */
  "Content-Length",             /* Content-Length       e      req.      SET_PARAMETER, ANNOUNCE, entity */
  "Content-Location",           /* Content-Location     e      opt.      entity */
  "Content-Type",               /* Content-Type         e      req.      SET_PARAMETER, ANNOUNCE, entity */
  "CSeq",                       /* CSeq                 g      req.      all */
  "Date",                       /* Date                 g      opt.      all */
  "Expires",                    /* Expires              e      opt.      DESCRIBE, ANNOUNCE */
  "From",                       /* From                 R      opt.      all */
  "If-Modified-Since",          /* If-Modified-Since    R      opt.      DESCRIBE, SETUP */
  "Last-Modified",              /* Last-Modified        e      opt.      entity */
  "Proxy-Authenticate",         /* Proxy-Authenticate */
  "Proxy-Require",              /* Proxy-Require        R      req.      all */
  "Public",                     /* Public               r      opt.      all */
  "Range",                      /* Range                Rr     opt.      PLAY, PAUSE, RECORD */
  "Referer",                    /* Referer              R      opt.      all */
  "Require",                    /* Require              R      req.      all */
  "Retry-After",                /* Retry-After          r      opt.      all */
  "RTP-Info",                   /* RTP-Info             r      req.      PLAY */
  "Scale",                      /* Scale                Rr     opt.      PLAY, RECORD */
  "Session",                    /* Session              Rr     req.      all but SETUP, OPTIONS */
  "Server",                     /* Server               r      opt.      all */
  "Speed",                      /* Speed                Rr     opt.      PLAY */
  "Transport",                  /* Transport            Rr     req.      SETUP */
  "Unsupported",                /* Unsupported          r      req.      all */
  "User-Agent",                 /* User-Agent           R      opt.      all */
  "Via",                        /* Via                  g      opt.      all */
  "WWW-Authenticate",           /* WWW-Authenticate     r      opt.      all */
  NULL
};

#define DEF_STATUS(c,t)

void
rtsp_init_status (void)
{
  DEF_STATUS (RTSP_STS_CONTINUE, "Continue");
  DEF_STATUS (RTSP_STS_OK, "OK");
  DEF_STATUS (RTSP_STS_CREATED, "Created");
  DEF_STATUS (RTSP_STS_LOW_ON_STORAGE, "Low on Storage Space");
  DEF_STATUS (RTSP_STS_MULTIPLE_CHOICES, "Multiple Choices");
  DEF_STATUS (RTSP_STS_MOVED_PERMANENTLY, "Moved Permanently");
  DEF_STATUS (RTSP_STS_MOVE_TEMPORARILY, "Moved Temporarily");
  DEF_STATUS (RTSP_STS_SEE_OTHER, "See Other");
  DEF_STATUS (RTSP_STS_NOT_MODIFIED, "Not Modified");
  DEF_STATUS (RTSP_STS_USE_PROXY, "Use Proxy");
  DEF_STATUS (RTSP_STS_BAD_REQUEST, "Bad Request");
  DEF_STATUS (RTSP_STS_UNAUTHORIZED, "Unauthorized");
  DEF_STATUS (RTSP_STS_PAYMENT_REQUIRED, "Payment Required");
  DEF_STATUS (RTSP_STS_FORBIDDEN, "Forbidden");
  DEF_STATUS (RTSP_STS_NOT_FOUND, "Not Found");
  DEF_STATUS (RTSP_STS_METHOD_NOT_ALLOWED, "Method Not Allowed");
  DEF_STATUS (RTSP_STS_NOT_ACCEPTABLE, "Not Acceptable");
  DEF_STATUS (RTSP_STS_PROXY_AUTH_REQUIRED, "Proxy Authentication Required");
  DEF_STATUS (RTSP_STS_REQUEST_TIMEOUT, "Request Time-out");
  DEF_STATUS (RTSP_STS_GONE, "Gone");
  DEF_STATUS (RTSP_STS_LENGTH_REQUIRED, "Length Required");
  DEF_STATUS (RTSP_STS_PRECONDITION_FAILED, "Precondition Failed");
  DEF_STATUS (RTSP_STS_REQUEST_ENTITY_TOO_LARGE, "Request Entity Too Large");
  DEF_STATUS (RTSP_STS_REQUEST_URI_TOO_LARGE, "Request-URI Too Large");
  DEF_STATUS (RTSP_STS_UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type");
  DEF_STATUS (RTSP_STS_PARAMETER_NOT_UNDERSTOOD, "Parameter Not Understood");
  DEF_STATUS (RTSP_STS_CONFERENCE_NOT_FOUND, "Conference Not Found");
  DEF_STATUS (RTSP_STS_NOT_ENOUGH_BANDWIDTH, "Not Enough Bandwidth");
  DEF_STATUS (RTSP_STS_SESSION_NOT_FOUND, "Session Not Found");
  DEF_STATUS (RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE,
      "Method Not Valid in This State");
  DEF_STATUS (RTSP_STS_HEADER_FIELD_NOT_VALID_FOR_RESOURCE,
      "Header Field Not Valid for Resource");
  DEF_STATUS (RTSP_STS_INVALID_RANGE, "Invalid Range");
  DEF_STATUS (RTSP_STS_PARAMETER_IS_READONLY, "Parameter Is Read-Only");
  DEF_STATUS (RTSP_STS_AGGREGATE_OPERATION_NOT_ALLOWED,
      "Aggregate operation not allowed");
  DEF_STATUS (RTSP_STS_ONLY_AGGREGATE_OPERATION_ALLOWED,
      "Only aggregate operation allowed");
  DEF_STATUS (RTSP_STS_UNSUPPORTED_TRANSPORT, "Unsupported transport");
  DEF_STATUS (RTSP_STS_DESTINATION_UNREACHABLE, "Destination unreachable");
  DEF_STATUS (RTSP_STS_INTERNAL_SERVER_ERROR, "Internal Server Error");
  DEF_STATUS (RTSP_STS_NOT_IMPLEMENTED, "Not Implemented");
  DEF_STATUS (RTSP_STS_BAD_GATEWAY, "Bad Gateway");
  DEF_STATUS (RTSP_STS_SERVICE_UNAVAILABLE, "Service Unavailable");
  DEF_STATUS (RTSP_STS_GATEWAY_TIMEOUT, "Gateway Time-out");
  DEF_STATUS (RTSP_STS_RTSP_VERSION_NOT_SUPPORTED,
      "RTSP Version not supported");
  DEF_STATUS (RTSP_STS_OPTION_NOT_SUPPORTED, "Option not supported");
}

const gchar *
rtsp_method_as_text (RTSPMethod method)
{
  gint i;

  if (method == 0)
    return NULL;

  i = 0;
  while ((method & 1) == 0) {
    i++;
    method >>= 1;
  }
  return rtsp_methods[i];
}

const gchar *
rtsp_header_as_text (RTSPHeaderField field)
{
  return rtsp_headers[field];
}

const gchar *
rtsp_status_as_text (RTSPStatusCode code)
{
  return NULL;
}

const gchar *
rtsp_status_to_string (RTSPStatusCode code)
{
  return NULL;
}

RTSPHeaderField
rtsp_find_header_field (gchar * header)
{
  gint idx;

  for (idx = 0; rtsp_headers[idx]; idx++) {
    if (g_ascii_strcasecmp (rtsp_headers[idx], header) == 0) {
      return idx;
    }
  }
  return -1;
}

RTSPMethod
rtsp_find_method (gchar * method)
{
  gint idx;

  for (idx = 0; rtsp_methods[idx]; idx++) {
    if (g_ascii_strcasecmp (rtsp_methods[idx], method) == 0) {
      return (1 << idx);
    }
  }
  return -1;
}
