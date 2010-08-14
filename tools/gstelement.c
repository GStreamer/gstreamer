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
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
% set-methods
  element_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_replace_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_replace_release_pad);
  element_class->get_state = GST_DEBUG_FUNCPTR (gst_replace_get_state);
  element_class->set_state = GST_DEBUG_FUNCPTR (gst_replace_set_state);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_replace_change_state);
  element_class->set_bus = GST_DEBUG_FUNCPTR (gst_replace_set_bus);
  element_class->provide_clock = GST_DEBUG_FUNCPTR (gst_replace_provide_clock);
  element_class->set_clock = GST_DEBUG_FUNCPTR (gst_replace_set_clock);
  element_class->get_index = GST_DEBUG_FUNCPTR (gst_replace_get_index);
  element_class->set_index = GST_DEBUG_FUNCPTR (gst_replace_set_index);
  element_class->send_event = GST_DEBUG_FUNCPTR (gst_replace_send_event);
  element_class->get_query_types = GST_DEBUG_FUNCPTR (gst_replace_get_query_types);
  element_class->query = GST_DEBUG_FUNCPTR (gst_replace_query);
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

  return GST_STATE_CHANGE_SUCCESS;
}

static GstStateChangeReturn
gst_replace_set_state (GstElement * element, GstState state)
{

  return GST_STATE_CHANGE_SUCCESS;
}

static GstStateChangeReturn
gst_replace_change_state (GstElement * element, GstStateChange transition)
{

  return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_replace_set_bus (GstElement * element, GstBus * bus)
{

}

static GstClock *
gst_replace_provide_clock (GstElement * element)
{

  return NULL;
}

static gboolean
gst_replace_set_clock (GstElement * element, GstClock * clock)
{

  return TRUE;
}

static GstIndex *
gst_replace_get_index (GstElement * element)
{

  return NULL;
}

static void
gst_replace_set_index (GstElement * element, GstIndex * index)
{

}

static gboolean
gst_replace_send_event (GstElement * element, GstEvent * event)
{

  return TRUE;
}

static const GstQueryType *
gst_replace_get_query_types (GstElement * element)
{

  return NULL;
}

static gboolean
gst_replace_query (GstElement * element, GstQuery * query)
{

  return FALSE;
}
% end
