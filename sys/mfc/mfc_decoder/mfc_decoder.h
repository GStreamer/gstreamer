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

/*
 * Interface for decoding of various video formats using the Samsung
 * Multi-Format Video Codec (MFC).
 */

#ifndef VIDEO_EXYNOS4_MFC_V4L2_INCLUDE_MFC_DECODER_H
#define VIDEO_EXYNOS4_MFC_V4L2_INCLUDE_MFC_DECODER_H

#include <sys/time.h>

struct mfc_buffer;
struct mfc_dec_context;

enum mfc_codec_type {
    CODEC_TYPE_H264,
    CODEC_TYPE_VC1,     /* VC1 advanced profile */
    CODEC_TYPE_VC1_RCV, /* VC1 simple/main profile */
    CODEC_TYPE_MPEG4,
    CODEC_TYPE_MPEG1,
    CODEC_TYPE_MPEG2,
    CODEC_TYPE_H263,
};

#ifdef __cplusplus
extern "C" {
#endif

void mfc_dec_init_debug (void);

/*
 * Open the MFC decoding device node, and allocate input buffers.
 *
 * Returns a mfc_dec_context.  Note that the context can only be used
 * for decoding one stream, i.e. mfc_dec_init can only be called once.
 *
 * Args:
 *   codec: Codec type (this can later be changed with mfc_set_codec()).
 *   num_input_buffers: Numbers of input buffers.  There is never any
 *   need to enqueue more than one buffer for correct decoding.
 *
 * Returns: A new mfc_dec_context if successful, NULL on error.  This
 * context is needed for most other calls below, and should be
 * deallocated with mfc_dec_destroy().
 */
struct mfc_dec_context* mfc_dec_create(enum mfc_codec_type codec);

int mfc_dec_init_input(struct mfc_dec_context*, int num_input_buffers);

/*
 * Destroy context created with mfc_dec_create().
 */
void mfc_dec_destroy(struct mfc_dec_context*);

/*
 * Initialize video decode.  Before this function is called, the
 * initial input frame (which contains the header) must be enqueued.
 *
 * This function allocate output buffers.  All output buffers will be
 * enqueued initially.  The actual number of output buffers depend on the
 *
 * Args:
 *   extra_buffers: Numbers of output buffers that can be kept
 *   dequeued at any time.
 *
 * Returns: Zero for success, negative value on failure.
 */
int mfc_dec_init_output(struct mfc_dec_context*, int extra_buffers);


/*
 * This function may be called only before mfc_dec_init().  It is only
 * necessary to call if the codec type is different from the one
 * supplied to mfc_dec_create().
 *
 * Args:
 *   codec: Codec type
 *
 * Returns: Zero for success, negative value on failure.
 */
int mfc_dec_set_codec(struct mfc_dec_context*, enum mfc_codec_type codec);


/*
 * Get size of image output from the MFC.  The data might be larger
 * than the actual video because of MFC alignment requirements (see
 * the crop size below).  This function can only be called after
 * mfc_dec_init() has been called.
 *
 * Args:
 *   w: Width (output).
 *   h: Height (output).
 *
 * Returns: Zero for success, negative value on failure.
 */
void mfc_dec_get_output_size(struct mfc_dec_context*, int *w, int *h);
void mfc_dec_get_output_stride(struct mfc_dec_context*, int *ystride, int *uvstride);
void mfc_dec_get_crop_size(struct mfc_dec_context*,
                           int *left, int *top, int *w, int *h);

/*
 * Check if there are output frames that can be dequeued.
 *
 * Returns: Positive value if a frame is available, zero if not.
 */
int mfc_dec_output_available(struct mfc_dec_context*);

/*
 * This module use 'input' and 'output' for input to, and output from
 * the MFC.  The VFL2 names for these interfaces are OUTPUT (for the
 * input) and CAPTURE (for the output).
 *
 * These functions return zero for success, negative value on failure.
 *
 * When the end of stream has been reached, an empty input frame
 * should be enqueued after the last valid input frame.  This signal
 * to the MFC that EOS has been reached.  After this no more input
 * frames can be enqueued.
 */

/* Enqueue frame containing compressed data */
int mfc_dec_enqueue_input(struct mfc_dec_context*, struct mfc_buffer *buffer, struct timeval *timestamp);
/*
 * Dequeue a processed input frame.  Will block until one is available.
 *
 * Returns 0 on success, -1 on failure, and -2 on timeout.  A timeout
 * typically happens if there are no free output buffers available.
 */
int mfc_dec_dequeue_input(struct mfc_dec_context*, struct mfc_buffer **buffer);
/* Enqueue empty output frame */
int mfc_dec_enqueue_output(struct mfc_dec_context*, struct mfc_buffer *buffer);
/*
 * Dequeue output frame with image data.  This should only be called
 * when mfc_dec_output_available() returns true.  If this is called
 * when mfc_dec_output_available() is not true, subsequent video
 * frames may not decode correctly.
 */
int mfc_dec_dequeue_output(struct mfc_dec_context*, struct mfc_buffer **buffer, struct timeval *timestamp);


/*
 * Flush (discard) all enqueued output and input frames.  This is
 * typically used if we want to seek.
 *
 * Calling flush resets the "end of stream" state that is entered by
 * enqueuing an empty input frame.  Thus it is safe to seek at the end
 * of the stream as long as mfc_dec_flush() is called first.
 */
int mfc_dec_flush(struct mfc_dec_context*);

/*
 * Get pointer to the input data in 'buffer'.
 */
void* mfc_buffer_get_input_data(struct mfc_buffer *buffer);

/*
 * Get maximum size of input buffer 'buffer' in bytes.
 */
int mfc_buffer_get_input_max_size(struct mfc_buffer *buffer);

/*
 * Set size of data that has been put into 'buffer' in bytes.
 */
void mfc_buffer_set_input_size(struct mfc_buffer *buffer, int size);

/*
 * Get pointers to data in output buffer 'buffer'.
 */
void mfc_buffer_get_output_data(struct mfc_buffer *buffer,
				void **ybuf, void **uvbuf);

#ifdef __cplusplus
}
#endif


#endif  /* VIDEO_EXYNOS4_MFC_V4L2_INCLUDE_MFC_DECODER_H */
