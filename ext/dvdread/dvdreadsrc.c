/* GStreamer DVD title source
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2001 Billy Biggs <vektor@dumbterm.net>.
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "_stdint.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "dvdreadsrc.h"

#include <gmodule.h>

#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gstgst_dvd_read_src_debug);
#define GST_CAT_DEFAULT (gstgst_dvd_read_src_debug)

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_TITLE,
  ARG_CHAPTER,
  ARG_ANGLE
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion=2, systemstream=(boolean)true"));

static GstFormat title_format;
static GstFormat angle_format;
static GstFormat sector_format;
static GstFormat chapter_format;

static gboolean gst_dvd_read_src_start (GstBaseSrc * basesrc);
static gboolean gst_dvd_read_src_stop (GstBaseSrc * basesrc);
static GstFlowReturn gst_dvd_read_src_create (GstPushSrc * pushsrc,
    GstBuffer ** buf);
static gboolean gst_dvd_read_src_src_query (GstBaseSrc * basesrc,
    GstQuery * query);
static gboolean gst_dvd_read_src_src_event (GstBaseSrc * basesrc,
    GstEvent * event);
static gboolean gst_dvd_read_src_goto_title (GstDvdReadSrc * src, gint title,
    gint angle);
static gboolean gst_dvd_read_src_goto_chapter (GstDvdReadSrc * src,
    gint chapter);
static gboolean gst_dvd_read_src_goto_sector (GstDvdReadSrc * src, gint angle);
static void gst_dvd_read_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dvd_read_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstEvent *gst_dvd_read_src_make_clut_change_event (GstDvdReadSrc * src,
    const guint * clut);
static gboolean gst_dvd_read_src_get_size (GstDvdReadSrc * src, gint64 * size);
static gboolean gst_dvd_read_src_do_seek (GstBaseSrc * src, GstSegment * s);
static gint64 gst_dvd_read_src_convert_timecode (dvd_time_t * time);
static gint gst_dvd_read_src_get_next_cell (GstDvdReadSrc * src,
    pgc_t * pgc, gint cell);
static GstClockTime gst_dvd_read_src_get_time_for_sector (GstDvdReadSrc * src,
    guint sector);
static gint gst_dvd_read_src_get_sector_from_time (GstDvdReadSrc * src,
    GstClockTime ts);

static void gst_dvd_read_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_dvd_read_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDvdReadSrc, gst_dvd_read_src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_dvd_read_src_uri_handler_init));

static void
gst_dvd_read_src_finalize (GObject * object)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (object);

  g_free (src->location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dvd_read_src_init (GstDvdReadSrc * src)
{
  src->dvd = NULL;
  src->vts_file = NULL;
  src->vmg_file = NULL;
  src->dvd_title = NULL;

  src->location = g_strdup ("/dev/dvd");
  src->new_seek = TRUE;
  src->new_cell = TRUE;
  src->change_cell = FALSE;
  src->uri_title = 1;
  src->uri_chapter = 1;
  src->uri_angle = 1;

  src->title_lang_event_pending = NULL;
  src->pending_clut_event = NULL;

  gst_pad_use_fixed_caps (GST_BASE_SRC_PAD (src));
  gst_pad_set_caps (GST_BASE_SRC_PAD (src),
      gst_static_pad_template_get_caps (&srctemplate));
}

static gboolean
gst_dvd_read_src_is_seekable (GstBaseSrc * src)
{
  return TRUE;
}

static void
gst_dvd_read_src_class_init (GstDvdReadSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->finalize = gst_dvd_read_src_finalize;
  gobject_class->set_property = gst_dvd_read_src_set_property;
  gobject_class->get_property = gst_dvd_read_src_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "DVD device location", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TITLE,
      g_param_spec_int ("title", "title", "title",
          1, 999, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHAPTER,
      g_param_spec_int ("chapter", "chapter", "chapter",
          1, 999, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ANGLE,
      g_param_spec_int ("angle", "angle", "angle",
          1, 999, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (gstelement_class, "DVD Source",
      "Source/File/DVD",
      "Access a DVD title/chapter/angle using libdvdread",
      "Erik Walthinsen <omega@cse.ogi.edu>");

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_dvd_read_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_dvd_read_src_stop);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_dvd_read_src_src_query);
  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_dvd_read_src_src_event);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_dvd_read_src_do_seek);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_dvd_read_src_is_seekable);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_dvd_read_src_create);

  title_format = gst_format_register ("title", "DVD title");
  angle_format = gst_format_register ("angle", "DVD angle");
  sector_format = gst_format_register ("sector", "DVD sector");
  chapter_format = gst_format_register ("chapter", "DVD chapter");
}

static gboolean
gst_dvd_read_src_start (GstBaseSrc * basesrc)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (basesrc);

  g_return_val_if_fail (src->location != NULL, FALSE);

  GST_DEBUG_OBJECT (src, "Opening DVD '%s'", src->location);

  if ((src->dvd = DVDOpen (src->location)) == NULL)
    goto open_failed;

  /* Load the video manager to find out the information about the titles */
  GST_DEBUG_OBJECT (src, "Loading VMG info");

  if (!(src->vmg_file = ifoOpen (src->dvd, 0)))
    goto ifo_open_failed;

  src->tt_srpt = src->vmg_file->tt_srpt;

  src->title = src->uri_title - 1;
  src->chapter = src->uri_chapter - 1;
  src->angle = src->uri_angle - 1;

  if (!gst_dvd_read_src_goto_title (src, src->title, src->angle))
    goto title_open_failed;

  if (!gst_dvd_read_src_goto_chapter (src, src->chapter))
    goto chapter_open_failed;

  src->new_seek = FALSE;
  src->change_cell = TRUE;

  return TRUE;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD")),
        ("DVDOpen(%s) failed: %s", src->location, g_strerror (errno)));
    return FALSE;
  }
ifo_open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD")),
        ("ifoOpen() failed: %s", g_strerror (errno)));
    return FALSE;
  }
title_open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD title %d"), src->uri_title), (NULL));
    return FALSE;
  }
chapter_open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Failed to go to chapter %d of DVD title %d"),
            src->uri_chapter, src->uri_title), (NULL));
    return FALSE;
  }
}

static gboolean
gst_dvd_read_src_stop (GstBaseSrc * basesrc)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (basesrc);

  if (src->vts_file) {
    ifoClose (src->vts_file);
    src->vts_file = NULL;
  }
  if (src->vmg_file) {
    ifoClose (src->vmg_file);
    src->vmg_file = NULL;
  }
  if (src->dvd_title) {
    DVDCloseFile (src->dvd_title);
    src->dvd_title = NULL;
  }
  if (src->dvd) {
    DVDClose (src->dvd);
    src->dvd = NULL;
  }
  src->new_cell = TRUE;
  src->new_seek = TRUE;
  src->change_cell = FALSE;
  src->chapter = 0;
  src->title = 0;
  src->need_newsegment = TRUE;
  src->vts_tmapt = NULL;
  if (src->title_lang_event_pending) {
    gst_event_unref (src->title_lang_event_pending);
    src->title_lang_event_pending = NULL;
  }
  if (src->pending_clut_event) {
    gst_event_unref (src->pending_clut_event);
    src->pending_clut_event = NULL;
  }
  if (src->chapter_starts) {
    g_free (src->chapter_starts);
    src->chapter_starts = NULL;
  }

  GST_LOG_OBJECT (src, "closed DVD");

  return TRUE;
}

static void
cur_title_get_chapter_pgc (GstDvdReadSrc * src, gint chapter, gint * p_pgn,
    gint * p_pgc_id, pgc_t ** p_pgc)
{
  pgc_t *pgc;
  gint pgn, pgc_id;

  g_assert (chapter >= 0 && chapter < src->num_chapters);

  pgc_id = src->vts_ptt_srpt->title[src->ttn - 1].ptt[chapter].pgcn;
  pgn = src->vts_ptt_srpt->title[src->ttn - 1].ptt[chapter].pgn;
  pgc = src->vts_file->vts_pgcit->pgci_srp[pgc_id - 1].pgc;

  *p_pgn = pgn;
  *p_pgc_id = pgc_id;
  *p_pgc = pgc;
}

static void
cur_title_get_chapter_bounds (GstDvdReadSrc * src, gint chapter,
    gint * p_first_cell, gint * p_last_cell)
{
  pgc_t *pgc;
  gint pgn, pgc_id, pgn_next_ch;

  g_assert (chapter >= 0 && chapter < src->num_chapters);

  cur_title_get_chapter_pgc (src, chapter, &pgn, &pgc_id, &pgc);

  *p_first_cell = pgc->program_map[pgn - 1] - 1;

  /* last cell is used as a 'up to boundary', not 'up to and including',
   * i.e. it is the first cell not included in the chapter range */
  if (chapter == (src->num_chapters - 1)) {
    *p_last_cell = pgc->nr_of_cells;
  } else {
    pgn_next_ch = src->vts_ptt_srpt->title[src->ttn - 1].ptt[chapter + 1].pgn;
    *p_last_cell = pgc->program_map[pgn_next_ch - 1] - 1;
  }

  GST_DEBUG_OBJECT (src, "Chapter %d bounds: %d %d (within %d cells)",
      chapter, *p_first_cell, *p_last_cell, pgc->nr_of_cells);
}

static gboolean
gst_dvd_read_src_goto_chapter (GstDvdReadSrc * src, gint chapter)
{
  gint i;

  /* make sure the chapter number is valid for this title */
  if (chapter < 0 || chapter >= src->num_chapters) {
    GST_WARNING_OBJECT (src, "invalid chapter %d (only %d available)",
        chapter, src->num_chapters);
    chapter = CLAMP (chapter, 0, src->num_chapters - 1);
  }

  /* determine which program chain we want to watch. This is
   * based on the chapter number */
  cur_title_get_chapter_pgc (src, chapter, &src->pgn, &src->pgc_id,
      &src->cur_pgc);
  cur_title_get_chapter_bounds (src, chapter, &src->start_cell,
      &src->last_cell);

  GST_LOG_OBJECT (src, "Opened chapter %d - cell %d-%d", chapter + 1,
      src->start_cell, src->last_cell);

  /* retrieve position */
  src->cur_pack = 0;
  for (i = 0; i < chapter; i++) {
    gint c1, c2;

    cur_title_get_chapter_bounds (src, i, &c1, &c2);

    while (c1 < c2) {
      src->cur_pack +=
          src->cur_pgc->cell_playback[c1].last_sector -
          src->cur_pgc->cell_playback[c1].first_sector;
      ++c1;
    }
  }

  /* prepare reading for new cell */
  src->new_cell = TRUE;
  src->next_cell = src->start_cell;

  src->chapter = chapter;

  if (src->pending_clut_event)
    gst_event_unref (src->pending_clut_event);

  src->pending_clut_event =
      gst_dvd_read_src_make_clut_change_event (src, src->cur_pgc->palette);

  return TRUE;
}

static void
gst_dvd_read_src_get_chapter_starts (GstDvdReadSrc * src)
{
  GstClockTime uptohere;
  guint c;

  g_free (src->chapter_starts);
  src->chapter_starts = g_new (GstClockTime, src->num_chapters);

  uptohere = (GstClockTime) 0;
  for (c = 0; c < src->num_chapters; ++c) {
    GstClockTime chapter_duration = 0;
    gint cell_start, cell_end, cell;
    gint pgn, pgc_id;
    pgc_t *pgc;

    cur_title_get_chapter_pgc (src, c, &pgn, &pgc_id, &pgc);
    cur_title_get_chapter_bounds (src, c, &cell_start, &cell_end);

    cell = cell_start;
    while (cell < cell_end) {
      dvd_time_t *cell_duration;

      cell_duration = &pgc->cell_playback[cell].playback_time;
      chapter_duration += gst_dvd_read_src_convert_timecode (cell_duration);
      cell = gst_dvd_read_src_get_next_cell (src, pgc, cell);
    }

    src->chapter_starts[c] = uptohere;

    GST_INFO_OBJECT (src, "[%02u] Chapter %02u starts at %" GST_TIME_FORMAT
        ", dur = %" GST_TIME_FORMAT ", cells %d-%d", src->title + 1, c + 1,
        GST_TIME_ARGS (uptohere), GST_TIME_ARGS (chapter_duration),
        cell_start, cell_end);

    uptohere += chapter_duration;
  }
}

static gboolean
gst_dvd_read_src_goto_title (GstDvdReadSrc * src, gint title, gint angle)
{
  GstStructure *s;
  gchar lang_code[3] = { '\0', '\0', '\0' }, *t;
  pgc_t *pgc0;
  gint title_set_nr;
  gint num_titles;
  gint pgn0, pgc0_id;
  gint i;

  /* make sure our title number is valid */
  num_titles = src->tt_srpt->nr_of_srpts;
  GST_INFO_OBJECT (src, "There are %d titles on this DVD", num_titles);
  if (title < 0 || title >= num_titles)
    goto invalid_title;

  src->num_chapters = src->tt_srpt->title[title].nr_of_ptts;
  GST_INFO_OBJECT (src, "Title %d has %d chapters", title + 1,
      src->num_chapters);

  /* make sure the angle number is valid for this title */
  src->num_angles = src->tt_srpt->title[title].nr_of_angles;
  GST_LOG_OBJECT (src, "Title %d has %d angles", title + 1, src->num_angles);
  if (angle < 0 || angle >= src->num_angles) {
    GST_WARNING_OBJECT (src, "Invalid angle %d (only %d available)",
        angle, src->num_angles);
    angle = CLAMP (angle, 0, src->num_angles - 1);
  }

  /* load the VTS information for the title set our title is in */
  title_set_nr = src->tt_srpt->title[title].title_set_nr;
  src->vts_file = ifoOpen (src->dvd, title_set_nr);
  if (src->vts_file == NULL)
    goto ifo_open_failed;

  src->ttn = src->tt_srpt->title[title].vts_ttn;
  src->vts_ptt_srpt = src->vts_file->vts_ptt_srpt;

  /* interactive title? */
  if (src->num_chapters > 0 &&
      src->vts_ptt_srpt->title[src->ttn - 1].ptt[0].pgn == 0) {
    goto commands_only_pgc;
  }

  /* we've got enough info, time to open the title set data */
  src->dvd_title = DVDOpenFile (src->dvd, title_set_nr, DVD_READ_TITLE_VOBS);
  if (src->dvd_title == NULL)
    goto title_open_failed;

  GST_INFO_OBJECT (src, "Opened title %d, angle %d", title + 1, angle);
  src->title = title;
  src->angle = angle;

  /* build event */

  if (src->title_lang_event_pending) {
    gst_event_unref (src->title_lang_event_pending);
    src->title_lang_event_pending = NULL;
  }

  s = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-lang-codes", NULL);

  /* so we can filter out invalid/unused streams (same for all chapters) */
  cur_title_get_chapter_pgc (src, 0, &pgn0, &pgc0_id, &pgc0);

  /* audio */
  for (i = 0; i < src->vts_file->vtsi_mat->nr_of_vts_audio_streams; i++) {
    const audio_attr_t *a;

    /* audio stream present? */
    if (pgc0 != NULL && (pgc0->audio_control[i] & 0x8000) == 0)
      continue;

    a = &src->vts_file->vtsi_mat->vts_audio_attr[i];

    t = g_strdup_printf ("audio-%d-format", i);
    gst_structure_set (s, t, G_TYPE_INT, (int) a->audio_format, NULL);
    g_free (t);
    t = g_strdup_printf ("audio-%d-stream", i);
    gst_structure_set (s, t, G_TYPE_INT, (int) i, NULL);
    g_free (t);

    if (a->lang_type) {
      t = g_strdup_printf ("audio-%d-language", i);
      lang_code[0] = (a->lang_code >> 8) & 0xff;
      lang_code[1] = a->lang_code & 0xff;
      gst_structure_set (s, t, G_TYPE_STRING, lang_code, NULL);
      g_free (t);
    } else {
      lang_code[0] = '\0';
    }

    GST_INFO_OBJECT (src, "[%02d] Audio    %02d: lang='%s', format=%d",
        src->title + 1, i, lang_code, (gint) a->audio_format);
  }

  /* subtitle */
  for (i = 0; i < src->vts_file->vtsi_mat->nr_of_vts_subp_streams; i++) {
    const subp_attr_t *u;
    const video_attr_t *v;
    gint sid;

    /* subpicture stream present? */
    if (pgc0 != NULL && (pgc0->subp_control[i] & 0x80000000) == 0)
      continue;

    u = &src->vts_file->vtsi_mat->vts_subp_attr[i];
    v = &src->vts_file->vtsi_mat->vts_video_attr;

    sid = i;
    if (pgc0 != NULL) {
      if (v->display_aspect_ratio == 0) /* 4:3 */
        sid = (pgc0->subp_control[i] >> 24) & 0x1f;
      else if (v->display_aspect_ratio == 3)    /* 16:9 */
        sid = (pgc0->subp_control[i] >> 8) & 0x1f;
    }

    if (u->type) {
      t = g_strdup_printf ("subpicture-%d-language", i);
      lang_code[0] = (u->lang_code >> 8) & 0xff;
      lang_code[1] = u->lang_code & 0xff;
      gst_structure_set (s, t, G_TYPE_STRING, lang_code, NULL);
      t = g_strdup_printf ("subpicture-%d-stream", i);
      gst_structure_set (s, t, G_TYPE_INT, (int) sid, NULL);
      g_free (t);
      t = g_strdup_printf ("subpicture-%d-format", i);
      gst_structure_set (s, t, G_TYPE_INT, (int) 0, NULL);
      g_free (t);
    } else {
      lang_code[0] = '\0';
    }

    GST_INFO_OBJECT (src, "[%02d] Subtitle %02d: lang='%s', type=%d",
        src->title + 1, sid, lang_code, u->type);
  }

  src->title_lang_event_pending =
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

  /* dump seek tables */
  src->vts_tmapt = src->vts_file->vts_tmapt;
  if (src->vts_tmapt) {
    gint i, j;

    GST_LOG_OBJECT (src, "nr_of_tmaps = %d", src->vts_tmapt->nr_of_tmaps);
    for (i = 0; i < src->vts_tmapt->nr_of_tmaps; ++i) {
      GST_LOG_OBJECT (src, "======= Table %d ===================", i);
      GST_LOG_OBJECT (src, "Offset relative to VTS_TMAPTI: %d",
          src->vts_tmapt->tmap_offset[i]);
      GST_LOG_OBJECT (src, "Time unit (seconds)          : %d",
          src->vts_tmapt->tmap[i].tmu);
      GST_LOG_OBJECT (src, "Number of entries            : %d",
          src->vts_tmapt->tmap[i].nr_of_entries);
      for (j = 0; j < src->vts_tmapt->tmap[i].nr_of_entries; j++) {
        guint64 time;

        time = (guint64) src->vts_tmapt->tmap[i].tmu * (j + 1) * GST_SECOND;
        GST_LOG_OBJECT (src, "Time: %" GST_TIME_FORMAT " VOBU "
            "Sector: 0x%08x %s", GST_TIME_ARGS (time),
            src->vts_tmapt->tmap[i].map_ent[j] & 0x7fffffff,
            (src->vts_tmapt->tmap[i].map_ent[j] >> 31) ? "discontinuity" : "");
      }
    }
  } else {
    GST_WARNING_OBJECT (src, "no vts_tmapt - seeking will suck");
  }

  gst_dvd_read_src_get_chapter_starts (src);

  return TRUE;

  /* ERRORS */
invalid_title:
  {
    GST_WARNING_OBJECT (src, "Invalid title %d (only %d available)",
        title, num_titles);
    return FALSE;
  }
ifo_open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD title %d"), title_set_nr),
        ("ifoOpen(%d) failed: %s", title_set_nr, g_strerror (errno)));
    return FALSE;
  }
title_open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD title %d"), title_set_nr),
        ("Can't open title VOBS (VTS_%02d_1.VOB)", title_set_nr));
    return FALSE;
  }
commands_only_pgc:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD title %d. Interactive titles are not supported "
                "by this element"), title_set_nr),
        ("Commands-only PGC, not supported, use rsndvdbin"));
    return FALSE;
  }
}

/* FIXME: double-check this function, compare against original */
static gint
gst_dvd_read_src_get_next_cell (GstDvdReadSrc * src, pgc_t * pgc, gint cell)
{
  /* Check if we're entering an angle block. */
  if (pgc->cell_playback[cell].block_type != BLOCK_TYPE_ANGLE_BLOCK)
    return (cell + 1);

  while (pgc->cell_playback[cell].block_mode != BLOCK_MODE_LAST_CELL)
    ++cell;

  return cell + 1;
}

/* Returns true if the pack is a NAV pack */
static gboolean
gst_dvd_read_src_is_nav_pack (const guint8 * data, gint lbn, dsi_t * dsi_pack)
{
  if (GST_READ_UINT32_BE (data + 0x26) != 0x000001BF)
    return FALSE;

  /* Check that this is substream 0 (PCI) */
  if (data[0x2c] != 0)
    return FALSE;

  if (GST_READ_UINT32_BE (data + 0x400) != 0x000001BF)
    return FALSE;

  /* Check that this is substream 1 (DSI) */
  if (data[0x406] != 1)
    return FALSE;

  /* Check sizes of PCI and DSI packets */
  if (GST_READ_UINT16_BE (data + 0x2a) != 0x03d4)
    return FALSE;

  if (GST_READ_UINT16_BE (data + 0x404) != 0x03fa)
    return FALSE;

  /* Read the DSI packet into the provided struct and check it */
  navRead_DSI (dsi_pack, (unsigned char *) data + DSI_START_BYTE);
  if (lbn != dsi_pack->dsi_gi.nv_pck_lbn)
    return FALSE;

  return TRUE;
}

/* find time for sector from index, returns NONE if there is no exact match */
static GstClockTime
gst_dvd_read_src_get_time_for_sector (GstDvdReadSrc * src, guint sector)
{
  gint i, j;

  if (src->vts_tmapt == NULL || src->vts_tmapt->nr_of_tmaps == 0)
    return GST_CLOCK_TIME_NONE;

  for (i = 0; i < src->vts_tmapt->nr_of_tmaps; ++i) {
    for (j = 0; j < src->vts_tmapt->tmap[i].nr_of_entries; ++j) {
      if ((src->vts_tmapt->tmap[i].map_ent[j] & 0x7fffffff) == sector)
        return (guint64) src->vts_tmapt->tmap[i].tmu * (j + 1) * GST_SECOND;
    }
  }

  if (sector == 0)
    return (GstClockTime) 0;

  return GST_CLOCK_TIME_NONE;
}

/* returns the sector in the index at (or before) the given time, or -1 */
static gint
gst_dvd_read_src_get_sector_from_time (GstDvdReadSrc * src, GstClockTime ts)
{
  gint sector, j;

  if (src->vts_tmapt == NULL || src->vts_tmapt->nr_of_tmaps < src->ttn)
    return -1;

  sector = 0;
  for (j = 0; j < src->vts_tmapt->tmap[src->ttn - 1].nr_of_entries; ++j) {
    GstClockTime entry_time;

    entry_time =
        (guint64) src->vts_tmapt->tmap[src->ttn - 1].tmu * (j + 1) * GST_SECOND;
    if (entry_time <= ts) {
      sector = src->vts_tmapt->tmap[src->ttn - 1].map_ent[j] & 0x7fffffff;
    }
    if (entry_time >= ts) {
      return sector;
    }
  }

  if (ts == 0)
    return 0;

  return -1;
}

typedef enum
{
  GST_DVD_READ_OK = 0,
  GST_DVD_READ_ERROR = -1,
  GST_DVD_READ_EOS = -2,
  GST_DVD_READ_AGAIN = -3
} GstDvdReadReturn;

static GstDvdReadReturn
gst_dvd_read_src_read (GstDvdReadSrc * src, gint angle, gint new_seek,
    GstBuffer ** p_buf)
{
  GstBuffer *buf;
  GstSegment *seg;
  guint8 oneblock[DVD_VIDEO_LB_LEN];
  dsi_t dsi_pack;
  guint next_vobu, cur_output_size;
  gint len;
  gint retries;
  gint64 next_time;
  GstMapInfo map;

  seg = &(GST_BASE_SRC (src)->segment);

  /* playback by cell in this pgc, starting at the cell for our chapter */
  if (new_seek)
    src->cur_cell = src->start_cell;

again:

  if (src->cur_cell >= src->last_cell) {
    /* advance to next chapter */
    if (src->chapter == (src->num_chapters - 1) ||
        (seg->format == chapter_format && seg->stop != -1 &&
            src->chapter == (seg->stop - 1))) {
      GST_DEBUG_OBJECT (src, "end of chapter segment");
      goto eos;
    }

    GST_INFO_OBJECT (src, "end of chapter %d, switch to next",
        src->chapter + 1);

    ++src->chapter;
    gst_dvd_read_src_goto_chapter (src, src->chapter);

    return GST_DVD_READ_AGAIN;
  }

  if (src->new_cell || new_seek) {
    if (!new_seek) {
      src->cur_cell = src->next_cell;
      if (src->cur_cell >= src->last_cell) {
        GST_LOG_OBJECT (src, "last cell in chapter");
        goto again;
      }
    }

    /* take angle into account */
    if (src->cur_pgc->cell_playback[src->cur_cell].block_type
        == BLOCK_TYPE_ANGLE_BLOCK)
      src->cur_cell += angle;

    /* calculate next cell */
    src->next_cell =
        gst_dvd_read_src_get_next_cell (src, src->cur_pgc, src->cur_cell);

    /* we loop until we're out of this cell */
    src->cur_pack = src->cur_pgc->cell_playback[src->cur_cell].first_sector;
    src->new_cell = FALSE;
    GST_DEBUG_OBJECT (src, "Starting new cell %d @ pack %d", src->cur_cell,
        src->cur_pack);
  }

  if (src->cur_pack >= src->cur_pgc->cell_playback[src->cur_cell].last_sector) {
    src->new_cell = TRUE;
    GST_LOG_OBJECT (src, "Beyond last sector for cell %d, going to next cell",
        src->cur_cell);
    return GST_DVD_READ_AGAIN;
  }

  /* read NAV packet */
  retries = 0;
nav_retry:
  retries++;

  len = DVDReadBlocks (src->dvd_title, src->cur_pack, 1, oneblock);
  if (len != 1)
    goto read_error;

  if (!gst_dvd_read_src_is_nav_pack (oneblock, src->cur_pack, &dsi_pack)) {
    GST_LOG_OBJECT (src, "Skipping nav packet @ pack %d", src->cur_pack);
    src->cur_pack++;

    if (retries < 2000) {
      goto nav_retry;
    } else {
      GST_LOG_OBJECT (src, "No nav packet @ pack %d after 2000 blocks",
          src->cur_pack);
      goto read_error;
    }
  }

  /* determine where we go next. These values are the ones we
   * mostly care about */
  cur_output_size = dsi_pack.dsi_gi.vobu_ea + 1;

  /* If we're not at the end of this cell, we can determine the next
   * VOBU to display using the VOBU_SRI information section of the
   * DSI.  Using this value correctly follows the current angle,
   * avoiding the doubled scenes in The Matrix, and makes our life
   * really happy.
   *
   * Otherwise, we set our next address past the end of this cell to
   * force the code above to go to the next cell in the program. */
  if (dsi_pack.vobu_sri.next_vobu != SRI_END_OF_CELL) {
    next_vobu = src->cur_pack + (dsi_pack.vobu_sri.next_vobu & 0x7fffffff);
  } else {
    next_vobu = src->cur_pgc->cell_playback[src->cur_cell].last_sector + 1;
  }

  g_assert (cur_output_size < 1024);

  /* create the buffer (TODO: use buffer pool?) */
  buf =
      gst_buffer_new_allocate (NULL, cur_output_size * DVD_VIDEO_LB_LEN, NULL);

  GST_LOG_OBJECT (src, "Going to read %u sectors @ pack %d", cur_output_size,
      src->cur_pack);

  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  /* read in and output cursize packs */
  len =
      DVDReadBlocks (src->dvd_title, src->cur_pack, cur_output_size, map.data);

  if (len != cur_output_size)
    goto block_read_error;

  gst_buffer_unmap (buf, &map);
  gst_buffer_resize (buf, 0, cur_output_size * DVD_VIDEO_LB_LEN);
  /* GST_BUFFER_OFFSET (buf) = priv->cur_pack * DVD_VIDEO_LB_LEN; */
  GST_BUFFER_TIMESTAMP (buf) =
      gst_dvd_read_src_get_time_for_sector (src, src->cur_pack);

  *p_buf = buf;

  GST_LOG_OBJECT (src, "Read %u sectors", cur_output_size);

  src->cur_pack = next_vobu;

  next_time = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (next_time) && seg->format == GST_FORMAT_TIME &&
      GST_CLOCK_TIME_IS_VALID (seg->stop) &&
      next_time > seg->stop + 5 * GST_SECOND) {
    GST_DEBUG_OBJECT (src, "end of TIME segment");
    goto eos;
  }

  return GST_DVD_READ_OK;

  /* ERRORS */
eos:
  {
    GST_INFO_OBJECT (src, "Reached end-of-segment/stream - EOS");
    return GST_DVD_READ_EOS;
  }
read_error:
  {
    GST_ERROR_OBJECT (src, "Read failed for block %d", src->cur_pack);
    return GST_DVD_READ_ERROR;
  }
block_read_error:
  {
    GST_ERROR_OBJECT (src, "Read failed for %d blocks at %d",
        cur_output_size, src->cur_pack);
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);
    return GST_DVD_READ_ERROR;
  }
}

/* we don't cache the result on purpose */
static gboolean
gst_dvd_read_descrambler_available (void)
{
  GModule *module;
  gpointer sym;
  gsize res;

  module = g_module_open ("libdvdcss", 0);
  if (module != NULL) {
    res = g_module_symbol (module, "dvdcss_open", &sym);
    g_module_close (module);
  } else {
    res = FALSE;
  }

  return res;
}

static GstFlowReturn
gst_dvd_read_src_create (GstPushSrc * pushsrc, GstBuffer ** p_buf)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (pushsrc);
  GstPad *srcpad;
  gint res;

  g_return_val_if_fail (src->dvd != NULL, GST_FLOW_ERROR);

  srcpad = GST_BASE_SRC (src)->srcpad;

  if (src->need_newsegment) {
    GstSegment seg;

    gst_segment_init (&seg, GST_FORMAT_BYTES);
    seg.start = src->cur_pack * DVD_VIDEO_LB_LEN;
    seg.stop = -1;
    seg.time = 0;
    gst_pad_push_event (srcpad, gst_event_new_segment (&seg));
    src->need_newsegment = FALSE;
  }

  if (src->new_seek) {
    gst_dvd_read_src_goto_title (src, src->title, src->angle);
    gst_dvd_read_src_goto_chapter (src, src->chapter);

    src->new_seek = FALSE;
    src->change_cell = TRUE;
  }

  if (src->title_lang_event_pending) {
    gst_pad_push_event (srcpad, src->title_lang_event_pending);
    src->title_lang_event_pending = NULL;
  }

  if (src->pending_clut_event) {
    gst_pad_push_event (srcpad, src->pending_clut_event);
    src->pending_clut_event = NULL;
  }

  /* read it in */
  do {
    res = gst_dvd_read_src_read (src, src->angle, src->change_cell, p_buf);
  } while (res == GST_DVD_READ_AGAIN);

  switch (res) {
    case GST_DVD_READ_ERROR:{
      /* FIXME: figure out a way to detect if scrambling is the problem */
      if (!gst_dvd_read_descrambler_available ()) {
        GST_ELEMENT_ERROR (src, RESOURCE, READ,
            (_("Could not read DVD. This may be because the DVD is encrypted "
                    "and a DVD decryption library is not installed.")), (NULL));
      } else {
        GST_ELEMENT_ERROR (src, RESOURCE, READ, (_("Could not read DVD.")),
            (NULL));
      }
      return GST_FLOW_ERROR;
    }
    case GST_DVD_READ_EOS:{
      return GST_FLOW_EOS;
    }
    case GST_DVD_READ_OK:{
      src->change_cell = FALSE;
      return GST_FLOW_OK;
    }
    default:
      break;
  }

  g_return_val_if_reached (GST_FLOW_EOS);
}

static void
gst_dvd_read_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (object);
  gboolean started;

  GST_OBJECT_LOCK (src);
  started = GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_FLAG_STARTED);

  switch (prop_id) {
    case ARG_DEVICE:{
      if (started) {
        g_warning ("%s: property '%s' needs to be set before the device is "
            "opened", GST_ELEMENT_NAME (src), pspec->name);
        break;
      }

      g_free (src->location);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL) {
        src->location = g_strdup ("/dev/dvd");
      } else {
        src->location = g_strdup (g_value_get_string (value));
      }
      break;
    }
    case ARG_TITLE:
      src->uri_title = g_value_get_int (value);
      if (started) {
        src->title = src->uri_title - 1;
        src->new_seek = TRUE;
      }
      break;
    case ARG_CHAPTER:
      src->uri_chapter = g_value_get_int (value);
      if (started) {
        src->chapter = src->uri_chapter - 1;
        src->new_seek = TRUE;
      }
      break;
    case ARG_ANGLE:
      src->uri_angle = g_value_get_int (value);
      if (started) {
        src->angle = src->uri_angle - 1;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (src);
}

static void
gst_dvd_read_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (object);

  GST_OBJECT_LOCK (src);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, src->location);
      break;
    case ARG_TITLE:
      g_value_set_int (value, src->uri_title);
      break;
    case ARG_CHAPTER:
      g_value_set_int (value, src->uri_chapter);
      break;
    case ARG_ANGLE:
      g_value_set_int (value, src->uri_angle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (src);
}

static gboolean
gst_dvd_read_src_get_size (GstDvdReadSrc * src, gint64 * size)
{
  gboolean ret = FALSE;

  if (src->dvd_title) {
    gssize blocks;

    blocks = DVDFileSize (src->dvd_title);
    if (blocks >= 0) {
      *size = (gint64) blocks *DVD_VIDEO_LB_LEN;

      ret = TRUE;
    } else {
      GST_WARNING_OBJECT (src, "DVDFileSize(%p) failed!", src->dvd_title);
    }
  }

  return ret;
}

/*** Querying and seeking ***/

static gboolean
gst_dvd_read_src_handle_seek_event (GstDvdReadSrc * src, GstEvent * event)
{
  GstSeekFlags flags;
  GstSeekType cur_type, end_type;
  gint64 new_off, total;
  GstFormat format;
  GstPad *srcpad;
  gboolean query_ok;
  gdouble rate;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &new_off,
      &end_type, NULL);

  if (rate <= 0.0) {
    GST_DEBUG_OBJECT (src, "cannot do backwards playback yet");
    return FALSE;
  }

  if (end_type != GST_SEEK_TYPE_NONE) {
    if ((format != chapter_format && format != GST_FORMAT_TIME) ||
        end_type != GST_SEEK_TYPE_SET) {
      GST_DEBUG_OBJECT (src, "end seek type not supported");
      return FALSE;
    }
  }

  if (cur_type != GST_SEEK_TYPE_SET) {
    GST_DEBUG_OBJECT (src, "only SEEK_TYPE_SET is supported");
    return FALSE;
  }

  if (format == angle_format) {
    GST_OBJECT_LOCK (src);
    if (new_off < 0 || new_off >= src->num_angles) {
      GST_OBJECT_UNLOCK (src);
      GST_DEBUG_OBJECT (src, "invalid angle %d, only %d available",
          src->num_angles, src->num_angles);
      return FALSE;
    }
    src->angle = (gint) new_off;
    GST_OBJECT_UNLOCK (src);
    GST_DEBUG_OBJECT (src, "switched to angle %d", (gint) new_off + 1);
    return TRUE;
  }

  if (format != chapter_format && format != title_format &&
      format != GST_FORMAT_BYTES && format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (src, "unsupported seek format %d (%s)", format,
        gst_format_get_name (format));
    return FALSE;
  }

  if (format == GST_FORMAT_BYTES) {
    GST_DEBUG_OBJECT (src, "Requested seek to byte %" G_GUINT64_FORMAT,
        new_off);
  } else if (format == GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (src, "Requested seek to time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (new_off));
    if (gst_dvd_read_src_get_sector_from_time (src, new_off) < 0) {
      GST_DEBUG_OBJECT (src, "Can't find sector for requested time");
      return FALSE;
    }
  }

  srcpad = GST_BASE_SRC_PAD (src);

  /* check whether the seek looks reasonable (ie within possible range) */
  if (format == GST_FORMAT_BYTES) {
    GST_OBJECT_LOCK (src);
    query_ok = gst_dvd_read_src_get_size (src, &total);
    GST_OBJECT_UNLOCK (src);
  } else {
    query_ok = gst_pad_query_duration (srcpad, format, &total);
  }

  if (!query_ok) {
    GST_DEBUG_OBJECT (src, "Failed to query duration in format %s",
        gst_format_get_name (format));
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "Total      %s: %12" G_GINT64_FORMAT,
      gst_format_get_name (format), total);
  GST_DEBUG_OBJECT (src, "Seek to    %s: %12" G_GINT64_FORMAT,
      gst_format_get_name (format), new_off);

  if (new_off >= total) {
    GST_DEBUG_OBJECT (src, "Seek position out of range");
    return FALSE;
  }

  /* set segment to seek format; this allows us to use the do_seek
   * virtual function and let the base source handle all the tricky
   * stuff for us. We don't use the segment internally anyway */
  /* FIXME: can't take the stream lock here - what to do? */
  GST_OBJECT_LOCK (src);
  GST_BASE_SRC (src)->segment.format = format;
  GST_BASE_SRC (src)->segment.start = 0;
  GST_BASE_SRC (src)->segment.stop = total;
  GST_BASE_SRC (src)->segment.duration = total;
  GST_OBJECT_UNLOCK (src);

  return GST_BASE_SRC_CLASS (parent_class)->event (GST_BASE_SRC (src), event);
}

static void
gst_dvd_read_src_get_sector_bounds (GstDvdReadSrc * src, gint * first,
    gint * last)
{
  gint c1, c2, tmp;
  cur_title_get_chapter_bounds (src, 0, &c1, &tmp);
  cur_title_get_chapter_bounds (src, src->num_chapters - 1, &tmp, &c2);
  *first = src->cur_pgc->cell_playback[c1].first_sector;
  *last = src->cur_pgc->cell_playback[c2].last_sector;
}

static gboolean
gst_dvd_read_src_do_seek (GstBaseSrc * basesrc, GstSegment * s)
{
  GstDvdReadSrc *src;

  src = GST_DVD_READ_SRC (basesrc);

  GST_DEBUG_OBJECT (src, "Seeking to %s: %12" G_GINT64_FORMAT,
      gst_format_get_name (s->format), s->position);

  if (s->format == sector_format || s->format == GST_FORMAT_BYTES
      || s->format == GST_FORMAT_TIME) {
    guint old;

    old = src->cur_pack;

    if (s->format == sector_format) {
      gint first, last;
      gst_dvd_read_src_get_sector_bounds (src, &first, &last);
      GST_DEBUG_OBJECT (src, "Format is sector, seeking to %" G_GINT64_FORMAT,
          s->position);
      src->cur_pack = s->position;
      if (src->cur_pack < first)
        src->cur_pack = first;
      if (src->cur_pack > last)
        src->cur_pack = last;
    } else if (s->format == GST_FORMAT_TIME) {
      gint sector;
      GST_DEBUG_OBJECT (src, "Format is time");

      sector = gst_dvd_read_src_get_sector_from_time (src, s->position);

      GST_DEBUG_OBJECT (src, "Time %" GST_TIME_FORMAT " => sector %d",
          GST_TIME_ARGS (s->position), sector);

      /* really shouldn't happen, we've checked this earlier ... */
      g_return_val_if_fail (sector >= 0, FALSE);

      src->cur_pack = sector;
    } else {
      /* byte format */
      gint first, last;
      gst_dvd_read_src_get_sector_bounds (src, &first, &last);
      GST_DEBUG_OBJECT (src, "Format is byte");
      src->cur_pack = s->position / DVD_VIDEO_LB_LEN;
      if (((gint64) src->cur_pack * DVD_VIDEO_LB_LEN) != s->position) {
        GST_LOG_OBJECT (src, "rounded down offset %" G_GINT64_FORMAT " => %"
            G_GINT64_FORMAT, s->position,
            (gint64) src->cur_pack * DVD_VIDEO_LB_LEN);
      }
      src->cur_pack += first;
    }

    if (!gst_dvd_read_src_goto_sector (src, src->angle)) {
      GST_DEBUG_OBJECT (src, "seek to sector 0x%08x failed", src->cur_pack);
      src->cur_pack = old;
      return FALSE;
    }

    GST_LOG_OBJECT (src, "seek to sector 0x%08x ok", src->cur_pack);
  } else if (s->format == chapter_format) {
    if (!gst_dvd_read_src_goto_chapter (src, (gint) s->position)) {
      GST_DEBUG_OBJECT (src, "seek to chapter %d failed",
          (gint) s->position + 1);
      return FALSE;
    }
    GST_INFO_OBJECT (src, "seek to chapter %d ok", (gint) s->position + 1);
    src->chapter = s->position;
  } else if (s->format == title_format) {
    if (!gst_dvd_read_src_goto_title (src, (gint) s->position, src->angle) ||
        !gst_dvd_read_src_goto_chapter (src, 0)) {
      GST_DEBUG_OBJECT (src, "seek to title %d failed", (gint) s->position);
      return FALSE;
    }
    src->title = (gint) s->position;
    src->chapter = 0;
    GST_INFO_OBJECT (src, "seek to title %d ok", src->title + 1);
  } else {
    g_return_val_if_reached (FALSE);
  }

  src->need_newsegment = TRUE;
  return TRUE;
}

static gboolean
gst_dvd_read_src_src_event (GstBaseSrc * basesrc, GstEvent * event)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (basesrc);
  gboolean res;

  GST_LOG_OBJECT (src, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_dvd_read_src_handle_seek_event (src, event);
      break;
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->event (basesrc, event);
      break;
  }

  return res;
}

static GstEvent *
gst_dvd_read_src_make_clut_change_event (GstDvdReadSrc * src,
    const guint * clut)
{
  GstStructure *structure;
  gchar name[16];
  gint i;

  structure = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-spu-clut-change", NULL);

  /* Create a separate field for each value in the table. */
  for (i = 0; i < 16; i++) {
    g_snprintf (name, sizeof (name), "clut%02d", i);
    gst_structure_set (structure, name, G_TYPE_INT, (int) clut[i], NULL);
  }

  /* Create the DVD event and put the structure into it. */
  return gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, structure);
}

static gint64
gst_dvd_read_src_convert_timecode (dvd_time_t * time)
{
  gint64 ret_time;
  const gint64 one_hour = 3600 * GST_SECOND;
  const gint64 one_min = 60 * GST_SECOND;

  g_return_val_if_fail ((time->hour >> 4) < 0xa
      && (time->hour & 0xf) < 0xa, -1);
  g_return_val_if_fail ((time->minute >> 4) < 0x7
      && (time->minute & 0xf) < 0xa, -1);
  g_return_val_if_fail ((time->second >> 4) < 0x7
      && (time->second & 0xf) < 0xa, -1);

  ret_time = ((time->hour >> 4) * 10 + (time->hour & 0xf)) * one_hour;
  ret_time += ((time->minute >> 4) * 10 + (time->minute & 0xf)) * one_min;
  ret_time += ((time->second >> 4) * 10 + (time->second & 0xf)) * GST_SECOND;

  return ret_time;
}

static gboolean
gst_dvd_read_src_do_duration_query (GstDvdReadSrc * src, GstQuery * query)
{
  GstFormat format;
  gint64 val;

  gst_query_parse_duration (query, &format, NULL);

  switch (format) {
    case GST_FORMAT_TIME:{
      if (src->cur_pgc == NULL)
        return FALSE;
      val = gst_dvd_read_src_convert_timecode (&src->cur_pgc->playback_time);
      if (val < 0)
        return FALSE;
      break;
    }
    case GST_FORMAT_BYTES:{
      if (!gst_dvd_read_src_get_size (src, &val))
        return FALSE;
      break;
    }
    default:{
      if (format == sector_format) {
        val = DVDFileSize (src->dvd_title);
      } else if (format == title_format) {
        val = src->tt_srpt->nr_of_srpts;
      } else if (format == chapter_format) {
        val = src->num_chapters;
      } else if (format == angle_format) {
        val = src->tt_srpt->title[src->title].nr_of_angles;
      } else {
        GST_DEBUG_OBJECT (src, "Don't know how to handle format %d (%s)",
            format, gst_format_get_name (format));
        return FALSE;
      }
      break;
    }
  }

  GST_LOG_OBJECT (src, "duration = %" G_GINT64_FORMAT " %s", val,
      gst_format_get_name (format));

  gst_query_set_duration (query, format, val);
  return TRUE;
}

static gboolean
gst_dvd_read_src_do_position_query (GstDvdReadSrc * src, GstQuery * query)
{
  GstFormat format;
  gint64 val;

  gst_query_parse_position (query, &format, NULL);

  switch (format) {
    case GST_FORMAT_BYTES:{
      val = (gint64) src->cur_pack * DVD_VIDEO_LB_LEN;
      break;
    }
    default:{
      if (format == sector_format) {
        val = src->cur_pack;
      } else if (format == title_format) {
        val = src->title;
      } else if (format == chapter_format) {
        val = src->chapter;
      } else if (format == angle_format) {
        val = src->angle;
      } else {
        GST_DEBUG_OBJECT (src, "Don't know how to handle format %d (%s)",
            format, gst_format_get_name (format));
        return FALSE;
      }
      break;
    }
  }

  GST_LOG_OBJECT (src, "position = %" G_GINT64_FORMAT " %s", val,
      gst_format_get_name (format));

  gst_query_set_position (query, format, val);
  return TRUE;
}

static gboolean
gst_dvd_read_src_do_convert_query (GstDvdReadSrc * src, GstQuery * query)
{
  GstFormat src_format, dest_format;
  gboolean ret = FALSE;
  gint64 src_val, dest_val = -1;

  gst_query_parse_convert (query, &src_format, &src_val, &dest_format, NULL);

  if (src_format == dest_format) {
    dest_val = src_val;
    ret = TRUE;
    goto done;
  }

  /* Formats to consider: TIME, DEFAULT, BYTES, title, chapter, sector.
   * Note: title and chapter are counted as starting from 0 here, just like
   * in the context of seek events. Another note: DEFAULT format is undefined */

  if (src_format == GST_FORMAT_BYTES) {
    src_format = sector_format;
    src_val /= DVD_VIDEO_LB_LEN;
  }

  if (src_format == sector_format) {
    /* SECTOR => xyz */
    if (dest_format == GST_FORMAT_TIME && src_val < G_MAXUINT) {
      dest_val = gst_dvd_read_src_get_time_for_sector (src, (guint) src_val);
      ret = (dest_val >= 0);
    } else if (dest_format == GST_FORMAT_BYTES) {
      dest_val = src_val * DVD_VIDEO_LB_LEN;
      ret = TRUE;
    } else {
      ret = FALSE;
    }
  } else if (src_format == title_format) {
    /* TITLE => xyz */
    if (dest_format == GST_FORMAT_TIME) {
      /* not really true, but we use this to trick the base source into
       * handling seeks in title-format for us (the source won't know that
       * we changed the title in this case) (changing titles should really
       * be done with an interface rather than a seek, but for now we're
       * stuck with this mechanism. Fix in 0.11) */
      dest_val = (GstClockTime) 0;
      ret = TRUE;
    } else {
      ret = FALSE;
    }
  } else if (src_format == chapter_format) {
    /* CHAPTER => xyz */
    if (dest_format == GST_FORMAT_TIME) {
      if (src->num_chapters >= 0 && src_val < src->num_chapters) {
        dest_val = src->chapter_starts[src_val];
        ret = TRUE;
      }
    } else if (dest_format == sector_format) {
    } else {
      ret = FALSE;
    }
  } else if (src_format == GST_FORMAT_TIME) {
    /* TIME => xyz */
    if (dest_format == sector_format || dest_format == GST_FORMAT_BYTES) {
      dest_val = gst_dvd_read_src_get_sector_from_time (src, src_val);
      ret = (dest_val >= 0);
      if (dest_format == GST_FORMAT_BYTES)
        dest_val *= DVD_VIDEO_LB_LEN;
    } else if (dest_format == chapter_format) {
      if (src->chapter_starts != NULL) {
        gint i;

        for (i = src->num_chapters - 1; i >= 0; --i) {
          if (src->chapter_starts && src->chapter_starts[i] >= src_val) {
            dest_val = i;
            ret = TRUE;
            break;
          }
        }
      } else {
        ret = FALSE;
      }
    } else {
      ret = FALSE;
    }
  } else {
    ret = FALSE;
  }

done:

  if (ret) {
    gst_query_set_convert (query, src_format, src_val, dest_format, dest_val);
  }

  return ret;
}

static gboolean
gst_dvd_read_src_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (basesrc);
  gboolean res = TRUE;

  GST_LOG_OBJECT (src, "handling %s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      GST_OBJECT_LOCK (src);
      if (GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_FLAG_STARTED)) {
        res = gst_dvd_read_src_do_duration_query (src, query);
      } else {
        GST_DEBUG_OBJECT (src, "query failed: not started");
        res = FALSE;
      }
      GST_OBJECT_UNLOCK (src);
      break;
    case GST_QUERY_POSITION:
      GST_OBJECT_LOCK (src);
      if (GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_FLAG_STARTED)) {
        res = gst_dvd_read_src_do_position_query (src, query);
      } else {
        GST_DEBUG_OBJECT (src, "query failed: not started");
        res = FALSE;
      }
      GST_OBJECT_UNLOCK (src);
      break;
    case GST_QUERY_CONVERT:
      GST_OBJECT_LOCK (src);
      if (GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_FLAG_STARTED)) {
        res = gst_dvd_read_src_do_convert_query (src, query);
      } else {
        GST_DEBUG_OBJECT (src, "query failed: not started");
        res = FALSE;
      }
      GST_OBJECT_UNLOCK (src);
      break;
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
      break;
  }

  return res;
}

static gboolean
gst_dvd_read_src_goto_sector (GstDvdReadSrc * src, int angle)
{
  gint seek_to = src->cur_pack;
  gint chapter, next, cur, i;

  /* retrieve position */
  src->cur_pack = 0;
  GST_DEBUG_OBJECT (src, "Goto sector %d, angle %d, within %d chapters",
      seek_to, angle, src->num_chapters);

  for (i = 0; i < src->num_chapters; i++) {
    gint c1, c2;

    cur_title_get_chapter_bounds (src, i, &c1, &c2);
    GST_DEBUG_OBJECT (src, " Looking in chapter %d, bounds: %d %d", i, c1, c2);

    for (next = cur = c1; cur < c2;) {
      gint first = src->cur_pgc->cell_playback[cur].first_sector;
      gint last = src->cur_pgc->cell_playback[cur].last_sector;
      GST_DEBUG_OBJECT (src, "Cell %d sector bounds: %d %d", cur, first, last);
      cur = next;
      if (src->cur_pgc->cell_playback[cur].block_type == BLOCK_TYPE_ANGLE_BLOCK)
        cur += angle;
      next = gst_dvd_read_src_get_next_cell (src, src->cur_pgc, cur);
      /* seeking to 0 should end up at first chapter in any case */
      if ((seek_to >= first && seek_to <= last) || (seek_to == 0 && i == 0)) {
        GST_DEBUG_OBJECT (src, "Seek target found in chapter %d", i);
        chapter = i;
        goto done;
      }
    }
  }

  GST_DEBUG_OBJECT (src, "Seek to sector %u failed", seek_to);

  return FALSE;

done:
  {
    /* so chapter $chapter and cell $cur contain our sector
     * of interest. Let's go there! */
    GST_INFO_OBJECT (src, "Seek succeeded, going to chapter %u, cell %u",
        chapter + 1, cur);

    gst_dvd_read_src_goto_chapter (src, chapter);
    src->cur_cell = cur;
    src->next_cell = next;
    src->new_cell = FALSE;
    src->cur_pack = seek_to;

    return TRUE;
  }
}


/*** URI interface ***/

static GstURIType
gst_dvd_read_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_dvd_read_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "dvd", NULL };

  return protocols;
}

static gchar *
gst_dvd_read_src_uri_get_uri (GstURIHandler * handler)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (handler);
  gchar *uri;

  GST_OBJECT_LOCK (src);
  uri = g_strdup_printf ("dvd://%d,%d,%d", src->uri_title, src->uri_chapter,
      src->uri_angle);
  GST_OBJECT_UNLOCK (src);

  return uri;
}

static gboolean
gst_dvd_read_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (handler);

  /* parse out the new t/c/a and seek to them */
  {
    gchar *location = NULL;
    gchar **strs;
    gchar **strcur;
    gint pos = 0;

    location = gst_uri_get_location (uri);

    GST_OBJECT_LOCK (src);

    src->uri_title = 1;
    src->uri_chapter = 1;
    src->uri_angle = 1;

    if (!location)
      goto empty_location;

    strcur = strs = g_strsplit (location, ",", 0);
    while (strcur && *strcur) {
      gint val;

      if (!sscanf (*strcur, "%d", &val))
        break;

      if (val <= 0) {
        g_warning ("Invalid value %d in URI '%s'. Must be 1 or greater",
            val, location);
        break;
      }

      switch (pos) {
        case 0:
          src->uri_title = val;
          break;
        case 1:
          src->uri_chapter = val;
          break;
        case 2:
          src->uri_angle = val;
          break;
      }

      strcur++;
      pos++;
    }

    if (pos > 0 && GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_FLAG_STARTED)) {
      src->title = src->uri_title - 1;
      src->chapter = src->uri_chapter - 1;
      src->angle = src->uri_angle - 1;
      src->new_seek = TRUE;
    }

    g_strfreev (strs);
    g_free (location);

  empty_location:

    GST_OBJECT_UNLOCK (src);
  }

  return TRUE;
}

static void
gst_dvd_read_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_dvd_read_src_uri_get_type;
  iface->get_protocols = gst_dvd_read_src_uri_get_protocols;
  iface->get_uri = gst_dvd_read_src_uri_get_uri;
  iface->set_uri = gst_dvd_read_src_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstgst_dvd_read_src_debug, "dvdreadsrc", 0,
      "DVD reader element based on dvdreadsrc");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  if (!gst_element_register (plugin, "dvdreadsrc", GST_RANK_NONE,
          GST_TYPE_DVD_READ_SRC)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dvdread,
    "Access a DVD with dvdread",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
