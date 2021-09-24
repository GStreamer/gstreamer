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

typedef struct {
  GstElement parent;

  GstFlowReturn return_value;
} FakeDecoder;

typedef struct {
  GstElementClass parent;
} FakeDecoderClass;

#define FAKE_DECODER_TYPE (fake_decoder_get_type ())
#define FAKE_DECODER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), FAKE_DECODER_TYPE, FakeDecoder))
#define FAKE_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), FAKE_DECODER_TYPE, FakeDecoderClass))
#define IS_FAKE_DECODER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FAKE_DECODER_TYPE))
#define IS_FAKE_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FAKE_DECODER_TYPE))
#define FAKE_DECODER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FAKE_DECODER_TYPE, FakeDecoderClass))

GType fake_decoder_get_type  (void);
GstElement * fake_decoder_new (void);

typedef struct {
  GstElement parent;

  GstFlowReturn return_value;

  /* <private> */
  gboolean sent_stream_start;
  gboolean sent_segment;
} FakeMixer;

typedef struct {
  GstElementClass parent;
} FakeMixerClass;

#define FAKE_MIXER_TYPE (fake_mixer_get_type ())
#define FAKE_MIXER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), FAKE_MIXER_TYPE, FakeMixer))
#define FAKE_MIXER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), FAKE_MIXER_TYPE, FakeMixerClass))
#define IS_FAKE_MIXER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FAKE_MIXER_TYPE))
#define IS_FAKE_MIXER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FAKE_MIXER_TYPE))
#define FAKE_MIXER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FAKE_MIXER_TYPE, FakeMixerClass))

GType fake_mixer_get_type  (void);
GstElement * fake_mixer_new (void);

typedef struct {
  GstElement parent;

  GstFlowReturn return_value;

} FakeSrc;

typedef struct {
  GstElementClass parent;
} FakeSrcClass;

#define FAKE_SRC_TYPE (fake_src_get_type ())
#define FAKE_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), FAKE_SRC_TYPE, FakeSrc))
#define FAKE_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), FAKE_SRC_TYPE, FakeSrcClass))
#define IS_FAKE_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FAKE_SRC_TYPE))
#define IS_FAKE_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FAKE_SRC_TYPE))
#define FAKE_SRC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FAKE_SRC_TYPE, FakeSrcClass))

GType fake_src_get_type  (void);
GstElement * fake_src_new (void);

void fake_elements_register (void);

G_END_DECLS

#endif /* _GST_VALIDATE_TEST_UTILS */
