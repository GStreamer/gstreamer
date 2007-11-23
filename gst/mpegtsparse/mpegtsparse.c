/*
 * mpegtsparse.c - 
 * Copyright (C) 2007 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#include <stdlib.h>

#include "mpegtsparse.h"
#include "mpegtsparsemarshal.h"

GST_DEBUG_CATEGORY_STATIC (mpegts_parse_debug);
#define GST_CAT_DEFAULT mpegts_parse_debug

typedef struct _MpegTSParsePad MpegTSParsePad;

typedef struct
{
  guint16 pid;
  guint8 stream_type;
} MpegTSParseStream;

typedef struct
{
  gint program_number;
  guint16 pmt_pid;
  guint16 pcr_pid;
  GstStructure *pmt_info;
  GHashTable *streams;
  gint patcount;
  gint selected;
  gboolean active;
  MpegTSParsePad *tspad;
} MpegTSParseProgram;

struct _MpegTSParsePad
{
  GstPad *pad;

  /* the program number that the peer wants on this pad */
  gint program_number;
  MpegTSParseProgram *program;

  /* set to FALSE before a push and TRUE after */
  gboolean pushed;

  /* the return of the latest push */
  GstFlowReturn flow_return;
};

static GstElementDetails mpegts_parse_details =
GST_ELEMENT_DETAILS ("MPEG transport stream parser",
    "Codec/Parser",
    "Parses MPEG2 transport streams",
    "Alessandro Decina <alessandro@nnva.org>");

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts, " "systemstream = (boolean) true ")
    );

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src%d", GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpegts, " "systemstream = (boolean) true ")
    );

static GstStaticPadTemplate program_template =
GST_STATIC_PAD_TEMPLATE ("program_%d", GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/mpegts, " "systemstream = (boolean) true ")
    );

enum
{
  SIGNAL_PAT,
  SIGNAL_PMT,
  SIGNAL_NIT,
  SIGNAL_SDT,
  SIGNAL_EIT,
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  PROP_PROGRAM_NUMBERS,
  /* FILL ME */
};

static void mpegts_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void mpegts_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void mpegts_parse_dispose (GObject * object);
static void mpegts_parse_finalize (GObject * object);

static MpegTSParsePad *mpegts_parse_create_tspad (MpegTSParse * parse,
    const gchar * name);
static void mpegts_parse_destroy_tspad (MpegTSParse * parse,
    MpegTSParsePad * tspad);
static GstPad *mpegts_parse_activate_program (MpegTSParse * parse,
    MpegTSParseProgram * program);
static void mpegts_parse_free_program (MpegTSParseProgram * program);
static void mpegts_parse_free_stream (MpegTSParseStream * ptream);
static void mpegts_parse_reset_selected_programs (MpegTSParse * parse,
    gchar * programs);

static void mpegts_parse_pad_removed (GstElement * element, GstPad * pad);
static GstPad *mpegts_parse_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void mpegts_parse_release_pad (GstElement * element, GstPad * pad);
static GstFlowReturn mpegts_parse_chain (GstPad * pad, GstBuffer * buf);
static gboolean mpegts_parse_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn mpegts_parse_change_state (GstElement * element,
    GstStateChange transition);

static guint signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (MpegTSParse, mpegts_parse, GstElement, GST_TYPE_ELEMENT);

static void
mpegts_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&program_template));

  gst_element_class_set_details (element_class, &mpegts_parse_details);
}

static void
mpegts_parse_class_init (MpegTSParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (klass);
  element_class->pad_removed = mpegts_parse_pad_removed;
  element_class->request_new_pad = mpegts_parse_request_new_pad;
  element_class->release_pad = mpegts_parse_release_pad;
  element_class->change_state = mpegts_parse_change_state;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = mpegts_parse_set_property;
  gobject_class->get_property = mpegts_parse_get_property;
  gobject_class->dispose = mpegts_parse_dispose;
  gobject_class->finalize = mpegts_parse_finalize;

  g_object_class_install_property (gobject_class, PROP_PROGRAM_NUMBERS,
      g_param_spec_string ("program-numbers",
          "Program Numbers",
          "Colon separated list of programs", "", G_PARAM_READWRITE));

  signals[SIGNAL_PAT] =
      g_signal_new ("pat-info", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (MpegTSParseClass, pat_info), NULL, NULL,
      g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, GST_TYPE_STRUCTURE);
  signals[SIGNAL_PMT] =
      g_signal_new ("pmt-info", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (MpegTSParseClass, pmt_info), NULL, NULL,
      g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, GST_TYPE_STRUCTURE);
  signals[SIGNAL_NIT] =
      g_signal_new ("nit-info", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (MpegTSParseClass, nit_info), NULL, NULL,
      g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, GST_TYPE_STRUCTURE);
  signals[SIGNAL_SDT] =
      g_signal_new ("sdt-info", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (MpegTSParseClass, sdt_info), NULL, NULL,
      g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, GST_TYPE_STRUCTURE);
  signals[SIGNAL_EIT] =
      g_signal_new ("eit-info", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (MpegTSParseClass, eit_info), NULL, NULL,
      g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, GST_TYPE_STRUCTURE);
}

static gboolean
foreach_psi_pid_remove (gpointer key, gpointer value, gpointer data)
{
  return TRUE;
}

static void
mpegts_parse_reset (MpegTSParse * parse)
{
  mpegts_packetizer_clear (parse->packetizer);
  g_hash_table_foreach_remove (parse->psi_pids, foreach_psi_pid_remove, NULL);

  /* PAT */
  g_hash_table_insert (parse->psi_pids,
      GINT_TO_POINTER (0), GINT_TO_POINTER (1));

  /* NIT */
  g_hash_table_insert (parse->psi_pids,
      GINT_TO_POINTER (0x10), GINT_TO_POINTER (1));

  /* SDT */
  g_hash_table_insert (parse->psi_pids,
      GINT_TO_POINTER (0x11), GINT_TO_POINTER (1));

  /* EIT */
  g_hash_table_insert (parse->psi_pids,
      GINT_TO_POINTER (0x12), GINT_TO_POINTER (1));

  /* pmt pids will be added and removed dinamically */
}

static void
mpegts_parse_init (MpegTSParse * parse, MpegTSParseClass * klass)
{
  parse->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (parse->sinkpad, mpegts_parse_chain);
  gst_pad_set_event_function (parse->sinkpad, mpegts_parse_sink_event);
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->disposed = FALSE;
  parse->packetizer = mpegts_packetizer_new ();
  parse->program_numbers = g_strdup ("");
  parse->pads_to_add = NULL;
  parse->programs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) mpegts_parse_free_program);
  parse->psi_pids = g_hash_table_new (g_direct_hash, g_direct_equal);
  mpegts_parse_reset (parse);
}

static void
mpegts_parse_dispose (GObject * object)
{
  MpegTSParse *parse = GST_MPEGTS_PARSE (object);

  if (!parse->disposed) {
    g_object_unref (parse->packetizer);
    parse->disposed = TRUE;
  }

  if (G_OBJECT_CLASS (parent_class)->dispose)
    G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mpegts_parse_finalize (GObject * object)
{
  MpegTSParse *parse = GST_MPEGTS_PARSE (object);

  g_free (parse->program_numbers);
  if (parse->pat)
    gst_structure_free (parse->pat);
  g_hash_table_destroy (parse->programs);
  g_hash_table_destroy (parse->psi_pids);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
mpegts_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  MpegTSParse *parse = GST_MPEGTS_PARSE (object);

  switch (prop_id) {
    case PROP_PROGRAM_NUMBERS:
      mpegts_parse_reset_selected_programs (parse, g_value_dup_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
mpegts_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  MpegTSParse *parse = GST_MPEGTS_PARSE (object);

  switch (prop_id) {
    case PROP_PROGRAM_NUMBERS:
      g_value_set_string (value, parse->program_numbers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static MpegTSParseProgram *
mpegts_parse_add_program (MpegTSParse * parse,
    gint program_number, guint16 pmt_pid)
{
  MpegTSParseProgram *program;

  program = g_new0 (MpegTSParseProgram, 1);
  program->program_number = program_number;
  program->pmt_pid = pmt_pid;
  program->pcr_pid = G_MAXUINT16;
  program->streams = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) mpegts_parse_free_stream);
  program->patcount = 1;
  program->selected = 0;
  program->active = FALSE;

  g_hash_table_insert (parse->programs,
      GINT_TO_POINTER (program_number), program);

  return program;
}

static MpegTSParseProgram *
mpegts_parse_get_program (MpegTSParse * parse, gint program_number)
{
  MpegTSParseProgram *program;

  program = (MpegTSParseProgram *) g_hash_table_lookup (parse->programs,
      GINT_TO_POINTER ((gint) program_number));

  return program;
}

static GstPad *
mpegts_parse_activate_program (MpegTSParse * parse,
    MpegTSParseProgram * program)
{
  MpegTSParsePad *tspad;
  gchar *pad_name;

  pad_name = g_strdup_printf ("program_%d", program->program_number);

  tspad = mpegts_parse_create_tspad (parse, pad_name);
  tspad->program_number = program->program_number;
  tspad->program = program;
  program->tspad = tspad;
  g_free (pad_name);
  gst_pad_set_active (tspad->pad, TRUE);
  program->active = TRUE;

  return tspad->pad;
}

static GstPad *
mpegts_parse_deactivate_program (MpegTSParse * parse,
    MpegTSParseProgram * program)
{
  MpegTSParsePad *tspad;

  tspad = program->tspad;
  gst_pad_set_active (tspad->pad, FALSE);
  program->active = FALSE;

  /* tspad will be destroyed in GstElementClass::pad_removed */

  return tspad->pad;
}

static void
mpegts_parse_free_program (MpegTSParseProgram * program)
{
  if (program->pmt_info)
    gst_structure_free (program->pmt_info);

  g_hash_table_destroy (program->streams);

  g_free (program);
}

static void
mpegts_parse_remove_program (MpegTSParse * parse, gint program_number)
{
  g_hash_table_remove (parse->programs, GINT_TO_POINTER (program_number));
}

static void
mpegts_parse_sync_program_pads (MpegTSParse * parse,
    GList * to_add, GList * to_remove)
{
  GList *walk;

  for (walk = to_remove; walk; walk = walk->next)
    gst_element_remove_pad (GST_ELEMENT (parse), GST_PAD (walk->data));

  for (walk = to_add; walk; walk = walk->next)
    gst_element_add_pad (GST_ELEMENT (parse), GST_PAD (walk->data));

  if (to_add)
    g_list_free (to_add);

  if (to_remove)
    g_list_free (to_remove);
}


static MpegTSParseStream *
mpegts_parse_program_add_stream (MpegTSParse * parse,
    MpegTSParseProgram * program, guint16 pid, guint8 stream_type)
{
  MpegTSParseStream *stream;

  stream = g_new0 (MpegTSParseStream, 1);
  stream->pid = pid;
  stream->stream_type = stream_type;

  g_hash_table_insert (program->streams, GINT_TO_POINTER ((gint) pid), stream);

  return stream;
}

static void
foreach_program_activate_or_deactivate (gpointer key, gpointer value,
    gpointer data)
{
  MpegTSParse *parse = GST_MPEGTS_PARSE (data);
  MpegTSParseProgram *program = (MpegTSParseProgram *) value;

  /* at this point selected programs have program->selected == 2,
   * unselected programs thay may have to be deactivated have selected == 1 and
   * unselected inactive programs have selected == 0 */

  switch (--program->selected) {
    case 1:
      /* selected */
      if (!program->active && program->pmt_pid != G_MAXUINT16)
        parse->pads_to_add = g_list_append (parse->pads_to_add,
            mpegts_parse_activate_program (parse, program));
      break;
    case 0:
      /* unselected */
      if (program->active)
        parse->pads_to_remove = g_list_append (parse->pads_to_remove,
            mpegts_parse_deactivate_program (parse, program));
      break;
    case -1:
      /* was already unselected */
      program->selected = 0;
      break;
    default:
      g_return_if_reached ();
  }
}

static void
mpegts_parse_reset_selected_programs (MpegTSParse * parse,
    gchar * program_numbers)
{
  GList *pads_to_add = NULL;
  GList *pads_to_remove = NULL;

  GST_OBJECT_LOCK (parse);
  if (parse->program_numbers)
    g_free (parse->program_numbers);

  parse->program_numbers = program_numbers;

  if (*parse->program_numbers != '\0') {
    gint program_number;
    MpegTSParseProgram *program;
    gchar **progs, **walk;

    progs = g_strsplit (parse->program_numbers, ":", 0);

    walk = progs;
    while (*walk != NULL) {
      program_number = strtol (*walk, NULL, 0);
      program = mpegts_parse_get_program (parse, program_number);
      if (program == NULL)
        /* create the program, it will get activated once we get a PMT for it */
        program = mpegts_parse_add_program (parse, program_number, G_MAXUINT16);

      program->selected = 2;
      ++walk;
    }
    g_strfreev (progs);
  }

  g_hash_table_foreach (parse->programs,
      foreach_program_activate_or_deactivate, parse);

  pads_to_add = parse->pads_to_add;
  parse->pads_to_add = NULL;
  pads_to_remove = parse->pads_to_remove;
  parse->pads_to_remove = NULL;
  GST_OBJECT_UNLOCK (parse);

  mpegts_parse_sync_program_pads (parse, pads_to_add, pads_to_remove);
}

static void
mpegts_parse_free_stream (MpegTSParseStream * stream)
{
  g_free (stream);
}

static void
mpegts_parse_program_remove_stream (MpegTSParse * parse,
    MpegTSParseProgram * program, guint16 pid)
{
  g_hash_table_remove (program->streams, GINT_TO_POINTER ((gint) pid));
}

static MpegTSParsePad *
mpegts_parse_create_tspad (MpegTSParse * parse, const gchar * pad_name)
{
  GstPad *pad;
  MpegTSParsePad *tspad;

  pad = gst_pad_new_from_static_template (&program_template, pad_name);

  /* create our wrapper */
  tspad = g_new0 (MpegTSParsePad, 1);
  tspad->pad = pad;
  tspad->program_number = -1;
  tspad->program = NULL;
  tspad->pushed = FALSE;
  tspad->flow_return = GST_FLOW_NOT_LINKED;
  gst_pad_set_element_private (pad, tspad);

  return tspad;
}

static void
mpegts_parse_destroy_tspad (MpegTSParse * parse, MpegTSParsePad * tspad)
{
  /* free the wrapper */
  g_free (tspad);
}

static void
mpegts_parse_pad_removed (GstElement * element, GstPad * pad)
{
  MpegTSParsePad *tspad;
  MpegTSParse *parse = GST_MPEGTS_PARSE (element);

  if (gst_pad_get_direction (pad) == GST_PAD_SINK)
    return;

  tspad = (MpegTSParsePad *) gst_pad_get_element_private (pad);
  mpegts_parse_destroy_tspad (parse, tspad);

  if (GST_ELEMENT_CLASS (parent_class)->pad_removed)
    GST_ELEMENT_CLASS (parent_class)->pad_removed (element, pad);
}

static GstPad *
mpegts_parse_request_new_pad (GstElement * element, GstPadTemplate * template,
    const gchar * unused)
{
  MpegTSParse *parse;
  gchar *name;
  GstPad *pad;

  g_return_val_if_fail (template != NULL, NULL);
  g_return_val_if_fail (GST_IS_MPEGTS_PARSE (element), NULL);

  parse = GST_MPEGTS_PARSE (element);

  GST_OBJECT_LOCK (element);
  name = g_strdup_printf ("src%d", parse->req_pads++);
  GST_OBJECT_UNLOCK (element);

  pad = mpegts_parse_create_tspad (parse, name)->pad;
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (element, pad);

  return pad;
}

static void
mpegts_parse_release_pad (GstElement * element, GstPad * pad)
{
  g_return_if_fail (GST_IS_MPEGTS_PARSE (element));

  gst_pad_set_active (pad, FALSE);
  /* we do the cleanup in GstElement::pad-removed */
  gst_element_remove_pad (element, pad);
}

static GstFlowReturn
mpegts_parse_tspad_push (MpegTSParse * parse, MpegTSParsePad * tspad,
    guint16 pid, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_NOT_LINKED;
  GHashTable *pad_pids = NULL;
  guint16 pmt_pid = G_MAXUINT16;
  guint16 pcr_pid = G_MAXUINT16;

  if (tspad->program_number != -1) {
    if (tspad->program) {
      pad_pids = tspad->program->streams;
      pmt_pid = tspad->program->pmt_pid;
      pcr_pid = tspad->program->pcr_pid;
    } else {
      /* there's a program filter on the pad but the PMT for the program has not
       * been parsed yet, ignore the pad until we get a PMT */
      gst_buffer_unref (buffer);
      ret = GST_FLOW_OK;
      goto out;
    }
  }

  /* FIXME: send all the SI pids not only PAT and PMT */
  if (pad_pids == NULL || pid == pcr_pid || pid == pmt_pid || pid == 0 ||
      g_hash_table_lookup (pad_pids, GINT_TO_POINTER ((gint) pid)) != NULL) {
    /* push if there's no filter or if the pid is in the filter */
    ret = gst_pad_push (tspad->pad, buffer);
  } else {
    gst_buffer_unref (buffer);
    if (gst_pad_is_linked (tspad->pad))
      ret = GST_FLOW_OK;
  }

out:
  return ret;
}

static void
pad_clear_for_push (GstPad * pad, MpegTSParse * parse)
{
  MpegTSParsePad *tspad = (MpegTSParsePad *) gst_pad_get_element_private (pad);

  tspad->flow_return = GST_FLOW_NOT_LINKED;
  tspad->pushed = FALSE;
}

static GstFlowReturn
mpegts_parse_push (MpegTSParse * parse, MpegTSPacketizerPacket * packet)
{
  GstIterator *iterator;
  gboolean done = FALSE;
  gpointer pad = NULL;
  MpegTSParsePad *tspad;
  guint16 pid;
  GstBuffer *buffer;
  GstFlowReturn ret;

  pid = packet->pid;
  buffer = packet->buffer;
  /* we have the same caps on all the src pads */
  gst_buffer_set_caps (buffer,
      gst_static_pad_template_get_caps (&src_template));

  GST_OBJECT_LOCK (parse);
  /* clear tspad->pushed on pads */
  g_list_foreach (GST_ELEMENT_CAST (parse)->srcpads,
      (GFunc) pad_clear_for_push, parse);
  if (GST_ELEMENT_CAST (parse)->srcpads)
    ret = GST_FLOW_NOT_LINKED;
  else
    ret = GST_FLOW_OK;
  GST_OBJECT_UNLOCK (parse);

  iterator = gst_element_iterate_src_pads (GST_ELEMENT_CAST (parse));
  while (!done) {
    switch (gst_iterator_next (iterator, &pad)) {
      case GST_ITERATOR_OK:
        tspad = gst_pad_get_element_private (GST_PAD (pad));

        /* make sure to push only once if the iterator resyncs */
        if (!tspad->pushed) {
          /* ref the buffer as gst_pad_push takes a ref but we want to reuse the
           * same buffer for next pushes */
          gst_buffer_ref (buffer);
          tspad->flow_return =
              mpegts_parse_tspad_push (parse, tspad, pid, buffer);
          tspad->pushed = TRUE;

          if (GST_FLOW_IS_FATAL (tspad->flow_return)) {
            /* return the error upstream */
            ret = tspad->flow_return;
            done = TRUE;
          }
        }

        if (ret == GST_FLOW_NOT_LINKED)
          ret = tspad->flow_return;

        /* the iterator refs the pad */
        g_object_unref (GST_PAD (pad));
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iterator);
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      default:
        g_warning ("this should not be reached");
    }
  }

  gst_iterator_free (iterator);

  gst_buffer_unref (buffer);
  packet->buffer = NULL;

  return ret;
}

static gboolean
mpegts_parse_is_psi_pid (MpegTSParse * parse, guint16 pid)
{
  return g_hash_table_lookup (parse->psi_pids,
      GINT_TO_POINTER ((gint) pid)) != NULL;
}

static void
mpegts_parse_apply_pat (MpegTSParse * parse, GstStructure * pat_info)
{
  const GValue *value;
  GstStructure *old_pat;
  GstStructure *program_info;
  guint program_number;
  guint pid;
  MpegTSParseProgram *program;
  gint i;
  GList *pads_to_add = NULL;
  GList *pads_to_remove = NULL;
  const GValue *programs;
  gchar *dbg;

  old_pat = parse->pat;
  parse->pat = pat_info;

  dbg = gst_structure_to_string (pat_info);
  GST_INFO_OBJECT (parse, "PAT %s", dbg);
  g_free (dbg);

  g_signal_emit (parse, signals[SIGNAL_PAT], 0, pat_info);

  GST_OBJECT_LOCK (parse);
  programs = gst_structure_get_value (pat_info, "programs");
  /* activate the new table */
  for (i = 0; i < gst_value_list_get_size (programs); ++i) {
    value = gst_value_list_get_value (programs, i);

    program_info = g_value_get_boxed (value);
    gst_structure_get_uint (program_info, "program-number", &program_number);
    gst_structure_get_uint (program_info, "pid", &pid);

    program = mpegts_parse_get_program (parse, program_number);
    if (program) {
      if (program->pmt_pid != pid) {
        if (program->pmt_pid != G_MAXUINT16) {
          /* pmt pid changed */
          g_hash_table_remove (parse->psi_pids,
              GINT_TO_POINTER ((gint) program->pmt_pid));
        }

        program->pmt_pid = pid;
        g_hash_table_insert (parse->psi_pids,
            GINT_TO_POINTER ((gint) pid), GINT_TO_POINTER (1));
      }

      program->patcount += 1;
    } else {
      g_hash_table_insert (parse->psi_pids,
          GINT_TO_POINTER ((gint) pid), GINT_TO_POINTER (1));
      program = mpegts_parse_add_program (parse, program_number, pid);
    }

    if (program->selected && !program->active)
      parse->pads_to_add = g_list_append (parse->pads_to_add,
          mpegts_parse_activate_program (parse, program));
  }

  if (old_pat) {
    /* deactivate the old table */

    programs = gst_structure_get_value (old_pat, "programs");
    for (i = 0; i < gst_value_list_get_size (programs); ++i) {
      value = gst_value_list_get_value (programs, i);

      program_info = g_value_get_boxed (value);
      gst_structure_get_uint (program_info, "program-number", &program_number);
      gst_structure_get_uint (program_info, "pid", &pid);

      program = mpegts_parse_get_program (parse, program_number);
      if (program == NULL) {
        GST_DEBUG_OBJECT (parse, "broken PAT, duplicated entry for program %d",
            program_number);
        continue;
      }

      if (--program->patcount > 0)
        /* the program has been referenced by the new pat, keep it */
        continue;

      {
        gchar *dbg = gst_structure_to_string (program_info);

        GST_INFO_OBJECT (parse, "PAT removing program %s", dbg);
        g_free (dbg);
      }

      if (program->active)
        parse->pads_to_remove = g_list_append (parse->pads_to_remove,
            mpegts_parse_deactivate_program (parse, program));

      mpegts_parse_remove_program (parse, program_number);
      g_hash_table_remove (parse->psi_pids, GINT_TO_POINTER ((gint) pid));
    }

    gst_structure_free (old_pat);
  }

  pads_to_add = parse->pads_to_add;
  parse->pads_to_add = NULL;
  pads_to_remove = parse->pads_to_remove;
  parse->pads_to_remove = NULL;
  GST_OBJECT_UNLOCK (parse);

  mpegts_parse_sync_program_pads (parse, pads_to_add, pads_to_remove);
}

static void
mpegts_parse_apply_pmt (MpegTSParse * parse,
    guint16 pmt_pid, GstStructure * pmt_info)
{
  MpegTSParseProgram *program;
  guint program_number;
  guint pcr_pid;
  guint pid;
  guint stream_type;
  GstStructure *stream;
  gint i;
  const GValue *old_streams;
  const GValue *new_streams;
  const GValue *value;

  gst_structure_get_uint (pmt_info, "program-number", &program_number);
  gst_structure_get_uint (pmt_info, "pcr-pid", &pcr_pid);
  new_streams = gst_structure_get_value (pmt_info, "streams");

  GST_OBJECT_LOCK (parse);
  program = mpegts_parse_get_program (parse, program_number);
  if (program) {
    if (program->pmt_info) {
      /* deactivate old pmt */
      old_streams = gst_structure_get_value (program->pmt_info, "streams");

      for (i = 0; i < gst_value_list_get_size (old_streams); ++i) {
        value = gst_value_list_get_value (old_streams, i);
        stream = g_value_get_boxed (value);

        gst_structure_get_uint (stream, "pid", &pid);
        gst_structure_get_uint (stream, "stream-type", &stream_type);
        mpegts_parse_program_remove_stream (parse, program, (guint16) pid);
      }

      gst_structure_free (program->pmt_info);
    }
  } else {
    /* no PAT?? */
    g_hash_table_insert (parse->psi_pids,
        GINT_TO_POINTER ((gint) pmt_pid), GINT_TO_POINTER (1));
    program = mpegts_parse_add_program (parse, program_number, pid);
  }

  /* activate new pmt */
  program->pmt_info = pmt_info;
  program->pmt_pid = pmt_pid;
  /* FIXME: check if the pcr pid is changed */
  program->pcr_pid = pcr_pid;
  mpegts_parse_program_add_stream (parse, program, (guint16) pcr_pid, -1);

  for (i = 0; i < gst_value_list_get_size (new_streams); ++i) {
    value = gst_value_list_get_value (new_streams, i);
    stream = g_value_get_boxed (value);

    gst_structure_get_uint (stream, "pid", &pid);
    gst_structure_get_uint (stream, "stream-type", &stream_type);
    mpegts_parse_program_add_stream (parse, program,
        (guint16) pid, (guint8) stream_type);
  }
  GST_OBJECT_UNLOCK (parse);

  {
    gchar *dbg = gst_structure_to_string (pmt_info);

    GST_DEBUG_OBJECT (parse, "new pmt %s", dbg);
    g_free (dbg);
  }

  g_signal_emit (parse, signals[SIGNAL_PMT], 0, pmt_info);
}

static void
mpegts_parse_apply_nit (MpegTSParse * parse,
    guint16 pmt_pid, GstStructure * nit_info)
{
  g_signal_emit (parse, signals[SIGNAL_NIT], 0, nit_info);
}

static void
mpegts_parse_apply_sdt (MpegTSParse * parse,
    guint16 pmt_pid, GstStructure * sdt_info)
{
  g_signal_emit (parse, signals[SIGNAL_SDT], 0, sdt_info);
}

static void
mpegts_parse_apply_eit (MpegTSParse * parse,
    guint16 pmt_pid, GstStructure * eit_info)
{
  g_signal_emit (parse, signals[SIGNAL_EIT], 0, eit_info);
}

static gboolean
mpegts_parse_handle_psi (MpegTSParse * parse, MpegTSPacketizerSection * section)
{
  gboolean res = TRUE;

  switch (section->table_id) {
    case 0x00:
    {
      /* PAT */
      GstStructure *pat_info;

      pat_info = mpegts_packetizer_parse_pat (parse->packetizer, section);
      if (pat_info)
        mpegts_parse_apply_pat (parse, pat_info);
      else
        res = FALSE;

      break;
    }
    case 0x02:
    {
      /* PMT */
      GstStructure *pmt_info;

      pmt_info = mpegts_packetizer_parse_pmt (parse->packetizer, section);
      if (pmt_info)
        mpegts_parse_apply_pmt (parse, section->pid, pmt_info);
      else
        res = FALSE;

      break;
    }
    case 0x40:
      /* NIT, actual network */
    case 0x41:
      /* NIT, other network */
    {
      GstStructure *nit_info;

      nit_info = mpegts_packetizer_parse_nit (parse->packetizer, section);
      if (nit_info)
        mpegts_parse_apply_nit (parse, section->pid, nit_info);
      else
        res = FALSE;

      break;
    }
    case 0x42:
    {
      /* SDT */
      GstStructure *sdt_info;

      sdt_info = mpegts_packetizer_parse_sdt (parse->packetizer, section);
      if (sdt_info)
        mpegts_parse_apply_sdt (parse, section->pid, sdt_info);
      else
        res = FALSE;
      break;
    }
    case 0x4E:
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
      /* EIT, schedule */
    {
      /* EIT */
      GstStructure *eit_info;

      eit_info = mpegts_packetizer_parse_eit (parse->packetizer, section);
      if (eit_info)
        mpegts_parse_apply_eit (parse, section->pid, eit_info);
      else
        res = FALSE;
      break;
    }
    default:
      break;
  }

  return res;
}

static gboolean
mpegts_parse_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  MpegTSParse *parse =
      GST_MPEGTS_PARSE (gst_object_get_parent (GST_OBJECT (pad)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      mpegts_packetizer_clear (parse->packetizer);
      res = gst_pad_event_default (pad, event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
  }

  gst_object_unref (parse);
  return res;
}

static GstFlowReturn
mpegts_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;
  MpegTSParse *parse;
  gboolean parsed;
  MpegTSPacketizer *packetizer;
  MpegTSPacketizerPacket packet;

  parse = GST_MPEGTS_PARSE (gst_object_get_parent (GST_OBJECT (pad)));
  packetizer = parse->packetizer;

  mpegts_packetizer_push (parse->packetizer, buf);
  while (mpegts_packetizer_has_packets (parse->packetizer) &&
      !GST_FLOW_IS_FATAL (res)) {
    /* get the next packet */
    parsed = mpegts_packetizer_next_packet (packetizer, &packet);
    if (!parsed)
      /* bad header, skip the packet */
      goto next;

    /* parse PSI data */
    if (packet.payload != NULL && mpegts_parse_is_psi_pid (parse, packet.pid)) {
      MpegTSPacketizerSection section;

      parsed = mpegts_packetizer_push_section (packetizer, &packet, &section);
      if (!parsed)
        /* bad section data */
        goto next;

      if (section.complete) {
        /* section complete */
        parsed = mpegts_parse_handle_psi (parse, &section);
        gst_buffer_unref (section.buffer);
        if (!parsed)
          /* bad PSI table */
          goto next;
      }
    }

    /* push the packet downstream */
    res = mpegts_parse_push (parse, &packet);

  next:
    mpegts_packetizer_clear_packet (parse->packetizer, &packet);
  }

  gst_object_unref (parse);
  return res;
}

static GstStateChangeReturn
mpegts_parse_change_state (GstElement * element, GstStateChange transition)
{
  MpegTSParse *parse;
  GstStateChangeReturn ret;

  parse = GST_MPEGTS_PARSE (element);
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      mpegts_parse_reset (parse);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mpegts_parse_debug, "mpegtsparse", 0,
      "MPEG transport stream parser");

  mpegts_packetizer_init_debug ();

  return gst_element_register (plugin, "mpegtsparse",
      GST_RANK_NONE, GST_TYPE_MPEGTS_PARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpegtsparse",
    "MPEG-2 transport stream parser",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
