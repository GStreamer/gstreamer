/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@entropywave.com>
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
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
#include <cog/cog.h>
#include <cog/cogvirtframe.h>
#include <math.h>
#include <png.h>
#include "gstcogutils.h"

#define GST_TYPE_LOGOINSERT \
  (gst_logoinsert_get_type())
#define GST_LOGOINSERT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LOGOINSERT,GstLogoinsert))
#define GST_LOGOINSERT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LOGOINSERT,GstLogoinsertClass))
#define GST_IS_LOGOINSERT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LOGOINSERT))
#define GST_IS_LOGOINSERT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LOGOINSERT))

typedef struct _GstLogoinsert GstLogoinsert;
typedef struct _GstLogoinsertClass GstLogoinsertClass;

struct _GstLogoinsert
{
  GstBaseTransform base_transform;

  char *location;
  GstBuffer *buffer;

  GstVideoFormat format;
  int width;
  int height;

  gchar *data;
  gsize size;
  CogFrame *overlay_frame;
  CogFrame *argb_frame;
  CogFrame *alpha_frame;

};

struct _GstLogoinsertClass
{
  GstBaseTransformClass parent_class;

};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_DATA
};

GType gst_logoinsert_get_type (void);

GST_DEBUG_CATEGORY_STATIC (gst_logoinsert_debug_category);
#define GST_CAT_DEFAULT gst_logoinsert_debug_category

static void gst_logoinsert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_logoinsert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_logoinsert_dispose (GObject * object);
static void gst_logoinsert_finalize (GObject * object);

static void
gst_logoinsert_set_location (GstLogoinsert * li, const char *location);
static void gst_logoinsert_set_data (GstLogoinsert * li, GstBuffer * buffer);

static gboolean gst_logoinsert_set_caps (GstBaseTransform * base_transform,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_logoinsert_transform_ip (GstBaseTransform *
    base_transform, GstBuffer * buf);
static CogFrame *cog_frame_new_from_png (void *data, int size);
static CogFrame *cog_virt_frame_extract_alpha (CogFrame * frame);
static CogFrame *cog_frame_realize (CogFrame * frame);

static GstStaticPadTemplate gst_logoinsert_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_logoinsert_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_logoinsert_debug_category, "logoinsert", 0, \
      "debug category for logoinsert element");

GST_BOILERPLATE_FULL (GstLogoinsert, gst_logoinsert, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_logoinsert_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_logoinsert_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_logoinsert_sink_template);

  gst_element_class_set_details_simple (element_class,
      "Overlay image onto video", "Filter/Effect/Video",
      "Overlay image onto video", "David Schleef <ds@schleef.org>");
}

static void
gst_logoinsert_class_init (GstLogoinsertClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_logoinsert_set_property;
  gobject_class->get_property = gst_logoinsert_get_property;
  gobject_class->dispose = gst_logoinsert_dispose;
  gobject_class->finalize = gst_logoinsert_finalize;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "location",
          "location of PNG file to overlay", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_DATA,
      gst_param_spec_mini_object ("data", "data",
          "Buffer containing PNG file to overlay", GST_TYPE_BUFFER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  base_transform_class->set_caps = gst_logoinsert_set_caps;
  base_transform_class->transform_ip = gst_logoinsert_transform_ip;
}

static void
gst_logoinsert_init (GstLogoinsert * logoinsert,
    GstLogoinsertClass * logoinsert_class)
{

  GST_DEBUG ("gst_logoinsert_init");
}

static void
gst_logoinsert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLogoinsert *src;

  g_return_if_fail (GST_IS_LOGOINSERT (object));
  src = GST_LOGOINSERT (object);

  GST_DEBUG ("gst_logoinsert_set_property");
  switch (prop_id) {
    case ARG_LOCATION:
      gst_logoinsert_set_location (src, g_value_get_string (value));
      break;
    case ARG_DATA:
      gst_logoinsert_set_data (src,
          (GstBuffer *) gst_value_get_mini_object (value));
      break;
    default:
      break;
  }
}

static void
gst_logoinsert_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstLogoinsert *src;

  g_return_if_fail (GST_IS_LOGOINSERT (object));
  src = GST_LOGOINSERT (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case ARG_DATA:
      gst_value_set_mini_object (value, (GstMiniObject *) src->buffer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_logoinsert_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_LOGOINSERT (object));

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_logoinsert_finalize (GObject * object)
{
  GstLogoinsert *logoinsert;

  g_return_if_fail (GST_IS_LOGOINSERT (object));
  logoinsert = GST_LOGOINSERT (object);

  g_free (logoinsert->location);
  if (logoinsert->buffer) {
    gst_buffer_unref (logoinsert->buffer);
  }
  if (logoinsert->overlay_frame) {
    cog_frame_unref (logoinsert->overlay_frame);
    logoinsert->overlay_frame = NULL;
  }
  if (logoinsert->alpha_frame) {
    cog_frame_unref (logoinsert->alpha_frame);
    logoinsert->alpha_frame = NULL;
  }
  if (logoinsert->argb_frame) {
    cog_frame_unref (logoinsert->argb_frame);
    logoinsert->argb_frame = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_logoinsert_set_caps (GstBaseTransform * base_transform,
    GstCaps * incaps, GstCaps * outcaps)
{
  GstLogoinsert *li;

  g_return_val_if_fail (GST_IS_LOGOINSERT (base_transform), GST_FLOW_ERROR);
  li = GST_LOGOINSERT (base_transform);

  gst_video_format_parse_caps (incaps, &li->format, &li->width, &li->height);

  return TRUE;
}

static GstFlowReturn
gst_logoinsert_transform_ip (GstBaseTransform * base_transform, GstBuffer * buf)
{
  GstLogoinsert *li;
  CogFrame *frame;

  g_return_val_if_fail (GST_IS_LOGOINSERT (base_transform), GST_FLOW_ERROR);
  li = GST_LOGOINSERT (base_transform);

  if (li->argb_frame == NULL)
    return GST_FLOW_OK;

  frame = gst_cog_buffer_wrap (gst_buffer_ref (buf),
      li->format, li->width, li->height);

  if (li->overlay_frame == NULL) {
    CogFrame *f;

    f = cog_virt_frame_extract_alpha (cog_frame_ref (li->argb_frame));
    f = cog_virt_frame_new_subsample (f, frame->format,
        COG_CHROMA_SITE_MPEG2, 2);
    li->alpha_frame = cog_frame_realize (f);

    f = cog_virt_frame_new_unpack (cog_frame_ref (li->argb_frame));
    f = cog_virt_frame_new_color_matrix_RGB_to_YCbCr (f, COG_COLOR_MATRIX_SDTV,
        8);
    f = cog_virt_frame_new_subsample (f, frame->format,
        COG_CHROMA_SITE_MPEG2, 2);
    li->overlay_frame = cog_frame_realize (f);
  }

  if (1) {
    int i, j;
    int k;
    guint8 *dest;
    guint8 *src;
    guint8 *alpha;
    int offset_x, offset_y;

    for (k = 0; k < 3; k++) {
      offset_x = frame->components[k].width -
          li->alpha_frame->components[k].width;
      offset_y = frame->components[k].height -
          li->alpha_frame->components[k].height;

      for (j = 0; j < li->overlay_frame->components[k].height; j++) {
        dest = COG_FRAME_DATA_GET_LINE (frame->components + k, j + offset_y);
        src = COG_FRAME_DATA_GET_LINE (li->overlay_frame->components + k, j);
        alpha = COG_FRAME_DATA_GET_LINE (li->alpha_frame->components + k, j);

#define oil_divide_255(x) ((((x)+128) + (((x)+128)>>8))>>8)

        for (i = 0; i < li->overlay_frame->components[k].width; i++) {
          dest[i + offset_x] =
              oil_divide_255 (src[i] * alpha[i] + dest[i + offset_x] * (255 -
                  alpha[i]));
        }
      }
    }
  }

  cog_frame_unref (frame);

  return GST_FLOW_OK;
}

static GstBuffer *
get_buffer_from_file (const char *filename)
{
  gboolean ret;
  GstBuffer *buffer;
  gsize size;
  gchar *data;

  ret = g_file_get_contents (filename, &data, &size, NULL);
  if (!ret)
    return NULL;

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = (guchar *) data;
  GST_BUFFER_MALLOCDATA (buffer) = (guchar *) data;
  GST_BUFFER_SIZE (buffer) = size;

  return buffer;
}

static void
gst_logoinsert_set_location (GstLogoinsert * li, const char *location)
{
  g_free (li->location);
  li->location = g_strdup (location);

  gst_logoinsert_set_data (li, get_buffer_from_file (li->location));
}

static void
gst_logoinsert_set_data (GstLogoinsert * li, GstBuffer * buffer)
{
  if (li->buffer)
    gst_buffer_unref (li->buffer);

  /* steals the reference */
  li->buffer = buffer;

  if (li->overlay_frame) {
    cog_frame_unref (li->overlay_frame);
    li->overlay_frame = NULL;
  }
  if (li->alpha_frame) {
    cog_frame_unref (li->alpha_frame);
    li->alpha_frame = NULL;
  }
  if (li->argb_frame) {
    cog_frame_unref (li->argb_frame);
    li->argb_frame = NULL;
  }

  if (li->buffer) {
    li->argb_frame = cog_frame_new_from_png (GST_BUFFER_DATA (li->buffer),
        GST_BUFFER_SIZE (li->buffer));
  }
}


/* load PNG into CogFrame */

struct png_data_struct
{
  unsigned char *data;
  int size;
  int offset;
};

static void
read_data (png_structp png_ptr, png_bytep data, png_size_t length)
{
  struct png_data_struct *s = png_get_io_ptr (png_ptr);

  memcpy (data, s->data + s->offset, length);
  s->offset += length;
}

static CogFrame *
cog_frame_new_from_png (void *data, int size)
{
  struct png_data_struct s = { 0 };
  png_structp png_ptr;
  png_infop info_ptr;
  png_bytep *rows;
  CogFrame *frame;
  guchar *frame_data;
  int j;
  int width, height;
  int color_type;

  png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info_ptr = png_create_info_struct (png_ptr);

  s.data = data;
  s.size = size;
  png_set_read_fn (png_ptr, (void *) &s, read_data);

  png_read_info (png_ptr, info_ptr);

  width = png_get_image_width (png_ptr, info_ptr);
  height = png_get_image_height (png_ptr, info_ptr);
  color_type = png_get_color_type (png_ptr, info_ptr);
  GST_DEBUG ("PNG size %dx%d color_type %d", width, height, color_type);

  png_set_strip_16 (png_ptr);
  png_set_packing (png_ptr);
  if (color_type == PNG_COLOR_TYPE_RGB) {
    png_set_filler (png_ptr, 0xff, PNG_FILLER_BEFORE);
  }
  if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
    png_set_swap_alpha (png_ptr);
  }

  frame_data = g_malloc (width * height * 4);
  frame = cog_frame_new_from_data_ARGB (frame_data, width, height);
  frame->regions[0] = frame_data;

  rows = (png_bytep *) g_malloc (sizeof (png_bytep) * height);

  for (j = 0; j < height; j++) {
    rows[j] = COG_FRAME_DATA_GET_LINE (frame->components + 0, j);
  }
  png_read_image (png_ptr, rows);
  g_free (rows);

  png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp) NULL);

  return frame;
}

static void
extract_alpha (CogFrame * frame, void *_dest, int component, int j)
{
  uint8_t *dest = _dest;
  uint8_t *src;
  int i;

  src = COG_FRAME_DATA_GET_LINE (frame->virt_frame1->components + 0, j);
  for (i = 0; i < frame->width; i++) {
    dest[i] = src[i * 4 + 0];
  }
}

static CogFrame *
cog_virt_frame_extract_alpha (CogFrame * frame)
{
  CogFrame *virt_frame;

  /* FIXME check that frame is a real AYUV frame */

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_U8_444,
      frame->width, frame->height);
  virt_frame->virt_frame1 = frame;
  virt_frame->render_line = extract_alpha;

  return virt_frame;
}

static CogFrame *
cog_frame_realize (CogFrame * frame)
{
  CogFrame *dest;

  dest = cog_frame_clone (NULL, frame);
  cog_virt_frame_render (frame, dest);
  cog_frame_unref (frame);

  return dest;
}
