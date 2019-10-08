/*
 * test-fei-enc-out.c - FEI Encoder Test application to dump output buffers
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/* ./test-fei-enc -i sample_320x240.nv12 -f nv12 -w 320 -h 240 -o out.264 -v mv.out -d dist.out -m mbcode.out -e 1 */

#include <stdio.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdlib.h>
#include "../gst/vaapi/gstvaapifeivideometa.h"
#include <gst/video/video.h>

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *filesrc, *videoparse, *enc, *capsfilter, *appsink;
  GError *err = NULL;
  GstStateChangeReturn ret;
  GstSample *sample;
  GstVideoFormat raw_format = GST_VIDEO_FORMAT_NV12;
  GOptionContext *ctx;
  FILE *file = NULL;
  FILE *mv_file = NULL;
  FILE *dist_file = NULL;
  FILE *mbcode_file = NULL;
  FILE *fei_stat_file = NULL;
  gchar *input_file_name = NULL;
  gchar *output_file_name = NULL;
  gchar *output_mv_name = NULL;
  gchar *output_distortion_name = NULL;
  gchar *output_mbcode_name = NULL;
  gchar *input_format;
  guint input_width;
  guint input_height;
  guint enc_frame_num = 0;
  guint block_size = 0;
  guint fei_mode = 1;
  guint fei_mode_flag = 0x00000004;
  gboolean link_ok = FALSE;
  guint mv_buffer_size = 0;
  guint mbcode_buffer_size = 0;
  guint dist_buffer_size = 0;
  gpointer mapped_data = NULL;
  guint mapped_data_size = 0;
  const gchar *caps_string = "video/x-h264, profile=constrained-baseline";
  GstCaps *filter_caps = NULL;

  GOptionEntry options[] = {
    {"input file", 'i', 0, G_OPTION_ARG_STRING, &input_file_name,
        "file to encode", NULL},
    {"output file", 'o', 0, G_OPTION_ARG_STRING, &output_file_name,
        "encpak output file", NULL},
    {"output mv file", 'v', 0, G_OPTION_ARG_STRING, &output_mv_name,
        "encpak mv output file", NULL},
    {"output distortion file", 'd', 0, G_OPTION_ARG_STRING,
          &output_distortion_name,
        "encpak distortion output file", NULL},
    {"output mbcode file", 'm', 0, G_OPTION_ARG_STRING, &output_mbcode_name,
        "encpak mbcode output file", NULL},
    {"format", 'f', 0, G_OPTION_ARG_STRING, &input_format,
        "input raw format: nv12 or i420", NULL},
    {"width", 'w', 0, G_OPTION_ARG_INT, &input_width,
        "input stream width", NULL},
    {"height", 'h', 0, G_OPTION_ARG_INT, &input_height,
        "input stream height", NULL},
    {"frame-num", 'n', 0, G_OPTION_ARG_INT, &enc_frame_num,
        "numumber of buffers to be encoded", NULL},
    {"blocksize", 's', 0, G_OPTION_ARG_INT, &block_size,
        "single buffer size of input stream", NULL},
    {"fei-mode", 'e', 0, G_OPTION_ARG_INT, &fei_mode,
        "1: ENC_PAK 2: ENC+PAK", NULL},
    {NULL}
  };

  ctx =
      g_option_context_new
      ("encpak with element filesrc, videoparse, vaapih264feienc, appsink");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error intializing: %s\n", err->message);
    g_option_context_free (ctx);
    g_clear_error (&err);
    return -1;
  }

  if (input_file_name == NULL || output_file_name == NULL) {
    g_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    g_option_context_free (ctx);
    return -1;
  }

  if (!g_strcmp0 (input_format, "nv12"))
    raw_format = GST_VIDEO_FORMAT_NV12;
  else if (!g_strcmp0 (input_format, "i420"))
    raw_format = GST_VIDEO_FORMAT_I420;
  else
    return -1;

  if (!input_width || !input_height) {
    g_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    g_option_context_free (ctx);
    return -1;
  }

  switch (fei_mode) {
    case 1:
      fei_mode_flag = 0x00000004;
      break;
    case 2:
      fei_mode_flag = 0x00000001 | 0x00000002;
      break;
    default:
      printf ("Unknown fei mode \n");
      g_assert (0);
      break;
  }

  g_option_context_free (ctx);

  gst_init (&argc, &argv);

  /* create pipeline */
  pipeline = gst_pipeline_new ("pipeline");
  filesrc = gst_element_factory_make ("filesrc", "source");
  videoparse = gst_element_factory_make ("videoparse", "videoparse");
  enc = gst_element_factory_make ("vaapih264feienc", "encpak");
  capsfilter = gst_element_factory_make ("capsfilter", "enccaps");
  appsink = gst_element_factory_make ("appsink", "sink");

  /* element prop setup */
  g_object_set (G_OBJECT (filesrc), "location", input_file_name, NULL);
  g_object_set (G_OBJECT (videoparse), "format", raw_format,
      "width", input_width, "height", input_height, NULL);

  if (enc_frame_num != 0)
    g_object_set (G_OBJECT (filesrc), "num-buffers", enc_frame_num, NULL);
  if (block_size != 0)
    g_object_set (G_OBJECT (filesrc), "blocksize", block_size, NULL);

  g_object_set (G_OBJECT (enc), "fei-mode", fei_mode_flag, NULL);
  g_object_set (G_OBJECT (enc), "search-window", 5, NULL);
  g_object_set (G_OBJECT (enc), "max-bframes", 0, NULL);

  filter_caps = gst_caps_from_string (caps_string);
  if (filter_caps)
    g_object_set (G_OBJECT (capsfilter), "caps", filter_caps, NULL);
  gst_caps_unref (filter_caps);

  gst_bin_add_many (GST_BIN (pipeline), filesrc, videoparse, enc, capsfilter,
      appsink, NULL);

  link_ok =
      gst_element_link_many (filesrc, videoparse, enc, capsfilter, appsink,
      NULL);
  if (!link_ok) {
    g_print ("filesrc, enc and appsink link fail");
    return -1;
  }

  file = fopen (output_file_name, "wb");

  if (output_mv_name != NULL)
    mv_file = fopen (output_mv_name, "wb");

  if (output_mbcode_name != NULL)
    mbcode_file = fopen (output_mbcode_name, "wb");

  if (output_distortion_name != NULL)
    dist_file = fopen (output_distortion_name, "wb");

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* pull sample from pipeline */
  while (1) {
    g_signal_emit_by_name (appsink, "pull-sample", &sample, NULL);
    if (sample) {
      GstBuffer *buffer = NULL;
      GstMapInfo map, info;
      GstMemory *mem;
      GstVaapiFeiVideoMeta *meta = NULL;
      GstMeta *m = NULL;
      const GstMetaInfo *meta_info;
      GType api;

      g_debug ("appsink received sample.\n");
      buffer = gst_sample_get_buffer (sample);
      if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
        mem = gst_buffer_peek_memory (buffer, 0);
        if (gst_memory_map (mem, &info, GST_MAP_READ))
          fwrite (info.data, 1, info.size, file);

        gst_memory_unmap (mem, &info);
        gst_buffer_unmap (buffer, &map);
      }

      meta_info = gst_meta_get_info ("GstVaapiFeiVideoMeta");
      api = meta_info->api;
      m = gst_buffer_get_meta (buffer, api);
      if (m != NULL)
        meta = ((GstVaapiFeiVideoMetaHolder *) (m))->meta;

      if (meta != NULL) {

        if (mv_file != NULL) {
          mapped_data = NULL;
          mapped_data_size = 0;
          if (gst_vaapi_fei_codec_object_map (GST_VAAPI_FEI_CODEC_OBJECT
                  (meta->mv), &mapped_data, &mapped_data_size)) {
            fwrite (mapped_data, 1, mapped_data_size, mv_file);
            gst_vaapi_fei_codec_object_unmap (GST_VAAPI_FEI_CODEC_OBJECT
                (meta->mv));
            mv_buffer_size = mapped_data_size;
          }
        }

        if (mbcode_file != NULL) {
          mapped_data = NULL;
          mapped_data_size = 0;
          if (gst_vaapi_fei_codec_object_map (GST_VAAPI_FEI_CODEC_OBJECT
                  (meta->mbcode), &mapped_data, &mapped_data_size)) {
            fwrite (mapped_data, 1, mapped_data_size, mbcode_file);
            gst_vaapi_fei_codec_object_unmap (GST_VAAPI_FEI_CODEC_OBJECT
                (meta->mbcode));
            mbcode_buffer_size = mapped_data_size;
          }
        }

        if (dist_file != NULL) {
          mapped_data = NULL;
          mapped_data_size = 0;
          if (gst_vaapi_fei_codec_object_map (GST_VAAPI_FEI_CODEC_OBJECT
                  (meta->dist), &mapped_data, &mapped_data_size)) {
            fwrite (mapped_data, 1, mapped_data_size, dist_file);
            gst_vaapi_fei_codec_object_unmap (GST_VAAPI_FEI_CODEC_OBJECT
                (meta->dist));
            dist_buffer_size = mapped_data_size;
          }
        }
      }

      gst_sample_unref (sample);
    } else {
      g_print ("appsink finished receive sample.\n");
      break;
    }
  }

  /* Fixme: Currently assuming the input video has only one resoultion
   * which may not be true */
  /* create a status file for dumping size of each fei output buffer */
  if (output_mv_name || output_mbcode_name || output_distortion_name) {
    fei_stat_file = fopen ("fei_stat.out", "wb");
    fprintf (fei_stat_file, "Frame_MotionVectorData_Buffer_Size => %d \n",
        mv_buffer_size);
    fprintf (fei_stat_file, "Frame_MacroblcokCode_Buffer_Size => %d \n",
        mbcode_buffer_size);
    fprintf (fei_stat_file, "Frame_Distortion_Buffer_Size => %d \n",
        dist_buffer_size);
  }

  /* free */
  fclose (file);
  if (mv_file != NULL)
    fclose (mv_file);
  if (mbcode_file != NULL)
    fclose (mbcode_file);
  if (dist_file != NULL)
    fclose (dist_file);
  if (fei_stat_file)
    fclose (fei_stat_file);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
