/*
 * Copyright (c) 2012 FXI Technologies
 *
 * This library is licensed under 2 different licenses and you
 * can choose to use it under the terms of any one of them. The
 * two licenses are the Apache License 2.0 and the LGPL.
 *
 * Apache License 2.0:
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LGPL:
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

/*
 * Author: Haavard Kvaalen <havardk@fxitech.com>
 */

#include "mfc_decoder.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/videodev2.h>

/* For logging */
#include <gst/gst.h>
GST_DEBUG_CATEGORY (mfc_decoder_debug);
#define GST_CAT_DEFAULT mfc_decoder_debug

#define MAX_DECODER_INPUT_BUFFER_SIZE  (1024 * 3072)
#define NUM_INPUT_PLANES 1
#define NUM_OUTPUT_PLANES 2
#define MAX_DECODING_TIME 50
#define MFC_PATH "/dev/video8"

static int mfc_in_use;

/* Protects the mfc_in_use variable */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct mfc_dec_context {
    int fd;
    int num_input_buffers;
    int num_output_buffers;
    struct mfc_buffer *input_buffer;
    struct mfc_buffer *output_buffer;

    int input_streamon, output_streamon;

    /*
     * Number of decoded frames the MFC needs to have access to to
     * decode correctly.
     */
    int required_output_buffers;
    int has_free_input_buffers;
    /*
     * Number of frames that have been decoded.  We cannot return them
     * if this number is less than required_output_buffers.
     */
    int output_frames_available;
    int input_frames_queued;
    /* We have reached end of stream */
    int eos_reached;
    struct {
        int w;
        int h;
    } output_size;
    struct {
        int left;
        int top;
        int w;
        int h;
    } crop_size;
    int output_stride[NUM_OUTPUT_PLANES];
};

struct mfc_buffer {
    struct {
        int length;
        int bytesused;
        void *data;
    } plane[2];
    int index;
    int state;
};


enum {
    BUFFER_FREE,
    BUFFER_ENQUEUED,
    BUFFER_DEQUEUED,
};

static unsigned int to_v4l2_codec(enum mfc_codec_type codec)
{
    switch (codec) {
        case CODEC_TYPE_H264:
            return V4L2_PIX_FMT_H264;
        case CODEC_TYPE_VC1:
            return V4L2_PIX_FMT_VC1_ANNEX_G;
        case CODEC_TYPE_VC1_RCV:
            return V4L2_PIX_FMT_VC1_ANNEX_L;
        case CODEC_TYPE_MPEG4:
            return V4L2_PIX_FMT_MPEG4;
        case CODEC_TYPE_MPEG1:
            return V4L2_PIX_FMT_MPEG1;
        case CODEC_TYPE_MPEG2:
            return V4L2_PIX_FMT_MPEG2;
        case CODEC_TYPE_H263:
            return V4L2_PIX_FMT_H263;
    }
    GST_ERROR ("Invalid codec type %d", codec);
    return 0;
}

int mfc_dec_set_codec(struct mfc_dec_context *ctx, enum mfc_codec_type codec)
{
    int ret;
    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
        .fmt = {
            .pix_mp = {
                .num_planes = 1,
                .plane_fmt = {
                    [0] = {
                        .sizeimage = MAX_DECODER_INPUT_BUFFER_SIZE,
                    },
                },
            },
        },
    };
    fmt.fmt.pix_mp.pixelformat = to_v4l2_codec(codec);

    ret = ioctl(ctx->fd, VIDIOC_S_FMT, &fmt);
    if (ret)
        GST_ERROR ("Unable to set input format");
    return ret;
}

static int request_input_buffers(struct mfc_dec_context *ctx, int num)
{
    struct v4l2_requestbuffers reqbuf = {
        .count = num,
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
    };
    int i;

    ctx->input_buffer = calloc(num, sizeof (struct mfc_buffer));
    if (!ctx->input_buffer) {
        GST_ERROR ("Failed to allocate space for input buffer meta data");
        return -1;
    }

    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        GST_ERROR ("Unable to request input buffers");
        return -1;
    }
    ctx->num_input_buffers = reqbuf.count;
    GST_INFO ("Requested %d input buffers, got %d", num, reqbuf.count);
    for (i = 0; i < num; i++) {
        void *ptr;
        struct v4l2_plane planes[NUM_INPUT_PLANES] = {{.length = 0}};
        struct v4l2_buffer buffer = {
            .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
            .length = NUM_INPUT_PLANES,
            .m = {
                .planes = planes,
            },
        };
        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            GST_ERROR ("Query of input buffer failed");
            return -1;
        }
        ptr = mmap(NULL, buffer.m.planes[0].length, PROT_READ | PROT_WRITE,
                   MAP_SHARED, ctx->fd, buffer.m.planes[0].m.mem_offset);
        if (ptr == MAP_FAILED) {
            GST_ERROR  ("Failed to map input buffer");
            return -1;
        }
        ctx->input_buffer[i].plane[0].length = planes[0].length;
        ctx->input_buffer[i].plane[0].data = ptr;
        ctx->input_buffer[i].index = i;
        ctx->input_buffer[i].state = BUFFER_FREE;
    }
    ctx->has_free_input_buffers = 1;
    return 0;
}

static int request_output_buffers(struct mfc_dec_context *ctx, int num)
{
    struct v4l2_requestbuffers reqbuf = {
        .count = num,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
    };
    int i;

    ctx->output_buffer = calloc(num, sizeof (struct mfc_buffer));
    if (!ctx->output_buffer) {
        GST_ERROR ("Failed to allocate space for output buffer meta data");
        return -1;
    }

    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        GST_ERROR ("Unable to request output buffers");
        return -1;
    }
    ctx->num_output_buffers = reqbuf.count;
    GST_INFO ("Requested %d output buffers, got %d", num, reqbuf.count);
    for (i = 0; i < ctx->num_output_buffers; i++) {
        int p;
        struct v4l2_plane planes[NUM_OUTPUT_PLANES] = {{.length = 0}};
        struct v4l2_buffer buffer = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
            .length = NUM_OUTPUT_PLANES,
            .m = {
                .planes = planes,
            },
        };
        ctx->output_buffer[i].index = i;
        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            GST_ERROR ("Query of output buffer failed");
            return -1;
        }
        for (p = 0; p < NUM_OUTPUT_PLANES; p++) {
            void *ptr = mmap(NULL, buffer.m.planes[p].length,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             ctx->fd, buffer.m.planes[p].m.mem_offset);
            if (ptr == MAP_FAILED) {
                GST_ERROR ("Failed to map output buffer");
                return -1;
            }
            ctx->output_buffer[i].plane[p].length = planes[p].length;
            ctx->output_buffer[i].plane[p].data = ptr;
        }
        if (mfc_dec_enqueue_output(ctx, &ctx->output_buffer[i]) < 0)
            return -1;
    }
    return 0;
}

struct mfc_dec_context* mfc_dec_create(unsigned int codec)
{
    struct mfc_dec_context *ctx;
    struct v4l2_capability caps;
    struct stat sb;

    pthread_mutex_lock(&mutex);
    if (mfc_in_use) {
        GST_ERROR ("Rejected because MFC is already in use");
        pthread_mutex_unlock(&mutex);
        return NULL;
    }
    mfc_in_use = 1;
    pthread_mutex_unlock(&mutex);

    ctx = calloc(1, sizeof (struct mfc_dec_context));
    ctx->output_frames_available = 0;
    if (!ctx) {
        GST_ERROR ("Unable to allocate memory for context");
        return NULL;
    }

    if (stat (MFC_PATH, &sb) < 0) {
        GST_INFO ("MFC device node doesn't exist, failing quietly");
        free(ctx);
        return NULL;
    }

    GST_INFO ("Opening MFC device node at: %s", MFC_PATH);
    ctx->fd = open(MFC_PATH, O_RDWR, 0);
    if (ctx->fd == -1) {
        GST_WARNING ("Unable to open MFC device node: %d", errno);
        free(ctx);
        return NULL;
    }

    if (ioctl (ctx->fd, VIDIOC_QUERYCAP, &caps) < 0) {
        GST_ERROR ("Unable to query capabilities: %d", errno);
        mfc_dec_destroy(ctx);
        return NULL;
    }

    if ((caps.capabilities & V4L2_CAP_STREAMING) == 0 ||
        (caps.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0 ||
        (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
        GST_ERROR ("Required capabilities not available");
        mfc_dec_destroy(ctx);
        return NULL;
    }

    if (mfc_dec_set_codec(ctx, codec) < 0) {
        mfc_dec_destroy(ctx);
        return NULL;
    }
    return ctx;
}

static int get_output_format(struct mfc_dec_context *ctx)
{
    int i;
    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
    };

    if (ioctl(ctx->fd, VIDIOC_G_FMT, &fmt) < 0) {
        GST_ERROR ("Failed to get output format");
        return -1;
    }

    ctx->output_size.w = fmt.fmt.pix_mp.width;
    ctx->output_size.h = fmt.fmt.pix_mp.height;

    for (i = 0; i < NUM_OUTPUT_PLANES; i++)
      ctx->output_stride[i] = fmt.fmt.pix_mp.plane_fmt[i].bytesperline;

    return 0;
}

static int get_crop_data(struct mfc_dec_context *ctx)
{
    struct v4l2_crop crop = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
    };
    if (ioctl(ctx->fd, VIDIOC_G_CROP, &crop) < 0) {
        GST_ERROR ("Unable to get crop data");
        return -1;
    }
    ctx->crop_size.left = crop.c.left;
    ctx->crop_size.top = crop.c.top;
    ctx->crop_size.w = crop.c.width;
    ctx->crop_size.h = crop.c.height;
    return 0;
}

static int get_minimum_output_buffers(struct mfc_dec_context *ctx)
{
    struct v4l2_control ctrl = {
        .id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
    };
    if (ioctl(ctx->fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        GST_ERROR ("Failed to get number of output buffers required");
        return -1;
    }
    ctx->required_output_buffers = ctrl.value + 1;
    return 0;
}

static int start_input_stream(struct mfc_dec_context *ctx)
{
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        GST_ERROR ("Unable to start input stream");
        return -1;
    }
    ctx->input_streamon = 1;
    return 0;
}

static int start_output_stream(struct mfc_dec_context *ctx)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        GST_ERROR ("Unable to start output stream");
        return -1;
    }
    ctx->output_streamon = 1;
    return 0;
}

int mfc_dec_init_input(struct mfc_dec_context *ctx, int num_input_buffers)
{
    if (request_input_buffers(ctx, num_input_buffers) < 0)
        return -1;

    return 0;
}

int mfc_dec_init_output(struct mfc_dec_context *ctx, int extra_buffers)
{
    if (start_input_stream(ctx) < 0)
        return -1;

    if (get_output_format(ctx) ||
        get_crop_data(ctx) ||
        get_minimum_output_buffers(ctx))
        return -1;

    if (request_output_buffers(ctx, ctx->required_output_buffers + extra_buffers))
        return -1;

    if (start_output_stream(ctx) < 0)
        return -1;

    return 0;
}

void mfc_dec_get_output_size(struct mfc_dec_context *ctx, int *w, int *h)
{
    *w = ctx->output_size.w;
    *h = ctx->output_size.h;
}

void mfc_dec_get_output_stride(struct mfc_dec_context *ctx, int *ystride, int *uvstride)
{
    *ystride = ctx->output_stride[0];
    *uvstride = ctx->output_stride[1];
}

void mfc_dec_get_crop_size(struct mfc_dec_context *ctx,
                           int *left, int *top, int *w, int *h)
{
    *left = ctx->crop_size.left;
    *top = ctx->crop_size.top;
    *w = ctx->crop_size.w;
    *h = ctx->crop_size.h;
}

void mfc_dec_destroy(struct mfc_dec_context *ctx)
{
    int i;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ctx->output_streamon)
        if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) < 0)
            GST_ERROR ("Streamoff failed on output");
    ctx->output_streamon = 0;

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ctx->input_streamon)
        if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) < 0)
            GST_ERROR ("Streamoff failed on input");
    ctx->input_streamon = 0;

    for (i = 0; i < ctx->num_input_buffers; i++) {
        if (ctx->input_buffer[i].plane[0].data)
            munmap(ctx->input_buffer[i].plane[0].data,
                   ctx->input_buffer[i].plane[0].length);
    }
    for (i = 0; i < ctx->num_output_buffers; i++) {
        int j;
        for (j = 0; j < NUM_OUTPUT_PLANES; j++)
            if (ctx->output_buffer[i].plane[j].data)
                munmap(ctx->output_buffer[i].plane[j].data,
                       ctx->output_buffer[i].plane[j].length);
    }
    if (ctx->input_buffer)
        free (ctx->input_buffer);
    if (ctx->output_buffer)
        free (ctx->output_buffer);

    close(ctx->fd);
    pthread_mutex_lock(&mutex);
    mfc_in_use = 0;
    pthread_mutex_unlock(&mutex);
    GST_INFO ("MFC device closed");
    free(ctx);
}

int mfc_dec_enqueue_input(struct mfc_dec_context *ctx, struct mfc_buffer *buffer, struct timeval *timestamp)
{
    struct v4l2_plane planes[NUM_INPUT_PLANES] = {
        [0] = {
            .bytesused = buffer->plane[0].bytesused,
        },
    };
    struct v4l2_buffer qbuf = {
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
        .index = buffer->index,
        .length = NUM_INPUT_PLANES,
        .m = {
            .planes = planes,
        },
    };

    if (timestamp)
      qbuf.timestamp = *timestamp;

    if (ioctl(ctx->fd, VIDIOC_QBUF, &qbuf) < 0) {
        GST_ERROR ("Enqueuing of input buffer %d failed; prev state: %d",
             buffer->index, buffer->state);
        return -1;
    }

    ctx->input_frames_queued++;
    buffer->state = BUFFER_ENQUEUED;
    if (buffer->plane[0].bytesused == 0)
            ctx->eos_reached = 1;
    return 0;
}

static int input_dqbuf(struct mfc_dec_context *ctx, struct mfc_buffer **buffer)
{
    struct v4l2_buffer qbuf = {
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
    };
    struct pollfd fd = {
        .fd = ctx->fd,
        .events = POLLOUT | POLLERR,
    };
    int pollret;

    pollret = poll(&fd, 1, MAX_DECODING_TIME);
    if (pollret < 0) {
        GST_ERROR ("%s: Poll returned error: %d", __func__, errno);
        return -1;
    }
    if (pollret == 0) {
        GST_INFO ("%s: timed out", __func__);
        return -2;
    }

    if (ioctl(ctx->fd, VIDIOC_DQBUF, &qbuf) < 0) {
        GST_ERROR ("Dequeuing failed");
        return -1;
    }
    ctx->input_buffer[qbuf.index].plane[0].bytesused = 0;
    *buffer = &ctx->input_buffer[qbuf.index];
    ctx->output_frames_available++;
    ctx->input_frames_queued--;
    return 0;
}

int mfc_dec_dequeue_input(struct mfc_dec_context *ctx, struct mfc_buffer **buffer)
{
    if (ctx->has_free_input_buffers) {
        int i;
        *buffer = NULL;
        for (i = 0; i < ctx->num_input_buffers; i++) {
            if (ctx->input_buffer[i].state == BUFFER_FREE)
                *buffer = &ctx->input_buffer[i];
        }
        if (!*buffer) {
            int ret;
            ctx->has_free_input_buffers = 0;
            if ((ret = input_dqbuf(ctx, buffer)) < 0)
                return ret;
        }
    } else {
        int ret = input_dqbuf(ctx, buffer);
        if (ret < 0)
            return ret;
    }
    (*buffer)->state = BUFFER_DEQUEUED;
    return 0;
}

static int release_input_buffer(struct mfc_dec_context *ctx)
{
    struct mfc_buffer *buffer;
    struct pollfd fd = {
        .fd = ctx->fd,
        .events = POLLOUT | POLLERR,
    };
    int pollret;

    if (ctx->input_frames_queued == 0) {
        GST_INFO ("Nothing to release!");
        return -1;
    }

    pollret = poll(&fd, 1, MAX_DECODING_TIME);
    if (pollret < 0) {
        GST_ERROR ("%s: Poll returned error: %d", __func__, errno);
        return -1;
    }
    if (pollret == 0) {
        GST_INFO ("%s: timed out", __func__);
        return -2;
    }

    GST_DEBUG ("releasing frame; frames queued: %d", ctx->input_frames_queued);
    input_dqbuf(ctx, &buffer);
    buffer->state = BUFFER_FREE;
    ctx->has_free_input_buffers = 1;
    return 0;
}

int mfc_dec_enqueue_output(struct mfc_dec_context *ctx, struct mfc_buffer *buffer)
{
    struct v4l2_plane planes[NUM_OUTPUT_PLANES] = {{.length = 0}};
    struct v4l2_buffer qbuf = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
        .index = buffer->index,
        .length = NUM_OUTPUT_PLANES,
        .m = {
            .planes = planes,
        },
    };
    if (ioctl(ctx->fd, VIDIOC_QBUF, &qbuf) < 0) {
        GST_ERROR ("Enqueuing of output buffer %d failed; prev state: %d",
             buffer->index, buffer->state);
        return -1;
    }
    buffer->state = BUFFER_ENQUEUED;
    return 0;
}

int mfc_dec_dequeue_output(struct mfc_dec_context *ctx, struct mfc_buffer **buffer, struct timeval *timestamp)
{
    int i;
    struct v4l2_plane planes[NUM_OUTPUT_PLANES];
    struct v4l2_buffer qbuf = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        .memory = V4L2_MEMORY_MMAP,
        .m = {
            .planes = planes,
        },
        .length = NUM_OUTPUT_PLANES,
    };

    if (ioctl(ctx->fd, VIDIOC_DQBUF, &qbuf) < 0) {
        GST_ERROR ("Dequeuing failed");
        return -1;
    }

    for (i = 0; i < NUM_OUTPUT_PLANES; i++)
        ctx->output_buffer[qbuf.index].plane[i].bytesused = qbuf.m.planes[i].bytesused;

    *buffer = &(ctx->output_buffer[qbuf.index]);

    if (timestamp)
      *timestamp = qbuf.timestamp;

    ctx->output_frames_available--;
    return 0;
}

int mfc_dec_output_available(struct mfc_dec_context *ctx)
{
    if (ctx->eos_reached) {
        if (ctx->input_frames_queued > 0 &&
            ctx->output_frames_available <= ctx->required_output_buffers) {
            release_input_buffer(ctx);
        }

        return ctx->output_frames_available > 0;
    }
    return ctx->output_frames_available >= ctx->required_output_buffers;
}

int mfc_dec_flush(struct mfc_dec_context *ctx)
{
    int type, i;
    int force_dequeue_output = 0;
    while (ctx->input_frames_queued > 0) {
        int status;
        struct mfc_buffer *buffer;
        /* Make sure there is room for the decode to finish */
        if (mfc_dec_output_available(ctx) || force_dequeue_output) {
            if (mfc_dec_dequeue_output(ctx, &buffer, NULL) < 0)
                return -1;
            if (mfc_dec_enqueue_output(ctx, buffer) < 0)
                return -1;
            force_dequeue_output = 0;
        }

        status = release_input_buffer(ctx);
        if (status == -2)
            force_dequeue_output = 1;
        if (status == -1)
            break;
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) < 0) {
        GST_ERROR ("Unable to stop output stream");
        return -1;
    }

    for (i = 0; i < ctx->num_output_buffers; i++) {
        if (ctx->output_buffer[i].state == BUFFER_ENQUEUED)
            if (mfc_dec_enqueue_output(ctx, &ctx->output_buffer[i]) < 0)
                return -1;
    }

    if (start_output_stream(ctx) < 0)
        return -1;

    ctx->output_frames_available = 0;
    ctx->eos_reached = 0;

    return 0;
}

void* mfc_buffer_get_input_data(struct mfc_buffer *buffer)
{
    return buffer->plane[0].data;
}

int mfc_buffer_get_input_max_size(struct mfc_buffer *buffer)
{
    return buffer->plane[0].length;
}


void mfc_buffer_set_input_size(struct mfc_buffer *buffer, int size)
{
    buffer->plane[0].bytesused = size;
}

void mfc_buffer_get_output_data(struct mfc_buffer *buffer,
                                void **ybuf, void **uvbuf)
{
    *ybuf = buffer->plane[0].data;
    *uvbuf = buffer->plane[1].data;
}

void mfc_dec_init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (mfc_decoder_debug, "mfc_decoder", 0, "MFC decoder library");
}
