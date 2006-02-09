/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2001 Billy Biggs <vektor@dumbterm.net>.
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

/* #include <gst/gst-i18n-plugin.h> */
/* FIXME: remove once GETTEXT_PACKAGE etc. is set */
#define _(s) s

GST_DEBUG_CATEGORY_STATIC (gstgst_dvd_read_src_debug);
#define GST_CAT_DEFAULT (gstgst_dvd_read_src_debug)

static void gst_dvd_read_src_do_init (GType dvdreadsrc_type);

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_TITLE,
  ARG_CHAPTER,
  ARG_ANGLE
};

static GstElementDetails gst_dvd_read_src_details = {
  "DVD Source",
  "Source/File/DVD",
  "Access a DVD title/chapter/angle using libdvdread",
  "Erik Walthinsen <omega@cse.ogi.edu>",
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

GST_BOILERPLATE_FULL (GstDvdReadSrc, gst_dvd_read_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_dvd_read_src_do_init)

     static void gst_dvd_read_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_dvd_read_src_details);
}

static void
gst_dvd_read_src_finalize (GObject * object)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (object);

  g_free (src->location);
  g_free (src->last_uri);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dvd_read_src_init (GstDvdReadSrc * src, GstDvdReadSrcClass * klass)
{
  src->dvd = NULL;
  src->vts_file = NULL;
  src->vmg_file = NULL;
  src->dvd_title = NULL;

  src->location = g_strdup ("/dev/dvd");
  src->last_uri = NULL;
  src->new_seek = TRUE;
  src->new_cell = TRUE;
  src->change_cell = FALSE;
  src->title = 0;
  src->chapter = 0;
  src->angle = 0;

  src->seek_pend = FALSE;
  src->flush_pend = FALSE;
  src->seek_pend_fmt = GST_FORMAT_UNDEFINED;
  src->title_lang_event_pending = NULL;
  src->pending_clut_event = NULL;
}

static void
gst_dvd_read_src_class_init (GstDvdReadSrcClass * klass)
{
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_dvd_read_src_finalize;
  gobject_class->set_property = gst_dvd_read_src_set_property;
  gobject_class->get_property = gst_dvd_read_src_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "DVD device location", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TITLE,
      g_param_spec_int ("title", "title", "title",
          0, 999, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHAPTER,
      g_param_spec_int ("chapter", "chapter", "chapter",
          0, 999, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ANGLE,
      g_param_spec_int ("angle", "angle", "angle",
          0, 999, 0, G_PARAM_READWRITE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_dvd_read_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_dvd_read_src_stop);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_dvd_read_src_src_query);
  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_dvd_read_src_src_event);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_dvd_read_src_create);
}

static gboolean
gst_dvd_read_src_start (GstBaseSrc * basesrc)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (basesrc);

  g_return_val_if_fail (src->location != NULL, FALSE);

  GST_DEBUG_OBJECT (src, "Opening DVD '%s'", src->location);

  src->dvd = DVDOpen (src->location);
  if (src->dvd == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD")),
        ("DVDOpen(%s) failed: %s", src->location, g_strerror (errno)));
    return FALSE;
  }

  /* Load the video manager to find out the information about the titles */
  GST_DEBUG_OBJECT (src, "Loading VMG info");

  src->vmg_file = ifoOpen (src->dvd, 0);
  if (!src->vmg_file) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD")),
        ("ifoOpen() failed: %s", g_strerror (errno)));
    return FALSE;
  }

  src->tt_srpt = src->vmg_file->tt_srpt;

  src->seek_pend_fmt = title_format;
  src->seek_pend = TRUE;

  return TRUE;
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
  src->flush_pend = FALSE;
  src->seek_pend = FALSE;
  src->seek_pend_fmt = GST_FORMAT_UNDEFINED;
  if (src->title_lang_event_pending) {
    gst_event_unref (src->title_lang_event_pending);
    src->title_lang_event_pending = NULL;
  }
  if (src->pending_clut_event) {
    gst_event_unref (src->pending_clut_event);
    src->pending_clut_event = NULL;
  }

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

  if (chapter == (src->num_chapters - 1)) {
    *p_last_cell = pgc->nr_of_cells;
  } else {
    pgn_next_ch = src->vts_ptt_srpt->title[src->ttn - 1].ptt[chapter + 1].pgn;
    *p_last_cell = pgc->program_map[pgn_next_ch - 1] - 1;
  }
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

  GST_LOG_OBJECT (src, "Opened chapter %d - cell %d-%d", chapter,
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

static gboolean
gst_dvd_read_src_goto_title (GstDvdReadSrc * src, gint title, gint angle)
{
  GstStructure *s;
  gchar lang_code[3] = { '\0', '\0', '\0' }, *t;
  gint title_set_nr;
  gint num_titles;
  gint num_angles;
  gint i;

  /* make sure our title number is valid */
  num_titles = src->tt_srpt->nr_of_srpts;
  GST_INFO_OBJECT (src, "There are %d titles on this DVD", num_titles);
  if (title < 0 || title >= num_titles) {
    GST_WARNING_OBJECT (src, "Invalid title %d (only %d available)",
        title, num_titles);
    title = CLAMP (title, 0, num_titles - 1);
  }

  src->num_chapters = src->tt_srpt->title[title].nr_of_ptts;
  GST_INFO_OBJECT (src, "Title %d has %d chapters", title, src->num_chapters);

  /* make sure the angle number is valid for this title */
  num_angles = src->tt_srpt->title[title].nr_of_angles;
  GST_LOG_OBJECT (src, "Title %d has %d angles", title, num_angles);
  if (angle < 0 || angle >= num_angles) {
    GST_WARNING_OBJECT (src, "Invalid angle %d (only %d available)",
        angle, num_angles);
    angle = CLAMP (angle, 0, num_angles - 1);
  }

  /* load the VTS information for the title set our title is in */
  title_set_nr = src->tt_srpt->title[title].title_set_nr;
  src->vts_file = ifoOpen (src->dvd, title_set_nr);
  if (src->vts_file == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD title %d"), title_set_nr),
        ("ifoOpen(%d) failed: %s", title_set_nr, g_strerror (errno)));
    return FALSE;
  }

  src->ttn = src->tt_srpt->title[title].vts_ttn;
  src->vts_ptt_srpt = src->vts_file->vts_ptt_srpt;

  /* we've got enough info, time to open the title set data */
  src->dvd_title = DVDOpenFile (src->dvd, title_set_nr, DVD_READ_TITLE_VOBS);
  if (src->dvd_title == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open DVD title %d"), title_set_nr),
        ("Can't open title VOBS (VTS_%02d_1.VOB)", title_set_nr));
    return FALSE;
  }

  GST_INFO_OBJECT (src, "Opened title %d, angle %d", title, angle);
  src->title = title;
  src->angle = angle;

  /* build event */

  if (src->title_lang_event_pending) {
    gst_event_unref (src->title_lang_event_pending);
    src->title_lang_event_pending = NULL;
  }

  s = gst_structure_new ("application/x-gst-event",
      "event", G_TYPE_STRING, "dvd-lang-codes", NULL);

  /* audio */
  for (i = 0; i < src->vts_file->vtsi_mat->nr_of_vts_audio_streams; i++) {
    const audio_attr_t *a = &src->vts_file->vtsi_mat->vts_audio_attr[i];

    t = g_strdup_printf ("audio-%d-format", i);
    gst_structure_set (s, t, G_TYPE_INT, (int) a->audio_format, NULL);
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
        src->title, i, lang_code, (gint) a->audio_format);
  }

  /* subtitle */
  for (i = 0; i < src->vts_file->vtsi_mat->nr_of_vts_subp_streams; i++) {
    const subp_attr_t *u = &src->vts_file->vtsi_mat->vts_subp_attr[i];

    if (u->type) {
      t = g_strdup_printf ("subtitle-%d-language", i);
      lang_code[0] = (u->lang_code >> 8) & 0xff;
      lang_code[1] = u->lang_code & 0xff;
      gst_structure_set (s, t, G_TYPE_STRING, lang_code, NULL);
      g_free (t);
    } else {
      lang_code[0] = '\0';
    }

    GST_INFO_OBJECT (src, "[%02d] Subtitle %02d: lang='%s', format=%d",
        src->title, i, lang_code);
  }

  src->title_lang_event_pending =
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

  return 0;
}

/* FIXME: double-check this function, compare against original */
static gint
gst_dvd_read_src_get_next_cell_for (GstDvdReadSrc * src, gint cell)
{
  /* Check if we're entering an angle block. */
  if (src->cur_pgc->cell_playback[cell].block_type != BLOCK_TYPE_ANGLE_BLOCK)
    return (cell + 1);

  while (src->cur_pgc->cell_playback[cell].block_mode == BLOCK_MODE_LAST_CELL)
    ++cell;

  return cell + 1;              /* really +1? (tpm) */
}

/* Returns true if the pack is a NAV pack.  This check is clearly insufficient,
 * and sometimes we incorrectly think that valid other packs are NAV packs.  I
 * need to make this stronger. */
static gboolean
gst_dvd_read_src_is_nav_pack (const guint8 * buffer)
{
  return (buffer[41] == 0xbf && buffer[1027] == 0xbf);
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
    GstBuffer * buf)
{
  guint8 *data;
  dsi_t dsi_pack;
  guint next_vobu, next_ilvu_start, cur_output_size;
  gint len;

  /* playback by cell in this pgc, starting at the cell for our chapter */
  if (new_seek)
    src->cur_cell = src->start_cell;

again:

  if (src->cur_cell >= src->last_cell) {
    /* advance to next chapter */
    if (src->chapter == (src->num_chapters - 1)) {
      GST_INFO_OBJECT (src, "last chapter done - EOS");
      return GST_DVD_READ_EOS;
    }

    GST_INFO_OBJECT (src, "end of chapter %d, switch to next", src->chapter);

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
    src->next_cell = gst_dvd_read_src_get_next_cell_for (src, src->cur_cell);

    /* we loop until we're out of this cell */
    src->cur_pack = src->cur_pgc->cell_playback[src->cur_cell].first_sector;
    src->new_cell = FALSE;
  }

  if (src->cur_pack >= src->cur_pgc->cell_playback[src->cur_cell].last_sector) {
    src->new_cell = TRUE;
    GST_LOG_OBJECT (src, "Beyond last sector, go to next cell");
    return GST_DVD_READ_AGAIN;
  }

  /* read NAV packet */
  data = GST_BUFFER_DATA (buf);

nav_retry:

  len = DVDReadBlocks (src->dvd_title, src->cur_pack, 1, data);
  if (len == 0) {
    GST_ERROR_OBJECT (src, "Read failed for block %d", src->cur_pack);
    return GST_DVD_READ_ERROR;
  }

  if (!gst_dvd_read_src_is_nav_pack (data)) {
    src->cur_pack++;
    goto nav_retry;
  }

  /* parse the contained dsi packet */
  navRead_DSI (&dsi_pack, &data[DSI_START_BYTE]);
  g_assert (src->cur_pack == dsi_pack.dsi_gi.nv_pck_lbn);

  /* determine where we go next. These values are the ones we
   * mostly care about */
  next_ilvu_start = src->cur_pack + dsi_pack.sml_agli.data[angle].address;
  cur_output_size = dsi_pack.dsi_gi.vobu_ea;

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
    next_vobu = src->cur_pack + cur_output_size + 1;
  }

  g_assert (cur_output_size < 1024);
  ++src->cur_pack;

  /* read in and output cursize packs */
  len = DVDReadBlocks (src->dvd_title, src->cur_pack, cur_output_size, data);
  if (len != cur_output_size) {
    GST_ERROR_OBJECT (src, "Read failed for %d blocks at %d",
        cur_output_size, src->cur_pack);
    return GST_DVD_READ_ERROR;
  }

  GST_BUFFER_SIZE (buf) = cur_output_size * DVD_VIDEO_LB_LEN;
  /* GST_BUFFER_OFFSET (buf) = priv->cur_pack * DVD_VIDEO_LB_LEN; */

  src->cur_pack = next_vobu;

  GST_LOG_OBJECT (src, "Read %u sectors", cur_output_size);
  return GST_DVD_READ_OK;
}

static GstFlowReturn
gst_dvd_read_src_create (GstPushSrc * pushsrc, GstBuffer ** p_buf)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (pushsrc);
  GstBuffer *buf;
  GstPad *srcpad;
  gint res;

  g_return_val_if_fail (src->dvd != NULL, GST_FLOW_ERROR);

  srcpad = GST_BASE_SRC (src)->srcpad;

  /* handle events, if any */
  if (src->seek_pend) {
    if (src->flush_pend) {
      gst_pad_push_event (srcpad, gst_event_new_flush_start ());
      gst_pad_push_event (srcpad, gst_event_new_flush_stop ());
      src->flush_pend = FALSE;
    }

    if (src->seek_pend_fmt != GST_FORMAT_UNDEFINED) {
      if (src->seek_pend_fmt == title_format) {
        gst_dvd_read_src_goto_title (src, src->title, src->angle);
      }
      gst_dvd_read_src_goto_chapter (src, src->chapter);

      src->seek_pend_fmt = GST_FORMAT_UNDEFINED;
    } else {
      if (!gst_dvd_read_src_goto_sector (src, src->angle)) {
        GST_DEBUG_OBJECT (src, "seek to sector failed, going EOS");
        gst_pad_push_event (srcpad, gst_event_new_eos ());
        return GST_FLOW_UNEXPECTED;
      }
    }

    gst_pad_push_event (srcpad,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
            src->cur_pack * DVD_VIDEO_LB_LEN, -1, 0));

    src->seek_pend = FALSE;
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

  /* create the buffer (TODO: use buffer pool?) */
  buf = gst_buffer_new_and_alloc (1024 * DVD_VIDEO_LB_LEN);

  /* read it in */
  do {
    res = gst_dvd_read_src_read (src, src->angle, src->change_cell, buf);
  } while (res == GST_DVD_READ_AGAIN);

  switch (res) {
    case GST_DVD_READ_ERROR:{
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), (NULL));
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }
    case GST_DVD_READ_EOS:{
      GST_INFO_OBJECT (src, "Reached EOS");
      gst_pad_push_event (GST_BASE_SRC (src)->srcpad, gst_event_new_eos ());
      gst_buffer_unref (buf);
      return GST_FLOW_UNEXPECTED;
    }
    case GST_DVD_READ_OK:{
      src->change_cell = FALSE;
      *p_buf = buf;
      return GST_FLOW_OK;
    }
    default:
      break;
  }

  g_return_val_if_reached (GST_FLOW_UNEXPECTED);
}

static void
gst_dvd_read_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (object);

  GST_OBJECT_LOCK (src);

  switch (prop_id) {
    case ARG_DEVICE:{
      if (!GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_STARTED)) {
        g_warning ("%s: property '%s' needs to be set before the device is "
            "opened", GST_ELEMENT_NAME (src), pspec->name);
        break;;
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
      src->title = g_value_get_int (value);
      src->new_seek = TRUE;
      break;
    case ARG_CHAPTER:
      src->chapter = g_value_get_int (value);
      src->new_seek = TRUE;
      break;
    case ARG_ANGLE:
      src->angle = g_value_get_int (value);
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
      g_value_set_int (value, src->title);
      break;
    case ARG_CHAPTER:
      g_value_set_int (value, src->chapter);
      break;
    case ARG_ANGLE:
      g_value_set_int (value, src->angle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (src);
}

/*** Querying and seeking ***/

static gboolean
gst_dvd_read_src_do_seek (GstDvdReadSrc * src, GstEvent * event)
{
  GstSeekFlags flags;
  GstSeekType cur_type, end_type;
  gint64 new_off, total, cur;
  GstFormat format;
  GstPad *srcpad;
  gdouble rate;

  srcpad = GST_BASE_SRC (src)->srcpad;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &new_off,
      &end_type, NULL);

  if (end_type != GST_SEEK_TYPE_NONE) {
    GST_WARNING_OBJECT (src, "End seek type not supported, will be ignored");
  }

  switch (format) {
    case GST_FORMAT_BYTES:{
      new_off /= DVD_VIDEO_LB_LEN;
      format = sector_format;
      break;
    }
    default:{
      if (format != chapter_format &&
          format != sector_format &&
          format != angle_format && format != title_format) {
        GST_DEBUG_OBJECT (src, "Unsupported seek format %d (%s)", format,
            gst_format_get_name (format));
        return FALSE;
      }
      break;
    }
  }

  /* get current offset and length */
  gst_pad_query_duration (srcpad, &format, &total);
  gst_pad_query_position (srcpad, &format, &cur);

  GST_DEBUG_OBJECT (src, "Current %s: %" G_GINT64_FORMAT,
      gst_format_get_name (format), cur);
  GST_DEBUG_OBJECT (src, "Total   %s: %" G_GINT64_FORMAT,
      gst_format_get_name (format), total);

  if (cur == new_off) {
    GST_DEBUG_OBJECT (src, "We're already at that position!");
    return TRUE;
  }

  /* get absolute */
  switch (cur_type) {
    case GST_SEEK_TYPE_SET:
      /* no-op */
      break;
    case GST_SEEK_TYPE_CUR:
      new_off += cur;
      break;
    case GST_SEEK_TYPE_END:
      new_off = total - new_off;
      break;
    default:
      GST_DEBUG_OBJECT (src, "Unsupported seek method");
      return FALSE;
  }

  if (new_off < 0 || new_off >= total) {
    GST_DEBUG_OBJECT (src, "Invalid seek position %" G_GINT64_FORMAT, new_off);
    return FALSE;
  }

  GST_LOG_OBJECT (src, "Seeking to unit %" G_GINT64_FORMAT, new_off);

  GST_OBJECT_LOCK (src);

  if (format == angle_format) {
    src->angle = new_off;
    GST_OBJECT_UNLOCK (src);
    return TRUE;
  }

  if (format == sector_format) {
    src->cur_pack = new_off;
  } else if (format == chapter_format) {
    src->cur_pack = 0;
    src->chapter = new_off;
    src->seek_pend_fmt = format;
  } else if (format == title_format) {
    src->cur_pack = 0;
    src->title = new_off;
    src->chapter = 0;
    src->seek_pend_fmt = format;
  } else {
    g_return_val_if_reached (FALSE);
  }

  /* leave for events */
  src->seek_pend = TRUE;
  if ((flags & GST_SEEK_FLAG_FLUSH) != 0)
    src->flush_pend = TRUE;

  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_dvd_read_src_src_event (GstBaseSrc * basesrc, GstEvent * event)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (basesrc);
  gboolean res;

  GST_DEBUG_OBJECT (src, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_dvd_read_src_do_seek (src, event);
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
      val = DVDFileSize (src->dvd_title) * DVD_VIDEO_LB_LEN;
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
      val = src->cur_pack * DVD_VIDEO_LB_LEN;
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

  gst_query_set_position (query, format, val);
  return TRUE;
}

static gboolean
gst_dvd_read_src_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (basesrc);
  gboolean started;
  gboolean res = TRUE;

  GST_LOG_OBJECT (src, "handling %s query",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  GST_OBJECT_LOCK (src);
  started = (GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_STARTED));
  GST_OBJECT_UNLOCK (src);

  if (!started) {
    GST_DEBUG_OBJECT (src, "query failed: not started");
    return FALSE;
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      GST_OBJECT_LOCK (src);
      res = gst_dvd_read_src_do_duration_query (src, query);
      GST_OBJECT_UNLOCK (src);
      break;
    case GST_QUERY_POSITION:
      GST_OBJECT_LOCK (src);
      res = gst_dvd_read_src_do_position_query (src, query);
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
  gint chapter, sectors, next, cur, i;

  /* retrieve position */
  src->cur_pack = 0;
  for (i = 0; i < src->num_chapters; i++) {
    gint c1, c2;

    cur_title_get_chapter_bounds (src, i, &c1, &c2);

    for (next = cur = c1; cur < c2;) {
      if (next != cur) {
        sectors =
            src->cur_pgc->cell_playback[cur].last_sector -
            src->cur_pgc->cell_playback[cur].first_sector;
        if (src->cur_pack + sectors > seek_to) {
          chapter = i;
          goto done;
        }
        src->cur_pack += sectors;
      }
      cur = next;
      if (src->cur_pgc->cell_playback[cur].block_type == BLOCK_TYPE_ANGLE_BLOCK)
        cur += angle;
      next = gst_dvd_read_src_get_next_cell_for (src, cur);
    }
  }

  GST_DEBUG_OBJECT (src, "Seek to sector %u failed", seek_to);
  return FALSE;

done:
  /* so chapter $chapter and cell $cur contain our sector
   * of interest. Let's go there! */
  GST_INFO_OBJECT (src, "Seek succeeded, going to chapter %u, cell %u",
      chapter, cur);

  gst_dvd_read_src_goto_chapter (src, chapter);
  src->cur_cell = cur;
  src->next_cell = next;
  src->new_cell = FALSE;
  src->cur_pack = seek_to;

  return TRUE;
}


/*** URI interface ***/

static GstURIType
gst_dvd_read_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_dvd_read_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "dvd", NULL };

  return protocols;
}

static const gchar *
gst_dvd_read_src_uri_get_uri (GstURIHandler * handler)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (handler);

  g_free (src->last_uri);
  src->last_uri =
      g_strdup_printf ("dvd://%d,%d,%d", src->title, src->chapter, src->angle);

  return src->last_uri;
}

static gboolean
gst_dvd_read_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstDvdReadSrc *src = GST_DVD_READ_SRC (handler);
  gboolean ret;
  gchar *protocol;

  protocol = gst_uri_get_protocol (uri);
  ret = (protocol != NULL && g_str_equal (protocol, "dvd"));
  g_free (protocol);
  protocol = NULL;

  if (!ret)
    return ret;

  /* parse out the new t/c/a and seek to them */
  {
    gchar *location = NULL;
    gchar **strs;
    gchar **strcur;
    gint pos = 0;

    location = gst_uri_get_location (uri);

    if (!location)
      return ret;

    strcur = strs = g_strsplit (location, ",", 0);
    while (strcur && *strcur) {
      gint val;

      if (!sscanf (*strcur, "%d", &val))
        break;

      switch (pos) {
        case 0:
          if (val != src->title) {
            src->title = val;
            src->new_seek = TRUE;
          }
          break;
        case 1:
          if (val != src->chapter) {
            src->chapter = val;
            src->new_seek = TRUE;
          }
          break;
        case 2:
          src->angle = val;
          break;
      }

      strcur++;
      pos++;
    }

    g_strfreev (strs);
    g_free (location);
  }

  return ret;
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

static void
gst_dvd_read_src_do_init (GType dvdreadsrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_dvd_read_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (dvdreadsrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);

  title_format = gst_format_register ("title", "DVD title");
  angle_format = gst_format_register ("angle", "DVD angle");
  sector_format = gst_format_register ("sector", "DVD sector");
  chapter_format = gst_format_register ("chapter", "DVD chapter");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstgst_dvd_read_src_debug, "dvdreadsrc", 0,
      "DVD reader element based on dvdreadsrc");

  if (!gst_element_register (plugin, "dvdreadsrc", GST_RANK_SECONDARY,
          GST_TYPE_DVD_READ_SRC)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvdread",
    "Access a DVD with dvdread",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
