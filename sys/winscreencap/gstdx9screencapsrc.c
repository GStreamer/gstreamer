/* GStreamer
 * Copyright (C) 2007 Haakon Sporsheim <hakon.sporsheim@tandberg.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-dx9screencapsrc
 *
 * This element uses DirectX to capture the desktop or a portion of it.
 * The default is capturing the whole desktop, but #GstDX9ScreenCapSrc:x,
 * #GstDX9ScreenCapSrc:y, #GstDX9ScreenCapSrc:width and
 * #GstDX9ScreenCapSrc:height can be used to select a particular region.
 * Use #GstDX9ScreenCapSrc:monitor for changing which monitor to capture
 * from.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch dx9screencapsrc ! ffmpegcolorspace ! dshowvideosink
 * ]| Capture the desktop and display it.
 * |[
 * gst-launch dx9screencapsrc x=100 y=100 width=320 height=240 !
 * ffmpegcolorspace ! dshowvideosink
 * ]| Capture a portion of the desktop and display it.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdx9screencapsrc.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (dx9screencapsrc_debug);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("BGR"))
    );

#define gst_dx9screencapsrc_parent_class parent_class
G_DEFINE_TYPE (GstDX9ScreenCapSrc, gst_dx9screencapsrc, GST_TYPE_PUSH_SRC);

enum
{
  PROP_0,
  PROP_MONITOR,
  PROP_X_POS,
  PROP_Y_POS,
  PROP_WIDTH,
  PROP_HEIGHT
};

static IDirect3D9 *g_d3d9 = NULL;

/* Fwd. decl. */
static void gst_dx9screencapsrc_dispose (GObject * object);
static void gst_dx9screencapsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dx9screencapsrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstCaps * gst_dx9screencapsrc_fixate (GstBaseSrc * bsrc, GstCaps * caps);
static gboolean gst_dx9screencapsrc_set_caps (GstBaseSrc * bsrc,
    GstCaps * caps);
static GstCaps *gst_dx9screencapsrc_get_caps (GstBaseSrc * bsrc, GstCaps * filter);
static gboolean gst_dx9screencapsrc_start (GstBaseSrc * bsrc);
static gboolean gst_dx9screencapsrc_stop (GstBaseSrc * bsrc);

static void gst_dx9screencapsrc_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_dx9screencapsrc_create (GstPushSrc * src,
    GstBuffer ** buf);

/* Implementation. */
static void
gst_dx9screencapsrc_class_init (GstDX9ScreenCapSrcClass * klass)
{
  GObjectClass *go_class;
  GstElementClass *e_class;
  GstBaseSrcClass *bs_class;
  GstPushSrcClass *ps_class;

  go_class = (GObjectClass *) klass;
  e_class = (GstElementClass *) klass;
  bs_class = (GstBaseSrcClass *) klass;
  ps_class = (GstPushSrcClass *) klass;

  go_class->dispose = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_dispose);
  go_class->set_property = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_set_property);
  go_class->get_property = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_get_property);

  bs_class->get_times = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_get_times);
  bs_class->get_caps = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_get_caps);
  bs_class->set_caps = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_set_caps);
  bs_class->start = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_start);
  bs_class->stop = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_stop);
  bs_class->fixate = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_fixate);

  ps_class->create = GST_DEBUG_FUNCPTR (gst_dx9screencapsrc_create);

  g_object_class_install_property (go_class, PROP_MONITOR,
      g_param_spec_int ("monitor", "Monitor",
          "Which monitor to use (0 = 1st monitor and default)",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (go_class, PROP_X_POS,
      g_param_spec_int ("x", "X",
          "Horizontal coordinate of top left corner for the screen capture "
          "area", 0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (go_class, PROP_Y_POS,
      g_param_spec_int ("y", "Y",
          "Vertical coordinate of top left corner for the screen capture "
          "area", 0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (go_class, PROP_WIDTH,
      g_param_spec_int ("width", "Width",
          "Width of screen capture area (0 = maximum)",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (go_class, PROP_HEIGHT,
      g_param_spec_int ("height", "Height",
          "Height of screen capture area (0 = maximum)",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  gst_element_class_add_pad_template (e_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (e_class,
      "DirectX 9 screen capture source", "Source/Video", "Captures screen",
      "Haakon Sporsheim <hakon.sporsheim@tandberg.com>");

  GST_DEBUG_CATEGORY_INIT (dx9screencapsrc_debug, "dx9screencapsrc", 0,
      "DirectX 9 screen capture source");
}

static void
gst_dx9screencapsrc_init (GstDX9ScreenCapSrc * src)
{
  /* Set src element inital values... */
  src->frames = 0;
  src->surface = NULL;
  src->d3d9_device = NULL;
  src->capture_x = 0;
  src->capture_y = 0;
  src->capture_w = 0;
  src->capture_h = 0;

  src->monitor = 0;

  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);

  if (!g_d3d9)
    g_d3d9 = Direct3DCreate9 (D3D_SDK_VERSION);
  else
    IDirect3D9_AddRef (g_d3d9);
}

static void
gst_dx9screencapsrc_dispose (GObject * object)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (object);

  if (src->surface) {
    GST_ERROR_OBJECT (object,
        "DX9 surface was not freed in _stop, freeing in _dispose!");
    IDirect3DSurface9_Release (src->surface);
    src->surface = NULL;
  }

  if (src->d3d9_device) {
    IDirect3DDevice9_Release (src->d3d9_device);
    src->d3d9_device = NULL;
  }

  if (!IDirect3D9_Release (g_d3d9))
    g_d3d9 = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dx9screencapsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (object);

  switch (prop_id) {
    case PROP_MONITOR:
      if (g_value_get_int (value) >= GetSystemMetrics (SM_CMONITORS)) {
        G_OBJECT_WARN_INVALID_PSPEC (object, "Monitor", prop_id, pspec);
        break;
      }
      src->monitor = g_value_get_int (value);
      break;
    case PROP_X_POS:
      src->capture_x = g_value_get_int (value);
      break;
    case PROP_Y_POS:
      src->capture_y = g_value_get_int (value);
      break;
    case PROP_WIDTH:
      src->capture_w = g_value_get_int (value);
      break;
    case PROP_HEIGHT:
      src->capture_h = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static void
gst_dx9screencapsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (object);

  switch (prop_id) {
    case PROP_MONITOR:
      g_value_set_int (value, src->monitor);
      break;
    case PROP_X_POS:
      g_value_set_int (value, src->capture_x);
      break;
    case PROP_Y_POS:
      g_value_set_int (value, src->capture_y);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, src->capture_w);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, src->capture_h);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static GstCaps *
gst_dx9screencapsrc_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstStructure *structure;

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 640);
  gst_structure_fixate_field_nearest_int (structure, "height", 480);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);

  return caps;
}

static gboolean
gst_dx9screencapsrc_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (bsrc);
  GstStructure *structure;
  const GValue *framerate;

  structure = gst_caps_get_structure (caps, 0);

  src->src_rect = src->screen_rect;
  if (src->capture_w && src->capture_h) {
    src->src_rect.left += src->capture_x;
    src->src_rect.top += src->capture_y;
    src->src_rect.right = src->src_rect.left + src->capture_w;
    src->src_rect.bottom = src->src_rect.top + src->capture_h;
  }

  framerate = gst_structure_get_value (structure, "framerate");
  if (framerate) {
    src->rate_numerator = gst_value_get_fraction_numerator (framerate);
    src->rate_denominator = gst_value_get_fraction_denominator (framerate);
  }

  GST_DEBUG_OBJECT (src, "size %dx%d, %d/%d fps",
      (gint) (src->src_rect.right - src->src_rect.left),
      (gint) (src->src_rect.bottom - src->src_rect.top),
      src->rate_numerator, src->rate_denominator);

  return TRUE;
}

static GstCaps *
gst_dx9screencapsrc_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (bsrc);
  RECT rect_dst;
  GstCaps * caps;

  if (src->monitor >= IDirect3D9_GetAdapterCount (g_d3d9) ||
      FAILED (IDirect3D9_GetAdapterDisplayMode (g_d3d9, src->monitor,
              &src->disp_mode))) {
    return NULL;
  }

  SetRect (&rect_dst, 0, 0, src->disp_mode.Width, src->disp_mode.Height);
  src->screen_rect = rect_dst;

  if (src->capture_w && src->capture_h &&
      src->capture_x + src->capture_w < rect_dst.right - rect_dst.left &&
      src->capture_y + src->capture_h < rect_dst.bottom - rect_dst.top) {
    rect_dst.left = src->capture_x;
    rect_dst.top = src->capture_y;
    rect_dst.right = src->capture_x + src->capture_w;
    rect_dst.bottom = src->capture_y + src->capture_h;
  } else {
    /* Default values */
    src->capture_x = src->capture_y = 0;
    src->capture_w = src->capture_h = 0;
  }

  /* Note:
   *    Expose as xRGB even though the Surface is allocated as ARGB!
   *    This is due to IDirect3DDevice9_GetFrontBufferData which only takes
   *    ARGB surface, but the A channel is in reality never used.
   *    I should add that I had problems specifying ARGB. It might be a bug
   *    in ffmpegcolorspace which I used for testing.
   *    Another interesting thing is that directdrawsink did not support ARGB,
   *    but only xRGB. (On my system, using 32b color depth) And according to
   *    the DirectX documentation ARGB is NOT a valid display buffer format,
   *    but xRGB is.
   */
  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRx",
      "width", G_TYPE_INT, rect_dst.right - rect_dst.left,
      "height", G_TYPE_INT, rect_dst.bottom - rect_dst.top,
      "framerate", GST_TYPE_FRACTION_RANGE, 1, 1, G_MAXINT, 1,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);

  if (filter) {
    GstCaps * tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  return caps;
}

static gboolean
gst_dx9screencapsrc_start (GstBaseSrc * bsrc)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (bsrc);
  D3DPRESENT_PARAMETERS d3dpp;
  HRESULT res;

  ZeroMemory (&d3dpp, sizeof (D3DPRESENT_PARAMETERS));
  d3dpp.Windowed = TRUE;
  d3dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
  d3dpp.BackBufferFormat = src->disp_mode.Format;
  d3dpp.BackBufferHeight = src->disp_mode.Height;
  d3dpp.BackBufferWidth = src->disp_mode.Width;
  d3dpp.BackBufferCount = 1;
  d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
  d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  d3dpp.hDeviceWindow = GetDesktopWindow ();
  d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
  d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

  src->frames = 0;

  res = IDirect3D9_CreateDevice (g_d3d9, src->monitor, D3DDEVTYPE_HAL,
      GetDesktopWindow (), D3DCREATE_SOFTWARE_VERTEXPROCESSING,
      &d3dpp, &src->d3d9_device);
  if (FAILED (res))
    return FALSE;

  return
      SUCCEEDED (IDirect3DDevice9_CreateOffscreenPlainSurface (src->d3d9_device,
          src->disp_mode.Width, src->disp_mode.Height, D3DFMT_A8R8G8B8,
          D3DPOOL_SYSTEMMEM, &src->surface, NULL));
}

static gboolean
gst_dx9screencapsrc_stop (GstBaseSrc * bsrc)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (bsrc);

  if (src->surface) {
    IDirect3DSurface9_Release (src->surface);
    src->surface = NULL;
  }

  return TRUE;
}

static void
gst_dx9screencapsrc_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end)
{
  GstClockTime timestamp;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GstClockTime duration = GST_BUFFER_DURATION (buffer);

    if (GST_CLOCK_TIME_IS_VALID (duration))
      *end = timestamp + duration;
    *start = timestamp;
  }
}

static GstFlowReturn
gst_dx9screencapsrc_create (GstPushSrc * push_src, GstBuffer ** buf)
{
  GstDX9ScreenCapSrc *src = GST_DX9SCREENCAPSRC (push_src);
  GstBuffer *new_buf;
  gint new_buf_size, i;
  gint width, height, stride;
  GstClock *clock;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  GstClockTime buf_time;
  D3DLOCKED_RECT locked_rect;
  LPBYTE p_dst, p_src;
  HRESULT hres;
  GstMapInfo map;

  if (G_UNLIKELY (!src->d3d9_device)) {
    GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before create function"));
    return GST_FLOW_NOT_NEGOTIATED;
  }

  clock = gst_element_get_clock (GST_ELEMENT (src));
  if (clock != NULL) {
    /* Calculate sync time. */
    GstClockTime base_time;
    GstClockTime frame_time =
        gst_util_uint64_scale_int (src->frames * GST_SECOND,
        src->rate_denominator, src->rate_numerator);

    time = gst_clock_get_time (clock);
    base_time = gst_element_get_base_time (GST_ELEMENT (src));
    buf_time = MAX (time - base_time, frame_time);
  } else {
    buf_time = GST_CLOCK_TIME_NONE;
  }

  height = (src->src_rect.bottom - src->src_rect.top);
  width = (src->src_rect.right - src->src_rect.left);
  new_buf_size = width * 4 * height;
  if (G_UNLIKELY (src->rate_numerator == 0 && src->frames == 1)) {
    GST_DEBUG_OBJECT (src, "eos: 0 framerate, frame %d", (gint) src->frames);
    return GST_FLOW_EOS;
  }

  GST_LOG_OBJECT (src,
      "creating buffer of %d bytes with %dx%d image for frame %d",
      new_buf_size, width, height, (gint) src->frames);

  /* Do screen capture and put it into buffer...
   * Aquire front buffer, and lock it
   */
  hres =
      IDirect3DDevice9_GetFrontBufferData (src->d3d9_device, 0, src->surface);
  if (FAILED (hres)) {
    GST_DEBUG_OBJECT (src, "DirectX::GetBackBuffer failed.");
    return GST_FLOW_ERROR;
  }

  hres =
      IDirect3DSurface9_LockRect (src->surface, &locked_rect, &(src->src_rect),
      D3DLOCK_NO_DIRTY_UPDATE | D3DLOCK_NOSYSLOCK | D3DLOCK_READONLY);
  if (FAILED (hres)) {
    GST_DEBUG_OBJECT (src, "DirectX::LockRect failed.");
    return GST_FLOW_ERROR;
  }

  new_buf = gst_buffer_new_and_alloc (new_buf_size);
  gst_buffer_map (new_buf, &map, GST_MAP_WRITE);
  p_dst = (LPBYTE) map.data;
  p_src = (LPBYTE) locked_rect.pBits;
  stride = width * 4;
  for (i = 0; i < height; ++i) {
    memcpy (p_dst, p_src, stride);
    p_dst += stride;
    p_src += locked_rect.Pitch;
  }
  gst_buffer_unmap (new_buf, &map);

  /* Unlock copy of front buffer */
  IDirect3DSurface9_UnlockRect (src->surface);

  GST_BUFFER_TIMESTAMP (new_buf) = buf_time;
  if (src->rate_numerator) {
    GST_BUFFER_DURATION (new_buf) =
        gst_util_uint64_scale_int (GST_SECOND,
        src->rate_denominator, src->rate_numerator);

    if (clock) {
      GST_BUFFER_DURATION (new_buf) = MAX (GST_BUFFER_DURATION (new_buf),
          gst_clock_get_time (clock) - time);
    }
  } else {
    GST_BUFFER_DURATION (new_buf) = GST_CLOCK_TIME_NONE;
  }

  GST_BUFFER_OFFSET (new_buf) = src->frames;
  src->frames++;
  GST_BUFFER_OFFSET_END (new_buf) = src->frames;

  if (clock != NULL)
    gst_object_unref (clock);

  *buf = new_buf;
  return GST_FLOW_OK;
}

