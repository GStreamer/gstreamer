/*
 *  gstvaapidecodedoc.c - VA-API video decoders documentation
 *
 *  Copyright (C) 2016 Intel Corporation
 *    Author: Victor Jaquez <victorx.jaquez@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:element-vaapijpegdec
 * @short_description: A VA-API based JPEG image decoder
 *
 * vaapijpegdec decodes a JPEG image to surfaces suitable for the
 * vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=~/image.jpeg ! jpegparse ! vaapijpegdec ! imagefreeze ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapimpeg2dec
 * @short_description: A VA-API based MPEG2 video decoder
 *
 * vaapimpeg2dec decodes from MPEG2 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=~/sample.mpg ! mpegpsdemux ! vaapimpeg2dec ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapimpeg4dec
 * @short_description: A VA-API based MPEG4 video decoder
 *
 * vaapimpeg4dec decodes from MPEG4 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=~/sample.mpeg4 ! mpeg4videoparse ! vaapimpeg4dec ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapih263dec
 * @short_description: A VA-API based H263 video decoder
 *
 * vaapih263dec decodes from H263 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=~/sample.h263 ! h263parse ! vaapih263dec ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapih264dec
 * @short_description: A VA-API based H264 video decoder
 *
 * vaapih264dec decodes from H264 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=~/big_buck_bunny.mov ! qtdemux ! h264parse ! vaapih264dec ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapih265dec
 * @short_description: A VA-API based H265 video decoder
 *
 * vaapih265dec decodes from H265 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=./sample.bin ! h265parse ! vaapih265dec ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapivc1dec
 * @short_description: A VA-API based VC1 video decoder
 *
 * vaapivc1dec decodes from VC1 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=~/elephants_dream.wmv  ! asfdemux ! vaapivc1dec ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapivp8dec
 * @short_description: A VA-API based VP8 video decoder
 *
 * vaapivp8dec decodes from VP8 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 filesrc location=./sample.webm ! matroskademux ! vaapivp8dec ! vaapisink
 * ]|
 */

/**
 * SECTION:element-vaapivp9dec
 * @short_description: A VA-API based VP9 video decoder
 *
 * vaapivp9dec decodes from VP9 bitstreams to surfaces suitable
 * for the vaapisink or vaapipostproc elements using the installed
 * [VA-API](https://wiki.freedesktop.org/www/Software/vaapi/) back-end.
 *
 * In the case of OpenGL based elements, the buffers have the
 * #GstVideoGLTextureUploadMeta meta, which efficiently copies the
 * content of the VA-API surface into a GL texture.
 *
 * Also it can deliver normal video buffers that can be rendered or
 * processed by other elements, but the performance would be rather
 * bad.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location=./sample.vp9.webm ! ivfparse ! vaapivp9dec ! vaapisink
 * ]|
 */
