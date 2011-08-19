/* GStreamer RTP payloader unit tests
 * Copyright (C) 2008 Nokia Corporation and its subsidary(-ies)
 *               contact: <stefan.kost@nokia.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <gst/check/gstcheck.h>
#include <stdlib.h>
#include <unistd.h>

#define RELEASE_ELEMENT(x) if(x) {gst_object_unref(x); x = NULL;}

#define LOOP_COUNT 1

/*
 * RTP pipeline structure to store the required elements.
 */
typedef struct
{
  GstElement *pipeline;
  GstElement *fdsrc;
  GstElement *capsfilter;
  GstElement *rtppay;
  GstElement *rtpdepay;
  GstElement *fakesink;
  int fd[2];
  const char *frame_data;
  int frame_data_size;
  int frame_count;
} rtp_pipeline;

/*
 * Number of bytes received in the chain list function when using buffer lists
 */
static guint chain_list_bytes_received;

/*
 * Chain list function for testing buffer lists
 */
static GstFlowReturn
rtp_pipeline_chain_list (GstPad * pad, GstBufferList * list)
{
  GstBufferListIterator *it;

  fail_if (!list);
  it = gst_buffer_list_iterate (list);

  /*
   * Count the size of the payload in the buffer list.
   */

  /* Loop through all groups */
  while (gst_buffer_list_iterator_next_group (it)) {
    GstBuffer *paybuf;

    /* Skip the first buffer in the group, its the RTP header */
    fail_if (!gst_buffer_list_iterator_next (it));

    /* Loop through all payload buffers in the current group */
    while ((paybuf = gst_buffer_list_iterator_next (it))) {
      chain_list_bytes_received += GST_BUFFER_SIZE (paybuf);
    }
  }

  gst_buffer_list_iterator_free (it);
  gst_buffer_list_unref (list);

  return GST_FLOW_OK;
}

/*
 * RTP bus callback.
 */
static gboolean
rtp_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *mainloop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;

      gchar *debug;

      gchar *element_name;

      element_name = (message->src) ? gst_object_get_name (message->src) : NULL;
      gst_message_parse_error (message, &err, &debug);
      /* FIXME: should we fail the test here? */
      g_print ("\nError from element %s: %s\n%s\n\n",
          GST_STR_NULL (element_name), err->message, (debug) ? debug : "");
      g_error_free (err);
      g_free (debug);
      g_free (element_name);

      g_main_loop_quit (mainloop);
    }
      break;

    case GST_MESSAGE_EOS:
    {
      g_main_loop_quit (mainloop);
    }
      break;
      break;

    default:
    {
    }
      break;
  }

  return TRUE;
}

/*
 * Creates a RTP pipeline for one test.
 * @param frame_data Pointer to the frame data which is used to pass thru pay/depayloaders.
 * @param frame_data_size Frame data size in bytes.
 * @param frame_count Frame count.
 * @param filtercaps Caps filters.
 * @param pay Payloader name.
 * @param depay Depayloader name.
 * @return
 * Returns pointer to the RTP pipeline.
 * The user must free the RTP pipeline when it's not used anymore.
 */
static rtp_pipeline *
rtp_pipeline_create (const char *frame_data, int frame_data_size,
    int frame_count, const char *filtercaps, const char *pay, const char *depay)
{
  gchar *pipeline_name;

  rtp_pipeline *p;

  GstCaps *caps;

  /* Check parameters. */
  if (!frame_data || !pay || !depay) {
    return NULL;
  }

  /* Allocate memory for the RTP pipeline. */
  p = (rtp_pipeline *) malloc (sizeof (rtp_pipeline));

  p->frame_data = frame_data;
  p->frame_data_size = frame_data_size;
  p->frame_count = frame_count;

  /* Create elements. */
  pipeline_name = g_strdup_printf ("%s-%s-pipeline", pay, depay);
  p->pipeline = gst_pipeline_new (pipeline_name);
  g_free (pipeline_name);
  p->fdsrc = gst_element_factory_make ("fdsrc", NULL);
  p->capsfilter = gst_element_factory_make ("capsfilter", NULL);
  p->rtppay = gst_element_factory_make (pay, NULL);
  p->rtpdepay = gst_element_factory_make (depay, NULL);
  p->fakesink = gst_element_factory_make ("fakesink", NULL);

  /* One or more elements are not created successfully or failed to create p? */
  if (!p->pipeline || !p->fdsrc || !p->capsfilter || !p->rtppay || !p->rtpdepay
      || !p->fakesink || pipe (p->fd) == -1) {
    /* Release created elements. */
    RELEASE_ELEMENT (p->pipeline);
    RELEASE_ELEMENT (p->fdsrc);
    RELEASE_ELEMENT (p->capsfilter);
    RELEASE_ELEMENT (p->rtppay);
    RELEASE_ELEMENT (p->rtpdepay);
    RELEASE_ELEMENT (p->fakesink);

    /* Close pipe. */
    if (p->fd[0]) {
      close (p->fd[0]);
    }

    if (p->fd[1]) {
      close (p->fd[1]);
    }

    /* Release allocated memory. */
    free (p);

    return NULL;
  }

  /* Set fdsrc properties. */
  g_object_set (p->fdsrc, "fd", p->fd[0], NULL);
  g_object_set (p->fdsrc, "do-timestamp", TRUE, NULL);
  g_object_set (p->fdsrc, "blocksize", p->frame_data_size, NULL);
  g_object_set (p->fdsrc, "num-buffers", p->frame_count * LOOP_COUNT, NULL);

  /* Set caps filters. */
  caps = gst_caps_from_string (filtercaps);

  g_object_set (p->capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  /* Add elements to the pipeline. */
  gst_bin_add (GST_BIN (p->pipeline), p->fdsrc);
  gst_bin_add (GST_BIN (p->pipeline), p->capsfilter);
  gst_bin_add (GST_BIN (p->pipeline), p->rtppay);
  gst_bin_add (GST_BIN (p->pipeline), p->rtpdepay);
  gst_bin_add (GST_BIN (p->pipeline), p->fakesink);

  /* Link elements. */
  gst_element_link (p->fdsrc, p->capsfilter);
  gst_element_link (p->capsfilter, p->rtppay);
  gst_element_link (p->rtppay, p->rtpdepay);
  gst_element_link (p->rtpdepay, p->fakesink);

  return p;
}

/*
 * Destroys the RTP pipeline.
 * @param p Pointer to the RTP pipeline.
 */
static void
rtp_pipeline_destroy (rtp_pipeline * p)
{
  /* Check parameters. */
  if (p == NULL) {
    return;
  }

  /* Release pipeline. */
  RELEASE_ELEMENT (p->pipeline);

  /* Close pipe. */
  if (p->fd[0]) {
    close (p->fd[0]);
  }

  if (p->fd[1]) {
    close (p->fd[1]);
  }

  /* Release allocated memory. */
  free (p);
}

/*
 * Runs the RTP pipeline.
 * @param p Pointer to the RTP pipeline.
 */
static void
rtp_pipeline_run (rtp_pipeline * p)
{
  GMainLoop *mainloop = NULL;

  GstBus *bus;

  gint i;

  /* Check parameters. */
  if (p == NULL) {
    return;
  }

  /* Create mainloop. */
  mainloop = g_main_loop_new (NULL, FALSE);
  if (!mainloop) {
    return;
  }

  /* Add bus callback. */
  bus = gst_pipeline_get_bus (GST_PIPELINE (p->pipeline));

  gst_bus_add_watch (bus, rtp_bus_callback, (gpointer) mainloop);
  gst_object_unref (bus);

  /* Set pipeline to PLAYING. */
  gst_element_set_state (p->pipeline, GST_STATE_PLAYING);

  /* TODO: Writing may need some changes... */

  for (i = 0; i < LOOP_COUNT; i++) {
    const char *frame_data_pointer = p->frame_data;
    int res;
    int frame_count = p->frame_count;

    /* Write in to the pipe. */
    while (frame_count > 0) {
      res = write (p->fd[1], frame_data_pointer, p->frame_data_size);
      fail_unless_equals_int (res, p->frame_data_size);
      frame_data_pointer += p->frame_data_size;
      frame_count--;
    }
  }

  /* Run mainloop. */
  g_main_loop_run (mainloop);

  /* Set pipeline to NULL. */
  gst_element_set_state (p->pipeline, GST_STATE_NULL);

  /* Release mainloop. */
  g_main_loop_unref (mainloop);
}

/*
 * Enables buffer lists. Sets the buffer-list property of the payloader
 * and adds a chain_list_function to the depayloader.
 * @param p Pointer to the RTP pipeline.
 */
static void
rtp_pipeline_enable_lists (rtp_pipeline * p, guint mtu_size)
{
  GstPad *pad;

  /* use buffer lists */
  g_object_set (p->rtppay, "buffer-list", TRUE, NULL);

  /* set mtu size if needed */
  if (mtu_size) {
    g_object_set (p->rtppay, "mtu", mtu_size, NULL);
  }

  /* Add chain list function for the buffer list tests */
  pad = gst_element_get_static_pad (p->rtpdepay, "sink");
  gst_pad_set_chain_list_function (pad,
      GST_DEBUG_FUNCPTR (rtp_pipeline_chain_list));
  gst_object_unref (pad);
}

/*
 * Creates the RTP pipeline and runs the test using the pipeline.
 * @param frame_data Pointer to the frame data which is used to pass thru pay/depayloaders.
 * @param frame_data_size Frame data size in bytes.
 * @param frame_count Frame count.
 * @param filtercaps Caps filters.
 * @param pay Payloader name.
 * @param depay Depayloader name.
 * @bytes_sent bytes that will be sent, used when testing buffer lists
 * @mtu_size set mtu size when testing lists
 * @use_lists enable buffer lists
 */
static void
rtp_pipeline_test (const char *frame_data, int frame_data_size, int frame_count,
    const char *filtercaps, const char *pay, const char *depay,
    guint bytes_sent, guint mtu_size, gboolean use_lists)
{
  /* Create RTP pipeline. */
  rtp_pipeline *p =
      rtp_pipeline_create (frame_data, frame_data_size, frame_count, filtercaps,
      pay, depay);

  if (p == NULL) {
    return;
  }

  if (use_lists) {
    rtp_pipeline_enable_lists (p, mtu_size);
    chain_list_bytes_received = 0;
  }

  /* Run RTP pipeline. */
  rtp_pipeline_run (p);

  /* Destroy RTP pipeline. */
  rtp_pipeline_destroy (p);

  if (use_lists) {
    /* 'next NAL' indicator is 4 bytes */
    fail_if (chain_list_bytes_received != bytes_sent * LOOP_COUNT);
  }
}

static char rtp_ilbc_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_ilbc_frame_data_size = 20;

static int rtp_ilbc_frame_count = 1;

GST_START_TEST (rtp_ilbc)
{
  rtp_pipeline_test (rtp_ilbc_frame_data, rtp_ilbc_frame_data_size,
      rtp_ilbc_frame_count, "audio/x-iLBC,mode=20", "rtpilbcpay",
      "rtpilbcdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_gsm_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_gsm_frame_data_size = 20;

static int rtp_gsm_frame_count = 1;

GST_START_TEST (rtp_gsm)
{
  rtp_pipeline_test (rtp_gsm_frame_data, rtp_gsm_frame_data_size,
      rtp_gsm_frame_count, "audio/x-gsm,rate=8000,channels=1", "rtpgsmpay",
      "rtpgsmdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_amr_frame_data[] =
    { 0x3c, 0x24, 0x03, 0xb3, 0x48, 0x10, 0x68, 0x46, 0x6c, 0xec, 0x03,
  0x7a, 0x37, 0x16, 0x41, 0x41, 0xc0, 0x00, 0x0d, 0xcd, 0x12, 0xed,
  0xad, 0x80, 0x00, 0x00, 0x11, 0x31, 0x00, 0x00, 0x0d, 0xa0
};

static int rtp_amr_frame_data_size = 32;

static int rtp_amr_frame_count = 1;

GST_START_TEST (rtp_amr)
{
  rtp_pipeline_test (rtp_amr_frame_data, rtp_amr_frame_data_size,
      rtp_amr_frame_count, "audio/AMR,channels=1,rate=8000", "rtpamrpay",
      "rtpamrdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_pcma_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_pcma_frame_data_size = 20;

static int rtp_pcma_frame_count = 1;

GST_START_TEST (rtp_pcma)
{
  rtp_pipeline_test (rtp_pcma_frame_data, rtp_pcma_frame_data_size,
      rtp_pcma_frame_count, "audio/x-alaw,channels=1,rate=8000", "rtppcmapay",
      "rtppcmadepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_pcmu_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_pcmu_frame_data_size = 20;

static int rtp_pcmu_frame_count = 1;

GST_START_TEST (rtp_pcmu)
{
  rtp_pipeline_test (rtp_pcmu_frame_data, rtp_pcmu_frame_data_size,
      rtp_pcmu_frame_count, "audio/x-mulaw,channels=1,rate=8000", "rtppcmupay",
      "rtppcmudepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_mpa_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_mpa_frame_data_size = 20;

static int rtp_mpa_frame_count = 1;

GST_START_TEST (rtp_mpa)
{
  rtp_pipeline_test (rtp_mpa_frame_data, rtp_mpa_frame_data_size,
      rtp_mpa_frame_count, "audio/mpeg", "rtpmpapay", "rtpmpadepay", 0, 0,
      FALSE);
}

GST_END_TEST;
static char rtp_h263_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_h263_frame_data_size = 20;

static int rtp_h263_frame_count = 1;

GST_START_TEST (rtp_h263)
{
  rtp_pipeline_test (rtp_h263_frame_data, rtp_h263_frame_data_size,
      rtp_h263_frame_count, "video/x-h263,variant=itu,h263version=h263",
      "rtph263pay", "rtph263depay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_h263p_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_h263p_frame_data_size = 20;

static int rtp_h263p_frame_count = 1;

GST_START_TEST (rtp_h263p)
{
  rtp_pipeline_test (rtp_h263p_frame_data, rtp_h263p_frame_data_size,
      rtp_h263p_frame_count, "video/x-h263,variant=itu", "rtph263ppay",
      "rtph263pdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_h264_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_h264_frame_data_size = 20;

static int rtp_h264_frame_count = 1;

GST_START_TEST (rtp_h264)
{
  rtp_pipeline_test (rtp_h264_frame_data, rtp_h264_frame_data_size,
      rtp_h264_frame_count, "video/x-h264", "rtph264pay", "rtph264depay",
      0, 0, FALSE);
}

GST_END_TEST;
static char rtp_h264_list_lt_mtu_frame_data[] =
    /* not packetized, next NAL starts with 0001 */
{ 0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
  0xad, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00
};

static int rtp_h264_list_lt_mtu_frame_data_size = 16;

static int rtp_h264_list_lt_mtu_frame_count = 2;

/* NAL = 4 bytes */
static int rtp_h264_list_lt_mtu_bytes_sent = 2 * (16 - 4);

static int rtp_h264_list_lt_mtu_mtu_size = 1024;

GST_START_TEST (rtp_h264_list_lt_mtu)
{
  rtp_pipeline_test (rtp_h264_list_lt_mtu_frame_data,
      rtp_h264_list_lt_mtu_frame_data_size, rtp_h264_list_lt_mtu_frame_count,
      "video/x-h264", "rtph264pay", "rtph264depay",
      rtp_h264_list_lt_mtu_bytes_sent, rtp_h264_list_lt_mtu_mtu_size, TRUE);
}

GST_END_TEST;
static char rtp_h264_list_gt_mtu_frame_data[] =
    /* not packetized, next NAL starts with 0001 */
{ 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_h264_list_gt_mtu_frame_data_size = 64;

static int rtp_h264_list_gt_mtu_frame_count = 1;

/* NAL = 4 bytes. When data does not fit into 1 mtu, 1 byte will be skipped */
static int rtp_h264_list_gt_mtu_bytes_sent = 1 * (64 - 4) - 1;

static int rtp_h264_list_gt_mtu_mty_size = 28;

GST_START_TEST (rtp_h264_list_gt_mtu)
{
  rtp_pipeline_test (rtp_h264_list_gt_mtu_frame_data,
      rtp_h264_list_gt_mtu_frame_data_size, rtp_h264_list_gt_mtu_frame_count,
      "video/x-h264", "rtph264pay", "rtph264depay",
      rtp_h264_list_gt_mtu_bytes_sent, rtp_h264_list_gt_mtu_mty_size, TRUE);
}

GST_END_TEST;
static char rtp_L16_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_L16_frame_data_size = 20;

static int rtp_L16_frame_count = 1;

GST_START_TEST (rtp_L16)
{
  rtp_pipeline_test (rtp_L16_frame_data, rtp_L16_frame_data_size,
      rtp_L16_frame_count,
      "audio/x-raw,format=S16_BE,rate=1,channels=1",
      "rtpL16pay", "rtpL16depay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_mp2t_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_mp2t_frame_data_size = 20;

static int rtp_mp2t_frame_count = 1;

GST_START_TEST (rtp_mp2t)
{
  rtp_pipeline_test (rtp_mp2t_frame_data, rtp_mp2t_frame_data_size,
      rtp_mp2t_frame_count, "video/mpegts,packetsize=188,systemstream=true",
      "rtpmp2tpay", "rtpmp2tdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_mp4v_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_mp4v_frame_data_size = 20;

static int rtp_mp4v_frame_count = 1;

GST_START_TEST (rtp_mp4v)
{
  rtp_pipeline_test (rtp_mp4v_frame_data, rtp_mp4v_frame_data_size,
      rtp_mp4v_frame_count, "video/mpeg,mpegversion=4,systemstream=false",
      "rtpmp4vpay", "rtpmp4vdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_mp4v_list_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_mp4v_list_frame_data_size = 20;

static int rtp_mp4v_list_frame_count = 1;

static int rtp_mp4v_list_bytes_sent = 1 * 20;

GST_START_TEST (rtp_mp4v_list)
{
  rtp_pipeline_test (rtp_mp4v_list_frame_data, rtp_mp4v_list_frame_data_size,
      rtp_mp4v_list_frame_count,
      "video/mpeg,mpegversion=4,codec_data=(buffer)000001b001",
      "rtpmp4vpay", "rtpmp4vdepay", rtp_mp4v_list_bytes_sent, 0, TRUE);
}

GST_END_TEST;
static char rtp_mp4g_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_mp4g_frame_data_size = 20;

static int rtp_mp4g_frame_count = 1;

GST_START_TEST (rtp_mp4g)
{
  rtp_pipeline_test (rtp_mp4g_frame_data, rtp_mp4g_frame_data_size,
      rtp_mp4g_frame_count,
      "video/mpeg,mpegversion=4,codec_data=(buffer)000001b001", "rtpmp4gpay",
      "rtpmp4gdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_theora_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_theora_frame_data_size = 20;

static int rtp_theora_frame_count = 1;

GST_START_TEST (rtp_theora)
{
  rtp_pipeline_test (rtp_theora_frame_data, rtp_theora_frame_data_size,
      rtp_theora_frame_count, "video/x-theora", "rtptheorapay",
      "rtptheoradepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_vorbis_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_vorbis_frame_data_size = 20;

static int rtp_vorbis_frame_count = 1;

GST_START_TEST (rtp_vorbis)
{
  rtp_pipeline_test (rtp_vorbis_frame_data, rtp_vorbis_frame_data_size,
      rtp_vorbis_frame_count, "audio/x-vorbis", "rtpvorbispay",
      "rtpvorbisdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_jpeg_frame_data[] =
    { /* SOF */ 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x08,
  0x03, 0x00, 0x21, 0x08, 0x01, 0x11, 0x08, 0x02, 0x11, 0x08,
  /* DQT */ 0xFF, 0xDB, 0x00, 0x43, 0x08,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  /* DATA */ 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_jpeg_frame_data_size = sizeof (rtp_jpeg_frame_data);

static int rtp_jpeg_frame_count = 1;

GST_START_TEST (rtp_jpeg)
{
  rtp_pipeline_test (rtp_jpeg_frame_data, rtp_jpeg_frame_data_size,
      rtp_jpeg_frame_count, "video/x-jpeg,height=640,width=480", "rtpjpegpay",
      "rtpjpegdepay", 0, 0, FALSE);
}

GST_END_TEST;
static char rtp_jpeg_list_frame_data[] =
    { /* SOF */ 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x08,
  0x03, 0x00, 0x21, 0x08, 0x01, 0x11, 0x08, 0x02, 0x11, 0x08,
  /* DQT */ 0xFF, 0xDB, 0x00, 0x43, 0x08,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  /* DATA */ 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_jpeg_list_frame_data_size = sizeof (rtp_jpeg_list_frame_data);

static int rtp_jpeg_list_frame_count = 1;

static int rtp_jpeg_list_bytes_sent = 1 * sizeof (rtp_jpeg_list_frame_data);

GST_START_TEST (rtp_jpeg_list)
{
  rtp_pipeline_test (rtp_jpeg_list_frame_data, rtp_jpeg_list_frame_data_size,
      rtp_jpeg_list_frame_count, "video/x-jpeg,height=640,width=480",
      "rtpjpegpay", "rtpjpegdepay", rtp_jpeg_list_bytes_sent, 0, TRUE);
}

GST_END_TEST;
static char rtp_g729_frame_data[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int rtp_g729_frame_data_size = 22;

static int rtp_g729_frame_count = 1;

GST_START_TEST (rtp_g729)
{
  rtp_pipeline_test (rtp_g729_frame_data, rtp_g729_frame_data_size,
      rtp_g729_frame_count, "audio/G729", "rtpg729pay",
      "rtpg729depay", 0, 0, FALSE);
}

GST_END_TEST;

/*
 * Creates the test suite.
 *
 * Returns: pointer to the test suite.
 */
static Suite *
rtp_payloading_suite (void)
{
  Suite *s = suite_create ("rtp_data_test");

  TCase *tc_chain = tcase_create ("linear");

  /* Set timeout to 60 seconds. */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, rtp_ilbc);
  tcase_add_test (tc_chain, rtp_gsm);
  tcase_add_test (tc_chain, rtp_amr);
  tcase_add_test (tc_chain, rtp_pcma);
  tcase_add_test (tc_chain, rtp_pcmu);
  tcase_add_test (tc_chain, rtp_mpa);
  tcase_add_test (tc_chain, rtp_h263);
  tcase_add_test (tc_chain, rtp_h263p);
  tcase_add_test (tc_chain, rtp_h264);
  tcase_add_test (tc_chain, rtp_h264_list_lt_mtu);
  tcase_add_test (tc_chain, rtp_h264_list_gt_mtu);
  tcase_add_test (tc_chain, rtp_L16);
  tcase_add_test (tc_chain, rtp_mp2t);
  tcase_add_test (tc_chain, rtp_mp4v);
  tcase_add_test (tc_chain, rtp_mp4v_list);
  tcase_add_test (tc_chain, rtp_mp4g);
  tcase_add_test (tc_chain, rtp_theora);
  tcase_add_test (tc_chain, rtp_vorbis);
  tcase_add_test (tc_chain, rtp_jpeg);
  tcase_add_test (tc_chain, rtp_jpeg_list);
  tcase_add_test (tc_chain, rtp_g729);
  return s;
}

GST_CHECK_MAIN (rtp_payloading)
