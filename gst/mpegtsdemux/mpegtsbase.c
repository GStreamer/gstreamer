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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

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
  ARG_0,
  /* FILL ME */
};

static void mpegts_base_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void mpegts_base_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void mpegts_base_dispose (GObject * object);
static void mpegts_base_finalize (GObject * object);
static void mpegts_base_free_program (MpegTSBaseProgram * program);
static void mpegts_base_free_stream (MpegTSBaseStream * ptream);
static gboolean mpegts_base_sink_activate (GstPad * pad, GstObject * parent);
static gboolean mpegts_base_sink_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);
static GstFlowReturn mpegts_base_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean mpegts_base_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstStateChangeReturn mpegts_base_change_state (GstElement * element,
    GstStateChange transition);
static void mpegts_base_get_tags_from_sdt (MpegTSBase * base,
    GstStructure * sdt_info);
static void mpegts_base_get_tags_from_eit (MpegTSBase * base,
    GstStructure * eit_info);
static gboolean
remove_each_program (gpointer key, MpegTSBaseProgram * program,
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

static const guint32 crc_tab[256] = {
  0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
  0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
  0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
  0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
  0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
  0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
  0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
  0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
  0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
  0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
  0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
  0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
  0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
  0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
  0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
  0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
  0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
  0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
  0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
  0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
  0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
  0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
  0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
  0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
  0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
  0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
  0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
  0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
  0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
  0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
  0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
  0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
  0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
  0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
  0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
  0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
  0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
  0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
  0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
  0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
  0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
  0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
  0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

/* relicenced to LGPL from fluendo ts demuxer */
static guint32
mpegts_base_calc_crc32 (guint8 * data, guint datalen)
{
  gint i;
  guint32 crc = 0xffffffff;

  for (i = 0; i < datalen; i++) {
    crc = (crc << 8) ^ crc_tab[((crc >> 24) ^ *data++) & 0xff];
  }
  return crc;
}

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
  gobject_class->set_property = mpegts_base_set_property;
  gobject_class->get_property = mpegts_base_get_property;
  gobject_class->dispose = mpegts_base_dispose;
  gobject_class->finalize = mpegts_base_finalize;

}

static void
mpegts_base_reset (MpegTSBase * base)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  mpegts_packetizer_clear (base->packetizer);
  memset (base->is_pes, 0, 1024);
  memset (base->known_psi, 0, 1024);

  /* Known PIDs : PAT, TSDT, IPMP CIT */
  MPEGTS_BIT_SET (base->known_psi, 0);
  MPEGTS_BIT_SET (base->known_psi, 2);
  MPEGTS_BIT_SET (base->known_psi, 3);
  /* TDT, TOT, ST */
  MPEGTS_BIT_SET (base->known_psi, 0x14);
  /* network synchronization */
  MPEGTS_BIT_SET (base->known_psi, 0x15);

  /* FIXME : Commenting the Following lines is to be in sync with the following
   * commit
   *
   * 61a885613316ce7657c36a6cd215b43f9dc67b79
   *     mpegtsparse: don't free PAT structure which may still be needed later
   */

  /* if (base->pat != NULL) */
  /*   gst_structure_free (base->pat); */
  /* base->pat = NULL; */
  /* pmt pids will be added and removed dynamically */

  gst_segment_init (&base->segment, GST_FORMAT_UNDEFINED);

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

  base->is_pes = g_new0 (guint8, 1024);
  base->known_psi = g_new0 (guint8, 1024);
  base->program_size = sizeof (MpegTSBaseProgram);
  base->stream_size = sizeof (MpegTSBaseStream);

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
    gst_structure_free (base->pat);
    base->pat = NULL;
  }
  g_hash_table_destroy (base->programs);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
mpegts_base_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* MpegTSBase *base = GST_MPEGTS_BASE (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
mpegts_base_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  /* MpegTSBase *base = GST_MPEGTS_BASE (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

/* returns NULL if no matching descriptor found *
 * otherwise returns a descriptor that needs to *
 * be freed */
guint8 *
mpegts_get_descriptor_from_stream (MpegTSBaseStream * stream, guint8 tag)
{
  GValueArray *descriptors = NULL;
  GstStructure *stream_info = stream->stream_info;
  guint8 *retval = NULL;
  int i;

  if (!gst_structure_has_field_typed (stream_info, "descriptors",
          G_TYPE_VALUE_ARRAY))
    goto beach;

  gst_structure_get (stream_info, "descriptors", G_TYPE_VALUE_ARRAY,
      &descriptors, NULL);

  for (i = 0; i < descriptors->n_values; i++) {
    GValue *value = g_value_array_get_nth (descriptors, i);
    GString *desc = g_value_dup_boxed (value);
    if (DESC_TAG (desc->str) == tag) {
      retval = (guint8 *) desc->str;
      g_string_free (desc, FALSE);
      break;
    } else
      g_string_free (desc, FALSE);
  }
  g_value_array_free (descriptors);

beach:
  return retval;
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
guint8 *
mpegts_get_descriptor_from_program (MpegTSBaseProgram * program, guint8 tag)
{
  GValueArray *descriptors = NULL;
  GstStructure *program_info;
  guint8 *retval = NULL;
  int i;

  if (G_UNLIKELY (program == NULL))
    goto beach;

  program_info = program->pmt_info;
  if (!gst_structure_has_field_typed (program_info, "descriptors",
          G_TYPE_VALUE_ARRAY))
    goto beach;

  gst_structure_get (program_info, "descriptors", G_TYPE_VALUE_ARRAY,
      &descriptors, NULL);

  for (i = 0; i < descriptors->n_values; i++) {
    GValue *value = g_value_array_get_nth (descriptors, i);
    GString *desc = g_value_dup_boxed (value);
    if (DESC_TAG (desc->str) == tag) {
      retval = (guint8 *) desc->str;
      g_string_free (desc, FALSE);
      break;
    } else
      g_string_free (desc, FALSE);
  }
  g_value_array_free (descriptors);

beach:
  return retval;
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

  if (program->pmt_info)
    gst_structure_free (program->pmt_info);

  for (tmp = program->stream_list; tmp; tmp = tmp->next)
    mpegts_base_free_stream ((MpegTSBaseStream *) tmp->data);
  if (program->stream_list)
    g_list_free (program->stream_list);

  g_free (program->streams);

  if (program->tags)
    gst_tag_list_unref (program->tags);

  g_free (program);
}

/* FIXME : This is being called by tsdemux::find_timestamps()
 * We need to avoid re-entrant code like that */
static gboolean
mpegts_base_stop_program (MpegTSBase * base, MpegTSBaseProgram * program)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  GST_DEBUG_OBJECT (base, "program_number : %d", program->program_number);

  if (klass->program_stopped)
    klass->program_stopped (base, program);

  return TRUE;
}

void
mpegts_base_remove_program (MpegTSBase * base, gint program_number)
{
  MpegTSBaseProgram *program;

  GST_DEBUG_OBJECT (base, "program_number : %d", program_number);
  program =
      (MpegTSBaseProgram *) g_hash_table_lookup (base->programs,
      GINT_TO_POINTER (program_number));
  if (program)
    mpegts_base_stop_program (base, program);

  g_hash_table_remove (base->programs, GINT_TO_POINTER (program_number));
}

static MpegTSBaseStream *
mpegts_base_program_add_stream (MpegTSBase * base,
    MpegTSBaseProgram * program, guint16 pid, guint8 stream_type,
    GstStructure * stream_info)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);
  MpegTSBaseStream *stream;

  GST_DEBUG ("pid:0x%04x, stream_type:0x%03x, stream_info:%" GST_PTR_FORMAT,
      pid, stream_type, stream_info);

  if (G_UNLIKELY (program->streams[pid])) {
    GST_WARNING ("Stream already present !");
    return NULL;
  }

  stream = g_malloc0 (base->stream_size);
  stream->pid = pid;
  stream->stream_type = stream_type;
  stream->stream_info = stream_info;

  program->streams[pid] = stream;
  program->stream_list = g_list_append (program->stream_list, stream);

  if (klass->stream_added)
    klass->stream_added (base, stream, program);

  return stream;
}

static void
mpegts_base_free_stream (MpegTSBaseStream * stream)
{
  g_free (stream);
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
  mpegts_base_free_stream (stream);
  program->streams[pid] = NULL;
}

/* Return TRUE if programs are equal */
static gboolean
mpegts_base_is_same_program (MpegTSBase * base, MpegTSBaseProgram * oldprogram,
    guint16 new_pmt_pid, GstStructure * new_pmt_info)
{
  guint i, nbstreams;
  guint pcr_pid;
  guint pid;
  guint stream_type;
  GstStructure *stream;
  MpegTSBaseStream *oldstream;
  gboolean sawpcrpid = FALSE;
  const GValue *new_streams;
  const GValue *value;

  if (oldprogram->pmt_pid != new_pmt_pid) {
    GST_DEBUG ("Different pmt_pid (new:0x%04x, old:0x%04x)", new_pmt_pid,
        oldprogram->pmt_pid);
    return FALSE;
  }

  gst_structure_id_get (new_pmt_info, QUARK_PCR_PID, G_TYPE_UINT, &pcr_pid,
      NULL);
  if (oldprogram->pcr_pid != pcr_pid) {
    GST_DEBUG ("Different pcr_pid (new:0x%04x, old:0x%04x)",
        pcr_pid, oldprogram->pcr_pid);
    return FALSE;
  }

  /* Check the streams */
  new_streams = gst_structure_id_get_value (new_pmt_info, QUARK_STREAMS);
  nbstreams = gst_value_list_get_size (new_streams);

  for (i = 0; i < nbstreams; ++i) {
    value = gst_value_list_get_value (new_streams, i);
    stream = g_value_get_boxed (value);

    gst_structure_id_get (stream, QUARK_PID, G_TYPE_UINT, &pid,
        QUARK_STREAM_TYPE, G_TYPE_UINT, &stream_type, NULL);
    oldstream = oldprogram->streams[pid];
    if (!oldstream) {
      GST_DEBUG ("New stream 0x%04x not present in old program", pid);
      return FALSE;
    }
    if (oldstream->stream_type != stream_type) {
      GST_DEBUG
          ("New stream 0x%04x has a different stream type (new:%d, old:%d)",
          pid, stream_type, oldstream->stream_type);
      return FALSE;
    }
    if (pid == oldprogram->pcr_pid)
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
  gint i, nbstreams;
  guint pid;
  GstStructure *stream;
  const GValue *streams;
  const GValue *value;
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  if (G_UNLIKELY (program->active == FALSE))
    return;

  GST_DEBUG_OBJECT (base, "Deactivating PMT");

  program->active = FALSE;

  if (program->pmt_info) {
    /* Inform subclasses we're deactivating this program */
    if (klass->program_stopped)
      klass->program_stopped (base, program);

    streams = gst_structure_id_get_value (program->pmt_info, QUARK_STREAMS);
    nbstreams = gst_value_list_get_size (streams);

    for (i = 0; i < nbstreams; ++i) {
      value = gst_value_list_get_value (streams, i);
      stream = g_value_get_boxed (value);

      gst_structure_id_get (stream, QUARK_PID, G_TYPE_UINT, &pid, NULL);
      mpegts_base_program_remove_stream (base, program, (guint16) pid);

      /* Only unset the is_pes bit if the PID isn't used in any other active
       * program */
      if (!mpegts_pid_in_active_programs (base, pid))
        MPEGTS_BIT_UNSET (base->is_pes, pid);
    }

    /* remove pcr stream */
    /* FIXME : This might actually be shared with another stream ? */
    mpegts_base_program_remove_stream (base, program, program->pcr_pid);
    if (!mpegts_pid_in_active_programs (base, program->pcr_pid))
      MPEGTS_BIT_UNSET (base->is_pes, program->pcr_pid);

    GST_DEBUG ("program stream_list is now %p", program->stream_list);
  }
}

static void
mpegts_base_activate_program (MpegTSBase * base, MpegTSBaseProgram * program,
    guint16 pmt_pid, GstStructure * pmt_info, gboolean initial_program)
{
  guint i, nbstreams;
  guint pcr_pid;
  guint pid;
  guint stream_type;
  GstStructure *stream;
  const GValue *new_streams;
  const GValue *value;
  MpegTSBaseClass *klass;

  if (G_UNLIKELY (program->active))
    return;

  GST_DEBUG ("Activating program %d", program->program_number);

  gst_structure_id_get (pmt_info, QUARK_PCR_PID, G_TYPE_UINT, &pcr_pid, NULL);

  /* activate new pmt */
  if (program->pmt_info)
    gst_structure_free (program->pmt_info);

  program->pmt_info = pmt_info;
  program->pmt_pid = pmt_pid;
  program->pcr_pid = pcr_pid;

  new_streams = gst_structure_id_get_value (pmt_info, QUARK_STREAMS);
  nbstreams = gst_value_list_get_size (new_streams);

  for (i = 0; i < nbstreams; ++i) {
    value = gst_value_list_get_value (new_streams, i);
    stream = g_value_get_boxed (value);

    gst_structure_id_get (stream, QUARK_PID, G_TYPE_UINT, &pid,
        QUARK_STREAM_TYPE, G_TYPE_UINT, &stream_type, NULL);
    MPEGTS_BIT_SET (base->is_pes, pid);
    mpegts_base_program_add_stream (base, program,
        (guint16) pid, (guint8) stream_type, stream);

  }
  /* We add the PCR pid last. If that PID is already used by one of the media
   * streams above, no new stream will be created */
  mpegts_base_program_add_stream (base, program, (guint16) pcr_pid, -1, NULL);
  MPEGTS_BIT_SET (base->is_pes, pcr_pid);

  program->active = TRUE;
  program->initial_program = initial_program;

  klass = GST_MPEGTS_BASE_GET_CLASS (base);
  if (klass->program_started != NULL)
    klass->program_started (base, program);

  GST_DEBUG_OBJECT (base, "new pmt %" GST_PTR_FORMAT, pmt_info);
}

static inline gboolean
mpegts_base_is_psi (MpegTSBase * base, MpegTSPacketizerPacket * packet)
{
  gboolean retval = FALSE;
  guint8 *data, table_id = TABLE_ID_UNSET, pointer;
  int i;

  static const guint8 si_tables[] =
      { 0x00, 0x01, 0x02, 0x03, 0x40, 0x41, 0x42, 0x46, 0x4A,
    0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
    0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7E, 0x7F,
    TABLE_ID_UNSET
  };

  /* check if it is a pes pid */
  if (MPEGTS_BIT_IS_SET (base->is_pes, packet->pid))
    goto invalid_pid;

  /* check if it part of the PIDs we know contain PSI */
  if (!MPEGTS_BIT_IS_SET (base->known_psi, packet->pid))
    goto invalid_pid;

  if (packet->payload_unit_start_indicator) {
    data = packet->data;
    pointer = *data++;
    data += pointer;

    /* 'pointer' value may be invalid on malformed packet
     * so we need to avoid out of range */
    if (!(data < packet->data_end)) {
      GST_WARNING_OBJECT (base,
          "Section pointer value exceeds packet size: 0x%x", pointer);
      return FALSE;
    }

    table_id = *(packet->data);
  } else {
    MpegTSPacketizerStream *stream = (MpegTSPacketizerStream *)
        base->packetizer->streams[packet->pid];

    if (stream)
      table_id = stream->section_table_id;
  }

  if (G_UNLIKELY (table_id == TABLE_ID_UNSET))
    goto beach;

  for (i = 0; si_tables[i] != TABLE_ID_UNSET; i++) {
    if (G_UNLIKELY (si_tables[i] == table_id)) {
      retval = TRUE;
      break;
    }
  }

beach:
  GST_DEBUG_OBJECT (base, "Packet of pid 0x%04x (table_id 0x%02x) is psi: %d",
      packet->pid, table_id, retval);
  return retval;

invalid_pid:
  GST_LOG_OBJECT (base, "Packet of pid 0x%04x doesn't belong to a SI stream",
      packet->pid);
  return FALSE;
}

static void
mpegts_base_apply_pat (MpegTSBase * base, GstStructure * pat_info)
{
  const GValue *value;
  GstStructure *old_pat;
  GstStructure *program_info;
  guint program_number;
  guint pid;
  MpegTSBaseProgram *program;
  gint i, nbprograms;
  const GValue *programs;

  GST_INFO_OBJECT (base, "PAT %" GST_PTR_FORMAT, pat_info);

  /* Applying a new PAT does two things:
   * * It adds the new programs to the list of programs this element handles
   *   and increments at the same time the number of times a program is referenced.
   *
   * * If there was a previously active PAT, It decrements the reference count
   *   of all program it used. If a program is no longer needed, it is removed.
   */

  old_pat = base->pat;
  base->pat = pat_info;

  gst_element_post_message (GST_ELEMENT_CAST (base),
      gst_message_new_element (GST_OBJECT (base),
          gst_structure_copy (pat_info)));


  GST_LOG ("Activating new Program Association Table");
  /* activate the new table */
  programs = gst_structure_id_get_value (pat_info, QUARK_PROGRAMS);
  nbprograms = gst_value_list_get_size (programs);
  for (i = 0; i < nbprograms; ++i) {
    value = gst_value_list_get_value (programs, i);

    program_info = g_value_get_boxed (value);
    gst_structure_id_get (program_info, QUARK_PROGRAM_NUMBER, G_TYPE_UINT,
        &program_number, QUARK_PID, G_TYPE_UINT, &pid, NULL);

    program = mpegts_base_get_program (base, program_number);
    if (program) {
      /* IF the program already existed, just check if the PMT PID changed */
      if (program->pmt_pid != pid) {
        if (program->pmt_pid != G_MAXUINT16) {
          /* pmt pid changed */
          /* FIXME: when this happens it may still be pmt pid of another
           * program, so setting to False may make it go through expensive
           * path in is_psi unnecessarily */
          MPEGTS_BIT_UNSET (base->known_psi, program->pmt_pid);
        }

        program->pmt_pid = pid;
        MPEGTS_BIT_SET (base->known_psi, pid);
      }
    } else {
      /* Create a new program */
      program = mpegts_base_add_program (base, program_number, pid);
    }
    /* We mark this program as being referenced by one PAT */
    program->patcount += 1;
  }

  if (old_pat) {
    /* deactivate the old table */
    GST_LOG ("Deactivating old Program Association Table");

    programs = gst_structure_id_get_value (old_pat, QUARK_PROGRAMS);
    nbprograms = gst_value_list_get_size (programs);
    for (i = 0; i < nbprograms; ++i) {
      value = gst_value_list_get_value (programs, i);

      program_info = g_value_get_boxed (value);
      gst_structure_id_get (program_info,
          QUARK_PROGRAM_NUMBER, G_TYPE_UINT, &program_number,
          QUARK_PID, G_TYPE_UINT, &pid, NULL);

      program = mpegts_base_get_program (base, program_number);
      if (G_UNLIKELY (program == NULL)) {
        GST_DEBUG_OBJECT (base, "broken PAT, duplicated entry for program %d",
            program_number);
        continue;
      }

      if (--program->patcount > 0)
        /* the program has been referenced by the new pat, keep it */
        continue;

      GST_INFO_OBJECT (base, "PAT removing program %" GST_PTR_FORMAT,
          program_info);

      mpegts_base_deactivate_program (base, program);
      mpegts_base_remove_program (base, program_number);
      /* FIXME: when this happens it may still be pmt pid of another
       * program, so setting to False may make it go through expensive
       * path in is_psi unnecessarily */
      MPEGTS_BIT_SET (base->known_psi, pid);
      mpegts_packetizer_remove_stream (base->packetizer, pid);
    }

    gst_structure_free (old_pat);
  }
}

static void
mpegts_base_apply_pmt (MpegTSBase * base,
    guint16 pmt_pid, GstStructure * pmt_info)
{
  MpegTSBaseProgram *program, *old_program;
  guint program_number;
  gboolean initial_program = TRUE;

  /* FIXME : not so sure this is valid anymore */
  if (G_UNLIKELY (base->seen_pat == FALSE)) {
    GST_WARNING ("Got pmt without pat first. Returning");
    /* remove the stream since we won't get another PMT otherwise */
    mpegts_packetizer_remove_stream (base->packetizer, pmt_pid);
    return;
  }

  gst_structure_id_get (pmt_info, QUARK_PROGRAM_NUMBER, G_TYPE_UINT,
      &program_number, NULL);

  GST_DEBUG ("Applying PMT (program_number:%d, pid:0x%04x)",
      program_number, pmt_pid);

  /* In order for stream switching to happen properly in decodebin(2),
   * we need to first add the new pads (i.e. activate the new program)
   * before removing the old ones (i.e. deactivating the old program)
   */

  old_program = mpegts_base_get_program (base, program_number);
  if (G_UNLIKELY (old_program == NULL))
    goto no_program;

  if (G_UNLIKELY (mpegts_base_is_same_program (base, old_program, pmt_pid,
              pmt_info)))
    goto same_program;

  /* If the current program is active, this means we have a new program */
  if (old_program->active) {
    old_program = mpegts_base_steal_program (base, program_number);
    program = mpegts_base_new_program (base, program_number, pmt_pid);
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
  mpegts_base_activate_program (base, program, pmt_pid, pmt_info,
      initial_program);

  gst_element_post_message (GST_ELEMENT_CAST (base),
      gst_message_new_element (GST_OBJECT (base),
          gst_structure_copy (pmt_info)));

  return;

no_program:
  {
    GST_ERROR ("Attempted to apply a PMT on a program that wasn't created");
    gst_structure_free (pmt_info);
    return;
  }

same_program:
  {
    GST_DEBUG ("Not applying identical program");
    gst_structure_free (pmt_info);
    return;
  }
}

static void
mpegts_base_apply_cat (MpegTSBase * base, GstStructure * cat_info)
{
  GST_DEBUG_OBJECT (base, "CAT %" GST_PTR_FORMAT, cat_info);

  gst_element_post_message (GST_ELEMENT_CAST (base),
      gst_message_new_element (GST_OBJECT (base), cat_info));
}

static void
mpegts_base_apply_nit (MpegTSBase * base,
    guint16 pmt_pid, GstStructure * nit_info)
{
  GST_DEBUG_OBJECT (base, "NIT %" GST_PTR_FORMAT, nit_info);

  gst_element_post_message (GST_ELEMENT_CAST (base),
      gst_message_new_element (GST_OBJECT (base), nit_info));
}

static void
mpegts_base_apply_sdt (MpegTSBase * base,
    guint16 pmt_pid, GstStructure * sdt_info)
{
  GST_DEBUG_OBJECT (base, "SDT %" GST_PTR_FORMAT, sdt_info);

  mpegts_base_get_tags_from_sdt (base, sdt_info);

  gst_element_post_message (GST_ELEMENT_CAST (base),
      gst_message_new_element (GST_OBJECT (base), sdt_info));
}

static void
mpegts_base_apply_eit (MpegTSBase * base,
    guint16 pmt_pid, GstStructure * eit_info)
{
  GST_DEBUG_OBJECT (base, "EIT %" GST_PTR_FORMAT, eit_info);

  mpegts_base_get_tags_from_eit (base, eit_info);

  gst_element_post_message (GST_ELEMENT_CAST (base),
      gst_message_new_element (GST_OBJECT (base), eit_info));
}

static void
mpegts_base_apply_tdt (MpegTSBase * base,
    guint16 tdt_pid, GstStructure * tdt_info)
{
  gst_element_post_message (GST_ELEMENT_CAST (base),
      gst_message_new_element (GST_OBJECT (base),
          gst_structure_copy (tdt_info)));

  GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base,
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, tdt_info));
}


gboolean
mpegts_base_handle_psi (MpegTSBase * base, MpegTSPacketizerSection * section)
{
  gboolean res = TRUE;
  GstStructure *structure = NULL;

  /* table ids 0x70 - 0x73 do not have a crc (EN 300 468) */
  /* table ids 0x75 - 0x77 do not have a crc (TS 102 323) */
  /* table id 0x7e does not have a crc (EN 300 468) */
  /* table ids 0x80 - 0x8f do not have a crc (CA_message section ETR 289) */
  if (G_LIKELY ((section->table_id < 0x70 || section->table_id > 0x73)
          && (section->table_id < 0x75 || section->table_id > 0x77)
          && (section->table_id < 0x80 || section->table_id > 0x8f)
          && (section->table_id != 0x7e))) {
    if (G_UNLIKELY (mpegts_base_calc_crc32 (section->data,
                section->section_length) != 0)) {
      GST_WARNING_OBJECT (base, "bad crc in psi pid 0x%04x (table_id:0x%02x)",
          section->pid, section->table_id);
      return FALSE;
    }
  }

  GST_DEBUG ("Handling PSI (pid: 0x%04x , table_id: 0x%02x)",
      section->pid, section->table_id);

  switch (section->table_id) {
    case TABLE_ID_PROGRAM_ASSOCIATION:
      /* PAT */
      structure = mpegts_packetizer_parse_pat (base->packetizer, section);
      if (G_LIKELY (structure)) {
        mpegts_base_apply_pat (base, structure);
        if (base->seen_pat == FALSE) {
          base->seen_pat = TRUE;
          GST_DEBUG ("First PAT offset: %" G_GUINT64_FORMAT, section->offset);
          mpegts_packetizer_set_reference_offset (base->packetizer,
              section->offset);
        }

      } else
        res = FALSE;

      break;
    case TABLE_ID_CONDITIONAL_ACCESS:
      /* CAT */
      structure = mpegts_packetizer_parse_cat (base->packetizer, section);
      if (structure)
        mpegts_base_apply_cat (base, structure);
      else
        res = FALSE;
      break;
    case TABLE_ID_TS_PROGRAM_MAP:
      /* PMT */
      structure = mpegts_packetizer_parse_pmt (base->packetizer, section);
      if (G_LIKELY (structure))
        mpegts_base_apply_pmt (base, section->pid, structure);
      else
        res = FALSE;

      break;
    case TABLE_ID_NETWORK_INFORMATION_ACTUAL_NETWORK:
      /* NIT, actual network */
    case TABLE_ID_NETWORK_INFORMATION_OTHER_NETWORK:
      /* NIT, other network */
      structure = mpegts_packetizer_parse_nit (base->packetizer, section);
      if (G_LIKELY (structure))
        mpegts_base_apply_nit (base, section->pid, structure);
      else
        res = FALSE;

      break;
    case TABLE_ID_SERVICE_DESCRIPTION_ACTUAL_TS:
    case TABLE_ID_SERVICE_DESCRIPTION_OTHER_TS:
      structure = mpegts_packetizer_parse_sdt (base->packetizer, section);
      if (G_LIKELY (structure))
        mpegts_base_apply_sdt (base, section->pid, structure);
      else
        res = FALSE;
      break;
    case 0x4E:
    case 0x4F:
      /* EIT, present/following */
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x58:
    case 0x59:
    case 0x5A:
    case 0x5B:
    case 0x5C:
    case 0x5D:
    case 0x5E:
    case 0x5F:
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6A:
    case 0x6B:
    case 0x6C:
    case 0x6D:
    case 0x6E:
    case 0x6F:
      /* EIT, schedule */
      /* FIXME : Can take up to 50% of total mpeg-ts demuxing cpu usage ! */
      structure = mpegts_packetizer_parse_eit (base->packetizer, section);
      if (G_LIKELY (structure))
        mpegts_base_apply_eit (base, section->pid, structure);
      else
        res = FALSE;
      break;
    case TABLE_ID_TIME_DATE:
      /* TDT (Time and Date table) */
      structure = mpegts_packetizer_parse_tdt (base->packetizer, section);
      if (G_LIKELY (structure))
        mpegts_base_apply_tdt (base, section->pid, structure);
      else
        res = FALSE;
      break;
    case TABLE_ID_TIME_OFFSET:
      /* TOT (Time Offset table) */
      structure = mpegts_packetizer_parse_tot (base->packetizer, section);
      if (G_LIKELY (structure))
        mpegts_base_apply_tdt (base, section->pid, structure);
      else
        res = FALSE;
      break;
    default:
      GST_WARNING ("Unhandled or unknown section type (table_id 0x%02x)",
          section->table_id);
      break;
  }

  return res;
}

static void
mpegts_base_get_tags_from_sdt (MpegTSBase * base, GstStructure * sdt_info)
{
  const GValue *services;
  guint i;

  services = gst_structure_get_value (sdt_info, "services");

  for (i = 0; i < gst_value_list_get_size (services); i++) {
    const GstStructure *service;
    const gchar *sid_str;
    gchar *tmp;
    gint program_number;
    MpegTSBaseProgram *program;

    service = gst_value_get_structure (gst_value_list_get_value (services, i));

    /* get program_number from structure name
     * which looks like service-%d */
    sid_str = gst_structure_get_name (service);
    tmp = g_strstr_len (sid_str, -1, "-");
    if (!tmp)
      continue;
    program_number = atoi (++tmp);

    program = mpegts_base_get_program (base, program_number);
    if (program && !program->tags) {
      program->tags = gst_tag_list_new (GST_TAG_ARTIST,
          gst_structure_get_string (service, "name"), NULL);
    }
  }
}

static void
mpegts_base_get_tags_from_eit (MpegTSBase * base, GstStructure * eit_info)
{
  const GValue *events;
  guint i;
  guint program_number;
  MpegTSBaseProgram *program;
  gboolean present_following;

  gst_structure_get_uint (eit_info, "service-id", &program_number);
  program = mpegts_base_get_program (base, program_number);

  gst_structure_get_boolean (eit_info, "present-following", &present_following);

  if (program && present_following) {
    events = gst_structure_get_value (eit_info, "events");

    for (i = 0; i < gst_value_list_get_size (events); i++) {
      const GstStructure *event;
      const gchar *title;
      guint status;
      guint event_id;
      guint duration;

      event = gst_value_get_structure (gst_value_list_get_value (events, i));

      title = gst_structure_get_string (event, "name");
      gst_structure_get_uint (event, "event-id", &event_id);
      gst_structure_get_uint (event, "running-status", &status);

      if (title && event_id != program->event_id
          && status == RUNNING_STATUS_RUNNING) {
        gst_structure_get_uint (event, "duration", &duration);

        program->event_id = event_id;
        program->tags = gst_tag_list_new (GST_TAG_TITLE,
            title, GST_TAG_DURATION, duration * GST_SECOND, NULL);
      }
    }
  }
}

static gboolean
remove_each_program (gpointer key, MpegTSBaseProgram * program,
    MpegTSBase * base)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  /* First deactivate it */
  mpegts_base_deactivate_program (base, program);
  /* Then remove it */
  if (klass->program_stopped)
    klass->program_stopped (base, program);

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

static inline void
mpegts_base_flush (MpegTSBase * base)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  /* Call implementation */
  if (G_UNLIKELY (klass->flush == NULL))
    GST_WARNING_OBJECT (base, "Class doesn't have a 'flush' implementation !");
  else
    klass->flush (base);
}

static gboolean
mpegts_base_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  MpegTSBase *base = GST_MPEGTS_BASE (parent);

  GST_DEBUG_OBJECT (base, "Got event %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &base->segment);
      /* Check if we need to switch PCR/PTS handling */
      if (base->segment.format == GST_FORMAT_TIME) {
        base->packetizer->calculate_offset = FALSE;
        base->packetizer->calculate_skew = TRUE;
      } else {
        base->packetizer->calculate_offset = TRUE;
        base->packetizer->calculate_skew = FALSE;
      }
      gst_event_unref (event);
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
      mpegts_packetizer_flush (base->packetizer);
      mpegts_base_flush (base);
      gst_segment_init (&base->segment, GST_FORMAT_UNDEFINED);
      base->seen_pat = FALSE;
      break;
      /* Passthrough */
    default:
      res = GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, event);
  }

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

static inline GstFlowReturn
mpegts_base_push (MpegTSBase * base, MpegTSPacketizerPacket * packet,
    MpegTSPacketizerSection * section)
{
  MpegTSBaseClass *klass = GST_MPEGTS_BASE_GET_CLASS (base);

  /* Call implementation */
  if (G_UNLIKELY (klass->push == NULL)) {
    GST_ERROR_OBJECT (base, "Class doesn't have a 'push' implementation !");
    return GST_FLOW_ERROR;
  }

  return klass->push (base, packet, section);
}

static GstFlowReturn
mpegts_base_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;
  MpegTSBase *base;
  gboolean based;
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

    /* FIXME : Handle the case where we have multiple sections in one
     * packet ! 
     * See bug #677443
     */
    /* base PSI data */
    if (packet.payload != NULL && mpegts_base_is_psi (base, &packet)) {
      MpegTSPacketizerSection section;
      based = mpegts_packetizer_push_section (packetizer, &packet, &section);
      if (G_UNLIKELY (!based))
        /* bad section data */
        goto next;

      if (G_LIKELY (section.complete)) {
        /* section complete */
        based = mpegts_base_handle_psi (base, &section);

        if (G_UNLIKELY (!based)) {
          /* bad PSI table */
          goto next;
        }
      }
      /* we need to push section packet downstream */
      res = mpegts_base_push (base, &packet, &section);

    } else if (MPEGTS_BIT_IS_SET (base->is_pes, packet.pid)) {
      /* push the packet downstream */
      res = mpegts_base_push (base, &packet, NULL);
    }

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
  GstFlowReturn ret;
  GstBuffer *buf = NULL;
  guint i;
  gboolean done = FALSE;
  MpegTSPacketizerPacketReturn pret;
  gint64 tmpval;
  guint64 upstream_size, seek_pos;
  GstFormat format;
  guint initial_pcr_seen;

  GST_DEBUG ("Scanning for initial sync point");

  /* Find initial sync point and at least 5 PCR values */
  for (i = 0; i < 10 && !done; i++) {
    GST_DEBUG ("Grabbing %d => %d", i * 65536, 65536);

    ret = gst_pad_pull_range (base->sinkpad, i * 65536, 65536, &buf);
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
        if (pret != PACKET_BAD &&
            mpegts_packetizer_get_seen_pcr (base->packetizer) >= 5) {
          GST_DEBUG ("Got enough initial PCR");
          done = TRUE;
          break;
        }
      }
    }
  }

  initial_pcr_seen = mpegts_packetizer_get_seen_pcr (base->packetizer);
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
            mpegts_packetizer_get_seen_pcr (base->packetizer) >
            initial_pcr_seen) {
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
      GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base, gst_event_new_eos ());
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

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME)
    return FALSE;

  if (base->mode == BASE_MODE_PUSHING) {
    /* First try if upstream supports seeking in TIME format */
    if (gst_pad_push_event (base->sinkpad, gst_event_ref (event))) {
      GST_DEBUG ("upstream handled SEEK event");
      return TRUE;
    }
    /* FIXME : Actually ... it is supported, we just need to convert
     * the seek event to BYTES */
    GST_ERROR ("seeking in push mode not supported");
    goto push_mode;
  }

  GST_DEBUG ("seek event, rate: %f start: %" GST_TIME_FORMAT
      " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
      GST_TIME_ARGS (stop));

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* stop streaming, either by flushing or by pausing the task */
  base->mode = BASE_MODE_SEEKING;
  if (flush) {
    GST_DEBUG_OBJECT (base, "sending flush start");
    gst_pad_push_event (base->sinkpad, gst_event_new_flush_start ());
    GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base,
        gst_event_new_flush_start ());
  } else
    gst_pad_pause_task (base->sinkpad);

  /* wait for streaming to finish */
  GST_PAD_STREAM_LOCK (base->sinkpad);

  if (flush) {
    /* send a FLUSH_STOP for the sinkpad, since we need data for seeking */
    GST_DEBUG_OBJECT (base, "sending flush stop");
    gst_pad_push_event (base->sinkpad, gst_event_new_flush_stop (TRUE));
    /* And actually flush our pending data */
    mpegts_base_flush (base);
    mpegts_packetizer_flush (base->packetizer);
  }

  if (flags & (GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_SKIP)) {
    GST_WARNING ("seek flags 0x%x are not supported", (int) flags);
    goto done;
  }


  if (format == GST_FORMAT_TIME) {
    /* If the subclass can seek, do that */
    if (klass->seek) {
      ret = klass->seek (base, event);
      if (G_UNLIKELY (ret != GST_FLOW_OK)) {
        GST_WARNING ("seeking failed %s", gst_flow_get_name (ret));
      }
    } else {
      GST_WARNING ("subclass has no seek implementation");
    }
  }

  if (flush) {
    /* if we sent a FLUSH_START, we now send a FLUSH_STOP */
    GST_DEBUG_OBJECT (base, "sending flush stop");
    //gst_pad_push_event (base->sinkpad, gst_event_new_flush_stop ());
    GST_MPEGTS_BASE_GET_CLASS (base)->push_event (base,
        gst_event_new_flush_stop (TRUE));
  }
  //else
done:
  gst_pad_start_task (base->sinkpad, (GstTaskFunction) mpegts_base_loop, base,
      NULL);
push_mode:
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

  gst_mpegtsdesc_init_debug ();

  return TRUE;
}
