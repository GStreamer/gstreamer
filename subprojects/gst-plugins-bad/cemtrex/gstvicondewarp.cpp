/*
 * Copyright (C) 2022, Fluendo S.A.
 * support@fluendo.com
 */
#include "config.h"
#include <iostream>
#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include "gstvicondewarp.h"

GST_DEBUG_CATEGORY_STATIC(gst_vicondewarp_debug);
#define GST_CAT_DEFAULT gst_vicondewarp_debug

void saveToPNG(const char* fileName, GstCaps* from_caps, GstBuffer* buf)
{
	GstMapInfo map_png;
	auto* snapshot_caps = gst_caps_from_string("image/png");
	auto* from_sample = gst_sample_new(buf, from_caps, NULL, NULL);
	auto* to_sample = gst_video_convert_sample(from_sample, snapshot_caps, GST_CLOCK_TIME_NONE, NULL);
	auto* buffer = gst_sample_get_buffer(to_sample);

	gst_buffer_map(buffer, &map_png, GST_MAP_READ);
	g_file_set_contents(fileName, (const gchar*)map_png.data, map_png.size, NULL);

	gst_caps_unref(snapshot_caps);
	gst_sample_unref(from_sample);
	gst_buffer_unmap(buffer, &map_png);
	gst_buffer_unref(buffer);
	gst_sample_unref(to_sample);
}

DewarpPlugin::DewarpPlugin() : m_mountPos{ 0 }, m_viewType{ 0 }, m_data{ 0 }, m_onOff{ false }, m_camera{ new IMV_CameraInterface() },
m_in{ new IMV_Buffer() }, m_out{ new IMV_Buffer() }, m_isCameraSetup{ false }
{
	GST_DEBUG_CATEGORY_INIT(gst_vicondewarp_debug, "vicon_dewarp", 0, "viconMsg");
	m_camera->SetACS(nullptr);
}

DewarpPlugin::~DewarpPlugin()
{
}

bool DewarpPlugin::setUpCamera(std::string format, int width, int height, unsigned char* bufferIn, unsigned char* bufferOut)
{
	auto colorFormat = IMV_Defs::E_YUV_NV12_STD | IMV_Defs::E_OBUF_TOPBOTTOM;

	if (format == "RGBA") {
		colorFormat = IMV_Defs::E_RGBA_32_STD | IMV_Defs::E_OBUF_TOPBOTTOM;
	}

	m_in->data = bufferIn;
	m_in->frameWidth = width;
	m_in->frameHeight = height;
	m_in->frameX = 0;
	m_in->frameY = 0;
	m_in->width = width;
	m_in->height = height;

	m_out->data = bufferOut;
	m_out->frameWidth = width;
	m_out->frameHeight = height;
	m_out->frameX = 0;
	m_out->frameY = 0;
	m_out->width = width;
	m_out->height = height;

	m_camera->SetLens((char*)m_lensName.c_str());

	auto result = m_camera->SetVideoParams(m_in.get(), m_out.get(), colorFormat, m_viewType, m_mountPos);

	if (result == IMV_Defs::E_ERR_OK) {
		auto* acsInfo = m_camera->GetACS();
		GST_INFO("The calibration value is %s", acsInfo);
		m_camera->SetACS(acsInfo);
		setPosition();
		m_camera->SetZoomLimits(24, 180);
		m_isCameraSetup = true;
	}
	else {
		//char acsInfo[] = "BAYJDA9WWlobPmxmJyzLcojjC35xyQaG";
		switch (result) {
		case IMV_Defs::E_ERR_NOTPANOMORPH:
			m_isCameraSetup = false;
			//m_camera->SetACS(acsInfo);
			break;
		}
	}

	return m_isCameraSetup;
}

void DewarpPlugin::setPosition()
{
	switch (m_viewType)
	{
	case IMV_Defs::E_VTYPE_QUAD:

		for (int i = 0; i < 4; i++) {
			auto result = m_camera->SetPosition(&(m_data[i].m_pan), &(m_data[i].m_tilt), &(m_data[i].m_roll), &(m_data[i].m_zoom), IMV_Defs::E_COOR_ABSOLUTE, i+1);
			if (result != IMV_Defs::E_ERR_OK)
			{
				GST_ERROR("We have some problem setting the position %d values", i+1);
			}
		}
		break;
	case IMV_Defs::E_VTYPE_PERI:
	case IMV_Defs::E_VTYPE_PERI_CUSTOM:
		if (m_mountPos == IMV_Defs::E_CPOS_GROUND || m_mountPos == IMV_Defs::E_CPOS_CEILING) {
			m_camera->SetPosition(&(m_data[0].m_pan), &(m_data[0].m_tilt), &(m_data[0].m_zoom));
		}
		break;
	case IMV_Defs::E_VTYPE_PTZ:
	case IMV_Defs::E_VTYPE_VERTICAL_SELFIE:
		m_camera->SetPosition(&(m_data[0].m_pan), &(m_data[0].m_tilt), &(m_data[0].m_zoom));
		break;

	}
}

void DewarpPlugin::setProperties(const GstStructure* properties)
{
	char panKey[] = "view_1_pan";
	char tiltKey[] = "view_1_tilt";
	char zoomKey[] = "view_1_zoom";
	char rollKey[] = "view_1_roll";

	for (int i = 0; i < 4; i++) {
		panKey[5] = tiltKey[5] = zoomKey[5] = rollKey[5] = '1' + i;
		m_data[i].m_pan = g_value_get_float(gst_structure_get_value(properties, panKey));
		m_data[i].m_tilt = g_value_get_float(gst_structure_get_value(properties, tiltKey));
		m_data[i].m_zoom = g_value_get_float(gst_structure_get_value(properties, zoomKey));
		m_data[i].m_roll = g_value_get_float(gst_structure_get_value(properties, rollKey));
	}

	setPosition();
}

void DewarpPlugin::setOnOff(bool onOff)
{
	m_onOff = onOff;
}

void DewarpPlugin::setMountPos(int mountPos)
{
	m_mountPos = mountPos;
}

void DewarpPlugin::setViewType(int viewType)
{
	if (m_viewType != viewType) {
		m_isCameraSetup = false;
	}
	m_viewType = viewType;
}

void DewarpPlugin::setLensName(const char* lensName)
{
	m_lensName = lensName;
}

bool DewarpPlugin::getOnOff()
{
	return m_onOff;
}

int DewarpPlugin::getMountPos()
{
	return m_mountPos;
}

int DewarpPlugin::getViewType()
{
	return m_viewType;
}

std::string DewarpPlugin::getLensName()
{
	return m_lensName;
}

bool DewarpPlugin::chain(GstPad* pad, GstCaps* inputCaps, GstBuffer* inputBuffer)
{
	if (!m_onOff) {
		return false;
	}

#ifdef VICON_SAVE_TO_PNG
	// Save to png
	saveToPNG("vicon_dewarp_input_frame.png", inputCaps, inputBuffer);
#endif

	GstMapInfo inputMap, outputMap;
	int width, height;

	if (gst_buffer_map(inputBuffer, &inputMap, (GstMapFlags)(GST_MAP_READ)) == FALSE) {
		return false;
	}

	auto caps = gst_pad_get_current_caps(pad);
	if (caps == nullptr) {
		return false;
	}

	auto* capsInfo = gst_caps_get_structure(caps, 0);
	if (capsInfo == nullptr) {
		gst_caps_unref(caps);
		return false;
	}

	std::string frmt = gst_structure_get_string(capsInfo, "format");
	auto res = gst_structure_get_int(capsInfo, "width", &width);
	res |= gst_structure_get_int(capsInfo, "height", &height);

	gst_caps_unref(caps);

	if (!res) {
		return false;
	}

	auto* outputBuffer = gst_buffer_new_allocate(NULL, inputMap.size, NULL);

	if (outputBuffer == nullptr) {
		return false;
	}

	if (gst_buffer_map(outputBuffer, &outputMap, (GstMapFlags)(GST_MAP_WRITE)) == FALSE) {
		gst_buffer_unref(outputBuffer);
		return false;
	}

	if (m_isCameraSetup == false)
	{
		if (setUpCamera(frmt, width, height, inputMap.data, outputMap.data) == false) {
			gst_buffer_unmap(outputBuffer, &outputMap);
			gst_buffer_unref(outputBuffer);
			gst_buffer_unmap(inputBuffer, &inputMap);
			return false;
		}
	}
	else
	{
		m_out->data = outputMap.data;
		m_in->data = inputMap.data;
	}

	auto ret = m_camera->Update();

	if (ret != IMV_Defs::E_ERR_OK) {
		GST_ERROR("Error to updating output buffer");
	}

#ifdef VICON_SAVE_TO_PNG
	saveToPNG("output_frame.png", inputCaps, outputBuffer);
#endif

	outputBuffer->pts = inputBuffer->pts;
	outputBuffer->dts = inputBuffer->dts;

	gst_buffer_unmap(outputBuffer, &outputMap);
	gst_buffer_unmap(inputBuffer, &inputMap);
	gst_buffer_unref(inputBuffer);

	gst_pad_push(pad, outputBuffer);

	return true;
}

/* thiz signals and args */
enum
{
	/* FILL ME */
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_SILENT,
	PROP_STATUS,
	PROP_MOUNTPOS,
	PROP_VIEWTYPE,
	PROP_LENSNAME,
	PROP_DEWARP_PROPERTIES
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS("ANY")
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS("ANY")
);

#define gst_vicondewarp_parent_class parent_class
G_DEFINE_TYPE(Gstvicondewarp, gst_vicondewarp, GST_TYPE_ELEMENT);

GST_ELEMENT_REGISTER_DEFINE(vicondewarp, "vicondewarp", GST_RANK_NONE, GST_TYPE_VICONDEWARP);

static void gst_vicondewarp_finalize(GObject* object);
static void gst_vicondewarp_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec);
static void gst_vicondewarp_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec);

static gboolean gst_vicondewarp_sink_event(GstPad* pad, GstObject* parent, GstEvent* event);
static GstFlowReturn gst_vicondewarp_chain(GstPad* pad, GstObject* parent, GstBuffer* buf);

/* GObject vmethod implementations */

/* initialize the vicondewarp's class */
static void
gst_vicondewarp_class_init(GstvicondewarpClass* klass)
{
	GObjectClass* gobject_class;
	GstElementClass* gstelement_class;

	gobject_class = (GObjectClass*)klass;
	gstelement_class = (GstElementClass*)klass;

	gobject_class->finalize = gst_vicondewarp_finalize;
	gobject_class->set_property = gst_vicondewarp_set_property;
	gobject_class->get_property = gst_vicondewarp_get_property;

	g_object_class_install_property(gobject_class, PROP_SILENT,
		g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
			FALSE, G_PARAM_READWRITE));
	g_object_class_install_property(gobject_class, PROP_STATUS,
		g_param_spec_boolean("dewarpstate", "Dewarpstate", "Enable or Disable dewarp",
			FALSE, G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, PROP_MOUNTPOS,
		g_param_spec_int("mountpos", "Mountpos", "Mount Position", 0, 4,
			0, G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, PROP_VIEWTYPE,
		g_param_spec_int("viewtype", "Viewtype", "View Type", 0, 5,
			0, G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, PROP_LENSNAME,
		g_param_spec_string("lensname", "Lensname", "Lens Name", "A0**V", G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, PROP_DEWARP_PROPERTIES,
		g_param_spec_boxed("dewarp-properties", "Dewarp properties",
			"List of dewarp properties",
			GST_TYPE_STRUCTURE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

	gst_element_class_set_details_simple(gstelement_class,
		"vicondewarp",
		"Filter",
		"Dewarps the 360 camera feed", "Jithin Nair jnair@cemtrexlabs.com");

	gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
	gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_vicondewarp_init(Gstvicondewarp* thiz)
{
	thiz->plugin = new DewarpPlugin();
	thiz->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");

	gst_pad_set_event_function(thiz->sinkpad, GST_DEBUG_FUNCPTR(gst_vicondewarp_sink_event));
	gst_pad_set_chain_function(thiz->sinkpad, GST_DEBUG_FUNCPTR(gst_vicondewarp_chain));

	GST_PAD_SET_PROXY_CAPS(thiz->sinkpad);
	gst_element_add_pad(GST_ELEMENT(thiz), thiz->sinkpad);

	thiz->srcpad = gst_pad_new_from_static_template(&src_factory, "src");

	GST_PAD_SET_PROXY_CAPS(thiz->srcpad);
	gst_element_add_pad(GST_ELEMENT(thiz), thiz->srcpad);
}

static void
gst_vicondewarp_finalize(GObject* object)
{
	Gstvicondewarp* thiz = GST_VICONDEWARP(object);
	if (thiz->plugin != nullptr) {
		delete thiz->plugin;
	}
	if (thiz->properties != NULL) {
		gst_structure_free(thiz->properties);
	}
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_vicondewarp_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	Gstvicondewarp* thiz = GST_VICONDEWARP(object);

	switch (prop_id) {
	case PROP_SILENT:
		thiz->silent = g_value_get_boolean(value);
		break;
	case PROP_STATUS:
		thiz->plugin->setOnOff(g_value_get_boolean(value) != FALSE);
		break;
	case PROP_MOUNTPOS:
		thiz->plugin->setMountPos(g_value_get_int(value));
		break;
	case PROP_VIEWTYPE:
		thiz->plugin->setViewType(g_value_get_int(value));
		break;
	case PROP_DEWARP_PROPERTIES:
	{
		auto* pointer = gst_value_get_structure(value);
		thiz->plugin->setProperties(pointer);
		thiz->properties = pointer ? gst_structure_copy(pointer) : NULL;
		break;
	}
	case PROP_LENSNAME:
	{
		thiz->plugin->setLensName(g_value_get_string(value));
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
gst_vicondewarp_get_property(GObject* object, guint prop_id,
	GValue* value, GParamSpec* pspec)
{
	Gstvicondewarp* thiz = GST_VICONDEWARP(object);

	switch (prop_id) {
	case PROP_SILENT:
		g_value_set_boolean(value, thiz->silent);
		break;
	case PROP_STATUS:
		g_value_set_boolean(value, thiz->plugin->getOnOff());
		break;
	case PROP_MOUNTPOS:
		g_value_set_int(value, thiz->plugin->getMountPos());
		break;
	case PROP_VIEWTYPE:
		g_value_set_int(value, thiz->plugin->getViewType());
		break;
	case PROP_LENSNAME:
		g_value_set_string(value, thiz->plugin->getLensName().c_str());
		break;
	case PROP_DEWARP_PROPERTIES:
		gst_value_set_structure(value, thiz->properties);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_vicondewarp_sink_event(GstPad* pad, GstObject* parent,
	GstEvent* event)
{
	Gstvicondewarp* thiz;
	gboolean ret;

	thiz = GST_VICONDEWARP(parent);

	GST_LOG_OBJECT(thiz, "Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event), event);

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_CAPS:
	{
		GstCaps* caps;
		gst_event_parse_caps(event, &caps);
		if (thiz->inputCaps != nullptr) {
			gst_caps_unref(thiz->inputCaps);
		}
		thiz->inputCaps = gst_caps_copy(caps);
		ret = gst_pad_event_default(pad, parent, event);
		break;
	}
	default:
		ret = gst_pad_event_default(pad, parent, event);
		break;
	}
	return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_vicondewarp_chain(GstPad* pad, GstObject* parent, GstBuffer* buffer)
{
	auto* thiz = GST_VICONDEWARP(parent);

	if (thiz != nullptr && thiz->plugin != nullptr) {
		if (thiz->plugin->chain(thiz->srcpad, thiz->inputCaps, buffer)) {
			return GST_FLOW_OK;
		}
	}

	gst_pad_push(thiz->srcpad, buffer);

	return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
vicondewarp_init(GstPlugin* vicondewarp)
{
	/* debug category for thizing log messages
	 *
	 * exchange the string 'Template vicondewarp' with your description
	 */
	GST_DEBUG_CATEGORY_INIT(gst_vicondewarp_debug, "vicondewarp", 0, "Dewarp vicondewarp");
	return GST_ELEMENT_REGISTER(vicondewarp, vicondewarp);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstvicondewarp"
#endif

 /* gstreamer looks for this structure to register vicondewarps
  *
  * exchange the string 'Template vicondewarp' with your vicondewarp description
  */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	vicondewarp,
	"vicondewarp",
	vicondewarp_init,
	PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
