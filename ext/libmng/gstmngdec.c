/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include "gstmngdec.h"
#include <gst/video/video.h>

static const GstElementDetails gst_mngdec_details =
GST_ELEMENT_DETAILS ("MNG video decoder",
    "Codec/Decoder/Video",
    "Decode a mng video to raw images",
    "Wim Taymans <wim@fluendo.com>");


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_mngdec_base_init (gpointer g_class);
static void gst_mngdec_class_init (GstMngDecClass * klass);
static void gst_mngdec_init (GstMngDec * mngdec);

static void gst_mngdec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_mngdec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_mngdec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_mngdec_loop (GstElement * element);

static GstCaps *gst_mngdec_src_getcaps (GstPad * pad);

static GstElementClass *parent_class = NULL;


GType
gst_mngdec_get_type (void)
{
  static GType mngdec_type = 0;

  if (!mngdec_type) {
    static const GTypeInfo mngdec_info = {
      sizeof (GstMngDecClass),
      gst_mngdec_base_init,
      NULL,
      (GClassInitFunc) gst_mngdec_class_init,
      NULL,
      NULL,
      sizeof (GstMngDec),
      0,
      (GInstanceInitFunc) gst_mngdec_init,
    };

    mngdec_type = g_type_register_static (GST_TYPE_ELEMENT, "GstMngDec",
        &mngdec_info, 0);
  }
  return mngdec_type;
}

static GstStaticPadTemplate gst_mngdec_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate gst_mngdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mng, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 0.0, MAX ]")
    );

static void
gst_mngdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mngdec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mngdec_sink_pad_template));
  gst_element_class_set_details (element_class, &gst_mngdec_details);
}

static void
gst_mngdec_class_init (GstMngDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_mngdec_change_state;

  gstelement_class->get_property = gst_mngdec_get_property;
  gstelement_class->set_property = gst_mngdec_set_property;

}


static GstPadLinkReturn
gst_mngdec_sinklink (GstPad * pad, const GstCaps * caps)
{
  GstMngDec *mngdec;
  GstStructure *structure;

  mngdec = GST_MNGDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_double (structure, "framerate", &mngdec->fps);
  gst_object_unref (mngdec);

  return TRUE;
}

static void
gst_mngdec_init (GstMngDec * mngdec)
{
  mngdec->sinkpad =
      gst_pad_new_from_static_template (&gst_mngdec_sink_pad_template, "sink");
  gst_element_add_pad (GST_ELEMENT (mngdec), mngdec->sinkpad);

  mngdec->srcpad =
      gst_pad_new_from_static_template (&gst_mngdec_src_pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (mngdec), mngdec->srcpad);

  gst_pad_set_link_function (mngdec->sinkpad, gst_mngdec_sinklink);

  gst_pad_set_getcaps_function (mngdec->srcpad, gst_mngdec_src_getcaps);

  mngdec->mng = NULL;
  mngdec->buffer_out = NULL;

  mngdec->color_type = -1;
  mngdec->width = -1;
  mngdec->height = -1;
  mngdec->fps = -1;

  gst_element_set_loop_function (GST_ELEMENT (mngdec), gst_mngdec_loop);
}

static GstCaps *
gst_mngdec_src_getcaps (GstPad * pad)
{
  GstMngDec *mngdec;
  GstCaps *caps;
  gint i;
  GstPadTemplate *templ;
  GstCaps *inter;

  mngdec = GST_MNGDEC (gst_pad_get_parent (pad));
  templ = gst_static_pad_template_get (&gst_mngdec_src_pad_template);
  caps = gst_caps_copy (gst_pad_template_get_caps (templ));

  if (mngdec->color_type != -1) {
    GstCaps *to_inter = NULL;

    switch (mngdec->color_type) {
      case MNG_COLORTYPE_RGB:
        to_inter = gst_caps_new_simple ("video/x-raw-rgb",
            "bpp", G_TYPE_INT, 24, NULL);
        break;
      case MNG_COLORTYPE_RGBA:
        to_inter = gst_caps_new_simple ("video/x-raw-rgb",
            "bpp", G_TYPE_INT, 32, NULL);
        break;
      case MNG_COLORTYPE_GRAY:
      case MNG_COLORTYPE_INDEXED:
      case MNG_COLORTYPE_GRAYA:
      default:
        GST_ELEMENT_ERROR (mngdec, STREAM, NOT_IMPLEMENTED, (NULL),
            ("mngdec does not support grayscale or paletted data yet"));
        break;
    }
    inter = gst_caps_intersect (caps, to_inter);
    gst_caps_free (caps);
    gst_caps_free (to_inter);
    caps = inter;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    if (mngdec->width != -1) {
      gst_structure_set (structure, "width", G_TYPE_INT, mngdec->width, NULL);
    }

    if (mngdec->height != -1) {
      gst_structure_set (structure, "height", G_TYPE_INT, mngdec->height, NULL);
    }

    if (mngdec->fps != -1) {
      gst_structure_set (structure,
          "framerate", G_TYPE_DOUBLE, mngdec->fps, NULL);
    }
  }

  gst_object_unref (mngdec);
  gst_object_unref (templ);

  return caps;
}


static void
gst_mngdec_loop (GstElement * element)
{
  GstMngDec *mngdec;
  mng_retcode ret;

  mngdec = GST_MNGDEC (element);

  if (mngdec->first) {
    GST_DEBUG ("display");
    ret = mng_readdisplay (mngdec->mng);
    mngdec->first = FALSE;
  } else {
    GST_DEBUG ("resume");
    ret = mng_display_resume (mngdec->mng);
  }
  if (ret == MNG_NEEDTIMERWAIT) {
    /* libmng needs more data later on */
  } else {
    /* assume EOS here */
    gst_pad_push (mngdec->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (element);
  }
}

static void
gst_mngdec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstMngDec *mngdec;

  mngdec = GST_MNGDEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_mngdec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstMngDec *mngdec;

  mngdec = GST_MNGDEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static mng_bool
mngdec_error (mng_handle mng, mng_int32 code, mng_int8 severity,
    mng_chunkid chunktype, mng_uint32 chunkseq,
    mng_int32 extra1, mng_int32 extra2, mng_pchar text)
{
  GstMngDec *mngdec;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));

  GST_ERROR_OBJECT (mngdec, "error in chunk %4.4s (%d): %s",
      (gchar *) & chunktype, chunkseq, text);

  return FALSE;
}

static mng_bool
mngdec_openstream (mng_handle mng)
{
  GstMngDec *mngdec;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));

  mngdec->bs = gst_bytestream_new (mngdec->sinkpad);

  return MNG_TRUE;
}

static mng_bool
mngdec_closestream (mng_handle mng)
{
  GstMngDec *mngdec;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));

  gst_bytestream_destroy (mngdec->bs);

  return MNG_TRUE;
}

static gboolean
mngdec_handle_sink_event (GstMngDec * mngdec)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;

  gst_bytestream_get_status (mngdec->bs, &remaining, &event);

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;
  GST_DEBUG ("mngdec: event %p %d", event, type);

  switch (type) {
    case GST_EVENT_EOS:
      gst_bytestream_flush (mngdec->bs, remaining);
      gst_pad_event_default (mngdec->sinkpad, event);
      return FALSE;
    case GST_EVENT_FLUSH:
      break;
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG ("discontinuous event");
      break;
    default:
      g_warning ("unhandled event %d", type);
      break;
  }

  gst_event_unref (event);
  return TRUE;
}

static mng_bool
mngdec_readdata (mng_handle mng, mng_ptr buffer,
    mng_uint32 size, mng_uint32 * bytesread)
{
  GstMngDec *mngdec;
  guint8 *bytes;
  guint32 read;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));

  GST_DEBUG ("read data %d", size);

  do {
    read = gst_bytestream_peek_bytes (mngdec->bs, &bytes, size);
    if (read != size) {
      if (!mngdec_handle_sink_event (mngdec)) {
        /* EOS */
        *bytesread = 0;
        return MNG_FALSE;
      }
    } else {
      break;
    }
  } while (TRUE);

  memcpy (buffer, bytes, size);
  gst_bytestream_flush_fast (mngdec->bs, read);
  *bytesread = size;

  return MNG_TRUE;
}

static mng_uint32
mngdec_gettickcount (mng_handle mng)
{
  GTimeVal time;
  guint32 val;

  g_get_current_time (&time);

  val = time.tv_sec * 1000 + time.tv_usec;
  GST_DEBUG ("get tick count %d", val);

  return val;
}

static mng_bool
mngdec_settimer (mng_handle mng, mng_uint32 msecs)
{
  GstMngDec *mngdec;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));
  //mymng->delay = msecs;
  GST_DEBUG ("set timer %d", msecs);

  return MNG_TRUE;
}

static mng_bool
mngdec_processheader (mng_handle mng, mng_uint32 width, mng_uint32 height)
{
  GstMngDec *mngdec;
  guint32 playtime;
  guint32 framecount;
  guint32 ticks;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));

  GST_DEBUG ("process header %d %d", width, height);

  playtime = mng_get_playtime (mng);
  framecount = mng_get_framecount (mng);
  ticks = mng_get_ticks (mng);

  if (playtime == 0) {
    mngdec->fps = ticks;
  } else {
    mngdec->fps = ((gfloat) ticks) / playtime;
  }

  if (mngdec->width != width || mngdec->height != height) {
    mngdec->width = width;
    mngdec->stride = ((width + 3) & ~3) * 4;
    mngdec->height = height;

    if (GST_PAD_LINK_FAILED (gst_pad_renegotiate (mngdec->srcpad))) {
      GST_ELEMENT_ERROR (mngdec, CORE, NEGOTIATION, (NULL), (NULL));
      return MNG_FALSE;
    }

    mngdec->buffer_out =
        gst_buffer_new_and_alloc (mngdec->height * mngdec->stride);
  }
  return MNG_TRUE;
}

static mng_ptr
mngdec_getcanvasline (mng_handle mng, mng_uint32 line)
{
  GstMngDec *mngdec;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));

  GST_DEBUG ("get canvas line %d", line);

  return (mng_ptr) (GST_BUFFER_DATA (mngdec->buffer_out) +
      (line * mngdec->stride));
}

static mng_bool
mngdec_refresh (mng_handle mng, mng_uint32 x, mng_uint32 y,
    mng_uint32 w, mng_uint32 h)
{
  GstMngDec *mngdec;
  guint32 current;

  mngdec = GST_MNGDEC (mng_get_userdata (mng));

  current = mng_get_currentplaytime (mng);

  GST_DEBUG ("refresh %d %d %d %d", x, y, w, h);
  if (h == mngdec->height) {
    GstBuffer *out = gst_buffer_copy (mngdec->buffer_out);

    gst_pad_push (mngdec->srcpad, GST_DATA (out));
  }

  return MNG_TRUE;
}

static GstStateChangeReturn
gst_mngdec_change_state (GstElement * element, GstStateChange transition)
{
  GstMngDec *mngdec = GST_MNGDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* init library, make sure to pass an alloc function that sets the
       * memory to 0 */
      mngdec->mng =
          mng_initialize (mngdec, (mng_memalloc) g_malloc0,
          (mng_memfree) g_free, MNG_NULL);
      if (mngdec->mng == MNG_NULL) {
        return GST_STATE_CHANGE_FAILURE;
      }
      /* set the callbacks */
      mng_setcb_errorproc (mngdec->mng, mngdec_error);
      mng_setcb_openstream (mngdec->mng, mngdec_openstream);
      mng_setcb_closestream (mngdec->mng, mngdec_closestream);
      mng_setcb_readdata (mngdec->mng, mngdec_readdata);
      mng_setcb_gettickcount (mngdec->mng, mngdec_gettickcount);
      mng_setcb_settimer (mngdec->mng, mngdec_settimer);
      mng_setcb_processheader (mngdec->mng, mngdec_processheader);
      mng_setcb_getcanvasline (mngdec->mng, mngdec_getcanvasline);
      mng_setcb_refresh (mngdec->mng, mngdec_refresh);
      mng_set_canvasstyle (mngdec->mng, MNG_CANVAS_RGBA8);
      mng_set_doprogressive (mngdec->mng, MNG_FALSE);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      mngdec->first = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      mng_cleanup (&mngdec->mng);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
