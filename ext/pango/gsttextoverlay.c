


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <gst/gst.h>
#include "gsttextoverlay.h"
/*#include "gsttexttestsrc.h"*/
/*#include "gstsubparse.h"*/
/*#include "SDL_blit.h"*/

static GstElementDetails textoverlay_details = {
  "Text Overlay",
  "Filter/Editor/Video",
  "Adds text strings on top of a video buffer",
  "Gustavo J. A. M. Carneiro <gjc@inescporto.pt>"
};

enum
{
  ARG_0,
  ARG_TEXT,
  ARG_VALIGN,
  ARG_HALIGN,
  ARG_X0,
  ARG_Y0,
  ARG_FONT_DESC,
};


static GstStaticPadTemplate textoverlay_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
	"format = (fourcc) I420, "
	"width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate video_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
	"format = (fourcc) I420, "
	"width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate text_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-pango-markup; text/plain")
    );

static void gst_textoverlay_base_init (gpointer g_class);
static void gst_textoverlay_class_init (GstTextOverlayClass * klass);
static void gst_textoverlay_init (GstTextOverlay * overlay);
static void gst_textoverlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_textoverlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_textoverlay_change_state (GstElement *
    element);
static void gst_textoverlay_finalize (GObject * object);


static GstElementClass *parent_class = NULL;

/*static guint gst_textoverlay_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_textoverlay_get_type (void)
{
  static GType textoverlay_type = 0;

  if (!textoverlay_type) {
    static const GTypeInfo textoverlay_info = {
      sizeof (GstTextOverlayClass),
      gst_textoverlay_base_init,
      NULL,
      (GClassInitFunc) gst_textoverlay_class_init,
      NULL,
      NULL,
      sizeof (GstTextOverlay),
      0,
      (GInstanceInitFunc) gst_textoverlay_init,
    };
    textoverlay_type =
	g_type_register_static (GST_TYPE_ELEMENT, "GstTextOverlay",
	&textoverlay_info, 0);
  }
  return textoverlay_type;
}

static void
gst_textoverlay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&textoverlay_src_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&text_sink_template_factory));

  gst_element_class_set_details (element_class, &textoverlay_details);
}

static void
gst_textoverlay_class_init (GstTextOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_textoverlay_finalize;
  gobject_class->set_property = gst_textoverlay_set_property;
  gobject_class->get_property = gst_textoverlay_get_property;

  gstelement_class->change_state = gst_textoverlay_change_state;
  klass->pango_context = pango_ft2_get_context (72, 72);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TEXT,
      g_param_spec_string ("text", "text",
	  "Text to be display,"
	  " in pango markup format.", "", G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VALIGN,
      g_param_spec_string ("valign", "vertical alignment",
	  "Vertical alignment of the text. "
	  "Can be either 'baseline', 'bottom', or 'top'",
	  "baseline", G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HALIGN,
      g_param_spec_string ("halign", "horizontal alignment",
	  "Horizontal alignment of the text. "
	  "Can be either 'left', 'right', or 'center'",
	  "center", G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_X0,
      g_param_spec_int ("x0", "X position",
	  "Initial X position."
	  " Horizontal aligment takes this point"
	  " as reference.", G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_Y0,
      g_param_spec_int ("y0", "Y position",
	  "Initial Y position."
	  " Vertical aligment takes this point"
	  " as reference.", G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
	  "Pango font description of font "
	  "to be used for rendering. "
	  "See documentation of "
	  "pango_font_description_from_string"
	  " for syntax.", "", G_PARAM_WRITABLE));
}


static void
resize_bitmap (GstTextOverlay * overlay, int width, int height)
{
  FT_Bitmap *bitmap = &overlay->bitmap;
  int pitch = (width | 3) + 1;
  int size = pitch * height;

  /* no need to keep reallocating; just keep the maximum size so far */
  if (size <= overlay->bitmap_buffer_size) {
    bitmap->rows = height;
    bitmap->width = width;
    bitmap->pitch = pitch;
    memset (bitmap->buffer, 0, overlay->bitmap_buffer_size);
    return;
  }
  if (!bitmap->buffer) {
    /* initialize */
    bitmap->pixel_mode = ft_pixel_mode_grays;
    bitmap->num_grays = 256;
  }
  if (bitmap->buffer)
    bitmap->buffer = g_realloc (bitmap->buffer, size);
  else
    bitmap->buffer = g_malloc (size);
  bitmap->rows = height;
  bitmap->width = width;
  bitmap->pitch = pitch;
  memset (bitmap->buffer, 0, size);
  overlay->bitmap_buffer_size = size;
}

static void
render_text (GstTextOverlay * overlay)
{
  PangoRectangle ink_rect, logical_rect;

  pango_layout_get_pixel_extents (overlay->layout, &ink_rect, &logical_rect);
  resize_bitmap (overlay, ink_rect.width, ink_rect.height + ink_rect.y);
  pango_ft2_render_layout (&overlay->bitmap, overlay->layout, 0, 0);
  overlay->baseline_y = ink_rect.y;
}

/* static GstPadLinkReturn */
/* gst_textoverlay_text_sinkconnect (GstPad *pad, GstCaps *caps) */
/* { */
/*     return GST_PAD_LINK_DONE; */
/* } */


static GstPadLinkReturn
gst_textoverlay_video_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstTextOverlay *overlay;
  GstStructure *structure;

  overlay = GST_TEXTOVERLAY (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  overlay->width = overlay->height = 0;
  gst_structure_get_int (structure, "width", &overlay->width);
  gst_structure_get_int (structure, "height", &overlay->height);

  return gst_pad_try_set_caps (overlay->srcpad, caps);
}


static void
gst_text_overlay_blit_yuv420 (GstTextOverlay * overlay, FT_Bitmap * bitmap,
    guchar * pixbuf, int x0, int y0)
{
  int y;			/* text bitmap coordinates */
  int x1, y1;			/* video buffer coordinates */
  int rowinc, bit_rowinc, uv_rowinc;
  guchar *p, *bitp, *u_p;
  int video_width = overlay->width, video_height = overlay->height;
  int bitmap_x0 = x0 < 1 ? -(x0 - 1) : 1;	/* 1 pixel border */
  int bitmap_y0 = y0 < 1 ? -(y0 - 1) : 1;	/* 1 pixel border */
  int bitmap_width = bitmap->width - bitmap_x0;
  int bitmap_height = bitmap->rows - bitmap_y0;
  int u_plane_size;
  int skip_y, skip_x;
  guchar v;

  if (x0 + bitmap_x0 + bitmap_width > video_width - 1)	/* 1 pixel border */
    bitmap_width -= x0 + bitmap_x0 + bitmap_width - video_width + 1;
  if (y0 + bitmap_y0 + bitmap_height > video_height - 1)	/* 1 pixel border */
    bitmap_height -= y0 + bitmap_y0 + bitmap_height - video_height + 1;

  rowinc = video_width - bitmap_width;
  uv_rowinc = video_width / 2 - bitmap_width / 2;
  bit_rowinc = bitmap->pitch - bitmap_width;
  u_plane_size = (video_width / 2) * (video_height / 2);

  y1 = y0 + bitmap_y0;
  x1 = x0 + bitmap_x0;
  p = pixbuf + video_width * y1 + x1;
  bitp = bitmap->buffer + bitmap->pitch * bitmap_y0 + bitmap_x0;
  for (y = bitmap_y0; y < bitmap_height; y++) {
    int n;

    for (n = bitmap_width; n > 0; --n) {
      v = *bitp;
      if (v) {
	p[-1] = CLAMP (p[-1] - v, 0, 255);
	p[1] = CLAMP (p[1] - v, 0, 255);
	p[-video_width] = CLAMP (p[-video_width] - v, 0, 255);
	p[video_width] = CLAMP (p[video_width] - v, 0, 255);
      }
      p++;
      bitp++;
    }
    p += rowinc;
    bitp += bit_rowinc;
  }

  y = bitmap_y0;
  y1 = y0 + bitmap_y0;
  x1 = x0 + bitmap_x0;
  bitp = bitmap->buffer + bitmap->pitch * bitmap_y0 + bitmap_x0;
  p = pixbuf + video_width * y1 + x1;
  u_p =
      pixbuf + video_width * video_height + (video_width >> 1) * (y1 >> 1) +
      (x1 >> 1);
  skip_y = 0;
  skip_x = 0;

  for (; y < bitmap_height; y++) {
    int n;

    x1 = x0 + bitmap_x0;
    skip_x = 0;
    for (n = bitmap_width; n > 0; --n) {
      v = *bitp;
      if (v) {
	*p = v;
	if (!skip_y) {
	  u_p[0] = u_p[u_plane_size] = 0x80;
	}
      }
      if (!skip_y) {
	skip_x = !skip_x;
	if (!skip_x)
	  u_p++;
      }
      p++;
      bitp++;
    }
    /*if (!skip_x && !skip_y) u_p--; */
    p += rowinc;
    bitp += bit_rowinc;
    skip_y = !skip_y;
    u_p += skip_y ? uv_rowinc : 0;
  }
}


static void
gst_textoverlay_video_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstTextOverlay *overlay;
  guchar *pixbuf;
  gint x0, y0;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  overlay = GST_TEXTOVERLAY (gst_pad_get_parent (pad));
  g_return_if_fail (overlay != NULL);
  g_return_if_fail (GST_IS_TEXTOVERLAY (overlay));

  pixbuf = GST_BUFFER_DATA (buf);

  x0 = overlay->x0;
  y0 = overlay->y0;
  switch (overlay->valign) {
    case GST_TEXT_OVERLAY_VALIGN_BOTTOM:
      y0 += overlay->bitmap.rows;
      break;
    case GST_TEXT_OVERLAY_VALIGN_BASELINE:
      y0 -= (overlay->bitmap.rows - overlay->baseline_y);
      break;
    case GST_TEXT_OVERLAY_VALIGN_TOP:
      break;
  }

  switch (overlay->halign) {
    case GST_TEXT_OVERLAY_HALIGN_LEFT:
      break;
    case GST_TEXT_OVERLAY_HALIGN_RIGHT:
      x0 -= overlay->bitmap.width;
      break;
    case GST_TEXT_OVERLAY_HALIGN_CENTER:
      x0 -= overlay->bitmap.width / 2;
      break;
  }

  if (overlay->bitmap.buffer)
    gst_text_overlay_blit_yuv420 (overlay, &overlay->bitmap, pixbuf, x0, y0);

  gst_pad_push (overlay->srcpad, GST_DATA (buf));
}

#define PAST_END(buffer, time) \
  (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE && \
   GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE && \
   GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer) \
     < (time))

static void
gst_textoverlay_loop (GstElement * element)
{
  GstTextOverlay *overlay;
  GstBuffer *video_frame;
  guint64 now;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_TEXTOVERLAY (element));
  overlay = GST_TEXTOVERLAY (element);

  video_frame = GST_BUFFER (gst_pad_pull (overlay->video_sinkpad));
  now = GST_BUFFER_TIMESTAMP (video_frame);

  /*
   * This state machine has a bug that can't be resolved easily.
   * (Needs a more complicated state machine.)  Basically, if the
   * text that came from a buffer from the sink pad is being
   * displayed, and the default text is changed by set_parameter,
   * we'll incorrectly display the default text.
   *
   * Otherwise, this is a pretty decent state machine that handles
   * buffer timestamps and durations correctly.  (I think)
   */

  while (overlay->next_buffer == NULL) {
    GST_DEBUG ("attempting to pull a buffer");

    /* read all text buffers until we get one "in the future" */
    if (!GST_PAD_IS_USABLE (overlay->text_sinkpad)) {
      break;
    }
    overlay->next_buffer = GST_BUFFER (gst_pad_pull (overlay->text_sinkpad));
    if (!overlay->next_buffer)
      break;

    if (PAST_END (overlay->next_buffer, now)) {
      gst_buffer_unref (overlay->next_buffer);
      overlay->next_buffer = NULL;
    }
  }

  if (overlay->next_buffer &&
      (GST_BUFFER_TIMESTAMP (overlay->next_buffer) <= now ||
	  GST_BUFFER_TIMESTAMP (overlay->next_buffer) == GST_CLOCK_TIME_NONE)) {
    GST_DEBUG ("using new buffer");

    if (overlay->current_buffer) {
      gst_buffer_unref (overlay->current_buffer);
    }
    overlay->current_buffer = overlay->next_buffer;
    overlay->next_buffer = NULL;

    GST_DEBUG ("rendering '%*s'",
	GST_BUFFER_SIZE (overlay->current_buffer),
	GST_BUFFER_DATA (overlay->current_buffer));
    pango_layout_set_markup (overlay->layout,
	GST_BUFFER_DATA (overlay->current_buffer),
	GST_BUFFER_SIZE (overlay->current_buffer));
    render_text (overlay);
    overlay->need_render = FALSE;
  }

  if (overlay->current_buffer && PAST_END (overlay->current_buffer, now)) {
    GST_DEBUG ("dropping old buffer");

    gst_buffer_unref (overlay->current_buffer);
    overlay->current_buffer = NULL;

    overlay->need_render = TRUE;
  }

  if (overlay->need_render) {
    GST_DEBUG ("rendering '%s'", overlay->default_text);
    pango_layout_set_markup (overlay->layout,
	overlay->default_text, strlen (overlay->default_text));
    render_text (overlay);

    overlay->need_render = FALSE;
  }

  gst_textoverlay_video_chain (overlay->srcpad, GST_DATA (video_frame));
}


static GstElementStateReturn
gst_textoverlay_change_state (GstElement * element)
{
  GstTextOverlay *overlay;

  overlay = GST_TEXTOVERLAY (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_textoverlay_finalize (GObject * object)
{
  GstTextOverlay *overlay = GST_TEXTOVERLAY (object);

  if (overlay->layout) {
    g_object_unref (overlay->layout);
    overlay->layout = NULL;
  }
  if (overlay->bitmap.buffer) {
    g_free (overlay->bitmap.buffer);
    overlay->bitmap.buffer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_textoverlay_init (GstTextOverlay * overlay)
{
  /* video sink */
  overlay->video_sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&video_sink_template_factory), "video_sink");
/*     gst_pad_set_chain_function(overlay->video_sinkpad, gst_textoverlay_video_chain); */
  gst_pad_set_link_function (overlay->video_sinkpad,
      gst_textoverlay_video_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->video_sinkpad);

  /* text sink */
  overlay->text_sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&text_sink_template_factory), "text_sink");
/*     gst_pad_set_link_function(overlay->text_sinkpad, gst_textoverlay_text_sinkconnect); */
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->text_sinkpad);

  /* (video) source */
  overlay->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&textoverlay_src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  overlay->layout =
      pango_layout_new (GST_TEXTOVERLAY_GET_CLASS (overlay)->pango_context);
  memset (&overlay->bitmap, 0, sizeof (overlay->bitmap));

  overlay->halign = GST_TEXT_OVERLAY_HALIGN_CENTER;
  overlay->valign = GST_TEXT_OVERLAY_VALIGN_BASELINE;
  overlay->x0 = overlay->y0 = 0;

  overlay->default_text = g_strdup ("");
  overlay->need_render = TRUE;

  gst_element_set_loop_function (GST_ELEMENT (overlay), gst_textoverlay_loop);
}


static void
gst_textoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTextOverlay *overlay;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEXTOVERLAY (object));
  overlay = GST_TEXTOVERLAY (object);

  switch (prop_id) {

    case ARG_TEXT:
      if (overlay->default_text) {
	g_free (overlay->default_text);
      }
      overlay->default_text = g_strdup (g_value_get_string (value));
      overlay->need_render = TRUE;
      break;

    case ARG_VALIGN:
      if (strcasecmp (g_value_get_string (value), "baseline") == 0)
	overlay->valign = GST_TEXT_OVERLAY_VALIGN_BASELINE;
      else if (strcasecmp (g_value_get_string (value), "bottom") == 0)
	overlay->valign = GST_TEXT_OVERLAY_VALIGN_BOTTOM;
      else if (strcasecmp (g_value_get_string (value), "top") == 0)
	overlay->valign = GST_TEXT_OVERLAY_VALIGN_TOP;
      else
	g_warning ("Invalid 'valign' property value: %s",
	    g_value_get_string (value));
      break;

    case ARG_HALIGN:
      if (strcasecmp (g_value_get_string (value), "left") == 0)
	overlay->halign = GST_TEXT_OVERLAY_HALIGN_LEFT;
      else if (strcasecmp (g_value_get_string (value), "right") == 0)
	overlay->halign = GST_TEXT_OVERLAY_HALIGN_RIGHT;
      else if (strcasecmp (g_value_get_string (value), "center") == 0)
	overlay->halign = GST_TEXT_OVERLAY_HALIGN_CENTER;
      else
	g_warning ("Invalid 'halign' property value: %s",
	    g_value_get_string (value));
      break;

    case ARG_X0:
      overlay->x0 = g_value_get_int (value);
      break;

    case ARG_Y0:
      overlay->y0 = g_value_get_int (value);
      break;

    case ARG_FONT_DESC:
    {
      PangoFontDescription *desc;

      desc = pango_font_description_from_string (g_value_get_string (value));
      if (desc) {
	g_message ("font description set: %s", g_value_get_string (value));
	pango_layout_set_font_description (overlay->layout, desc);
	pango_font_description_free (desc);
	render_text (overlay);
      } else
	g_warning ("font description parse failed: %s",
	    g_value_get_string (value));
      break;
    }

    default:
      break;
  }
}

static void
gst_textoverlay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTextOverlay *overlay;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEXTOVERLAY (object));
  overlay = GST_TEXTOVERLAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "textoverlay", GST_RANK_PRIMARY,
	  GST_TYPE_TEXTOVERLAY))
    return FALSE;

  /*texttestsrc_plugin_init(module, plugin); */
  /*subparse_plugin_init(module, plugin); */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "textoverlay",
    "Text overlay", plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
