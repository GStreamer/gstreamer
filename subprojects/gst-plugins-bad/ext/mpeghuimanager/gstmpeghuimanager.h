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
#include <gst/base/gstbasetransform.h>

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
  GstBaseTransform element;

  /* set under the object lock, so persistence_memory is not replaced while
   * the UI manager uses it */
  HANDLE_MPEGH_UI_MANAGER mpegh_ui_manager;

  /* protected by the object lock */
  /* XML of the latest scene, as in the last "mpegh-ui-config" message */
  gchar* latest_config;
  /* XML messages of "mpegh-ui-event" events not applied yet */
  GQueue ui_event_queue;
  /* "input-persistence": stored data set by the application, or NULL */
  GBytes* input_persistence;
  /* "output-persistence": copy of persistence_memory taken when it was last
   * brought up to date, or NULL before the first start */
  GBytes* output_persistence;
  /* memory used in place by the UI manager. Created in start() from
   * input_persistence or with the default size, and kept across runs until
   * "input-persistence" is set again. */
  guint8* persistence_memory;
  guint16 persistence_memory_size;

  /* number of frames in a row that could not be processed and were passed
   * through unchanged */
  guint consecutive_errors;
  /* the latest scene reported by the UI manager is a valid one; while it is
   * not, UI events it rejects are kept and retried */
  gboolean scene_valid;
  /* the UI manager is inactive and leaves frames unchanged, see
   * MPEGH_UI_NOT_ALLOWED */
  gboolean inactive;

  /* scratch buffer for the XML scene state */
  gchar* scene_state_buffer;
};

struct _GstMpeghUiManagerClass {
  GstBaseTransformClass parent_class;
};

GType gst_mpeghuimanager_get_type(void);

GST_ELEMENT_REGISTER_DECLARE (mpeghuimanager);

G_END_DECLS

#endif /* __GSTMPEGHUIMANAGER_H__ */
