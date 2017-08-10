/*
 * GStreamer
 *
 * unit test for (audio) parser
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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

#include <gst/check/gstcheck.h>

#define MAX_HEADERS 10

typedef struct
{
  guint discard;
  guint buffers_before_offset_skip;
  guint offset_skip_amount;
  const guint8 *data_to_verify;
  guint data_to_verify_size;
  GstCaps *caps;
  gboolean no_metadata;

  GstClockTime ts_counter;
  gint64 offset_counter;
  guint buffer_counter;
} buffer_verify_data_s;

typedef struct {
    guint8     *data;
    guint       size;
} datablob;

typedef gboolean (*VerifyBuffer) (buffer_verify_data_s * vdata, GstBuffer * buf);
typedef GstElement* (*ElementSetup) (const gchar * desc);

/* context state variables; to be set by test using this helper */
/* mandatory */
GST_EXPORT const gchar *ctx_factory;
GST_EXPORT GstStaticPadTemplate *ctx_sink_template;
GST_EXPORT GstStaticPadTemplate *ctx_src_template;
/* optional */
GST_EXPORT GstCaps *ctx_input_caps;
GST_EXPORT GstCaps *ctx_output_caps;
GST_EXPORT guint ctx_discard;
GST_EXPORT datablob ctx_headers[MAX_HEADERS];
GST_EXPORT gboolean ctx_no_metadata;

GST_EXPORT VerifyBuffer ctx_verify_buffer;
GST_EXPORT ElementSetup ctx_setup;
GST_EXPORT gboolean ctx_frame_generated;

/* no refs taken/kept, all up to caller */
typedef struct
{
  const gchar          *factory;
  ElementSetup         factory_setup;
  GstStaticPadTemplate *sink_template;
  GstStaticPadTemplate *src_template;
  /* caps that go into element */
  GstCaps              *src_caps;
  /* optional: output caps to verify */
  GstCaps              *sink_caps;
  /* initial headers */
  datablob              headers[MAX_HEADERS];
  /* initial (header) output to forego checking */
  guint                 discard;
  /* series of buffers; middle series considered garbage */
  struct {
    /* data and size */
    guint8     *data;
    guint      size;
    /* num of frames with above data per buffer */
    guint      fpb;
    /* num of buffers */
    guint      num;
  } series[3];
  /* sigh, weird cases */
  gboolean              framed;
  guint                 dropped;
  gboolean              no_metadata;
} GstParserTest;

GST_EXPORT
void gst_parser_test_init (GstParserTest * ptest, guint8 * data, guint size, guint num);

GST_EXPORT
void gst_parser_test_run (GstParserTest * test, GstCaps ** out_caps);

GST_EXPORT
void gst_parser_test_normal (guint8 *data, guint size);

GST_EXPORT
void gst_parser_test_drain_single (guint8 *data, guint size);

GST_EXPORT
void gst_parser_test_drain_garbage (guint8 *data, guint size, guint8 *garbage, guint gsize);

GST_EXPORT
void gst_parser_test_split (guint8 *data, guint size);

GST_EXPORT
void gst_parser_test_skip_garbage (guint8 *data, guint size, guint8 *garbage, guint gsize);

GST_EXPORT
void gst_parser_test_output_caps (guint8 *data, guint size, const gchar * input_caps,
                                  const gchar * output_caps);

GST_EXPORT
GstCaps *gst_parser_test_get_output_caps (guint8 *data, guint size, const gchar * input_caps);

