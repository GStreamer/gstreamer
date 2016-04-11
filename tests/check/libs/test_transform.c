
#include <gst/base/gstbasetransform.h>

typedef struct
{
  GstPad *srcpad;
  GstPad *sinkpad;
  GList *events;
  GList *buffers;
  GstElement *trans;
  GstBaseTransformClass *klass;
} TestTransData;

static GstStaticPadTemplate gst_test_trans_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("foo/x-bar")
    );

static GstStaticPadTemplate gst_test_trans_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("foo/x-bar")
    );

typedef struct _GstTestTrans GstTestTrans;
typedef struct _GstTestTransClass GstTestTransClass;

#define GST_TEST_TRANS(obj) ((GstTestTrans *)(obj))

struct _GstTestTrans
{
  GstBaseTransform element;

  TestTransData *data;
};

struct _GstTestTransClass
{
  GstBaseTransformClass parent_class;
};

static GstFlowReturn (*klass_transform) (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf) = NULL;
static GstFlowReturn (*klass_transform_ip) (GstBaseTransform * trans,
    GstBuffer * buf) = NULL;
static gboolean (*klass_set_caps) (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps) = NULL;
static GstCaps *(*klass_transform_caps) (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter) = NULL;
static gboolean (*klass_transform_size) (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps,
    gsize * othersize) = NULL;
static gboolean klass_passthrough_on_same_caps = FALSE;
GstFlowReturn (*klass_submit_input_buffer) (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * input) = NULL;
GstFlowReturn (*klass_generate_output) (GstBaseTransform * trans,
    GstBuffer ** outbuf) = NULL;

static GstStaticPadTemplate *sink_template = &gst_test_trans_sink_template;
static GstStaticPadTemplate *src_template = &gst_test_trans_src_template;

static void
gst_test_trans_class_init (GstTestTransClass * klass)
{
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;

  element_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gst_element_class_set_metadata (element_class, "TestTrans",
      "Filter/Test", "Test transform", "Wim Taymans <wim.taymans@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, sink_template);
  gst_element_class_add_static_pad_template (element_class, src_template);

  GST_INFO ("setting up %s", g_type_name (((GTypeClass *) klass)->g_type));

  trans_class->passthrough_on_same_caps = klass_passthrough_on_same_caps;
  if (klass_transform_ip != NULL)
    trans_class->transform_ip = klass_transform_ip;
  if (klass_transform != NULL)
    trans_class->transform = klass_transform;
  if (klass_transform_caps != NULL)
    trans_class->transform_caps = klass_transform_caps;
  if (klass_transform_size != NULL)
    trans_class->transform_size = klass_transform_size;
  if (klass_set_caps != NULL)
    trans_class->set_caps = klass_set_caps;
  if (klass_submit_input_buffer != NULL)
    trans_class->submit_input_buffer = klass_submit_input_buffer;
  if (klass_generate_output)
    trans_class->generate_output = klass_generate_output;
}

static void
gst_test_trans_init (GstTestTrans * this)
{
}

static void
gst_test_trans_set_data (GstTestTrans * this, TestTransData * data)
{
  this->data = data;
}

static GstFlowReturn
result_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  TestTransData *data;

  data = gst_pad_get_element_private (pad);

  data->buffers = g_list_append (data->buffers, buffer);

  return GST_FLOW_OK;
}

#if 0
static GstFlowReturn
result_buffer_alloc (GstPad * pad, guint64 offset, guint size, GstCaps * caps,
    GstBuffer ** buf)
{
  GstFlowReturn res;
  TestTransData *data;

  data = gst_pad_get_element_private (pad);

  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, caps);
  res = GST_FLOW_OK;

  return res;
}
#endif

static TestTransData *
gst_test_trans_new (void)
{
  TestTransData *res;
  GstPad *tmp;
  GstPadTemplate *templ;
  GType type;

  /* we register a new sub-class for every test-run, so the class init
   * function is called for every test run and can be set up properly
   * even with CK_FORK=no */
  {
    static gint counter = 0;
    gchar name[100];

    g_snprintf (name, sizeof (name), "GstTestTrans%d", ++counter);

    type = g_type_register_static_simple (GST_TYPE_BASE_TRANSFORM, name,
        sizeof (GstTestTransClass), (GClassInitFunc) gst_test_trans_class_init,
        sizeof (GstTestTrans), (GInstanceInitFunc) gst_test_trans_init, 0);
  }

  res = g_new0 (TestTransData, 1);
  res->trans = g_object_new (type, NULL);

  templ = gst_static_pad_template_get (sink_template);
  templ->direction = GST_PAD_SRC;
  res->srcpad = gst_pad_new_from_template (templ, "src");
  gst_object_unref (templ);

  templ = gst_static_pad_template_get (src_template);
  templ->direction = GST_PAD_SINK;
  res->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_object_unref (templ);

  res->klass = GST_BASE_TRANSFORM_GET_CLASS (res->trans);

  gst_test_trans_set_data (GST_TEST_TRANS (res->trans), res);
  gst_pad_set_element_private (res->sinkpad, res);

  gst_pad_set_chain_function (res->sinkpad, result_sink_chain);

  tmp = gst_element_get_static_pad (res->trans, "sink");
  gst_pad_link (res->srcpad, tmp);
  gst_object_unref (tmp);

  tmp = gst_element_get_static_pad (res->trans, "src");
  gst_pad_link (tmp, res->sinkpad);
  gst_object_unref (tmp);

  gst_pad_set_active (res->sinkpad, TRUE);
  gst_element_set_state (res->trans, GST_STATE_PAUSED);
  gst_pad_set_active (res->srcpad, TRUE);

  gst_pad_push_event (res->srcpad, gst_event_new_stream_start ("test"));

  return res;
}

static void
gst_test_trans_free (TestTransData * data)
{
  GstPad *tmp;

  gst_pad_set_active (data->sinkpad, FALSE);
  gst_element_set_state (data->trans, GST_STATE_NULL);
  gst_pad_set_active (data->srcpad, FALSE);

  tmp = gst_element_get_static_pad (data->trans, "src");
  gst_pad_unlink (tmp, data->sinkpad);
  gst_object_unref (tmp);

  tmp = gst_element_get_static_pad (data->trans, "sink");
  gst_pad_link (data->srcpad, tmp);
  gst_object_unref (tmp);

  gst_object_unref (data->srcpad);
  gst_object_unref (data->sinkpad);
  gst_object_unref (data->trans);

  g_free (data);
}

static GstFlowReturn
gst_test_trans_push (TestTransData * data, GstBuffer * buffer)
{
  GstFlowReturn ret;

  ret = gst_pad_push (data->srcpad, buffer);

  return ret;
}

static GstBuffer *
gst_test_trans_pop (TestTransData * data)
{
  GstBuffer *ret;

  if (data->buffers) {
    ret = data->buffers->data;
    data->buffers = g_list_delete_link (data->buffers, data->buffers);
  } else {
    ret = NULL;
  }
  return ret;
}

static gboolean
gst_test_trans_setcaps (TestTransData * data, GstCaps * caps)
{
  return gst_pad_set_caps (data->srcpad, caps);
}

static gboolean
gst_test_trans_push_segment (TestTransData * data)
{
  GstSegment segment;

  gst_segment_init (&segment, GST_FORMAT_TIME);

  return gst_pad_push_event (data->srcpad, gst_event_new_segment (&segment));
}
