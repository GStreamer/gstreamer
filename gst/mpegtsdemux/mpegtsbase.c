/*
 * mpegtsbase.c -
 * Copyright (C) 2007 Alessandro Decina
 *               2010 Edward Hervey
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *  Author: Edward Hervey <bilboed@bilboed.com>, Collabora Ltd.
 *
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
 *   Edward Hervey <edward.hervey@collabora.co.uk>
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

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <gst/gst-i18n-plugin.h>
#include "mpegtsbase.h"
#include "gstmpegdesc.h"

/* latency in mseconds */
#define TS_LATENCY 700

#define RUNNING_STATUS_RUNNING 4

GST_DEBUG_CATEGORY_STATIC (mpegts_base_debug);
#define GST_CAT_DEFAULT mpegts_base_debug

static GQuark QUARK_PROGRAMS;
static GQuark QUARK_PROGRAM_NUMBER;
static GQuark QUARK_PID;
static GQuark QUARK_PCR_PID;
static GQuark QUARK_STREAMS;
static GQuark QUARK_STREAM_TYPE;

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts, " "systemstream = (boolean) true ")
    );

enum
{
  PROP_0,
  PROP_PARSE_PRIVATE_SECTIONS,
  /* FILL ME */
};

static void mpegts_base_dispose (GObject * object);
static void mpegts_base_finalize (GObject * object);
static void mpegts_base_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void mpegts_base_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void mpegts_base_free_program (MpegTSBaseProgram * program);
static gboolean mpegts_base_sink_activate (GstPad * pad, GstObject * parent);
static gboolean mpegts_base_sink_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);
static GstFlowReturn mpegts_base_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean mpegts_base_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstStateChangeReturn mpegts_base_change_state (GstElement * element,
    GstStateChange transition);
static gboolean mpegts_base_get_tags_from_eit (MpegTSBase * base,
    GstMpegtsSection * section);
static gboolean mpegts_base_parse_atsc_mgt (MpegTSBase * base,
    GstMpegtsSection * section);
static gboolean remove_each_program (gpointer key, MpegTSBaseProgram * program,
    MpegTSBase * base);

static void
_extra_init (void)
{
  QUARK_PROGRAMS = g_quark_from_string ("programs");
  QUARK_PROGRAM_NUMBER = g_quark_from_string ("program-number");
  QUARK_PID = g_quark_from_string ("pid");
  QUARK_PCR_PID = g_quark_from_string ("pcr-pid");
  QUARK_STREAMS = g_quark_from_string ("streams");
  QUARK_STREAM_TYPE = g_quark_from_string ("stream-type");
}

#define mpegts_base_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (MpegTSBase, mpegts_base, GST_TYPE_ELEMENT,
    _extra_init ());

static void
mpegts_base_class_init (MpegTSBaseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (klass);
  element_class->change_state = mpegts_base_change_state;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = mpegts_base_dispose;
  gobject_class->finalize = mpegts_base_finalize;
  gobject_class->set_property = mpegts_base_set_property;
  gobject_class->get_property = mpegts_base_get_property;

  g_object_class_install_property (gobject_class, PROP_PARSE_PRIVATE_SECTIONS,
      g_param_spec_boolean ("parse-private-sections", "Parse private sections",
          "Parse private sections", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
mpegts_base_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  MpegTSBase *base = GST_MPEGTS_BASE (object);

  switch (prop_id) {
    case PROP_PARSE_PRIVATE_SECTIONS:
      base->parse_private_sections = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
mpegts_base_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  MpegTSBase *base = GST_MPEGTS_BASE (object);

  switch (prop_id) {
    case PROP_PARSE_PRIVATE_SECTIONS:
      g_value_set_boolean (value, base->parse_private_sections);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}


static void
mpegts_base_reset (MpegTSBase * base)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  mpegts_packetizer_clear (base->packetizer);
  memset (base->is_pes, 0, 1024);
  memset (base->known_psi, 0, 1024);

  /* FIXME : Actually these are not *always* know SI streams
   * depending on the variant of mpeg-ts being used. */

  /* Known PIDs : PAT, TSDT, IPMP CIT */
  MPEGTS_BIT_SET (base->known_psi, 0);
  MPEGTS_BIT_SET (base->known_psi, 2);
  MPEGTS_BIT_SET (base->known_psi, 3);
  /* TDT, TOT, ST */
  MPEGTS_BIT_SET (base->known_psi, 0x14);
  /* network synchronization */
  MPEGTS_BIT_SET (base->known_psi, 0x15);

  /* ATSC */
  MPEGTS_BIT_SET (base->known_psi, 0x1ffb);

  if (base->pat) {
    g_ptr_array_unref (base->pat);
    base->pat = NULL;
  }

  gst_segment_init (&base->segment, GST_FORMAT_UNDEFINED);
  base->last_seek_seqnum = (guint32) - 1;

  base->mode = BASE_MODE_STREAMING;
  base->seen_pat = FALSE;
  base->seek_offset = -1;

  base->upstream_live = FALSE;
  base->queried_latency = FALSE;

  g_hash_table_foreach_remove (base->programs, (GHRFunc) remove_each_program,
      base);

  if (klass->reset)
    klass->reset (base);
}

static void
mpegts_base_init (MpegTSBase * base)
{
  base->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_activate_function (base->sinkpad, mpegts_base_sink_activate);
  gst_pad_set_activatemode_function (base->sinkpad,
      mpegts_base_sink_activate_mode);
  gst_pad_set_chain_function (base->sinkpad, mpegts_base_chain);
  gst_pad_set_event_function (base->sinkpad, mpegts_base_sink_event);
  gst_element_add_pad (GST_ELEMENT (base), base->sinkpad);

  base->disposed = FALSE;
  base->packetizer = mpegts_packetizer_new ();
  base->programs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) mpegts_base_free_program);

  base->parse_private_sections = FALSE;
  base->is_pes = g_new0 (guint8, 1024);
  base->known_psi = g_new0 (guint8, 1024);
  base->program_size = sizeof (MpegTSBaseProgram);
  base->stream_size = sizeof (MpegTSBaseStream);

  base->push_data = TRUE;
  base->push_section = TRUE;

  mpegts_base_reset (base);
}

static void
mpegts_base_dispose (GObject * object)
{
  MpegTSBase *base = GST_MPEGTS_BASE (object);

  if (!base->disposed) {
    g_object_unref (base->packetizer);
    base->disposed = TRUE;
    g_free (base->known_psi);
    g_free (base->is_pes);
  }

  if (G_OBJECT_CLASS (parent_class)->dispose)
    G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mpegts_base_finalize (GObject * object)
{
  MpegTSBase *base = GST_MPEGTS_BASE (object);

  if (base->pat) {
    g_ptr_array_unref (base->pat);
    base->pat = NULL;
  }
  g_hash_table_destroy (base->programs);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}


/* returns NULL if no matching descriptor found *
 * otherwise returns a descriptor that needs to *
 * be freed */
const GstMpegtsDescriptor *
mpegts_get_descriptor_from_stream (MpegTSBaseStream * stream, guint8 tag)
{
  GstMpegtsPMTStream *pmt = stream->stream;

  GST_DEBUG ("Searching for tag 0x%02x in stream 0x%04x (stream_type 0x%02x)",
      tag, stream->pid, stream->stream_type);

  return gst_mpegts_find_descriptor (pmt->descriptors, tag);
}

typedef struct
{
  gboolean res;
  guint16 pid;
} PIDLookup;

static void
foreach_pid_in_program (gpointer key, MpegTSBaseProgram * program,
    PIDLookup * lookup)
{
  if (!program->active)
    return;
  if (program->streams[lookup->pid])
    lookup->res = TRUE;
}

static gboolean
mpegts_pid_in_active_programs (MpegTSBase * base, guint16 pid)
{
  PIDLookup lookup;

  lookup.res = FALSE;
  lookup.pid = pid;
  g_hash_table_foreach (base->programs, (GHFunc) foreach_pid_in_program,
      &lookup);

  return lookup.res;
}

/* returns NULL if no matching descriptor found *
 * otherwise returns a descriptor that needs to *
 * be freed */
const GstMpegtsDescriptor *
mpegts_get_descriptor_from_program (MpegTSBaseProgram * program, guint8 tag)
{
  const GstMpegtsPMT *pmt = program->pmt;

  return gst_mpegts_find_descriptor (pmt->descriptors, tag);
}

static MpegTSBaseProgram *
mpegts_base_new_program (MpegTSBase * base,
    gint program_number, guint16 pmt_pid)
{
  MpegTSBaseProgram *program;

  GST_DEBUG_OBJECT (base, "program_number : %d, pmt_pid : %d",
      program_number, pmt_pid);

  program = g_malloc0 (base->program_size);
  program->program_number = program_number;
  program->pmt_pid = pmt_pid;
  program->pcr_pid = G_MAXUINT16;
  program->streams = g_new0 (MpegTSBaseStream *, 0x2000);
  program->patcount = 0;

  return program;
}

MpegTSBaseProgram *
mpegts_base_add_program (MpegTSBase * base,
    gint program_number, guint16 pmt_pid)
{
  MpegTSBaseProgram *program;

  GST_DEBUG_OBJECT (base, "program_number : %d, pmt_pid : %d",
      program_number, pmt_pid);

  program = mpegts_base_new_program (base, program_number, pmt_pid);

  /* Mark the PMT PID as being a known PSI PID */
  if (G_UNLIKELY (MPEGTS_BIT_IS_SET (base->known_psi, pmt_pid))) {
    GST_FIXME ("Refcounting. Setting twice a PID (0x%04x) as known PSI",
        pmt_pid);
  }
  MPEGTS_BIT_SET (base->known_psi, pmt_pid);

  g_hash_table_insert (base->programs,
      GINT_TO_POINTER (program_number), program);

  return program;
}

MpegTSBaseProgram *
mpegts_base_get_program (MpegTSBase * base, gint program_number)
{
  MpegTSBaseProgram *program;

  program = (MpegTSBaseProgram *) g_hash_table_lookup (base->programs,
      GINT_TO_POINTER ((gint) program_number));

  return program;
}

static MpegTSBaseProgram *
mpegts_base_steal_program (MpegTSBase * base, gint program_number)
{
  MpegTSBaseProgram *program;

  program = (MpegTSBaseProgram *) g_hash_table_lookup (base->programs,
      GINT_TO_POINTER ((gint) program_number));

  if (program)
    g_hash_table_steal (base->programs,
        GINT_TO_POINTER ((gint) program_number));

  return program;
}

static void
mpegts_base_free_program (MpegTSBaseProgram * program)
{
  GList *tmp;

  if (program->pmt) {
    gst_mpegts_section_unref (program->section);
    program->pmt = NULL;
  }

  for (tmp = program->stream_list; tmp; tmp = tmp->next)
    g_free (tmp->data);
  if (program->stream_list)
    g_list_free (program->stream_list);

  g_free (program->streams);

  if (program->tags)
    gst_tag_list_unref (program->tags);

  g_free (program);
}

void
mpegts_base_remove_program (MpegTSBase * base, gint program_number)
{
  GST_DEBUG_OBJECT (base, "program_number : %d", program_number);

  g_hash_table_remove (base->programs, GINT_TO_POINTER (program_number));
}

static guint32
get_registration_from_descriptors (GPtrArray * descriptors)
{
  const GstMpegtsDescriptor *desc;

  if ((desc =
          gst_mpegts_find_descriptor (descriptors,
              GST_MTS_DESC_REGISTRATION))) {
    if (G_UNLIKELY (desc->length < 4)) {
      GST_WARNING ("Registration descriptor with length < 4. (Corrupted ?)");
    } else
      return GST_READ_UINT32_BE (desc->data + 2);
  }

  return 0;
}

static MpegTSBaseStream *
mpegts_base_program_add_stream (MpegTSBase * base,
    MpegTSBaseProgram * program, guint16 pid, guint8 stream_type,
    GstMpegtsPMTStream * stream)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);
  MpegTSBaseStream *bstream;

  GST_DEBUG ("pid:0x%04x, stream_type:0x%03x", pid, stream_type);

  if (G_UNLIKELY (program->streams[pid])) {
    if (stream_type != 0xff)
      GST_WARNING ("Stream already present !");
    return NULL;
  }

  bstream = g_malloc0 (base->stream_size);
  bstream->pid = pid;
  bstream->stream_type = stream_type;
  bstream->stream = stream;
  if (stream) {
    bstream->registration_id =
        get_registration_from_descriptors (stream->descriptors);
    GST_DEBUG ("PID 0x%04x, registration_id %" SAFE_FOURCC_FORMAT,
        bstream->pid, SAFE_FOURCC_ARGS (bstream->registration_id));
  }


  program->streams[pid] = bstream;
  program->stream_list = g_list_append (program->stream_list, bstream);

  if (klass->stream_added)
    klass->stream_added (base, bstream, program);

  return bstream;
}

void
mpegts_base_program_remove_stream (MpegTSBase * base,
    MpegTSBaseProgram * program, guint16 pid)
{
  MpegTSBaseClass *klass;
  MpegTSBaseStream *stream = program->streams[pid];

  GST_DEBUG ("pid:0x%04x", pid);

  if (G_UNLIKELY (stream == NULL)) {
    /* Can happen if the PCR PID is the same as a audio/video PID */
    GST_DEBUG ("Stream already removed");
    return;
  }

  klass = GST_MPEGTS_BASE_GET_CLASS (base);

  /* If subclass needs it, inform it of the stream we are about to remove */
  if (klass->stream_removed)
    klass->stream_removed (base, stream);

  program->stream_list = g_list_remove_all (program->stream_list, stream);
  g_free (stream);
  program->streams[pid] = NULL;
}

/* Return TRUE if programs are equal */
static gboolean
mpegts_base_is_same_program (MpegTSBase * base, MpegTSBaseProgram * oldprogram,
    guint16 new_pmt_pid, const GstMpegtsPMT * new_pmt)
{
  guint i, nbstreams;
  MpegTSBaseStream *oldstream;
  gboolean sawpcrpid = FALSE;

  if (oldprogram->pmt_pid != new_pmt_pid) {
    GST_DEBUG ("Different pmt_pid (new:0x%04x, old:0x%04x)", new_pmt_pid,
        oldprogram->pmt_pid);
    return FALSE;
  }

  if (oldprogram->pcr_pid != new_pmt->pcr_pid) {
    GST_DEBUG ("Different pcr_pid (new:0x%04x, old:0x%04x)",
        new_pmt->pcr_pid, oldprogram->pcr_pid);
    return FALSE;
  }

  /* Check the streams */
  nbstreams = new_pmt->streams->len;
  for (i = 0; i < nbstreams; ++i) {
    GstMpegtsPMTStream *stream = g_ptr_array_index (new_pmt->streams, i);

    oldstream = oldprogram->streams[stream->pid];
    if (!oldstream) {
      GST_DEBUG ("New stream 0x%04x not present in old program", stream->pid);
      return FALSE;
    }
    if (oldstream->stream_type != stream->stream_type) {
      GST_DEBUG
          ("New stream 0x%04x has a different stream type (new:%d, old:%d)",
          stream->pid, stream->stream_type, oldstream->stream_type);
      return FALSE;
    }
    if (stream->pid == oldprogram->pcr_pid)
      sawpcrpid = TRUE;
  }

  /* If the pcr is not shared with an existing stream, we'll have one extra stream */
  if (!sawpcrpid)
    nbstreams += 1;

  if (nbstreams != g_list_length (oldprogram->stream_list)) {
    GST_DEBUG ("Different number of streams (new:%d, old:%d)",
        nbstreams, g_list_length (oldprogram->stream_list));
    return FALSE;
  }

  GST_DEBUG ("Programs are equal");
  return TRUE;
}

static void
mpegts_base_deactivate_program (MpegTSBase * base, MpegTSBaseProgram * program)
{
  gint i;
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  if (G_UNLIKELY (program->active == FALSE))
    return;

  GST_DEBUG_OBJECT (base, "Deactivating PMT");

  program->active = FALSE;

  if (program->pmt) {
    for (i = 0; i < program->pmt->streams->len; ++i) {
      GstMpegtsPMTStream *stream = g_ptr_array_index (program->pmt->streams, i);

      mpegts_base_program_remove_stream (base, program, stream->pid);

      /* Only unset the is_pes/known_psi bit if the PID isn't used in any other active
       * program */
      if (!mpegts_pid_in_active_programs (base, stream->pid)) {
        switch (stream->stream_type) {
          case GST_MPEGTS_STREAM_TYPE_SCTE_DSMCC_DCB:
          case GST_MPEGTS_STREAM_TYPE_SCTE_SIGNALING:
          {
            guint32 registration_id =
                get_registration_from_descriptors (stream->descriptors);

            /* Not a private section stream */
            if (registration_id != DRF_ID_CUEI
                && registration_id != DRF_ID_ETV1)
              break;
            /* Fall through on purpose - remove this PID from known_psi */
          }
          case GST_MPEGTS_STREAM_TYPE_PRIVATE_SECTIONS:
          case GST_MPEGTS_STREAM_TYPE_MHEG:
          case GST_MPEGTS_STREAM_TYPE_DSM_CC:
          case GST_MPEGTS_STREAM_TYPE_DSMCC_A:
          case GST_MPEGTS_STREAM_TYPE_DSMCC_B:
          case GST_MPEGTS_STREAM_TYPE_DSMCC_C:
          case GST_MPEGTS_STREAM_TYPE_DSMCC_D:
          case GST_MPEGTS_STREAM_TYPE_SL_FLEXMUX_SECTIONS:
          case GST_MPEGTS_STREAM_TYPE_METADATA_SECTIONS:
            /* Set known PSI streams */
            if (base->parse_private_sections)
              MPEGTS_BIT_UNSET (base->known_psi, stream->pid);
            break;
          default:
            MPEGTS_BIT_UNSET (base->is_pes, stream->pid);
            break;
        }
      }
    }

    /* remove pcr stream */
    /* FIXME : This might actually be shared with another stream ? */
    mpegts_base_program_remove_stream (base, program, program->pcr_pid);
    if (!mpegts_pid_in_active_programs (base, program->pcr_pid))
      MPEGTS_BIT_UNSET (base->is_pes, program->pcr_pid);

    GST_DEBUG ("program stream_list is now %p", program->stream_list);
  }

  /* Inform subclasses we're deactivating this program */
  if (klass->program_stopped)
    klass->program_stopped (base, program);
}

static void
mpegts_base_activate_program (MpegTSBase * base, MpegTSBaseProgram * program,
    guint16 pmt_pid, GstMpegtsSection * section, const GstMpegtsPMT * pmt,
    gboolean initial_program)
{
  guint i;
  MpegTSBaseClass *klass;

  if (G_UNLIKELY (program->active))
    return;

  GST_DEBUG ("Activating program %d", program->program_number);

  /* activate new pmt */
  if (program->section)
    gst_mpegts_section_unref (program->section);
  program->section = gst_mpegts_section_ref (section);

  program->pmt = pmt;
  program->pmt_pid = pmt_pid;
  program->pcr_pid = pmt->pcr_pid;

  /* extract top-level registration_id if present */
  program->registration_id =
      get_registration_from_descriptors (pmt->descriptors);
  GST_DEBUG ("program 0x%04x, registration_id %" SAFE_FOURCC_FORMAT,
      program->program_number, SAFE_FOURCC_ARGS (program->registration_id));

  for (i = 0; i < pmt->streams->len; ++i) {
    GstMpegtsPMTStream *stream = g_ptr_array_index (pmt->streams, i);

    switch (stream->stream_type) {
      case GST_MPEGTS_STREAM_TYPE_SCTE_DSMCC_DCB:
      case GST_MPEGTS_STREAM_TYPE_SCTE_SIGNALING:
      {
        guint32 registration_id =
            get_registration_from_descriptors (stream->descriptors);
        /* Not a private section stream */
        if (registration_id != DRF_ID_CUEI && registration_id != DRF_ID_ETV1)
          break;
        /* Fall through on purpose - remove this PID from known_psi */
      }
      case GST_MPEGTS_STREAM_TYPE_PRIVATE_SECTIONS:
      case GST_MPEGTS_STREAM_TYPE_MHEG:
      case GST_MPEGTS_STREAM_TYPE_DSM_CC:
      case GST_MPEGTS_STREAM_TYPE_DSMCC_A:
      case GST_MPEGTS_STREAM_TYPE_DSMCC_B:
      case GST_MPEGTS_STREAM_TYPE_DSMCC_C:
      case GST_MPEGTS_STREAM_TYPE_DSMCC_D:
      case GST_MPEGTS_STREAM_TYPE_SL_FLEXMUX_SECTIONS:
      case GST_MPEGTS_STREAM_TYPE_METADATA_SECTIONS:
        /* Set known PSI streams */
        if (base->parse_private_sections)
          MPEGTS_BIT_SET (base->known_psi, stream->pid);
        break;
      default:
        if (G_UNLIKELY (MPEGTS_BIT_IS_SET (base->is_pes, stream->pid)))
          GST_FIXME
              ("Refcounting issue. Setting twice a PID (0x%04x) as known PES",
              stream->pid);
        if (G_UNLIKELY (MPEGTS_BIT_IS_SET (base->known_psi, stream->pid))) {
          GST_FIXME
              ("Refcounting issue. Setting a known PSI PID (0x%04x) as known PES",
              stream->pid);
          MPEGTS_BIT_UNSET (base->known_psi, stream->pid);
        }

        MPEGTS_BIT_SET (base->is_pes, stream->pid);
        break;
    }
    mpegts_base_program_add_stream (base, program,
        stream->pid, stream->stream_type, stream);

  }
  /* We add the PCR pid last. If that PID is already used by one of the media
   * streams above, no new stream will be created */
  mpegts_base_program_add_stream (base, program, pmt->pcr_pid, -1, NULL);
  MPEGTS_BIT_SET (base->is_pes, pmt->pcr_pid);

  program->active = TRUE;
  program->initial_program = initial_program;

  klass = GST_MPEGTS_BASE_GET_CLASS (base);
  if (klass->program_started != NULL)
    klass->program_started (base, program);

  GST_DEBUG_OBJECT (base, "new pmt activated");
}


static gboolean
mpegts_base_apply_pat (MpegTSBase * base, GstMpegtsSection * section)
{
  GPtrArray *pat = gst_mpegts_section_get_pat (section);
  GPtrArray *old_pat;
  MpegTSBaseProgram *program;
  gint i;

  if (G_UNLIKELY (pat == NULL))
    return FALSE;

  GST_INFO_OBJECT (base, "PAT");

  /* Applying a new PAT does two things:
   * * It adds the new programs to the list of programs this element handles
   *   and increments at the same time the number of times a program is referenced.
   *
   * * If there was a previously active PAT, It decrements the reference count
   *   of all program it used. If a program is no longer needed, it is removed.
   */

  old_pat = base->pat;
  base->pat = pat;

  GST_LOG ("Activating new Program Association Table");
  /* activate the new table */
  for (i = 0; i < pat->len; ++i) {
    GstMpegtsPatProgram *patp = g_ptr_array_index (pat, i);

    program = mpegts_base_get_program (base, patp->program_number);
    if (program) {
      /* IF the program already existed, just check if the PMT PID changed */
      if (program->pmt_pid != patp->network_or_program_map_PID) {
        if (program->pmt_pid != G_MAXUINT16) {
          /* pmt pid changed */
          /* FIXME: when this happens it may still be pmt pid of another
           * program, so setting to False may make it go through expensive
           * path in is_psi unnecessarily */
          MPEGTS_BIT_UNSET (base->known_psi, program->pmt_pid);
        }

        program->pmt_pid = patp->network_or_program_map_PID;
        if (G_UNLIKELY (MPEGTS_BIT_IS_SET (base->known_psi, program->pmt_pid)))
          GST_FIXME
              ("Refcounting issue. Setting twice a PMT PID (0x%04x) as know PSI",
              program->pmt_pid);
        MPEGTS_BIT_SET (base->known_psi, patp->network_or_program_map_PID);
      }
    } else {
      /* Create a new program */
      program =
          mpegts_base_add_program (base, patp->program_number,
          patp->network_or_program_map_PID);
    }
    /* We mark this program as being referenced by one PAT */
    program->patcount += 1;
  }

  if (old_pat) {
    /* deactivate the old table */
    GST_LOG ("Deactivating old Program Association Table");

    for (i = 0; i < old_pat->len; ++i) {
      GstMpegtsPatProgram *patp = g_ptr_array_index (old_pat, i);

      program = mpegts_base_get_program (base, patp->program_number);
      if (G_UNLIKELY (program == NULL)) {
        GST_DEBUG_OBJECT (base, "broken PAT, duplicated entry for program %d",
            patp->program_number);
        continue;
      }

      if (--program->patcount > 0)
        /* the program has been referenced by the new pat, keep it */
        continue;

      GST_INFO_OBJECT (base, "PAT removing program 0x%04x 0x%04x",
          patp->program_number, patp->network_or_program_map_PID);

      mpegts_base_deactivate_program (base, program);
      mpegts_base_remove_program (base, patp->program_number);
      /* FIXME: when this happens it may still be pmt pid of another
       * program, so setting to False may make it go through expensive
       * path in is_psi unnecessarily */
      if (G_UNLIKELY (MPEGTS_BIT_IS_SET (base->known_psi,
                  patp->network_or_program_map_PID))) {
        GST_FIXME
            ("Program refcounting : Setting twice a pid (0x%04x) as known PSI",
            patp->network_or_program_map_PID);
      }
      MPEGTS_BIT_SET (base->known_psi, patp->network_or_program_map_PID);
      mpegts_packetizer_remove_stream (base->packetizer,
          patp->network_or_program_map_PID);
    }

    g_ptr_array_unref (old_pat);
  }

  return TRUE;
}

static gboolean
mpegts_base_apply_pmt (MpegTSBase * base, GstMpegtsSection * section)
{
  const GstMpegtsPMT *pmt;
  MpegTSBaseProgram *program, *old_program;
  guint program_number;
  gboolean initial_program = TRUE;

  pmt = gst_mpegts_section_get_pmt (section);
  if (G_UNLIKELY (pmt == NULL)) {
    GST_ERROR ("Could not get PMT (corrupted ?)");
    return FALSE;
  }

  /* FIXME : not so sure this is valid anymore */
  if (G_UNLIKELY (base->seen_pat == FALSE)) {
    GST_WARNING ("Got pmt without pat first. Returning");
    /* remove the stream since we won't get another PMT otherwise */
    mpegts_packetizer_remove_stream (base->packetizer, section->pid);
    return TRUE;
  }

  program_number = section->subtable_extension;
  GST_DEBUG ("Applying PMT (program_number:%d, pid:0x%04x)",
      program_number, section->pid);

  /* In order for stream switching to happen properly in decodebin(2),
   * we need to first add the new pads (i.e. activate the new program)
   * before removing the old ones (i.e. deactivating the old program)
   */

  old_program = mpegts_base_get_program (base, program_number);
  if (G_UNLIKELY (old_program == NULL))
    goto no_program;

  if (G_UNLIKELY (mpegts_base_is_same_program (base, old_program, section->pid,
              pmt)))
    goto same_program;

  /* If the current program is active, this means we have a new program */
  if (old_program->active) {
    old_program = mpegts_base_steal_program (base, program_number);
    program = mpegts_base_new_program (base, program_number, section->pid);
    g_hash_table_insert (base->programs,
        GINT_TO_POINTER (program_number), program);

    /* Desactivate the old program */
    mpegts_base_deactivate_program (base, old_program);
    mpegts_base_free_program (old_program);
    initial_program = FALSE;
  } else
    program = old_program;

  /* activate program */
  /* Ownership of pmt_info is given to the program */
  mpegts_base_activate_program (base, program, section->pid, section, pmt,
      initial_program);

  return TRUE;

no_program:
  {
    GST_ERROR ("Attempted to apply a PMT on a program that wasn't created");
    return TRUE;
  }

same_program:
  {
    GST_DEBUG ("Not applying identical program");
    return TRUE;
  }
}

static void
mpegts_base_handle_psi (MpegTSBase * base, GstMpegtsSection * section)
{
  gboolean post_message = TRUE;

  GST_DEBUG ("Handling PSI (pid: 0x%04x , table_id: 0x%02x)",
      section->pid, section->table_id);

  switch (section->section_type) {
    case GST_MPEGTS_SECTION_PAT:
      post_message = mpegts_base_apply_pat (base, section);
      if (base->seen_pat == FALSE) {
        base->seen_pat = TRUE;
        GST_DEBUG ("First PAT offset: %" G_GUINT64_FORMAT, section->offset);
        mpegts_packetizer_set_reference_offset (base->packetizer,
            section->offset);
      }
      break;
    case GST_MPEGTS_SECTION_PMT:
      post_message = mpegts_base_apply_pmt (base, section);
      break;
    case GST_MPEGTS_SECTION_EIT:
      /* some tag xtraction + posting */
      post_message = mpegts_base_get_tags_from_eit (base, section);
      break;
    case GST_MPEGTS_SECTION_ATSC_MGT:
      post_message = mpegts_base_parse_atsc_mgt (base, section);
      break;
    default:
      break;
  }

  /* Finally post message (if it wasn't corrupted) */
  if (post_message)
    gst_element_post_message (GST_ELEMENT_CAST (base),
        gst_message_new_mpegts_section (GST_OBJECT (base), section));
  gst_mpegts_section_unref (section);
}

static gboolean
mpegts_base_parse_atsc_mgt (MpegTSBase * base, GstMpegtsSection * section)
{
  const GstMpegtsAtscMGT *mgt;
  gint i;

  mgt = gst_mpegts_section_get_atsc_mgt (section);
  if (G_UNLIKELY (mgt == NULL))
    return FALSE;

  for (i = 0; i < mgt->tables->len; ++i) {
    GstMpegtsAtscMGTTable *table = g_ptr_array_index (mgt->tables, i);

    if ((table->table_type >= GST_MPEGTS_ATSC_MGT_TABLE_TYPE_EIT0 &&
            table->table_type <= GST_MPEGTS_ATSC_MGT_TABLE_TYPE_EIT127) ||
        (table->table_type >= GST_MPEGTS_ATSC_MGT_TABLE_TYPE_ETT0 &&
            table->table_type <= GST_MPEGTS_ATSC_MGT_TABLE_TYPE_ETT127)) {
      MPEGTS_BIT_SET (base->known_psi, table->pid);
    }
  }

  return TRUE;
}

static gboolean
mpegts_base_get_tags_from_eit (MpegTSBase * base, GstMpegtsSection * section)
{
  const GstMpegtsEIT *eit;
  guint i;
  MpegTSBaseProgram *program;

  /* Early exit if it's not from the present/following table_id */
  if (section->table_id != GST_MTS_TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_PRESENT
      || section->table_id !=
      GST_MTS_TABLE_ID_EVENT_INFORMATION_OTHER_TS_PRESENT)
    return TRUE;

  eit = gst_mpegts_section_get_eit (section);
  if (G_UNLIKELY (eit == NULL))
    return FALSE;

  program = mpegts_base_get_program (base, section->subtable_extension);

  GST_DEBUG
      ("program_id:0x%04x, table_id:0x%02x, actual_stream:%d, present_following:%d, program:%p",
      section->subtable_extension, section->table_id, eit->actual_stream,
      eit->present_following, program);

  if (program && eit->present_following) {
    for (i = 0; i < eit->events->len; i++) {
      GstMpegtsEITEvent *event = g_ptr_array_index (eit->events, i);
      const GstMpegtsDescriptor *desc;

      if (event->running_status == RUNNING_STATUS_RUNNING) {
        program->event_id = event->event_id;
        if ((desc =
                gst_mpegts_find_descriptor (event->descriptors,
                    GST_MTS_DESC_DVB_SHORT_EVENT))) {
          gchar *name;
          if (gst_mpegts_descriptor_parse_dvb_short_event (desc, NULL, &name,
                  NULL)) {
            /* FIXME : Is it correct to post an event duration as a GST_TAG_DURATION ??? */
            program->tags =
                gst_tag_list_new (GST_TAG_TITLE, name, GST_TAG_DURATION,
                event->duration * GST_SECOND, NULL);
            return TRUE;
          }
        }
      }
    }
  }

  return TRUE;
}

static gboolean
remove_each_program (gpointer key, MpegTSBaseProgram * program,
    MpegTSBase * base)
{
  /* First deactivate it */
  mpegts_base_deactivate_program (base, program);

  return TRUE;
}

static gboolean
gst_mpegts_base_handle_eos (MpegTSBase * base)
{
  g_hash_table_foreach_remove (base->programs, (GHRFunc) remove_each_program,
      base);
  /* finally remove  */
  return TRUE;
}

static inline GstFlowReturn
mpegts_base_drain (MpegTSBase * base)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  /* Call implementation */
  if (klass->drain)
    return klass->drain (base);

  return GST_FLOW_OK;
}

static inline void
mpegts_base_flush (MpegTSBase * base, gboolean hard)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  /* Call implementation */
  if (klass->flush)
    klass->flush (base, hard);
}

static gboolean
mpegts_base_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  gboolean hard;
  MpegTSBase *base = GST_MPEGTS_BASE (parent);

  GST_DEBUG_OBJECT (base, "Got event %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &base->segment);
      GST_DEBUG_OBJECT (base, "Received segment %" GST_SEGMENT_FORMAT,
          &base->segment);
      /* Check if we need to switch PCR/PTS handling */
      if (base->segment.format == GST_FORMAT_TIME) {
        base->packetizer->calculate_offset = FALSE;
        base->packetizer->calculate_skew = TRUE;
      } else {
        base->packetizer->calculate_offset = TRUE;
        base->packetizer->calculate_skew = FALSE;
      }
      res = GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, event);
      break;
    case GST_EVENT_STREAM_START:
      gst_event_unref (event);
      break;
    case GST_EVENT_EOS:
      res = GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, event);
      res = gst_mpegts_base_handle_eos (base);
      break;
    case GST_EVENT_CAPS:
      /* FIXME, do something */
      gst_event_unref (event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, event);
      hard = (base->mode != BASE_MODE_SEEKING);
      mpegts_packetizer_flush (base->packetizer, hard);
      mpegts_base_flush (base, hard);
      gst_segment_init (&base->segment, GST_FORMAT_UNDEFINED);
      base->seen_pat = FALSE;
      break;
    default:
      res = GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, event);
  }

  /* Always return TRUE for sticky events */
  if (GST_EVENT_IS_STICKY (event))
    res = TRUE;

  return res;
}

static void
query_upstream_latency (MpegTSBase * base)
{
  GstQuery *query;

  query = gst_query_new_latency ();
  if (gst_pad_peer_query (base->sinkpad, query)) {
    gst_query_parse_latency (query, &base->upstream_live, NULL, NULL);
    GST_DEBUG_OBJECT (base, "Upstream is %s",
        base->upstream_live ? "LIVE" : "NOT LIVE");
  } else
    GST_WARNING_OBJECT (base, "Failed to query upstream latency");
  gst_query_unref (query);
  base->queried_latency = TRUE;
}

static GstFlowReturn
mpegts_base_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;
  MpegTSBase *base;
  MpegTSPacketizerPacketReturn pret;
  MpegTSPacketizer2 *packetizer;
  MpegTSPacketizerPacket packet;
  MpegTSBaseClass *klass;

  base = GST_MPEGTS_BASE (parent);
  klass = GST_MPEGTS_BASE_GET_CLASS (base);

  packetizer = base->packetizer;

  if (G_UNLIKELY (base->queried_latency == FALSE)) {
    query_upstream_latency (base);
  }

  if (klass->input_done)
    gst_buffer_ref (buf);

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_DEBUG_OBJECT (base, "Got DISCONT buffer, flushing");
    res = mpegts_base_drain (base);
    if (G_UNLIKELY (res != GST_FLOW_OK))
      return res;

    mpegts_base_flush (base, FALSE);
    mpegts_packetizer_flush (base->packetizer, FALSE);
  }

  mpegts_packetizer_push (base->packetizer, buf);

  while (res == GST_FLOW_OK) {
    pret = mpegts_packetizer_next_packet (base->packetizer, &packet);

    /* If we don't have enough data, return */
    if (G_UNLIKELY (pret == PACKET_NEED_MORE))
      break;

    if (G_UNLIKELY (pret == PACKET_BAD)) {
      /* bad header, skip the packet */
      GST_DEBUG_OBJECT (base, "bad packet, skipping");
      goto next;
    }

    /* If it's a known PES, push it */
    if (MPEGTS_BIT_IS_SET (base->is_pes, packet.pid)) {
      /* push the packet downstream */
      if (base->push_data)
        res = klass->push (base, &packet, NULL);
    } else if (packet.payload
        && MPEGTS_BIT_IS_SET (base->known_psi, packet.pid)) {
      /* base PSI data */
      GList *others, *tmp;
      GstMpegtsSection *section;

      section = mpegts_packetizer_push_section (packetizer, &packet, &others);
      if (section)
        mpegts_base_handle_psi (base, section);
      if (G_UNLIKELY (others)) {
        for (tmp = others; tmp; tmp = tmp->next)
          mpegts_base_handle_psi (base, (GstMpegtsSection *) tmp->data);
        g_list_free (others);
      }

      /* we need to push section packet downstream */
      if (base->push_section)
        res = klass->push (base, &packet, section);

    } else if (packet.payload && packet.pid != 0x1fff)
      GST_LOG ("PID 0x%04x Saw packet on a pid we don't handle", packet.pid);

  next:
    mpegts_packetizer_clear_packet (base->packetizer, &packet);
  }

  if (klass->input_done) {
    if (res == GST_FLOW_OK)
      res = klass->input_done (base, buf);
    else
      gst_buffer_unref (buf);
  }

  return res;
}

static GstFlowReturn
mpegts_base_scan (MpegTSBase * base)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf = NULL;
  guint i;
  gboolean done = FALSE;
  MpegTSPacketizerPacketReturn pret;
  gint64 tmpval;
  gint64 upstream_size, seek_pos;
  GstFormat format;
  guint initial_pcr_seen;

  GST_DEBUG ("Scanning for initial sync point");

  /* Find initial sync point and at least 5 PCR values */
  for (i = 0; i < 10 && !done; i++) {
    GST_DEBUG ("Grabbing %d => %d", i * 65536, 65536);

    ret = gst_pad_pull_range (base->sinkpad, i * 65536, 65536, &buf);
    if (G_UNLIKELY (ret == GST_FLOW_EOS))
      break;
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto beach;

    /* Push to packetizer */
    mpegts_packetizer_push (base->packetizer, buf);
    buf = NULL;

    if (mpegts_packetizer_has_packets (base->packetizer)) {
      if (base->seek_offset == -1) {
        /* Mark the initial sync point and remember the packetsize */
        base->seek_offset = base->packetizer->offset;
        GST_DEBUG ("Sync point is now %" G_GUINT64_FORMAT, base->seek_offset);
        base->packetsize = base->packetizer->packet_size;
      }
      while (1) {
        /* Eat up all packets */
        pret = mpegts_packetizer_process_next_packet (base->packetizer);
        if (pret == PACKET_NEED_MORE)
          break;
        if (pret != PACKET_BAD && base->packetizer->nb_seen_offsets >= 5) {
          GST_DEBUG ("Got enough initial PCR");
          done = TRUE;
          break;
        }
      }
    }
  }

  initial_pcr_seen = base->packetizer->nb_seen_offsets;
  if (G_UNLIKELY (initial_pcr_seen == 0))
    goto no_initial_pcr;
  GST_DEBUG ("Seen %d initial PCR", initial_pcr_seen);

  /* Now send data from the end */
  mpegts_packetizer_clear (base->packetizer);

  /* Get the size of upstream */
  format = GST_FORMAT_BYTES;
  if (!gst_pad_peer_query_duration (base->sinkpad, format, &tmpval))
    goto beach;
  upstream_size = tmpval;
  done = FALSE;

  /* Find last PCR value */
  for (seek_pos = MAX (0, upstream_size - 655360);
      seek_pos < upstream_size && !done; seek_pos += 65536) {
    GST_DEBUG ("Grabbing %" G_GUINT64_FORMAT " => %d", seek_pos, 65536);

    ret = gst_pad_pull_range (base->sinkpad, seek_pos, 65536, &buf);
    if (G_UNLIKELY (ret == GST_FLOW_EOS))
      break;
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto beach;

    /* Push to packetizer */
    mpegts_packetizer_push (base->packetizer, buf);
    buf = NULL;

    if (mpegts_packetizer_has_packets (base->packetizer)) {
      while (1) {
        /* Eat up all packets */
        pret = mpegts_packetizer_process_next_packet (base->packetizer);
        if (pret == PACKET_NEED_MORE)
          break;
        if (pret != PACKET_BAD &&
            base->packetizer->nb_seen_offsets > initial_pcr_seen) {
          GST_DEBUG ("Got last PCR");
          done = TRUE;
          break;
        }
      }
    }
  }

beach:
  mpegts_packetizer_clear (base->packetizer);
  return ret;

no_initial_pcr:
  mpegts_packetizer_clear (base->packetizer);
  GST_WARNING_OBJECT (base, "Couldn't find any PCR within the first %d bytes",
      10 * 65536);
  return GST_FLOW_ERROR;
}


static void
mpegts_base_loop (MpegTSBase * base)
{
  GstFlowReturn ret = GST_FLOW_ERROR;

  switch (base->mode) {
    case BASE_MODE_SCANNING:
      /* Find first sync point */
      ret = mpegts_base_scan (base);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto error;
      base->mode = BASE_MODE_STREAMING;
      GST_DEBUG ("Changing to Streaming");
      break;
    case BASE_MODE_SEEKING:
      /* FIXME : unclear if we still need mode_seeking... */
      base->mode = BASE_MODE_STREAMING;
      break;
    case BASE_MODE_STREAMING:
    {
      GstBuffer *buf = NULL;

      GST_DEBUG ("Pulling data from %" G_GUINT64_FORMAT, base->seek_offset);

      ret = gst_pad_pull_range (base->sinkpad, base->seek_offset,
          100 * base->packetsize, &buf);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto error;
      base->seek_offset += gst_buffer_get_size (buf);
      ret = mpegts_base_chain (base->sinkpad, GST_OBJECT_CAST (base), buf);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto error;
    }
      break;
    case BASE_MODE_PUSHING:
      GST_WARNING ("wrong BASE_MODE_PUSHING mode in pull loop");
      break;
  }

  return;

error:
  {
    const gchar *reason = gst_flow_get_name (ret);
    GST_DEBUG_OBJECT (base, "Pausing task, reason %s", reason);
    if (ret == GST_FLOW_EOS) {
      if (!GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base,
              gst_event_new_eos ()))
        GST_ELEMENT_ERROR (base, STREAM, FAILED,
            (_("Internal data stream error.")),
            ("No program activated before EOS"));
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (base, STREAM, FAILED,
          (_("Internal data stream error.")),
          ("stream stopped, reason %s", reason));
      GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, gst_event_new_eos ());
    }
    gst_pad_pause_task (base->sinkpad);
  }
}


gboolean
mpegts_base_handle_seek_event (MpegTSBase * base, GstPad * pad,
    GstEvent * event)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);
  GstFlowReturn ret = GST_FLOW_ERROR;
  gdouble rate;
  gboolean flush;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  GstEvent *flush_event = NULL;

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME)
    return FALSE;

  if (GST_EVENT_SEQNUM (event) == base->last_seek_seqnum) {
    GST_DEBUG_OBJECT (base, "Skipping already handled seek");
    return TRUE;
  }

  if (base->mode == BASE_MODE_PUSHING) {
    /* First try if upstream supports seeking in TIME format */
    if (gst_pad_push_event (base->sinkpad, gst_event_ref (event))) {
      GST_DEBUG ("upstream handled SEEK event");
      return TRUE;
    }

    /* If the subclass can seek, do that */
    if (klass->seek) {
      ret = klass->seek (base, event);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        GST_WARNING ("seeking failed %s", gst_flow_get_name (ret));
      else {
        GstEvent *new_seek;
        base->mode = BASE_MODE_SEEKING;

        new_seek = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
            GST_SEEK_TYPE_SET, base->seek_offset, GST_SEEK_TYPE_NONE, -1);
        gst_event_set_seqnum (new_seek, GST_EVENT_SEQNUM (event));
        if (!gst_pad_push_event (base->sinkpad, new_seek))
          ret = GST_FLOW_ERROR;
        else
          base->last_seek_seqnum = GST_EVENT_SEQNUM (event);
        base->mode = BASE_MODE_PUSHING;
      }
    }

    return ret == GST_FLOW_OK;
  }

  GST_DEBUG ("seek event, rate: %f start: %" GST_TIME_FORMAT
      " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
      GST_TIME_ARGS (stop));

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* stop streaming, either by flushing or by pausing the task */
  base->mode = BASE_MODE_SEEKING;
  if (flush) {
    GST_DEBUG_OBJECT (base, "sending flush start");
    flush_event = gst_event_new_flush_start ();
    gst_event_set_seqnum (flush_event, GST_EVENT_SEQNUM (event));
    gst_pad_push_event (base->sinkpad, gst_event_ref (flush_event));
    GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, flush_event);
  } else
    gst_pad_pause_task (base->sinkpad);

  /* wait for streaming to finish */
  GST_PAD_STREAM_LOCK (base->sinkpad);

  if (flush) {
    /* send a FLUSH_STOP for the sinkpad, since we need data for seeking */
    GST_DEBUG_OBJECT (base, "sending flush stop");
    flush_event = gst_event_new_flush_stop (TRUE);
    gst_event_set_seqnum (flush_event, GST_EVENT_SEQNUM (event));

    /* ref for it to be reused later */
    gst_pad_push_event (base->sinkpad, gst_event_ref (flush_event));
    /* And actually flush our pending data but allow to preserve some info
     * to perform the seek */
    mpegts_base_flush (base, FALSE);
    mpegts_packetizer_flush (base->packetizer, FALSE);
  }

  if (flags & (GST_SEEK_FLAG_SEGMENT)) {
    GST_WARNING ("seek flags 0x%x are not supported", (int) flags);
    goto done;
  }


  /* If the subclass can seek, do that */
  if (klass->seek) {
    ret = klass->seek (base, event);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      GST_WARNING ("seeking failed %s", gst_flow_get_name (ret));
    else
      base->last_seek_seqnum = GST_EVENT_SEQNUM (event);
  } else {
    /* FIXME : Check this before so we don't do seeks we can't handle ? */
    GST_WARNING ("subclass has no seek implementation");
  }

  if (flush_event) {
    /* if we sent a FLUSH_START, we now send a FLUSH_STOP */
    GST_DEBUG_OBJECT (base, "sending flush stop");
    GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, flush_event);
    flush_event = NULL;
  }
done:
  if (flush_event)
    gst_event_unref (flush_event);
  gst_pad_start_task (base->sinkpad, (GstTaskFunction) mpegts_base_loop, base,
      NULL);

  GST_PAD_STREAM_UNLOCK (base->sinkpad);
  return ret == GST_FLOW_OK;
}


static gboolean
mpegts_base_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
  gst_query_unref (query);

  if (!pull_mode)
    goto activate_push;

  GST_DEBUG_OBJECT (sinkpad, "activating pull");
  return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PULL, TRUE);

activate_push:
  {
    GST_DEBUG_OBJECT (sinkpad, "activating push");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, TRUE);
  }
}

static gboolean
mpegts_base_sink_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;
  MpegTSBase *base = GST_MPEGTS_BASE (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      base->mode = BASE_MODE_PUSHING;
      res = TRUE;
      break;
    case GST_PAD_MODE_PULL:
      if (active) {
        base->mode = BASE_MODE_SCANNING;
        /* When working pull-based, we always use offsets for estimation */
        base->packetizer->calculate_offset = TRUE;
        base->packetizer->calculate_skew = FALSE;
        gst_segment_init (&base->segment, GST_FORMAT_BYTES);
        res =
            gst_pad_start_task (pad, (GstTaskFunction) mpegts_base_loop, base,
            NULL);
      } else
        res = gst_pad_stop_task (pad);
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static GstStateChangeReturn
mpegts_base_change_state (GstElement * element, GstStateChange transition)
{
  MpegTSBase *base;
  GstStateChangeReturn ret;

  base = GST_MPEGTS_BASE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      mpegts_base_reset (base);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      mpegts_base_reset (base);
      if (base->mode != BASE_MODE_PUSHING)
        base->mode = BASE_MODE_SCANNING;
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_mpegtsbase_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mpegts_base_debug, "mpegtsbase", 0,
      "MPEG transport stream base class");

  return TRUE;
}
