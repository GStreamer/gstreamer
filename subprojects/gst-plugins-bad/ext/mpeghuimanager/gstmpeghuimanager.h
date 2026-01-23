/*
 * Copyright (C) 2026 Fraunhofer Institute for Integrated Circuits IIS
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

#ifndef __GSTMPEGHUIMANAGER_H__
#define __GSTMPEGHUIMANAGER_H__

#include <gst/gst.h>

#include <mpeghdec/mpeghUIManager.h>

G_BEGIN_DECLS

#define GST_TYPE_MPEGHUIMANAGER (gst_mpeghuimanager_get_type())
#define GST_MPEGHUIMANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_MPEGHUIMANAGER, GstMpeghUiManager))
#define GST_MPEGHUIMANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_MPEGHUIMANAGER, GstMpeghUiManagerClass))
#define GST_IS_MPEGHUIMANAGER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_MPEGHUIMANAGER))
#define GST_IS_MPEGHUIMANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_MPEGHUIMANAGER))

typedef struct _GstMpeghUiManager GstMpeghUiManager;
typedef struct _GstMpeghUiManagerClass GstMpeghUiManagerClass;

struct _GstMpeghUiManager {
  GstElement element;

  GstPad* sink_pad;
  GstPad* src_pad;

  HANDLE_MPEGH_UI_MANAGER mpegh_ui_manager;

  GString* latest_config;

  GString* persistence_filename;
  gchar* persistence_memory;

  GQueue* ui_event_queue;
};

struct _GstMpeghUiManagerClass {
  GstElementClass parent_class;
};

GType gst_mpeghuimanager_get_type(void);

GST_ELEMENT_REGISTER_DECLARE (mpeghuimanager);

G_END_DECLS

#endif /* __GSTMPEGHUIMANAGER_H__ */
