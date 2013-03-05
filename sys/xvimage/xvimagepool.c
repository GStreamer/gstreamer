/* GStreamer
 * Copyright (C) <2005> Julien Moutte <julien@moutte.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Object header */
#include "xvimagesink.h"

/* Debugging category */
#include <gst/gstinfo.h>

/* Helper functions */
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>


GST_DEBUG_CATEGORY_EXTERN (gst_debug_xvimagepool);
#define GST_CAT_DEFAULT gst_debug_xvimagepool


struct _GstXvImageBufferPoolPrivate
{
  GstCaps *caps;
  gint im_format;
  GstVideoInfo info;
  GstVideoAlignment align;
  guint padded_width;
  guint padded_height;
  gboolean add_metavideo;
  gboolean need_alignment;
};

/* X11 stuff */
static gboolean error_caught = FALSE;

static int
gst_xvimagesink_handle_xerror (Display * display, XErrorEvent * xevent)
{
  char error_msg[1024];

  XGetErrorText (display, xevent->error_code, error_msg, 1024);
  GST_DEBUG ("xvimagesink triggered an XError. error: %s", error_msg);
  error_caught = TRUE;
  return 0;
}

static GstMemory *
gst_xvimage_memory_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  return NULL;
}

static void
gst_xvimage_memory_free (GstAllocator * allocator, GstMemory * gmem)
{
  GstXvImageMemory *mem = (GstXvImageMemory *) gmem;
  GstXvImageSink *xvimagesink;

  if (gmem->parent)
    goto sub_mem;

  xvimagesink = mem->sink;

  GST_DEBUG_OBJECT (xvimagesink, "free memory %p", mem);

  /* Hold the object lock to ensure the XContext doesn't disappear */
  GST_OBJECT_LOCK (xvimagesink);
  /* We might have some buffers destroyed after changing state to NULL */
  if (xvimagesink->xcontext == NULL) {
    GST_DEBUG_OBJECT (xvimagesink, "Destroying XvImage after Xcontext");
#ifdef HAVE_XSHM
    /* Need to free the shared memory segment even if the x context
     * was already cleaned up */
    if (mem->SHMInfo.shmaddr != ((void *) -1)) {
      shmdt (mem->SHMInfo.shmaddr);
    }
#endif
    if (mem->xvimage)
      XFree (mem->xvimage);
    goto beach;
  }

  g_mutex_lock (&xvimagesink->x_lock);

#ifdef HAVE_XSHM
  if (xvimagesink->xcontext->use_xshm) {
    if (mem->SHMInfo.shmaddr != ((void *) -1)) {
      GST_DEBUG_OBJECT (xvimagesink, "XServer ShmDetaching from 0x%x id 0x%lx",
          mem->SHMInfo.shmid, mem->SHMInfo.shmseg);
      XShmDetach (xvimagesink->xcontext->disp, &mem->SHMInfo);
      XSync (xvimagesink->xcontext->disp, FALSE);
      shmdt (mem->SHMInfo.shmaddr);
      mem->SHMInfo.shmaddr = (void *) -1;
    }
    if (mem->xvimage)
      XFree (mem->xvimage);
  } else
#endif /* HAVE_XSHM */
  {
    if (mem->xvimage) {
      g_free (mem->xvimage->data);
      XFree (mem->xvimage);
    }
  }

  XSync (xvimagesink->xcontext->disp, FALSE);

  g_mutex_unlock (&xvimagesink->x_lock);

beach:
  GST_OBJECT_UNLOCK (xvimagesink);

  gst_object_unref (mem->sink);

sub_mem:
  g_slice_free (GstXvImageMemory, mem);
}

static gpointer
xvimage_memory_map (GstXvImageMemory * mem, gsize maxsize, GstMapFlags flags)
{
  return mem->xvimage->data + mem->parent.offset;
}

static gboolean
xvimage_memory_unmap (GstXvImageMemory * mem)
{
  return TRUE;
}

static GstXvImageMemory *
xvimage_memory_share (GstXvImageMemory * mem, gssize offset, gsize size)
{
  GstXvImageMemory *sub;
  GstMemory *parent;

  /* We can only share the complete memory */
  if (offset != 0)
    return NULL;
  if (size != -1 && size != mem->size)
    return NULL;

  /* find the real parent */
  if ((parent = mem->parent.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->parent.size - offset;

  /* the shared memory is always readonly */
  sub = g_slice_new (GstXvImageMemory);

  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->parent.allocator,
      &mem->parent, mem->parent.maxsize, mem->parent.align,
      mem->parent.offset + offset, size);
  sub->sink = mem->sink;
  sub->xvimage = mem->xvimage;
  sub->SHMInfo = mem->SHMInfo;
  sub->x = mem->x;
  sub->y = mem->y;
  sub->width = mem->width;
  sub->height = mem->height;

  return sub;
}

typedef GstAllocator GstXvImageMemoryAllocator;
typedef GstAllocatorClass GstXvImageMemoryAllocatorClass;

GType xvimage_memory_allocator_get_type (void);
G_DEFINE_TYPE (GstXvImageMemoryAllocator, xvimage_memory_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_XVIMAGE_ALLOCATOR_NAME "xvimage"
#define GST_TYPE_XVIMAGE_MEMORY_ALLOCATOR   (xvimage_memory_allocator_get_type())
#define GST_IS_XVIMAGE_MEMORY_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_XVIMAGE_MEMORY_ALLOCATOR))

static void
xvimage_memory_allocator_class_init (GstXvImageMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_xvimage_memory_alloc;
  allocator_class->free = gst_xvimage_memory_free;
}

static void
xvimage_memory_allocator_init (GstXvImageMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_XVIMAGE_ALLOCATOR_NAME;
  alloc->mem_map = (GstMemoryMapFunction) xvimage_memory_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) xvimage_memory_unmap;
  alloc->mem_share = (GstMemoryShareFunction) xvimage_memory_share;
  /* fallback copy and is_span */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
xvimage_memory_alloc (GstXvImageBufferPool * xvpool)
{
  GstXvImageSink *xvimagesink;
  int (*handler) (Display *, XErrorEvent *);
  gboolean success = FALSE;
  GstXContext *xcontext;
  gint width, height, im_format, align = 15, offset;
  GstXvImageBufferPoolPrivate *priv;
  GstXvImageMemory *mem;

  priv = xvpool->priv;
  xvimagesink = xvpool->sink;
  xcontext = xvimagesink->xcontext;

  width = priv->padded_width;
  height = priv->padded_height;
  im_format = priv->im_format;

  mem = g_slice_new (GstXvImageMemory);

#ifdef HAVE_XSHM
  mem->SHMInfo.shmaddr = ((void *) -1);
  mem->SHMInfo.shmid = -1;
#endif
  mem->x = priv->align.padding_left;
  mem->y = priv->align.padding_top;
  mem->width = priv->info.width;
  mem->height = priv->info.height;
  mem->sink = gst_object_ref (xvimagesink);
  mem->im_format = im_format;

  GST_DEBUG_OBJECT (xvimagesink, "creating image %p (%dx%d)", mem,
      width, height);

  g_mutex_lock (&xvimagesink->x_lock);

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_xvimagesink_handle_xerror);

#ifdef HAVE_XSHM
  if (xcontext->use_xshm) {
    int expected_size;

    mem->xvimage = XvShmCreateImage (xcontext->disp,
        xcontext->xv_port_id, im_format, NULL, width, height, &mem->SHMInfo);
    if (!mem->xvimage || error_caught) {
      g_mutex_unlock (&xvimagesink->x_lock);

      /* Reset error flag */
      error_caught = FALSE;

      /* Push a warning */
      GST_ELEMENT_WARNING (xvimagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              width, height),
          ("could not XShmCreateImage a %dx%d image", width, height));

      /* Retry without XShm */
      xvimagesink->xcontext->use_xshm = FALSE;

      /* Hold X mutex again to try without XShm */
      g_mutex_lock (&xvimagesink->x_lock);
      goto no_xshm;
    }

    /* we have to use the returned data_size for our shm size */
    mem->size = mem->xvimage->data_size;
    GST_LOG_OBJECT (xvimagesink, "XShm image size is %" G_GSIZE_FORMAT,
        mem->size);

    /* calculate the expected size.  This is only for sanity checking the
     * number we get from X. */
    switch (im_format) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      {
        gint pitches[3];
        gint offsets[3];
        guint plane;

        offsets[0] = 0;
        pitches[0] = GST_ROUND_UP_4 (width);
        offsets[1] = offsets[0] + pitches[0] * GST_ROUND_UP_2 (height);
        pitches[1] = GST_ROUND_UP_8 (width) / 2;
        offsets[2] = offsets[1] + pitches[1] * GST_ROUND_UP_2 (height) / 2;
        pitches[2] = GST_ROUND_UP_8 (pitches[0]) / 2;

        expected_size = offsets[2] + pitches[2] * GST_ROUND_UP_2 (height) / 2;

        for (plane = 0; plane < mem->xvimage->num_planes; plane++) {
          GST_DEBUG_OBJECT (xvimagesink,
              "Plane %u has a expected pitch of %d bytes, " "offset of %d",
              plane, pitches[plane], offsets[plane]);
        }
        break;
      }
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        expected_size = height * GST_ROUND_UP_4 (width * 2);
        break;
      default:
        expected_size = 0;
        break;
    }
    if (expected_size != 0 && mem->size != expected_size) {
      GST_WARNING_OBJECT (xvimagesink,
          "unexpected XShm image size (got %" G_GSIZE_FORMAT ", expected %d)",
          mem->size, expected_size);
    }

    /* Be verbose about our XvImage stride */
    {
      guint plane;

      for (plane = 0; plane < mem->xvimage->num_planes; plane++) {
        GST_DEBUG_OBJECT (xvimagesink, "Plane %u has a pitch of %d bytes, "
            "offset of %d", plane, mem->xvimage->pitches[plane],
            mem->xvimage->offsets[plane]);
      }
    }

    /* get shared memory */
    mem->SHMInfo.shmid =
        shmget (IPC_PRIVATE, mem->size + align, IPC_CREAT | 0777);
    if (mem->SHMInfo.shmid == -1)
      goto shmget_failed;

    /* attach */
    mem->SHMInfo.shmaddr = shmat (mem->SHMInfo.shmid, NULL, 0);
    if (mem->SHMInfo.shmaddr == ((void *) -1))
      goto shmat_failed;

    /* now we can set up the image data */
    mem->xvimage->data = mem->SHMInfo.shmaddr;
    mem->SHMInfo.readOnly = FALSE;

    if (XShmAttach (xcontext->disp, &mem->SHMInfo) == 0)
      goto xattach_failed;

    XSync (xcontext->disp, FALSE);

    /* Delete the shared memory segment as soon as we everyone is attached.
     * This way, it will be deleted as soon as we detach later, and not
     * leaked if we crash. */
    shmctl (mem->SHMInfo.shmid, IPC_RMID, NULL);

    GST_DEBUG_OBJECT (xvimagesink, "XServer ShmAttached to 0x%x, id 0x%lx",
        mem->SHMInfo.shmid, mem->SHMInfo.shmseg);
  } else
  no_xshm:
#endif /* HAVE_XSHM */
  {
    mem->xvimage = XvCreateImage (xcontext->disp,
        xcontext->xv_port_id, im_format, NULL, width, height);
    if (!mem->xvimage || error_caught)
      goto create_failed;

    /* we have to use the returned data_size for our image size */
    mem->size = mem->xvimage->data_size;
    mem->xvimage->data = g_malloc (mem->size + align);

    XSync (xcontext->disp, FALSE);
  }

  if ((offset = ((guintptr) mem->xvimage->data & align)))
    offset = (align + 1) - offset;

  GST_DEBUG_OBJECT (xvimagesink, "memory %p, align %d, offset %d",
      mem->xvimage->data, align, offset);

  /* Reset error handler */
  error_caught = FALSE;
  XSetErrorHandler (handler);

  gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_NO_SHARE,
      xvpool->allocator, NULL, mem->size + align, align, offset, mem->size);

  g_mutex_unlock (&xvimagesink->x_lock);

  success = TRUE;

beach:
  if (!success) {
    g_slice_free (GstXvImageMemory, mem);
    mem = NULL;
  }

  return GST_MEMORY_CAST (mem);

  /* ERRORS */
create_failed:
  {
    g_mutex_unlock (&xvimagesink->x_lock);
    /* Reset error handler */
    error_caught = FALSE;
    XSetErrorHandler (handler);
    /* Push an error */
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height),
        ("could not XvShmCreateImage a %dx%d image", width, height));
    goto beach;
  }
#ifdef HAVE_XSHM
shmget_failed:
  {
    g_mutex_unlock (&xvimagesink->x_lock);
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height),
        ("could not get shared memory of %" G_GSIZE_FORMAT " bytes",
            mem->size));
    goto beach;
  }
shmat_failed:
  {
    g_mutex_unlock (&xvimagesink->x_lock);
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height), ("Failed to shmat: %s", g_strerror (errno)));
    /* Clean up the shared memory segment */
    shmctl (mem->SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }
xattach_failed:
  {
    /* Clean up the shared memory segment */
    shmctl (mem->SHMInfo.shmid, IPC_RMID, NULL);
    g_mutex_unlock (&xvimagesink->x_lock);

    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height), ("Failed to XShmAttach"));
    goto beach;
  }
#endif
}

#ifdef HAVE_XSHM
/* This function checks that it is actually really possible to create an image
   using XShm */
gboolean
gst_xvimagesink_check_xshm_calls (GstXvImageSink * xvimagesink,
    GstXContext * xcontext)
{
  XvImage *xvimage;
  XShmSegmentInfo SHMInfo;
  size_t size;
  int (*handler) (Display *, XErrorEvent *);
  gboolean result = FALSE;
  gboolean did_attach = FALSE;

  g_return_val_if_fail (xcontext != NULL, FALSE);

  /* Sync to ensure any older errors are already processed */
  XSync (xcontext->disp, FALSE);

  /* Set defaults so we don't free these later unnecessarily */
  SHMInfo.shmaddr = ((void *) -1);
  SHMInfo.shmid = -1;

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_xvimagesink_handle_xerror);

  /* Trying to create a 1x1 picture */
  GST_DEBUG ("XvShmCreateImage of 1x1");
  xvimage = XvShmCreateImage (xcontext->disp, xcontext->xv_port_id,
      xcontext->im_format, NULL, 1, 1, &SHMInfo);

  /* Might cause an error, sync to ensure it is noticed */
  XSync (xcontext->disp, FALSE);
  if (!xvimage || error_caught) {
    GST_WARNING ("could not XvShmCreateImage a 1x1 image");
    goto beach;
  }
  size = xvimage->data_size;

  SHMInfo.shmid = shmget (IPC_PRIVATE, size, IPC_CREAT | 0777);
  if (SHMInfo.shmid == -1) {
    GST_WARNING ("could not get shared memory of %" G_GSIZE_FORMAT " bytes",
        size);
    goto beach;
  }

  SHMInfo.shmaddr = shmat (SHMInfo.shmid, NULL, 0);
  if (SHMInfo.shmaddr == ((void *) -1)) {
    GST_WARNING ("Failed to shmat: %s", g_strerror (errno));
    /* Clean up the shared memory segment */
    shmctl (SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }

  xvimage->data = SHMInfo.shmaddr;
  SHMInfo.readOnly = FALSE;

  if (XShmAttach (xcontext->disp, &SHMInfo) == 0) {
    GST_WARNING ("Failed to XShmAttach");
    /* Clean up the shared memory segment */
    shmctl (SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }

  /* Sync to ensure we see any errors we caused */
  XSync (xcontext->disp, FALSE);

  /* Delete the shared memory segment as soon as everyone is attached.
   * This way, it will be deleted as soon as we detach later, and not
   * leaked if we crash. */
  shmctl (SHMInfo.shmid, IPC_RMID, NULL);

  if (!error_caught) {
    GST_DEBUG ("XServer ShmAttached to 0x%x, id 0x%lx", SHMInfo.shmid,
        SHMInfo.shmseg);

    did_attach = TRUE;
    /* store whether we succeeded in result */
    result = TRUE;
  } else {
    GST_WARNING ("MIT-SHM extension check failed at XShmAttach. "
        "Not using shared memory.");
  }

beach:
  /* Sync to ensure we swallow any errors we caused and reset error_caught */
  XSync (xcontext->disp, FALSE);

  error_caught = FALSE;
  XSetErrorHandler (handler);

  if (did_attach) {
    GST_DEBUG ("XServer ShmDetaching from 0x%x id 0x%lx",
        SHMInfo.shmid, SHMInfo.shmseg);
    XShmDetach (xcontext->disp, &SHMInfo);
    XSync (xcontext->disp, FALSE);
  }
  if (SHMInfo.shmaddr != ((void *) -1))
    shmdt (SHMInfo.shmaddr);
  if (xvimage)
    XFree (xvimage);
  return result;
}
#endif /* HAVE_XSHM */

/* bufferpool */
static void gst_xvimage_buffer_pool_finalize (GObject * object);

#define GST_XVIMAGE_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_XVIMAGE_BUFFER_POOL, GstXvImageBufferPoolPrivate))

#define gst_xvimage_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstXvImageBufferPool, gst_xvimage_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const gchar **
xvimage_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };

  return options;
}

static gboolean
xvimage_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstXvImageBufferPool *xvpool = GST_XVIMAGE_BUFFER_POOL_CAST (pool);
  GstXvImageBufferPoolPrivate *priv = xvpool->priv;
  GstVideoInfo info;
  GstCaps *caps;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  priv->im_format = gst_xvimagesink_get_format_from_info (xvpool->sink, &info);
  if (priv->im_format == -1)
    goto unknown_format;

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_ref (caps);

  /* enable metadata based on config of the pool */
  priv->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* parse extra alignment info */
  priv->need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (priv->need_alignment) {
    gst_buffer_pool_config_get_video_alignment (config, &priv->align);

    GST_LOG_OBJECT (pool, "padding %u-%ux%u-%u", priv->align.padding_top,
        priv->align.padding_left, priv->align.padding_left,
        priv->align.padding_bottom);

    /* do padding and alignment */
    gst_video_info_align (&info, &priv->align);

    /* we need the video metadata too now */
    priv->add_metavideo = TRUE;
  } else {
    gst_video_alignment_reset (&priv->align);
  }

  /* add the padding */
  priv->padded_width =
      GST_VIDEO_INFO_WIDTH (&info) + priv->align.padding_left +
      priv->align.padding_right;
  priv->padded_height =
      GST_VIDEO_INFO_HEIGHT (&info) + priv->align.padding_top +
      priv->align.padding_bottom;

  priv->info = info;

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
unknown_format:
  {
    GST_WARNING_OBJECT (xvpool->sink, "failed to get format from caps %"
        GST_PTR_FORMAT, caps);
    GST_ELEMENT_ERROR (xvpool->sink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            priv->info.width, priv->info.height),
        ("Invalid input caps %" GST_PTR_FORMAT, caps));
    return FALSE;;
  }
}

/* This function handles GstXImageBuffer creation depending on XShm availability */
static GstFlowReturn
xvimage_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstXvImageBufferPool *xvpool = GST_XVIMAGE_BUFFER_POOL_CAST (pool);
  GstXvImageBufferPoolPrivate *priv = xvpool->priv;
  GstVideoInfo *info;
  GstBuffer *xvimage;
  GstMemory *mem;

  info = &priv->info;

  xvimage = gst_buffer_new ();
  mem = xvimage_memory_alloc (xvpool);
  if (mem == NULL) {
    gst_buffer_unref (xvimage);
    goto no_buffer;
  }
  gst_buffer_append_memory (xvimage, mem);

  if (priv->add_metavideo) {
    GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");
    gst_buffer_add_video_meta_full (xvimage, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride);
  }

  *buffer = xvimage;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "can't create image");
    return GST_FLOW_ERROR;
  }
}

GstBufferPool *
gst_xvimage_buffer_pool_new (GstXvImageSink * xvimagesink)
{
  GstXvImageBufferPool *pool;

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);

  pool = g_object_new (GST_TYPE_XVIMAGE_BUFFER_POOL, NULL);
  pool->sink = gst_object_ref (xvimagesink);
  pool->allocator = g_object_new (GST_TYPE_XVIMAGE_MEMORY_ALLOCATOR, NULL);

  GST_LOG_OBJECT (pool, "new XvImage buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_xvimage_buffer_pool_class_init (GstXvImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  g_type_class_add_private (klass, sizeof (GstXvImageBufferPoolPrivate));

  gobject_class->finalize = gst_xvimage_buffer_pool_finalize;

  gstbufferpool_class->get_options = xvimage_buffer_pool_get_options;
  gstbufferpool_class->set_config = xvimage_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = xvimage_buffer_pool_alloc;
}

static void
gst_xvimage_buffer_pool_init (GstXvImageBufferPool * pool)
{
  pool->priv = GST_XVIMAGE_BUFFER_POOL_GET_PRIVATE (pool);
}

static void
gst_xvimage_buffer_pool_finalize (GObject * object)
{
  GstXvImageBufferPool *pool = GST_XVIMAGE_BUFFER_POOL_CAST (object);
  GstXvImageBufferPoolPrivate *priv = pool->priv;

  GST_LOG_OBJECT (pool, "finalize XvImage buffer pool %p", pool);

  if (priv->caps)
    gst_caps_unref (priv->caps);
  gst_object_unref (pool->sink);
  g_object_unref (pool->allocator);

  G_OBJECT_CLASS (gst_xvimage_buffer_pool_parent_class)->finalize (object);
}

/* This function tries to get a format matching with a given caps in the
   supported list of formats we generated in gst_xvimagesink_get_xv_support */
gint
gst_xvimagesink_get_format_from_info (GstXvImageSink * xvimagesink,
    GstVideoInfo * info)
{
  GList *list = NULL;

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), 0);

  list = xvimagesink->xcontext->formats_list;

  while (list) {
    GstXvImageFormat *format = list->data;

    if (format && format->vformat == GST_VIDEO_INFO_FORMAT (info))
      return format->format;

    list = g_list_next (list);
  }

  return -1;
}
