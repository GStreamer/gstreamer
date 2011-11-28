/*
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002 David A. Schleef <ds@schleef.org>
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

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "efence.h"

#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#else
/* assume we don't need it */
#define MAP_ANONYMOUS 0
#endif
#endif

GST_DEBUG_CATEGORY_STATIC (gst_efence_debug);
#define GST_CAT_DEFAULT  gst_efence_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FENCE_TOP
};

static GstStaticPadTemplate gst_efence_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_efence_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_efence_base_init (gpointer g_class);
static void gst_efence_class_init (GstEFenceClass * klass);
static void gst_efence_init (GstEFence * filter);

static void gst_efence_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_efence_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_efence_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_efence_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer);
static gboolean gst_efence_checkgetrange (GstPad * pad);
static gboolean gst_efence_activate_src_pull (GstPad * pad, gboolean active);

static GstElementClass *parent_class = NULL;

typedef struct _GstFencedBuffer GstFencedBuffer;
struct _GstFencedBuffer
{
  GstBuffer buffer;
  void *region;
  unsigned int length;
};

GType gst_fenced_buffer_get_type (void);
static void gst_fenced_buffer_finalize (GstFencedBuffer * buf);
static GstFencedBuffer *gst_fenced_buffer_copy (const GstBuffer * buffer);
static void *gst_fenced_buffer_alloc (GstBuffer * buffer, unsigned int length,
    gboolean fence_top);
static GstFlowReturn gst_efence_buffer_alloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);

#define GST_TYPE_FENCED_BUFFER (gst_fenced_buffer_get_type())

#define GST_IS_FENCED_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FENCED_BUFFER))
#define GST_FENCED_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FENCED_BUFFER, GstFencedBuffer))

GType
gst_gst_efence_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstEFenceClass),
      gst_efence_base_init,
      NULL,
      (GClassInitFunc) gst_efence_class_init,
      NULL,
      NULL,
      sizeof (GstEFence),
      0,
      (GInstanceInitFunc) gst_efence_init,
    };

    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstEFence", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_efence_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_efence_sink_factory);
  gst_element_class_add_static_pad_template (element_class,
      &gst_efence_src_factory);
  gst_element_class_set_details_simple (element_class, "Electric Fence",
      "Testing",
      "This element converts a stream of normal GStreamer buffers into a "
      "stream of buffers that are allocated in such a way that out-of-bounds "
      "access to data in the buffer is more likely to cause segmentation "
      "faults.  This allocation method is very similar to the debugging tool "
      "\"Electric Fence\".", "David A. Schleef <ds@schleef.org>");
}

/* initialize the plugin's class */
static void
gst_efence_class_init (GstEFenceClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_efence_set_property;
  gobject_class->get_property = gst_efence_get_property;

  g_object_class_install_property (gobject_class, ARG_FENCE_TOP,
      g_param_spec_boolean ("fence-top", "Fence Top",
          "Align buffers with top of fenced region", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_efence_init (GstEFence * filter)
{
  filter->sinkpad =
      gst_pad_new_from_static_template (&gst_efence_sink_factory, "sink");
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_efence_chain));
  gst_pad_set_bufferalloc_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_efence_buffer_alloc));
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad =
      gst_pad_new_from_static_template (&gst_efence_src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_setcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_checkgetrange_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_efence_checkgetrange));
  gst_pad_set_getrange_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_efence_getrange));
  gst_pad_set_activatepull_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_efence_activate_src_pull));

  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->fence_top = TRUE;
}

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_efence_chain (GstPad * pad, GstBuffer * buffer)
{
  GstEFence *efence;
  GstBuffer *copy;

  efence = GST_EFENCE (GST_OBJECT_PARENT (pad));
  g_return_val_if_fail (GST_IS_EFENCE (efence), GST_FLOW_ERROR);

  if (GST_IS_FENCED_BUFFER (buffer)) {
    GST_DEBUG_OBJECT (efence, "Passing on existing fenced buffer with caps %"
        GST_PTR_FORMAT, GST_BUFFER_CAPS (buffer));
    return gst_pad_push (efence->srcpad, buffer);
  }

  copy = (GstBuffer *) gst_fenced_buffer_copy (buffer);

  GST_DEBUG_OBJECT (efence, "Pushing newly fenced buffer with caps %"
      GST_PTR_FORMAT ", data=%p, size=%u", GST_BUFFER_CAPS (copy),
      GST_BUFFER_DATA (copy), GST_BUFFER_SIZE (copy));

  gst_buffer_unref (buffer);

  return gst_pad_push (efence->srcpad, copy);
}

static GstFlowReturn
gst_efence_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstEFence *efence;
  GstFlowReturn ret;
  GstBuffer *ownbuf;
  GstPad *peer;

  efence = GST_EFENCE (GST_OBJECT_PARENT (pad));

  peer = gst_pad_get_peer (efence->sinkpad);
  if (!peer)
    return GST_FLOW_NOT_LINKED;

  if ((ret = gst_pad_get_range (peer, offset, length, buffer)) != GST_FLOW_OK)
    goto beach;

  ownbuf = (GstBuffer *) gst_fenced_buffer_copy (*buffer);
  gst_buffer_unref ((GstBuffer *) * buffer);
  *buffer = ownbuf;

beach:
  gst_object_unref (peer);
  return ret;
}

static gboolean
gst_efence_checkgetrange (GstPad * pad)
{
  GstEFence *efence = GST_EFENCE (GST_OBJECT_PARENT (pad));

  return gst_pad_check_pull_range (efence->sinkpad);
}

static gboolean
gst_efence_activate_src_pull (GstPad * pad, gboolean active)
{
  GstEFence *efence = GST_EFENCE (GST_OBJECT_PARENT (pad));

  return gst_pad_activate_pull (efence->sinkpad, active);
}

static GstFlowReturn
gst_efence_buffer_alloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstBuffer *buffer;
  GstEFence *efence;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);

  efence = GST_EFENCE (GST_OBJECT_PARENT (pad));

  buffer = (GstBuffer *) gst_mini_object_new (GST_TYPE_FENCED_BUFFER);

  GST_BUFFER_DATA (buffer) = gst_fenced_buffer_alloc (buffer, size,
      efence->fence_top);
  GST_BUFFER_SIZE (buffer) = size;
  GST_BUFFER_OFFSET (buffer) = offset;

  if (caps)
    gst_buffer_set_caps (buffer, caps);

  *buf = buffer;

  GST_DEBUG_OBJECT (efence, "Allocated buffer of size %u, caps: %"
      GST_PTR_FORMAT, GST_BUFFER_SIZE (buffer), GST_BUFFER_CAPS (buffer));

  return GST_FLOW_OK;
}

static void
gst_efence_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEFence *filter;

  g_return_if_fail (GST_IS_EFENCE (object));
  filter = GST_EFENCE (object);

  switch (prop_id) {
    case ARG_FENCE_TOP:
      filter->fence_top = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_efence_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEFence *filter;

  g_return_if_fail (GST_IS_EFENCE (object));
  filter = GST_EFENCE (object);

  switch (prop_id) {
    case ARG_FENCE_TOP:
      g_value_set_boolean (value, filter->fence_top);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "efence", GST_RANK_NONE, GST_TYPE_EFENCE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_efence_debug, "efence", 0,
      "Debug output from the efence element");

  /* plugin initialisation succeeded */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "efence",
    "This element converts a stream of normal GStreamer buffers into a "
    "stream of buffers that are allocated in such a way that out-of-bounds "
    "access to data in the buffer is more likely to cause segmentation "
    "faults.  This allocation method is very similar to the debugging tool "
    "\"Electric Fence\".",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);


static GstBufferClass *fenced_buffer_parent_class = NULL;

static void
gst_fenced_buffer_finalize (GstFencedBuffer * buffer)
{
  GstFencedBuffer *fenced_buffer;

  GST_DEBUG ("free buffer=%p", buffer);

  fenced_buffer = GST_FENCED_BUFFER (buffer);

  /* free our data */
  if (GST_BUFFER_DATA (buffer)) {
    GST_DEBUG ("free region %p %d", fenced_buffer->region,
        fenced_buffer->length);
    munmap (fenced_buffer->region, fenced_buffer->length);
  }

  GST_MINI_OBJECT_CLASS (fenced_buffer_parent_class)->finalize (GST_MINI_OBJECT
      (buffer));
}

static GstFencedBuffer *
gst_fenced_buffer_copy (const GstBuffer * buffer)
{
  GstBuffer *copy;
  void *ptr;
  guint mask;

  g_return_val_if_fail (buffer != NULL, NULL);

  /* create a fresh new buffer */
  copy = (GstBuffer *) gst_mini_object_new (GST_TYPE_FENCED_BUFFER);

  /* we simply copy everything from our parent */
  ptr = gst_fenced_buffer_alloc (GST_BUFFER (copy),
      GST_BUFFER_SIZE (buffer), TRUE);
  memcpy (ptr, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));

  /* copy relevant flags */
  mask = GST_BUFFER_FLAG_PREROLL | GST_BUFFER_FLAG_IN_CAPS |
      GST_BUFFER_FLAG_DELTA_UNIT;
  GST_MINI_OBJECT (copy)->flags |= GST_MINI_OBJECT (buffer)->flags & mask;

  GST_BUFFER_DATA (copy) = ptr;
  GST_BUFFER_SIZE (copy) = GST_BUFFER_SIZE (buffer);
  GST_BUFFER_TIMESTAMP (copy) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (copy) = GST_BUFFER_DURATION (buffer);
  GST_BUFFER_OFFSET (copy) = GST_BUFFER_OFFSET (buffer);
  GST_BUFFER_OFFSET_END (copy) = GST_BUFFER_OFFSET_END (buffer);

  if (GST_BUFFER_CAPS (buffer))
    GST_BUFFER_CAPS (copy) = gst_caps_ref (GST_BUFFER_CAPS (buffer));
  else
    GST_BUFFER_CAPS (copy) = NULL;

  GST_DEBUG ("Copied buffer %p with ts %" GST_TIME_FORMAT
      ", caps: %" GST_PTR_FORMAT, buffer,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (copy)), GST_BUFFER_CAPS (copy));

  return GST_FENCED_BUFFER (copy);
}

void *
gst_fenced_buffer_alloc (GstBuffer * buffer, unsigned int length,
    gboolean fence_top)
{
  int alloc_size;
  void *region;
  GstFencedBuffer *fenced_buffer = (GstFencedBuffer *) buffer;
  int page_size;

  GST_DEBUG ("buffer=%p length=%d fence_top=%d", buffer, length, fence_top);

  if (length == 0)
    return NULL;

#ifdef _SC_PAGESIZE
  page_size = sysconf (_SC_PAGESIZE);
#else
  page_size = getpagesize ();
#endif

  /* Allocate a complete page, and one on either side */
  alloc_size = ((length - 1) & ~(page_size - 1)) + page_size;
  alloc_size += 2 * page_size;

  region = mmap (NULL, alloc_size, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) {
    g_warning ("mmap failed");
    return NULL;
  }
#if 0
  munmap (region, page_size);
  munmap (region + alloc_size - page_size, page_size);

  fenced_buffer->region = region + page_size;
  fenced_buffer->length = alloc_size - page_size;
#else
  mprotect (region, page_size, PROT_NONE);
  mprotect ((char *) region + alloc_size - page_size, page_size, PROT_NONE);

  fenced_buffer->region = region;
  fenced_buffer->length = alloc_size;
#endif

  GST_DEBUG ("new region %p %d", fenced_buffer->region, fenced_buffer->length);

  if (fence_top) {
    int offset;

    /* Align to top of region, but force alignment to 4 bytes */
    offset = alloc_size - page_size - length;
    offset &= ~0x3;
    return (void *) ((char *) region + offset);
  } else {
    return (void *) ((char *) region + page_size);
  }
}

static void
gst_fenced_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  fenced_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize =
      (GstMiniObjectFinalizeFunction) gst_fenced_buffer_finalize;
  mini_object_class->copy = (GstMiniObjectCopyFunction) gst_fenced_buffer_copy;
}

GType
gst_fenced_buffer_get_type (void)
{
  static GType fenced_buf_type = 0;

  if (G_UNLIKELY (!fenced_buf_type)) {
    static const GTypeInfo fenced_buf_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_fenced_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstFencedBuffer),
      0,
      NULL,
    };

    fenced_buf_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstFencedBuffer", &fenced_buf_info, 0);
  }
  return fenced_buf_type;
}
