/* GStreamer
 * Copyright (C) 2002 Wim Taymans <wtay@chello.be>
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

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
#include <gst/audio/audio.h>
#include <string.h>

#define GST_TYPE_MIXMATRIX \
  (gst_mixmatrix_get_type())
#define GST_MIXMATRIX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MIXMATRIX,GstMixMatrix))
#define GST_MIXMATRIX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MIXMATRIX,GstMixMatrix))
#define GST_IS_MIXMATRIX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MIXMATRIX))
#define GST_IS_MIXMATRIX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MIXMATRIX))

typedef struct _GstMixMatrix GstMixMatrix;
typedef struct _GstMixMatrixClass GstMixMatrixClass;

struct _GstMixMatrix
{
  GstElement element;

  GstCaps *caps;
  gint samplerate;

  gint grpsize;
  gint outsize;

  GstPad **sinkpads;
  GstByteStream **sinkbs;
  gint sinkpadalloc;

  GstPad **srcpads;
  gint srcpadalloc;

  gfloat **matrix;
};

struct _GstMixMatrixClass
{
  GstElementClass parent_class;

  void (*resize) (GstMixMatrix * mix);
};

/* elementfactory information */
static GstElementDetails mixmatrix_details = {
  "Mixing Matrix",
  "Filter/Editor/Audio",
  "Mix N audio channels together into M channels",
  "Erik Walthinsen <omega@temple-baptist.com>"
};

enum
{
  /* FILL ME */
  RESIZE_SIGNAL,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_GRPSIZE,
  ARG_OUTSIZE,
  ARG_SINKPADS,
  ARG_SRCPADS,
  ARG_MATRIXPTR
};

static GstStaticPadTemplate mixmatrix_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate mixmatrix_src_template =
GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS)
    );

static void gst_mixmatrix_class_init (GstMixMatrixClass * klass);
static void gst_mixmatrix_base_init (GstMixMatrixClass * klass);
static void gst_mixmatrix_init (GstMixMatrix * element);

static void gst_mixmatrix_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mixmatrix_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstPad *gst_mixmatrix_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * name);

static GstPadLinkReturn gst_mixmatrix_connect (GstPad * pad,
    const GstCaps * caps);

static void gst_mixmatrix_loop (GstElement * element);

static guint gst_mixmatrix_signals[LAST_SIGNAL] = { 0 };
static GstElementClass *parent_class = NULL;

GType
gst_mixmatrix_get_type (void)
{
  static GType mixmatrix_type = 0;

  if (!mixmatrix_type) {
    static const GTypeInfo mixmatrix_info = {
      sizeof (GstMixMatrixClass),
      (GBaseInitFunc) gst_mixmatrix_base_init,
      NULL,
      (GClassInitFunc) gst_mixmatrix_class_init,
      NULL,
      NULL,
      sizeof (GstMixMatrix),
      0,
      (GInstanceInitFunc) gst_mixmatrix_init,
    };

    mixmatrix_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMixMatrix",
        &mixmatrix_info, 0);
  }
  return mixmatrix_type;
}

static void
gst_mixmatrix_base_init (GstMixMatrixClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mixmatrix_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mixmatrix_src_template));
  gst_element_class_set_details (element_class, &mixmatrix_details);
}

static void
gst_mixmatrix_class_init (GstMixMatrixClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_mixmatrix_signals[RESIZE_SIGNAL] =
      g_signal_new ("resize",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMixMatrixClass, resize),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SINKPADS,
      g_param_spec_int ("sinkpads", "Sink Pads",
          "Number of sink pads in matrix", 0, G_MAXINT, 8, G_PARAM_READABLE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SRCPADS,
      g_param_spec_int ("srcpads", "Src Pads", "Number of src pads in matrix",
          0, G_MAXINT, 8, G_PARAM_READABLE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MATRIXPTR,
      g_param_spec_pointer ("matrixptr", "Matrix Pointer",
          "Pointer to gfloat mix matrix", G_PARAM_READABLE));

  gobject_class->set_property = gst_mixmatrix_set_property;
  gobject_class->get_property = gst_mixmatrix_get_property;
  gstelement_class->request_new_pad = gst_mixmatrix_request_new_pad;
}

static gfloat **
mixmatrix_alloc_matrix (int x, int y)
{
  gfloat **matrix;
  int i;

  GST_DEBUG ("mixmatrix: allocating a %dx%d matrix of floats\n", x, y);
  matrix = g_new (gfloat *, x);
  GST_DEBUG ("mixmatrix: %p: ", matrix);
  for (i = 0; i < x; i++) {
    matrix[i] = g_new (gfloat, y);
    GST_DEBUG ("%p, ", matrix[i]);
  }
  GST_DEBUG ("\n");
  return matrix;
}

static void
mixmatrix_free_matrix (gfloat ** matrix, int x)
{
  int i;

  for (i = 0; i < x; i++)
    g_free (matrix[i]);
  g_free (matrix);
}

static void
gst_mixmatrix_init (GstMixMatrix * mix)
{
  mix->grpsize = 8;
  mix->outsize = 1024;

  // start with zero pads
  mix->sinkpadalloc = mix->grpsize;
  mix->srcpadalloc = mix->grpsize;

  // allocate the pads
  mix->sinkpads = g_new (GstPad *, mix->sinkpadalloc);
  mix->sinkbs = g_new (GstByteStream *, mix->sinkpadalloc);

  mix->srcpads = g_new (GstPad *, mix->srcpadalloc);

  // allocate a similarly sized matrix
  mix->matrix = mixmatrix_alloc_matrix (mix->sinkpadalloc, mix->srcpadalloc);

  // set the loop function that does all the work
  gst_element_set_loop_function (GST_ELEMENT (mix), gst_mixmatrix_loop);
}

#define ROUND_UP(val,bound) ((((val)/bound)+1)*bound)

static void **
grow_ptrlist (void **origlist, int origsize, int newsize)
{
  void **newlist = g_new (void *, newsize);
  memcpy (newlist, origlist, sizeof (void *) * origsize);
  g_free (origlist);
  return newlist;
}

void
mixmatrix_resize (GstMixMatrix * mix, int sinkpads, int srcpads)
{
  int sinkresize = (sinkpads != mix->sinkpadalloc);
  int srcresize = (srcpads != mix->srcpadalloc);

  gfloat **newmatrix;
  int i;

  GST_DEBUG ("mixmatrix: resizing matrix!!!!\n");

  // check the sinkpads list
  if (sinkresize) {
    mix->sinkpads =
        (GstPad **) grow_ptrlist ((void **) mix->sinkpads, mix->sinkpadalloc,
        sinkpads);
    mix->sinkbs =
        (GstByteStream **) grow_ptrlist ((void **) mix->sinkbs,
        mix->sinkpadalloc, sinkpads);
  }
  // check the srcpads list
  if (srcresize) {
    mix->srcpads =
        (GstPad **) grow_ptrlist ((void **) mix->srcpads, mix->srcpadalloc,
        srcpads);
  }
  // now resize the matrix if either has changed
  if (sinkresize || srcresize) {
    // allocate the new matrix
    newmatrix = mixmatrix_alloc_matrix (sinkpads, srcpads);
    // if only the srcpad count changed (y axis), we can just copy
    if (!sinkresize) {
      memcpy (newmatrix, mix->matrix, sizeof (gfloat *) * sinkpads);
      // otherwise we have to copy line by line
    } else {
      for (i = 0; i < mix->srcpadalloc; i++)
        memcpy (newmatrix[i], mix->matrix[i],
            sizeof (gfloat) * mix->srcpadalloc);
    }

    // would signal here!

    // free old matrix and replace it
    mixmatrix_free_matrix (mix->matrix, mix->sinkpadalloc);
    mix->matrix = newmatrix;
  }

  mix->sinkpadalloc = sinkpads;
  mix->srcpadalloc = srcpads;
}

/*
static gboolean
gst_mixmatrix_set_all_caps (GstMixMatrix *mix)
{
  int i;

  // sink pads
  for (i=0;i<mix->sinkpadalloc;i++) {
    if (mix->sinkpads[i]) {
      if (GST_PAD_CAPS(mix->sinkpads[i]) == NULL)
        if (gst_pad_try_set_caps(mix->sinkpads[i],mix->caps) <= 0) return FALSE;
    }
  }

  // src pads
  for (i=0;i<mix->srcpadalloc;i++) {
    if (mix->srcpads[i]) {
      if (GST_PAD_CAPS(mix->srcpads[i]) == NULL)
        if (gst_pad_try_set_caps(mix->srcpads[i],mix->caps) <= 0) return FALSE;
    }
  }

  return TRUE;
}
*/

static GstPadLinkReturn
gst_mixmatrix_connect (GstPad * pad, const GstCaps * caps)
{
  GstMixMatrix *mix = GST_MIXMATRIX (GST_PAD_PARENT (pad));
  gint i;

  for (i = 0; i < mix->srcpadalloc; i++) {
    if (mix->srcpads[i]) {
      if (GST_PAD_CAPS (mix->srcpads[i]) == NULL) {
        if (gst_pad_try_set_caps (mix->srcpads[i], caps) <= 0) {
          return GST_PAD_LINK_REFUSED;
        }
      }
    }
  }

  mix->caps = gst_caps_copy (caps);

  return GST_PAD_LINK_OK;
}

static GstPad *
gst_mixmatrix_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstMixMatrix *mix;
  gint padnum;
  GstPad *pad = NULL;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_MIXMATRIX (element), NULL);

  mix = GST_MIXMATRIX (element);

  // figure out if it's a sink pad
  if (sscanf (name, "sink%d", &padnum)) {
    // check to see if it already exists
    if (padnum < mix->sinkpadalloc && mix->sinkpads[padnum])
      return mix->sinkpads[padnum];

    // determine if it's bigger than the current size
    if (padnum >= mix->sinkpadalloc)
      mixmatrix_resize (mix, ROUND_UP (padnum, mix->grpsize),
          mix->sinkpadalloc);

    pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&mixmatrix_sink_template), name);
    GST_PAD_ELEMENT_PRIVATE (pad) = GINT_TO_POINTER (padnum);
    gst_element_add_pad (GST_ELEMENT (mix), pad);
//    g_signal_connect(G_OBJECT(pad), "unlink", G_CALLBACK(sink_unlinked), mix);
    gst_pad_set_link_function (pad, gst_mixmatrix_connect);

    // create a bytestream for it
    mix->sinkbs[padnum] = gst_bytestream_new (pad);

    // store away the pad and account for it
    mix->sinkpads[padnum] = pad;
  }
  // or it's a src pad
  else if (sscanf (name, "src%d", &padnum)) {
    // check to see if it already exists
    if (padnum < mix->srcpadalloc && mix->srcpads[padnum])
      return mix->srcpads[padnum];

    // determine if it's bigger than the current size
    if (padnum >= mix->srcpadalloc)
      mixmatrix_resize (mix, ROUND_UP (padnum, mix->grpsize), mix->srcpadalloc);

    pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&mixmatrix_src_template), name);
    GST_PAD_ELEMENT_PRIVATE (pad) = GINT_TO_POINTER (padnum);
    gst_element_add_pad (GST_ELEMENT (mix), pad);
//    g_signal_connect(G_OBJECT(pad), "unlink", G_CALLBACK(sink_unlinked), mix);
    //gst_pad_set_link_function (pad, gst_mixmatrix_connect);

    // store away the pad and account for it  
    mix->srcpads[padnum] = pad;
  }

  return pad;
}

static void
gst_mixmatrix_loop (GstElement * element)
{
  GstMixMatrix *mix = GST_MIXMATRIX (element);
  int i, j, k;
  GstBuffer **inbufs;
  gfloat **infloats;
  GstBuffer **outbufs;
  gfloat **outfloats;
  int bytesize = sizeof (gfloat) * mix->outsize;
  gfloat gain;

  // create the output buffers
  outbufs = g_new (GstBuffer *, mix->srcpadalloc);
  outfloats = g_new (gfloat *, mix->srcpadalloc);
  for (i = 0; i < mix->srcpadalloc; i++) {
    if (mix->srcpads[i] != NULL) {
      outbufs[i] = gst_buffer_new_and_alloc (bytesize);
      outfloats[i] = (gfloat *) GST_BUFFER_DATA (outbufs[i]);
      memset (outfloats[i], 0, bytesize);
    }
  }

  // go through all the input buffers and pull them
  inbufs = g_new (GstBuffer *, mix->sinkpadalloc);
  infloats = g_new (gfloat *, mix->sinkpadalloc);
  for (i = 0; i < mix->sinkpadalloc; i++) {
    if (mix->sinkpads[i] != NULL) {
      gst_bytestream_read (mix->sinkbs[i], &inbufs[i], bytesize);
      infloats[i] = (gfloat *) GST_BUFFER_DATA (inbufs[i]);
      // loop through each src pad
      for (j = 0; j < mix->srcpadalloc; j++) {
        if (mix->srcpads[j] != NULL) {
/*
{
  int z;
  fprintf(stderr,"matrix is %p: ",mix->matrix);
  for (z=0;z<mix->sinkpadalloc;z++)
    fprintf(stderr,"%p, ",mix->matrix[i]);
  fprintf(stderr,"\n");
}
fprintf(stderr,"attempting to get gain for %dx%d\n",i,j);
*/
          gain = mix->matrix[i][j];
//          fprintf(stderr,"%d->%d=%0.2f ",i,j,gain);
          for (k = 0; k < mix->outsize; k++) {
            outfloats[j][k] += infloats[i][k] * gain;
          }
        }
      }
    }
  }
//  fprintf(stderr,"\n");

  for (i = 0; i < mix->srcpadalloc; i++) {
    if (mix->srcpads[i] != NULL) {
      gst_pad_push (mix->srcpads[i], GST_DATA (outbufs[i]));
    }
  }
}

static void
gst_mixmatrix_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMixMatrix *mix;

  g_return_if_fail (GST_IS_MIXMATRIX (object));
  mix = GST_MIXMATRIX (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_mixmatrix_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMixMatrix *mix;

  g_return_if_fail (GST_IS_MIXMATRIX (object));
  mix = GST_MIXMATRIX (object);

  switch (prop_id) {
    case ARG_SINKPADS:
      g_value_set_int (value, mix->sinkpadalloc);
      break;
    case ARG_SRCPADS:
      g_value_set_int (value, mix->srcpadalloc);
      break;
    case ARG_MATRIXPTR:
      g_value_set_pointer (value, mix->matrix);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  return gst_element_register (plugin, "mixmatrix",
      GST_RANK_NONE, GST_TYPE_MIXMATRIX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mixmatrix",
    "An audio mixer matrix",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
