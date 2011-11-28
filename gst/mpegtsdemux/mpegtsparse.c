/*
 * mpegtsparse.c - 
 * Copyright (C) 2007 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
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

#include "mpegtsbase.h"
#include "mpegtsparse.h"
#include "gstmpegdesc.h"

/* latency in mseconds */
#define TS_LATENCY 700

#define TABLE_ID_UNSET 0xFF
#define RUNNING_STATUS_RUNNING 4

GST_DEBUG_CATEGORY_STATIC (mpegts_parse_debug);
#define GST_CAT_DEFAULT mpegts_parse_debug

typedef struct _MpegTSParsePad MpegTSParsePad;

typedef struct
{
  MpegTSBaseProgram program;
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

  GstTagList *tags;
  guint event_id;
};

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
  ARG_0,
  PROP_PROGRAM_NUMBERS,
  /* FILL ME */
};

static void
mpegts_parse_program_started (MpegTSBase * base, MpegTSBaseProgram * program);
static void
mpegts_parse_program_stopped (MpegTSBase * base, MpegTSBaseProgram * program);

static GstFlowReturn
mpegts_parse_push (MpegTSBase * base, MpegTSPacketizerPacket * packet,
    MpegTSPacketizerSection * section);
static void mpegts_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void mpegts_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void mpegts_parse_finalize (GObject * object);

static MpegTSParsePad *mpegts_parse_create_tspad (MpegTSParse2 * parse,
    const gchar * name);
static void mpegts_parse_destroy_tspad (MpegTSParse2 * parse,
    MpegTSParsePad * tspad);
static GstPad *mpegts_parse_activate_program (MpegTSParse2 * parse,
    MpegTSParseProgram * program);
static void mpegts_parse_reset_selected_programs (MpegTSParse2 * parse,
    gchar * programs);

static void mpegts_parse_pad_removed (GstElement * element, GstPad * pad);
static GstPad *mpegts_parse_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void mpegts_parse_release_pad (GstElement * element, GstPad * pad);
static gboolean mpegts_parse_src_pad_query (GstPad * pad, GstQuery * query);
static gboolean push_event (MpegTSBase * base, GstEvent * event);

GST_BOILERPLATE (MpegTSParse2, mpegts_parse, MpegTSBase, GST_TYPE_MPEGTS_BASE);

static void
mpegts_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &program_template);

  gst_element_class_set_details_simple (element_class,
      "MPEG transport stream parser", "Codec/Parser",
      "Parses MPEG2 transport streams",
      "Alessandro Decina <alessandro@nnva.org>, "
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");
}

static void
mpegts_parse_class_init (MpegTSParse2Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  MpegTSBaseClass *ts_class;

  element_class = GST_ELEMENT_CLASS (klass);
  element_class->pad_removed = mpegts_parse_pad_removed;
  element_class->request_new_pad = mpegts_parse_request_new_pad;
  element_class->release_pad = mpegts_parse_release_pad;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = mpegts_parse_set_property;
  gobject_class->get_property = mpegts_parse_get_property;
  gobject_class->finalize = mpegts_parse_finalize;

  g_object_class_install_property (gobject_class, PROP_PROGRAM_NUMBERS,
      g_param_spec_string ("program-numbers",
          "Program Numbers",
          "Colon separated list of programs", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  ts_class = GST_MPEGTS_BASE_CLASS (klass);
  ts_class->push = GST_DEBUG_FUNCPTR (mpegts_parse_push);
  ts_class->push_event = GST_DEBUG_FUNCPTR (push_event);
  ts_class->program_started = GST_DEBUG_FUNCPTR (mpegts_parse_program_started);
  ts_class->program_stopped = GST_DEBUG_FUNCPTR (mpegts_parse_program_stopped);
}

static void
mpegts_parse_init (MpegTSParse2 * parse, MpegTSParse2Class * klass)
{
  parse->need_sync_program_pads = FALSE;
  parse->program_numbers = g_strdup ("");
  parse->pads_to_add = NULL;
  parse->pads_to_remove = NULL;
  GST_MPEGTS_BASE (parse)->program_size = sizeof (MpegTSParseProgram);
}

static void
mpegts_parse_finalize (GObject * object)
{
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (object);

  g_free (parse->program_numbers);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
mpegts_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (object);

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
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (object);

  switch (prop_id) {
    case PROP_PROGRAM_NUMBERS:
      g_value_set_string (value, parse->program_numbers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static GstPad *
mpegts_parse_activate_program (MpegTSParse2 * parse,
    MpegTSParseProgram * program)
{
  MpegTSParsePad *tspad;
  gchar *pad_name;

  pad_name =
      g_strdup_printf ("program_%d",
      ((MpegTSBaseProgram *) program)->program_number);

  tspad = mpegts_parse_create_tspad (parse, pad_name);
  tspad->program_number = ((MpegTSBaseProgram *) program)->program_number;
  tspad->program = program;
  program->tspad = tspad;
  g_free (pad_name);
  gst_pad_set_active (tspad->pad, TRUE);
  program->active = TRUE;

  return tspad->pad;
}

static gboolean
push_event (MpegTSBase * base, GstEvent * event)
{
  MpegTSParse2 *parse = (MpegTSParse2 *) base;
  GList *tmp;

  for (tmp = GST_ELEMENT_CAST (parse)->srcpads; tmp; tmp = tmp->next) {
    GstPad *pad = (GstPad *) tmp->data;
    if (pad) {
      gst_event_ref (event);
      gst_pad_push_event (pad, event);
    }
  }
  return TRUE;
}

static GstPad *
mpegts_parse_deactivate_program (MpegTSParse2 * parse,
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
mpegts_parse_sync_program_pads (MpegTSParse2 * parse)
{
  GList *walk;

  GST_INFO_OBJECT (parse, "begin sync pads");
  for (walk = parse->pads_to_remove; walk; walk = walk->next)
    gst_element_remove_pad (GST_ELEMENT (parse), GST_PAD (walk->data));

  for (walk = parse->pads_to_add; walk; walk = walk->next)
    gst_element_add_pad (GST_ELEMENT (parse), GST_PAD (walk->data));

  if (parse->pads_to_add)
    g_list_free (parse->pads_to_add);

  if (parse->pads_to_remove)
    g_list_free (parse->pads_to_remove);

  GST_OBJECT_LOCK (parse);
  parse->pads_to_remove = NULL;
  parse->pads_to_add = NULL;
  parse->need_sync_program_pads = FALSE;
  GST_OBJECT_UNLOCK (parse);

  GST_INFO_OBJECT (parse, "end sync pads");
}

static void
foreach_program_activate_or_deactivate (gpointer key, gpointer value,
    gpointer data)
{
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (data);
  MpegTSParseProgram *program = (MpegTSParseProgram *) value;

  /* at this point selected programs have program->selected == 2,
   * unselected programs thay may have to be deactivated have selected == 1 and
   * unselected inactive programs have selected == 0 */

  switch (--program->selected) {
    case 1:
      /* selected */
      if (!program->active
          && ((MpegTSBaseProgram *) program)->pmt_pid != G_MAXUINT16)
        parse->pads_to_add =
            g_list_append (parse->pads_to_add,
            mpegts_parse_activate_program (parse, program));
      else {
        program->selected = 2;
      }
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
mpegts_parse_reset_selected_programs (MpegTSParse2 * parse,
    gchar * program_numbers)
{
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
      program =
          (MpegTSParseProgram *) mpegts_base_get_program ((MpegTSBase *) parse,
          program_number);
      if (program == NULL)
        /* create the program, it will get activated once we get a PMT for it */
        program = (MpegTSParseProgram *) mpegts_base_add_program ((MpegTSBase *)
            parse, program_number, G_MAXUINT16);
      program->selected = 2;
      ++walk;
    }
    g_strfreev (progs);
  }

  g_hash_table_foreach (((MpegTSBase *) parse)->programs,
      foreach_program_activate_or_deactivate, parse);

  if (parse->pads_to_remove || parse->pads_to_add)
    parse->need_sync_program_pads = TRUE;
  GST_OBJECT_UNLOCK (parse);
}


static MpegTSParsePad *
mpegts_parse_create_tspad (MpegTSParse2 * parse, const gchar * pad_name)
{
  GstPad *pad;
  MpegTSParsePad *tspad;

  pad = gst_pad_new_from_static_template (&program_template, pad_name);
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (mpegts_parse_src_pad_query));

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
mpegts_parse_destroy_tspad (MpegTSParse2 * parse, MpegTSParsePad * tspad)
{
  if (tspad->tags) {
    gst_tag_list_free (tspad->tags);
  }

  /* free the wrapper */
  g_free (tspad);
}

static void
mpegts_parse_pad_removed (GstElement * element, GstPad * pad)
{
  MpegTSParsePad *tspad;
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (element);

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
  MpegTSParse2 *parse;
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
  g_free (name);

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
mpegts_parse_tspad_push_section (MpegTSParse2 * parse, MpegTSParsePad * tspad,
    MpegTSPacketizerSection * section, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_NOT_LINKED;
  gboolean to_push = TRUE;

  if (tspad->program_number != -1) {
    if (tspad->program) {
      /* we push all sections to all pads except PMTs which we
       * only push to pads meant to receive that program number */
      if (section->table_id == 0x02) {
        /* PMT */
        if (section->subtable_extension != tspad->program_number)
          to_push = FALSE;
      }
    } else {
      /* there's a program filter on the pad but the PMT for the program has not
       * been parsed yet, ignore the pad until we get a PMT */
      to_push = FALSE;
      ret = GST_FLOW_OK;
    }
  }
  GST_DEBUG_OBJECT (parse,
      "pushing section: %d program number: %d table_id: %d", to_push,
      tspad->program_number, section->table_id);
  if (to_push) {
    ret = gst_pad_push (tspad->pad, buffer);
  } else {
    gst_buffer_unref (buffer);
    if (gst_pad_is_linked (tspad->pad))
      ret = GST_FLOW_OK;
  }

  return ret;
}

static GstFlowReturn
mpegts_parse_tspad_push (MpegTSParse2 * parse, MpegTSParsePad * tspad,
    guint16 pid, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_NOT_LINKED;
  MpegTSBaseStream **pad_pids = NULL;

  if (tspad->program_number != -1) {
    if (tspad->program) {
      MpegTSBaseProgram *bp = (MpegTSBaseProgram *) tspad->program;
      pad_pids = bp->streams;
      if (bp->tags) {
        gst_element_found_tags_for_pad (GST_ELEMENT_CAST (parse), tspad->pad,
            bp->tags);
        bp->tags = NULL;
      }
    } else {
      /* there's a program filter on the pad but the PMT for the program has not
       * been parsed yet, ignore the pad until we get a PMT */
      gst_buffer_unref (buffer);
      ret = GST_FLOW_OK;
      goto out;
    }
  }

  if (pad_pids == NULL || pad_pids[pid]) {
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
pad_clear_for_push (GstPad * pad, MpegTSParse2 * parse)
{
  MpegTSParsePad *tspad = (MpegTSParsePad *) gst_pad_get_element_private (pad);

  tspad->flow_return = GST_FLOW_NOT_LINKED;
  tspad->pushed = FALSE;
}

static GstFlowReturn
mpegts_parse_push (MpegTSBase * base, MpegTSPacketizerPacket * packet,
    MpegTSPacketizerSection * section)
{
  MpegTSParse2 *parse = (MpegTSParse2 *) base;
  guint32 pads_cookie;
  gboolean done = FALSE;
  GstPad *pad = NULL;
  MpegTSParsePad *tspad;
  guint16 pid;
  GstBuffer *buffer;
  GstFlowReturn ret;
  GList *srcpads;

  if (G_UNLIKELY (parse->need_sync_program_pads))
    mpegts_parse_sync_program_pads (parse);

  pid = packet->pid;
  buffer = gst_buffer_make_metadata_writable (packet->buffer);
  /* we have the same caps on all the src pads */
  gst_buffer_set_caps (buffer, base->packetizer->caps);

  GST_OBJECT_LOCK (parse);
  /* clear tspad->pushed on pads */
  g_list_foreach (GST_ELEMENT_CAST (parse)->srcpads,
      (GFunc) pad_clear_for_push, parse);
  if (GST_ELEMENT_CAST (parse)->srcpads)
    ret = GST_FLOW_NOT_LINKED;
  else
    ret = GST_FLOW_OK;

  /* Get cookie and source pads list */
  pads_cookie = GST_ELEMENT_CAST (parse)->pads_cookie;
  srcpads = GST_ELEMENT_CAST (parse)->srcpads;
  if (G_LIKELY (srcpads)) {
    pad = GST_PAD_CAST (srcpads->data);
    g_object_ref (pad);
  }
  GST_OBJECT_UNLOCK (parse);

  while (pad && !done) {
    tspad = gst_pad_get_element_private (pad);

    if (G_LIKELY (!tspad->pushed)) {
      /* ref the buffer as gst_pad_push takes a ref but we want to reuse the
       * same buffer for next pushes */
      gst_buffer_ref (buffer);
      if (section) {
        tspad->flow_return =
            mpegts_parse_tspad_push_section (parse, tspad, section, buffer);
      } else {
        tspad->flow_return =
            mpegts_parse_tspad_push (parse, tspad, pid, buffer);
      }
      tspad->pushed = TRUE;

      if (G_UNLIKELY (tspad->flow_return != GST_FLOW_OK
              && tspad->flow_return != GST_FLOW_NOT_LINKED)) {
        /* return the error upstream */
        ret = tspad->flow_return;
        done = TRUE;
      }

    }

    if (ret == GST_FLOW_NOT_LINKED)
      ret = tspad->flow_return;

    g_object_unref (pad);

    if (G_UNLIKELY (!done)) {
      GST_OBJECT_LOCK (parse);
      if (G_UNLIKELY (pads_cookie != GST_ELEMENT_CAST (parse)->pads_cookie)) {
        /* resync */
        GST_DEBUG ("resync");
        pads_cookie = GST_ELEMENT_CAST (parse)->pads_cookie;
        srcpads = GST_ELEMENT_CAST (parse)->srcpads;
      } else {
        GST_DEBUG ("getting next pad");
        /* Get next pad */
        srcpads = g_list_next (srcpads);
      }

      if (srcpads) {
        pad = GST_PAD_CAST (srcpads->data);
        g_object_ref (pad);
      } else
        done = TRUE;
      GST_OBJECT_UNLOCK (parse);
    }
  }

  gst_buffer_unref (buffer);
  packet->buffer = NULL;

  return ret;
}

static void
mpegts_parse_program_started (MpegTSBase * base, MpegTSBaseProgram * program)
{
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (base);
  MpegTSParseProgram *parseprogram = (MpegTSParseProgram *) program;
  if (parseprogram->selected == 2) {
    parse->pads_to_add =
        g_list_append (parse->pads_to_add,
        mpegts_parse_activate_program (parse, parseprogram));
    parseprogram->selected = 1;
    parse->need_sync_program_pads = TRUE;
  }

}

static void
mpegts_parse_program_stopped (MpegTSBase * base, MpegTSBaseProgram * program)
{
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (base);
  MpegTSParseProgram *parseprogram = (MpegTSParseProgram *) program;

  if (parseprogram->active) {
    parse->pads_to_remove =
        g_list_append (parse->pads_to_remove,
        mpegts_parse_deactivate_program (parse, parseprogram));
    parse->need_sync_program_pads = TRUE;
  }
}

static gboolean
mpegts_parse_src_pad_query (GstPad * pad, GstQuery * query)
{
  MpegTSParse2 *parse = GST_MPEGTS_PARSE (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      if ((res = gst_pad_peer_query (((MpegTSBase *) parse)->sinkpad, query))) {
        gboolean is_live;
        GstClockTime min_latency, max_latency;

        gst_query_parse_latency (query, &is_live, &min_latency, &max_latency);
        if (is_live) {
          min_latency += TS_LATENCY * GST_MSECOND;
          if (max_latency != GST_CLOCK_TIME_NONE)
            max_latency += TS_LATENCY * GST_MSECOND;
        }

        gst_query_set_latency (query, is_live, min_latency, max_latency);
      }

      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
  }
  gst_object_unref (parse);
  return res;
}

gboolean
gst_mpegtsparse_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mpegts_parse_debug, "tsparse", 0,
      "MPEG transport stream parser");

  gst_mpegtsdesc_init_debug ();

  return gst_element_register (plugin, "tsparse",
      GST_RANK_NONE, GST_TYPE_MPEGTS_PARSE);
}
