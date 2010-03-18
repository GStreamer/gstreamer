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

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

static GstStaticPadTemplate gst_mng_dec_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate gst_mng_dec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mng, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 0.0, MAX ]")
    );

static void gst_mng_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_mng_dec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_mng_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_mng_dec_sink_event (GstPad * pad, GstEvent * event);

GST_BOILERPLATE (GstMngDec, gst_mng_dec, GstElement, GST_TYPE_ELEMENT);

static void
gst_mng_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mng_dec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mng_dec_sink_pad_template));
  gst_element_class_set_details_simple (element_class, "MNG video decoder",
      "Codec/Decoder/Video",
      "Decode a mng video to raw images", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_mng_dec_class_init (GstMngDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_mng_dec_get_property;
  gobject_class->set_property = gst_mng_dec_set_property;

  gstelement_class->change_state = gst_mng_dec_change_state;
}

static gboolean
gst_mng_dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMngDec *mngdec;
  GstStructure *structure;

  mngdec = GST_MNG_DEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_double (structure, "framerate", &mngdec->fps);

  gst_object_unref (mngdec);

  return TRUE;
}

static void
gst_mng_dec_init (GstMngDec * mngdec, GstMngDecClass * gclass)
{
  mngdec->sinkpad =
      gst_pad_new_from_static_template (&gst_mng_dec_sink_pad_template, "sink");
  gst_element_add_pad (GST_ELEMENT (mngdec), mngdec->sinkpad);

  mngdec->srcpad =
      gst_pad_new_from_static_template (&gst_mng_dec_src_pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (mngdec), mngdec->srcpad);

  gst_pad_set_setcaps_function (mngdec->sinkpad, gst_mng_dec_sink_setcaps);
  gst_pad_set_event_function (mngdec->sinkpad, gst_mng_dec_sink_event);
  //gst_pad_set_getcaps_function (mngdec->srcpad, gst_mng_dec_src_getcaps);

  mngdec->mng = NULL;
  mngdec->buffer_out = NULL;

  mngdec->color_type = -1;
  mngdec->width = -1;
  mngdec->height = -1;
  mngdec->fps = -1;
}

#if 0
static GstCaps *
gst_mng_dec_src_getcaps (GstPad * pad)
{
  GstMngDec *mngdec;
  GstCaps *caps;
  gint i;
  GstPadTemplate *templ;
  GstCaps *inter;

  mngdec = GST_MNG_DEC (gst_pad_get_parent (pad));
  templ = gst_static_pad_template_get (&gst_mng_dec_src_pad_template);
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
#endif

#if 0
static void
gst_mng_dec_loop (GstElement * element)
{
  GstMngDec *mngdec;
  mng_retcode ret;

  mngdec = GST_MNG_DEC (element);

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
    gst_pad_push_event (mngdec->srcpad, (gst_event_new_eos ()));
  }
}
#endif

static void
gst_mng_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstMngDec *mngdec;

  mngdec = GST_MNG_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_mng_dec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstMngDec *mngdec;

  mngdec = GST_MNG_DEC (object);

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

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));

  GST_ERROR_OBJECT (mngdec, "error in chunk %4.4s (%d): %s",
      (gchar *) & chunktype, chunkseq, text);

  return FALSE;
}

static mng_bool
mngdec_openstream (mng_handle mng)
{
  GstMngDec *mngdec;

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));

  return MNG_TRUE;
}

static mng_bool
mngdec_closestream (mng_handle mng)
{
  GstMngDec *mngdec;

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));

  return MNG_TRUE;
}

static gboolean
gst_mng_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstEventType type;
  GstMngDec *mngdec;

  mngdec = GST_MNG_DEC_CAST (gst_pad_get_parent (pad));

  type = GST_EVENT_TYPE (event);

  GST_DEBUG_OBJECT (mngdec, "mngdec: event %p %d", event, type);

  switch (type) {
    case GST_EVENT_EOS:
      break;
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      break;
    case GST_EVENT_NEWSEGMENT:
      break;
    default:
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
#if 0
  guint8 *bytes;
  guint32 read;
#endif

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));

  GST_DEBUG ("read data %d", size);

#if 0
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
#endif

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

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));
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

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));

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

#if 0
    if (GST_PAD_LINK_FAILED (gst_pad_renegotiate (mngdec->srcpad))) {
      GST_ELEMENT_ERROR (mngdec, CORE, NEGOTIATION, (NULL), (NULL));
      return MNG_FALSE;
    }
#endif

    mngdec->buffer_out =
        gst_buffer_new_and_alloc (mngdec->height * mngdec->stride);
  }
  return MNG_TRUE;
}

static mng_ptr
mngdec_getcanvasline (mng_handle mng, mng_uint32 line)
{
  GstMngDec *mngdec;

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));

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

  mngdec = GST_MNG_DEC (mng_get_userdata (mng));

  current = mng_get_currentplaytime (mng);

  GST_DEBUG ("refresh %d %d %d %d", x, y, w, h);
  if (h == mngdec->height) {
    GstBuffer *out = gst_buffer_copy (mngdec->buffer_out);

    gst_pad_push (mngdec->srcpad, out);
  }

  return MNG_TRUE;
}

static GstStateChangeReturn
gst_mng_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMngDec *mngdec = GST_MNG_DEC (element);

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
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
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
  return ret;
}
