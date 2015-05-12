/* GStreamer DVD title source
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2001> Billy Biggs <vektor@dumbterm.net>.
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_DVD_READ_SRC_H__
#define __GST_DVD_READ_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_types.h>
#include <dvdread/ifo_read.h>
#include <dvdread/nav_read.h>
#include <dvdread/nav_print.h>

G_BEGIN_DECLS

#define GST_TYPE_DVD_READ_SRC            (gst_dvd_read_src_get_type())
#define GST_DVD_READ_SRC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVD_READ_SRC,GstDvdReadSrc))
#define GST_DVD_READ_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVD_READ_SRC,GstDvdReadSrcClass))
#define GST_IS_DVD_READ_SRC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVD_READ_SRC))
#define GST_IS_DVD_READ_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVD_READ_SRC))

typedef struct _GstDvdReadSrc GstDvdReadSrc;
typedef struct _GstDvdReadSrcClass GstDvdReadSrcClass;

struct _GstDvdReadSrc {
  GstPushSrc       pushsrc;

  /* location */
  gchar           *location;

  gboolean         first_seek;
  gboolean         new_seek;
  gboolean         change_cell;

  gboolean         new_cell;

  gint             uri_title;     /* set via the URI handler or properties,  */
  gint             uri_chapter;   /* otherwise not touched; these values     */
  gint             uri_angle;     /* start from 1                            */

  gint             title;         /* current position while open, set to the */
  gint             chapter;       /* URI-set values in ::start(). these      */
  gint             angle;         /* values start from 0                     */

  gint             start_cell, last_cell, cur_cell;
  gint             cur_pack;
  gint             next_cell;
  dvd_reader_t    *dvd;
  ifo_handle_t    *vmg_file;

  /* title stuff */
  gint             ttn;
  tt_srpt_t       *tt_srpt;
  ifo_handle_t    *vts_file;
  vts_ptt_srpt_t  *vts_ptt_srpt;
  vts_tmapt_t     *vts_tmapt;
  dvd_file_t      *dvd_title;
  gint             num_chapters;
  gint             num_angles;

  GstClockTime    *chapter_starts;  /* start time of chapters within title   */

  /* which program chain to watch (based on title and chapter number) */
  pgc_t           *cur_pgc;
  gint             pgc_id;
  gint             pgn;

  gboolean         need_newsegment;
  GstEvent        *title_lang_event_pending;
  GstEvent        *pending_clut_event;
};

struct _GstDvdReadSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_dvd_read_src_get_type (void);

G_END_DECLS

#endif /* __GST_DVD_READ_SRC_H__ */

