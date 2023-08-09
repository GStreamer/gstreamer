/* Quicktime muxer documentation
 * Copyright (C) 2008-2010 Thiago Santos <thiagoss@embedded.ufcg.edu.br>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* ============================= qtmux ==================================== */

/**
 * SECTION:element-qtmux
 * @title: qtmux
 * @short_description: Muxer for quicktime(.mov) files
 *
 * This element merges streams (audio and video) into QuickTime(.mov) files.
 *
 * The following background intends to explain why various similar muxers
 * are present in this plugin.
 *
 * The [QuickTime file format specification](http://www.apple.com/quicktime/resources/qtfileformat.pdf)
 * served as basis for the MP4 file format specification (mp4mux), and as such
 * the QuickTime file structure is nearly identical to the so-called ISO Base
 * Media file format defined in ISO 14496-12 (except for some media specific
 * parts).
 *
 * In turn, the latter ISO Base Media format was further specialized as a
 * Motion JPEG-2000 file format in ISO 15444-3 (mj2mux)
 * and in various 3GPP(2) specs (gppmux).
 * The fragmented file features defined (only) in ISO Base Media are used by
 * ISMV files making up (a.o.) Smooth Streaming (ismlmux).
 *
 * A few properties (#GstBaseQTMux:movie-timescale, #GstBaseQTMux:trak-timescale,
 * #GstQTMuxPad:trak-timescale) allow adjusting some technical parameters,
 * which might be useful in (rare) cases to resolve compatibility issues in
 * some situations.
 *
 * Some other properties influence the result more fundamentally.
 * A typical mov/mp4 file's metadata (aka moov) is located at the end of the
 * file, somewhat contrary to this usually being called "the header".
 * However, a #GstBaseQTMux:faststart file will (with some effort) arrange this to
 * be located near start of the file, which then allows it e.g. to be played
 * while downloading. Alternatively, rather than having one chunk of metadata at
 * start (or end), there can be some metadata at start and most of the other
 * data can be spread out into fragments of #GstBaseQTMux:fragment-duration.
 * If such fragmented layout is intended for streaming purposes, then
 * #GstQTMux:streamable allows foregoing to add index metadata (at the end of
 * file).
 *
 * When the maximum duration to be recorded can be known in advance, #GstQTMux
 * also supports a 'Robust Muxing' mode. In robust muxing mode,  space for the
 * headers are reserved at the start of muxing, and rewritten at a configurable
 * interval, so that the output file is always playable, even if the recording
 * is interrupted uncleanly by a crash. Robust muxing mode requires a seekable
 * output, such as filesink, because it needs to rewrite the start of the file.
 *
 * To enable robust muxing mode, set the #GstBaseQTMux:reserved-moov-update-period
 * and #GstBaseQTMux:reserved-max-duration property. Also present is the
 * #GstBaseQTMux:reserved-bytes-per-sec property, which can be increased if
 * for some reason the default is not large enough and the initial reserved
 * space for headers is too small. Applications can monitor the
 * #GstBaseQTMux:reserved-duration-remaining property to see how close to full
 * the reserved space is becoming.
 *
 * Applications that wish to be able to use/edit a file while it is being
 * written to by live content, can use the "Robust Prefill Muxing" mode. That
 * mode is a variant of the "Robust Muxing" mode in that it will pre-allocate a
 * completely valid header from the start for all tracks (i.e. it appears as
 * though the file is "reserved-max-duration" long with all samples
 * present). This mode can be enabled by setting the
 * #GstBaseQTMux:reserved-moov-update-period and #GstBaseQTMux:reserved-prefill
 * properties. Note that this mode is only possible with input streams that have
 * a fixed sample size (such as raw audio and Prores Video) and that don't
 * have reordered samples.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 v4l2src num-buffers=500 ! video/x-raw,width=320,height=240 ! videoconvert ! qtmux ! filesink location=video.mov
 * ]|
 * Records a video stream captured from a v4l2 device and muxes it into a qt file.
 *
 */

/* ============================= mp4mux ==================================== */

/**
 * SECTION:element-mp4mux
 * @title: mp4mux
 * @short_description: Muxer for ISO MPEG-4 (.mp4) files
 *
 * This element merges streams (audio and video) into ISO MPEG-4 (.mp4) files.
 *
 * The following background intends to explain why various similar muxers
 * are present in this plugin.
 *
 * The [QuickTime file format specification](http://www.apple.com/quicktime/resources/qtfileformat.pdf)
 * served as basis for the MP4 file format specification (mp4mux), and as such
 * the QuickTime file structure is nearly identical to the so-called ISO Base
 * Media file format defined in ISO 14496-12 (except for some media specific
 * parts).
 *
 * In turn, the latter ISO Base Media format was further specialized as a
 * Motion JPEG-2000 file format in ISO 15444-3 (mj2mux)
 * and in various 3GPP(2) specs (3gppmux).
 * The fragmented file features defined (only) in ISO Base Media are used by
 * ISMV files making up (a.o.) Smooth Streaming (ismlmux).
 *
 * A few properties (#GstBaseQTMux:movie-timescale, #GstBaseQTMux:trak-timescale)
 * allow adjusting some technical parameters, which might be useful in (rare)
 * cases to resolve compatibility issues in some situations.
 *
 * Some other properties influence the result more fundamentally.
 * A typical mov/mp4 file's metadata (aka moov) is located at the end of the
 * file, somewhat contrary to this usually being called "the header".
 * However, a #GstBaseQTMux:faststart file will (with some effort) arrange this to
 * be located near start of the file, which then allows it e.g. to be played
 * while downloading. Alternatively, rather than having one chunk of metadata at
 * start (or end), there can be some metadata at start and most of the other
 * data can be spread out into fragments of #GstBaseQTMux:fragment-duration.
 * If such fragmented layout is intended for streaming purposes, then
 * #GstMP4Mux:streamable allows foregoing to add index metadata (at the end of
 * file).
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 gst-launch-1.0 v4l2src num-buffers=50 ! queue ! x264enc ! mp4mux ! filesink location=video.mp4
 * ]|
 * Records a video stream captured from a v4l2 device, encodes it into H.264
 * and muxes it into an mp4 file.
 *
 */

/* ============================= 3gppmux ==================================== */

/**
 * SECTION:element-3gppmux
 * @title: 3gppmux
 * @short_description: Muxer for 3GPP (.3gp) files
 *
 * This element merges streams (audio and video) into 3GPP (.3gp) files.
 *
 * The following background intends to explain why various similar muxers
 * are present in this plugin.
 *
 * The [QuickTime file format specification](http://www.apple.com/quicktime/resources/qtfileformat.pdf)
 * served as basis for the MP4 file format specification (mp4mux), and as such
 * the QuickTime file structure is nearly identical to the so-called ISO Base
 * Media file format defined in ISO 14496-12 (except for some media specific
 * parts).
 *
 * In turn, the latter ISO Base Media format was further specialized as a
 * Motion JPEG-2000 file format in ISO 15444-3 (mj2mux)
 * and in various 3GPP(2) specs (3gppmux).
 * The fragmented file features defined (only) in ISO Base Media are used by
 * ISMV files making up (a.o.) Smooth Streaming (ismlmux).
 *
 * A few properties (#GstBaseQTMux:movie-timescale, #GstBaseQTMux:trak-timescale)
 * allow adjusting some technical parameters, which might be useful in (rare)
 * cases to resolve compatibility issues in some situations.
 *
 * Some other properties influence the result more fundamentally.
 * A typical mov/mp4 file's metadata (aka moov) is located at the end of the file,
 * somewhat contrary to this usually being called "the header". However, a
 * #GstBaseQTMux:faststart file will (with some effort) arrange this to be located
 * near start of the file, which then allows it e.g. to be played while
 * downloading. Alternatively, rather than having one chunk of metadata at start
 * (or end), there can be some metadata at start and most of the other data can
 * be spread out into fragments of #GstBaseQTMux:fragment-duration. If such
 * fragmented layout is intended for streaming purposes, then
 * #Gst3GPPMux:streamable allows foregoing to add index metadata (at the end of
 * file).
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 v4l2src num-buffers=50 ! queue ! ffenc_h263 ! 3gppmux ! filesink location=video.3gp
 * ]|
 * Records a video stream captured from a v4l2 device, encodes it into H.263
 * and muxes it into an 3gp file.
 *
 * Documentation last reviewed on 2011-04-21
 */

/* ============================= mj2pmux ==================================== */

/**
 * SECTION:element-mj2mux
 * @title: mj2mux
 * @short_description: Muxer for Motion JPEG-2000 (.mj2) files
 *
 * This element merges streams (audio and video) into MJ2 (.mj2) files.
 *
 * The following background intends to explain why various similar muxers
 * are present in this plugin.
 *
 * The [QuickTime file format specification](http://www.apple.com/quicktime/resources/qtfileformat.pdf)
 * served as basis for the MP4 file format specification (mp4mux), and as such
 * the QuickTime file structure is nearly identical to the so-called ISO Base
 * Media file format defined in ISO 14496-12 (except for some media specific
 * parts).
 *
 * In turn, the latter ISO Base Media format was further specialized as a
 * Motion JPEG-2000 file format in ISO 15444-3 (mj2mux)
 * and in various 3GPP(2) specs (3gppmux).
 * The fragmented file features defined (only) in ISO Base Media are used by
 * ISMV files making up (a.o.) Smooth Streaming (ismlmux).
 *
 * A few properties (#GstBaseQTMux:movie-timescale, #GstBaseQTMux:trak-timescale)
 * allow adjusting some technical parameters, which might be useful in (rare)
 * cases to resolve compatibility issues in some situations.
 *
 * Some other properties influence the result more fundamentally.
 * A typical mov/mp4 file's metadata (aka moov) is located at the end of the file,
 * somewhat contrary to this usually being called "the header". However, a
 * #GstBaseQTMux:faststart file will (with some effort) arrange this to be located
 * near start of the file, which then allows it e.g. to be played while
 * downloading. Alternatively, rather than having one chunk of metadata at start
 * (or end), there can be some metadata at start and most of the other data can
 * be spread out into fragments of #GstBaseQTMux:fragment-duration. If such
 * fragmented layout is intended for streaming purposes, then
 * #GstMJ2Mux:streamable allows foregoing to add index metadata (at the end of
 * file).
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 v4l2src num-buffers=50 ! queue ! jp2kenc ! mj2mux ! filesink location=video.mj2
 * ]|
 * Records a video stream captured from a v4l2 device, encodes it into JPEG-2000
 * and muxes it into an mj2 file.
 *
 * Documentation last reviewed on 2011-04-21
 */

/* ============================= ismlmux ==================================== */

/**
 * SECTION:element-ismlmux
 * @title: ismlmux
 * @short_description: Muxer for ISML smooth streaming (.isml) files
 *
 * This element merges streams (audio and video) into ISML (.isml) files.
 *
 * The following background intends to explain why various similar muxers
 * are present in this plugin.
 *
 * The [QuickTime file format specification](http://www.apple.com/quicktime/resources/qtfileformat.pdf)
 * served as basis for the MP4 file format specification (mp4mux), and as such
 * the QuickTime file structure is nearly identical to the so-called ISO Base
 * Media file format defined in ISO 14496-12 (except for some media specific
 * parts).
 *
 * In turn, the latter ISO Base Media format was further specialized as a
 * Motion JPEG-2000 file format in ISO 15444-3 (mj2mux)
 * and in various 3GPP(2) specs (3gppmux).
 * The fragmented file features defined (only) in ISO Base Media are used by
 * ISMV files making up (a.o.) Smooth Streaming (ismlmux).
 *
 * A few properties (#GstBaseQTMux:movie-timescale, #GstBaseQTMux:trak-timescale)
 * allow adjusting some technical parameters, which might be useful in (rare)
 * cases to resolve compatibility issues in some situations.
 *
 * Some other properties influence the result more fundamentally.
 * A typical mov/mp4 file's metadata (aka moov) is located at the end of the file,
 * somewhat contrary to this usually being called "the header". However, a
 * #GstBaseQTMux:faststart file will (with some effort) arrange this to be located
 * near start of the file, which then allows it e.g. to be played while
 * downloading. Alternatively, rather than having one chunk of metadata at start
 * (or end), there can be some metadata at start and most of the other data can
 * be spread out into fragments of #GstBaseQTMux:fragment-duration. If such
 * fragmented layout is intended for streaming purposes, then
 * #GstISMLMux:streamable allows foregoing to add index metadata (at the end of
 * file).
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 videotestsrc num-buffers=50 ! queue ! x264enc ! ismlmux fragment-duration=10 ! filesink location=video.isml
 * ]|
 * Records a video stream captured from a v4l2 device, encodes it into H.264
 * and muxes it into an isml file.
 *
 * Documentation last reviewed on 2011-04-21
 */
