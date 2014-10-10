/* GstValidate
 * Copyright (C) 2014 Thibault Saunier <thibault.saunier@collabora.com>
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

#ifndef _GST_VALIDATE_TEST_UTILS
#define _GST_VALIDATE_TEST_UTILS

#include "test-utils.h"
#include <gst/check/gstcheck.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-pad-monitor.h>

G_BEGIN_DECLS

void check_destroyed (gpointer object_to_unref, gpointer first_object, ...) G_GNUC_NULL_TERMINATED;
GstValidateRunner * setup_runner (GstObject * object);
void clean_bus (GstElement *element);
GstValidatePadMonitor * get_pad_monitor (GstPad *pad);
GstElement * create_and_monitor_element (const gchar *factoryname, const gchar *name, GstValidateRunner *runner);
void free_element_monitor (GstElement *element);

typedef struct {
  GstElement parent;

  GstFlowReturn return_value;
} FakeDemuxer;

typedef struct {
  GstElementClass parent;
} FakeDemuxerClass;

#define FAKE_DEMUXER_TYPE (fake_demuxer_get_type ())
#define FAKE_DEMUXER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), FAKE_DEMUXER_TYPE, FakeDemuxer))
#define FAKE_DEMUXER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), FAKE_DEMUXER_TYPE, FakeDemuxerClass))
#define IS_FAKE_DEMUXER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FAKE_DEMUXER_TYPE))
#define IS_FAKE_DEMUXER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FAKE_DEMUXER_TYPE))
#define FAKE_DEMUXER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FAKE_DEMUXER_TYPE, FakeDemuxerClass))

GType fake_demuxer_get_type  (void);
GstElement * fake_demuxer_new (void);


G_END_DECLS

#endif /* _GST_VALIDATE_TEST_UTILS */
