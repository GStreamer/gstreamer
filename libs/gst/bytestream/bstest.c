/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstidentity.c: 
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

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#define GST_TYPE_IDENTITY \
  (gst_identity_get_type())
#define GST_IDENTITY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IDENTITY,GstIdentity))
#define GST_IDENTITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IDENTITY,GstIdentityClass))
#define GST_IS_IDENTITY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IDENTITY))
#define GST_IS_IDENTITY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IDENTITY))

typedef struct _GstIdentity GstIdentity;
typedef struct _GstIdentityClass GstIdentityClass;

struct _GstIdentity {
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstByteStream *bs;
  gint byte_size;
  gint count;
};

struct _GstIdentityClass {
  GstElementClass parent_class;
};

GType gst_identity_get_type(void);


GstElementDetails gst_identity_details = {
  "ByteStreamTest",
  "Filter",
  "Test for the GstByteStream code",
  VERSION,
  "Erik Walthinsen <omega@temple-baptist.com>",
  "(C) 2001",
};


/* Identity signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BYTE_SIZE,
  ARG_COUNT,
};


static void gst_identity_class_init	(GstIdentityClass *klass);
static void gst_identity_init		(GstIdentity *identity);

static void gst_identity_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_identity_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_identity_loop		(GstElement *element);

static GstElementClass *parent_class = NULL;
/* static guint gst_identity_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_identity_get_type (void) 
{
  static GType identity_type = 0;

  if (!identity_type) {
    static const GTypeInfo identity_info = {
      sizeof(GstIdentityClass),      NULL,
      NULL,
      (GClassInitFunc)gst_identity_class_init,
      NULL,
      NULL,
      sizeof(GstIdentity),
      0,
      (GInstanceInitFunc)gst_identity_init,
    };
    identity_type = g_type_register_static (GST_TYPE_ELEMENT, "GstBSTest", &identity_info, 0);
  }
  return identity_type;
}

static void 
gst_identity_class_init (GstIdentityClass *klass) 
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTE_SIZE,
    g_param_spec_uint ("byte_size", "byte_size", "byte_size",
                       0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COUNT,
    g_param_spec_uint ("count", "count", "count",
                       0, G_MAXUINT, 0, G_PARAM_READWRITE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_identity_set_property);  
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_identity_get_property);
}

/*
static GstPadNegotiateReturn
gst_identity_negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_negotiate_proxy (pad, identity->sinkpad, caps);
}

static GstPadNegotiateReturn
gst_identity_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_negotiate_proxy (pad, identity->srcpad, caps);
}
*/

static void 
gst_identity_init (GstIdentity *identity) 
{
  identity->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (identity), identity->sinkpad);
  /*gst_pad_set_negotiate_function (identity->sinkpad, gst_identity_negotiate_sink); */
  
  identity->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (identity), identity->srcpad);
  /*gst_pad_set_negotiate_function (identity->srcpad, gst_identity_negotiate_src); */

  gst_element_set_loop_function (GST_ELEMENT (identity), gst_identity_loop);

  identity->byte_size = 384;
  identity->count = 5;

  identity->bs = gst_bytestream_new(identity->sinkpad);
}

static void 
gst_identity_loop (GstElement *element) 
{
  GstIdentity *identity;
  GstBuffer *buf;
  int i;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_IDENTITY (element));

  identity = GST_IDENTITY (element);

/* THIS IS THE BUFFER BASED ONE
  do {
*    g_print("\n"); *

    for (i=0;i<identity->count;i++) {
*      g_print("bstest: getting a buffer of %d bytes\n",identity->byte_size); *
      buf = gst_bytestream_read(identity->bs,identity->byte_size);
      if (!buf) g_print("BUFFER IS BOGUS\n");
*      g_print("pushing the buffer, %d bytes at %d\n",GST_BUFFER_SIZE(buf),GST_BUFFER_OFFSET(buf)); *
      gst_pad_push(identity->srcpad,buf);
*      g_print("\n"); *
      gst_bytestream_print_status(identity->bs);
*      g_print("\n\n"); *
    }

    exit(1);
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
*/

/* THIS IS THE BYTE BASED ONE*/
  do {
    for (i=0;i<identity->count;i++) {
      buf = gst_buffer_new();
      /* note that this is dangerous, as it does *NOT* refcount the data, it can go away!!! */
      GST_BUFFER_DATA(buf) = gst_bytestream_peek_bytes(identity->bs,identity->byte_size);
      GST_BUFFER_SIZE(buf) = identity->byte_size;
      GST_BUFFER_FLAG_SET(buf,GST_BUFFER_DONTFREE);
      gst_pad_push(identity->srcpad,buf);
      gst_bytestream_flush(identity->bs,identity->byte_size);
    }

    exit(1);
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
/**/
}

static void 
gst_identity_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstIdentity *identity;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));
  
  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case ARG_BYTE_SIZE:
      identity->byte_size = g_value_get_uint (value);
      break;
    case ARG_COUNT:
      identity->count = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void gst_identity_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
  GstIdentity *identity;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));
  
  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case ARG_BYTE_SIZE:
      g_value_set_uint (value, identity->byte_size);
      break;
    case ARG_COUNT:
      g_value_set_uint (value, identity->count);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* we need gstbytestream */
  if (!gst_library_load ("gstbytestream")) {
    g_print("can't load bytestream\n");
    return FALSE;
  }
  
  /* We need to create an ElementFactory for each element we provide.
   * This consists of the name of the element, the GType identifier,
   * and a pointer to the details structure at the top of the file.
   */
  factory = gst_elementfactory_new("gstbstest", GST_TYPE_IDENTITY, &gst_identity_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* The very last thing is to register the elementfactory with the plugin. */
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
 
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstbstest",
  plugin_init
};

