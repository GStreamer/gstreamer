/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gstpipeline.h>
#include <gst/gstsink.h>
#include <gst/gstutils.h>
#include <gst/gsttype.h>

#include "config.h"

GstElementDetails gst_pipeline_details = {
  "Pipeline object",
  "Bin",
  "Complete pipeline object",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Pipeline signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_pipeline_class_init(GstPipelineClass *klass);
static void gst_pipeline_init(GstPipeline *pipeline);

static GstElementStateReturn gst_pipeline_change_state(GstElement *element);

static void gst_pipeline_prepare(GstPipeline *pipeline);

static void gst_pipeline_have_type(GstSink *sink, GstSink *sink2, gpointer data);
static void gst_pipeline_pads_autoplug(GstElement *src, GstElement *sink);

static GstBin *parent_class = NULL;
//static guint gst_pipeline_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_pipeline_get_type(void) {
  static GtkType pipeline_type = 0;

  if (!pipeline_type) {
    static const GtkTypeInfo pipeline_info = {
      "GstPipeline",
      sizeof(GstPipeline),
      sizeof(GstPipelineClass),
      (GtkClassInitFunc)gst_pipeline_class_init,
      (GtkObjectInitFunc)gst_pipeline_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    pipeline_type = gtk_type_unique(gst_bin_get_type(),&pipeline_info);
  }
  return pipeline_type;
}

static void
gst_pipeline_class_init(GstPipelineClass *klass) {
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(gst_bin_get_type());

  gstelement_class->change_state = gst_pipeline_change_state;
}

static void gst_pipeline_init(GstPipeline *pipeline) {
}


/**
 * gst_pipeline_new:
 * @name: name of new pipeline
 *
 * Create a new pipeline with the given name.
 *
 * Returns: newly created GstPipeline
 */
GstPipeline *gst_pipeline_new(guchar *name) {
  GstPipeline *pipeline;

  pipeline = gtk_type_new(gst_pipeline_get_type());
  gst_element_set_name(GST_ELEMENT(pipeline),name);
  return pipeline;
}

static void gst_pipeline_prepare(GstPipeline *pipeline) {
  g_print("GstPipeline: preparing pipeline \"%s\" for playing\n", gst_element_get_name(GST_ELEMENT(pipeline)));
}

static void gst_pipeline_have_type(GstSink *sink, GstSink *sink2, gpointer data) {
  g_print("GstPipeline: pipeline have type %p\n", (gboolean *)data);

  *(gboolean *)data = TRUE;
}

static guint16 gst_pipeline_typefind(GstPipeline *pipeline, GstElement *element) {
  gboolean found = FALSE;
  GstElement *typefind;
  guint16 type_id = 0;

  g_print("GstPipeline: typefind for element \"%s\" %p\n", gst_element_get_name(element), &found);

  typefind = gst_elementfactory_make("typefind","typefind");
  g_return_val_if_fail(typefind != NULL, FALSE);

  gtk_signal_connect(GTK_OBJECT(typefind),"have_type",
                    GTK_SIGNAL_FUNC(gst_pipeline_have_type), &found);

  gst_pad_connect(gst_element_get_pad(element,"src"),
                  gst_element_get_pad(typefind,"sink"));

  gst_bin_add(GST_BIN(pipeline), typefind);

  gst_bin_create_plan(GST_BIN(pipeline));
  gst_element_set_state(GST_ELEMENT(element),GST_STATE_READY);
  gst_element_set_state(GST_ELEMENT(element),GST_STATE_PLAYING);

  while (!found) {
    gst_src_push(GST_SRC(element));
  }

  gst_element_set_state(GST_ELEMENT(element),GST_STATE_NULL);

  if (found) {
    type_id = gst_util_get_int_arg(GTK_OBJECT(typefind),"type");
  }

  gst_pad_set_type_id(gst_element_get_pad(element, "src"), type_id);

  gst_pad_disconnect(gst_element_get_pad(element,"src"),
                    gst_element_get_pad(typefind,"sink"));
  gst_bin_remove(GST_BIN(pipeline), typefind);

  return type_id;
}

static void gst_pipeline_pads_autoplug_func(GstElement *src, GstPad *pad, GstElement *sink) {
  GList *sinkpads;
  GstPad *sinkpad;

  g_print("gstpipeline: autoplug pad connect function type %d\n", pad->type);

  sinkpads = gst_element_get_pad_list(sink);
  while (sinkpads) {
    sinkpad = (GstPad *)sinkpads->data;

    // if we have a match, connect the pads
    if (sinkpad->type == pad->type && sinkpad->direction == GST_PAD_SINK && !GST_PAD_CONNECTED(sinkpad)) {
      gst_pad_connect(pad, sinkpad);
      g_print("gstpipeline: autoconnect pad \"%s\" (%d) in element %s <-> ", pad->name, pad->type, gst_element_get_name(src));
      g_print("pad \"%s\" (%d) in element %s\n", sinkpad->name, sinkpad->type, gst_element_get_name(sink));
      break;
    }
    sinkpads = g_list_next(sinkpads);
  }
}

static void gst_pipeline_pads_autoplug(GstElement *src, GstElement *sink) {
  GList *srcpads, *sinkpads;
  gboolean connected = FALSE;

  srcpads = gst_element_get_pad_list(src);

  while (srcpads) {
    GstPad *srcpad = (GstPad *)srcpads->data;
    GstPad *sinkpad;

    if (srcpad->direction == GST_PAD_SRC && !GST_PAD_CONNECTED(srcpad)) {

      sinkpads = gst_element_get_pad_list(sink);
      // FIXME could O(n) if the types were sorted...
      while (sinkpads) {
        sinkpad = (GstPad *)sinkpads->data;

	// if we have a match, connect the pads
	if (sinkpad->type == srcpad->type && sinkpad->direction == GST_PAD_SINK && !GST_PAD_CONNECTED(sinkpad)) {
          gst_pad_connect(srcpad, sinkpad);
          g_print("gstpipeline: autoconnect pad \"%s\" (%d) in element %s <-> ", srcpad->name, srcpad->type, gst_element_get_name(src));
          g_print("pad \"%s\" (%d) in element %s\n", sinkpad->name, sinkpad->type, gst_element_get_name(sink));
	  connected = TRUE;
	  goto end;
	}
        sinkpads = g_list_next(sinkpads);
      }
    }
    srcpads = g_list_next(srcpads);
  }
  
end:
  if (!connected) {
    g_print("gstpipeline: delaying pad connections\n");
    gtk_signal_connect(GTK_OBJECT(src),"new_pad",
                 GTK_SIGNAL_FUNC(gst_pipeline_pads_autoplug_func), sink);
  }
}

gboolean gst_pipeline_autoplug(GstPipeline *pipeline) {
  GList *elements;
  GstElement *element, *srcelement, *sinkelement;
  GList *factories;
  GstElementFactory *factory;
  GList *src_types, *sink_types;
  guint16 src_type = 0, sink_type = 0;
  gboolean complete = FALSE;

  g_return_val_if_fail(GST_IS_PIPELINE(pipeline), FALSE);

  g_print("GstPipeline: autopluging pipeline \"%s\"\n", gst_element_get_name(GST_ELEMENT(pipeline)));

  elements = gst_bin_get_list(GST_BIN(pipeline));

  // fase 1, find all the sinks and sources... FIXME need better way to do this...
  while (elements) {
    element = GST_ELEMENT(elements->data);

    if (GST_IS_SINK(element)) {
      g_print("GstPipeline: found sink \"%s\"\n", gst_element_get_name(element));

      if (sink_type) {
        g_print("GstPipeline: multiple sinks detected, can't autoplug pipeline \"%s\"\n", gst_element_get_name(GST_ELEMENT(pipeline)));
	return FALSE;
      }
      sinkelement = element;
      factory = gst_element_get_factory(element);
      
      sink_types = factory->sink_types;
      if (sink_types == NULL) {
        g_print("GstPipeline: sink \"%s\" has no MIME type, can't autoplug \n", gst_element_get_name(element));
	return FALSE;
      }
      else {
	sink_type = GPOINTER_TO_UINT(sink_types->data);
        g_print("GstPipeline: sink \"%s\" has MIME type %d \n", gst_element_get_name(element), sink_type);
      }
    }
    else if (GST_IS_SRC(element)) {
      g_print("GstPipeline: found source \"%s\"\n", gst_element_get_name(element));

      if (src_type) {
        g_print("GstPipeline: multiple sources detected, can't autoplug pipeline \"%s\"\n", gst_element_get_name(GST_ELEMENT(pipeline)));
	return FALSE;
      }

      srcelement = element;

      factory = gst_element_get_factory(element);

      src_types = factory->src_types;
      if (src_types == NULL) {
        g_print("GstPipeline: source \"%s\" has no MIME type, running typefind...\n", gst_element_get_name(element));

	src_type = gst_pipeline_typefind(pipeline, element);

	if (src_type) {
          g_print("GstPipeline: source \"%s\" type found %d\n", gst_element_get_name(element), src_type);
	}
	else {
          g_print("GstPipeline: source \"%s\" has no type\n", gst_element_get_name(element));
	  return FALSE;
	}
      }
      else {
        while (src_types) {
	  src_types = g_list_next(src_types);
        }
      }
    }
    else {
      g_print("GstPipeline: found invalid element \"%s\", not source or sink\n", gst_element_get_name(element));
    }

    elements = g_list_next(elements);
  }

  factories = gst_type_get_sink_to_src(src_type, sink_type);

  while (factories) {
    // fase 2: find elements to form a pad
       
    factory = (GstElementFactory *)(factories->data);

    g_print("GstPipeline: factory \"%s\"\n", factory->name);

    element = gst_elementfactory_create(factory, factory->name);
    gst_bin_add(GST_BIN(pipeline), element);

    gst_pipeline_pads_autoplug(srcelement, element);

    srcelement = element;

    factories = g_list_next(factories);

    complete = TRUE;
  }

  if (complete) {
    gst_pipeline_pads_autoplug(srcelement, sinkelement);
    return TRUE;
  }
  
  g_print("GstPipeline: unable to autoplug pipeline \"%s\"\n", gst_element_get_name(GST_ELEMENT(pipeline)));
  return FALSE;
}

static GstElementStateReturn gst_pipeline_change_state(GstElement *element) {
  GstPipeline *pipeline;

  g_return_val_if_fail(GST_IS_PIPELINE(element), FALSE);
  pipeline = GST_PIPELINE(element);


  switch (GST_STATE_PENDING(pipeline)) {
    case GST_STATE_READY:
      // we need to set up internal state
      gst_pipeline_prepare(pipeline);
      break;
    default:
      break;
  }
    
  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);
  return GST_STATE_SUCCESS;
}


/**
 * gst_pipeline_iterate:
 * @pipeline: GstPipeline to iterate
 *
 * Cause the pipeline's contents to be run through one full 'iteration'.
 */
void gst_pipeline_iterate(GstPipeline *pipeline) {
  g_return_if_fail(pipeline != NULL);
  g_return_if_fail(GST_IS_PIPELINE(pipeline));
}
