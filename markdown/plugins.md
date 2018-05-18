# List of Elements and Plugins

<!-- WARNING: This page is generated! Any modifications will be overwritten -->

Note: this list is not complete! It does not contain OS-specific plugins
for Android, Windows, macOS, iOS, or wrapper plugins (gst-libav, gst-omx),
nor gst-rtsp-server or gstreamer-vaapi elements.

There may be links to pages that don't exist, this means that the element or
plugin does not have documentation yet or the documentation is not hooked up
properly (help welcome!).

| Element | Description | Plugin  | Module |
|---------|-------------|---------|--------|
|[3gppmux][element-3gppmux]|Multiplex audio and video into a 3GPP file|[isomp4][isomp4]|[gst-plugins-good][good]|
|a2dpsink|Plays audio to an A2DP device|bluez|[gst-plugins-bad][bad]|
|[a52dec][element-a52dec]|Decodes ATSC A/52 encoded audio streams|[a52dec][a52dec]|[gst-plugins-ugly][ugly]|
|[aacparse][element-aacparse]|Advanced Audio Coding parser|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|[aasink][element-aasink]|An ASCII art videosink|[aasink][aasink]|[gst-plugins-good][good]|
|[ac3parse][element-ac3parse]|AC3 parser|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|[accurip][element-accurip]|Computes an AccurateRip CRC|accurip|[gst-plugins-bad][bad]|
|[adder][element-adder]|Add N audio channels together|[adder][adder]|[gst-plugins-base][base]|
|adpcmdec|Decode MS and IMA ADPCM audio|adpcmdec|[gst-plugins-bad][bad]|
|adpcmenc|Encode ADPCM audio|adpcmenc|[gst-plugins-bad][bad]|
|[agingtv][element-agingtv]|AgingTV adds age to video input using scratches and dust|[effectv][effectv]|[gst-plugins-good][good]|
|[aiffmux][element-aiffmux]|Multiplex raw audio into AIFF|[aiff][aiff]|[gst-plugins-bad][bad]|
|[aiffparse][element-aiffparse]|Parse a .aiff file into raw audio|[aiff][aiff]|[gst-plugins-bad][bad]|
|[alawdec][element-alawdec]|Convert 8bit A law to 16bit PCM|[alaw][alaw]|[gst-plugins-good][good]|
|[alawenc][element-alawenc]|Convert 16bit PCM to 8bit A law|[alaw][alaw]|[gst-plugins-good][good]|
|[alpha][element-alpha]|Adds an alpha channel to video - uniform or via chroma-keying|[alpha][alpha]|[gst-plugins-good][good]|
|[alphacolor][element-alphacolor]|ARGB from/to AYUV colorspace conversion preserving the alpha channel|[alphacolor][alphacolor]|[gst-plugins-good][good]|
|[alsamidisrc][element-alsamidisrc]|Push ALSA MIDI sequencer events around|[alsa][alsa]|[gst-plugins-base][base]|
|[alsasink][element-alsasink]|Output to a sound card via ALSA|[alsa][alsa]|[gst-plugins-base][base]|
|[alsasrc][element-alsasrc]|Read from a sound card via ALSA|[alsa][alsa]|[gst-plugins-base][base]|
|[amrnbdec][element-amrnbdec]|Adaptive Multi-Rate Narrow-Band audio decoder|[amrnb][amrnb]|[gst-plugins-ugly][ugly]|
|[amrnbenc][element-amrnbenc]|Adaptive Multi-Rate Narrow-Band audio encoder|[amrnb][amrnb]|[gst-plugins-ugly][ugly]|
|[amrparse][element-amrparse]|Adaptive Multi-Rate audio parser|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|[amrwbdec][element-amrwbdec]|Adaptive Multi-Rate Wideband audio decoder|[amrwbdec][amrwbdec]|[gst-plugins-ugly][ugly]|
|[apedemux][element-apedemux]|Read and output APE tags while demuxing the contents|[apetag][apetag]|[gst-plugins-good][good]|
|[apev2mux][element-apev2mux]|Adds an APEv2 header to the beginning of files using taglib|[taglib][taglib]|[gst-plugins-good][good]|
|[appsink][element-appsink]|Allow the application to get access to raw buffer|[app][app]|[gst-plugins-base][base]|
|[appsrc][element-appsrc]|Allow the application to feed buffers to a pipeline|[app][app]|[gst-plugins-base][base]|
|asfdemux|Demultiplexes ASF Streams|[asf][asf]|[gst-plugins-ugly][ugly]|
|asfmux|Muxes audio and video into an ASF stream|asfmux|[gst-plugins-bad][bad]|
|asfparse|Parses ASF|asfmux|[gst-plugins-bad][bad]|
|[aspectratiocrop][element-aspectratiocrop]|Crops video into a user-defined aspect-ratio|[videocrop][videocrop]|[gst-plugins-good][good]|
|[assrender][element-assrender]|Renders ASS/SSA subtitles with libass|[assrender][assrender]|[gst-plugins-bad][bad]|
|[asteriskh263][element-asteriskh263]|Extracts H263 video from RTP and encodes in Asterisk H263 format|[rtp][rtp]|[gst-plugins-good][good]|
|[audioamplify][element-audioamplify]|Amplifies an audio stream by a given factor|[audiofx][audiofx]|[gst-plugins-good][good]|
|audiochannelmix|Mixes left/right channels of stereo audio|audiofxbad|[gst-plugins-bad][bad]|
|[audiochebband][element-audiochebband]|Chebyshev band pass and band reject filter|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audiocheblimit][element-audiocheblimit]|Chebyshev low pass and high pass filter|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audioconvert][element-audioconvert]|Convert audio to different formats|[audioconvert][audioconvert]|[gst-plugins-base][base]|
|[audiodynamic][element-audiodynamic]|Compressor and Expander|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audioecho][element-audioecho]|Adds an echo or reverb effect to an audio stream|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audiofirfilter][element-audiofirfilter]|Generic audio FIR filter with custom filter kernel|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audioiirfilter][element-audioiirfilter]|Generic audio IIR filter with custom filter kernel|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audiointerleave][element-audiointerleave]|Mixes multiple audio streams|[audiomixer][audiomixer]|[gst-plugins-bad][bad]|
|[audioinvert][element-audioinvert]|Swaps upper and lower half of audio samples|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audiokaraoke][element-audiokaraoke]|Removes voice from sound|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audiomixer][element-audiomixer]|Mixes multiple audio streams|[audiomixer][audiomixer]|[gst-plugins-bad][bad]|
|[audiopanorama][element-audiopanorama]|Positions audio streams in the stereo panorama|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audioparse][element-audioparse]|Converts stream into audio frames (deprecated: use rawaudioparse instead)|[rawparse][rawparse]|[gst-plugins-bad][bad]|
|[audiorate][element-audiorate]|Drops/duplicates/adjusts timestamps on audio samples to make a perfect stream|[audiorate][audiorate]|[gst-plugins-base][base]|
|[audioresample][element-audioresample]|Resamples audio|[audioresample][audioresample]|[gst-plugins-base][base]|
|audiosegmentclip|Clips audio buffers to the configured segment|segmentclip|[gst-plugins-bad][bad]|
|[audiotestsrc][element-audiotestsrc]|Creates audio test signals of given frequency and volume|[audiotestsrc][audiotestsrc]|[gst-plugins-base][base]|
|[audiowsincband][element-audiowsincband]|Band pass and band reject windowed sinc filter|[audiofx][audiofx]|[gst-plugins-good][good]|
|[audiowsinclimit][element-audiowsinclimit]|Low pass and high pass windowed sinc filter|[audiofx][audiofx]|[gst-plugins-good][good]|
|[auparse][element-auparse]|Parse an .au file into raw audio|[auparse][auparse]|[gst-plugins-good][good]|
|[autoaudiosink][element-autoaudiosink]|Wrapper audio sink for automatically detected audio sink|[autodetect][autodetect]|[gst-plugins-good][good]|
|[autoaudiosrc][element-autoaudiosrc]|Wrapper audio source for automatically detected audio source|[autodetect][autodetect]|[gst-plugins-good][good]|
|[autoconvert][element-autoconvert]|Selects the right transform element based on the caps|[autoconvert][autoconvert]|[gst-plugins-bad][bad]|
|autovideoconvert|Selects the right color space convertor based on the caps|[autoconvert][autoconvert]|[gst-plugins-bad][bad]|
|[autovideosink][element-autovideosink]|Wrapper video sink for automatically detected video sink|[autodetect][autodetect]|[gst-plugins-good][good]|
|[autovideosrc][element-autovideosrc]|Wrapper video source for automatically detected video source|[autodetect][autodetect]|[gst-plugins-good][good]|
|avdtpsink|Plays audio to an A2DP device|bluez|[gst-plugins-bad][bad]|
|avdtpsrc|Receives audio from an A2DP device|bluez|[gst-plugins-bad][bad]|
|[avidemux][element-avidemux]|Demultiplex an avi file into audio and video|[avi][avi]|[gst-plugins-good][good]|
|[avimux][element-avimux]|Muxes audio and video into an avi stream|[avi][avi]|[gst-plugins-good][good]|
|[avisubtitle][element-avisubtitle]|Parse avi subtitle stream|[avi][avi]|[gst-plugins-good][good]|
|bayer2rgb|Converts video/x-bayer to video/x-raw|[bayer][bayer]|[gst-plugins-bad][bad]|
|bpmdetect|Detect the BPM of an audio stream|[soundtouch][soundtouch]|[gst-plugins-bad][bad]|
|[breakmydata][element-breakmydata]|randomly change data in the stream|[debug][debug]|[gst-plugins-good][good]|
|[bs2b][element-bs2b]|Improve headphone listening of stereo audio records using the bs2b library.|[bs2b][bs2b]|[gst-plugins-bad][bad]|
|[bulge][element-bulge]|Adds a protuberance in the center point|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[burn][element-burn]|Burn adjusts the colors in the video signal.|[gaudieffects][gaudieffects]|[gst-plugins-bad][bad]|
|bz2dec|Decodes compressed streams|[bz2][bz2]|[gst-plugins-bad][bad]|
|bz2enc|Compresses streams|[bz2][bz2]|[gst-plugins-bad][bad]|
|[cacasink][element-cacasink]|A colored ASCII art videosink|[cacasink][cacasink]|[gst-plugins-good][good]|
|[cairooverlay][element-cairooverlay]|Render overlay on a video stream using Cairo|[cairo][cairo]|[gst-plugins-good][good]|
|[camerabin][element-camerabin]|Take image snapshots and record movies from camera|[camerabin][camerabin]|[gst-plugins-bad][bad]|
|[capsfilter][element-capsfilter]|Pass data without modification, limiting formats|[coreelements][coreelements]|[gstreamer][core]|
|[capssetter][element-capssetter]|Set/merge caps on stream|[debug][debug]|[gst-plugins-good][good]|
|[cdiocddasrc][element-cdiocddasrc]|Read audio from CD using libcdio|[cdio][cdio]|[gst-plugins-ugly][ugly]|
|[cdparanoiasrc][element-cdparanoiasrc]|Read audio from CD in paranoid mode|[cdparanoia][cdparanoia]|[gst-plugins-base][base]|
|checksumsink|Calculates a checksum for buffers|[debugutilsbad][debugutilsbad]|[gst-plugins-bad][bad]|
|chopmydata|FIXME|[debugutilsbad][debugutilsbad]|[gst-plugins-bad][bad]|
|chromahold|Removes all color information except for one color|[coloreffects][coloreffects]|[gst-plugins-bad][bad]|
|chromaprint|Find an audio fingerprint using the Chromaprint library|chromaprint|[gst-plugins-bad][bad]|
|[chromium][element-chromium]|Chromium breaks the colors of the video signal.|[gaudieffects][gaudieffects]|[gst-plugins-bad][bad]|
|[circle][element-circle]|Warps the picture into an arc shaped form|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[clockoverlay][element-clockoverlay]|Overlays the current clock time on a video stream|[pango][pango]|[gst-plugins-base][base]|
|[coloreffects][element-coloreffects]|Color Look-up Table filter|[coloreffects][coloreffects]|[gst-plugins-bad][bad]|
|combdetect|Detect combing artifacts in video stream|ivtc|[gst-plugins-bad][bad]|
|compare|Compares incoming buffers|[debugutilsbad][debugutilsbad]|[gst-plugins-bad][bad]|
|[compositor][element-compositor]|Composite multiple video streams|compositor|[gst-plugins-bad][bad]|
|[concat][element-concat]|Concatenate multiple streams|[coreelements][coreelements]|[gstreamer][core]|
|[cpureport][element-cpureport]|Post cpu usage information every buffer|[debug][debug]|[gst-plugins-good][good]|
|curlfilesink|Upload data over FILE protocol using libcurl|[curl][curl]|[gst-plugins-bad][bad]|
|curlftpsink|Upload data over FTP protocol using libcurl|[curl][curl]|[gst-plugins-bad][bad]|
|curlhttpsink|Upload data over HTTP/HTTPS protocol using libcurl|[curl][curl]|[gst-plugins-bad][bad]|
|curlsmtpsink|Upload data over SMTP protocol using libcurl|[curl][curl]|[gst-plugins-bad][bad]|
|[cutter][element-cutter]|Audio Cutter to split audio into non-silent bits|[cutter][cutter]|[gst-plugins-good][good]|
|[cvdilate][element-cvdilate]|Applies cvDilate OpenCV function to the image|[opencv][opencv]|[gst-plugins-bad][bad]|
|[cvequalizehist][element-cvequalizehist]|Applies cvEqualizeHist OpenCV function to the image|[opencv][opencv]|[gst-plugins-bad][bad]|
|[cverode][element-cverode]|Applies cvErode OpenCV function to the image|[opencv][opencv]|[gst-plugins-bad][bad]|
|[cvlaplace][element-cvlaplace]|Applies cvLaplace OpenCV function to the image|[opencv][opencv]|[gst-plugins-bad][bad]|
|[cvsmooth][element-cvsmooth]|Applies cvSmooth OpenCV function to the image|[opencv][opencv]|[gst-plugins-bad][bad]|
|[cvsobel][element-cvsobel]|Applies cvSobel OpenCV function to the image|[opencv][opencv]|[gst-plugins-bad][bad]|
|dashdemux|Dynamic Adaptive Streaming over HTTP demuxer|dashdemux|[gst-plugins-bad][bad]|
|[dataurisrc][element-dataurisrc]|Handles data: uris|[dataurisrc][dataurisrc]|[gst-plugins-bad][bad]|
|[dcaparse][element-dcaparse]|DCA parser|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|debugspy|DebugSpy provides information on buffers with bus messages|[debugutilsbad][debugutilsbad]|[gst-plugins-bad][bad]|
|decklinkaudiosink|Decklink Sink|decklink|[gst-plugins-bad][bad]|
|decklinkaudiosrc|Decklink Source|decklink|[gst-plugins-bad][bad]|
|decklinkvideosink|Decklink Sink|decklink|[gst-plugins-bad][bad]|
|decklinkvideosrc|Decklink Source|decklink|[gst-plugins-bad][bad]|
|[decodebin][element-decodebin]|Autoplug and decode to raw media|[playback][playback]|[gst-plugins-base][base]|
|[decodebin3][element-decodebin3]|Autoplug and decode to raw media|[playback][playback]|[gst-plugins-base][base]|
|[deinterlace][element-deinterlace]|Deinterlace Methods ported from DScaler/TvTime|[deinterlace][deinterlace]|[gst-plugins-good][good]|
|[deinterleave][element-deinterleave]|Splits one interleaved multichannel audio stream into many mono audio streams|[interleave][interleave]|[gst-plugins-good][good]|
|[dicetv][element-dicetv]|'Dices' the screen up into many small squares|[effectv][effectv]|[gst-plugins-good][good]|
|[diffuse][element-diffuse]|Diffuses the image by moving its pixels in random directions|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[dilate][element-dilate]|Dilate copies the brightest pixel around.|[gaudieffects][gaudieffects]|[gst-plugins-bad][bad]|
|diracparse|Parses Dirac streams|videoparsersbad|[gst-plugins-bad][bad]|
|[directsoundsink][element-directsoundsink]|DirectSound audio sink|[directsound][directsound]|[gst-plugins-good][good]|
|disparity|Calculates the stereo disparity map from two (sequences of) rectified and aligned stereo images|[opencv][opencv]|[gst-plugins-bad][bad]|
|[dodge][element-dodge]|Dodge saturates the colors in the video signal.|[gaudieffects][gaudieffects]|[gst-plugins-bad][bad]|
|[downloadbuffer][element-downloadbuffer]|Download Buffer element|[coreelements][coreelements]|[gstreamer][core]|
|dtlsdec|Decodes DTLS packets|dtls|[gst-plugins-bad][bad]|
|dtlsenc|Encodes packets with DTLS|dtls|[gst-plugins-bad][bad]|
|dtlssrtpdec|Decodes SRTP packets with a key received from DTLS|dtls|[gst-plugins-bad][bad]|
|dtlssrtpdemux|Demultiplexes DTLS and SRTP packets|dtls|[gst-plugins-bad][bad]|
|dtlssrtpenc|Encodes SRTP packets with a key received from DTLS|dtls|[gst-plugins-bad][bad]|
|[dtmfdetect][element-dtmfdetect]|This element detects DTMF tones|spandsp|[gst-plugins-bad][bad]|
|[dtmfsrc][element-dtmfsrc]|Generates DTMF tones|[dtmf][dtmf]|[gst-plugins-good][good]|
|[dtsdec][element-dtsdec]|Decodes DTS audio streams|[dtsdec][dtsdec]|[gst-plugins-bad][bad]|
|[dv1394src][element-dv1394src]|Source for DV video data from firewire port|[1394][1394]|[gst-plugins-good][good]|
|dvbbasebin|Access descramble and split DVB streams|[dvb][dvb]|[gst-plugins-bad][bad]|
|[dvbsrc][element-dvbsrc]|Digital Video Broadcast Source|[dvb][dvb]|[gst-plugins-bad][bad]|
|dvbsuboverlay|Renders DVB subtitles|dvbsuboverlay|[gst-plugins-bad][bad]|
|[dvdec][element-dvdec]|Uses libdv to decode DV video (smpte314) (libdv.sourceforge.net)|[dv][dv]|[gst-plugins-good][good]|
|[dvdemux][element-dvdemux]|Uses libdv to separate DV audio from DV video (libdv.sourceforge.net)|[dv][dv]|[gst-plugins-good][good]|
|dvdlpcmdec|Decode DVD LPCM frames into standard PCM audio|[dvdlpcmdec][dvdlpcmdec]|[gst-plugins-ugly][ugly]|
|dvdreadsrc|Access a DVD title/chapter/angle using libdvdread|[dvdread][dvdread]|[gst-plugins-ugly][ugly]|
|[dvdspu][element-dvdspu]|Parses Sub-Picture command streams and renders the SPU overlay onto the video as it passes through|[dvdspu][dvdspu]|[gst-plugins-bad][bad]|
|dvdsubdec|Decodes DVD subtitles into AYUV video frames|[dvdsub][dvdsub]|[gst-plugins-ugly][ugly]|
|dvdsubparse|Parses and packetizes DVD subtitle streams|[dvdsub][dvdsub]|[gst-plugins-ugly][ugly]|
|[dynudpsink][element-dynudpsink]|Send data over the network via UDP with packet destinations picked up dynamically from meta on the buffers passed|[udp][udp]|[gst-plugins-good][good]|
|[edgedetect][element-edgedetect]|Performs canny edge detection on videos and images.|[opencv][opencv]|[gst-plugins-bad][bad]|
|[edgetv][element-edgetv]|Apply edge detect on video|[effectv][effectv]|[gst-plugins-good][good]|
|[encodebin][element-encodebin]|Convenience encoding/muxing element|[encoding][encoding]|[gst-plugins-base][base]|
|[equalizer-10bands][element-equalizer-10bands]|Direct Form 10 band IIR equalizer|[equalizer][equalizer]|[gst-plugins-good][good]|
|[equalizer-3bands][element-equalizer-3bands]|Direct Form 3 band IIR equalizer|[equalizer][equalizer]|[gst-plugins-good][good]|
|[equalizer-nbands][element-equalizer-nbands]|Direct Form IIR equalizer|[equalizer][equalizer]|[gst-plugins-good][good]|
|errorignore|Pass through all packets but ignore some GstFlowReturn types|[debugutilsbad][debugutilsbad]|[gst-plugins-bad][bad]|
|[exclusion][element-exclusion]|Exclusion exclodes the colors in the video signal.|[gaudieffects][gaudieffects]|[gst-plugins-bad][bad]|
|[faac][element-faac]|Free MPEG-2/4 AAC encoder|[faac][faac]|[gst-plugins-bad][bad]|
|[faad][element-faad]|Free MPEG-2/4 AAC decoder|[faad][faad]|[gst-plugins-bad][bad]|
|[faceblur][element-faceblur]|Blurs faces in images and videos|[opencv][opencv]|[gst-plugins-bad][bad]|
|[facedetect][element-facedetect]|Performs face detection on videos and images, providing detected positions via bus messages|[opencv][opencv]|[gst-plugins-bad][bad]|
|[fakesink][element-fakesink]|Black hole for data|[coreelements][coreelements]|[gstreamer][core]|
|[fakesrc][element-fakesrc]|Push empty (no data) buffers around|[coreelements][coreelements]|[gstreamer][core]|
|fbdevsink|Linux framebuffer videosink|fbdevsink|[gst-plugins-bad][bad]|
|[fdsink][element-fdsink]|Write data to a file descriptor|[coreelements][coreelements]|[gstreamer][core]|
|[fdsrc][element-fdsrc]|Read from a file descriptor|[coreelements][coreelements]|[gstreamer][core]|
|[festival][element-festival]|Synthesizes plain text into audio|[festival][festival]|[gst-plugins-bad][bad]|
|fieldanalysis|Analyse fields from video frames to identify if they are progressive/telecined/interlaced|fieldanalysis|[gst-plugins-bad][bad]|
|[filesink][element-filesink]|Write stream to a file|[coreelements][coreelements]|[gstreamer][core]|
|[filesrc][element-filesrc]|Read from arbitrary point in a file|[coreelements][coreelements]|[gstreamer][core]|
|[fisheye][element-fisheye]|Simulate a fisheye lens by zooming on the center of the image and compressing the edges|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[flacdec][element-flacdec]|Decodes FLAC lossless audio streams|[flac][flac]|[gst-plugins-good][good]|
|[flacenc][element-flacenc]|Encodes audio with the FLAC lossless audio encoder|[flac][flac]|[gst-plugins-good][good]|
|[flacparse][element-flacparse]|Parses audio with the FLAC lossless audio codec|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|[flactag][element-flactag]|Rewrite tags in a FLAC file|[flac][flac]|[gst-plugins-good][good]|
|flitetestsrc|Creates audio test signals identifying channels|flite|[gst-plugins-bad][bad]|
|fluiddec|Midi Synthesizer Element|fluidsynthmidi|[gst-plugins-bad][bad]|
|[flvdemux][element-flvdemux]|Demux FLV feeds into digital streams|[flv][flv]|[gst-plugins-good][good]|
|[flvmux][element-flvmux]|Muxes video/audio streams into a FLV stream|[flv][flv]|[gst-plugins-good][good]|
|[flxdec][element-flxdec]|FLC/FLI/FLX video decoder|[flxdec][flxdec]|[gst-plugins-good][good]|
|[fpsdisplaysink][element-fpsdisplaysink]|Shows the current frame-rate and drop-rate of the videosink as overlay or text on stdout|[debugutilsbad][debugutilsbad]|[gst-plugins-bad][bad]|
|freeverb|Add reverberation to audio streams|freeverb|[gst-plugins-bad][bad]|
|[funnel][element-funnel]|N-to-1 pipe fitting|[coreelements][coreelements]|[gstreamer][core]|
|[gamma][element-gamma]|Adjusts gamma on a video stream|[videofilter][videofilter]|[gst-plugins-good][good]|
|[gaussianblur][element-gaussianblur]|Perform Gaussian blur/sharpen on a video|[gaudieffects][gaudieffects]|[gst-plugins-bad][bad]|
|[gdkpixbufdec][element-gdkpixbufdec]|Decodes images in a video stream using GdkPixbuf|[gdkpixbuf][gdkpixbuf]|[gst-plugins-good][good]|
|[gdkpixbufoverlay][element-gdkpixbufoverlay]|Overlay an image onto a video stream|[gdkpixbuf][gdkpixbuf]|[gst-plugins-good][good]|
|[gdkpixbufsink][element-gdkpixbufsink]|Output images as GdkPixbuf objects in bus messages|[gdkpixbuf][gdkpixbuf]|[gst-plugins-good][good]|
|gdpdepay|Depayloads GStreamer Data Protocol buffers|gdp|[gst-plugins-bad][bad]|
|gdppay|Payloads GStreamer Data Protocol buffers|gdp|[gst-plugins-bad][bad]|
|[giosink][element-giosink]|Write to any GIO-supported location|[gio][gio]|[gst-plugins-base][base]|
|[giosrc][element-giosrc]|Read from any GIO-supported location|[gio][gio]|[gst-plugins-base][base]|
|[giostreamsink][element-giostreamsink]|Write to any GIO stream|[gio][gio]|[gst-plugins-base][base]|
|[giostreamsrc][element-giostreamsrc]|Read from any GIO stream|[gio][gio]|[gst-plugins-base][base]|
|[glcolorbalance][element-glcolorbalance]|Adjusts brightness, contrast, hue, saturation on a video stream|[opengl][opengl]|[gst-plugins-base][base]|
|[glcolorconvert][element-glcolorconvert]|Converts between color spaces using OpenGL shaders|[opengl][opengl]|[gst-plugins-base][base]|
|[glcolorscale][element-glcolorscale]|Colorspace converter and video scaler|[opengl][opengl]|[gst-plugins-base][base]|
|[gldeinterlace][element-gldeinterlace]|Deinterlacing based on fragment shaders|[opengl][opengl]|[gst-plugins-base][base]|
|[gldifferencematte][element-gldifferencematte]|Saves a background frame and replace it with a pixbuf|[opengl][opengl]|[gst-plugins-base][base]|
|[gldownload][element-gldownload]|Downloads data from OpenGL|[opengl][opengl]|[gst-plugins-base][base]|
|[gleffects][element-gleffects]|GL Shading Language effects|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_blur|GL Shading Language effects - Blur with 9x9 separable convolution Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_bulge|GL Shading Language effects - Bulge Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_fisheye|GL Shading Language effects - FishEye Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_glow|GL Shading Language effects - Glow Lighting Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_heat|GL Shading Language effects - Heat Signature Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_identity|GL Shading Language effects - Do nothing Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_laplacian|GL Shading Language effects - Laplacian Convolution Demo Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_lumaxpro|GL Shading Language effects - Luma Cross Processing Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_mirror|GL Shading Language effects - Mirror Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_sepia|GL Shading Language effects - Sepia Toning Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_sin|GL Shading Language effects - All Grey but Red Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_sobel|GL Shading Language effects - Sobel edge detection Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_square|GL Shading Language effects - Square Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_squeeze|GL Shading Language effects - Squeeze Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_stretch|GL Shading Language effects - Stretch Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_tunnel|GL Shading Language effects - Light Tunnel Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_twirl|GL Shading Language effects - Twirl Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_xpro|GL Shading Language effects - Cross Processing Effect|[opengl][opengl]|[gst-plugins-base][base]|
|gleffects_xray|GL Shading Language effects - Glowing negative effect|[opengl][opengl]|[gst-plugins-base][base]|
|[glfilterapp][element-glfilterapp]|Use client callbacks to define the scene|[opengl][opengl]|[gst-plugins-base][base]|
|[glfilterbin][element-glfilterbin]|Infrastructure to process GL textures|[opengl][opengl]|[gst-plugins-base][base]|
|[glfiltercube][element-glfiltercube]|Map input texture on the 6 cube faces|[opengl][opengl]|[gst-plugins-base][base]|
|[glfilterglass][element-glfilterglass]|Glass Filter|[opengl][opengl]|[gst-plugins-base][base]|
|[glimagesink][element-glimagesink]|Infrastructure to process GL textures|[opengl][opengl]|[gst-plugins-base][base]|
|[glimagesinkelement][element-glimagesinkelement]|A videosink based on OpenGL|[opengl][opengl]|[gst-plugins-base][base]|
|[glmixerbin][element-glmixerbin]|OpenGL video_mixer empty bin|[openglmixers][openglmixers]|[gst-plugins-bad][bad]|
|[glmosaic][element-glmosaic]|OpenGL mosaic|[openglmixers][openglmixers]|[gst-plugins-bad][bad]|
|[gloverlay][element-gloverlay]|Overlay GL video texture with a JPEG/PNG image|[opengl][opengl]|[gst-plugins-base][base]|
|[glshader][element-glshader]|Perform operations with a GLSL shader|[opengl][opengl]|[gst-plugins-base][base]|
|[glsinkbin][element-glsinkbin]|Infrastructure to process GL textures|[opengl][opengl]|[gst-plugins-base][base]|
|[glsrcbin][element-glsrcbin]|Infrastructure to process GL textures|[opengl][opengl]|[gst-plugins-base][base]|
|[glstereomix][element-glstereomix]|OpenGL stereo video combiner|[openglmixer][openglmixer]|[gst-plugins-bad][bad]|
|[glstereosplit][element-glstereosplit]|Splits a stereoscopic stream into separate left/right streams|[opengl][opengl]|[gst-plugins-base][base]|
|[gltestsrc][element-gltestsrc]|Creates a test video stream|[opengl][opengl]|[gst-plugins-base][base]|
|[glupload][element-glupload]|Uploads data into OpenGL|[opengl][opengl]|[gst-plugins-base][base]|
|[glvideomixer][element-glvideomixer]|OpenGL video_mixer bin|[openglmixer][openglmixer]|[gst-plugins-bad][bad]|
|[glvideomixerelement][element-glvideomixerelement]|OpenGL video_mixer|[openglmixer][openglmixer]|[gst-plugins-bad][bad]|
|[glviewconvert][element-glviewconvert]|Convert stereoscopic/multiview video formats|[opengl][opengl]|[gst-plugins-base][base]|
|gmedec|Uses libgme to emulate a gaming console sound processors|gmedec|[gst-plugins-bad][bad]|
|[goom][element-goom]|Takes frames of data and outputs video frames using the GOOM filter|[goom][goom]|[gst-plugins-good][good]|
|[goom2k1][element-goom2k1]|Takes frames of data and outputs video frames using the GOOM 2k1 filter|[goom2k1][goom2k1]|[gst-plugins-good][good]|
|grabcut|(too long)|[opencv][opencv]|[gst-plugins-bad][bad]|
|gsmdec|Decodes GSM encoded audio|[gsm][gsm]|[gst-plugins-bad][bad]|
|gsmenc|Encodes GSM audio|[gsm][gsm]|[gst-plugins-bad][bad]|
|gtkglsink|A video sink that renders to a GtkWidget using OpenGL|gstgtk|[gst-plugins-bad][bad]|
|gtksink|A video sink that renders to a GtkWidget|gstgtk|[gst-plugins-bad][bad]|
|h263parse|Parses H.263 streams|videoparsersbad|[gst-plugins-bad][bad]|
|h264parse|Parses H.264 streams|videoparsersbad|[gst-plugins-bad][bad]|
|h265parse|Parses H.265 streams|videoparsersbad|[gst-plugins-bad][bad]|
|handdetect|Performs hand gesture detection on videos, providing detected hand positions via bus message and navigation event, and deals with hand gesture events|[opencv][opencv]|[gst-plugins-bad][bad]|
|[hdv1394src][element-hdv1394src]|Source for MPEG-TS video data from firewire port|[1394][1394]|[gst-plugins-good][good]|
|hlsdemux|HTTP Live Streaming demuxer|hls|[gst-plugins-bad][bad]|
|hlssink|HTTP Live Streaming sink|hls|[gst-plugins-bad][bad]|
|[icydemux][element-icydemux]|Read and output ICY tags while demuxing the contents|[icydemux][icydemux]|[gst-plugins-good][good]|
|[id3demux][element-id3demux]|Read and output ID3v1 and ID3v2 tags while demuxing the contents|[id3demux][id3demux]|[gst-plugins-good][good]|
|id3mux|Adds an ID3v2 header and ID3v1 footer to a file|id3tag|[gst-plugins-bad][bad]|
|[id3v2mux][element-id3v2mux]|Adds an ID3v2 header to the beginning of MP3 files using taglib|[taglib][taglib]|[gst-plugins-good][good]|
|[identity][element-identity]|Pass data without modification|[coreelements][coreelements]|[gstreamer][core]|
|[imagefreeze][element-imagefreeze]|Generates a still frame stream from an image|[imagefreeze][imagefreeze]|[gst-plugins-good][good]|
|[input-selector][element-input-selector]|N-to-1 input stream selector|[coreelements][coreelements]|[gstreamer][core]|
|interaudiosink|Virtual audio sink for internal process communication|inter|[gst-plugins-bad][bad]|
|interaudiosrc|Virtual audio source for internal process communication|inter|[gst-plugins-bad][bad]|
|interlace|Creates an interlaced video from progressive frames|interlace|[gst-plugins-bad][bad]|
|[interleave][element-interleave]|Folds many mono channels into one interleaved audio stream|[interleave][interleave]|[gst-plugins-good][good]|
|intersubsink|Virtual subtitle sink for internal process communication|inter|[gst-plugins-bad][bad]|
|intersubsrc|Virtual subtitle source for internal process communication|inter|[gst-plugins-bad][bad]|
|intervideosink|Virtual video sink for internal process communication|inter|[gst-plugins-bad][bad]|
|intervideosrc|Virtual video source for internal process communication|inter|[gst-plugins-bad][bad]|
|irtspparse|Parses a raw interleaved RTSP stream|[pcapparse][pcapparse]|[gst-plugins-bad][bad]|
|[ismlmux][element-ismlmux]|Multiplex audio and video into a ISML file|[isomp4][isomp4]|[gst-plugins-good][good]|
|ivfparse|Demuxes a IVF stream|ivfparse|[gst-plugins-bad][bad]|
|ivorbisdec|decode raw vorbis streams to integer audio|[ivorbisdec][ivorbisdec]|[gst-plugins-base][base]|
|ivtc|Inverse Telecine Filter|ivtc|[gst-plugins-bad][bad]|
|[jackaudiosink][element-jackaudiosink]|Output audio to a JACK server|[jack][jack]|[gst-plugins-good][good]|
|[jackaudiosrc][element-jackaudiosrc]|Captures audio from a JACK server|[jack][jack]|[gst-plugins-good][good]|
|jifmux|Remuxes JPEG images with markers and tags|[jpegformat][jpegformat]|[gst-plugins-bad][bad]|
|jp2kdecimator|Removes information from JPEG2000 streams without recompression|jp2kdecimator|[gst-plugins-bad][bad]|
|jpeg2000parse|Parses JPEG 2000 files|videoparsersbad|[gst-plugins-bad][bad]|
|[jpegdec][element-jpegdec]|Decode images from JPEG format|[jpeg][jpeg]|[gst-plugins-good][good]|
|[jpegenc][element-jpegenc]|Encode images in JPEG format|[jpeg][jpeg]|[gst-plugins-good][good]|
|[jpegparse][element-jpegparse]|Parse JPEG images into single-frame buffers|[jpegformat][jpegformat]|[gst-plugins-bad][bad]|
|[kaleidoscope][element-kaleidoscope]|Applies 'kaleidoscope' geometric transform to the image|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|katedec|Decodes Kate text streams|kate|[gst-plugins-bad][bad]|
|kateenc|Encodes Kate streams from text or subpictures|kate|[gst-plugins-bad][bad]|
|kateparse|parse raw kate streams|kate|[gst-plugins-bad][bad]|
|katetag|Retags kate streams|kate|[gst-plugins-bad][bad]|
|kmssink|Video sink using the Linux kernel mode setting API|kms|[gst-plugins-bad][bad]|
|ladspa-amp-so-amp-mono|Mono Amplifier|ladspa|[gst-plugins-bad][bad]|
|ladspa-amp-so-amp-stereo|Stereo Amplifier|ladspa|[gst-plugins-bad][bad]|
|ladspa-delay-so-delay-5s|Simple Delay Line|ladspa|[gst-plugins-bad][bad]|
|ladspa-filter-so-hpf|Simple High Pass Filter|ladspa|[gst-plugins-bad][bad]|
|ladspa-filter-so-lpf|Simple Low Pass Filter|ladspa|[gst-plugins-bad][bad]|
|ladspa-sine-so-sine-faaa|Sine Oscillator (Freq:audio, Amp:audio)|ladspa|[gst-plugins-bad][bad]|
|ladspa-sine-so-sine-faac|Sine Oscillator (Freq:audio, Amp:control)|ladspa|[gst-plugins-bad][bad]|
|ladspa-sine-so-sine-fcaa|Sine Oscillator (Freq:control, Amp:audio)|ladspa|[gst-plugins-bad][bad]|
|ladspasrc-noise-so-noise-white|White Noise Source|ladspa|[gst-plugins-bad][bad]|
|ladspasrc-sine-so-sine-fcac|Sine Oscillator (Freq:control, Amp:control)|ladspa|[gst-plugins-bad][bad]|
|[lamemp3enc][element-lamemp3enc]|High-quality free MP3 encoder|[lame][lame]|[gst-plugins-ugly][ugly]|
|[level][element-level]|RMS/Peak/Decaying Peak Level messager for audio/raw|[level][level]|[gst-plugins-good][good]|
|libvisual_bumpscope|Bumpscope visual plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|libvisual_corona|Libvisual corona plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|libvisual_infinite|Infinite visual plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|libvisual_jakdaw|jakdaw visual plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|libvisual_jess|Jess visual plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|libvisual_lv_analyzer|Libvisual analyzer plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|libvisual_lv_scope|Libvisual scope plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|libvisual_oinksie|Libvisual Oinksie visual plugin|[libvisual][libvisual]|[gst-plugins-base][base]|
|[liveadder][element-liveadder]|Mixes multiple audio streams|[audiomixer][audiomixer]|[gst-plugins-bad][bad]|
|[mad][element-mad]|Uses mad code to decode mp3 streams|[mad][mad]|[gst-plugins-ugly][ugly]|
|[marble][element-marble]|Applies a marbling effect to the image|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[matroskademux][element-matroskademux]|Demuxes Matroska/WebM streams into video/audio/subtitles|[matroska][matroska]|[gst-plugins-good][good]|
|[matroskamux][element-matroskamux]|Muxes video/audio/subtitle streams into a matroska stream|[matroska][matroska]|[gst-plugins-good][good]|
|[matroskaparse][element-matroskaparse]|Parses Matroska/WebM streams into video/audio/subtitles|[matroska][matroska]|[gst-plugins-good][good]|
|midiparse|Midi Parser Element|midi|[gst-plugins-bad][bad]|
|[mimdec][element-mimdec]|MSN Messenger compatible Mimic video decoder element|[mimic][mimic]|[gst-plugins-bad][bad]|
|[mimenc][element-mimenc]|MSN Messenger compatible Mimic video encoder element|[mimic][mimic]|[gst-plugins-bad][bad]|
|[mirror][element-mirror]|Split the image into two halves and reflect one over each other|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[mj2mux][element-mj2mux]|Multiplex audio and video into a MJ2 file|[isomp4][isomp4]|[gst-plugins-good][good]|
|mmssrc|Receive data streamed via MSFT Multi Media Server protocol|[mms][mms]|[gst-plugins-bad][bad]|
|[modplug][element-modplug]|Module decoder based on modplug engine|[modplug][modplug]|[gst-plugins-bad][bad]|
|[monoscope][element-monoscope]|Displays a highly stabilised waveform of audio input|[monoscope][monoscope]|[gst-plugins-good][good]|
|motioncells|Performs motion detection on videos and images, providing detected motion cells index via bus messages|[opencv][opencv]|[gst-plugins-bad][bad]|
|[mp4mux][element-mp4mux]|Multiplex audio and video into a MP4 file|[isomp4][isomp4]|[gst-plugins-good][good]|
|mpeg2dec|Uses libmpeg2 to decode MPEG video streams|[mpeg2dec][mpeg2dec]|[gst-plugins-ugly][ugly]|
|[mpeg2enc][element-mpeg2enc]|High-quality MPEG-1/2 video encoder|[mpeg2enc][mpeg2enc]|[gst-plugins-bad][bad]|
|mpeg4videoparse|Parses MPEG-4 Part 2 elementary video streams|videoparsersbad|[gst-plugins-bad][bad]|
|[mpegaudioparse][element-mpegaudioparse]|Parses and frames mpeg1 audio streams (levels 1-3), provides seek|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|mpegpsdemux|Demultiplexes MPEG Program Streams|mpegpsdemux|[gst-plugins-bad][bad]|
|[mpegpsmux][element-mpegpsmux]|Multiplexes media streams into an MPEG Program Stream|[mpegpsmux][mpegpsmux]|[gst-plugins-bad][bad]|
|[mpegtsmux][element-mpegtsmux]|Multiplexes media streams into an MPEG Transport Stream|[mpegtsmux][mpegtsmux]|[gst-plugins-bad][bad]|
|mpegvideoparse|Parses and frames MPEG-1 and MPEG-2 elementary video streams|videoparsersbad|[gst-plugins-bad][bad]|
|[mpg123audiodec][element-mpg123audiodec]|Decodes mp3 streams using the mpg123 library|[mpg123][mpg123]|[gst-plugins-ugly][ugly]|
|[mplex][element-mplex]|High-quality MPEG/DVD/SVCD/VCD video/audio multiplexer|[mplex][mplex]|[gst-plugins-bad][bad]|
|mssdemux|Parse and demultiplex a Smooth Streaming manifest into audio and video streams|smoothstreaming|[gst-plugins-bad][bad]|
|[mulawdec][element-mulawdec]|Convert 8bit mu law to 16bit PCM|[mulaw][mulaw]|[gst-plugins-good][good]|
|[mulawenc][element-mulawenc]|Convert 16bit PCM to 8bit mu law|[mulaw][mulaw]|[gst-plugins-good][good]|
|[multifdsink][element-multifdsink]|Send data to multiple filedescriptors|[tcp][tcp]|[gst-plugins-base][base]|
|[multifilesink][element-multifilesink]|Write buffers to a sequentially named set of files|[multifile][multifile]|[gst-plugins-good][good]|
|[multifilesrc][element-multifilesrc]|Read a sequentially named set of files into buffers|[multifile][multifile]|[gst-plugins-good][good]|
|[multipartdemux][element-multipartdemux]|demux multipart streams|[multipart][multipart]|[gst-plugins-good][good]|
|[multipartmux][element-multipartmux]|mux multipart streams|[multipart][multipart]|[gst-plugins-good][good]|
|[multiqueue][element-multiqueue]|Multiple data queue|[coreelements][coreelements]|[gstreamer][core]|
|[multisocketsink][element-multisocketsink]|Send data to multiple sockets|[tcp][tcp]|[gst-plugins-base][base]|
|[multiudpsink][element-multiudpsink]|Send data over the network via UDP to one or multiple recipients which can be added or removed at runtime using action signals|[udp][udp]|[gst-plugins-good][good]|
|mxfdemux|Demux MXF files|mxf|[gst-plugins-bad][bad]|
|mxfmux|Muxes video/audio streams into a MXF stream|mxf|[gst-plugins-bad][bad]|
|[navigationtest][element-navigationtest]|Handle navigation events showing a black square following mouse pointer|[navigationtest][navigationtest]|[gst-plugins-good][good]|
|[navseek][element-navseek]|Seek based on navigation keys left-right|[debug][debug]|[gst-plugins-good][good]|
|[neonhttpsrc][element-neonhttpsrc]|Receive data as a client over the network via HTTP using NEON|[neon][neon]|[gst-plugins-bad][bad]|
|netsim|An element that simulates network jitter, packet loss and packet duplication|netsim|[gst-plugins-bad][bad]|
|[ofa][element-ofa]|Find a music fingerprint using MusicIP's libofa|[ofa][ofa]|[gst-plugins-bad][bad]|
|[oggaviparse][element-oggaviparse]|parse an ogg avi stream into pages (info about ogg: http://xiph.org)|[ogg][ogg]|[gst-plugins-base][base]|
|[oggdemux][element-oggdemux]|demux ogg streams (info about ogg: http://xiph.org)|[ogg][ogg]|[gst-plugins-base][base]|
|[oggmux][element-oggmux]|mux ogg streams (info about ogg: http://xiph.org)|[ogg][ogg]|[gst-plugins-base][base]|
|[oggparse][element-oggparse]|parse ogg streams into pages (info about ogg: http://xiph.org)|[ogg][ogg]|[gst-plugins-base][base]|
|[ogmaudioparse][element-ogmaudioparse]|parse an OGM audio header and stream|[ogg][ogg]|[gst-plugins-base][base]|
|[ogmtextparse][element-ogmtextparse]|parse an OGM text header and stream|[ogg][ogg]|[gst-plugins-base][base]|
|[ogmvideoparse][element-ogmvideoparse]|parse an OGM video header and stream|[ogg][ogg]|[gst-plugins-base][base]|
|[openalsink][element-openalsink]|Output audio through OpenAL|[openal][openal]|[gst-plugins-bad][bad]|
|[openalsrc][element-openalsrc]|Input audio through OpenAL|[openal][openal]|[gst-plugins-bad][bad]|
|[opencvtextoverlay][element-opencvtextoverlay]|Write text on the top of video|[opencv][opencv]|[gst-plugins-bad][bad]|
|openexrdec|Decode EXR streams|openexr|[gst-plugins-bad][bad]|
|openjpegdec|Decode JPEG2000 streams|openjpeg|[gst-plugins-bad][bad]|
|openjpegenc|Encode JPEG2000 streams|openjpeg|[gst-plugins-bad][bad]|
|[optv][element-optv]|Optical art meets real-time video effect|[effectv][effectv]|[gst-plugins-good][good]|
|[opusdec][element-opusdec]|decode opus streams to audio|[opus][opus]|[gst-plugins-base][base]|
|[opusenc][element-opusenc]|Encodes audio in Opus format|[opus][opus]|[gst-plugins-base][base]|
|opusparse|parses opus audio streams|opusparse|[gst-plugins-bad][bad]|
|[oss4sink][element-oss4sink]|Output to a sound card via OSS version 4|[oss4][oss4]|[gst-plugins-good][good]|
|[oss4src][element-oss4src]|Capture from a sound card via OSS version 4|[oss4][oss4]|[gst-plugins-good][good]|
|[osssink][element-osssink]|Output to a sound card via OSS|[ossaudio][ossaudio]|[gst-plugins-good][good]|
|[osssrc][element-osssrc]|Capture from a sound card via OSS|[ossaudio][ossaudio]|[gst-plugins-good][good]|
|[osxaudiosink][element-osxaudiosink]|Output to a sound card in OS X|[osxaudio][osxaudio]|[gst-plugins-good][good]|
|[osxaudiosrc][element-osxaudiosrc]|Input from a sound card in OS X|[osxaudio][osxaudio]|[gst-plugins-good][good]|
|[osxvideosink][element-osxvideosink]|OSX native videosink|[osxvideo][osxvideo]|[gst-plugins-good][good]|
|[output-selector][element-output-selector]|1-to-N output stream selector|[coreelements][coreelements]|[gstreamer][core]|
|[parsebin][element-parsebin]|Parse and de-multiplex to elementary stream|[playback][playback]|[gst-plugins-base][base]|
|[pcapparse][element-pcapparse]|Parses a raw pcap stream|[pcapparse][pcapparse]|[gst-plugins-bad][bad]|
|perspective|Apply a 2D perspective transform|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[pinch][element-pinch]|Applies 'pinch' geometric transform to the image|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|pitch|Control the pitch of an audio stream|[soundtouch][soundtouch]|[gst-plugins-bad][bad]|
|[playbin][element-playbin]|Autoplug and play media from an uri|[playback][playback]|[gst-plugins-base][base]|
|[playbin3][element-playbin3]|Autoplug and play media from an uri|[playback][playback]|[gst-plugins-base][base]|
|[playsink][element-playsink]|Convenience sink for multiple streams|[playback][playback]|[gst-plugins-base][base]|
|[pngdec][element-pngdec]|Decode a png video frame to a raw image|[png][png]|[gst-plugins-good][good]|
|[pngenc][element-pngenc]|Encode a video frame to a .png image|[png][png]|[gst-plugins-good][good]|
|pngparse|Parses PNG files|videoparsersbad|[gst-plugins-bad][bad]|
|pnmdec|Decodes images in portable pixmap/graymap/bitmap/anymamp (PNM) format|pnm|[gst-plugins-bad][bad]|
|pnmenc|Encodes images into portable pixmap or graymap (PNM) format|pnm|[gst-plugins-bad][bad]|
|pnmsrc|Receive data over the network via PNM|[realmedia][realmedia]|[gst-plugins-ugly][ugly]|
|[progressreport][element-progressreport]|Periodically query and report on processing progress|[debug][debug]|[gst-plugins-good][good]|
|[pulsesink][element-pulsesink]|Plays audio to a PulseAudio server|[pulseaudio][pulseaudio]|[gst-plugins-good][good]|
|[pulsesrc][element-pulsesrc]|Captures audio from a PulseAudio server|[pulseaudio][pulseaudio]|[gst-plugins-good][good]|
|[pushfilesrc][element-pushfilesrc]|Implements pushfile:// URI-handler for push-based file access|[debug][debug]|[gst-plugins-good][good]|
|qmlglsink|A video sink the renders to a QQuickItem|qt|[gst-plugins-bad][bad]|
|qmlglsrc|A video src the grab window from a qml view|qt|[gst-plugins-bad][bad]|
|[qtdemux][element-qtdemux]|Demultiplex a QuickTime file into audio and video streams|[isomp4][isomp4]|[gst-plugins-good][good]|
|[qtmoovrecover][element-qtmoovrecover]|Recovers unfinished qtmux files|[isomp4][isomp4]|[gst-plugins-good][good]|
|[qtmux][element-qtmux]|Multiplex audio and video into a QuickTime file|[isomp4][isomp4]|[gst-plugins-good][good]|
|[quarktv][element-quarktv]|Motion dissolver|[effectv][effectv]|[gst-plugins-good][good]|
|[queue][element-queue]|Simple data queue|[coreelements][coreelements]|[gstreamer][core]|
|[queue2][element-queue2]|Simple data queue|[coreelements][coreelements]|[gstreamer][core]|
|[rademux][element-rademux]|Demultiplex a RealAudio file|[realmedia][realmedia]|[gst-plugins-ugly][ugly]|
|[radioactv][element-radioactv]|motion-enlightment effect|[effectv][effectv]|[gst-plugins-good][good]|
|[rawaudioparse][element-rawaudioparse]|Converts unformatted data streams into timestamped raw audio frames|[rawparse][rawparse]|[gst-plugins-bad][bad]|
|[rawvideoparse][element-rawvideoparse]|Converts unformatted data streams into timestamped raw video frames|[rawparse][rawparse]|[gst-plugins-bad][bad]|
|rdtdepay|Extracts RealMedia from RDT packets|[realmedia][realmedia]|[gst-plugins-ugly][ugly]|
|[rdtmanager][element-rdtmanager]|Accepts raw RTP and RTCP packets and sends them forward|[realmedia][realmedia]|[gst-plugins-ugly][ugly]|
|removesilence|Removes all the silence periods from the audio stream.|removesilence|[gst-plugins-bad][bad]|
|retinex|Multiscale retinex for colour image enhancement|[opencv][opencv]|[gst-plugins-bad][bad]|
|[revtv][element-revtv]|A video waveform monitor for each line of video processed|[effectv][effectv]|[gst-plugins-good][good]|
|[rfbsrc][element-rfbsrc]|Creates a rfb video stream|[rfbsrc][rfbsrc]|[gst-plugins-bad][bad]|
|[rganalysis][element-rganalysis]|Perform the ReplayGain analysis|[replaygain][replaygain]|[gst-plugins-good][good]|
|rgb2bayer|Converts video/x-raw to video/x-bayer|[bayer][bayer]|[gst-plugins-bad][bad]|
|[rglimiter][element-rglimiter]|Apply signal compression to raw audio data|[replaygain][replaygain]|[gst-plugins-good][good]|
|[rgvolume][element-rgvolume]|Apply ReplayGain volume adjustment|[replaygain][replaygain]|[gst-plugins-good][good]|
|[rippletv][element-rippletv]|RippleTV does ripple mark effect on the video input|[effectv][effectv]|[gst-plugins-good][good]|
|[rmdemux][element-rmdemux]|Demultiplex a RealMedia file into audio and video streams|[realmedia][realmedia]|[gst-plugins-ugly][ugly]|
|[rndbuffersize][element-rndbuffersize]|pull random sized buffers|[debug][debug]|[gst-plugins-good][good]|
|rotate|Rotates the picture by an arbitrary angle|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|rsndvdbin|DVD playback element|resindvd|[gst-plugins-bad][bad]|
|rsvgdec|Uses librsvg to decode SVG images|rsvg|[gst-plugins-bad][bad]|
|rsvgoverlay|Overlays SVG graphics over a video stream|rsvg|[gst-plugins-bad][bad]|
|[rtmpsink][element-rtmpsink]|Sends FLV content to a server via RTMP|[rtmp][rtmp]|[gst-plugins-bad][bad]|
|[rtmpsrc][element-rtmpsrc]|Read RTMP streams|[rtmp][rtmp]|[gst-plugins-bad][bad]|
|[rtpL16depay][element-rtpL16depay]|Extracts raw audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpL16pay][element-rtpL16pay]|Payload-encode Raw audio into RTP packets (RFC 3551)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpL24depay][element-rtpL24depay]|Extracts raw 24-bit audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpL24pay][element-rtpL24pay]|Payload-encode Raw 24-bit audio into RTP packets (RFC 3190)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpac3depay][element-rtpac3depay]|Extracts AC3 audio from RTP packets (RFC 4184)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpac3pay][element-rtpac3pay]|Payload AC3 audio as RTP packets (RFC 4184)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpamrdepay][element-rtpamrdepay]|Extracts AMR or AMR-WB audio from RTP packets (RFC 3267)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpamrpay][element-rtpamrpay]|Payload-encode AMR or AMR-WB audio into RTP packets (RFC 3267)|[rtp][rtp]|[gst-plugins-good][good]|
|rtpasfdepay|Extracts ASF streams from RTP|[asf][asf]|[gst-plugins-ugly][ugly]|
|rtpasfpay|Payload-encodes ASF into RTP packets (MS_RTSP)|asfmux|[gst-plugins-bad][bad]|
|[rtpbin][element-rtpbin]|Real-Time Transport Protocol bin|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtpbvdepay][element-rtpbvdepay]|Extracts BroadcomVoice audio from RTP packets (RFC 4298)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpbvpay][element-rtpbvpay]|Packetize BroadcomVoice audio streams into RTP packets (RFC 4298)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpceltdepay][element-rtpceltdepay]|Extracts CELT audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpceltpay][element-rtpceltpay]|Payload-encodes CELT audio into a RTP packet|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpdec][element-rtpdec]|Accepts raw RTP and RTCP packets and sends them forward|[rtsp][rtsp]|[gst-plugins-good][good]|
|[rtpdtmfdepay][element-rtpdtmfdepay]|Generates DTMF Sound from telephone-event RTP packets|[dtmf][dtmf]|[gst-plugins-good][good]|
|[rtpdtmfmux][element-rtpdtmfmux]|mixes RTP DTMF streams into other RTP streams|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtpdtmfsrc][element-rtpdtmfsrc]|Generates RTP DTMF packets|[dtmf][dtmf]|[gst-plugins-good][good]|
|[rtpdvdepay][element-rtpdvdepay]|Depayloads DV from RTP packets (RFC 3189)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpdvpay][element-rtpdvpay]|Payloads DV into RTP packets (RFC 3189)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg722depay][element-rtpg722depay]|Extracts G722 audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg722pay][element-rtpg722pay]|Payload-encode Raw audio into RTP packets (RFC 3551)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg723depay][element-rtpg723depay]|Extracts G.723 audio from RTP packets (RFC 3551)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg723pay][element-rtpg723pay]|Packetize G.723 audio into RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg726depay][element-rtpg726depay]|Extracts G.726 audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg726pay][element-rtpg726pay]|Payload-encodes G.726 audio into a RTP packet|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg729depay][element-rtpg729depay]|Extracts G.729 audio from RTP packets (RFC 3551)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpg729pay][element-rtpg729pay]|Packetize G.729 audio into RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpgsmdepay][element-rtpgsmdepay]|Extracts GSM audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpgsmpay][element-rtpgsmpay]|Payload-encodes GSM audio into a RTP packet|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpgstdepay][element-rtpgstdepay]|Extracts GStreamer buffers from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpgstpay][element-rtpgstpay]|Payload GStreamer buffers as RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph261depay][element-rtph261depay]|Extracts H261 video from RTP packets (RFC 4587)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph261pay][element-rtph261pay]|Payload-encodes H261 video in RTP packets (RFC 4587)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph263depay][element-rtph263depay]|Extracts H263 video from RTP packets (RFC 2190)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph263pay][element-rtph263pay]|Payload-encodes H263 video in RTP packets (RFC 2190)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph263pdepay][element-rtph263pdepay]|Extracts H263/+/++ video from RTP packets (RFC 4629)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph263ppay][element-rtph263ppay]|Payload-encodes H263/+/++ video in RTP packets (RFC 4629)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph264depay][element-rtph264depay]|Extracts H264 video from RTP packets (RFC 3984)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph264pay][element-rtph264pay]|Payload-encode H264 video into RTP packets (RFC 3984)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph265depay][element-rtph265depay]|Extracts H265 video from RTP packets (RFC 7798)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtph265pay][element-rtph265pay]|Payload-encode H265 video into RTP packets (RFC 7798)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpilbcdepay][element-rtpilbcdepay]|Extracts iLBC audio from RTP packets (RFC 3952)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpilbcpay][element-rtpilbcpay]|Packetize iLBC audio streams into RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpj2kdepay][element-rtpj2kdepay]|Extracts JPEG 2000 video from RTP packets (RFC 5371)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpj2kpay][element-rtpj2kpay]|Payload-encodes JPEG 2000 pictures into RTP packets (RFC 5371)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpjitterbuffer][element-rtpjitterbuffer]|A buffer that deals with network jitter and other transmission faults|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtpjpegdepay][element-rtpjpegdepay]|Extracts JPEG video from RTP packets (RFC 2435)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpjpegpay][element-rtpjpegpay]|Payload-encodes JPEG pictures into RTP packets (RFC 2435)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpklvdepay][element-rtpklvdepay]|Extracts KLV (SMPTE ST 336) metadata from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpklvpay][element-rtpklvpay]|Payloads KLV (SMPTE ST 336) metadata as RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp1sdepay][element-rtpmp1sdepay]|Extracts MPEG1 System Streams from RTP packets (RFC 3555)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp2tdepay][element-rtpmp2tdepay]|Extracts MPEG2 TS from RTP packets (RFC 2250)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp2tpay][element-rtpmp2tpay]|Payload-encodes MPEG2 TS into RTP packets (RFC 2250)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp4adepay][element-rtpmp4adepay]|Extracts MPEG4 audio from RTP packets (RFC 3016)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp4apay][element-rtpmp4apay]|Payload MPEG4 audio as RTP packets (RFC 3016)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp4gdepay][element-rtpmp4gdepay]|Extracts MPEG4 elementary streams from RTP packets (RFC 3640)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp4gpay][element-rtpmp4gpay]|Payload MPEG4 elementary streams as RTP packets (RFC 3640)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp4vdepay][element-rtpmp4vdepay]|Extracts MPEG4 video from RTP packets (RFC 3016)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmp4vpay][element-rtpmp4vpay]|Payload MPEG-4 video as RTP packets (RFC 3016)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmpadepay][element-rtpmpadepay]|Extracts MPEG audio from RTP packets (RFC 2038)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmpapay][element-rtpmpapay]|Payload MPEG audio as RTP packets (RFC 2038)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmparobustdepay][element-rtpmparobustdepay]|Extracts MPEG audio from RTP packets (RFC 5219)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmpvdepay][element-rtpmpvdepay]|Extracts MPEG video from RTP packets (RFC 2250)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmpvpay][element-rtpmpvpay]|Payload-encodes MPEG2 ES into RTP packets (RFC 2250)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpmux][element-rtpmux]|multiplex N rtp streams into one|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|rtponvifparse|Add absolute timestamps and flags of recorded data in a playback session|rtponvif|[gst-plugins-bad][bad]|
|rtponviftimestamp|Add absolute timestamps and flags of recorded data in a playback session|rtponvif|[gst-plugins-bad][bad]|
|[rtpopusdepay][element-rtpopusdepay]|Extracts Opus audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpopuspay][element-rtpopuspay]|Puts Opus audio in RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtppcmadepay][element-rtppcmadepay]|Extracts PCMA audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtppcmapay][element-rtppcmapay]|Payload-encodes PCMA audio into a RTP packet|[rtp][rtp]|[gst-plugins-good][good]|
|[rtppcmudepay][element-rtppcmudepay]|Extracts PCMU audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtppcmupay][element-rtppcmupay]|Payload-encodes PCMU audio into a RTP packet|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpptdemux][element-rtpptdemux]|Parses codec streams transmitted in the same RTP session|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtpqcelpdepay][element-rtpqcelpdepay]|Extracts QCELP (PureVoice) audio from RTP packets (RFC 2658)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpqdm2depay][element-rtpqdm2depay]|Extracts QDM2 audio from RTP packets (no RFC)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtprtxqueue][element-rtprtxqueue]|Keep RTP packets in a queue for retransmission|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtprtxreceive][element-rtprtxreceive]|Receive retransmitted RTP packets according to RFC4588|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtprtxsend][element-rtprtxsend]|Retransmit RTP packets when needed, according to RFC4588|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtpsbcdepay][element-rtpsbcdepay]|Extracts SBC audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpsbcpay][element-rtpsbcpay]|Payload SBC audio as RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpsession][element-rtpsession]|Implement an RTP session|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtpsirendepay][element-rtpsirendepay]|Extracts Siren audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpsirenpay][element-rtpsirenpay]|Packetize Siren audio streams into RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpspeexdepay][element-rtpspeexdepay]|Extracts Speex audio from RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpspeexpay][element-rtpspeexpay]|Payload-encodes Speex audio into a RTP packet|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpssrcdemux][element-rtpssrcdemux]|Splits RTP streams based on the SSRC|[rtpmanager][rtpmanager]|[gst-plugins-good][good]|
|[rtpstreamdepay][element-rtpstreamdepay]|Depayloads RTP/RTCP packets for streaming protocols according to RFC4571|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpstreampay][element-rtpstreampay]|Payloads RTP/RTCP packets for streaming protocols according to RFC4571|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpsv3vdepay][element-rtpsv3vdepay]|Extracts SVQ3 video from RTP packets (no RFC)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtptheoradepay][element-rtptheoradepay]|Extracts Theora video from RTP packets (draft-01 of RFC XXXX)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtptheorapay][element-rtptheorapay]|Payload-encode Theora video into RTP packets (draft-01 RFC XXXX)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvorbisdepay][element-rtpvorbisdepay]|Extracts Vorbis Audio from RTP packets (RFC 5215)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvorbispay][element-rtpvorbispay]|Payload-encode Vorbis audio into RTP packets (RFC 5215)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvp8depay][element-rtpvp8depay]|Extracts VP8 video from RTP packets)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvp8pay][element-rtpvp8pay]|Puts VP8 video in RTP packets|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvp9depay][element-rtpvp9depay]|Extracts VP9 video from RTP packets)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvp9pay][element-rtpvp9pay]|Puts VP9 video in RTP packets)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvrawdepay][element-rtpvrawdepay]|Extracts raw video from RTP packets (RFC 4175)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpvrawpay][element-rtpvrawpay]|Payload raw video as RTP packets (RFC 4175)|[rtp][rtp]|[gst-plugins-good][good]|
|[rtpxqtdepay][element-rtpxqtdepay]|Extracts Quicktime audio/video from RTP packets|[isomp4][isomp4]|[gst-plugins-good][good]|
|[rtspreal][element-rtspreal]|Extends RTSP so that it can handle RealMedia setup|[realmedia][realmedia]|[gst-plugins-ugly][ugly]|
|[rtspsrc][element-rtspsrc]|Receive data over the network via RTSP (RFC 2326)|[rtsp][rtsp]|[gst-plugins-good][good]|
|[rtspwms][element-rtspwms]|Extends RTSP so that it can handle WMS setup|[asf][asf]|[gst-plugins-ugly][ugly]|
|[sbcparse][element-sbcparse]|Parses an SBC bluetooth audio stream|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|[scaletempo][element-scaletempo]|Sync audio tempo with playback rate|[audiofx][audiofx]|[gst-plugins-good][good]|
|scenechange|Detects scene changes in video|videofiltersbad|[gst-plugins-bad][bad]|
|schrodec|Decode Dirac streams|schro|[gst-plugins-bad][bad]|
|schroenc|Encode raw video into Dirac stream|schro|[gst-plugins-bad][bad]|
|[sdpdemux][element-sdpdemux]|Receive data over the network via SDP|[sdp][sdp]|[gst-plugins-bad][bad]|
|sdpsrc|Stream RTP based on an SDP|[sdp][sdp]|[gst-plugins-bad][bad]|
|segmentation|Create a Foregound/Background mask applying a particular algorithm|[opencv][opencv]|[gst-plugins-bad][bad]|
|sfdec|Read audio streams using libsndfile|sndfile|[gst-plugins-bad][bad]|
|[shagadelictv][element-shagadelictv]|Oh behave, ShagedelicTV makes images shagadelic!|[effectv][effectv]|[gst-plugins-good][good]|
|[shapewipe][element-shapewipe]|Adds a shape wipe transition to a video stream|[shapewipe][shapewipe]|[gst-plugins-good][good]|
|[shmsink][element-shmsink]|Send data over shared memory to the matching source|[shm][shm]|[gst-plugins-bad][bad]|
|[shmsrc][element-shmsrc]|Receive data from the shared memory sink|[shm][shm]|[gst-plugins-bad][bad]|
|[shout2send][element-shout2send]|Sends data to an icecast server|[shout2send][shout2send]|[gst-plugins-good][good]|
|[siddec][element-siddec]|Use libsidplay to decode SID audio tunes|[siddec][siddec]|[gst-plugins-ugly][ugly]|
|simplevideomark|Marks a video signal with a pattern|videosignal|[gst-plugins-bad][bad]|
|simplevideomarkdetect|Detect patterns in a video signal|videosignal|[gst-plugins-bad][bad]|
|sirendec|Decode streams encoded with the Siren7 codec into 16bit PCM|gstsiren|[gst-plugins-bad][bad]|
|sirenenc|Encode 16bit PCM streams into the Siren7 codec|gstsiren|[gst-plugins-bad][bad]|
|skindetect|Performs non-parametric skin detection on input|[opencv][opencv]|[gst-plugins-bad][bad]|
|smooth|Apply a smooth filter to an image|smooth|[gst-plugins-bad][bad]|
|[smpte][element-smpte]|Apply the standard SMPTE transitions on video images|[smpte][smpte]|[gst-plugins-good][good]|
|[smptealpha][element-smptealpha]|Apply the standard SMPTE transitions as alpha on video images|[smpte][smpte]|[gst-plugins-good][good]|
|[socketsrc][element-socketsrc]|Receive data from a socket|[tcp][tcp]|[gst-plugins-base][base]|
|[solarize][element-solarize]|Solarize tunable inverse in the video signal.|[gaudieffects][gaudieffects]|[gst-plugins-bad][bad]|
|[souphttpclientsink][element-souphttpclientsink]|Sends streams to HTTP server via PUT|[soup][soup]|[gst-plugins-good][good]|
|[souphttpsrc][element-souphttpsrc]|Receive data as a client over the network via HTTP using SOUP|[soup][soup]|[gst-plugins-good][good]|
|[spacescope][element-spacescope]|Simple stereo visualizer|[audiovisualizers][audiovisualizers]|[gst-plugins-bad][bad]|
|spanplc|Adds packet loss concealment to audio|spandsp|[gst-plugins-bad][bad]|
|[spectrascope][element-spectrascope]|Simple frequency spectrum scope|[audiovisualizers][audiovisualizers]|[gst-plugins-bad][bad]|
|[spectrum][element-spectrum]|Run an FFT on the audio signal, output spectrum data|[spectrum][spectrum]|[gst-plugins-good][good]|
|[speed][element-speed]|Set speed/pitch on audio/raw streams (resampler)|[speed][speed]|[gst-plugins-bad][bad]|
|[speexdec][element-speexdec]|decode speex streams to audio|[speex][speex]|[gst-plugins-good][good]|
|[speexenc][element-speexenc]|Encodes audio in Speex format|[speex][speex]|[gst-plugins-good][good]|
|[sphere][element-sphere]|Applies 'sphere' geometric transform to the image|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[splitfilesrc][element-splitfilesrc]|Read a sequentially named set of files as if it was one large file|[multifile][multifile]|[gst-plugins-good][good]|
|[splitmuxsink][element-splitmuxsink]|Convenience bin that muxes incoming streams into multiple time/size limited files|[multifile][multifile]|[gst-plugins-good][good]|
|[splitmuxsrc][element-splitmuxsrc]|Source that reads a set of files created by splitmuxsink|[multifile][multifile]|[gst-plugins-good][good]|
|[square][element-square]|Distort center part of the image into a square|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|srtenc|Srt subtitle encoder|subenc|[gst-plugins-bad][bad]|
|srtpdec|A SRTP and SRTCP decoder|srtp|[gst-plugins-bad][bad]|
|srtpenc|A SRTP and SRTCP encoder|srtp|[gst-plugins-bad][bad]|
|[ssaparse][element-ssaparse]|Parses SSA subtitle streams|[subparse][subparse]|[gst-plugins-base][base]|
|stereo|Muck with the stereo signal to enhance its 'stereo-ness'|stereo|[gst-plugins-bad][bad]|
|[streaktv][element-streaktv]|StreakTV makes after images of moving objects|[effectv][effectv]|[gst-plugins-good][good]|
|[streamiddemux][element-streamiddemux]|1-to-N output stream by stream-id|[coreelements][coreelements]|[gstreamer][core]|
|[streamsynchronizer][element-streamsynchronizer]|Synchronizes a group of streams to have equal durations and starting points|[playback][playback]|[gst-plugins-base][base]|
|[stretch][element-stretch]|Stretch the image in a circle around the center point|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[subparse][element-subparse]|Parses subtitle (.sub) files into text streams|[subparse][subparse]|[gst-plugins-base][base]|
|[subtitleoverlay][element-subtitleoverlay]|Overlays a video stream with subtitles|[playback][playback]|[gst-plugins-base][base]|
|[synaescope][element-synaescope]|Creates video visualizations of audio input, using stereo and pitch information|[audiovisualizers][audiovisualizers]|[gst-plugins-bad][bad]|
|[taginject][element-taginject]|inject metadata tags|[debug][debug]|[gst-plugins-good][good]|
|[tcpclientsink][element-tcpclientsink]|Send data as a client over the network via TCP|[tcp][tcp]|[gst-plugins-base][base]|
|[tcpclientsrc][element-tcpclientsrc]|Receive data as a client over the network via TCP|[tcp][tcp]|[gst-plugins-base][base]|
|[tcpserversink][element-tcpserversink]|Send data as a server over the network via TCP|[tcp][tcp]|[gst-plugins-base][base]|
|[tcpserversrc][element-tcpserversrc]|Receive data as a server over the network via TCP|[tcp][tcp]|[gst-plugins-base][base]|
|[tee][element-tee]|1-to-N pipe fitting|[coreelements][coreelements]|[gstreamer][core]|
|teletextdec|Decode a raw VBI stream containing teletext information to RGBA and text|teletext|[gst-plugins-bad][bad]|
|[templatematch][element-templatematch]|Performs template matching on videos and images, providing detected positions via bus messages.|[opencv][opencv]|[gst-plugins-bad][bad]|
|[testsink][element-testsink]|perform a number of tests|[debug][debug]|[gst-plugins-good][good]|
|[textoverlay][element-textoverlay]|Adds text strings on top of a video buffer|[pango][pango]|[gst-plugins-base][base]|
|[textrender][element-textrender]|Renders a text string to an image bitmap|[pango][pango]|[gst-plugins-base][base]|
|[theoradec][element-theoradec]|decode raw theora streams to raw YUV video|[theora][theora]|[gst-plugins-base][base]|
|[theoraenc][element-theoraenc]|encode raw YUV video to a theora stream|[theora][theora]|[gst-plugins-base][base]|
|[theoraparse][element-theoraparse]|parse raw theora streams|[theora][theora]|[gst-plugins-base][base]|
|[timeoverlay][element-timeoverlay]|Overlays buffer time stamps on a video stream|[pango][pango]|[gst-plugins-base][base]|
|tonegeneratesrc|Creates telephony signals of given frequency, volume, cadence|spandsp|[gst-plugins-bad][bad]|
|tsdemux|Demuxes MPEG2 transport streams|mpegtsdemux|[gst-plugins-bad][bad]|
|tsparse|Parses MPEG2 transport streams|mpegtsdemux|[gst-plugins-bad][bad]|
|[tunnel][element-tunnel]|Light tunnel effect|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[twirl][element-twirl]|Twists the image from the center out|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|twolamemp2enc|High-quality free MP2 encoder|[twolame][twolame]|[gst-plugins-ugly][ugly]|
|[typefind][element-typefind]|Finds the media type of a stream|[coreelements][coreelements]|[gstreamer][core]|
|[udpsink][element-udpsink]|Send data over the network via UDP|[udp][udp]|[gst-plugins-good][good]|
|[udpsrc][element-udpsrc]|Receive data over the network via UDP|[udp][udp]|[gst-plugins-good][good]|
|[unalignedaudioparse][element-unalignedaudioparse]|Parse unaligned raw audio data|[rawparse][rawparse]|[gst-plugins-bad][bad]|
|[unalignedvideoparse][element-unalignedvideoparse]|Parse unaligned raw video data|[rawparse][rawparse]|[gst-plugins-bad][bad]|
|[uridecodebin][element-uridecodebin]|Autoplug and decode an URI to raw media|[playback][playback]|[gst-plugins-base][base]|
|[urisourcebin][element-urisourcebin]|Download and buffer a URI as needed|[playback][playback]|[gst-plugins-base][base]|
|uvch264mjpgdemux|Demux UVC H264 auxiliary streams from MJPG images|uvch264|[gst-plugins-bad][bad]|
|uvch264src|UVC H264 Encoding camera source|uvch264|[gst-plugins-bad][bad]|
|[v4l2radio][element-v4l2radio]|Controls a Video4Linux2 radio device|[video4linux2][video4linux2]|[gst-plugins-good][good]|
|[v4l2sink][element-v4l2sink]|Displays frames on a video4linux2 device|[video4linux2][video4linux2]|[gst-plugins-good][good]|
|[v4l2src][element-v4l2src]|Reads frames from a Video4Linux2 device|[video4linux2][video4linux2]|[gst-plugins-good][good]|
|[valve][element-valve]|Drops buffers and events or lets them through|[coreelements][coreelements]|[gstreamer][core]|
|vc1parse|Parses VC1 streams|videoparsersbad|[gst-plugins-bad][bad]|
|vcdsrc|Asynchronous read from VCD disk|vcdsrc|[gst-plugins-bad][bad]|
|vdpaumpegdec|Decode mpeg stream with vdpau|vdpau|[gst-plugins-bad][bad]|
|[vertigotv][element-vertigotv]|A loopback alpha blending effector with rotating and scaling|[effectv][effectv]|[gst-plugins-good][good]|
|videoanalyse|Analyse video signal|videosignal|[gst-plugins-bad][bad]|
|[videobalance][element-videobalance]|Adjusts brightness, contrast, hue, saturation on a video stream|[videofilter][videofilter]|[gst-plugins-good][good]|
|[videobox][element-videobox]|Resizes a video by adding borders or cropping|[videobox][videobox]|[gst-plugins-good][good]|
|[videoconvert][element-videoconvert]|Converts video from one colorspace to another|[videoconvert][videoconvert]|[gst-plugins-base][base]|
|[videocrop][element-videocrop]|Crops video into a user-defined region|[videocrop][videocrop]|[gst-plugins-good][good]|
|videodiff|Visualize differences between adjacent video frames|videofiltersbad|[gst-plugins-bad][bad]|
|[videoflip][element-videoflip]|Flips and rotates video|[videofilter][videofilter]|[gst-plugins-good][good]|
|videoframe-audiolevel|Synchronized audio/video RMS Level messenger for audio/raw|videoframe_audiolevel|[gst-plugins-bad][bad]|
|[videomedian][element-videomedian]|Apply a median filter to an image|[videofilter][videofilter]|[gst-plugins-good][good]|
|[videomixer][element-videomixer]|Mix multiple video streams|[videomixer][videomixer]|[gst-plugins-good][good]|
|[videoparse][element-videoparse]|Converts stream into video frames (deprecated: use rawvideoparse instead)|[rawparse][rawparse]|[gst-plugins-bad][bad]|
|[videorate][element-videorate]|Drops/duplicates/adjusts timestamps on video frames to make a perfect stream|[videorate][videorate]|[gst-plugins-base][base]|
|[videoscale][element-videoscale]|Resizes video|[videoscale][videoscale]|[gst-plugins-base][base]|
|videosegmentclip|Clips video buffers to the configured segment|segmentclip|[gst-plugins-bad][bad]|
|[videotestsrc][element-videotestsrc]|Creates a test video stream|[videotestsrc][videotestsrc]|[gst-plugins-base][base]|
|viewfinderbin|Viewfinder Bin used in camerabin2|[camerabin][camerabin]|[gst-plugins-bad][bad]|
|vmncdec|Decode VmWare video to raw (RGB) video|vmnc|[gst-plugins-bad][bad]|
|[voaacenc][element-voaacenc]|AAC audio encoder|[voaacenc][voaacenc]|[gst-plugins-bad][bad]|
|[voamrwbenc][element-voamrwbenc]|Adaptive Multi-Rate Wideband audio encoder|[voamrwbenc][voamrwbenc]|[gst-plugins-bad][bad]|
|[volume][element-volume]|Set volume on audio/raw streams|[volume][volume]|[gst-plugins-base][base]|
|[vorbisdec][element-vorbisdec]|decode raw vorbis streams to float audio|[vorbis][vorbis]|[gst-plugins-base][base]|
|[vorbisenc][element-vorbisenc]|Encodes audio in Vorbis format|[vorbis][vorbis]|[gst-plugins-base][base]|
|[vorbisparse][element-vorbisparse]|parse raw vorbis streams|[vorbis][vorbis]|[gst-plugins-base][base]|
|[vorbistag][element-vorbistag]|Retags vorbis streams|[vorbis][vorbis]|[gst-plugins-base][base]|
|[vp8dec][element-vp8dec]|Decode VP8 video streams|[vpx][vpx]|[gst-plugins-good][good]|
|[vp8enc][element-vp8enc]|Encode VP8 video streams|[vpx][vpx]|[gst-plugins-good][good]|
|[vp9dec][element-vp9dec]|Decode VP9 video streams|[vpx][vpx]|[gst-plugins-good][good]|
|[vp9enc][element-vp9enc]|Encode VP9 video streams|[vpx][vpx]|[gst-plugins-good][good]|
|[warptv][element-warptv]|WarpTV does realtime goo'ing of the video input|[effectv][effectv]|[gst-plugins-good][good]|
|watchdog|Watches for pauses in stream buffers|[debugutilsbad][debugutilsbad]|[gst-plugins-bad][bad]|
|[waterripple][element-waterripple]|Creates a water ripple effect on the image|[geometrictransform][geometrictransform]|[gst-plugins-bad][bad]|
|[waveformsink][element-waveformsink]|WaveForm audio sink|[waveform][waveform]|[gst-plugins-good][good]|
|[wavenc][element-wavenc]|Encode raw audio into WAV|[wavenc][wavenc]|[gst-plugins-good][good]|
|[wavescope][element-wavescope]|Simple waveform oscilloscope|[audiovisualizers][audiovisualizers]|[gst-plugins-bad][bad]|
|[wavpackdec][element-wavpackdec]|Decodes Wavpack audio data|[wavpack][wavpack]|[gst-plugins-good][good]|
|[wavpackenc][element-wavpackenc]|Encodes audio with the Wavpack lossless/lossy audio codec|[wavpack][wavpack]|[gst-plugins-good][good]|
|[wavpackparse][element-wavpackparse]|Wavpack parser|[audioparsers][audioparsers]|[gst-plugins-good][good]|
|[wavparse][element-wavparse]|Parse a .wav file into raw audio|[wavparse][wavparse]|[gst-plugins-good][good]|
|waylandsink|Output to wayland surface|waylandsink|[gst-plugins-bad][bad]|
|[webmmux][element-webmmux]|Muxes video and audio streams into a WebM stream|[matroska][matroska]|[gst-plugins-good][good]|
|webpdec|Decode images from WebP format|webp|[gst-plugins-bad][bad]|
|webpenc|Encode images in WEBP format|webp|[gst-plugins-bad][bad]|
|[webrtcdsp][element-webrtcdsp]|Pre-processes voice with WebRTC Audio Processing Library|[webrtcdsp][webrtcdsp]|[gst-plugins-bad][bad]|
|[webrtcechoprobe][element-webrtcechoprobe]|Gathers playback buffers for webrtcdsp|[webrtcdsp][webrtcdsp]|[gst-plugins-bad][bad]|
|webvttenc|WebVTT subtitle encoder|subenc|[gst-plugins-bad][bad]|
|wildmidi|Midi Synthesizer Element|wildmidi|[gst-plugins-bad][bad]|
|wrappercamerabinsrc|Wrapper camera src element for camerabin2|[camerabin][camerabin]|[gst-plugins-bad][bad]|
|[x264enc][element-x264enc]|H264 Encoder|[x264][x264]|[gst-plugins-ugly][ugly]|
|x265enc|H265 Encoder|x265|[gst-plugins-bad][bad]|
|[ximagesink][element-ximagesink]|A standard X based videosink|[ximagesink][ximagesink]|[gst-plugins-base][base]|
|[ximagesrc][element-ximagesrc]|Creates a screenshot video stream|[ximagesrc][ximagesrc]|[gst-plugins-good][good]|
|[xingmux][element-xingmux]|Adds a Xing header to the beginning of a VBR MP3 file|xingmux|[gst-plugins-ugly][ugly]|
|[xvimagesink][element-xvimagesink]|A Xv based videosink|[xvimagesink][xvimagesink]|[gst-plugins-base][base]|
|y4mdec|Demuxes/decodes YUV4MPEG streams|y4mdec|[gst-plugins-bad][bad]|
|[y4menc][element-y4menc]|Encodes a YUV frame into the yuv4mpeg format (mjpegtools)|[y4menc][y4menc]|[gst-plugins-good][good]|
|yadif|Deinterlace video using YADIF filter|yadif|[gst-plugins-bad][bad]|
|[zbar][element-zbar]|Detect bar codes in the video streams|[zbar][zbar]|[gst-plugins-bad][bad]|
|zebrastripe|Overlays zebra striping on overexposed areas of video|videofiltersbad|[gst-plugins-bad][bad]|


[core]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/
[base]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/
[good]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/
[ugly]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/
[bad]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/

[element-3gppmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-3gppmux.html
[element-a52dec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-a52dec.html
[element-aacparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-aacparse.html
[element-aasink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-aasink.html
[element-ac3parse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-ac3parse.html
[element-accurip]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-accurip.html
[element-adder]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-adder.html
[element-agingtv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-agingtv.html
[element-aiffmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-aiffmux.html
[element-aiffparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-aiffparse.html
[element-alawdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-alawdec.html
[element-alawenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-alawenc.html
[element-alpha]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-alpha.html
[element-alphacolor]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-alphacolor.html
[element-alsamidisrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-alsamidisrc.html
[element-alsasink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-alsasink.html
[element-alsasrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-alsasrc.html
[element-amrnbdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-amrnbdec.html
[element-amrnbenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-amrnbenc.html
[element-amrparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-amrparse.html
[element-amrwbdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-amrwbdec.html
[element-apedemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-apedemux.html
[element-apev2mux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-apev2mux.html
[element-appsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-appsink.html
[element-appsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-appsrc.html
[element-aspectratiocrop]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-aspectratiocrop.html
[element-assrender]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-assrender.html
[element-asteriskh263]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-asteriskh263.html
[element-audioamplify]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audioamplify.html
[element-audiochebband]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiochebband.html
[element-audiocheblimit]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiocheblimit.html
[element-audioconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-audioconvert.html
[element-audiodynamic]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiodynamic.html
[element-audioecho]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audioecho.html
[element-audiofirfilter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiofirfilter.html
[element-audioiirfilter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audioiirfilter.html
[element-audiointerleave]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-audiointerleave.html
[element-audioinvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audioinvert.html
[element-audiokaraoke]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiokaraoke.html
[element-audiomixer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-audiomixer.html
[element-audiopanorama]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiopanorama.html
[element-audioparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-audioparse.html
[element-audiorate]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-audiorate.html
[element-audioresample]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-audioresample.html
[element-audiotestsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-audiotestsrc.html
[element-audiowsincband]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiowsincband.html
[element-audiowsinclimit]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-audiowsinclimit.html
[element-auparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-auparse.html
[element-autoaudiosink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-autoaudiosink.html
[element-autoaudiosrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-autoaudiosrc.html
[element-autoconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-autoconvert.html
[element-autovideosink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-autovideosink.html
[element-autovideosrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-autovideosrc.html
[element-avidemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-avidemux.html
[element-avimux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-avimux.html
[element-avisubtitle]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-avisubtitle.html
[element-breakmydata]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-breakmydata.html
[element-bs2b]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-bs2b.html
[element-bulge]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-bulge.html
[element-burn]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-burn.html
[element-cacasink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-cacasink.html
[element-cairooverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-cairooverlay.html
[element-camerabin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-camerabin.html
[element-capsfilter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-capsfilter.html
[element-capssetter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-capssetter.html
[element-cdiocddasrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-cdiocddasrc.html
[element-cdparanoiasrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-cdparanoiasrc.html
[element-chromium]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-chromium.html
[element-circle]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-circle.html
[element-clockoverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-clockoverlay.html
[element-coloreffects]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-coloreffects.html
[element-compositor]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-compositor.html
[element-concat]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-concat.html
[element-cpureport]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-cpureport.html
[element-cutter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-cutter.html
[element-cvdilate]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-cvdilate.html
[element-cvequalizehist]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-cvequalizehist.html
[element-cverode]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-cverode.html
[element-cvlaplace]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-cvlaplace.html
[element-cvsmooth]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-cvsmooth.html
[element-cvsobel]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-cvsobel.html
[element-dataurisrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-dataurisrc.html
[element-dcaparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-dcaparse.html
[element-decodebin3]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-decodebin3.html
[element-decodebin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-decodebin.html
[element-deinterlace]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-deinterlace.html
[element-deinterleave]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-deinterleave.html
[element-dicetv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-dicetv.html
[element-diffuse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-diffuse.html
[element-dilate]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-dilate.html
[element-directsoundsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-directsoundsink.html
[element-dodge]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-dodge.html
[element-downloadbuffer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-downloadbuffer.html
[element-dtmfdetect]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-dtmfdetect.html
[element-dtmfsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-dtmfsrc.html
[element-dtsdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-dtsdec.html
[element-dv1394src]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-dv1394src.html
[element-dvbsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-dvbsrc.html
[element-dvdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-dvdec.html
[element-dvdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-dvdemux.html
[element-dvdspu]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-dvdspu.html
[element-dynudpsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-dynudpsink.html
[element-edgedetect]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-edgedetect.html
[element-edgetv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-edgetv.html
[element-encodebin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-encodebin.html
[element-equalizer-10bands]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-equalizer-10bands.html
[element-equalizer-3bands]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-equalizer-3bands.html
[element-equalizer-nbands]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-equalizer-nbands.html
[element-exclusion]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-exclusion.html
[element-faac]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-faac.html
[element-faad]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-faad.html
[element-faceblur]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-faceblur.html
[element-facedetect]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-facedetect.html
[element-fakesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-fakesink.html
[element-fakesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-fakesrc.html
[element-fdsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-fdsink.html
[element-fdsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-fdsrc.html
[element-festival]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-festival.html
[element-filesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-filesink.html
[element-filesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-filesrc.html
[element-fisheye]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-fisheye.html
[element-flacdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-flacdec.html
[element-flacenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-flacenc.html
[element-flacparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-flacparse.html
[element-flactag]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-flactag.html
[element-flvdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-flvdemux.html
[element-flvmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-flvmux.html
[element-flxdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-flxdec.html
[element-fpsdisplaysink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-fpsdisplaysink.html
[element-funnel]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-funnel.html
[element-gamma]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-gamma.html
[element-gaussianblur]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-gaussianblur.html
[element-gdkpixbufdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-gdkpixbufdec.html
[element-gdkpixbufoverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-gdkpixbufoverlay.html
[element-gdkpixbufsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-gdkpixbufsink.html
[element-giosink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-giosink.html
[element-giosrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-giosrc.html
[element-giostreamsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-giostreamsink.html
[element-giostreamsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-giostreamsrc.html
[element-glcolorbalance]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glcolorbalance.html
[element-glcolorconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glcolorconvert.html
[element-glcolorscale]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glcolorscale.html
[element-gldeinterlace]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-gldeinterlace.html
[element-gldifferencematte]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-gldifferencematte.html
[element-gldownload]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-gldownload.html
[element-gleffects]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-gleffects.html
[element-glfilterapp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glfilterapp.html
[element-glfilterbin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glfilterbin.html
[element-glfiltercube]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glfiltercube.html
[element-glfilterglass]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glfilterglass.html
[element-glimagesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glimagesink.html
[element-glimagesinkelement]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glimagesinkelement.html
[element-glmixerbin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glmixerbin.html
[element-glmosaic]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glmosaic.html
[element-gloverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-gloverlay.html
[element-glshader]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glshader.html
[element-glsinkbin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glsinkbin.html
[element-glsrcbin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glsrcbin.html
[element-glstereomix]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glstereomix.html
[element-glstereosplit]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glstereosplit.html
[element-gltestsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-gltestsrc.html
[element-glupload]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glupload.html
[element-glvideomixer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glvideomixer.html
[element-glvideomixerelement]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glvideomixerelement.html
[element-glviewconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-glviewconvert.html
[element-goom2k1]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-goom2k1.html
[element-goom]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-goom.html
[element-hdv1394src]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-hdv1394src.html
[element-icydemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-icydemux.html
[element-id3demux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-id3demux.html
[element-id3v2mux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-id3v2mux.html
[element-identity]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-identity.html
[element-imagefreeze]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-imagefreeze.html
[element-input-selector]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-input-selector.html
[element-interleave]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-interleave.html
[element-ismlmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-ismlmux.html
[element-jackaudiosink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-jackaudiosink.html
[element-jackaudiosrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-jackaudiosrc.html
[element-jpegdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-jpegdec.html
[element-jpegenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-jpegenc.html
[element-jpegparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-jpegparse.html
[element-kaleidoscope]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-kaleidoscope.html
[element-lamemp3enc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-lamemp3enc.html
[element-level]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-level.html
[element-liveadder]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-liveadder.html
[element-mad]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-mad.html
[element-marble]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-marble.html
[element-matroskademux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-matroskademux.html
[element-matroskamux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-matroskamux.html
[element-matroskaparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-matroskaparse.html
[element-mimdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-mimdec.html
[element-mimenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-mimenc.html
[element-mirror]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-mirror.html
[element-mj2mux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-mj2mux.html
[element-modplug]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-modplug.html
[element-monoscope]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-monoscope.html
[element-mp4mux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-mp4mux.html
[element-mpeg2enc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-mpeg2enc.html
[element-mpegaudioparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-mpegaudioparse.html
[element-mpegpsmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-mpegpsmux.html
[element-mpegtsmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-mpegtsmux.html
[element-mpg123audiodec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-mpg123audiodec.html
[element-mplex]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-mplex.html
[element-mulawdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-mulawdec.html
[element-mulawenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-mulawenc.html
[element-multifdsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-multifdsink.html
[element-multifilesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-multifilesink.html
[element-multifilesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-multifilesrc.html
[element-multipartdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-multipartdemux.html
[element-multipartmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-multipartmux.html
[element-multiqueue]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-multiqueue.html
[element-multisocketsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-multisocketsink.html
[element-multiudpsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-multiudpsink.html
[element-navigationtest]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-navigationtest.html
[element-navseek]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-navseek.html
[element-neonhttpsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-neonhttpsrc.html
[element-ofa]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-ofa.html
[element-oggaviparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-oggaviparse.html
[element-oggdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-oggdemux.html
[element-oggmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-oggmux.html
[element-oggparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-oggparse.html
[element-ogmaudioparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-ogmaudioparse.html
[element-ogmtextparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-ogmtextparse.html
[element-ogmvideoparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-ogmvideoparse.html
[element-openalsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-openalsink.html
[element-openalsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-openalsrc.html
[element-opencvtextoverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-opencvtextoverlay.html
[element-optv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-optv.html
[element-opusdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-opusdec.html
[element-opusenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-opusenc.html
[element-oss4sink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-oss4sink.html
[element-oss4src]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-oss4src.html
[element-osssink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-osssink.html
[element-osssrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-osssrc.html
[element-osxaudiosink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-osxaudiosink.html
[element-osxaudiosrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-osxaudiosrc.html
[element-osxvideosink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-osxvideosink.html
[element-output-selector]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-output-selector.html
[element-parsebin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-parsebin.html
[element-pcapparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-pcapparse.html
[element-pinch]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-pinch.html
[element-playbin3]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-playbin3.html
[element-playbin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-playbin.html
[element-playsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-playsink.html
[element-pngdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-pngdec.html
[element-pngenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-pngenc.html
[element-progressreport]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-progressreport.html
[element-pulsesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-pulsesink.html
[element-pulsesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-pulsesrc.html
[element-pushfilesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-pushfilesrc.html
[element-qtdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-qtdemux.html
[element-qtmoovrecover]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-qtmoovrecover.html
[element-qtmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-qtmux.html
[element-quarktv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-quarktv.html
[element-queue2]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-queue2.html
[element-queue]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-queue.html
[element-rademux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-rademux.html
[element-radioactv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-radioactv.html
[element-rawaudioparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-rawaudioparse.html
[element-rawvideoparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-rawvideoparse.html
[element-rdtmanager]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-rdtmanager.html
[element-revtv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-revtv.html
[element-rfbsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-rfbsrc.html
[element-rganalysis]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rganalysis.html
[element-rglimiter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rglimiter.html
[element-rgvolume]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rgvolume.html
[element-rippletv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rippletv.html
[element-rmdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-rmdemux.html
[element-rndbuffersize]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rndbuffersize.html
[element-rtmpsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-rtmpsink.html
[element-rtmpsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-rtmpsrc.html
[element-rtpL16depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpL16depay.html
[element-rtpL16pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpL16pay.html
[element-rtpL24depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpL24depay.html
[element-rtpL24pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpL24pay.html
[element-rtpac3depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpac3depay.html
[element-rtpac3pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpac3pay.html
[element-rtpamrdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpamrdepay.html
[element-rtpamrpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpamrpay.html
[element-rtpbin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpbin.html
[element-rtpbvdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpbvdepay.html
[element-rtpbvpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpbvpay.html
[element-rtpceltdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpceltdepay.html
[element-rtpceltpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpceltpay.html
[element-rtpdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpdec.html
[element-rtpdtmfdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpdtmfdepay.html
[element-rtpdtmfmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpdtmfmux.html
[element-rtpdtmfsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpdtmfsrc.html
[element-rtpdvdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpdvdepay.html
[element-rtpdvpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpdvpay.html
[element-rtpg722depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg722depay.html
[element-rtpg722pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg722pay.html
[element-rtpg723depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg723depay.html
[element-rtpg723pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg723pay.html
[element-rtpg726depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg726depay.html
[element-rtpg726pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg726pay.html
[element-rtpg729depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg729depay.html
[element-rtpg729pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpg729pay.html
[element-rtpgsmdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpgsmdepay.html
[element-rtpgsmpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpgsmpay.html
[element-rtpgstdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpgstdepay.html
[element-rtpgstpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpgstpay.html
[element-rtph261depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph261depay.html
[element-rtph261pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph261pay.html
[element-rtph263depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph263depay.html
[element-rtph263pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph263pay.html
[element-rtph263pdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph263pdepay.html
[element-rtph263ppay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph263ppay.html
[element-rtph264depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph264depay.html
[element-rtph264pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph264pay.html
[element-rtph265depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph265depay.html
[element-rtph265pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtph265pay.html
[element-rtpilbcdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpilbcdepay.html
[element-rtpilbcpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpilbcpay.html
[element-rtpj2kdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpj2kdepay.html
[element-rtpj2kpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpj2kpay.html
[element-rtpjitterbuffer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpjitterbuffer.html
[element-rtpjpegdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpjpegdepay.html
[element-rtpjpegpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpjpegpay.html
[element-rtpklvdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpklvdepay.html
[element-rtpklvpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpklvpay.html
[element-rtpmp1sdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp1sdepay.html
[element-rtpmp2tdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp2tdepay.html
[element-rtpmp2tpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp2tpay.html
[element-rtpmp4adepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp4adepay.html
[element-rtpmp4apay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp4apay.html
[element-rtpmp4gdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp4gdepay.html
[element-rtpmp4gpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp4gpay.html
[element-rtpmp4vdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp4vdepay.html
[element-rtpmp4vpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmp4vpay.html
[element-rtpmpadepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmpadepay.html
[element-rtpmpapay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmpapay.html
[element-rtpmparobustdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmparobustdepay.html
[element-rtpmpvdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmpvdepay.html
[element-rtpmpvpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmpvpay.html
[element-rtpmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpmux.html
[element-rtpopusdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpopusdepay.html
[element-rtpopuspay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpopuspay.html
[element-rtppcmadepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtppcmadepay.html
[element-rtppcmapay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtppcmapay.html
[element-rtppcmudepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtppcmudepay.html
[element-rtppcmupay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtppcmupay.html
[element-rtpptdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpptdemux.html
[element-rtpqcelpdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpqcelpdepay.html
[element-rtpqdm2depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpqdm2depay.html
[element-rtprtxqueue]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtprtxqueue.html
[element-rtprtxreceive]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtprtxreceive.html
[element-rtprtxsend]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtprtxsend.html
[element-rtpsbcdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpsbcdepay.html
[element-rtpsbcpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpsbcpay.html
[element-rtpsession]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpsession.html
[element-rtpsirendepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpsirendepay.html
[element-rtpsirenpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpsirenpay.html
[element-rtpspeexdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpspeexdepay.html
[element-rtpspeexpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpspeexpay.html
[element-rtpssrcdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpssrcdemux.html
[element-rtpstreamdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpstreamdepay.html
[element-rtpstreampay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpstreampay.html
[element-rtpsv3vdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpsv3vdepay.html
[element-rtptheoradepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtptheoradepay.html
[element-rtptheorapay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtptheorapay.html
[element-rtpvorbisdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvorbisdepay.html
[element-rtpvorbispay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvorbispay.html
[element-rtpvp8depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvp8depay.html
[element-rtpvp8pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvp8pay.html
[element-rtpvp9depay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvp9depay.html
[element-rtpvp9pay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvp9pay.html
[element-rtpvrawdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvrawdepay.html
[element-rtpvrawpay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpvrawpay.html
[element-rtpxqtdepay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtpxqtdepay.html
[element-rtspreal]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-rtspreal.html
[element-rtspsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-rtspsrc.html
[element-rtspwms]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-rtspwms.html
[element-sbcparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-sbcparse.html
[element-scaletempo]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-scaletempo.html
[element-sdpdemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-sdpdemux.html
[element-shagadelictv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-shagadelictv.html
[element-shapewipe]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-shapewipe.html
[element-shmsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-shmsink.html
[element-shmsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-shmsrc.html
[element-shout2send]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-shout2send.html
[element-siddec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-siddec.html
[element-smpte]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-smpte.html
[element-smptealpha]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-smptealpha.html
[element-socketsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-socketsrc.html
[element-solarize]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-solarize.html
[element-souphttpclientsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-souphttpclientsink.html
[element-souphttpsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-souphttpsrc.html
[element-spacescope]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-spacescope.html
[element-spectrascope]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-spectrascope.html
[element-spectrum]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-spectrum.html
[element-speed]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-speed.html
[element-speexdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-speexdec.html
[element-speexenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-speexenc.html
[element-sphere]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-sphere.html
[element-splitfilesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-splitfilesrc.html
[element-splitmuxsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-splitmuxsink.html
[element-splitmuxsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-splitmuxsrc.html
[element-square]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-square.html
[element-ssaparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-ssaparse.html
[element-streaktv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-streaktv.html
[element-streamiddemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-streamiddemux.html
[element-streamsynchronizer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-streamsynchronizer.html
[element-stretch]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-stretch.html
[element-subparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-subparse.html
[element-subtitleoverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-subtitleoverlay.html
[element-synaescope]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-synaescope.html
[element-taginject]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-taginject.html
[element-tcpclientsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-tcpclientsink.html
[element-tcpclientsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-tcpclientsrc.html
[element-tcpserversink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-tcpserversink.html
[element-tcpserversrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-tcpserversrc.html
[element-tee]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-tee.html
[element-templatematch]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-templatematch.html
[element-testsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-testsink.html
[element-textoverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-textoverlay.html
[element-textrender]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-textrender.html
[element-theoradec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-theoradec.html
[element-theoraenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-theoraenc.html
[element-theoraparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-theoraparse.html
[element-timeoverlay]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-timeoverlay.html
[element-tunnel]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-tunnel.html
[element-twirl]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-twirl.html
[element-typefind]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-typefind.html
[element-udpsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-udpsink.html
[element-udpsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-udpsrc.html
[element-unalignedaudioparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-unalignedaudioparse.html
[element-unalignedvideoparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-unalignedvideoparse.html
[element-uridecodebin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-uridecodebin.html
[element-urisourcebin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-urisourcebin.html
[element-v4l2radio]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-v4l2radio.html
[element-v4l2sink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-v4l2sink.html
[element-v4l2src]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-v4l2src.html
[element-valve]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-valve.html
[element-vertigotv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-vertigotv.html
[element-videobalance]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-videobalance.html
[element-videobox]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-videobox.html
[element-videoconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-videoconvert.html
[element-videocrop]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-videocrop.html
[element-videoflip]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-videoflip.html
[element-videomedian]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-videomedian.html
[element-videomixer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-videomixer.html
[element-videoparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-videoparse.html
[element-videorate]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-videorate.html
[element-videoscale]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-videoscale.html
[element-videotestsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-videotestsrc.html
[element-voaacenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-voaacenc.html
[element-voamrwbenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-voamrwbenc.html
[element-volume]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-volume.html
[element-vorbisdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-vorbisdec.html
[element-vorbisenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-vorbisenc.html
[element-vorbisparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-vorbisparse.html
[element-vorbistag]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-vorbistag.html
[element-vp8dec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-vp8dec.html
[element-vp8enc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-vp8enc.html
[element-vp9dec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-vp9dec.html
[element-vp9enc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-vp9enc.html
[element-warptv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-warptv.html
[element-waterripple]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-waterripple.html
[element-waveformsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-waveformsink.html
[element-wavenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-wavenc.html
[element-wavescope]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-wavescope.html
[element-wavpackdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-wavpackdec.html
[element-wavpackenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-wavpackenc.html
[element-wavpackparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-wavpackparse.html
[element-wavparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-wavparse.html
[element-webmmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-webmmux.html
[element-webrtcdsp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-webrtcdsp.html
[element-webrtcechoprobe]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-webrtcechoprobe.html
[element-x264enc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-x264enc.html
[element-ximagesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-ximagesink.html
[element-ximagesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-ximagesrc.html
[element-xingmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-xingmux.html
[element-xvimagesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-xvimagesink.html
[element-y4menc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-y4menc.html
[element-zbar]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-zbar.html

[1394]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-1394.html
[a52dec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-a52dec.html
[aasink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-aasink.html
[adder]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-adder.html
[aiff]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-aiff.html
[alaw]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-alaw.html
[alpha]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-alpha.html
[alphacolor]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-alphacolor.html
[alsa]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-alsa.html
[amrnb]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-amrnb.html
[amrwbdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-amrwbdec.html
[apetag]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-apetag.html
[app]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-app.html
[asf]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-asf.html
[assrender]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-assrender.html
[audioconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-audioconvert.html
[audiofx]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-audiofx.html
[audiomixer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-audiomixer.html
[audioparsers]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-audioparsers.html
[audiorate]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-audiorate.html
[audioresample]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-audioresample.html
[audiotestsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-audiotestsrc.html
[audiovisualizers]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-audiovisualizers.html
[auparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-auparse.html
[autoconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-autoconvert.html
[autodetect]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-autodetect.html
[avi]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-avi.html
[bayer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-bayer.html
[bs2b]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-bs2b.html
[bz2]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-bz2.html
[cacasink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-cacasink.html
[cairo]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-cairo.html
[camerabin]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-camerabin.html
[cdio]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-cdio.html
[cdparanoia]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-cdparanoia.html
[coloreffects]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-coloreffects.html
[coreelements]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/gstreamer-plugins-plugin-coreelements.html
[curl]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-curl.html
[cutter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-cutter.html
[dataurisrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-dataurisrc.html
[debug]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-debug.html
[debugutilsbad]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-debugutilsbad.html
[deinterlace]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-deinterlace.html
[directsound]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-directsound.html
[dtmf]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-dtmf.html
[dtsdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-dtsdec.html
[dv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-dv.html
[dvb]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-dvb.html
[dvdlpcmdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-dvdlpcmdec.html
[dvdread]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-dvdread.html
[dvdspu]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-dvdspu.html
[dvdsub]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-dvdsub.html
[effectv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-effectv.html
[encoding]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-encoding.html
[equalizer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-equalizer.html
[faac]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-faac.html
[faad]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-faad.html
[festival]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-festival.html
[flac]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-flac.html
[flv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-flv.html
[flxdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-flxdec.html
[gaudieffects]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-gaudieffects.html
[gdkpixbuf]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-gdkpixbuf.html
[geometrictransform]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-geometrictransform.html
[gio]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-gio.html
[goom2k1]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-goom2k1.html
[goom]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-goom.html
[gsm]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-gsm.html
[icydemux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-icydemux.html
[id3demux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-id3demux.html
[imagefreeze]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-imagefreeze.html
[interleave]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-interleave.html
[isomp4]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-isomp4.html
[ivorbisdec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-ivorbisdec.html
[jack]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-jack.html
[jpeg]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-jpeg.html
[jpegformat]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-jpegformat.html
[lame]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-lame.html
[level]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-level.html
[libvisual]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-libvisual.html
[mad]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-mad.html
[matroska]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-matroska.html
[mimic]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-mimic.html
[mms]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-mms.html
[modplug]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-modplug.html
[monoscope]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-monoscope.html
[mpeg2dec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-mpeg2dec.html
[mpeg2enc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-mpeg2enc.html
[mpegpsmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-mpegpsmux.html
[mpegtsmux]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-mpegtsmux.html
[mpg123]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-mpg123.html
[mplex]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-mplex.html
[mulaw]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-mulaw.html
[multifile]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-multifile.html
[multipart]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-multipart.html
[navigationtest]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-navigationtest.html
[neon]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-neon.html
[ofa]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-ofa.html
[ogg]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-ogg.html
[openal]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-openal.html
[opencv]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-opencv.html
[opengl]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-opengl.html
[opus]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-opus.html
[oss4]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-oss4.html
[ossaudio]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-ossaudio.html
[osxaudio]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-osxaudio.html
[osxvideo]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-osxvideo.html
[pango]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-pango.html
[pcapparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-pcapparse.html
[playback]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-playback.html
[png]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-png.html
[pulseaudio]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-pulseaudio.html
[rawparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-rawparse.html
[realmedia]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-realmedia.html
[replaygain]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-replaygain.html
[rfbsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-rfbsrc.html
[rtmp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-rtmp.html
[rtp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-rtp.html
[rtpmanager]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-rtpmanager.html
[rtsp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-rtsp.html
[sdp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-sdp.html
[shapewipe]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-shapewipe.html
[shm]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-shm.html
[shout2send]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-shout2send.html
[siddec]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-siddec.html
[smpte]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-smpte.html
[soundtouch]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-soundtouch.html
[soup]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-soup.html
[spectrum]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-spectrum.html
[speed]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-speed.html
[speex]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-speex.html
[subparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-subparse.html
[taglib]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-taglib.html
[tcp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-tcp.html
[theora]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-theora.html
[twolame]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-twolame.html
[udp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-udp.html
[video4linux2]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-video4linux2.html
[videobox]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-videobox.html
[videoconvert]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-videoconvert.html
[videocrop]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-videocrop.html
[videofilter]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-videofilter.html
[videomixer]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-videomixer.html
[videorate]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-videorate.html
[videoscale]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-videoscale.html
[videotestsrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-videotestsrc.html
[voaacenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-voaacenc.html
[voamrwbenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-voamrwbenc.html
[volume]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-volume.html
[vorbis]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-vorbis.html
[vpx]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-vpx.html
[waveform]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-waveform.html
[wavenc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-wavenc.html
[wavpack]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-wavpack.html
[wavparse]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-wavparse.html
[webrtcdsp]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-webrtcdsp.html
[x264]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/gst-plugins-ugly-plugins-plugin-x264.html
[ximagesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-ximagesink.html
[ximagesrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-ximagesrc.html
[xvimagesink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-plugin-xvimagesink.html
[y4menc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-plugin-y4menc.html
[zbar]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/gst-plugins-bad-plugins-plugin-zbar.html

