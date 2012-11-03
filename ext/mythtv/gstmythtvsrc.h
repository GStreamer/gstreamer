/*
 * GStreamer
 * Copyright (C) <2006> INdT - Rosfran Borges <rosfran.borges@indt.org.br>
 * Copyright (C) <2007> INdT - Rentao Filho <renato.filho@indt.org.br>
 *
 * This library is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Library
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library
 * General Public License for more details. You should have received a copy
 * of the GNU Library General Public License along with this library; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin St,
 * Fifth Floor, Boston, MA 02110-1301, USA. 
 */

#ifndef __GST_MYTHTV_SRC_H__
#define __GST_MYTHTV_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#include <stdio.h>

#include <gmyth/gmyth_socket.h>
#include <gmyth/gmyth_file.h>
#include <gmyth/gmyth_file_transfer.h>
#include <gmyth/gmyth_file_local.h>
#include <gmyth/gmyth_livetv.h>
#include <gmyth/gmyth_backendinfo.h>

G_BEGIN_DECLS
#define GST_TYPE_MYTHTV_SRC \
  (gst_mythtv_src_get_type())
#define GST_MYTHTV_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MYTHTV_SRC,GstMythtvSrc))
#define GST_MYTHTV_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MYTHTV_SRC,GstMythtvSrcClass))
#define GST_IS_MYTHTV_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MYTHTV_SRC))
#define GST_IS_MYTHTV_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MYTHTV_SRC))
typedef struct _GstMythtvSrc GstMythtvSrc;
typedef struct _GstMythtvSrcClass GstMythtvSrcClass;

typedef enum {
  GST_MYTHTV_SRC_FILE_TRANSFER,
  GST_MYTHTV_SRC_NEXT_PROGRAM_CHAIN,
  GST_MYTHTV_SRC_INVALID_DATA
} GstMythtvState;

struct _GstMythtvSrc {
  GstPushSrc      element;

  /*
   * MythFileTransfer 
   */
  GMythFile      *file;
  GMythLiveTV    *spawn_livetv;
  GMythBackendInfo *backend_info;
  GstMythtvState  state;
  gchar          *uri_name;
  gchar          *user_agent;
  gchar          *live_chain_id;
  gint            mythtv_version;
  gint64          content_size;
  gint64          prev_content_size;
  gint64          content_size_last;
  guint64         bytes_read;
  gint64          read_offset;
  gboolean        eos;
  gboolean        do_start;
  gboolean        unique_setup;
  gboolean        live_tv;
  gboolean        enable_timing_position;
  gint            live_tv_id;
  gchar          *channel_name;
  guint           mode;

  /*
   * MythTV capabilities 
   */
  GstCaps        *mythtv_caps;
  gboolean        update_prog_chain;

  /*
   * stablish a maximum iteration value to the IS_RECORDING message 
   */
  guint           wait_to_transfer;
};

struct _GstMythtvSrcClass {
  GstPushSrcClass parent_class;
};

GType           gst_mythtv_src_get_type(void);

G_END_DECLS
#endif /* __GST_MYTHTV_SRC_H__ */
