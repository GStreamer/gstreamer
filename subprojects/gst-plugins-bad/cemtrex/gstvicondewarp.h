/*
 * Copyright (C) 2022, Fluendo S.A.
 * support@fluendo.com
 */
#pragma once

#include <gst/gst.h>
#include <memory>
#include "IMV1.h"

class DewarpPlugin
{
public:
	explicit DewarpPlugin();
	~DewarpPlugin();
	bool setUpCamera(std::string format, int width, int height, unsigned char* bufferIn, unsigned char* bufferOut);
	bool chain(GstPad* pad, GstCaps* inputCaps, GstBuffer* buffer);
	void setPosition();

	void setProperties(const GstStructure* properties);
	void setOnOff(bool status);
	void setMountPos(int mountPos);
	void setViewType(int viewType);
	void setLensName(const char* lensName);

	bool getOnOff();
	int getMountPos();
	int getViewType();
	std::string getLensName();


private:
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

	bool m_onOff;

	std::unique_ptr<IMV_CameraInterface> m_camera;
	std::unique_ptr<IMV_Buffer> m_in;
	std::unique_ptr<IMV_Buffer> m_out;

	bool m_isCameraSetup;

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
