/* GStreamer
 * Copyright (C) 2008 Jan Schmidt <thaytan@noraisin.net>
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
#ifndef __RESINDVDSRC_H__
#define __RESINDVDSRC_H__

#include <gst/gst.h>

#include "rsnpushsrc.h"

#include "_stdint.h"

#ifndef DVDNAV_OLD

#include <dvdnav/dvdnav.h>
#include <dvdread/ifo_read.h>

#else

#include <dvdnav/dvd_reader.h>
#include <dvdnav/ifo_read.h>

#include <dvdnav/dvdnav.h>
#include <dvdnav/nav_print.h>

#endif

G_BEGIN_DECLS

#define RESIN_TYPE_DVDSRC (rsn_dvdsrc_get_type())
#define RESINDVDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),RESIN_TYPE_DVDSRC,resinDvdSrc))
#define RESINDVDSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),RESIN_TYPE_DVDSRC,resinDvdSrcClass))
#define IS_RESINDVDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),RESIN_TYPE_DVDSRC))
#define IS_RESINDVDSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),RESIN_TYPE_DVDSRC))

typedef struct _resinDvdSrc      resinDvdSrc;
typedef struct _resinDvdSrcClass resinDvdSrcClass;

struct _resinDvdSrc
{
  RsnPushSrc parent;

  GMutex	*dvd_lock;
  GCond		*still_cond;
  GMutex	*branch_lock;
  gboolean	branching;

  gchar		*device;
  dvdnav_t	*dvdnav;

  /* dvd_reader instance is used to load and cache VTS/VMG ifo info */
  dvd_reader_t  *dvdread;

  /* vmgi_mat_t from the VMG ifo: */
  vmgi_mat_t                vmgm_attr;        /* VMGM domain info             */
  /* Array of cached vtsi_mat_t strctures from each IFO: */
  GArray                   *vts_attrs;

  ifo_handle_t              *vmg_file;
  ifo_handle_t              *vts_file;

  /* Current playback location: VTS 0 = VMG, plus in_menu or not */
  gint		vts_n;
  gboolean	in_menu;

  gboolean	running;
  gboolean	discont;
  gboolean	need_segment;
  gboolean	active_highlight;

  GstBuffer	*alloc_buf;
  GstBuffer	*next_buf;

  /* Start timestamp of the previous NAV block */
  GstClockTime  cur_start_ts;
  /* End timestamp of the previous NAV block */
  GstClockTime  cur_end_ts;
  /* Position info of the previous NAV block */
  GstClockTime  cur_position;
  /* Duration of the current PGC */
  GstClockTime  pgc_duration;

  gint          active_button;
  dvdnav_highlight_area_t area;

  /* Pending events to output */
  GstEvent	*streams_event;
  GstEvent	*clut_event;
  GstEvent	*spu_select_event;
  GstEvent	*audio_select_event;
  GstEvent	*highlight_event;
};

struct _resinDvdSrcClass 
{
  RsnPushSrcClass parent_class;
};

GType rsn_dvdsrc_get_type (void);

G_END_DECLS

#endif /* __RESINDVDSRC_H__ */
