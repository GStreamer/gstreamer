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

#ifndef __RTSP_DEFS_H__
#define __RTSP_DEFS_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  RTSP_OK	=  0,
  /* errors */
  RTSP_EINVAL	= -1,
  RTSP_ENOMEM	= -2,
  RTSP_ERESOLV	= -3,
  RTSP_ENOTIMPL	= -4,
  RTSP_ESYS	= -5,
  RTSP_EPARSE	= -6,
} RTSPResult;

typedef enum {
  RTSP_PROTO_TCP,
  RTSP_PROTO_UDP,
} RTSPProto;

typedef enum {
  RTSP_FAM_NONE,
  RTSP_FAM_INET,
  RTSP_FAM_INET6,
} RTSPFamily;

typedef enum {
  RTSP_STATE_INIT,
  RTSP_STATE_READY,
  RTSP_STATE_PLAYING,
  RTSP_STATE_RECORDING,
} RTSPState;

typedef enum {
  RTSP_DESCRIBE		= (1 <<  0),
  RTSP_ANNOUNCE		= (1 <<  1),
  RTSP_GET_PARAMETER	= (1 <<  2),
  RTSP_OPTIONS		= (1 <<  3),
  RTSP_PAUSE		= (1 <<  4),
  RTSP_PLAY		= (1 <<  5),
  RTSP_RECORD		= (1 <<  6),
  RTSP_REDIRECT		= (1 <<  7),
  RTSP_SETUP		= (1 <<  8),
  RTSP_SET_PARAMETER	= (1 <<  9),
  RTSP_TEARDOWN		= (1 << 10),
} RTSPMethod;

typedef enum {
  /*
   * R = Request
   * r = response
   * g = general
   * e = entity
   */
  RTSP_HDR_ACCEPT, 		/* Accept               R      opt.      entity */
  RTSP_HDR_ACCEPT_ENCODING, 	/* Accept-Encoding      R      opt.      entity */
  RTSP_HDR_ACCEPT_LANGUAGE,	/* Accept-Language      R      opt.      all */
  RTSP_HDR_ALLOW,		/* Allow                r      opt.      all */
  RTSP_HDR_AUTHORIZATION,	/* Authorization        R      opt.      all */
  RTSP_HDR_BANDWIDTH,		/* Bandwidth            R      opt.      all */
  RTSP_HDR_BLOCKSIZE,		/* Blocksize            R      opt.      all but OPTIONS, TEARDOWN */
  RTSP_HDR_CACHE_CONTROL,	/* Cache-Control        g      opt.      SETUP */
  RTSP_HDR_CONFERENCE,		/* Conference           R      opt.      SETUP */
  RTSP_HDR_CONNECTION,		/* Connection           g      req.      all */
  RTSP_HDR_CONTENT_BASE,	/* Content-Base         e      opt.      entity */
  RTSP_HDR_CONTENT_ENCODING,	/* Content-Encoding     e      req.      SET_PARAMETER, DESCRIBE, ANNOUNCE */
  RTSP_HDR_CONTENT_LANGUAGE,	/* Content-Language     e      req.      DESCRIBE, ANNOUNCE */
  RTSP_HDR_CONTENT_LENGTH,	/* Content-Length       e      req.      SET_PARAMETER, ANNOUNCE, entity */
  RTSP_HDR_CONTENT_LOCATION,	/* Content-Location     e      opt.      entity */
  RTSP_HDR_CONTENT_TYPE,	/* Content-Type         e      req.      SET_PARAMETER, ANNOUNCE, entity */
  RTSP_HDR_CSEQ,		/* CSeq                 g      req.      all */
  RTSP_HDR_DATE,		/* Date                 g      opt.      all */
  RTSP_HDR_EXPIRES,		/* Expires              e      opt.      DESCRIBE, ANNOUNCE */
  RTSP_HDR_FROM,		/* From                 R      opt.      all */
  RTSP_HDR_IF_MODIFIED_SINCE,	/* If-Modified-Since    R      opt.      DESCRIBE, SETUP */
  RTSP_HDR_LAST_MODIFIED,	/* Last-Modified        e      opt.      entity */
  RTSP_HDR_PROXY_AUTHENTICATE,	/* Proxy-Authenticate */
  RTSP_HDR_PROXY_REQUIRE,	/* Proxy-Require        R      req.      all */
  RTSP_HDR_PUBLIC,		/* Public               r      opt.      all */
  RTSP_HDR_RANGE,		/* Range                Rr     opt.      PLAY, PAUSE, RECORD */
  RTSP_HDR_REFERER,		/* Referer              R      opt.      all */
  RTSP_HDR_REQUIRE,		/* Require              R      req.      all */
  RTSP_HDR_RETRY_AFTER,		/* Retry-After          r      opt.      all */
  RTSP_HDR_RTP_INFO,		/* RTP-Info             r      req.      PLAY */
  RTSP_HDR_SCALE,		/* Scale                Rr     opt.      PLAY, RECORD */
  RTSP_HDR_SESSION,		/* Session              Rr     req.      all but SETUP, OPTIONS */
  RTSP_HDR_SERVER,		/* Server               r      opt.      all */
  RTSP_HDR_SPEED,		/* Speed                Rr     opt.      PLAY */
  RTSP_HDR_TRANSPORT,		/* Transport            Rr     req.      SETUP */
  RTSP_HDR_UNSUPPORTED,		/* Unsupported          r      req.      all */
  RTSP_HDR_USER_AGENT,		/* User-Agent           R      opt.      all */
  RTSP_HDR_VIA,			/* Via                  g      opt.      all */
  RTSP_HDR_WWW_AUTHENTICATE,	/* WWW-Authenticate     r      opt.      all */

} RTSPHeaderField;

typedef enum {
  RTSP_STS_CONTINUE 				= 100, 
  RTSP_STS_OK 					= 200, 
  RTSP_STS_CREATED 				= 201, 
  RTSP_STS_LOW_ON_STORAGE 			= 250, 
  RTSP_STS_MULTIPLE_CHOICES 			= 300, 
  RTSP_STS_MOVED_PERMANENTLY 			= 301, 
  RTSP_STS_MOVE_TEMPORARILY 			= 302, 
  RTSP_STS_SEE_OTHER 				= 303, 
  RTSP_STS_NOT_MODIFIED 			= 304, 
  RTSP_STS_USE_PROXY 				= 305, 
  RTSP_STS_BAD_REQUEST 				= 400, 
  RTSP_STS_UNAUTHORIZED 			= 401, 
  RTSP_STS_PAYMENT_REQUIRED 			= 402, 
  RTSP_STS_FORBIDDEN 				= 403, 
  RTSP_STS_NOT_FOUND 				= 404, 
  RTSP_STS_METHOD_NOT_ALLOWED 			= 405, 
  RTSP_STS_NOT_ACCEPTABLE 			= 406, 
  RTSP_STS_PROXY_AUTH_REQUIRED 			= 407, 
  RTSP_STS_REQUEST_TIMEOUT 			= 408, 
  RTSP_STS_GONE 				= 410, 
  RTSP_STS_LENGTH_REQUIRED 			= 411, 
  RTSP_STS_PRECONDITION_FAILED 			= 412, 
  RTSP_STS_REQUEST_ENTITY_TOO_LARGE 		= 413, 
  RTSP_STS_REQUEST_URI_TOO_LARGE 		= 414, 
  RTSP_STS_UNSUPPORTED_MEDIA_TYPE 		= 415, 
  RTSP_STS_PARAMETER_NOT_UNDERSTOOD 		= 451, 
  RTSP_STS_CONFERENCE_NOT_FOUND 		= 452, 
  RTSP_STS_NOT_ENOUGH_BANDWIDTH 		= 453, 
  RTSP_STS_SESSION_NOT_FOUND 			= 454, 
  RTSP_STS_METHOD_NOT_VALID_IN_THIS_STATE 	= 455, 
  RTSP_STS_HEADER_FIELD_NOT_VALID_FOR_RESOURCE 	= 456, 
  RTSP_STS_INVALID_RANGE 			= 457, 
  RTSP_STS_PARAMETER_IS_READONLY 		= 458, 
  RTSP_STS_AGGREGATE_OPERATION_NOT_ALLOWED 	= 459, 
  RTSP_STS_ONLY_AGGREGATE_OPERATION_ALLOWED 	= 460, 
  RTSP_STS_UNSUPPORTED_TRANSPORT 		= 461, 
  RTSP_STS_DESTINATION_UNREACHABLE 		= 462, 
  RTSP_STS_INTERNAL_SERVER_ERROR 		= 500, 
  RTSP_STS_NOT_IMPLEMENTED 			= 501, 
  RTSP_STS_BAD_GATEWAY 				= 502, 
  RTSP_STS_SERVICE_UNAVAILABLE 			= 503, 
  RTSP_STS_GATEWAY_TIMEOUT 			= 504, 
  RTSP_STS_RTSP_VERSION_NOT_SUPPORTED 		= 505, 
  RTSP_STS_OPTION_NOT_SUPPORTED 		= 551, 
} RTSPStatusCode;

const gchar*	rtsp_method_as_text   	(RTSPMethod method);
const gchar*	rtsp_header_as_text   	(RTSPHeaderField field);
const gchar*	rtsp_status_as_text   	(RTSPStatusCode code);
const gchar*	rtsp_status_to_string 	(RTSPStatusCode code);

RTSPHeaderField	rtsp_find_header_field 	(gchar *header);
RTSPMethod	rtsp_find_method	(gchar *method);

G_END_DECLS

#endif /* __RTSP_DEFS_H__ */
