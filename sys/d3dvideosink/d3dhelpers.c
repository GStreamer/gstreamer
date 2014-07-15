/* GStreamer
 * Copyright (C) 2012 Roland Krikava <info@bluedigits.com>
 * Copyright (C) 2010-2011 David Hoyt <dhoyt@hoytsoft.org>
 * Copyright (C) 2010 Andoni Morales <ylatuya@gmail.com>
 * Copyright (C) 2012 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "d3dvideosink.h"
#include "d3dhelpers.h"

#include <stdio.h>

/** FWD DECLS **/

static gboolean d3d_hidden_window_thread (GstD3DVideoSinkClass * klass);
static gboolean d3d_window_wndproc_set (GstD3DVideoSink * sink);
static void d3d_window_wndproc_unset (GstD3DVideoSink * sink);
static gboolean d3d_init_swap_chain (GstD3DVideoSink * sink, HWND hWnd);
static gboolean d3d_release_swap_chain (GstD3DVideoSink * sink);
static gboolean d3d_resize_swap_chain (GstD3DVideoSink * sink);
static gboolean d3d_present_swap_chain (GstD3DVideoSink * sink);
static gboolean d3d_copy_buffer (GstD3DVideoSink * sink,
    GstBuffer * from, GstBuffer * to);
static gboolean d3d_stretch_and_copy (GstD3DVideoSink * sink,
    LPDIRECT3DSURFACE9 back_buffer);
static HWND d3d_create_internal_window (GstD3DVideoSink * sink);

static void d3d_class_notify_device_lost (GstD3DVideoSink * sink);


static LRESULT APIENTRY d3d_wnd_proc_internal (HWND hWnd, UINT message,
    WPARAM wParam, LPARAM lParam);
static LRESULT APIENTRY d3d_wnd_proc (HWND hWnd, UINT message, WPARAM wParam,
    LPARAM lParam);


GST_DEBUG_CATEGORY_EXTERN (gst_d3dvideosink_debug);
#define GST_CAT_DEFAULT gst_d3dvideosink_debug

static gint WM_D3DVIDEO_NOTIFY_DEVICE_LOST = 0;
#define IDT_DEVICE_RESET_TIMER 0

#define WM_QUIT_THREAD  WM_USER+0

/** Helpers **/

#define ERROR_CHECK_HR(hr)                          \
  if(hr != S_OK) {                                  \
    const gchar * str_err=NULL, *t1=NULL;           \
    gchar tmp[128]="";                              \
    switch(hr)
#define CASE_HR_ERR(hr_err)                         \
      case hr_err: str_err = #hr_err; break;
#define CASE_HR_DBG_ERR_END(sink, gst_err_msg, level) \
      default:                                      \
        t1=gst_err_msg;                             \
      sprintf(tmp, "HR-SEV:%u HR-FAC:%u HR-CODE:%u", (guint)HRESULT_SEVERITY(hr), (guint)HRESULT_FACILITY(hr), (guint)HRESULT_CODE(hr)); \
        str_err = tmp;                              \
    } /* end switch */                              \
    GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT, level, sink, "%s HRESULT: %s", t1?t1:"", str_err);
#define CASE_HR_ERR_END(sink, gst_err_msg)          \
  CASE_HR_DBG_ERR_END(sink, gst_err_msg, GST_LEVEL_ERROR)
#define CASE_HR_DBG_END(sink, gst_err_msg)          \
  CASE_HR_DBG_ERR_END(sink, gst_err_msg, GST_LEVEL_DEBUG)

#define CHECK_D3D_DEVICE(klass, sink, goto_label)                       \
  if(!klass->d3d.d3d || !klass->d3d.device.d3d_device) {                \
    GST_ERROR_OBJECT(sink, "Direct3D device or object does not exist"); \
    goto goto_label;                                                    \
  }
#define CHECK_D3D_SWAPCHAIN(sink, goto_label)                       \
  if(!sink->d3d.swapchain) {                                        \
    GST_ERROR_OBJECT(sink, "Direct3D swap chain does not exist");   \
    goto goto_label;                                                \
  }
#define CHECK_D3D_SURFACE(sink, goto_label)                 \
  if(!sink->d3d.surface) {                                  \
    GST_ERROR_OBJECT(sink, "NULL D3D offscreen surface");   \
    goto goto_label;                                        \
  }
#define CHECK_WINDOW_HANDLE(sink, goto_label, is_error)             \
  if(!sink->d3d.window_handle) {                                    \
    GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT,                              \
                      (is_error?GST_LEVEL_ERROR:GST_LEVEL_DEBUG),   \
                      sink, "No window handle is set");             \
    goto goto_label;                                                \
  }

#ifndef D3DFMT_YV12
#define D3DFMT_YV12 MAKEFOURCC ('Y', 'V', '1', '2')
#endif
#ifndef D3DFMT_NV12
#define D3DFMT_NV12 MAKEFOURCC ('N', 'V', '1', '2')
#endif

/** FORMATS **/

#define CASE(x) case x: return #x;
static const gchar *
d3d_format_to_string (D3DFORMAT format)
{
  /* Self defined up above */
  if (format == D3DFMT_YV12)
    return "D3DFMT_YV12";
  else if (format == D3DFMT_NV12)
    return "D3DFMT_NV12";

  switch (format) {
      /* From D3D enum */
      CASE (D3DFMT_UNKNOWN);
      CASE (D3DFMT_X8R8G8B8);
      CASE (D3DFMT_YUY2);
      CASE (D3DFMT_A8R8G8B8);
      CASE (D3DFMT_UYVY);
      CASE (D3DFMT_R8G8B8);
      CASE (D3DFMT_R5G6B5);
      CASE (D3DFMT_X1R5G5B5);
      CASE (D3DFMT_A1R5G5B5);
      CASE (D3DFMT_A4R4G4B4);
      CASE (D3DFMT_R3G3B2);
      CASE (D3DFMT_A8);
      CASE (D3DFMT_A8R3G3B2);
      CASE (D3DFMT_X4R4G4B4);
      CASE (D3DFMT_A2B10G10R10);
      CASE (D3DFMT_A8B8G8R8);
      CASE (D3DFMT_X8B8G8R8);
      CASE (D3DFMT_G16R16);
      CASE (D3DFMT_A2R10G10B10);
      CASE (D3DFMT_A16B16G16R16);
      CASE (D3DFMT_A8P8);
      CASE (D3DFMT_P8);
      CASE (D3DFMT_L8);
      CASE (D3DFMT_A8L8);
      CASE (D3DFMT_A4L4);
      CASE (D3DFMT_V8U8);
      CASE (D3DFMT_L6V5U5);
      CASE (D3DFMT_X8L8V8U8);
      CASE (D3DFMT_Q8W8V8U8);
      CASE (D3DFMT_V16U16);
      CASE (D3DFMT_A2W10V10U10);
      CASE (D3DFMT_DXT1);
      CASE (D3DFMT_DXT2);
      CASE (D3DFMT_DXT3);
      CASE (D3DFMT_DXT4);
      CASE (D3DFMT_DXT5);
      CASE (D3DFMT_MULTI2_ARGB8);
      CASE (D3DFMT_G8R8_G8B8);
      CASE (D3DFMT_R8G8_B8G8);
      CASE (D3DFMT_D16_LOCKABLE);
      CASE (D3DFMT_D32);
      CASE (D3DFMT_D15S1);
      CASE (D3DFMT_D24S8);
      CASE (D3DFMT_D24X8);
      CASE (D3DFMT_D24X4S4);
      CASE (D3DFMT_D16);
      CASE (D3DFMT_L16);
      CASE (D3DFMT_D32F_LOCKABLE);
      CASE (D3DFMT_D24FS8);
      CASE (D3DFMT_VERTEXDATA);
      CASE (D3DFMT_INDEX16);
      CASE (D3DFMT_INDEX32);
      CASE (D3DFMT_Q16W16V16U16);
      CASE (D3DFMT_R16F);
      CASE (D3DFMT_G16R16F);
      CASE (D3DFMT_A16B16G16R16F);
      CASE (D3DFMT_R32F);
      CASE (D3DFMT_G32R32F);
      CASE (D3DFMT_A32B32G32R32F);
      CASE (D3DFMT_CxV8U8);
      CASE (D3DFMT_FORCE_DWORD);
  }

  return "UNKNOWN";
}

#undef CASE

static const struct
{
  GstVideoFormat gst_format;
  D3DFORMAT d3d_format;
} gst_d3d_format_map[] = {
  {
  GST_VIDEO_FORMAT_BGRx, D3DFMT_X8R8G8B8}, {
  GST_VIDEO_FORMAT_RGBx, D3DFMT_X8B8G8R8}, {
  GST_VIDEO_FORMAT_BGRA, D3DFMT_A8R8G8B8}, {
  GST_VIDEO_FORMAT_RGBA, D3DFMT_A8B8G8R8}, {
  GST_VIDEO_FORMAT_BGR, D3DFMT_R8G8B8}, {
  GST_VIDEO_FORMAT_RGB16, D3DFMT_R5G6B5}, {
  GST_VIDEO_FORMAT_RGB15, D3DFMT_X1R5G5B5}, {
  GST_VIDEO_FORMAT_I420, D3DFMT_YV12}, {
  GST_VIDEO_FORMAT_YV12, D3DFMT_YV12}, {
  GST_VIDEO_FORMAT_NV12, D3DFMT_NV12}, {
  GST_VIDEO_FORMAT_YUY2, D3DFMT_YUY2}, {
  GST_VIDEO_FORMAT_UYVY, D3DFMT_UYVY}
};

static D3DFORMAT
gst_video_format_to_d3d_format (GstVideoFormat format)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (gst_d3d_format_map); i++)
    if (gst_d3d_format_map[i].gst_format == format)
      return gst_d3d_format_map[i].d3d_format;
  return D3DFMT_UNKNOWN;
}

static gboolean
gst_video_d3d_format_check (GstD3DVideoSink * sink, D3DFORMAT fmt)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  HRESULT hr;
  gboolean ret = FALSE;

  hr = IDirect3D9_CheckDeviceFormat (klass->d3d.d3d,
      klass->d3d.device.adapter,
      D3DDEVTYPE_HAL, klass->d3d.device.format, 0, D3DRTYPE_SURFACE, fmt);
  if (hr == D3D_OK) {
    /* test whether device can perform color-conversion
     * from that format to target format
     */
    hr = IDirect3D9_CheckDeviceFormatConversion (klass->d3d.d3d,
        klass->d3d.device.adapter,
        D3DDEVTYPE_HAL, fmt, klass->d3d.device.format);
    if (hr == D3D_OK)
      ret = TRUE;
  }
  GST_DEBUG_OBJECT (sink, "Checking: %s - %s", d3d_format_to_string (fmt),
      ret ? "TRUE" : "FALSE");

  return ret;
}

static gboolean
gst_video_query_d3d_format (GstD3DVideoSink * sink, D3DFORMAT d3dformat)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);

  /* If it's the display adapter format we don't need to probe */
  if (d3dformat == klass->d3d.device.format)
    return TRUE;

  if (gst_video_d3d_format_check (sink, d3dformat))
    return TRUE;

  return FALSE;
}

typedef struct
{
  GstVideoFormat fmt;
  D3DFORMAT d3d_fmt;
  gboolean display;
} D3DFormatComp;

static void
d3d_format_comp_free (D3DFormatComp * comp)
{
  g_slice_free (D3DFormatComp, comp);
}

static gint
d3d_format_comp_rate (const D3DFormatComp * comp)
{
  gint points = 0;
  const GstVideoFormatInfo *info;

  info = gst_video_format_get_info (comp->fmt);

  if (comp->display)
    points += 10;
  if (GST_VIDEO_FORMAT_INFO_IS_YUV (info))
    points += 5;
  else if (GST_VIDEO_FORMAT_INFO_IS_RGB (info)) {
    guint i, bit_depth = 0;
    for (i = 0; i < GST_VIDEO_FORMAT_INFO_N_COMPONENTS (info); i++)
      bit_depth += GST_VIDEO_FORMAT_INFO_DEPTH (info, i);
    if (bit_depth >= 24)
      points += 1;
  }

  return points;
}

static gint
d3d_format_comp_compare (gconstpointer a, gconstpointer b)
{
  gint ptsa = 0, ptsb = 0;

  ptsa = d3d_format_comp_rate ((const D3DFormatComp *) a);
  ptsb = d3d_format_comp_rate ((const D3DFormatComp *) b);

  if (ptsa < ptsb)
    return -1;
  else if (ptsa == ptsb)
    return 0;
  else
    return 1;
}

#define GST_D3D_SURFACE_MEMORY_NAME "D3DSurface"

typedef struct
{
  GstMemory mem;

  GstD3DVideoSink *sink;

  GMutex lock;
  gint map_count;

  LPDIRECT3DSURFACE9 surface;
  D3DLOCKED_RECT lr;
  gint x, y, width, height;
} GstD3DSurfaceMemory;

static GstMemory *
gst_d3d_surface_memory_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_assert_not_reached ();
  return NULL;
}

static void
gst_d3d_surface_memory_allocator_free (GstAllocator * allocator,
    GstMemory * mem)
{
  GstD3DSurfaceMemory *dmem = (GstD3DSurfaceMemory *) mem;

  /* If this is a sub-memory, do nothing */
  if (mem->parent)
    return;

  if (dmem->lr.pBits)
    g_warning ("d3dvideosink: Freeing memory that is still mapped");

  IDirect3DSurface9_Release (dmem->surface);
  gst_object_unref (dmem->sink);
  g_mutex_clear (&dmem->lock);
  g_slice_free (GstD3DSurfaceMemory, dmem);
}

static gpointer
gst_d3d_surface_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstD3DSurfaceMemory *parent;
  gpointer ret = NULL;

  /* find the real parent */
  if ((parent = (GstD3DSurfaceMemory *) mem->parent) == NULL)
    parent = (GstD3DSurfaceMemory *) mem;

  g_mutex_lock (&parent->lock);
  if (!parent->map_count
      && IDirect3DSurface9_LockRect (parent->surface, &parent->lr, NULL,
          0) != D3D_OK) {
    ret = NULL;
    goto done;
  }

  ret = parent->lr.pBits;
  parent->map_count++;

done:
  g_mutex_unlock (&parent->lock);

  return ret;
}

static void
gst_d3d_surface_memory_unmap (GstMemory * mem)
{
  GstD3DSurfaceMemory *parent;

  /* find the real parent */
  if ((parent = (GstD3DSurfaceMemory *) mem->parent) == NULL)
    parent = (GstD3DSurfaceMemory *) mem;

  g_mutex_lock (&parent->lock);
  parent->map_count--;
  if (parent->map_count == 0) {
    IDirect3DSurface9_UnlockRect (parent->surface);
    memset (&parent->lr, 0, sizeof (parent->lr));
  }

  g_mutex_unlock (&parent->lock);
}

static GstMemory *
gst_d3d_surface_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  GstD3DSurfaceMemory *sub;
  GstD3DSurfaceMemory *parent;

  /* find the real parent */
  if ((parent = (GstD3DSurfaceMemory *) mem->parent) == NULL)
    parent = (GstD3DSurfaceMemory *) mem;

  if (size == -1)
    size = mem->size - offset;

  sub = g_slice_new0 (GstD3DSurfaceMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->allocator,
      GST_MEMORY_CAST (parent), mem->maxsize, mem->align, mem->offset + offset,
      size);

  return GST_MEMORY_CAST (sub);
}

typedef struct
{
  GstAllocator parent;
} GstD3DSurfaceMemoryAllocator;

typedef struct
{
  GstAllocatorClass parent_class;
} GstD3DSurfaceMemoryAllocatorClass;

GType gst_d3d_surface_memory_allocator_get_type (void);
G_DEFINE_TYPE (GstD3DSurfaceMemoryAllocator, gst_d3d_surface_memory_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_D3D_SURFACE_MEMORY_ALLOCATOR   (gst_d3d_surface_memory_allocator_get_type())
#define GST_IS_D3D_SURFACE_MEMORY_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D_SURFACE_MEMORY_ALLOCATOR))

static void
gst_d3d_surface_memory_allocator_class_init (GstD3DSurfaceMemoryAllocatorClass *
    klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_d3d_surface_memory_allocator_alloc;
  allocator_class->free = gst_d3d_surface_memory_allocator_free;
}

static void
gst_d3d_surface_memory_allocator_init (GstD3DSurfaceMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_D3D_SURFACE_MEMORY_NAME;
  alloc->mem_map = gst_d3d_surface_memory_map;
  alloc->mem_unmap = gst_d3d_surface_memory_unmap;
  alloc->mem_share = gst_d3d_surface_memory_share;
  /* fallback copy */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

G_DEFINE_TYPE (GstD3DSurfaceBufferPool, gst_d3dsurface_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

GstBufferPool *
gst_d3dsurface_buffer_pool_new (GstD3DVideoSink * sink)
{
  GstD3DSurfaceBufferPool *pool;

  pool = g_object_new (GST_TYPE_D3DSURFACE_BUFFER_POOL, NULL);
  pool->sink = gst_object_ref (sink);

  GST_LOG_OBJECT (pool, "new buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_d3dsurface_buffer_pool_finalize (GObject * object)
{
  GstD3DSurfaceBufferPool *pool = GST_D3DSURFACE_BUFFER_POOL_CAST (object);

  GST_LOG_OBJECT (pool, "finalize buffer pool %p", pool);

  gst_object_unref (pool->sink);
  if (pool->allocator)
    gst_object_unref (pool->allocator);

  G_OBJECT_CLASS (gst_d3dsurface_buffer_pool_parent_class)->finalize (object);
}

static const gchar **
gst_d3dsurface_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };

  return options;
}

static gboolean
gst_d3dsurface_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstD3DSurfaceBufferPool *pool = GST_D3DSURFACE_BUFFER_POOL_CAST (bpool);
  GstCaps *caps;
  GstVideoInfo info;

  if (!GST_BUFFER_POOL_CLASS
      (gst_d3dsurface_buffer_pool_parent_class)->set_config (bpool, config)) {
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)
      || !caps) {
    GST_ERROR_OBJECT (pool, "Buffer pool configuration without caps");
    return FALSE;
  }

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (pool, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (gst_video_format_to_d3d_format (GST_VIDEO_INFO_FORMAT (&info)) ==
      D3DFMT_UNKNOWN) {
    GST_ERROR_OBJECT (pool, "Unsupported video format in caps %" GST_PTR_FORMAT,
        caps);
    return FALSE;
  }

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  pool->info = info;

  pool->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (pool->add_metavideo)
    pool->allocator =
        g_object_new (GST_TYPE_D3D_SURFACE_MEMORY_ALLOCATOR, NULL);

  return TRUE;
}

static GstFlowReturn
gst_d3dsurface_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstD3DSurfaceBufferPool *pool = GST_D3DSURFACE_BUFFER_POOL_CAST (bpool);
  GstD3DVideoSink *sink = pool->sink;
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  GstD3DSurfaceMemory *mem;
  LPDIRECT3DSURFACE9 surface;
  D3DFORMAT d3dformat;
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, };
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
  D3DLOCKED_RECT lr;
  HRESULT hr;
  gsize size = 0;

  *buffer = NULL;
  if (!pool->add_metavideo) {
    GST_DEBUG_OBJECT (pool, "No video meta allowed, fallback alloc");
    goto fallback;
  }

  d3dformat =
      gst_video_format_to_d3d_format (GST_VIDEO_INFO_FORMAT (&pool->info));
  hr = IDirect3DDevice9_CreateOffscreenPlainSurface (klass->d3d.
      device.d3d_device, GST_VIDEO_INFO_WIDTH (&pool->info),
      GST_VIDEO_INFO_HEIGHT (&pool->info), d3dformat, D3DPOOL_DEFAULT, &surface,
      NULL);
  if (hr != D3D_OK) {
    GST_ERROR_OBJECT (sink, "Failed to create D3D surface");
    goto fallback;
  }

  IDirect3DSurface9_LockRect (surface, &lr, NULL, 0);
  if (!lr.pBits) {
    GST_ERROR_OBJECT (sink, "Failed to lock D3D surface");
    IDirect3DSurface9_Release (surface);
    goto fallback;
  }

  switch (GST_VIDEO_INFO_FORMAT (&pool->info)) {
    case GST_VIDEO_FORMAT_BGR:
      offset[0] = 0;
      stride[0] = lr.Pitch;
      size = lr.Pitch * GST_VIDEO_INFO_HEIGHT (&pool->info) * 3;
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
      offset[0] = 0;
      stride[0] = lr.Pitch;
      size = lr.Pitch * GST_VIDEO_INFO_HEIGHT (&pool->info) * 4;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_RGB15:
      offset[0] = 0;
      stride[0] = lr.Pitch;
      size = lr.Pitch * GST_VIDEO_INFO_HEIGHT (&pool->info) * 2;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      offset[0] = 0;
      stride[0] = lr.Pitch;
      size = lr.Pitch * GST_VIDEO_INFO_HEIGHT (&pool->info) * 2;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      offset[0] = 0;
      stride[0] = lr.Pitch;
      if (GST_VIDEO_INFO_FORMAT (&pool->info) == GST_VIDEO_FORMAT_YV12) {
        offset[1] =
            offset[0] + stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 0);
        stride[1] = lr.Pitch / 2;
        offset[2] =
            offset[1] + stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 1);
        stride[2] = lr.Pitch / 2;
        size =
            offset[2] + stride[2] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 2);
      } else {
        offset[2] =
            offset[0] + stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 0);
        stride[2] = lr.Pitch / 2;
        offset[1] =
            offset[2] + stride[2] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 2);
        stride[1] = lr.Pitch / 2;
        size =
            offset[1] + stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 1);
      }
      break;
    case GST_VIDEO_FORMAT_NV12:
      offset[0] = 0;
      stride[0] = lr.Pitch;
      offset[1] =
          offset[0] + stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 0);
      stride[1] = lr.Pitch;
      size =
          offset[1] + stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&pool->info, 1);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  IDirect3DSurface9_UnlockRect (surface);

  *buffer = gst_buffer_new ();

  gst_buffer_add_video_meta_full (*buffer, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (&pool->info), GST_VIDEO_INFO_WIDTH (&pool->info),
      GST_VIDEO_INFO_HEIGHT (&pool->info),
      GST_VIDEO_INFO_N_PLANES (&pool->info), offset, stride);

  mem = g_slice_new0 (GstD3DSurfaceMemory);
  gst_memory_init (GST_MEMORY_CAST (mem), 0, pool->allocator, NULL, size, 0, 0,
      size);

  mem->surface = surface;
  mem->sink = gst_object_ref (sink);
  mem->x = mem->y = 0;
  mem->width = GST_VIDEO_INFO_WIDTH (&pool->info);
  mem->height = GST_VIDEO_INFO_HEIGHT (&pool->info);
  g_mutex_init (&mem->lock);

  gst_buffer_append_memory (*buffer, GST_MEMORY_CAST (mem));

  return GST_FLOW_OK;

fallback:
  {
    return
        GST_BUFFER_POOL_CLASS
        (gst_d3dsurface_buffer_pool_parent_class)->alloc_buffer (bpool, buffer,
        params);
  }
}

static void
gst_d3dsurface_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer)
{
  GstMemory *mem = NULL;

  /* Check if something replaced our memory */
  if (gst_buffer_n_memory (buffer) != 1 ||
      (mem = gst_buffer_peek_memory (buffer, 0)) == 0 ||
      !gst_memory_is_type (mem, GST_D3D_SURFACE_MEMORY_NAME)) {
    gst_buffer_unref (buffer);
    return;
  }

  return GST_BUFFER_POOL_CLASS
      (gst_d3dsurface_buffer_pool_parent_class)->release_buffer (bpool, buffer);
}

static void
gst_d3dsurface_buffer_pool_class_init (GstD3DSurfaceBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_d3dsurface_buffer_pool_finalize;

  gstbufferpool_class->get_options = gst_d3dsurface_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_d3dsurface_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_d3dsurface_buffer_pool_alloc_buffer;
  gstbufferpool_class->release_buffer =
      gst_d3dsurface_buffer_pool_release_buffer;
}

static void
gst_d3dsurface_buffer_pool_init (GstD3DSurfaceBufferPool * pool)
{
}

GstCaps *
d3d_supported_caps (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  int i;
  GList *fmts = NULL, *l;
  GstCaps *caps = NULL;
  GstVideoFormat gst_format;
  D3DFORMAT d3d_format;
  GValue va = { 0, };
  GValue v = { 0, };

  LOCK_SINK (sink);

  if (sink->supported_caps) {
    caps = gst_caps_ref (sink->supported_caps);
    goto unlock;
  }

  LOCK_CLASS (sink, klass);
  if (klass->d3d.refs == 0) {
    UNLOCK_CLASS (sink, klass);
    goto unlock;
  }
  UNLOCK_CLASS (sink, klass);

  for (i = 0; i < G_N_ELEMENTS (gst_d3d_format_map); i++) {
    D3DFormatComp *comp;

    gst_format = gst_d3d_format_map[i].gst_format;
    d3d_format = gst_d3d_format_map[i].d3d_format;
    if (!gst_video_query_d3d_format (sink, d3d_format))
      continue;

    comp = g_slice_new0 (D3DFormatComp);
    comp->fmt = (GstVideoFormat) gst_format;
    comp->d3d_fmt = d3d_format;
    comp->display = (d3d_format == klass->d3d.device.format);
    fmts = g_list_insert_sorted (fmts, comp, d3d_format_comp_compare);
  }

  GST_DEBUG_OBJECT (sink, "Supported Caps:");

  g_value_init (&va, GST_TYPE_LIST);
  g_value_init (&v, G_TYPE_STRING);

  for (l = fmts; l; l = g_list_next (l)) {
    D3DFormatComp *comp = (D3DFormatComp *) l->data;

    GST_DEBUG_OBJECT (sink, "%s -> %s %s",
        gst_video_format_to_string (comp->fmt),
        d3d_format_to_string (comp->d3d_fmt), comp->display ? "[display]" : "");
    g_value_set_string (&v, gst_video_format_to_string (comp->fmt));
    gst_value_list_append_value (&va, &v);
  }

  caps = gst_caps_new_simple ("video/x-raw",
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  gst_caps_set_value (caps, "format", &va);
  g_value_unset (&v);
  g_value_unset (&va);
  g_list_free_full (fmts, (GDestroyNotify) d3d_format_comp_free);

  sink->supported_caps = gst_caps_ref (caps);

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *tmp = gst_caps_to_string (caps);
    GST_DEBUG_OBJECT (sink, "Supported caps: %s", tmp);
    g_free (tmp);
  }
#endif

unlock:
  UNLOCK_SINK (sink);

  return caps;
}

gboolean
d3d_set_render_format (GstD3DVideoSink * sink)
{
  D3DFORMAT fmt;
  gboolean ret = FALSE;

  LOCK_SINK (sink);

  fmt = gst_video_format_to_d3d_format (sink->format);
  if (fmt == D3DFMT_UNKNOWN) {
    GST_ERROR_OBJECT (sink, "Unsupported video format %s",
        gst_video_format_to_string (sink->format));
    goto end;
  }

  if (!gst_video_query_d3d_format (sink, fmt)) {
    GST_ERROR_OBJECT (sink, "Failed to query a D3D render format for %s",
        gst_video_format_to_string (sink->format));
    goto end;
  }

  GST_DEBUG_OBJECT (sink, "Selected %s -> %s",
      gst_video_format_to_string (sink->format), d3d_format_to_string (fmt));

  sink->d3d.format = fmt;

  ret = TRUE;

end:
  UNLOCK_SINK (sink);

  return ret;
}

static gboolean
d3d_get_hwnd_window_size (HWND hwnd, gint * width, gint * height)
{
  RECT sz;

  g_return_val_if_fail (width != NULL, FALSE);
  g_return_val_if_fail (height != NULL, FALSE);

  *width = 0;
  *height = 0;

  if (!hwnd)
    return FALSE;

  GetClientRect (hwnd, &sz);

  *width = MAX (1, ABS (sz.right - sz.left));
  *height = MAX (1, ABS (sz.bottom - sz.top));

  return TRUE;
}

static gboolean
d3d_get_render_rects (GstVideoRectangle * rr, RECT * dst, RECT * src)
{
  if (!rr)
    return FALSE;

  /* Rect on target */
  if (dst) {
    dst->left = rr->x;
    dst->top = rr->y;
    dst->right = rr->x + rr->w;
    dst->bottom = rr->y + rr->h;
  }

  /* Rect on source */
  if (src) {
    src->left = 0;
    src->top = 0;
    src->right = rr->w;
    src->bottom = rr->h;
  }

  return TRUE;
}

static gboolean
d3d_get_render_coordinates (GstD3DVideoSink * sink, gint in_x, gint in_y,
    gdouble * out_x, gdouble * out_y)
{
  GstVideoRectangle r_area;
  gdouble tmp;
  gboolean ret = FALSE;

  g_return_val_if_fail (out_x != NULL, FALSE);
  g_return_val_if_fail (out_y != NULL, FALSE);

  LOCK_SINK (sink);
  CHECK_WINDOW_HANDLE (sink, end, FALSE);

  /* Get renderable area of the window */
  if (sink->d3d.render_rect) {
    memcpy (&r_area, sink->d3d.render_rect, sizeof (r_area));
  } else {
    memset (&r_area, 0, sizeof (r_area));
    d3d_get_hwnd_window_size (sink->d3d.window_handle, &r_area.w, &r_area.h);
  }

  /* If window coords outside render area.. return */
  if (in_x < r_area.x || in_x > r_area.x + r_area.w ||
      in_y < r_area.y || in_y > r_area.y + r_area.h)
    goto end;

  /* Convert window coordinates to source frame pixel coordinates */
  if (sink->force_aspect_ratio) {
    GstVideoRectangle tmp = { 0, 0, 0, 0 };
    GstVideoRectangle dst = { 0, 0, 0, 0 };

    tmp.w = GST_VIDEO_SINK_WIDTH (sink);
    tmp.h = GST_VIDEO_SINK_HEIGHT (sink);
    gst_video_sink_center_rect (tmp, r_area, &dst, TRUE);

    r_area.x = r_area.x + dst.x;
    r_area.y = r_area.y + dst.y;
    r_area.w = dst.w;
    r_area.h = dst.h;

    /* If window coords outside render area.. return */
    if (in_x < r_area.x || in_x > (r_area.x + r_area.w) ||
        in_y < r_area.y || in_y > (r_area.y + r_area.h))
      goto end;
  }

  tmp = in_x - r_area.x;
  if (r_area.w == GST_VIDEO_SINK_WIDTH (sink))
    *out_x = tmp;
  else if (r_area.w > GST_VIDEO_SINK_WIDTH (sink))
    *out_x =
        ((gdouble) tmp / ((gdouble) r_area.w /
            (gdouble) GST_VIDEO_SINK_WIDTH (sink)));
  else
    *out_x =
        ((gdouble) GST_VIDEO_SINK_WIDTH (sink) / (gdouble) r_area.w) *
        (gdouble) tmp;

  tmp = in_y - r_area.y;
  if (r_area.h == GST_VIDEO_SINK_HEIGHT (sink))
    *out_y = tmp;
  else if (r_area.h > GST_VIDEO_SINK_HEIGHT (sink))
    *out_y =
        ((gdouble) tmp / ((gdouble) r_area.h /
            (gdouble) GST_VIDEO_SINK_HEIGHT (sink)));
  else
    *out_y =
        ((gdouble) GST_VIDEO_SINK_HEIGHT (sink) / (gdouble) r_area.h) *
        (gdouble) tmp;

  ret = TRUE;
end:
  UNLOCK_SINK (sink);
  return ret;
}

/** Windows for rendering (User Set or Internal) **/

static void
d3d_window_wndproc_unset (GstD3DVideoSink * sink)
{
  WNDPROC cur_wnd_proc = NULL;

  LOCK_SINK (sink);

  GST_DEBUG_OBJECT (sink, " ");

  if (sink->d3d.window_handle == NULL) {
    GST_WARNING_OBJECT (sink, "D3D window_handle is NULL");
    goto end;
  }

  cur_wnd_proc =
      (WNDPROC) GetWindowLongPtr (sink->d3d.window_handle, GWLP_WNDPROC);

  if (cur_wnd_proc != d3d_wnd_proc) {
    GST_WARNING_OBJECT (sink, "D3D window proc is not set on current window");
    goto end;
  }

  if (sink->d3d.orig_wnd_proc == NULL) {
    GST_WARNING_OBJECT (sink, "D3D orig window proc is NULL, can not restore");
    goto end;
  }

  /* Restore orignal WndProc for window_handle */
  if (!SetWindowLongPtr (sink->d3d.window_handle, GWLP_WNDPROC,
          (LONG_PTR) sink->d3d.orig_wnd_proc)) {
    GST_WARNING_OBJECT (sink, "D3D failed to set original WndProc");
    goto end;
  }

end:
  sink->d3d.orig_wnd_proc = NULL;
  sink->d3d.window_handle = NULL;

  UNLOCK_SINK (sink);
}

static gboolean
d3d_window_wndproc_set (GstD3DVideoSink * sink)
{
  WNDPROC cur_wnd_proc;
  gboolean ret = FALSE;

  LOCK_SINK (sink);

  cur_wnd_proc =
      (WNDPROC) GetWindowLongPtr (sink->d3d.window_handle, GWLP_WNDPROC);

  if (cur_wnd_proc != NULL && cur_wnd_proc == d3d_wnd_proc) {
    GST_DEBUG_OBJECT (sink,
        "D3D window proc func is already set on the current window");
    ret = TRUE;
    goto end;
  }

  /* Store the original window proc function */
  sink->d3d.orig_wnd_proc =
      (WNDPROC) SetWindowLongPtr (sink->d3d.window_handle, GWLP_WNDPROC,
      (LONG_PTR) d3d_wnd_proc);

  /* Note: If the window belongs to another process this will fail */
  if (sink->d3d.orig_wnd_proc == NULL) {
    GST_ERROR_OBJECT (sink,
        "Failed to set WndProc function on window. Error: %d",
        (gint) GetLastError ());
    goto end;
  }

  /* Make sink accessible to d3d_wnd_proc */
  SetProp (sink->d3d.window_handle, TEXT ("GstD3DVideoSink"), sink);

  ret = TRUE;

end:
  UNLOCK_SINK (sink);
  return ret;
}

static void
d3d_prepare_render_window (GstD3DVideoSink * sink)
{
  LOCK_SINK (sink);

  if (sink->d3d.window_handle == NULL) {
    GST_DEBUG_OBJECT (sink, "No window handle has been set..");
    goto end;
  }

  if (d3d_init_swap_chain (sink, sink->d3d.window_handle)) {
    d3d_window_wndproc_set (sink);
    sink->d3d.renderable = TRUE;
    GST_DEBUG_OBJECT (sink, "Prepared window for render [HWND:%p]",
        sink->d3d.window_handle);
  } else {
    GST_ERROR_OBJECT (sink, "Failed preparing window for render [HWND:%p]",
        sink->d3d.window_handle);
  }

end:
  UNLOCK_SINK (sink);

}

void
d3d_set_window_handle (GstD3DVideoSink * sink, guintptr window_id,
    gboolean is_internal)
{
  LOCK_SINK (sink);

  if (sink->d3d.window_handle == (HWND) window_id) {
    GST_WARNING_OBJECT (sink, "Window HWND already set to: %" G_GUINTPTR_FORMAT,
        window_id);
    goto end;
  }

  /* Unset current window  */
  if (sink->d3d.window_handle != NULL) {
    PostMessage (sink->d3d.window_handle, WM_QUIT_THREAD, 0, 0);
    GST_DEBUG_OBJECT (sink, "Unsetting window [HWND:%p]",
        sink->d3d.window_handle);
    d3d_window_wndproc_unset (sink);
    d3d_release_swap_chain (sink);
    sink->d3d.window_handle = NULL;
    sink->d3d.window_is_internal = FALSE;
    sink->d3d.renderable = FALSE;
  }

  /* Set new one */
  if (window_id) {
    sink->d3d.window_handle = (HWND) window_id;
    sink->d3d.window_is_internal = is_internal;
    if (!is_internal)
      sink->d3d.external_window_handle = sink->d3d.window_handle;
    /* If caps have been set.. prepare window */
    if (sink->format != 0)
      d3d_prepare_render_window (sink);
  }

end:
  UNLOCK_SINK (sink);
}

void
d3d_set_render_rectangle (GstD3DVideoSink * sink)
{
  LOCK_SINK (sink);
  /* Setting the pointer lets us know render rect is set */
  sink->d3d.render_rect = &sink->render_rect;
  d3d_resize_swap_chain (sink);
  d3d_present_swap_chain (sink);
  UNLOCK_SINK (sink);
}

void
d3d_expose_window (GstD3DVideoSink * sink)
{
  GST_DEBUG_OBJECT (sink, "EXPOSE");
  d3d_present_swap_chain (sink);
}

gboolean
d3d_prepare_window (GstD3DVideoSink * sink)
{
  HWND hWnd;
  gboolean ret = FALSE;

  LOCK_SINK (sink);

  /* if we already had an external window, then use it again */
  if (sink->d3d.external_window_handle)
    sink->d3d.window_handle = sink->d3d.external_window_handle;

  /* Give the app a last chance to set a window id */
  if (!sink->d3d.window_handle)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (sink));

  /* If the user did not set a window id .. check if we should create one */
  if (!sink->d3d.window_handle) {
    if (sink->create_internal_window) {
      if ((hWnd = d3d_create_internal_window (sink))) {
        GST_DEBUG_OBJECT (sink,
            "No window id was set.. creating internal window");
        d3d_set_window_handle (sink, (guintptr) hWnd, TRUE);
      } else {
        GST_ERROR_OBJECT (sink, "Failed to create internal window");
        goto end;
      }
    } else {
      GST_DEBUG_OBJECT (sink, "No window id is set..");
      goto end;
    }
  } else {
    d3d_prepare_render_window (sink);
  }

  ret = TRUE;

end:
  UNLOCK_SINK (sink);

  return ret;
}

gboolean
d3d_stop (GstD3DVideoSink * sink)
{
  if (sink->pool)
    gst_buffer_pool_set_active (sink->pool, FALSE);
  if (sink->fallback_pool)
    gst_buffer_pool_set_active (sink->fallback_pool, FALSE);
  gst_object_replace ((GstObject **) & sink->pool, NULL);
  gst_object_replace ((GstObject **) & sink->fallback_pool, NULL);
  gst_buffer_replace (&sink->fallback_buffer, NULL);

  /* Release D3D resources */
  d3d_set_window_handle (sink, 0, FALSE);
  return TRUE;
}

/** D3D Lost and Reset Device **/

static void
d3d_notify_device_lost (GstD3DVideoSink * sink)
{
  gboolean notify = FALSE;

  g_return_if_fail (GST_IS_D3DVIDEOSINK (sink));

  LOCK_SINK (sink);

  if (!sink->d3d.device_lost) {
    GST_WARNING_OBJECT (sink,
        "D3D Device has been lost. Cleanup up resources..");

    /* Stream will continue with GST_FLOW_OK, until device has been reset */
    sink->d3d.device_lost = TRUE;

    /* First we clean up all resources in this d3dvideo instance */
    d3d_release_swap_chain (sink);

    /* Notify our hidden thread */
    notify = TRUE;
  }

  UNLOCK_SINK (sink);

  if (notify)
    d3d_class_notify_device_lost (sink);
}

static void
d3d_notify_device_reset (GstD3DVideoSink * sink)
{
  LOCK_SINK (sink);

  if (sink->d3d.device_lost) {
    GST_DEBUG_OBJECT (sink,
        "D3D Device has been reset. Re-init swap chain if still streaming");
    /* If we're still streaming.. reset swap chain */
    if (sink->d3d.window_handle != NULL)
      d3d_init_swap_chain (sink, sink->d3d.window_handle);
    sink->d3d.device_lost = FALSE;
  }

  UNLOCK_SINK (sink);
}

/** Swap Chains **/

static gboolean
d3d_init_swap_chain (GstD3DVideoSink * sink, HWND hWnd)
{
  D3DPRESENT_PARAMETERS present_params;
  LPDIRECT3DSWAPCHAIN9 d3d_swapchain = NULL;
  D3DTEXTUREFILTERTYPE d3d_filtertype;
  HRESULT hr;
  GstD3DVideoSinkClass *klass;
  gboolean ret = FALSE;

  g_return_val_if_fail (sink != NULL, FALSE);
  klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  g_return_val_if_fail (klass != NULL, FALSE);

  LOCK_SINK (sink);
  LOCK_CLASS (sink, klass);

  /* We need a display device */
  CHECK_D3D_DEVICE (klass, sink, error);

  GST_DEBUG ("Initializing Direct3D swap chain");

  GST_DEBUG ("Direct3D back buffer size: %dx%d", GST_VIDEO_SINK_WIDTH (sink),
      GST_VIDEO_SINK_HEIGHT (sink));

  /* When windowed, width and height determined by HWND */
  ZeroMemory (&present_params, sizeof (present_params));
  present_params.Windowed = TRUE;
  present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;    /* D3DSWAPEFFECT_COPY */
  present_params.hDeviceWindow = hWnd;
  present_params.BackBufferFormat = klass->d3d.device.format;

  hr = IDirect3DDevice9_CreateAdditionalSwapChain (klass->d3d.device.d3d_device,
      &present_params, &d3d_swapchain);
  ERROR_CHECK_HR (hr) {
    CASE_HR_ERR (D3DERR_NOTAVAILABLE);
    CASE_HR_ERR (D3DERR_DEVICELOST);
    CASE_HR_ERR (D3DERR_INVALIDCALL);
    CASE_HR_ERR (D3DERR_OUTOFVIDEOMEMORY);
    CASE_HR_ERR (E_OUTOFMEMORY);
    CASE_HR_ERR_END (sink, "Error creating D3D swapchian");
    goto error;
  }

  /* Determine texture filtering support. If it's supported for this format,
   * use the filter type determined when we created the dev and checked the 
   * dev caps.
   */
  hr = IDirect3D9_CheckDeviceFormat (klass->d3d.d3d,
      klass->d3d.device.adapter,
      D3DDEVTYPE_HAL,
      klass->d3d.device.format,
      D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, sink->d3d.format);
  if (hr == D3D_OK)
    d3d_filtertype = klass->d3d.device.filter_type;
  else
    d3d_filtertype = D3DTEXF_NONE;

  GST_DEBUG ("Direct3D stretch rect texture filter: %d", d3d_filtertype);

  sink->d3d.filtertype = d3d_filtertype;
  sink->d3d.swapchain = d3d_swapchain;

  ret = TRUE;

error:
  if (!ret) {
    if (d3d_swapchain)
      IDirect3DSwapChain9_Release (d3d_swapchain);
  }

  UNLOCK_CLASS (sink, klass);
  UNLOCK_SINK (sink);

  return ret;
}

static gboolean
d3d_release_swap_chain (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  int ref_count;
  gboolean ret = FALSE;

  LOCK_SINK (sink);

  GST_DEBUG_OBJECT (sink, "Releasing Direct3D swap chain");

  CHECK_D3D_DEVICE (klass, sink, end);

  if (!sink->d3d.swapchain) {
    ret = TRUE;
    goto end;
  }

  if (sink->d3d.swapchain) {
    ref_count = IDirect3DSwapChain9_Release (sink->d3d.swapchain);
    sink->d3d.swapchain = NULL;
    GST_DEBUG_OBJECT (sink, "D3D swapchain released. Ref count: %d", ref_count);
  }

  if (sink->d3d.surface) {
    ref_count = IDirect3DSurface9_Release (sink->d3d.surface);
    sink->d3d.surface = NULL;
    GST_DEBUG_OBJECT (sink, "D3D surface released. Ref count: %d", ref_count);
  }

  ret = TRUE;

end:
  UNLOCK_SINK (sink);

  return ret;
}

static gboolean
d3d_resize_swap_chain (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass;
  D3DPRESENT_PARAMETERS d3d_pp;
  LPDIRECT3DSWAPCHAIN9 swapchain = NULL;
  gint w = 0, h = 0, ref_count = 0;
  gboolean ret = FALSE;
  HRESULT hr;
  gboolean need_new = FALSE;

  g_return_val_if_fail (sink != NULL, FALSE);
  klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  g_return_val_if_fail (klass != NULL, FALSE);

  LOCK_SINK (sink);

  if (!sink->d3d.renderable || sink->d3d.device_lost) {
    UNLOCK_SINK (sink);
    return FALSE;
  }

  LOCK_CLASS (sink, klass);

  CHECK_WINDOW_HANDLE (sink, end, FALSE);
  CHECK_D3D_DEVICE (klass, sink, end);
  CHECK_D3D_SWAPCHAIN (sink, end);

  d3d_get_hwnd_window_size (sink->d3d.window_handle, &w, &h);
  ZeroMemory (&d3d_pp, sizeof (d3d_pp));

  /* Get the parameters used to create this swap chain */
  hr = IDirect3DSwapChain9_GetPresentParameters (sink->d3d.swapchain, &d3d_pp);
  if (hr != D3D_OK) {
    GST_ERROR_OBJECT (sink,
        "Unable to determine Direct3D present parameters for swap chain");
    goto end;
  }

  /* Reisze needed? */
  if (d3d_pp.BackBufferWidth != w || d3d_pp.BackBufferHeight != h)
    need_new = TRUE;
#if 0
  /* Render rect set or unset? */
  if ((d3d_pp.SwapEffect != D3DSWAPEFFECT_COPY && sink->d3d.render_rect) ||
      (d3d_pp.SwapEffect != D3DSWAPEFFECT_DISCARD
          && sink->d3d.render_rect == NULL)) {
    d3d_pp.SwapEffect =
        (sink->d3d.render_rect ==
        NULL) ? D3DSWAPEFFECT_DISCARD : D3DSWAPEFFECT_COPY;
    GST_DEBUG_OBJECT (sink, "Setting SwapEffect: %s",
        sink->d3d.render_rect ? "COPY" : "DISCARD");
    need_new = TRUE;
  }
#endif
  if (!need_new) {
    ret = TRUE;
    goto end;
  }

  GST_DEBUG_OBJECT (sink, "Resizing swapchain %dx%d to %dx%d",
      d3d_pp.BackBufferWidth, d3d_pp.BackBufferHeight, w, h);


  /* As long as present params windowed == TRUE, width or height
   * of 0 will force use of HWND's size.
   */
  d3d_pp.BackBufferWidth = 0;
  d3d_pp.BackBufferHeight = 0;

  /* Release current swapchain */
  if (sink->d3d.swapchain != NULL) {
    ref_count = IDirect3DSwapChain9_Release (sink->d3d.swapchain);
    if (ref_count > 0) {
      GST_WARNING_OBJECT (sink, "Release swapchain refcount: %d", ref_count);
    }
    sink->d3d.swapchain = NULL;
  }

  hr = IDirect3DDevice9_CreateAdditionalSwapChain (klass->d3d.device.d3d_device,
      &d3d_pp, &swapchain);
  ERROR_CHECK_HR (hr) {
    CASE_HR_ERR (D3DERR_NOTAVAILABLE);
    CASE_HR_ERR (D3DERR_DEVICELOST);
    CASE_HR_ERR (D3DERR_INVALIDCALL);
    CASE_HR_ERR (D3DERR_OUTOFVIDEOMEMORY);
    CASE_HR_ERR (E_OUTOFMEMORY);
    CASE_HR_ERR_END (sink, "Error creating swapchian");
    goto end;
  }

  sink->d3d.swapchain = swapchain;
  ret = TRUE;

end:
  UNLOCK_CLASS (sink, klass);
  UNLOCK_SINK (sink);

  return ret;
}

static gboolean
d3d_copy_buffer (GstD3DVideoSink * sink, GstBuffer * from, GstBuffer * to)
{
  gboolean ret = FALSE;
  GstVideoFrame from_frame, to_frame;

  memset (&from_frame, 0, sizeof (from_frame));
  memset (&to_frame, 0, sizeof (to_frame));

  LOCK_SINK (sink);

  if (!sink->d3d.renderable || sink->d3d.device_lost)
    goto end;

  if (!gst_video_frame_map (&from_frame, &sink->info, from, GST_MAP_READ) ||
      !gst_video_frame_map (&to_frame, &sink->info, to, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (sink, "NULL GstBuffer");
    goto end;
  }

  switch (sink->format) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:{
      const guint8 *src;
      guint8 *dst;
      gint dststride, srcstride;
      gint i, h, w;

      src = GST_VIDEO_FRAME_PLANE_DATA (&from_frame, 0);
      dst = GST_VIDEO_FRAME_PLANE_DATA (&to_frame, 0);
      srcstride = GST_VIDEO_FRAME_PLANE_STRIDE (&from_frame, 0);
      dststride = GST_VIDEO_FRAME_PLANE_STRIDE (&to_frame, 0);
      h = GST_VIDEO_FRAME_HEIGHT (&from_frame);
      w = GST_ROUND_UP_4 (GST_VIDEO_FRAME_WIDTH (&from_frame) * 2);

      for (i = 0; i < h; i++) {
        memcpy (dst, src, w);
        dst += dststride;
        src += srcstride;
      }

      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:{
      const guint8 *src;
      guint8 *dst;
      gint srcstride, dststride;
      gint i, j, h_, w_;

      for (i = 0; i < 3; i++) {
        src = GST_VIDEO_FRAME_COMP_DATA (&from_frame, i);
        dst = GST_VIDEO_FRAME_COMP_DATA (&to_frame, i);
        srcstride = GST_VIDEO_FRAME_COMP_STRIDE (&from_frame, i);
        dststride = GST_VIDEO_FRAME_COMP_STRIDE (&to_frame, i);
        h_ = GST_VIDEO_FRAME_COMP_HEIGHT (&from_frame, i);
        w_ = GST_VIDEO_FRAME_COMP_WIDTH (&from_frame, i);

        for (j = 0; j < h_; j++) {
          memcpy (dst, src, w_);
          dst += dststride;
          src += srcstride;
        }
      }

      break;
    }
    case GST_VIDEO_FORMAT_NV12:{
      const guint8 *src;
      guint8 *dst;
      gint srcstride, dststride;
      gint i, j, h_, w_;

      for (i = 0; i < 2; i++) {
        src = GST_VIDEO_FRAME_PLANE_DATA (&from_frame, i);
        dst = GST_VIDEO_FRAME_PLANE_DATA (&to_frame, i);
        srcstride = GST_VIDEO_FRAME_PLANE_STRIDE (&from_frame, i);
        dststride = GST_VIDEO_FRAME_PLANE_STRIDE (&to_frame, i);
        h_ = GST_VIDEO_FRAME_COMP_HEIGHT (&from_frame, i);
        w_ = GST_VIDEO_FRAME_COMP_WIDTH (&from_frame, i);

        for (j = 0; j < h_; j++) {
          memcpy (dst, src, w_ * 2);
          dst += dststride;
          src += srcstride;
        }
      }

      break;
    }
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGBx:{
      const guint8 *src;
      guint8 *dst;
      gint srcstride, dststride;
      gint i, h, w;

      src = GST_VIDEO_FRAME_PLANE_DATA (&from_frame, 0);
      dst = GST_VIDEO_FRAME_PLANE_DATA (&to_frame, 0);
      srcstride = GST_VIDEO_FRAME_PLANE_STRIDE (&from_frame, 0);
      dststride = GST_VIDEO_FRAME_PLANE_STRIDE (&to_frame, 0);
      h = GST_VIDEO_FRAME_HEIGHT (&from_frame);
      w = GST_VIDEO_FRAME_WIDTH (&from_frame) * 4;

      for (i = 0; i < h; i++) {
        memcpy (dst, src, w);
        dst += dststride;
        src += srcstride;
      }

      break;
    }
    case GST_VIDEO_FORMAT_BGR:{
      const guint8 *src;
      guint8 *dst;
      gint srcstride, dststride;
      gint i, h, w;

      src = GST_VIDEO_FRAME_PLANE_DATA (&from_frame, 0);
      dst = GST_VIDEO_FRAME_PLANE_DATA (&to_frame, 0);
      srcstride = GST_VIDEO_FRAME_PLANE_STRIDE (&from_frame, 0);
      dststride = GST_VIDEO_FRAME_PLANE_STRIDE (&to_frame, 0);
      h = GST_VIDEO_FRAME_HEIGHT (&from_frame);
      w = GST_VIDEO_FRAME_WIDTH (&from_frame) * 3;

      for (i = 0; i < h; i++) {
        memcpy (dst, src, w);
        dst += dststride;
        src += srcstride;
      }

      break;
    }
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_RGB15:{
      const guint8 *src;
      guint8 *dst;
      gint srcstride, dststride;
      gint i, h, w;

      src = GST_VIDEO_FRAME_PLANE_DATA (&from_frame, 0);
      dst = GST_VIDEO_FRAME_PLANE_DATA (&to_frame, 0);
      srcstride = GST_VIDEO_FRAME_PLANE_STRIDE (&from_frame, 0);
      dststride = GST_VIDEO_FRAME_PLANE_STRIDE (&to_frame, 0);
      h = GST_VIDEO_FRAME_HEIGHT (&from_frame);
      w = GST_VIDEO_FRAME_WIDTH (&from_frame) * 2;

      for (i = 0; i < h; i++) {
        memcpy (dst, src, w);
        dst += dststride;
        src += srcstride;
      }

      break;
    }
    default:
      goto unhandled_format;
  }

  ret = TRUE;

end:
  if (from_frame.buffer)
    gst_video_frame_unmap (&from_frame);
  if (to_frame.buffer)
    gst_video_frame_unmap (&to_frame);

  UNLOCK_SINK (sink);
  return ret;

unhandled_format:
  GST_ERROR_OBJECT (sink,
      "Unhandled format '%s' -> '%s' (should not get here)",
      gst_video_format_to_string (sink->format),
      d3d_format_to_string (sink->d3d.format));
  ret = FALSE;
  goto end;
}

static gboolean
d3d_present_swap_chain (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  LPDIRECT3DSURFACE9 back_buffer = NULL;
  gboolean ret = FALSE;
  HRESULT hr;
  RECT dstr, srcr, *pDestRect = NULL, *pSrcRect = NULL;

  LOCK_SINK (sink);

  if (!sink->d3d.renderable || sink->d3d.device_lost) {
    UNLOCK_SINK (sink);
    return FALSE;
  }

  LOCK_CLASS (sink, klass);

  CHECK_WINDOW_HANDLE (sink, end, FALSE);
  CHECK_D3D_DEVICE (klass, sink, end);
  CHECK_D3D_SWAPCHAIN (sink, end);

  /* Set the render target to our swap chain */
  IDirect3DSwapChain9_GetBackBuffer (sink->d3d.swapchain, 0,
      D3DBACKBUFFER_TYPE_MONO, &back_buffer);
  IDirect3DDevice9_SetRenderTarget (klass->d3d.device.d3d_device, 0,
      back_buffer);
  IDirect3DSurface9_Release (back_buffer);

  /* Clear the target */
  IDirect3DDevice9_Clear (klass->d3d.device.d3d_device, 0, NULL,
      D3DCLEAR_TARGET, D3DCOLOR_XRGB (0, 0, 0), 1.0f, 0);

  hr = IDirect3DDevice9_BeginScene (klass->d3d.device.d3d_device);
  ERROR_CHECK_HR (hr) {
    CASE_HR_ERR (D3DERR_INVALIDCALL);
    CASE_HR_ERR_END (sink, "IDirect3DDevice9_BeginScene");
    goto end;
  }

  /* Stretch and blit ops, to copy offscreen surface buffer
   * to Display back buffer.
   */
  d3d_stretch_and_copy (sink, back_buffer);
  IDirect3DDevice9_EndScene (klass->d3d.device.d3d_device);

  if (d3d_get_render_rects (sink->d3d.render_rect, &dstr, &srcr)) {
    pDestRect = &dstr;
    pSrcRect = &srcr;
  }

  /*
   * Swap back and front buffers on video card and present to the user
   */
  hr = IDirect3DSwapChain9_Present (sink->d3d.swapchain, pSrcRect, pDestRect,
      NULL, NULL, 0);
  if (hr == D3DERR_DEVICELOST) {
    d3d_notify_device_lost (sink);
    ret = TRUE;
    goto end;
  }
  ERROR_CHECK_HR (hr) {
    CASE_HR_ERR (D3DERR_DEVICELOST);
    CASE_HR_ERR (D3DERR_DRIVERINTERNALERROR);
    CASE_HR_ERR (D3DERR_INVALIDCALL);
    CASE_HR_ERR (D3DERR_OUTOFVIDEOMEMORY);
    CASE_HR_ERR (E_OUTOFMEMORY);
    CASE_HR_DBG_END (sink, "IDirect3DSwapChain9_Present failure");
    goto end;
  }

  ret = TRUE;

end:
  UNLOCK_SINK (sink);
  UNLOCK_CLASS (sink, klass);
  return ret;
}

static gboolean
d3d_stretch_and_copy (GstD3DVideoSink * sink, LPDIRECT3DSURFACE9 back_buffer)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  GstVideoRectangle *render_rect = NULL;
  RECT r, s;
  RECT *r_p = NULL;
  HRESULT hr;
  gboolean ret = FALSE;

  LOCK_SINK (sink);

  CHECK_WINDOW_HANDLE (sink, end, FALSE);
  CHECK_D3D_DEVICE (klass, sink, end);
  CHECK_D3D_SURFACE (sink, end);

  render_rect = sink->d3d.render_rect;

  if (sink->force_aspect_ratio) {
    gint window_width;
    gint window_height;
    GstVideoRectangle src;
    GstVideoRectangle dst;
    GstVideoRectangle result;

    memset (&dst, 0, sizeof (dst));
    memset (&src, 0, sizeof (src));

    /* Set via GstXOverlay set_render_rect */
    if (render_rect) {
      memcpy (&dst, render_rect, sizeof (dst));
    } else {
      d3d_get_hwnd_window_size (sink->d3d.window_handle, &window_width,
          &window_height);
      dst.w = window_width;
      dst.h = window_height;
    }

    src.w = GST_VIDEO_SINK_WIDTH (sink);
    src.h = GST_VIDEO_SINK_HEIGHT (sink);

    gst_video_sink_center_rect (src, dst, &result, TRUE);

    r.left = result.x;
    r.top = result.y;
    r.right = result.x + result.w;
    r.bottom = result.y + result.h;
    r_p = &r;
  } else if (render_rect) {
    r.left = 0;
    r.top = 0;
    r.right = render_rect->w;
    r.bottom = render_rect->h;
    r_p = &r;
  }

  s.left = sink->crop_rect.x;
  s.top = sink->crop_rect.y;
  s.right = sink->crop_rect.x + sink->crop_rect.w;
  s.bottom = sink->crop_rect.y + sink->crop_rect.h;

  /* TODO: StretchRect returns error if the dest rect is outside
   * the backbuffer area. So we need to calc how much of the src
   * surface is being scaled / copied to the render rect..
   */

  hr = IDirect3DDevice9_StretchRect (klass->d3d.device.d3d_device, sink->d3d.surface,   /* Source Surface */
      &s,                       /* Source Surface Rect (NULL: Whole) */
      back_buffer,              /* Dest Surface */
      r_p,                      /* Dest Surface Rect (NULL: Whole) */
      klass->d3d.device.filter_type);

  if (hr == D3D_OK) {
    ret = TRUE;
  } else {
    GST_ERROR_OBJECT (sink, "Failure calling Direct3DDevice9_StretchRect");
  }

end:
  UNLOCK_SINK (sink);

  return ret;
}

GstFlowReturn
d3d_render_buffer (GstD3DVideoSink * sink, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory *mem;
  LPDIRECT3DSURFACE9 surface = NULL;
  GstVideoCropMeta *crop = NULL;

  LOCK_SINK (sink);

  if (!sink->d3d.window_handle) {
    if (sink->stream_stop_on_close) {
      /* Handle window deletion by posting an error on the bus */
      GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
          ("Output window was closed"), (NULL));
      ret = GST_FLOW_ERROR;
    }
    goto end;
  }

  if (sink->d3d.device_lost) {
    GST_LOG_OBJECT (sink, "Device lost, waiting for reset..");
    goto end;
  }

  GST_INFO_OBJECT (sink, "%s %" GST_TIME_FORMAT,
      (sink->d3d.window_handle != NULL) ? "Render" : "No Win",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  crop = gst_buffer_get_video_crop_meta (buf);
  if (crop) {
    sink->crop_rect.x = crop->x;
    sink->crop_rect.y = crop->y;
    sink->crop_rect.w = crop->width;
    sink->crop_rect.h = crop->height;
  } else {
    sink->crop_rect.x = 0;
    sink->crop_rect.y = 0;
    sink->crop_rect.w = sink->info.width;
    sink->crop_rect.h = sink->info.height;
  }

  /* Resize swapchain if needed */
  if (!d3d_resize_swap_chain (sink)) {
    ret = GST_FLOW_ERROR;
    goto end;
  }

  if (gst_buffer_n_memory (buf) != 1 ||
      (mem = gst_buffer_peek_memory (buf, 0)) == 0 ||
      !gst_memory_is_type (mem, GST_D3D_SURFACE_MEMORY_NAME)) {
    GstBuffer *tmp;
    GstBufferPoolAcquireParams params = { 0, };

    if (!sink->fallback_pool
        || !gst_buffer_pool_set_active (sink->fallback_pool, TRUE)) {
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto end;
    }

    /* take a buffer from our pool, if there is no buffer in the pool something
     * is seriously wrong, waiting for the pool here might deadlock when we try
     * to go to PAUSED because we never flush the pool. */
    params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
    ret = gst_buffer_pool_acquire_buffer (sink->fallback_pool, &tmp, &params);
    if (ret != GST_FLOW_OK)
      goto end;

    if (sink->fallback_buffer) {
      gst_buffer_unref (sink->fallback_buffer);
      sink->fallback_buffer = NULL;
    }

    mem = gst_buffer_peek_memory (tmp, 0);
    if (!mem || !gst_memory_is_type (mem, GST_D3D_SURFACE_MEMORY_NAME)) {
      ret = GST_FLOW_ERROR;
      gst_buffer_unref (tmp);
      goto end;
    }
    d3d_copy_buffer (sink, buf, tmp);
    buf = tmp;

    surface = ((GstD3DSurfaceMemory *) mem)->surface;

    /* Need to keep an additional ref until the next buffer
     * to make sure it isn't reused until then */
    sink->fallback_buffer = buf;
  } else {
    mem = gst_buffer_peek_memory (buf, 0);
    surface = ((GstD3DSurfaceMemory *) mem)->surface;

    if (sink->fallback_buffer) {
      gst_buffer_unref (sink->fallback_buffer);
      sink->fallback_buffer = NULL;
    }
  }

  if (sink->d3d.surface)
    IDirect3DSurface9_Release (sink->d3d.surface);
  IDirect3DSurface9_AddRef (surface);
  sink->d3d.surface = surface;

  if (!d3d_present_swap_chain (sink)) {
    ret = GST_FLOW_ERROR;
    goto end;
  }

end:
  UNLOCK_SINK (sink);
  return ret;
}


/** D3D Window Proc Functions **/

static LRESULT APIENTRY
d3d_wnd_proc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  GstD3DVideoSink *sink =
      (GstD3DVideoSink *) GetProp (hWnd, TEXT ("GstD3DVideoSink"));
  WNDPROC proc;
  LRESULT ret = 0;

  LOCK_SINK (sink);
  proc = sink->d3d.orig_wnd_proc;
  UNLOCK_SINK (sink);

  switch (message) {
    case WM_ERASEBKGND:
      return TRUE;
    case WM_PAINT:{
      if (proc)
        ret = CallWindowProc (proc, hWnd, message, wParam, lParam);
      /* Call this afterwards to ensure that our paint happens last */
      d3d_present_swap_chain (sink);
      goto end;
    }
    case WM_SIZE:{
      if (proc)
        ret = CallWindowProc (proc, hWnd, message, wParam, lParam);
      d3d_resize_swap_chain (sink);
      goto end;
    }
    case WM_KEYDOWN:
    case WM_KEYUP:
      if (sink->enable_navigation_events) {
        gunichar2 wcrep[128];
        if (GetKeyNameTextW (lParam, (LPWSTR) wcrep, 128)) {
          gchar *utfrep = g_utf16_to_utf8 (wcrep, 128, NULL, NULL, NULL);
          if (utfrep) {
            if (message == WM_KEYDOWN)
              gst_navigation_send_key_event (GST_NAVIGATION (sink), "key-press",
                  utfrep);
            else if (message == WM_KEYUP)
              gst_navigation_send_key_event (GST_NAVIGATION (sink),
                  "key-release", utfrep);
            g_free (utfrep);
          }
        }
      }
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:{
      gdouble x = 0, y = 0;
      if (sink->enable_navigation_events
          && d3d_get_render_coordinates (sink, LOWORD (lParam), HIWORD (lParam),
              &x, &y)) {
        gint button;
        const gchar *action = NULL;
        switch (message) {
          case WM_MOUSEMOVE:
            button = 0;
            action = "mouse-move";
            break;
          case WM_LBUTTONDOWN:
            button = 1;
            action = "mouse-button-press";
            break;
          case WM_LBUTTONUP:
            button = 1;
            action = "mouse-button-release";
            break;
          case WM_RBUTTONDOWN:
            button = 2;
            action = "mouse-button-press";
            break;
          case WM_RBUTTONUP:
            button = 2;
            action = "mouse-button-release";
            break;
          case WM_MBUTTONDOWN:
            button = 3;
            action = "mouse-button-press";
            break;
          case WM_MBUTTONUP:
            button = 3;
            action = "mouse-button-release";
            break;
          default:
            break;
        }
        if (action) {
          /* GST_DEBUG_OBJECT(sink, "%s: %lfx%lf", action, x, y); */
          gst_navigation_send_mouse_event (GST_NAVIGATION (sink), action,
              button, x, y);
        }
      }
      break;
    }
    case WM_CLOSE:
      d3d_set_window_handle (sink, 0, FALSE);
      break;
    default:
      break;
  }

  if (proc)
    ret = CallWindowProc (proc, hWnd, message, wParam, lParam);
  else
    ret = DefWindowProc (hWnd, message, wParam, lParam);

end:
  return ret;
}

/** Internal Window **/

static LRESULT APIENTRY
d3d_wnd_proc_internal (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
    case WM_DESTROY:
      GST_DEBUG ("Internal window: WM_DESTROY");
      /* Tell the internal window thread to shut down */
      PostQuitMessage (0);
      GST_DEBUG ("Posted quit..");
      break;
  }

  return DefWindowProc (hWnd, message, wParam, lParam);
}

static HWND
_d3d_create_internal_window (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  int width, height;
  int offx, offy;
  DWORD exstyle, style;
  HWND video_window;
  RECT rect;
  int screenwidth;
  int screenheight;

  /*
   * GST_VIDEO_SINK_WIDTH() is the aspect-ratio-corrected size of the video.
   * GetSystemMetrics() returns the width of the dialog's border (doubled
   * b/c of left and right borders).
   */
  width = GST_VIDEO_SINK_WIDTH (sink) + GetSystemMetrics (SM_CXSIZEFRAME) * 2;
  height =
      GST_VIDEO_SINK_HEIGHT (sink) + GetSystemMetrics (SM_CYCAPTION) +
      (GetSystemMetrics (SM_CYSIZEFRAME) * 2);

  SystemParametersInfo (SPI_GETWORKAREA, 0, &rect, 0);
  screenwidth = rect.right - rect.left;
  screenheight = rect.bottom - rect.top;
  offx = rect.left;
  offy = rect.top;

  /* Make it fit into the screen without changing the aspect ratio. */
  if (width > screenwidth) {
    double ratio = (double) screenwidth / (double) width;
    width = screenwidth;
    height = (int) (height * ratio);
  }

  if (height > screenheight) {
    double ratio = (double) screenheight / (double) height;
    height = screenheight;
    width = (int) (width * ratio);
  }

  style = WS_OVERLAPPEDWINDOW;  /* Normal top-level window */
  exstyle = 0;
  video_window = CreateWindowEx (exstyle,
      klass->d3d.wnd_class.lpszClassName,
      TEXT ("GStreamer D3D video sink (internal window)"),
      style, offx, offy, width, height,
      NULL, NULL, klass->d3d.wnd_class.hInstance, sink);

  if (video_window == NULL) {
    GST_ERROR_OBJECT (sink, "Failed to create internal window: %lu",
        GetLastError ());
    return NULL;
  }

  /* Now show the window, as appropriate */
  ShowWindow (video_window, SW_SHOWNORMAL);

  /* Trigger the initial paint of the window */
  UpdateWindow (video_window);

  return video_window;
}

typedef struct
{
  GstD3DVideoSink *sink;
  gboolean running;
  HWND hWnd;
} D3DInternalWindowDat;

static gpointer
d3d_internal_window_thread (D3DInternalWindowDat * dat)
{
  GstD3DVideoSink *sink;
  HWND hWnd;
  MSG msg;

  g_return_val_if_fail (dat != NULL, NULL);

  sink = dat->sink;
  GST_DEBUG_OBJECT (sink, "Entering internal window thread: %p",
      g_thread_self ());

  /* Create internal window */
  hWnd = _d3d_create_internal_window (sink);
  if (!hWnd) {
    GST_ERROR_OBJECT (sink, "Failed to create internal window");
    goto end;
  }

  dat->hWnd = hWnd;
  dat->running = TRUE;

  /*
   * Internal window message loop
   */

  while (GetMessage (&msg, NULL, 0, 0)) {
    if (msg.message == WM_QUIT_THREAD)
      break;
    TranslateMessage (&msg);
    DispatchMessage (&msg);
  }

end:
  GST_DEBUG_OBJECT (sink, "Exiting internal window thread: %p",
      g_thread_self ());
  return NULL;
}

static HWND
d3d_create_internal_window (GstD3DVideoSink * sink)
{
  GThread *thread;
  D3DInternalWindowDat dat;
  gulong timeout_interval = 10000;      /* 10 ms interval */
  gulong intervals = (10000000 / timeout_interval);     /* 10 secs */
  gulong i;

  dat.sink = sink;
  dat.running = FALSE;
  dat.hWnd = 0;

  thread =
      g_thread_new ("d3dvideosink-window-thread",
      (GThreadFunc) d3d_internal_window_thread, &dat);
  if (!thread) {
    GST_ERROR ("Failed to created internal window thread");
    return 0;
  }

  /* Wait 10 seconds for window proc loop to start up */
  for (i = 0; dat.running == FALSE && i < intervals; i++) {
    g_usleep (timeout_interval);
  }

  GST_DEBUG_OBJECT (sink, "Created window: %p (intervals: %lu)", dat.hWnd, i);

  return dat.hWnd;
}

/*** D3D Video Class Methdos ***/

gboolean
d3d_class_init (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  gulong timeout_interval = 10000;      /* 10 ms interval */
  gulong intervals = (10000000 / timeout_interval);     /* 10 secs */
  gboolean ret = FALSE;
  gulong i;

  g_return_val_if_fail (klass != NULL, FALSE);

  LOCK_CLASS (sink, klass);

  klass->d3d.refs += 1;
  GST_DEBUG ("D3D class init [refs:%u]", klass->d3d.refs);
  klass->d3d.sink_list = g_list_append (klass->d3d.sink_list, sink);

  if (klass->d3d.refs > 1)
    goto end;

  WM_D3DVIDEO_NOTIFY_DEVICE_LOST =
      RegisterWindowMessage ("WM_D3DVIDEO_NOTIFY_DEVICE_LOST");

  klass->d3d.d3d = Direct3DCreate9 (D3D_SDK_VERSION);
  if (!klass->d3d.d3d) {
    GST_ERROR ("Unable to create Direct3D interface");
    goto error;
  }

  /* Register Window Class for internal Windows */
  memset (&klass->d3d.wnd_class, 0, sizeof (WNDCLASS));
  klass->d3d.wnd_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  klass->d3d.wnd_class.hInstance = GetModuleHandle (NULL);
  klass->d3d.wnd_class.lpszClassName = TEXT ("GstD3DVideoSinkInternalWindow");
  klass->d3d.wnd_class.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
  klass->d3d.wnd_class.hCursor = LoadCursor (NULL, IDC_ARROW);
  klass->d3d.wnd_class.hIcon = LoadIcon (NULL, IDI_APPLICATION);
  klass->d3d.wnd_class.cbClsExtra = 0;
  klass->d3d.wnd_class.cbWndExtra = 0;
  klass->d3d.wnd_class.lpfnWndProc = d3d_wnd_proc_internal;

  if (RegisterClass (&klass->d3d.wnd_class) == 0) {
    GST_ERROR ("Failed to register window class: %lu", GetLastError ());
    goto error;
  }

  klass->d3d.running = FALSE;
  klass->d3d.error_exit = FALSE;
  UNLOCK_CLASS (sink, klass);
  klass->d3d.thread =
      g_thread_new ("d3dvideosink-window-thread",
      (GThreadFunc) d3d_hidden_window_thread, klass);
  LOCK_CLASS (sink, klass);

  if (!klass->d3d.thread) {
    GST_ERROR ("Failed to created hidden window thread");
    goto error;
  }

  UNLOCK_CLASS (sink, klass);
  /* Wait 10 seconds for window proc loop to start up */
  for (i = 0; klass->d3d.running == FALSE && i < intervals; i++) {
    g_usleep (timeout_interval);
  }
  LOCK_CLASS (sink, klass);

  if (klass->d3d.error_exit)
    goto error;

  if (!klass->d3d.running) {
    GST_ERROR ("Waited %lu ms, window proc loop has not started",
        (timeout_interval * intervals) / 1000);
    goto error;
  }

  GST_DEBUG ("Hidden window message loop is running..");

end:
  ret = TRUE;
error:
  UNLOCK_CLASS (sink, klass);

  if (!ret)
    d3d_class_destroy (sink);

  return ret;
}

void
d3d_class_destroy (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);

  g_return_if_fail (klass != NULL);

  LOCK_CLASS (sink, klass);

  klass->d3d.refs -= 1;

  GST_DEBUG ("D3D class destroy [refs:%u]", klass->d3d.refs);

  klass->d3d.sink_list = g_list_remove (klass->d3d.sink_list, sink);

  if (klass->d3d.refs >= 1)
    goto end;

  UNLOCK_CLASS (sink, klass);

  if (klass->d3d.running) {
    GST_DEBUG ("Shutting down window proc thread, waiting to join..");
    PostMessage (klass->d3d.hidden_window, WM_QUIT, 0, 0);
    g_thread_join (klass->d3d.thread);
    GST_DEBUG ("Joined..");
  }

  LOCK_CLASS (sink, klass);

  if (klass->d3d.d3d) {
    int ref_count;
    ref_count = IDirect3D9_Release (klass->d3d.d3d);
    GST_DEBUG ("Direct3D object released. Reference count: %d", ref_count);
  }

  UnregisterClass (klass->d3d.wnd_class.lpszClassName,
      klass->d3d.wnd_class.hInstance);

  memset (&klass->d3d, 0, sizeof (GstD3DDataClass));

end:
  UNLOCK_CLASS (sink, klass);
}

static gboolean
d3d_class_display_device_create (GstD3DVideoSinkClass * klass, UINT adapter)
{
  LPDIRECT3D9 d3d;
  GstD3DDisplayDevice *device;
  HWND hwnd;
  D3DCAPS9 caps;
  D3DDISPLAYMODE disp_mode;
  DWORD create_mask = 0;
  HRESULT hr;
  gboolean ret = FALSE;

  g_return_val_if_fail (klass != NULL, FALSE);

  GST_DEBUG (" ");

  LOCK_CLASS (NULL, klass);

  d3d = klass->d3d.d3d;
  device = &klass->d3d.device;
  hwnd = klass->d3d.hidden_window;

  memset (&caps, 0, sizeof (caps));
  memset (&disp_mode, 0, sizeof (disp_mode));
  memset (&device->present_params, 0, sizeof (device->present_params));

  device->adapter = adapter;

  if (IDirect3D9_GetAdapterDisplayMode (d3d, adapter, &disp_mode) != D3D_OK) {
    GST_ERROR ("Unable to request adapter[%u] display mode", adapter);
    goto error;
  }

  if (IDirect3D9_GetDeviceCaps (d3d, adapter, D3DDEVTYPE_HAL, &caps) != D3D_OK) {
    GST_ERROR ("Unable to request adapter[%u] device caps", adapter);
    goto error;
  }

  /* Ask DirectX to please not clobber the FPU state when making DirectX
   * API calls. This can cause libraries such as cairo to misbehave in
   * certain scenarios.
   */
  create_mask = 0 | D3DCREATE_FPU_PRESERVE;

  /* Make sure that device access is threadsafe */
  create_mask |= D3DCREATE_MULTITHREADED;

  /* Determine vertex processing capabilities. Some cards have issues
   * using software vertex processing. Courtesy:
   * http://www.chadvernon.com/blog/resources/directx9/improved-direct3d-initialization/
   */
  if ((caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ==
      D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
    create_mask |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
    /* if ((d3dcaps.DevCaps & D3DDEVCAPS_PUREDEVICE) == D3DDEVCAPS_PUREDEVICE) */
    /*  d3dcreate |= D3DCREATE_PUREDEVICE; */
  } else {
    create_mask |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
  }

  /* Check the filter type. */
  if ((caps.StretchRectFilterCaps & D3DPTFILTERCAPS_MINFLINEAR) ==
      D3DPTFILTERCAPS_MINFLINEAR
      || (caps.StretchRectFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR) ==
      D3DPTFILTERCAPS_MAGFLINEAR) {
    device->filter_type = D3DTEXF_LINEAR;
  } else {
    device->filter_type = D3DTEXF_NONE;
  }

  /* Setup the display mode format. */
  device->format = disp_mode.Format;

  /* present_params.Flags = D3DPRESENTFLAG_VIDEO; */
  device->present_params.Windowed = TRUE;
  device->present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
  device->present_params.BackBufferCount = 1;
  device->present_params.BackBufferFormat = device->format;
  device->present_params.BackBufferWidth = 1;
  device->present_params.BackBufferHeight = 1;
  device->present_params.MultiSampleType = D3DMULTISAMPLE_NONE;
  device->present_params.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;    /* D3DPRESENT_INTERVAL_IMMEDIATE; */

  GST_DEBUG ("Creating Direct3D device for hidden window %p", NULL);

  if ((hr = IDirect3D9_CreateDevice (d3d, adapter, D3DDEVTYPE_HAL, hwnd,
              create_mask, &device->present_params,
              &device->d3d_device)) != D3D_OK) {
    GST_ERROR ("Unable to create Direct3D device. Result: %ld (0x%lx)", hr, hr);
    goto error;
  }

  GST_DEBUG ("Display Device format: %s",
      d3d_format_to_string (disp_mode.Format));

  ret = TRUE;
  goto end;
error:
  memset (device, 0, sizeof (GstD3DDisplayDevice));
end:
  UNLOCK_CLASS (NULL, klass);

  return ret;
}

static void
d3d_class_display_device_destroy (GstD3DVideoSinkClass * klass)
{
  g_return_if_fail (klass != NULL);

  LOCK_CLASS (NULL, klass);
  if (klass->d3d.device.d3d_device) {
    int ref_count;
    ref_count = IDirect3DDevice9_Release (klass->d3d.device.d3d_device);
    GST_DEBUG ("Direct3D device [adapter:%u] released. Reference count: %d",
        klass->d3d.device.adapter, ref_count);
  }
  memset (&klass->d3d.device, 0, sizeof (GstD3DDisplayDevice));
  UNLOCK_CLASS (NULL, klass);
}

static void
d3d_class_notify_device_lost (GstD3DVideoSink * sink)
{
  GstD3DVideoSinkClass *klass = GST_D3DVIDEOSINK_GET_CLASS (sink);
  PostMessage (klass->d3d.hidden_window, WM_D3DVIDEO_NOTIFY_DEVICE_LOST, 0, 0);
}

static void
d3d_class_notify_device_lost_all (GstD3DVideoSinkClass * klass)
{
  g_return_if_fail (klass != NULL);

  LOCK_CLASS (NULL, klass);
  if (!klass->d3d.device_lost) {
    GList *lst, *clst;
    klass->d3d.device_lost = TRUE;

    GST_DEBUG ("Notifying all instances of device loss");

    clst = g_list_copy (klass->d3d.sink_list);
    UNLOCK_CLASS (NULL, klass);

    for (lst = clst; lst != NULL; lst = lst->next) {
      GstD3DVideoSink *sink = (GstD3DVideoSink *) lst->data;
      if (!sink)
        continue;
      d3d_notify_device_lost (sink);
    }
    g_list_free (clst);
    LOCK_CLASS (NULL, klass);

    /* Set timer to try reset at given interval */
    SetTimer (klass->d3d.hidden_window, IDT_DEVICE_RESET_TIMER, 500, NULL);
  }
  UNLOCK_CLASS (NULL, klass);
}

static void
d3d_class_reset_display_device (GstD3DVideoSinkClass * klass)
{
  HRESULT hr;

  g_return_if_fail (klass != NULL);

  LOCK_CLASS (NULL, klass);
  hr = IDirect3DDevice9_Reset (klass->d3d.device.d3d_device,
      &klass->d3d.device.present_params);
  ERROR_CHECK_HR (hr) {
    CASE_HR_ERR (D3DERR_DEVICELOST);
    CASE_HR_ERR (D3DERR_DEVICEREMOVED);
    CASE_HR_ERR (D3DERR_DRIVERINTERNALERROR);
    CASE_HR_ERR (D3DERR_OUTOFVIDEOMEMORY);
    CASE_HR_DBG_END (NULL, "Attempt device reset.. failed");
    goto end;
  }

  GST_INFO ("Attempt device reset.. success");

  klass->d3d.device_lost = FALSE;
  KillTimer (klass->d3d.hidden_window, IDT_DEVICE_RESET_TIMER);

  g_list_foreach (klass->d3d.sink_list, (GFunc) d3d_notify_device_reset, NULL);
end:;
  UNLOCK_CLASS (NULL, klass);
}

/** Hidden Window Loop Thread **/

static LRESULT APIENTRY
D3DHiddenWndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
    case WM_TIMER:
      switch (wParam) {
        case IDT_DEVICE_RESET_TIMER:
          d3d_class_reset_display_device ((GstD3DVideoSinkClass *)
              GetWindowLongPtr (hWnd, GWLP_USERDATA));
          break;
        default:;
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage (0);
      return 0;
    default:
      /* non constants */
      if (message == WM_D3DVIDEO_NOTIFY_DEVICE_LOST) {
        d3d_class_notify_device_lost_all ((GstD3DVideoSinkClass *)
            GetWindowLongPtr (hWnd, GWLP_USERDATA));
        return 0;
      }
  }

  return DefWindowProc (hWnd, message, wParam, lParam);
}

static gboolean
d3d_hidden_window_thread (GstD3DVideoSinkClass * klass)
{
  WNDCLASS WndClass;
  gboolean reged = FALSE;
  HWND hWnd = 0;
  gboolean ret = FALSE;

  g_return_val_if_fail (klass != NULL, FALSE);

  memset (&WndClass, 0, sizeof (WNDCLASS));
  WndClass.hInstance = GetModuleHandle (NULL);
  WndClass.lpszClassName = TEXT ("gstd3dvideo-hidden-window-class");
  WndClass.lpfnWndProc = D3DHiddenWndProc;

  if (!RegisterClass (&WndClass)) {
    GST_ERROR ("Unable to register Direct3D hidden window class");
    goto error;
  }
  reged = TRUE;

  hWnd = CreateWindowEx (0,
      WndClass.lpszClassName,
      TEXT ("GStreamer Direct3D hidden window"),
      WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, WndClass.hInstance, klass);

  if (hWnd == NULL) {
    GST_ERROR ("Failed to create Direct3D hidden window");
    goto error;
  }

  GST_DEBUG ("Direct3D hidden window handle: %p", hWnd);

  klass->d3d.hidden_window = hWnd;

  /* TODO: Multi-monitor setup? */
  if (!d3d_class_display_device_create (klass, D3DADAPTER_DEFAULT)) {
    GST_ERROR ("Failed to initiazlize adapter: %u", D3DADAPTER_DEFAULT);
    goto error;
  }

  /* Attach data to window */
  SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR) klass);

  GST_DEBUG ("Entering Direct3D hidden window message loop");

  klass->d3d.running = TRUE;

  /* Hidden Window Message Loop */
  while (1) {
    MSG msg;
    while (GetMessage (&msg, NULL, 0, 0)) {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }
    if (msg.message == WM_QUIT || msg.message == WM_CLOSE)
      break;
  }

  klass->d3d.running = FALSE;

  GST_DEBUG ("Leaving Direct3D hidden window message loop");

  ret = TRUE;

error:
  if (!ret)
    klass->d3d.error_exit = TRUE;
  if (hWnd) {
    PostMessage (hWnd, WM_DESTROY, 0, 0);
    DestroyWindow (hWnd);
    klass->d3d.hidden_window = 0;
  }
  if (reged)
    UnregisterClass (WndClass.lpszClassName, WndClass.hInstance);
  d3d_class_display_device_destroy (klass);

  return ret;
}
