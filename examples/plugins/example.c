/* GStreamer
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

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */
#include <string.h>
#include "example.h"

/* The ElementDetails structure gives a human-readable description of the
 * plugin, as well as author and version data. Use the GST_ELEMENT_DETAILS
 * macro when defining it.
 */
static GstElementDetails example_details = GST_ELEMENT_DETAILS (
  "An example plugin",
  "Example/FirstExample",
  "Shows the basic structure of a plugin",
  "your name <your.name@your.isp>"
);

/* These are the signals that this element can fire.  They are zero-
 * based because the numbers themselves are private to the object.
 * LAST_SIGNAL is used for initialization of the signal array.
 */
enum {
  ASDF,
  /* FILL ME */
  LAST_SIGNAL
};

/* Arguments are identified the same way, but cannot be zero, so you
 * must leave the ARG_0 entry in as a placeholder.
 */
enum {
  ARG_0,
  ARG_ACTIVE,
  /* FILL ME */
};

/* The PadFactory structures describe what pads the element has or
 * can have.  They can be quite complex, but for this example plugin
 * they are rather simple.
 */
GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE (
  "sink",		/* The name of the pad */
  GST_PAD_SINK,		/* Direction of the pad */
  GST_PAD_ALWAYS,	/* The pad exists for every instance */
  GST_STATIC_CAPS (
    "unknown/unknown, "	/* The MIME media type */
    "foo:int=1, "	/* an integer property */
    "bar:boolean=true, " /* a boolean property */
    "baz:int={ 1, 3 }"	/* a list of values */
  )
);

/* This factory is much simpler, and defines the source pad. */
GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "unknown/unknown"
  )
);


/* A number of functon prototypes are given so we can refer to them later. */
static void	gst_example_class_init		(GstExampleClass *klass);
static void	gst_example_init		(GstExample *example);

static void	gst_example_chain		(GstPad *pad, GstData *_data);

static void	gst_example_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_example_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);
static GstElementStateReturn
		gst_example_change_state 	(GstElement *element);

/* The parent class pointer needs to be kept around for some object
 * operations.
 */
static GstElementClass *parent_class = NULL;

/* This array holds the ids of the signals registered for this object.
 * The array indexes are based on the enum up above.
 */
static guint gst_example_signals[LAST_SIGNAL] = { 0 };

/* This function is used to register and subsequently return the type
 * identifier for this object class.  On first invocation, it will
 * register the type, providing the name of the class, struct sizes,
 * and pointers to the various functions that define the class.
 */
GType
gst_example_get_type(void)
{
  static GType example_type = 0;

  if (!example_type) {
    static const GTypeInfo example_info = {
      sizeof(GstExampleClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_example_class_init,
      NULL,
      NULL,
      sizeof(GstExample),
      0,
      (GInstanceInitFunc)gst_example_init,
    };
    example_type = g_type_register_static(GST_TYPE_ELEMENT, "GstExample", &example_info, 0);
  }
  return example_type;
}

/* In order to create an instance of an object, the class must be
 * initialized by this function.  GObject will take care of running
 * it, based on the pointer to the function provided above.
 */
static void
gst_example_class_init (GstExampleClass *klass)
{
  /* Class pointers are needed to supply pointers to the private
   * implementations of parent class methods.
   */
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  /* Since the example class contains the parent classes, you can simply
   * cast the pointer to get access to the parent classes.
   */
  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  /* The parent class is needed for class method overrides. */
  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  /* Here we add an argument to the object.  This argument is an integer,
   * and can be both read and written.
   */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ACTIVE,
    g_param_spec_int("active","active","active",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */

  /* Here we add a signal to the object. This is avery useless signal
   * called asdf. The signal will also pass a pointer to the listeners
   * which happens to be the example element itself */
  gst_example_signals[ASDF] =
    g_signal_new("asdf", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstExampleClass, asdf), NULL, NULL,
                   g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                   GST_TYPE_EXAMPLE);


  /* The last thing is to provide the functions that implement get and set
   * of arguments.
   */
  gobject_class->set_property = gst_example_set_property;
  gobject_class->get_property = gst_example_get_property;

  /* we also override the default state change handler with our own
   * implementation */
  gstelement_class->change_state = gst_example_change_state;
  /* We can now provide the details for this element, that we defined earlier. */
  gst_element_class_set_details (gstelement_class, &example_details);
  /* The pad templates can be easily generated from the factories above,
   * and then added to the list of padtemplates for the class.
   */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
}

/* This function is responsible for initializing a specific instance of
 * the plugin.
 */
static void
gst_example_init(GstExample *example)
{
  /* First we create the sink pad, which is the input to the element.
   * We will use the template constructed by the factory.
   */
  example->sinkpad = gst_pad_new_from_template (
		  gst_static_pad_template_get (&sink_template), "sink");
  /* Setting the chain function allows us to supply the function that will
   * actually be performing the work.  Without this, the element would do
   * nothing, with undefined results (assertion failures and such).
   */
  gst_pad_set_chain_function(example->sinkpad,gst_example_chain);
  /* We then must add this pad to the element's list of pads.  The base
   * element class manages the list of pads, and provides accessors to it.
   */
  gst_element_add_pad(GST_ELEMENT(example),example->sinkpad);

  /* The src pad, the output of the element, is created and registered
   * in the same way, with the exception of the chain function.  Source
   * pads don't have chain functions, because they can't accept buffers,
   * they only produce them.
   */
  example->srcpad = gst_pad_new_from_template (
		  gst_static_pad_template_get (&src_template), "src");
  gst_element_add_pad(GST_ELEMENT(example),example->srcpad);

  /* Initialization of element's private variables. */
  example->active = FALSE;
}

/* The chain function is the heart of the element.  It's where all the
 * work is done.  It is passed a pointer to the pad in question, as well
 * as the buffer provided by the peer element.
 */
static void
gst_example_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstExample *example;
  GstBuffer *outbuf;

  /* Some of these checks are of dubious value, since if there were not
   * already true, the chain function would never be called.
   */
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  /* We need to get a pointer to the element this pad belogs to. */
  example = GST_EXAMPLE(gst_pad_get_parent (pad));

  /* A few more sanity checks to make sure that the element that owns
   * this pad is the right kind of element, in case something got confused.
   */
  g_return_if_fail(example != NULL);
  g_return_if_fail(GST_IS_EXAMPLE(example));

  /* If we are supposed to be doing something, here's where it happens. */
  if (example->active) {
    /* In this example we're going to copy the buffer to another one, 
     * so we need to allocate a new buffer first. */
    outbuf = gst_buffer_new();

    /* We need to copy the size and offset of the buffer at a minimum. */
    GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (buf);
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buf);

    /* Then allocate the memory for the new buffer */
    GST_BUFFER_DATA (outbuf) = (guchar *)g_malloc (GST_BUFFER_SIZE (outbuf));

    /* Then copy the data in the incoming buffer into the new buffer. */
    memcpy (GST_BUFFER_DATA (outbuf), GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (outbuf));

    /* we don't need the incomming buffer anymore so we unref it. When we are
     * the last plugin with a handle to the buffer, its memory will be freed */
    gst_buffer_unref (buf);

    /* When we're done with the buffer, we push it on to the next element
     * in the pipeline, through the element's source pad, which is stored
     * in the element's structure.
     */
    gst_pad_push(example->srcpad,GST_DATA (outbuf));

    /* For fun we'll emit our useless signal here */
    g_signal_emit(G_OBJECT (example), gst_example_signals[ASDF], 0,
                  example);

  /* If we're not doing something, just send the original incoming buffer. */
  } else {
    gst_pad_push(example->srcpad,GST_DATA (buf));
  }
}

/* Arguments are part of the Gtk+ object system, and these functions
 * enable the element to respond to various arguments.
 */
static void
gst_example_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstExample *example;

  /* It's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_EXAMPLE(object));

  /* Get a pointer of the right type. */
  example = GST_EXAMPLE(object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case ARG_ACTIVE:
      /* Here we simply copy the value of the argument to our private
       * storage.  More complex operations can be done, but beware that
       * they may occur at any time, possibly even while your chain function
       * is running, if you are using threads.
       */
      example->active = g_value_get_int (value);
      g_print("example: set active to %d\n",example->active);
      break;
    default:
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_example_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstExample *example;

  /* It's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_EXAMPLE(object));
  example = GST_EXAMPLE(object);

  switch (prop_id) {
    case ARG_ACTIVE:
      g_value_set_int (value, example->active);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* This is the state change function that will be called when
 * the element goes through the different state changes.
 * The plugin can prepare itself and its internal data structures
 * in the various state transitions.
 */
static GstElementStateReturn
gst_example_change_state (GstElement *element)
{
  GstExample *example;
	    
  /* cast to our plugin */
  example = GST_EXAMPLE(element);
	      
  /* we perform our actions based on the state transition
   * of the element */
  switch (GST_STATE_TRANSITION (element)) {
    /* The NULL to READY transition is used to
     * create threads (if any) */
    case GST_STATE_NULL_TO_READY:
      break;
    /* In the READY to PAUSED state, the element should
     * open devices (if any) */
    case GST_STATE_READY_TO_PAUSED:
      break;
    /* In the PAUSED to PLAYING state, the element should
     * prepare itself for operation or continue after a PAUSE */
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    /* In the PLAYING to PAUSED state, the element should
     * PAUSE itself and make sure it can resume operation */
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    /* In the PAUSED to READY state, the element should reset
     * its internal state and close any devices. */
    case GST_STATE_PAUSED_TO_READY:
      break;
    /* The element should free all resources, terminate threads
     * and put itself into its initial state again */
    case GST_STATE_READY_TO_NULL:
      break;
  }

  /* Then we call the parent state change handler */
  return parent_class->change_state (element);
}


/* This is the entry into the plugin itself.  When the plugin loads,
 * this function is called to register everything that the plugin provides.
 */
static gboolean
plugin_init (GstPlugin *plugin)
{
  /* We need to register each element we provide with the plugin. This consists 
   * of the name of the element, a rank that gives the importance of the element 
   * when compared to similar plugins and the GType identifier.
   */
  if (!gst_element_register (plugin, "example", GST_RANK_MARGINAL, GST_TYPE_EXAMPLE))
    return FALSE;

  /* Now we can return successfully. */
  return TRUE;

  /* At this point, the GStreamer core registers the plugin, its
   * elementfactories, padtemplates, etc., for use in your application.
   */
}

/* This structure describes the plugin to the system for dynamically loading
 * plugins, so that the version number and name can be checked in a uniform
 * way.
 *
 * The symbol pointing to this structure is the only symbol looked up when
 * loading the plugin.
 */
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,	/* The major version of the core that this was built with */
  GST_VERSION_MINOR,	/* The minor version of the core that this was built with */
  "example",		/* The name of the plugin.  This must be unique: plugins with
			 * the same name will be assumed to be identical, and only
			 * one will be loaded. */
  "an example plugin",	/* a short description of the plugin in English */
  plugin_init,		/* Pointer to the initialisation function for the plugin. */
  "0.1",		/* The version number of the plugin */
  "LGPL",		/* ieffective license the plugin can be shipped with. Must be 
			 * valid for all libraries it links to, too. */
  "my nifty plugin package",
			/* package this plugin belongs to. */
  "http://www.mydomain.com"
			/* originating URL for this plugin. This is the place to look
			 * for updates, information and so on. */
);

