/* GStreamer
 * Copyright (C) 2001 RidgeRun, Inc. (www.ridgerun.com)
 *
 * gstautoplugcache.c: Data cache for the dynamic autoplugger
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

#include <gst/gst.h>

GstElementDetails gst_autoplugcache_details = {
  "AutoplugCache",
  "Connection",
  "Data cache for the dynamic autoplugger",
  VERSION,
  "Erik Walthinsen <omega@temple-baptist.com>",
  "(C) 2001 RidgeRun, Inc. (www.ridgerun.com)",
};

#define GST_TYPE_AUTOPLUGCACHE \
  (gst_autoplugcache_get_type())
#define GST_AUTOPLUGCACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUTOPLUGCACHE,GstAutoplugCache))
#define GST_AUTOPLUGCACHE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUGCACHE,GstAutoplugCacheClass))
#define GST_IS_AUTOPLUGCACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUTOPLUGCACHE))
#define GST_IS_AUTOPLUGCACHE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUGCACHE))

typedef struct _GstAutoplugCache GstAutoplugCache;
typedef struct _GstAutoplugCacheClass GstAutoplugCacheClass;

struct _GstAutoplugCache {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean caps_proxy;

  GList *cache;
  GList *cache_start;
  gint buffer_count;
  GList *current_playout;
  gboolean fire_empty;
  gboolean fire_first;
};

struct _GstAutoplugCacheClass {
  GstElementClass parent_class;

  void		(*first_buffer)		(GstElement *element, GstBuffer *buf);
  void		(*cache_empty)		(GstElement *element);
};


/* Cache signals and args */
enum {
  FIRST_BUFFER,
  CACHE_EMPTY,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BUFFER_COUNT,
  ARG_CAPS_PROXY,
  ARG_RESET
};


static void			gst_autoplugcache_class_init	(GstAutoplugCacheClass *klass);
static void			gst_autoplugcache_init		(GstAutoplugCache *cache);

static void			gst_autoplugcache_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_autoplugcache_get_property	(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static void			gst_autoplugcache_loop		(GstElement *element);

static GstPadNegotiateReturn    gst_autoplugcache_nego_src	(GstPad *pad, GstCaps **caps, gpointer *data);
static GstPadNegotiateReturn    gst_autoplugcache_nego_sink	(GstPad *pad, GstCaps **caps, gpointer *data);
static GstElementStateReturn	gst_autoplugcache_change_state	(GstElement *element);


static GstElementClass *parent_class = NULL;
static guint gst_autoplugcache_signals[LAST_SIGNAL] = { 0 };

GType
gst_autoplugcache_get_type(void) {
  static GType autoplugcache_type = 0;

  if (!autoplugcache_type) {
    static const GTypeInfo autoplugcache_info = {
      sizeof(GstAutoplugCacheClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_autoplugcache_class_init,
      NULL,
      NULL,
      sizeof(GstAutoplugCache),
      0,
      (GInstanceInitFunc)gst_autoplugcache_init,
    };
    autoplugcache_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAutoplugCache", &autoplugcache_info, 0);
  }
  return autoplugcache_type;
}

static void
gst_autoplugcache_class_init (GstAutoplugCacheClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_autoplugcache_signals[FIRST_BUFFER] =
    g_signal_newc ("first_buffer", G_OBJECT_TYPE(gobject_class), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstAutoplugCacheClass, first_buffer), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_autoplugcache_signals[CACHE_EMPTY] =
    g_signal_newc ("cache_empty", G_OBJECT_TYPE(gobject_class), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstAutoplugCacheClass, cache_empty), NULL, NULL,
                    g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFER_COUNT,
    g_param_spec_enum("buffer_count","buffer_count","buffer_count",
                      G_TYPE_INT,0,G_PARAM_READABLE)); // CHECKME!
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CAPS_PROXY,
    g_param_spec_boolean("caps_proxy","caps_proxy","caps_proxy",
                         FALSE,G_PARAM_READWRITE)); // CHECKME!
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RESET,
    g_param_spec_boolean("reset","reset","reset",
                         FALSE,G_PARAM_WRITABLE)); // CHECKME!

  gobject_class->set_property = gst_autoplugcache_set_property;
  gobject_class->get_property = gst_autoplugcache_get_property;

  gstelement_class->change_state = gst_autoplugcache_change_state;
}

static void
gst_autoplugcache_init (GstAutoplugCache *cache)
{
  gst_element_set_loop_function(GST_ELEMENT(cache), GST_DEBUG_FUNCPTR(gst_autoplugcache_loop));

  cache->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
//  gst_pad_set_negotiate_function (cache->sinkpad, gst_autoplugcache_nego_sink);
  gst_element_add_pad (GST_ELEMENT(cache), cache->sinkpad);

  cache->srcpad = gst_pad_new ("src", GST_PAD_SRC);
//  gst_pad_set_negotiate_function (cache->srcpad, gst_autoplugcache_nego_src);
  gst_element_add_pad (GST_ELEMENT(cache), cache->srcpad);

  cache->caps_proxy = FALSE;

  // provide a zero basis for the cache
  cache->cache = g_list_prepend(NULL, NULL);
  cache->cache_start = cache->cache;
  cache->buffer_count = 0;
  cache->current_playout = 0;
  cache->fire_empty = FALSE;
  cache->fire_first = FALSE;
}

static void
gst_autoplugcache_loop (GstElement *element)
{
  GstAutoplugCache *cache;
  GstBuffer *buf = NULL;

  cache = GST_AUTOPLUGCACHE (element);

  /* Theory:
   * The cache is a doubly-linked list.  The front of the list is the most recent
   * buffer, the end of the list is the first buffer.  The playout pointer always
   * points to the latest buffer sent out the end.  cache points to the front
   * (most reccent) of the list at all times.  cache_start points to the first 
   * buffer, i.e. the end of the list.
   * If the playout pointer does not have a prev (towards the most recent) buffer 
   * (== NULL), a buffer must be pulled from the sink pad and added to the cache.
   * When the playout pointer gets reset (as in a set_property), the cache is walked
   * without problems, because the playout pointer has a non-NULL next.  When
   * the playout pointer hits the end of cache again it has to start pulling.
   */

  do {
    // the first time through, the current_playout pointer is going to be NULL
    if (cache->current_playout == NULL) {
      // get a buffer
      buf = gst_pad_pull (cache->sinkpad);

      // add it to the cache, though cache == NULL
      gst_buffer_ref (buf);
      cache->cache = g_list_prepend (cache->cache, buf);
      cache->buffer_count++;

      // set the current_playout pointer
      cache->current_playout = cache->cache;

      g_signal_emit (G_OBJECT(cache), gst_autoplugcache_signals[FIRST_BUFFER], 0, buf);

      // send the buffer on its way
      gst_pad_push (cache->srcpad, buf);
    }

    // the steady state is where the playout is at the front of the cache
    else if (g_list_previous(cache->current_playout) == NULL) {

      // if we've been told to fire an empty signal (after a reset)
      if (cache->fire_empty) {
        int oldstate = GST_STATE(cache);
        GST_DEBUG(0,"at front of cache, about to pull, but firing signal\n");
        gst_object_ref (GST_OBJECT (cache));
        g_signal_emit (G_OBJECT(cache), gst_autoplugcache_signals[CACHE_EMPTY], 0, NULL);
        if (GST_STATE(cache) != oldstate) {
          gst_object_ref (GST_OBJECT (cache));
          GST_DEBUG(GST_CAT_AUTOPLUG, "state changed during signal, aborting\n");
          cothread_switch(cothread_current_main());
        }
        gst_object_unref (GST_OBJECT (cache));
      }

      // get a buffer
      buf = gst_pad_pull (cache->sinkpad);

      // add it to the front of the cache
      gst_buffer_ref (buf);
      cache->cache = g_list_prepend (cache->cache, buf);
      cache->buffer_count++;

      // set the current_playout pointer
      cache->current_playout = cache->cache;

      // send the buffer on its way
      gst_pad_push (cache->srcpad, buf);
    }

    // otherwise we're trundling through existing cached buffers
    else {
      // move the current_playout pointer
      cache->current_playout = g_list_previous (cache->current_playout);

      if (cache->fire_first) {
        g_signal_emit (G_OBJECT(cache), gst_autoplugcache_signals[FIRST_BUFFER], 0, buf);
        cache->fire_first = FALSE;
      }

      // push that buffer
      gst_pad_push (cache->srcpad, GST_BUFFER(cache->current_playout->data));
    }
  } while (!GST_FLAG_IS_SET (element, GST_ELEMENT_COTHREAD_STOPPING));
}

static GstPadNegotiateReturn
gst_autoplugcache_nego_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstAutoplugCache *cache = GST_AUTOPLUGCACHE (GST_PAD_PARENT (pad));

  return gst_pad_negotiate_proxy (pad, cache->sinkpad, caps);
}

static GstPadNegotiateReturn
gst_autoplugcache_nego_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstAutoplugCache *cache = GST_AUTOPLUGCACHE (GST_PAD_PARENT (pad));

  return gst_pad_negotiate_proxy (pad, cache->srcpad, caps);
}


static GstElementStateReturn
gst_autoplugcache_change_state (GstElement *element)
{
  // FIXME this should do something like free the cache on ->NULL
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_autoplugcache_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAutoplugCache *cache;

  cache = GST_AUTOPLUGCACHE (object);

  switch (prop_id) {
    case ARG_CAPS_PROXY:
      cache->caps_proxy = g_value_get_boolean (value);
GST_DEBUG(0,"caps_proxy is %d\n",cache->caps_proxy);
      if (cache->caps_proxy) {
        gst_pad_set_negotiate_function (cache->sinkpad, GST_DEBUG_FUNCPTR(gst_autoplugcache_nego_sink));
        gst_pad_set_negotiate_function (cache->srcpad, GST_DEBUG_FUNCPTR(gst_autoplugcache_nego_src));
      } else {
        gst_pad_set_negotiate_function (cache->sinkpad, NULL);
        gst_pad_set_negotiate_function (cache->srcpad, NULL);
      }
      break;
    case ARG_RESET:
      // no idea why anyone would set this to FALSE, but just in case ;-)
      if (g_value_get_boolean (value)) {
        GST_DEBUG(0,"resetting playout pointer\n");
        // reset the playout pointer to the begining again
        cache->current_playout = cache->cache_start;
        // now we can fire a signal when the cache runs dry
        cache->fire_empty = TRUE;
        // also set it up to fire the first_buffer signal again
        cache->fire_first = TRUE;
      }
      break;
    default:
      break;
  }
}

static void
gst_autoplugcache_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAutoplugCache *cache;

  cache = GST_AUTOPLUGCACHE (object);

  switch (prop_id) {
    case ARG_BUFFER_COUNT:
      g_value_set_int (value, cache->buffer_count);
      break;
    case ARG_CAPS_PROXY:
      g_value_set_boolean (value, cache->caps_proxy);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new ("autoplugcache", GST_TYPE_AUTOPLUGCACHE,
                                    &gst_autoplugcache_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_plugin_add_factory (plugin, factory);

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "autoplugcache",
  plugin_init
};

