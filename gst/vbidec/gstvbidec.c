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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <gst/gst.h>
#include "gstvbidec.h"
#include "vbidata.h"
#include "vbiscreen.h"

#define GST_TYPE_VBIDEC \
  (gst_vbidec_get_type())
#define GST_VBIDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VBIDEC,GstVBIDec))
#define GST_VBIDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VBIDEC,GstVBIDec))
#define GST_IS_VBIDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VBIDEC))
#define GST_IS_VBIDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VBIDEC))

//typedef struct _GstVBIDec GstVBIDec;
typedef struct _GstVBIDecClass GstVBIDecClass;

struct _GstVBIDec
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;
  char caption[128];
  vbiscreen_t *vbiscreen;
  vbidata_t *vbidata;
  int caption_type;
  gboolean dvd_input;
};

struct _GstVBIDecClass
{
  GstElementClass parent_class;
};

GType gst_vbidec_get_type (void);

/* elementfactory information */
static GstElementDetails gst_vbidec_details =
GST_ELEMENT_DETAILS ("VBI decoder",
    "Codec/Decoder/Video",
    "Decodes closed captions and XDS data from VBI data",
    "David I. Lehn <dlehn@users.sourceforge.net>");

/* VBIDec signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_VERBOSE,
  ARG_CAPTION_TYPE,
  ARG_DVD_INPUT
};

static GstStaticPadTemplate gst_vbidec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_vbidec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/plain")
    );


#define GST_TYPE_VBIDEC_CAPTION_TYPE_TYPE (gst_vbidec_caption_type_get_type())
static GType
gst_vbidec_caption_type_get_type (void)
{
  static GType vbidec_caption_type_type = 0;
  static GEnumValue vbidec_caption_type[] = {
    {CAPTURE_OFF, "0", "Closed Captions off"},
    {CAPTURE_CC1, "1", "Closed Caption CC1"},
    {CAPTURE_CC2, "2", "Closed Caption CC2"},
    {CAPTURE_CC3, "4", "Closed Caption CC3"},
    {CAPTURE_CC4, "5", "Closed Caption CC4"},
    {CAPTURE_T1, "6", "Closed Caption T1"},
    {CAPTURE_T2, "7", "Closed Caption T2"},
    {CAPTURE_T3, "8", "Closed Caption T3"},
    {CAPTURE_T4, "9", "Closed Caption T4"},
    {0, NULL, NULL},
  };

  if (!vbidec_caption_type_type) {
    vbidec_caption_type_type =
        g_enum_register_static ("GstVBIDecCaptionTypeType",
        vbidec_caption_type);
  }
  return vbidec_caption_type_type;
}

static void gst_vbidec_base_init (gpointer g_class);
static void gst_vbidec_class_init (GstVBIDecClass * klass);
static void gst_vbidec_init (GstVBIDec * vbidec);

static void gst_vbidec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vbidec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_vbidec_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

/*static guint gst_vbidec_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_vbidec_get_type (void)
{
  static GType vbidec_type = 0;

  if (!vbidec_type) {
    static const GTypeInfo vbidec_info = {
      sizeof (GstVBIDecClass),
      gst_vbidec_base_init,
      NULL,
      (GClassInitFunc) gst_vbidec_class_init,
      NULL,
      NULL,
      sizeof (GstVBIDec),
      0,
      (GInstanceInitFunc) gst_vbidec_init,
    };

    vbidec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstVBIDec", &vbidec_info, 0);
  }
  return vbidec_type;
}

static void
gst_vbidec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_vbidec_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vbidec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vbidec_sink_template));
}
static void
gst_vbidec_class_init (GstVBIDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_vbidec_set_property;
  gobject_class->get_property = gst_vbidec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VERBOSE,
      g_param_spec_boolean ("verbose", "verbose", "verbose",
          FALSE, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CAPTION_TYPE,
      g_param_spec_enum ("caption type", "caption type", "Closed Caption Type",
          GST_TYPE_VBIDEC_CAPTION_TYPE_TYPE, CAPTURE_OFF, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DVD_INPUT,
      g_param_spec_boolean ("dvd input", "dvd input",
          "VBI is encapsulated in MPEG2 GOP user_data field (as on DVDs)",
          FALSE, G_PARAM_READWRITE));
}

static void
gst_vbidec_init (GstVBIDec * vbidec)
{
  /* create the sink and src pads */
  vbidec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_vbidec_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (vbidec), vbidec->sinkpad);
  gst_pad_set_chain_function (vbidec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vbidec_chain));

  vbidec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_vbidec_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (vbidec), vbidec->srcpad);

  vbidec->vbiscreen = vbiscreen_new (0, 0, 1.0, 0, (void *) vbidec);
  vbidec->vbidata = vbidata_new_line (vbidec->vbiscreen, 0);
  vbidec->caption_type = CAPTURE_OFF;
  vbidata_capture_mode (vbidec->vbidata, vbidec->caption_type);
  vbidec->dvd_input = FALSE;
}

static void
line21_decode (GstVBIDec * vbidec, guint8 * data, guint32 size)
{
  vbidata_process_line (vbidec->vbidata, data, 0);
}

static void
dvd_user_data_decode (GstVBIDec * vbidec, guint8 * data, guint32 size)
{
  //char caption[128];
  //int ci; /* caption index */
  int i;                        /* buf index */
  int num_disp_field;
  guint8 b1, b2;
  int w;

  //g_print("%%%% vbi decode\n");
  //g_print("== %p %d\n", data, size);
  i = 0;
  /* Check for Closed Captioning data */
  if (data[i] != 0x43 || data[i + 1] != 0x43 ||
      data[i + 2] != 0x01 || data[i + 3] != 0xf8) {
    g_print ("non-CC data\n");
    return;
  }
  //g_print ("CC data\n");
  i += 4;                       /* above */
  i += 4;                       /* ? */
  num_disp_field = data[i] & 0x3f;
  //g_print ("ndf %d\n", num_disp_field);
  while ((data[i] & 0xfe) == 0xfe) {
    if (data[i] & 0x1) {
      b1 = data[i + 1] & 0x7f;
      b2 = data[i + 2] & 0x7f;
      w = (b2 << 8) | b1;
      vbidata_process_16b (vbidec->vbidata, 0, w);
    }
    i += 3;
  }
}

static void
gst_vbidec_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVBIDec *vbidec = GST_VBIDEC (gst_pad_get_parent (pad));
  guint32 size;
  guint8 *data;
  guint64 pts;

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);
  pts = GST_BUFFER_TIMESTAMP (buf);

  /*
     g_print("** user_data: addr:%p len:%d state:%d\n", data, size, 0);
     {
     int i;
     guint8 ud;
     g_print("** \"");
     for (i=0; i<size; i++) {
     ud = data[i];
     if (isprint((char)ud)) {
     g_print("%c", (char)ud);
     } else {
     g_print("[0x%02x]", ud);
     }
     }
     g_print("\"\n");
     }
   */

  if (vbidec->dvd_input) {
    dvd_user_data_decode (vbidec, data, size);
  } else {
    line21_decode (vbidec, data, size);
  }

  gst_buffer_unref (buf);
}

void
gst_vbidec_show_text (GstVBIDec * vbidec, char *text, int len)
{
  //fprintf(stderr, "%*s\n", len, text);
  if (len > 0) {
    if (GST_PAD_IS_USABLE (vbidec->srcpad)) {
      GstBuffer *buf = gst_buffer_new_and_alloc (len);

      memcpy (GST_BUFFER_DATA (buf), text, len);
      GST_BUFFER_SIZE (buf) = len;
      // FIXME
      //GST_BUFFER_TIMESTAMP (buf) = vbidec->...
      //...
      //fprintf(stderr, "vbi text pushed\n");
      gst_pad_push (vbidec->srcpad, GST_DATA (buf));
    }
  }
}

static void
gst_vbidec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVBIDec *vbidec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VBIDEC (object));
  vbidec = GST_VBIDEC (object);

  switch (prop_id) {
    case ARG_VERBOSE:
      vbidata_set_verbose (vbidec->vbidata, g_value_get_boolean (value));
      vbiscreen_set_verbose (vbidec->vbiscreen, g_value_get_boolean (value));
      break;
    case ARG_DVD_INPUT:
      vbidec->dvd_input = g_value_get_boolean (value);
      break;
    case ARG_CAPTION_TYPE:
      vbidec->caption_type = g_value_get_enum (value);
      vbidata_capture_mode (vbidec->vbidata, vbidec->caption_type);
      break;
    default:
      break;
  }
}

static void
gst_vbidec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVBIDec *vbidec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VBIDEC (object));
  vbidec = GST_VBIDEC (object);

  switch (prop_id) {
    case ARG_DVD_INPUT:
      g_value_set_boolean (value, vbidec->dvd_input);
      break;
    case ARG_CAPTION_TYPE:
      g_value_set_enum (value, vbidec->caption_type);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "vbidec", GST_RANK_NONE,
      GST_TYPE_VBIDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vbidec",
    "Decodes closed captions and XDS data from VBI data",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
