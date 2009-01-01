/* GStreamer
 * Copyright (C) 2007 Stefan Kost <ensonic@users.sf.net>
 *
 * gstdebugutils.c: debugging and analysis utillities
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
/* TODO:
 * edge [ constraint=false ];
 *   this creates strange graphs ("minlen=0" is better)
 * try puting src/sink ghostpads for each bin into invisible clusters
 *
 * for more compact nodes, try
 * - changing node-shape from box into record
 * - use labels like : element [ label="{element | <src> src | <sink> sink}"]
 * - point to record-connectors : element1:src -> element2:sink
 */

#include "gst_private.h"
#include "gstdebugutils.h"

#ifndef GST_DISABLE_GST_DEBUG

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "gstinfo.h"
#include "gstbin.h"
#include "gstobject.h"
#include "gstghostpad.h"
#include "gstpad.h"
#include "gstutils.h"

/*** PIPELINE GRAPHS **********************************************************/

const gchar *priv_gst_dump_dot_dir;     /* NULL *//* set from gst.c */

extern GstClockTime _priv_gst_info_start_time;

static gchar *
debug_dump_make_object_name (GstObject * element)
{
  return g_strcanon (g_strdup_printf ("%s_%p", GST_OBJECT_NAME (element),
          element), G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "_", '_');
}

static gchar *
debug_dump_get_element_state (GstElement * element)
{
  gchar *state_name = NULL;
  const gchar *state_icons = "~0-=>";
  GstState state = 0, pending = 0;

  gst_element_get_state (element, &state, &pending, 0);
  if (pending == GST_STATE_VOID_PENDING) {
    state_name = g_strdup_printf ("\\n[%c]", state_icons[state]);
  } else {
    state_name = g_strdup_printf ("\\n[%c]->[%c]", state_icons[state],
        state_icons[pending]);
  }
  return state_name;
}

static gchar *
debug_dump_get_element_params (GstElement * element)
{
  gchar *param_name = NULL;
  GParamSpec **properties, *property;
  GValue value = { 0, };
  guint i, number_of_properties;
  gchar *tmp, *value_str;

  /* get paramspecs and show non-default properties */
  properties =
      g_object_class_list_properties (G_OBJECT_CLASS (GST_ELEMENT_GET_CLASS
          (element)), &number_of_properties);
  if (properties) {
    for (i = 0; i < number_of_properties; i++) {
      property = properties[i];

      /* ski some properties */
      if (!(property->flags & G_PARAM_READABLE))
        continue;
      if (!strcmp (property->name, "name"))
        continue;

      g_value_init (&value, property->value_type);
      g_object_get_property (G_OBJECT (element), property->name, &value);
      if (!(g_param_value_defaults (property, &value))) {
        tmp = g_strdup_value_contents (&value);
        value_str = g_strescape (tmp, NULL);
        g_free (tmp);
        if (param_name) {
          tmp = param_name;
          param_name = g_strdup_printf ("%s\\n%s=%s",
              tmp, property->name, value_str);
          g_free (tmp);
        } else {
          param_name = g_strdup_printf ("\\n%s=%s", property->name, value_str);
        }
        g_free (value_str);
      }
      g_value_unset (&value);
    }
    g_free (properties);
  }
  return param_name;
}

static void
debug_dump_element_pad (GstPad * pad, GstElement * element,
    GstDebugGraphDetails details, FILE * out, const gint indent)
{
  GstElement *target_element;
  GstPad *target_pad, *tmp_pad;
  GstPadDirection dir;
  GstPadTemplate *pad_templ;
  GstPadPresence presence;
  gchar *pad_name, *element_name;
  gchar *target_pad_name, *target_element_name;
  gchar *color_name, *style_name;
  gchar *spc = NULL;

  spc = g_malloc (1 + indent * 2);
  memset (spc, 32, indent * 2);
  spc[indent * 2] = '\0';

  dir = gst_pad_get_direction (pad);
  pad_name = debug_dump_make_object_name (GST_OBJECT (pad));
  element_name = debug_dump_make_object_name (GST_OBJECT (element));
  if (GST_IS_GHOST_PAD (pad)) {
    color_name =
        (dir == GST_PAD_SRC) ? "#ffdddd" : ((dir ==
            GST_PAD_SINK) ? "#ddddff" : "#ffffff");
    /* output target-pad so that it belongs to this element */
    if ((tmp_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (pad)))) {
      if ((target_pad = gst_pad_get_peer (tmp_pad))) {
        target_pad_name = debug_dump_make_object_name (GST_OBJECT (target_pad));
        if ((target_element = gst_pad_get_parent_element (target_pad))) {
          target_element_name =
              debug_dump_make_object_name (GST_OBJECT (target_element));
        } else {
          target_element_name = "";
        }
        style_name = "filled,solid";
        if ((pad_templ = gst_pad_get_pad_template (target_pad))) {
          presence = GST_PAD_TEMPLATE_PRESENCE (pad_templ);
          if (presence == GST_PAD_SOMETIMES) {
            style_name = "filled,dotted";
          } else if (presence == GST_PAD_REQUEST) {
            style_name = "filled,dashed";
          }
        }
        fprintf (out,
            "%s  %s_%s [color=black, fillcolor=\"%s\", label=\"%s\", height=\"0.2\", style=\"%s\"];\n",
            spc, target_element_name, target_pad_name, color_name,
            GST_OBJECT_NAME (target_pad), style_name);
        g_free (target_pad_name);
        if (target_element) {
          g_free (target_element_name);
          gst_object_unref (target_element);
        }
        gst_object_unref (target_pad);
      }
      gst_object_unref (tmp_pad);
    }
  } else {
    color_name =
        (dir == GST_PAD_SRC) ? "#ffaaaa" : ((dir ==
            GST_PAD_SINK) ? "#aaaaff" : "#cccccc");
  }
  /* pads */
  style_name = "filled,solid";
  if ((pad_templ = gst_pad_get_pad_template (pad))) {
    presence = GST_PAD_TEMPLATE_PRESENCE (pad_templ);
    if (presence == GST_PAD_SOMETIMES) {
      style_name = "filled,dotted";
    } else if (presence == GST_PAD_REQUEST) {
      style_name = "filled,dashed";
    }
  }
  fprintf (out,
      "%s  %s_%s [color=black, fillcolor=\"%s\", label=\"%s\", height=\"0.2\", style=\"%s\"];\n",
      spc, element_name, pad_name, color_name, GST_OBJECT_NAME (pad),
      style_name);

  g_free (pad_name);
  g_free (element_name);
  g_free (spc);
}

static void
debug_dump_element_pad_link (GstPad * pad, GstElement * element,
    GstDebugGraphDetails details, FILE * out, const gint indent)
{
  GstElement *peer_element, *target_element;
  GstPad *peer_pad, *target_pad, *tmp_pad;
  GstCaps *caps;
  GstStructure *structure;
  gboolean free_caps, free_media;
  gchar *media = NULL;
  gchar *pad_name, *element_name;
  gchar *peer_pad_name, *peer_element_name;
  gchar *target_pad_name, *target_element_name;
  gchar *spc = NULL;

  spc = g_malloc (1 + indent * 2);
  memset (spc, 32, indent * 2);
  spc[indent * 2] = '\0';

  if ((peer_pad = gst_pad_get_peer (pad))) {
    free_media = FALSE;
    if ((details & GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE) ||
        (details & GST_DEBUG_GRAPH_SHOW_CAPS_DETAILS)
        ) {
      if ((caps = gst_pad_get_negotiated_caps (pad))) {
        free_caps = TRUE;
      } else {
        free_caps = FALSE;
        if (!(caps = (GstCaps *)
                gst_pad_get_pad_template_caps (pad))) {
          /* this should not happen */
          media = "?";
        }
      }
      if (caps) {
        if (details & GST_DEBUG_GRAPH_SHOW_CAPS_DETAILS) {
          gchar *tmp = g_strdelimit (gst_caps_to_string (caps), ",",
              '\n');

          media = g_strescape (tmp, NULL);
          free_media = TRUE;
          g_free (tmp);
        } else {
          if (GST_CAPS_IS_SIMPLE (caps)) {
            structure = gst_caps_get_structure (caps, 0);
            media = (gchar *) gst_structure_get_name (structure);
          } else
            media = "*";
        }
        if (free_caps) {
          gst_caps_unref (caps);
        }
      }
    }

    pad_name = debug_dump_make_object_name (GST_OBJECT (pad));
    if (element) {
      element_name = debug_dump_make_object_name (GST_OBJECT (element));
    } else {
      element_name = "";
    }
    peer_pad_name = debug_dump_make_object_name (GST_OBJECT (peer_pad));
    if ((peer_element = gst_pad_get_parent_element (peer_pad))) {
      peer_element_name =
          debug_dump_make_object_name (GST_OBJECT (peer_element));
    } else {
      peer_element_name = "";
    }

    if (GST_IS_GHOST_PAD (pad)) {
      if ((tmp_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (pad)))) {
        if ((target_pad = gst_pad_get_peer (tmp_pad))) {
          target_pad_name =
              debug_dump_make_object_name (GST_OBJECT (target_pad));
          if ((target_element = gst_pad_get_parent_element (target_pad))) {
            target_element_name =
                debug_dump_make_object_name (GST_OBJECT (target_element));
          } else {
            target_element_name = "";
          }
          /* src ghostpad relationship */
          fprintf (out, "%s%s_%s -> %s_%s [style=dashed, minlen=0]\n", spc,
              target_element_name, target_pad_name, element_name, pad_name);

          g_free (target_pad_name);
          if (target_element) {
            g_free (target_element_name);
            gst_object_unref (target_element);
          }
          gst_object_unref (target_pad);
        }
        gst_object_unref (tmp_pad);
      }
    }
    if (GST_IS_GHOST_PAD (peer_pad)) {
      if ((tmp_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (peer_pad)))) {
        if ((target_pad = gst_pad_get_peer (tmp_pad))) {
          target_pad_name =
              debug_dump_make_object_name (GST_OBJECT (target_pad));
          if ((target_element = gst_pad_get_parent_element (target_pad))) {
            target_element_name =
                debug_dump_make_object_name (GST_OBJECT (target_element));
          } else {
            target_element_name = "";
          }
          /* sink ghostpad relationship */
          fprintf (out, "%s%s_%s -> %s_%s [style=dashed, minlen=0]\n", spc,
              peer_element_name, peer_pad_name,
              target_element_name, target_pad_name);
          /* FIXME: we are missing links from the proxy pad
           * theoretically we need to:
           * pad=gst_object_ref(target_pad);
           * goto line 280: if ((peer_pad = gst_pad_get_peer (pad)))
           * as this would e ugly we need to refactor ...
           */
          debug_dump_element_pad_link (target_pad, target_element, details, out,
              indent);
          g_free (target_pad_name);
          if (target_element) {
            g_free (target_element_name);
            gst_object_unref (target_element);
          }
          gst_object_unref (target_pad);
        }
        gst_object_unref (tmp_pad);
      }
    }

    /* pad link */
    if (media) {
      fprintf (out, "%s%s_%s -> %s_%s [label=\"%s\"]\n", spc,
          element_name, pad_name, peer_element_name, peer_pad_name, media);
      if (free_media) {
        g_free (media);
      }
    } else {
      fprintf (out, "%s%s_%s -> %s_%s\n", spc,
          element_name, pad_name, peer_element_name, peer_pad_name);
    }

    g_free (pad_name);
    if (element) {
      g_free (element_name);
    }
    g_free (peer_pad_name);
    if (peer_element) {
      g_free (peer_element_name);
      gst_object_unref (peer_element);
    }
    gst_object_unref (peer_pad);
  }
  g_free (spc);
}

/*
 * debug_dump_element:
 * @bin: the bin that should be analyzed
 * @out: file to write to
 * @indent: level of graph indentation
 *
 * Helper for _gst_debug_bin_to_dot_file() to recursively dump a pipeline.
 */
static void
debug_dump_element (GstBin * bin, GstDebugGraphDetails details, FILE * out,
    const gint indent)
{
  GstIterator *element_iter, *pad_iter;
  gboolean elements_done, pads_done;
  GstElement *element;
  GstPad *pad;
  GstPadDirection dir;
  guint src_pads, sink_pads;
  gchar *element_name;
  gchar *state_name = NULL;
  gchar *param_name = NULL;
  gchar *spc = NULL;

  spc = g_malloc (1 + indent * 2);
  memset (spc, 32, indent * 2);
  spc[indent * 2] = '\0';

  element_iter = gst_bin_iterate_elements (bin);
  elements_done = FALSE;
  while (!elements_done) {
    switch (gst_iterator_next (element_iter, (gpointer) & element)) {
      case GST_ITERATOR_OK:
        element_name = debug_dump_make_object_name (GST_OBJECT (element));

        if (details & GST_DEBUG_GRAPH_SHOW_STATES) {
          state_name = debug_dump_get_element_state (GST_ELEMENT (element));
        }
        if (details & GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS) {
          param_name = debug_dump_get_element_params (GST_ELEMENT (element));
        }
        /* elements */
        fprintf (out, "%ssubgraph cluster_%s {\n", spc, element_name);
        fprintf (out, "%s  fontname=\"Bitstream Vera Sans\";\n", spc);
        fprintf (out, "%s  fontsize=\"8\";\n", spc);
        fprintf (out, "%s  style=filled;\n", spc);
        fprintf (out, "%s  color=black;\n\n", spc);
        fprintf (out, "%s  label=\"%s\\n%s%s%s\";\n", spc,
            G_OBJECT_TYPE_NAME (element), GST_OBJECT_NAME (element),
            (state_name ? state_name : ""), (param_name ? param_name : "")
            );
        if (state_name) {
          g_free (state_name);
          state_name = NULL;
        }
        if (param_name) {
          g_free (param_name);
          param_name = NULL;
        }
        g_free (element_name);

        src_pads = sink_pads = 0;
        if ((pad_iter = gst_element_iterate_pads (element))) {
          pads_done = FALSE;
          while (!pads_done) {
            switch (gst_iterator_next (pad_iter, (gpointer) & pad)) {
              case GST_ITERATOR_OK:
                debug_dump_element_pad (pad, element, details, out, indent);
                dir = gst_pad_get_direction (pad);
                if (dir == GST_PAD_SRC)
                  src_pads++;
                else if (dir == GST_PAD_SINK)
                  sink_pads++;
                gst_object_unref (pad);
                break;
              case GST_ITERATOR_RESYNC:
                gst_iterator_resync (pad_iter);
                break;
              case GST_ITERATOR_ERROR:
              case GST_ITERATOR_DONE:
                pads_done = TRUE;
                break;
            }
          }
          gst_iterator_free (pad_iter);
        }
        if (GST_IS_BIN (element)) {
          fprintf (out, "%s  fillcolor=\"#ffffff\";\n", spc);
          /* recurse */
          debug_dump_element (GST_BIN (element), details, out, indent + 1);
        } else {
          if (src_pads && !sink_pads)
            fprintf (out, "%s  fillcolor=\"#ffaaaa\";\n", spc);
          else if (!src_pads && sink_pads)
            fprintf (out, "%s  fillcolor=\"#aaaaff\";\n", spc);
          else if (src_pads && sink_pads)
            fprintf (out, "%s  fillcolor=\"#aaffaa\";\n", spc);
          else
            fprintf (out, "%s  fillcolor=\"#ffffff\";\n", spc);
        }
        fprintf (out, "%s}\n\n", spc);
        if ((pad_iter = gst_element_iterate_pads (element))) {
          pads_done = FALSE;
          while (!pads_done) {
            switch (gst_iterator_next (pad_iter, (gpointer) & pad)) {
              case GST_ITERATOR_OK:
                if (gst_pad_is_linked (pad)
                    && gst_pad_get_direction (pad) == GST_PAD_SRC) {
                  debug_dump_element_pad_link (pad, element, details, out,
                      indent);
                }
                gst_object_unref (pad);
                break;
              case GST_ITERATOR_RESYNC:
                gst_iterator_resync (pad_iter);
                break;
              case GST_ITERATOR_ERROR:
              case GST_ITERATOR_DONE:
                pads_done = TRUE;
                break;
            }
          }
          gst_iterator_free (pad_iter);
        }
        gst_object_unref (element);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (element_iter);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        elements_done = TRUE;
        break;
    }
  }
  gst_iterator_free (element_iter);
  g_free (spc);
}

/*
 * _gst_debug_bin_to_dot_file:
 * @bin: the top-level pipeline that should be analyzed
 * @file_name: output base filename (e.g. "myplayer")
 *
 * To aid debugging applications one can use this method to write out the whole
 * network of gstreamer elements that form the pipeline into an dot file.
 * This file can be processed with graphviz to get an image.
 * <informalexample><programlisting>
 *  dot -Tpng -oimage.png graph_lowlevel.dot
 * </programlisting></informalexample>
 */
void
_gst_debug_bin_to_dot_file (GstBin * bin, GstDebugGraphDetails details,
    const gchar * file_name)
{
  gchar *full_file_name = NULL;
  FILE *out;

  g_return_if_fail (GST_IS_BIN (bin));

  if (G_LIKELY (priv_gst_dump_dot_dir == NULL))
    return;

  if (!file_name) {
    file_name = g_get_application_name ();
    if (!file_name)
      file_name = "unnamed";
  }

  full_file_name = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "%s.dot",
      priv_gst_dump_dot_dir, file_name);

  if ((out = fopen (full_file_name, "wb"))) {
    gchar *state_name = NULL;
    gchar *param_name = NULL;

    if (details & GST_DEBUG_GRAPH_SHOW_STATES) {
      state_name = debug_dump_get_element_state (GST_ELEMENT (bin));
    }
    if (details & GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS) {
      param_name = debug_dump_get_element_params (GST_ELEMENT (bin));
    }

    /* write header */
    fprintf (out,
        "digraph pipeline {\n"
        "  rankdir=LR;\n"
        "  fontname=\"Bitstream Vera Sans\";\n"
        "  fontsize=\"8\";\n"
        "  labelloc=t;\n"
        "  nodesep=.1;\n"
        "  ranksep=.2;\n"
        "  label=\"<%s>\\n%s%s%s\";\n"
        "  node [style=filled, shape=box, fontsize=\"7\", fontname=\"Bitstream Vera Sans\", margin=\"0.0,0.0\"];\n"
        "  edge [labelfontsize=\"7\", fontsize=\"7\", labelfontname=\"Bitstream Vera Sans\", fontname=\"Bitstream Vera Sans\"];\n"
        "\n", G_OBJECT_TYPE_NAME (bin), GST_OBJECT_NAME (bin),
        (state_name ? state_name : ""), (param_name ? param_name : "")
        );
    if (state_name)
      g_free (state_name);
    if (param_name)
      g_free (param_name);

    debug_dump_element (bin, details, out, 1);

    /* write footer */
    fprintf (out, "}\n");
    fclose (out);
    GST_INFO ("wrote bin graph to : '%s'", full_file_name);
  } else {
    GST_WARNING ("Failed to open file '%s' for writing: %s", full_file_name,
        g_strerror (errno));
  }
  g_free (full_file_name);
}

/*
 * _gst_debug_bin_to_dot_file_with_ts:
 * @bin: the top-level pipeline that should be analyzed
 * @file_name: output base filename (e.g. "myplayer")
 *
 * This works like _gst_debug_bin_to_dot_file(), but adds the current timestamp
 * to the filename, so that it can be used to take multiple snapshots.
 */
void
_gst_debug_bin_to_dot_file_with_ts (GstBin * bin, GstDebugGraphDetails details,
    const gchar * file_name)
{
  gchar *ts_file_name = NULL;
  GstClockTime elapsed;

  g_return_if_fail (GST_IS_BIN (bin));

  if (!file_name) {
    file_name = g_get_application_name ();
    if (!file_name)
      file_name = "unnamed";
  }

  /* add timestamp */
  elapsed = GST_CLOCK_DIFF (_priv_gst_info_start_time,
      gst_util_get_timestamp ());
  ts_file_name =
      g_strdup_printf ("%" GST_TIME_FORMAT "-%s", GST_TIME_ARGS (elapsed),
      file_name);

  _gst_debug_bin_to_dot_file (bin, details, ts_file_name);
  g_free (ts_file_name);
}

#endif /* GST_DISABLE_GST_DEBUG */
