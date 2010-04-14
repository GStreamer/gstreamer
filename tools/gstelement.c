% ClassName
GstElement
% TYPE_CLASS_NAME
GST_TYPE_ELEMENT
% pkg-config
gstreamer-0.10
% includes
#include <gst/gst.h>
% prototypes
static GstPad *gst_replace_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_replace_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_replace_get_state (GstElement * element, GstState * state,
    GstState * pending, GstClockTime timeout);
static GstStateChangeReturn
gst_replace_set_state (GstElement * element, GstState state);
static GstStateChangeReturn
gst_replace_change_state (GstElement * element, GstStateChange transition);
static void gst_replace_set_bus (GstElement * element, GstBus * bus);
static GstClock *gst_replace_provide_clock (GstElement * element);
static gboolean gst_replace_set_clock (GstElement * element, GstClock * clock);
static GstIndex *gst_replace_get_index (GstElement * element);
static void gst_replace_set_index (GstElement * element, GstIndex * index);
static gboolean gst_replace_send_event (GstElement * element, GstEvent * event);
static const GstQueryType *gst_replace_get_query_types (GstElement * element);
static gboolean gst_replace_query (GstElement * element, GstQuery * query);
% declare-class
  GstElement *element_class = GST_ELEMENT (klass);
% set-methods
  element_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods


static GstPad *
gst_replace_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{

  return NULL;
}

static void
gst_replace_release_pad (GstElement * element, GstPad * pad)
{

}

static GstStateChangeReturn
gst_replace_get_state (GstElement * element, GstState * state,
    GstState * pending, GstClockTime timeout)
{

  return GST_STATE_CHANGE_OK;
}

static GstStateChangeReturn
gst_replace_set_state (GstElement * element, GstState state)
{

  return GST_STATE_CHANGE_OK;
}

static GstStateChangeReturn
gst_replace_change_state (GstElement * element, GstStateChange transition)
{

  return GST_STATE_CHANGE_OK;
}

static void
gst_replace_set_bus (GstElement * element, GstBus * bus)
{

}

static GstClock *
gst_replace_provide_clock (GstElement * element)
{

}

static gboolean
gst_replace_set_clock (GstElement * element, GstClock * clock)
{

}

static GstIndex *
gst_replace_get_index (GstElement * element)
{

}

static void
gst_replace_set_index (GstElement * element, GstIndex * index)
{

}

static gboolean
gst_replace_send_event (GstElement * element, GstEvent * event)
{

}

static const GstQueryType *
gst_replace_get_query_types (GstElement * element)
{

}

static gboolean
gst_replace_query (GstElement * element, GstQuery * query)
{

}
% end
