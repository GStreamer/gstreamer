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
#include <gst/gstthread.h>
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


static void 			gst_pipeline_class_init		(GstPipelineClass *klass);
static void 			gst_pipeline_init		(GstPipeline *pipeline);

static GstElementStateReturn 	gst_pipeline_change_state	(GstElement *element);

static void 			gst_pipeline_prepare		(GstPipeline *pipeline);

static void 			gst_pipeline_have_type		(GstSink *sink, GstSink *sink2, gpointer data);
static void 			gst_pipeline_pads_autoplug	(GstElement *src, GstElement *sink);

static GstBin *parent_class = NULL;
//static guint gst_pipeline_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_pipeline_get_type (void) {
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
    pipeline_type = gtk_type_unique (gst_bin_get_type (), &pipeline_info);
  }
  return pipeline_type;
}

static void
gst_pipeline_class_init (GstPipelineClass *klass) 
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(gst_bin_get_type());

  gstelement_class->change_state = gst_pipeline_change_state;
  gstelement_class->elementfactory = gst_elementfactory_find ("pipeline");
}

static void 
gst_pipeline_init (GstPipeline *pipeline) 
{
  pipeline->src = NULL;
  pipeline->sinks = NULL;
}


/**
 * gst_pipeline_new:
 * @name: name of new pipeline
 *
 * Create a new pipeline with the given name.
 *
 * Returns: newly created GstPipeline
 */
GstElement*
gst_pipeline_new (guchar *name) 
{
  GstPipeline *pipeline;

  pipeline = gtk_type_new (gst_pipeline_get_type ());
  gst_element_set_name (GST_ELEMENT (pipeline), name);
  
  return GST_ELEMENT (pipeline);
}

static void 
gst_pipeline_prepare (GstPipeline *pipeline) 
{
  g_print("GstPipeline: preparing pipeline \"%s\" for playing\n", 
		  gst_element_get_name(GST_ELEMENT(pipeline)));
}

static void 
gst_pipeline_have_type (GstSink *sink, GstSink *sink2, gpointer data) 
{
  g_print("GstPipeline: pipeline have type %p\n", (gboolean *)data);

  *(gboolean *)data = TRUE;
}

static guint16 
gst_pipeline_typefind (GstPipeline *pipeline, GstElement *element) 
{
  gboolean found = FALSE;
  GstElement *typefind;
  guint16 type_id = 0;

  g_print("GstPipeline: typefind for element \"%s\" %p\n", 
		  gst_element_get_name(element), &found);

  typefind = gst_elementfactory_make ("typefind", "typefind");
  g_return_val_if_fail (typefind != NULL, FALSE);

  gtk_signal_connect (GTK_OBJECT (typefind), "have_type",
                      GTK_SIGNAL_FUNC (gst_pipeline_have_type), &found);

  gst_pad_connect (gst_element_get_pad (element, "src"),
                   gst_element_get_pad (typefind, "sink"));

  gst_bin_add (GST_BIN (pipeline), typefind);

  gst_bin_create_plan (GST_BIN (pipeline));
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  // keep pushing buffers... the have_type signal handler will set the found flag
  while (!found) {
    gst_bin_iterate (GST_BIN (pipeline));
  }

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  if (found) {
    type_id = gst_util_get_int_arg (GTK_OBJECT (typefind), "type");
    //gst_pad_add_type_id (gst_element_get_pad (element, "src"), type_id);
  }

  gst_pad_disconnect (gst_element_get_pad (element, "src"),
                      gst_element_get_pad (typefind, "sink"));
  gst_bin_remove (GST_BIN (pipeline), typefind);
  gst_object_unref (GST_OBJECT (typefind));

  return type_id;
}

static gboolean 
gst_pipeline_pads_autoplug_func (GstElement *src, GstPad *pad, GstElement *sink) 
{
  GList *sinkpads;
  gboolean connected = FALSE;

  g_print("gstpipeline: autoplug pad connect function for \"%s\" to \"%s\"\n", 
		  gst_element_get_name(src), gst_element_get_name(sink));

  sinkpads = gst_element_get_pad_list(sink);
  while (sinkpads) {
    GstPad *sinkpad = (GstPad *)sinkpads->data;

    // if we have a match, connect the pads
    if (sinkpad->direction == GST_PAD_SINK && 
        !GST_PAD_CONNECTED(sinkpad) &&
	gst_caps_check_compatibility (pad->caps, sinkpad->caps)) 
    {
      gst_pad_connect(pad, sinkpad);
      g_print("gstpipeline: autoconnect pad \"%s\" in element %s <-> ", pad->name, 
		       gst_element_get_name(src));
      g_print("pad \"%s\" in element %s\n", sinkpad->name,  
		      gst_element_get_name(sink));
      connected = TRUE;
      break;
    }
    sinkpads = g_list_next(sinkpads);
  }

  if (!connected) {
    g_print("gstpipeline: no path to sinks for type\n");
  }
  return connected;
}

static void 
gst_pipeline_pads_autoplug (GstElement *src, GstElement *sink) 
{
  GList *srcpads;
  gboolean connected = FALSE;

  srcpads = gst_element_get_pad_list(src);

  while (srcpads && !connected) {
    GstPad *srcpad = (GstPad *)srcpads->data;

    connected = gst_pipeline_pads_autoplug_func (src, srcpad, sink);

    srcpads = g_list_next(srcpads);
  }
  
  if (!connected) {
    g_print("gstpipeline: delaying pad connections for \"%s\" to \"%s\"\n",
		    gst_element_get_name(src), gst_element_get_name(sink));
    gtk_signal_connect(GTK_OBJECT(src),"new_pad",
                 GTK_SIGNAL_FUNC(gst_pipeline_pads_autoplug_func), sink);
  }
}

/**
 * gst_pipeline_add_src:
 * @pipeline: the pipeline to add the src to
 * @src: the src to add to the pipeline
 *
 * Adds a src element to the pipeline. This element
 * will be used as a src for autoplugging. If you add more
 * than one src element, the previously added element will
 * be removed.
 */
void 
gst_pipeline_add_src (GstPipeline *pipeline, GstElement *src) 
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (GST_IS_PIPELINE (pipeline));
  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_ELEMENT (src));

  if (pipeline->src) {
    printf("gstpipeline: *WARNING* removing previously added element \"%s\"\n",
			  gst_element_get_name(pipeline->src));
    gst_bin_remove(GST_BIN(pipeline), pipeline->src);
  }
  pipeline->src = src;
  gst_bin_add(GST_BIN(pipeline), src);
}

/**
 * gst_pipeline_add_sink:
 * @pipeline: the pipeline to add the sink to
 * @sink: the sink to add to the pipeline
 *
 * Adds a sink element to the pipeline. This element
 * will be used as a sink for autoplugging
 */
void 
gst_pipeline_add_sink (GstPipeline *pipeline, GstElement *sink) 
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (GST_IS_PIPELINE (pipeline));
  g_return_if_fail (sink != NULL);
  g_return_if_fail (GST_IS_ELEMENT (sink));

  pipeline->sinks = g_list_prepend (pipeline->sinks, sink);
  //gst_bin_add(GST_BIN(pipeline), sink);
}

/**
 * gst_pipeline_autoplug:
 * @pipeline: the pipeline to autoplug
 *
 * Constructs a complete pipeline by automatically
 * detecting the plugins needed.
 *
 * Returns: a gboolean indicating success or failure.
 */
gboolean 
gst_pipeline_autoplug (GstPipeline *pipeline) 
{
  GList *elements;
  GstElement *element, *srcelement = NULL, *sinkelement= NULL;
  GList **factories;
  GList **base_factories;
  GstElementFactory *factory;
  GList *src_types;
  guint16 src_type = 0, sink_type = 0;
  guint i, numsinks;
  gboolean use_thread = FALSE, have_common = FALSE;

  g_return_val_if_fail(pipeline != NULL, FALSE);
  g_return_val_if_fail(GST_IS_PIPELINE(pipeline), FALSE);

  g_print("GstPipeline: autopluging pipeline \"%s\"\n", 
		  gst_element_get_name(GST_ELEMENT(pipeline)));


  // fase 1, run typedetect on the source if needed... 
  if (!pipeline->src) {
    g_print("GstPipeline: no source detected, can't autoplug pipeline \"%s\"\n", 
		gst_element_get_name(GST_ELEMENT(pipeline)));
    return FALSE;
  }

  factory = gst_element_get_factory(pipeline->src);

  src_types = factory->src_caps;
  if (src_types == NULL) {
    g_print("GstPipeline: source \"%s\" has no MIME type, running typefind...\n", 
		gst_element_get_name(pipeline->src));

    src_type = gst_pipeline_typefind(pipeline, pipeline->src);

    if (src_type) {
      g_print("GstPipeline: source \"%s\" type found %d\n", gst_element_get_name(pipeline->src), 
		  src_type);
    }
    else {
      g_print("GstPipeline: source \"%s\" has no type\n", gst_element_get_name(pipeline->src));
      return FALSE;
    }
  }
  else {
    while (src_types) {
      // FIXME loop over types and find paths...
      src_types = g_list_next(src_types);
    }
  }

  srcelement = pipeline->src;

  elements = pipeline->sinks;

  numsinks = g_list_length(elements);
  factories = g_new0(GList *, numsinks);
  base_factories = g_new0(GList *, numsinks);

  i = 0;
  // fase 2, loop over all the sinks.. 
  while (elements) {
    GList *pads;
    GstPad *pad;

    element = GST_ELEMENT(elements->data);

    pads = gst_element_get_pad_list(element);

    while (pads) {
      pad = (GstPad *)pads->data;

      if (pad->direction == GST_PAD_SINK) {
	      /*
        GList *types = gst_pad_get_type_ids(pad);
        if (types) {
          sink_type = GPOINTER_TO_INT (types->data);
	  break;
        }
	else
	*/
	  sink_type = 0;

      }
      g_print ("type %d\n", sink_type);

      pads = g_list_next(pads);
    }

    base_factories[i] = factories[i] = gst_type_get_sink_to_src(src_type, sink_type);
    i++;

    elements = g_list_next(elements);
  }
  
  while (factories[0]) {
    // fase 3: add common elements 
    factory = (GstElementFactory *)(factories[0]->data);

    // check to other paths for mathing elements (factories)
    for (i=1; i<numsinks; i++) {
      if (factory != (GstElementFactory *)(factories[i]->data)) {
	goto differ;
      }
      factories[i] = g_list_next(factories[i]);
    }
    factory = (GstElementFactory *)(factories[0]->data);

    g_print("GstPipeline: common factory \"%s\"\n", factory->name);

    element = gst_elementfactory_create(factory, factory->name);
    gst_bin_add(GST_BIN(pipeline), element);

    gst_pipeline_pads_autoplug(srcelement, element);

    srcelement = element;

    factories[0] = g_list_next(factories[0]);

    have_common = TRUE;
  }

differ:
  // loop over all the sink elements
  elements = pipeline->sinks;

  i = 0;
  while (elements) {
    GstElement *thesrcelement = srcelement;
    GstElement *thebin = GST_ELEMENT(pipeline);

    if (g_list_length(base_factories[i]) == 0) goto next;

    sinkelement = (GstElement *)elements->data;

    use_thread = have_common;

    while (factories[i] || sinkelement) {
      // fase 4: add other elements...
       
      if (factories[i]) {
        factory = (GstElementFactory *)(factories[i]->data);
        g_print("GstPipeline: factory \"%s\"\n", factory->name);
        element = gst_elementfactory_create(factory, factory->name);
        factories[i] = g_list_next(factories[i]);
      }
      // we have arived to the final sink element
      else {
	element = sinkelement;
	sinkelement = NULL;
      }

      // this element suggests the use of a thread, so we set one up...
      if (GST_ELEMENT_IS_THREAD_SUGGESTED(element) || use_thread) {
        GstElement *queue;
        GList *sinkpads;
        GstPad *srcpad, *sinkpad;

	use_thread = FALSE;

        g_print("GstPipeline: sugest new thread for \"%s\" %08x\n", element->name, GST_FLAGS(element));

	// create a new queue and add to the previous bin
        queue = gst_elementfactory_make("queue", g_strconcat("queue_", gst_element_get_name(element), NULL));
        gst_bin_add(GST_BIN(thebin), queue);

	// this will be the new bin for all following elements
        thebin = gst_elementfactory_make("thread", g_strconcat("thread_", gst_element_get_name(element), NULL));

        srcpad = gst_element_get_pad(queue, "src");

        sinkpads = gst_element_get_pad_list(element);
        while (sinkpads) {
          sinkpad = (GstPad *)sinkpads->data;

	  /*
	  // FIXME connect matching pads, not just the first one...
          if (sinkpad->direction == GST_PAD_SINK && 
	      !GST_PAD_CONNECTED(sinkpad)) {
	    guint16 sinktype = 0; 
            GList *types = gst_pad_get_type_ids(sinkpad);
            if (types) 
              sinktype = GPOINTER_TO_INT (types->data);
	    // the queue has the type of the elements it connects
	    gst_pad_set_type_id (srcpad, sinktype);
            gst_pad_set_type_id (gst_element_get_pad(queue, "sink"), sinktype);
	    break;
	  }
	  */
          sinkpads = g_list_next(sinkpads);
        }
        gst_pipeline_pads_autoplug(thesrcelement, queue);

        gst_bin_add(GST_BIN(thebin), element);
        gst_bin_add(GST_BIN(pipeline), thebin);
        thesrcelement = queue;
      }
      // no thread needed, easy case
      else {
        gst_bin_add(GST_BIN(thebin), element);
      }
      gst_pipeline_pads_autoplug(thesrcelement, element);

      // this element is now the new source element
      thesrcelement = element;
    }
next:
    elements = g_list_next(elements);
    i++;
  }
  return TRUE;
  
  g_print("GstPipeline: unable to autoplug pipeline \"%s\"\n", 
		  gst_element_get_name(GST_ELEMENT(pipeline)));
  return FALSE;
}

static GstElementStateReturn 
gst_pipeline_change_state (GstElement *element) 
{
  GstPipeline *pipeline;

  g_return_val_if_fail (GST_IS_PIPELINE (element), FALSE);
  
  pipeline = GST_PIPELINE (element);

  switch (GST_STATE_PENDING (pipeline)) {
    case GST_STATE_READY:
      // we need to set up internal state
      gst_pipeline_prepare (pipeline);
      break;
    default:
      break;
  }
    
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return GST_STATE_SUCCESS;
}


/**
 * gst_pipeline_iterate:
 * @pipeline: GstPipeline to iterate
 *
 * Cause the pipeline's contents to be run through one full 'iteration'.
 */
void 
gst_pipeline_iterate (GstPipeline *pipeline) 
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (GST_IS_PIPELINE(pipeline));
}
