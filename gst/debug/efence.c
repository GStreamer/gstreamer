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
#define MAP_ANONYMOUS MAP_ANON
#endif

static GstElementDetails plugin_details = {
  "Electric Fence",
  "Testing/EFence",
  "This element converts a stream of normal GStreamer buffers into a "
"stream of buffers that are allocated in such a way that out-of-bounds "
"access to data in the buffer is more likely to cause segmentation "
"faults.  This allocation method is very similar to the debugging tool "
"\"Electric Fence\".",
  "David A. Schleef <ds@schleef.org>",
};

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FENCE_TOP
};

static GstStaticPadTemplate gst_efence_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate gst_efence_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY
);

static void     gst_efence_base_init    (gpointer g_class);
static void	gst_efence_class_init	(GstEFenceClass *klass);
static void	gst_efence_init	(GstEFence *filter);

static void	gst_efence_set_property(GObject *object, guint prop_id,
                                                 const GValue *value,
					         GParamSpec *pspec);
static void	gst_efence_get_property(GObject *object, guint prop_id,
                                                 GValue *value,
						 GParamSpec *pspec);

static void	gst_efence_chain	(GstPad *pad, GstData *_data);

static GstElementClass *parent_class = NULL;

typedef struct _GstFencedBuffer GstFencedBuffer;
struct _GstFencedBuffer {
  GstBuffer buffer;
  void *region;
  unsigned int length;
};

void gst_fenced_buffer_default_free (GstData *data);
GstData * gst_fenced_buffer_default_copy (const GstData *data);
void *gst_fenced_buffer_alloc(GstBuffer *buffer, unsigned int length,
    gboolean fence_top);
static GstBuffer * gst_efence_buffer_alloc (GstPad *pad, guint64 offset, guint size);

GstBuffer *gst_fenced_buffer_new(void);

GType
gst_gst_efence_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type)
  {
    static const GTypeInfo plugin_info =
    {
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
	                                  "GstEFence",
	                                  &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_efence_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get(&gst_efence_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get(&gst_efence_src_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

/* initialize the plugin's class */
static void
gst_efence_class_init (GstEFenceClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_FENCE_TOP,
    g_param_spec_boolean ("fence_top", "Fence Top", "Align buffers with top of fenced region",
                          TRUE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_efence_set_property;
  gobject_class->get_property = gst_efence_get_property;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_efence_init (GstEFence *filter)
{
  filter->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get(&gst_efence_sink_factory), "sink");
  gst_pad_set_getcaps_function (filter->sinkpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (filter->sinkpad, gst_pad_proxy_pad_link);
  filter->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get(&gst_efence_src_factory), "src");
  gst_pad_set_getcaps_function (filter->srcpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (filter->srcpad, gst_pad_proxy_pad_link);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_efence_chain);
  gst_pad_set_bufferalloc_function (filter->sinkpad, gst_efence_buffer_alloc);

  filter->fence_top = TRUE;
}

/* chain function
 * this function does the actual processing
 */

static void
gst_efence_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buffer = GST_BUFFER (_data);
  GstEFence *efence;
  GstBuffer *copy;
  void *ptr;

  GST_DEBUG ("gst_efence_chain");

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buffer != NULL);

  efence = GST_EFENCE (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_EFENCE (efence));

  if (GST_DATA_FREE_FUNC (_data) == gst_fenced_buffer_default_free) {
    gst_pad_push (efence->srcpad, _data);
    return;
  }

  copy = gst_fenced_buffer_new();

  ptr = gst_fenced_buffer_alloc(copy, GST_BUFFER_SIZE(buffer),
      efence->fence_top);
  memcpy(ptr, GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));

  GST_BUFFER_DATA (copy)         = ptr;
  GST_BUFFER_SIZE (copy)         = GST_BUFFER_SIZE (buffer);
  GST_BUFFER_MAXSIZE (copy)      = GST_BUFFER_SIZE (buffer);
  GST_BUFFER_TIMESTAMP (copy)    = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (copy)     = GST_BUFFER_DURATION (buffer);
  GST_BUFFER_OFFSET (copy)       = GST_BUFFER_OFFSET (buffer);
  GST_BUFFER_FREE_DATA_FUNC (copy) = NULL;
  GST_BUFFER_PRIVATE (copy)      = NULL;

  gst_buffer_unref(buffer);
  gst_pad_push (efence->srcpad, GST_DATA (copy));
}

static GstBuffer *
gst_efence_buffer_alloc (GstPad *pad, guint64 offset, guint size)
{
  GstBuffer *buffer;
  GstEFence *efence;

  efence = GST_EFENCE (GST_OBJECT_PARENT (pad));

  buffer = gst_fenced_buffer_new ();

  GST_BUFFER_DATA (buffer) = gst_fenced_buffer_alloc(buffer, size,
      efence->fence_top);
  GST_BUFFER_SIZE (buffer) = size;

  return buffer;
}

static void
gst_efence_set_property (GObject *object, guint prop_id,
                                  const GValue *value, GParamSpec *pspec)
{
  GstEFence *filter;

  g_return_if_fail (GST_IS_EFENCE (object));
  filter = GST_EFENCE (object);

  switch (prop_id)
  {
  case ARG_FENCE_TOP:
    filter->fence_top = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_efence_get_property (GObject *object, guint prop_id,
                                  GValue *value, GParamSpec *pspec)
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
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "efence", GST_RANK_NONE, GST_TYPE_EFENCE))
    return FALSE;
  
  /* plugin initialisation succeeded */
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "efence",
  "This element converts a stream of normal GStreamer buffers into a "
  "stream of buffers that are allocated in such a way that out-of-bounds "
  "access to data in the buffer is more likely to cause segmentation "
  "faults.  This allocation method is very similar to the debugging tool "
  "\"Electric Fence\".",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)

GstBuffer *gst_fenced_buffer_new(void)
{
  GstBuffer *newbuf;

  newbuf = (GstBuffer *) g_new0(GstFencedBuffer,1);

  gst_data_init (GST_DATA (newbuf), _gst_buffer_type, 0,
                  gst_fenced_buffer_default_free,
                  gst_fenced_buffer_default_copy);

  GST_BUFFER_DATA (newbuf)         = NULL;
  GST_BUFFER_SIZE (newbuf)         = 0;
  GST_BUFFER_MAXSIZE (newbuf)      = GST_BUFFER_MAXSIZE_NONE;
  GST_BUFFER_TIMESTAMP (newbuf)    = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (newbuf)     = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (newbuf)       = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_FREE_DATA_FUNC (newbuf) = NULL;
  GST_BUFFER_PRIVATE (newbuf)      = NULL;

  GST_DEBUG ("new buffer=%p", newbuf);

  return newbuf;
}

void gst_fenced_buffer_default_free (GstData *data)
{
  GstFencedBuffer *fenced_buffer;
  GstBuffer *buffer = GST_BUFFER (data);

  GST_DEBUG ("free buffer=%p", data);

  g_return_if_fail (data != NULL);

  fenced_buffer = (GstFencedBuffer *) data;

  /* free our data */
  if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_DONTFREE) &&
      GST_BUFFER_DATA (buffer)) {
    GST_DEBUG ("free region %p %d", fenced_buffer->region,
	fenced_buffer->length);
    munmap(fenced_buffer->region, fenced_buffer->length);
  }else{
    GST_DEBUG ("not freeing region %p %d %p", fenced_buffer->region,
	GST_BUFFER_FLAGS(buffer), GST_BUFFER_DATA(buffer));
  }

  /* set to safe values */
  GST_BUFFER_DATA (buffer) = NULL;
  GST_BUFFER_SIZE (buffer) = 0;
  
  g_free (buffer);
}

GstData* gst_fenced_buffer_default_copy (const GstData *data)
{ 
  GstBuffer *buffer = GST_BUFFER (data);
  GstData *copy;
  void *ptr;
  
  g_return_val_if_fail (buffer != NULL, NULL);
  
  /* create a fresh new buffer */
  copy = (GstData *) g_new0(GstFencedBuffer,1);
  
  gst_data_init (copy, _gst_buffer_type, 0,
                  gst_fenced_buffer_default_free,
                  gst_fenced_buffer_default_copy);
  
  /* we simply copy everything from our parent */
  ptr = gst_fenced_buffer_alloc(GST_BUFFER(copy),
      GST_BUFFER_SIZE(buffer), TRUE);
  memcpy(ptr, GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));

  GST_BUFFER_SIZE (copy)         = GST_BUFFER_SIZE (buffer);
  GST_BUFFER_MAXSIZE (copy)      = GST_BUFFER_SIZE (buffer);
  GST_BUFFER_TIMESTAMP (copy)    = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (copy)     = GST_BUFFER_DURATION (buffer);
  GST_BUFFER_OFFSET (copy)       = GST_BUFFER_OFFSET (buffer);
  GST_BUFFER_FREE_DATA_FUNC (copy) = NULL;
  GST_BUFFER_PRIVATE (copy)      = NULL;
  
  return copy;
}

void *gst_fenced_buffer_alloc(GstBuffer *buffer, unsigned int length,
    gboolean fence_top)
{
  int alloc_size;
  void *region;
  GstFencedBuffer *fenced_buffer = (GstFencedBuffer *) buffer;
  int page_size;

  GST_DEBUG ("buffer=%p length=%d fence_top=%d", buffer, length, fence_top);

  if(length==0)return NULL;

#ifdef _SC_PAGESIZE
  page_size = sysconf(_SC_PAGESIZE);
#else
  page_size = getpagesize();
#endif

  alloc_size = ((length - 1) & ~(page_size - 1)) + page_size;
  alloc_size += 2*page_size;

  region = mmap(NULL, alloc_size, PROT_READ|PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(region == MAP_FAILED){
    g_warning("mmap failed");
    return NULL;
  }

  munmap(region, page_size);
  munmap(region + alloc_size - page_size, page_size);

  fenced_buffer->region = region;
  fenced_buffer->length = alloc_size;

  GST_DEBUG ("new region %p %d", fenced_buffer->region,
      fenced_buffer->length);
  
  if(fence_top){
    int offset;
    /* Align to top of region, but force alignment to 4 bytes */
    offset = alloc_size - page_size - length;
    offset &= ~0x3;
    return region + offset;
  }else{
    return region + page_size;
  }
}

