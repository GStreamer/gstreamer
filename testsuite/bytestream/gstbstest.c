/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbstest.c: 
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
#include <string.h>

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#define GST_TYPE_BSTEST  		(gst_bstest_get_type())
#define GST_BSTEST(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BSTEST,GstBsTest))
#define GST_BSTEST_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BSTEST,GstBsTestClass))
#define GST_IS_BSTEST(obj)  		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BSTEST))
#define GST_IS_BSTEST_CLASS(obj)  	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BSTEST))

typedef struct _GstBsTest GstBsTest;
typedef struct _GstBsTestClass GstBsTestClass;

struct _GstBsTest
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstByteStream *bs;
  
  gchar 	*accesspattern;
  guint 	num_patterns;
  gchar		**patterns;
  guint 	sizemin;
  guint 	sizemax;
  gint 		count;
  gboolean 	silent;
};

struct _GstBsTestClass
{
  GstElementClass parent_class;
};

GType gst_bstest_get_type (void);


GstElementDetails gst_bstest_details = GST_ELEMENT_DETAILS (
  "ByteStreamTest",
  "Filter",
  "Test for the GstByteStream code",
  "Erik Walthinsen <omega@temple-baptist.com>, "
  "Wim Taymans <wim.taymans@chello.be>"
);


/* BsTest signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SIZEMIN,
  ARG_SIZEMAX,
  ARG_COUNT,
  ARG_SILENT,
  ARG_ACCESSPATTERN,
};


static void	gst_bstest_base_init		(gpointer g_class);
static void 	gst_bstest_class_init 		(GstBsTestClass * klass);
static void 	gst_bstest_init 		(GstBsTest * bstest);

static void gst_bstest_set_property (GObject * object, guint prop_id, const GValue * value,
				     GParamSpec * pspec);
static void gst_bstest_get_property (GObject * object, guint prop_id, GValue * value,
				     GParamSpec * pspec);

static GstElementStateReturn 	gst_bstest_change_state 	(GstElement *element);
static void 			gst_bstest_loop 		(GstElement * element);

static GstElementClass *parent_class = NULL;

/* static guint gst_bstest_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_bstest_get_type (void)
{
  static GType bstest_type = 0;

  if (!bstest_type) {
    static const GTypeInfo bstest_info = {
      sizeof (GstBsTestClass),
      gst_bstest_base_init,
      NULL,
      (GClassInitFunc) gst_bstest_class_init,
      NULL,
      NULL,
      sizeof (GstBsTest),
      0,
      (GInstanceInitFunc) gst_bstest_init,
    };

    bstest_type = g_type_register_static (GST_TYPE_ELEMENT, "BSTest", &bstest_info, 0);
  }
  return bstest_type;
}
static void
gst_bstest_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_bstest_details);
}

static void
gst_bstest_class_init (GstBsTestClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIZEMIN,
				   g_param_spec_int ("sizemin", "sizemin", "sizemin", 0, G_MAXINT,
						     0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIZEMAX,
				   g_param_spec_int ("sizemax", "sizemax", "sizemax", 0, G_MAXINT,
						     384, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ACCESSPATTERN,
				   g_param_spec_string ("accesspattern", "accesspattern", "accesspattern",
						      "r", G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COUNT,
				   g_param_spec_uint ("count", "count", "count",
						      0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
		        	   g_param_spec_boolean ("silent", "silent", "silent",
				                          FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_bstest_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_bstest_get_property);

  gstelement_class->change_state = gst_bstest_change_state;

}

static GstCaps *
gst_bstest_getcaps (GstPad *pad)
{
  GstBsTest *bstest = GST_BSTEST (gst_pad_get_parent (pad));
  GstPad *otherpad;

  otherpad = (pad == bstest->srcpad) ? bstest->sinkpad : bstest->srcpad;

  return gst_pad_get_allowed_caps (otherpad);
}

static void
gst_bstest_init (GstBsTest * bstest)
{
  bstest->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (bstest), bstest->sinkpad);
  gst_pad_set_getcaps_function (bstest->sinkpad, gst_bstest_getcaps);

  bstest->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (bstest), bstest->srcpad);
  gst_pad_set_getcaps_function (bstest->srcpad, gst_bstest_getcaps);

  gst_element_set_loop_function (GST_ELEMENT (bstest), gst_bstest_loop);

  bstest->sizemin = 0;
  bstest->sizemax = 384;
  bstest->accesspattern = g_strdup ("r");
  bstest->patterns = g_strsplit (bstest->accesspattern, ":", 0);
  bstest->count = 5;
  bstest->silent = FALSE;
  bstest->bs = NULL;
}

static guint
gst_bstest_get_size (GstBsTest *bstest, gchar *sizestring, guint prevsize)
{
  guint size;

  if (sizestring[0] == 0) {
    size = bstest->sizemax;
  }
  else if (sizestring[0] == 'r') {
    size = bstest->sizemin + (guint8)(((gfloat)bstest->sizemax)*rand()/(RAND_MAX + (gfloat)bstest->sizemin));
  }
  else if (sizestring[0] == '<') {
    size = prevsize;
  }
  else {
    size = atoi (sizestring);
  }

  if (size == 0) size++;

  return size;
}

static void
gst_bstest_loop (GstElement * element)
{
  GstBsTest *bstest;
  GstBuffer *buf = NULL;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_BSTEST (element));

  bstest = GST_BSTEST (element);

  do {
    guint size = 0;
    guint i = 0;
    guint8 *ptr;

    while (i < bstest->num_patterns) {
      buf = NULL;

      if (bstest->patterns[i][0] == 'r') {
	size = gst_bstest_get_size (bstest, &bstest->patterns[i][1], size);
        if (!bstest->silent) g_print ("bstest: ***** read %d bytes\n", size);
        gst_bytestream_read (bstest->bs, &buf, size);
      }
      else if (bstest->patterns[i][0] == 'f') {
	size = gst_bstest_get_size (bstest, &bstest->patterns[i][1], size);
        if (!bstest->silent) g_print ("bstest: ***** flush %d bytes\n", size);
        gst_bytestream_flush (bstest->bs, size);
      }
      else if (!strncmp (bstest->patterns[i], "pb", 2)) {
	size = gst_bstest_get_size (bstest, &bstest->patterns[i][2], size);
        if (!bstest->silent) g_print ("bstest: ***** peek bytes %d bytes\n", size);
        gst_bytestream_peek_bytes (bstest->bs, &ptr, size);
      }
      else if (bstest->patterns[i][0] == 'p') {
	size = gst_bstest_get_size (bstest, &bstest->patterns[i][1], size);
        if (!bstest->silent) g_print ("bstest: ***** peek %d bytes\n", size);
        gst_bytestream_peek (bstest->bs, &buf, size);
	gst_buffer_unref (buf);
	buf = NULL;
      }

      if (buf)
        gst_pad_push (bstest->srcpad, GST_DATA (buf));
      
      i++;
    }
/*  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element)); */

  } while (0);
}

static void
gst_bstest_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstBsTest *bstest;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_BSTEST (object));

  bstest = GST_BSTEST (object);

  switch (prop_id) {
    case ARG_SIZEMIN:
      bstest->sizemin = g_value_get_int (value);
      break;
    case ARG_SIZEMAX:
      bstest->sizemax = g_value_get_int (value);
      break;
    case ARG_ACCESSPATTERN:
      if (bstest->accesspattern) {
	g_free (bstest->accesspattern);
	g_strfreev (bstest->patterns);
      }
      if (g_value_get_string (value) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        bstest->accesspattern = NULL;
        bstest->num_patterns = 0;
      } else {
	guint i = 0;

        bstest->accesspattern = g_strdup (g_value_get_string (value));
        bstest->patterns = g_strsplit (bstest->accesspattern, ":", 0);
        while (bstest->patterns[i++]);
        bstest->num_patterns = i-1;
      }
      break;
    case ARG_COUNT:
      bstest->count = g_value_get_uint (value);
      break;
    case ARG_SILENT:
      bstest->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_bstest_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstBsTest *bstest;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_BSTEST (object));

  bstest = GST_BSTEST (object);

  switch (prop_id) {
    case ARG_SIZEMIN:
      g_value_set_int (value, bstest->sizemin);
      break;
    case ARG_SIZEMAX:
      g_value_set_int (value, bstest->sizemax);
      break;
    case ARG_ACCESSPATTERN:
      g_value_set_string (value, bstest->accesspattern);
      break;
    case ARG_COUNT:
      g_value_set_uint (value, bstest->count);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, bstest->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_bstest_change_state (GstElement *element)
{
  GstBsTest *bstest;

  g_return_val_if_fail (GST_IS_BSTEST (element), GST_STATE_FAILURE);

  bstest = GST_BSTEST (element);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (bstest->bs) {
      gst_bytestream_destroy (bstest->bs);
      bstest->bs = NULL;
    }
  }
  else {
    if (!bstest->bs) {
      bstest->bs = gst_bytestream_new (bstest->sinkpad);
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* We need to create an ElementFactory for each element we provide.
   * This consists of the name of the element, the GType identifier,
   * and a pointer to the details structure at the top of the file.
   */
  return gst_element_register (plugin, "bstest", GST_RANK_PRIMARY, GST_TYPE_BSTEST);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR, 
  GST_VERSION_MINOR, 
  "bstest", 
  "test for the bytestream element",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
