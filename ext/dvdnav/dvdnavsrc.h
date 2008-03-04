/* GStreamer
 * Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
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

#ifndef __GST_DVD_NAV_SRC_H__
#define __GST_DVD_NAV_SRC_H__

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

#include <dvdnav/dvdnav.h>
#include <dvdnav/nav_print.h>

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#define GST_TYPE_DVD_NAV_SRC             (gst_dvd_nav_src_get_type())
#define GST_DVD_NAV_SRC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVD_NAV_SRC,GstDvdNavSrc))
#define GST_DVD_NAV_SRC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVD_NAV_SRC,GstDvdNavSrcClass))
#define GST_IS_DVD_NAV_SRC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVD_NAV_SRC))
#define GST_IS_DVD_NAV_SRC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVD_NAV_SRC))

typedef struct _GstDvdNavSrc GstDvdNavSrc;
typedef struct _GstDvdNavSrcClass GstDvdNavSrcClass;

/* The pause modes to handle still frames. */
typedef enum
{
  GST_DVD_NAV_SRC_PAUSE_OFF,          /* No pause active. */
  GST_DVD_NAV_SRC_PAUSE_LIMITED,      /* A time limited pause is active. */
  GST_DVD_NAV_SRC_PAUSE_UNLIMITED     /* An time unlimited pause is active. */
} GstDvdNavSrcPauseMode;

/* The DVD domain types. */
typedef enum
{
  GST_DVD_NAV_SRC_DOMAIN_UNKNOWN,     /* Unknown domain.  */
  GST_DVD_NAV_SRC_DOMAIN_FP,          /* First Play domain. */
  GST_DVD_NAV_SRC_DOMAIN_VMGM,        /* Video Management Menu domain */
  GST_DVD_NAV_SRC_DOMAIN_VTSM,        /* Video Title Menu domain. */
  GST_DVD_NAV_SRC_DOMAIN_VTS          /* Video Title domain. */
} GstDvdNavSrcDomainType;

struct _GstDvdNavSrc
{
  GstPushSrc               pushsrc;

  GstCaps                 *streaminfo;

  gchar                   *device;
  gchar                   *last_uri;

  gint64                   pending_offset;  /* Next newsegment event offset */
  gboolean                 new_seek;
  gboolean                 seek_pending;
  gboolean                 need_flush;
  gboolean                 first_seek;
  gboolean                 use_tmaps;
  gboolean                 still_frame;

  /* Timing */
#if 0
  GstClock                 *clock;        /* The clock for this element      */
#endif

  /* Pause handling */
  GstDvdNavSrcPauseMode     pause_mode;   /* The current pause mode          */
  GstClockTime              pause_remain; /* Remaining duration of the pause */

  /* Highligh handling */
  gint                      button;   /* The currently highlighted button    *
                                       * number (0 if no highlight)          */

  dvdnav_highlight_area_t   area;     /* The area corresponding to the       *
                                       * currently highlighted button        */

  /* State handling */
  GstDvdNavSrcDomainType    domain;    /* The current DVD domain */

  gint                      title;        /* Current title, chapter, angle;  */
  gint                      chapter;      /* can be changed by seek events   */
  gint                      angle;        /* initialised at start from uri_x */

  gint                      uri_title;    /* Current title, chapter, angle   */
  gint                      uri_chapter;  /* as set via the uri handler      */
  gint                      uri_angle;

  gint                      audio_phys;   /* The current audio streams */
  gint                      audio_log;

  gint                      subp_phys;    /* The current subpicture streams  */
  gint                      subp_log;

  dvdnav_t                 *dvdnav;       /* The libdvdnav handle */

  GstCaps                  *buttoninfo;

  GstBuffer                *cur_buf;         /* Current output buffer.       *
                                              * See dvd_nav_src_get()        */

  GstClockTime              pgc_length;      /* Length of the current        *
                                              * program chain (title)        */

  GstClockTime              cell_start;      /* Start of the current cell    */
  GstClockTime              pg_start;        /* Start of the current program *
                                              * within the PGC               */

  gint                      cur_vts;         /* Current VTS being read       */
  vmgi_mat_t                vmgm_attr;        /* VMGM domain info             */
  GArray                   *vts_attrs;       /* Array of vts_attributes_t    *
                                              * structures  cached from      *
                                              * the VMG ifo                  */
  guint32                   sector_length;

  dvd_reader_t              *dvd;
  ifo_handle_t              *vmg_file;
  tt_srpt_t                 *tt_srpt;
  vts_ptt_srpt_t            *vts_ptt_srpt;
  ifo_handle_t              *vts_file;
  vts_tmapt_t               *vts_tmapt;
  vts_tmap_t                *title_tmap;
};

struct _GstDvdNavSrcClass
{
  GstPushSrcClass parent_class;

  void (*user_op) (GstDvdNavSrc * src, gint op);
};

#endif /* __GST_DVD_NAV_SRC_H__ */

