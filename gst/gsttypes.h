/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_TYPES_H__
#define __GST_TYPES_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GstObject GstObject;
typedef struct _GstObjectClass GstObjectClass;
typedef struct _GstPad GstPad;
typedef struct _GstPadClass GstPadClass;
typedef struct _GstPadTemplate GstPadTemplate;
typedef struct _GstPadTemplateClass GstPadTemplateClass;
typedef struct _GstElement GstElement;
typedef struct _GstElementClass GstElementClass;
typedef struct _GstBin GstBin;
typedef struct _GstBinClass GstBinClass;
typedef struct _GstScheduler GstScheduler;
typedef struct _GstSchedulerClass GstSchedulerClass;
typedef struct _GstEvent GstEvent;

typedef enum {
  GST_STATE_VOID_PENDING        = 0,
  GST_STATE_NULL                = (1 << 0),
  GST_STATE_READY               = (1 << 1),
  GST_STATE_PAUSED              = (1 << 2),
  GST_STATE_PLAYING             = (1 << 3)
} GstElementState;

typedef enum {
  GST_STATE_FAILURE             = 0,
  GST_STATE_SUCCESS             = 1,
  GST_STATE_ASYNC               = 2
} GstElementStateReturn;

typedef enum {
  GST_RESULT_OK,
  GST_RESULT_NOK,
  GST_RESULT_NOT_IMPL
} GstResult;

#define GST_RANK_PRIMARY    256
#define GST_RANK_SECONDARY  128
#define GST_RANK_MARGINAL   64
#define GST_RANK_NONE       0

#define GST_PADDING 4
#define GST_PADDING_INIT	{ 0 }

#ifdef WIN32
#define GST_FILE_MODE_READ  "rb"
#define GST_FILE_MODE_WRITE "wb"
#define GST_O_READONLY      O_RDONLY|O_BINARY
#else
#define GST_FILE_MODE_READ  "r"
#define GST_FILE_MODE_WRITE "w"
#define GST_O_READONLY      O_RDONLY
#endif

G_END_DECLS

#endif /* __GST_TYPES_H__ */
