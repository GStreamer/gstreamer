/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __RTSP_DEFS_H__
#define __RTSP_DEFS_H__

#include <glib.h>

G_BEGIN_DECLS

#define RTSP_CHECK(stmt, label)  \
G_STMT_START { \
  if (G_UNLIKELY ((res = (stmt)) != RTSP_OK)) \
    goto label; \
} G_STMT_END

typedef enum {
  RTSP_OK          =  0,
  /* errors */
  RTSP_ERROR       = -1,
  RTSP_EINVAL      = -2,
  RTSP_EINTR       = -3,
  RTSP_ENOMEM      = -4,
  RTSP_ERESOLV     = -5,
  RTSP_ENOTIMPL    = -6,
  RTSP_ESYS        = -7,
  RTSP_EPARSE      = -8,
  RTSP_EWSASTART   = -9,
  RTSP_EWSAVERSION = -10,
  RTSP_EEOF        = -11,
  RTSP_ENET        = -12,
  RTSP_ENOTIP      = -13,
  RTSP_ETIMEOUT    = -14,

  RTSP_ELAST       = -15,
} RTSPResult;

typedef enum {
  RTSP_FAM_NONE,
  RTSP_FAM_INET,
  RTSP_FAM_INET6,
} RTSPFamily;

typedef enum {
  RTSP_STATE_INVALID,
  RTSP_STATE_INIT,
  RTSP_STATE_READY,
  RTSP_STATE_SEEKING,
  RTSP_STATE_PLAYING,
  RTSP_STATE_RECORDING,
} RTSPState;

typedef enum {
  RTSP_VERSION_INVALID = 0x00,
  RTSP_VERSION_1_0     = 0x10,
} RTSPVersion;

typedef enum {
  RTSP_INVALID          = 0,
  RTSP_DESCRIBE         = (1 <<  0),
  RTSP_ANNOUNCE         = (1 <<  1),
  RTSP_GET_PARAMETER    = (1 <<  2),
  RTSP_OPTIONS          = (1 <<  3),
  RTSP_PAUSE            = (1 <<  4),
  RTSP_PLAY             = (1 <<  5),
  RTSP_RECORD           = (1 <<  6),
  RTSP_REDIRECT         = (1 <<  7),
  RTSP_SETUP            = (1 <<  8),
  RTSP_SET_PARAMETER    = (1 <<  9),
  RTSP_TEARDOWN         = (1 << 10),
} RTSPMethod;

/* Authentication methods, ordered by strength */
typedef enum {
  RTSP_AUTH_NONE    = 0x00,
  RTSP_AUTH_BASIC   = 0x01,
  RTSP_AUTH_DIGEST  = 0x02
} RTSPAuthMethod;

/* Strongest available authentication method */
#define RTSP_AUTH_MAX RTSP_AUTH_DIGEST

typedef enum {
  RTSP_HDR_INVALID,

  /*
   * R = Request
   * r = response
   * g = general
   * e = entity
   */
  RTSP_HDR_ACCEPT,              /* Accept               R      opt.      entity */
  RTSP_HDR_ACCEPT_ENCODING,     /* Accept-Encoding      R      opt.      entity */
  RTSP_HDR_ACCEPT_LANGUAGE,     /* Accept-Language      R      opt.      all */
  RTSP_HDR_ALLOW,               /* Allow                r      opt.      all */
  RTSP_HDR_AUTHORIZATION,       /* Authorization        R      opt.      all */
  RTSP_HDR_BANDWIDTH,           /* Bandwidth            R      opt.      all */
  RTSP_HDR_BLOCKSIZE,           /* Blocksize            R      opt.      all but OPTIONS, TEARDOWN */
  RTSP_HDR_CACHE_CONTROL,       /* Cache-Control        g      opt.      SETUP */
  RTSP_HDR_CONFERENCE,          /* Conference           R      opt.      SETUP */
  RTSP_HDR_CONNECTION,          /* Connection           g      req.      all */
  RTSP_HDR_CONTENT_BASE,        /* Content-Base         e      opt.      entity */
  RTSP_HDR_CONTENT_ENCODING,    /* Content-Encoding     e      req.      SET_PARAMETER, DESCRIBE, ANNOUNCE */
  RTSP_HDR_CONTENT_LANGUAGE,    /* Content-Language     e      req.      DESCRIBE, ANNOUNCE */
  RTSP_HDR_CONTENT_LENGTH,      /* Content-Length       e      req.      SET_PARAMETER, ANNOUNCE, entity */
  RTSP_HDR_CONTENT_LOCATION,    /* Content-Location     e      opt.      entity */
  RTSP_HDR_CONTENT_TYPE,        /* Content-Type         e      req.      SET_PARAMETER, ANNOUNCE, entity */
  RTSP_HDR_CSEQ,                /* CSeq                 g      req.      all */
  RTSP_HDR_DATE,                /* Date                 g      opt.      all */
  RTSP_HDR_EXPIRES,             /* Expires              e      opt.      DESCRIBE, ANNOUNCE */
  RTSP_HDR_FROM,                /* From                 R      opt.      all */
  RTSP_HDR_IF_MODIFIED_SINCE,   /* If-Modified-Since    R      opt.      DESCRIBE, SETUP */
  RTSP_HDR_LAST_MODIFIED,       /* Last-Modified        e      opt.      entity */
  RTSP_HDR_PROXY_AUTHENTICATE,  /* Proxy-Authenticate */
  RTSP_HDR_PROXY_REQUIRE,       /* Proxy-Require        R      req.      all */
  RTSP_HDR_PUBLIC,              /* Public               r      opt.      all */
  RTSP_HDR_RANGE,               /* Range                Rr     opt.      PLAY, PAUSE, RECORD */
  RTSP_HDR_REFERER,             /* Referer              R      opt.      all */
  RTSP_HDR_REQUIRE,             /* Require              R      req.      all */
  RTSP_HDR_RETRY_AFTER,         /* Retry-After          r      opt.      all */
  RTSP_HDR_RTP_INFO,            /* RTP-Info             r      req.      PLAY */
  RTSP_HDR_SCALE,               /* Scale                Rr     opt.      PLAY, RECORD */
  RTSP_HDR_SESSION,             /* Session              Rr     req.      all but SETUP, OPTIONS */
  RTSP_HDR_SERVER,              /* Server               r      opt.      all */
  RTSP_HDR_SPEED,               /* Speed                Rr     opt.      PLAY */
  RTSP_HDR_TRANSPORT,           /* Transport            Rr     req.      SETUP */
  RTSP_HDR_UNSUPPORTED,         /* Unsupported          r      req.      all */
  RTSP_HDR_USER_AGENT,          /* User-Agent           R      opt.      all */
  RTSP_HDR_VIA,                 /* Via                  g      opt.      all */
  RTSP_HDR_WWW_AUTHENTICATE,    /* WWW-Authenticate     r      opt.      all */

  /* Real extensions */
  RTSP_HDR_CLIENT_CHALLENGE,    /* ClientChallenge */
  RTSP_HDR_REAL_CHALLENGE1,     /* RealChallenge1 */
  RTSP_HDR_REAL_CHALLENGE2,     /* RealChallenge2 */
  RTSP_HDR_REAL_CHALLENGE3,     /* RealChallenge3 */
  RTSP_HDR_SUBSCRIBE,           /* Subscribe */
  RTSP_HDR_ALERT,               /* Alert */
  RTSP_HDR_CLIENT_ID,           /* ClientID */
  RTSP_HDR_COMPANY_ID,          /* CompanyID */
  RTSP_HDR_GUID,                /* GUID */
  RTSP_HDR_REGION_DATA,         /* RegionData */
  RTSP_HDR_MAX_ASM_WIDTH,       /* SupportsMaximumASMBandwidth */
  RTSP_HDR_LANGUAGE,            /* Language */
  RTSP_HDR_PLAYER_START_TIME,   /* PlayerStarttime */


} RTSPHeaderField;

typedef enum {
  RTSP_STS_INVALID                              = 0, 
  RTSP_STS_CONTINUE                             = 100, 
  RTSP_STS_OK                                   = 200, 
  RTSP_STS_CREATED                              = 201, 
  RTSP_STS_LOW_ON_STORAGE                       = 250, 
  RTSP_STS_MULTIPLE_CHOICES                     = 300, 
  RTSP_STS_MOVED_PERMANENTLY                    = 301, 
  RTSP_STS_MOVE_TEMPORARILY                     = 302, 
  RTSP_STS_SEE_OTHER                            = 303, 
  RTSP_STS_NOT_MODIFIED                         = 304, 
  RTSP_STS_USE_PROXY                            = 305, 
  RTSP_STS_BAD_REQUEST                          = 400, 
  RTSP_STS_UNAUTHORIZED                         = 401, 
  RTSP_STS_PAYMENT_REQUIRED                     = 402, 
  RTSP_STS_FORBIDDEN                            = 403, 
  RTSP_STS_NOT_FOUND                            = 404, 
  RTSP_STS_METHOD_NOT_ALLOWED                   = 405, 
  RTSP_STS_NOT_ACCEPTABLE                       = 406, 
  RTSP_STS_PROXY_AUTH_REQUIRED                  = 407, 
  RTSP_STS_REQUEST_TIMEOUT                      = 408, 
  RTSP_STS_GONE                                 = 410, 
  RTSP_STS_LENGTH_REQUIRED                      = 411, 
  RTSP_STS_PRECONDITION_FAILED                  = 412, 
  RTSP_STS_REQUEST_ENTITY_TOO_LARGE             = 413, 
  RTSP_STS_REQUEST_URI_TOO_LARGE                = 414, 
  RTSP_STS_UNSUPPORTED_MEDIA_TYPE               = 415, 
  RTSP_STS_PARAMETER_NOT_UNDERSTOOD             = 451, 
  RTSP_STS_CONFERENCE_NOT_FOUND                 = 452, 
  RTSP_STS_NOT_ENOUGH_BANDWIDTH                 = 453, 
  RTSP_STS_SESSION_NOT_FOUND                    = 454, 
  RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE       = 455, 
  RTSP_STS_HEADER_FIELD_NOT_VALID_FOR_RESOURCE  = 456, 
  RTSP_STS_INVALID_RANGE                        = 457, 
  RTSP_STS_PARAMETER_IS_READONLY                = 458, 
  RTSP_STS_AGGREGATE_OPERATION_NOT_ALLOWED      = 459, 
  RTSP_STS_ONLY_AGGREGATE_OPERATION_ALLOWED     = 460, 
  RTSP_STS_UNSUPPORTED_TRANSPORT                = 461, 
  RTSP_STS_DESTINATION_UNREACHABLE              = 462, 
  RTSP_STS_INTERNAL_SERVER_ERROR                = 500, 
  RTSP_STS_NOT_IMPLEMENTED                      = 501, 
  RTSP_STS_BAD_GATEWAY                          = 502, 
  RTSP_STS_SERVICE_UNAVAILABLE                  = 503, 
  RTSP_STS_GATEWAY_TIMEOUT                      = 504, 
  RTSP_STS_RTSP_VERSION_NOT_SUPPORTED           = 505, 
  RTSP_STS_OPTION_NOT_SUPPORTED                 = 551, 
} RTSPStatusCode;

gchar*          rtsp_strresult          (RTSPResult result);

const gchar*    rtsp_method_as_text     (RTSPMethod method);
const gchar*    rtsp_version_as_text    (RTSPVersion version);
const gchar*    rtsp_header_as_text     (RTSPHeaderField field);
const gchar*    rtsp_status_as_text     (RTSPStatusCode code);

RTSPHeaderField rtsp_find_header_field  (gchar *header);
RTSPMethod      rtsp_find_method        (gchar *method);

G_END_DECLS

#endif /* __RTSP_DEFS_H__ */
