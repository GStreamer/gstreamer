/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_QSG_MATERIAL_H__
#define __GST_QSG_MATERIAL_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>

#include "gstqtgl.h"
#include <QtQuick/QSGMaterial>
#include <QtQuick/QSGMaterialShader>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLShaderProgram>

class GstQSGMaterialShader;

class GstQSGMaterial : public QSGMaterial
{
protected:
    GstQSGMaterial();
    ~GstQSGMaterial();
public:
    static GstQSGMaterial *new_for_format (GstVideoFormat format);

    void setCaps (GstCaps * caps);
    gboolean setBuffer (GstBuffer * buffer);
    GstBuffer * getBuffer (gboolean * was_bound);
    bool compatibleWith(GstVideoInfo *v_info);

    void bind(GstQSGMaterialShader *, GstVideoFormat);

    /* QSGMaterial */
    QSGMaterialShader *createShader() const override;

private:
    GstBuffer * buffer_;
    gboolean buffer_was_bound;
    GstBuffer * sync_buffer_;
    GWeakRef qt_context_ref_;
    GstMemory * mem_;
    GstVideoInfo v_info;
    GstVideoFrame v_frame;
    float *cms_offset;
    float *cms_ycoeff;
    float *cms_ucoeff;
    float *cms_vcoeff;
    guint dummy_textures[GST_VIDEO_MAX_PLANES];
};

#endif /* __GST_QSG_MATERIAL_H__ */
