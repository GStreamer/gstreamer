/*
 * Copyright (C) 2022, Fluendo S.A.
 * support@fluendo.com
 */
#pragma once

#include <gst/gst.h>
#include <memory>
#include <mutex>
#include "IMV1.h"


class InputOutputBuffers
{
public:
	explicit InputOutputBuffers();
	~InputOutputBuffers();

	void reset();

	bool setInputBuffer(GstBuffer* inputBuffer, int width, int height);

	IMV_Buffer* in();
	IMV_Buffer* out();

	int width();
	int height();

	GstBuffer* outputTransferFull();

private:
	std::unique_ptr<IMV_Buffer> m_in;
	std::unique_ptr<IMV_Buffer> m_out;
	GstMapInfo m_inputMap;
	GstMapInfo m_outputMap;
	GstBuffer* m_outputBuffer;
	int m_width;
	int m_height;
};

class DewarpPlugin
{
public:
	explicit DewarpPlugin();
	~DewarpPlugin();
	bool chain(GstPad* pad, GstCaps* inputCaps, GstBuffer* buffer);
	void setPosition();

	void setProperties(const GstStructure* properties);
	void setMountPos(int mountPos);
	void setViewType(int viewType);
	void setLensName(const char* lensName);

	bool getOnOff();
	int getMountPos();
	int getViewType();
	std::string getLensName();


private:
	bool setUpCamera(std::string format, int width, int height, GstBuffer* originalInputBuffer);
	bool calibrateLens(std::string format, int width, int height, GstCaps* caps, GstBuffer* originalInputBuffer);

	std::string m_lensName;
	int m_mountPos;
	int m_viewType;
	struct internal
	{
		float m_pan;
		float m_tilt;
		float m_roll;
		float m_zoom;
	} m_data[4];

	std::unique_ptr<IMV_CameraInterface> m_camera;
	InputOutputBuffers m_buffers;
	std::string m_acsInfo;
	bool m_isCameraSetup;
	bool m_isLensCalibrated;

	std::mutex m_render;

};

G_BEGIN_DECLS

#define GST_TYPE_VICONDEWARP (gst_vicondewarp_get_type())
G_DECLARE_FINAL_TYPE(Gstvicondewarp, gst_vicondewarp, GST, VICONDEWARP, GstElement)

struct _Gstvicondewarp
{
	GstElement element;

	GstPad* sinkpad, * srcpad;

	gboolean silent;

	GstStructure* properties;

	GstCaps* inputCaps;

	DewarpPlugin* plugin;
};

G_END_DECLS
