---
short-description: Tool to test GStreamer components
...

# gst-validate-transcoding

`gst-validate-transcoding` is tool to create media files transcoding
pipelines running inside the GstValidate monitoring infrastructure.

You can for example transcode any media file to Vorbis audio + VP8 video
in a WebM container by doing:

    gst-validate-transcoding-GST_API_VERSION file:///./file.ogg file:///.../transcoded.webm -o 'video/webm:video/x-vp8:audio/x-vorbis'

`gst-validate-transcoding` will list every issue encountered during the
execution of the transcoding operation in a human readable report like
the one below:

    issue : buffer is out of the segment range Detected on theoradec0.srcpad
    at 0:00:00.096556426 Details : buffer is out of segment and shouldn't be
    pushed. Timestamp: 0:00:25.000 - duration: 0:00:00.040 Range:
    0:00:00.000 - 0:00:04.520 Description : buffer being pushed is out of
    the current segment's start-stop range. Meaning it is going to be
    discarded downstream without any use

The return code of the process will be 18 in case a `CRITICAL` issue has
been found.

## The encoding profile serialization format

This is the serialization format of a [GstEncodingProfile](GstEncodingProfile).

Internally the transcoding application uses [GstEncodeBin](encodebin).
`gst-validate-transcoding-GST_API_VERSION` uses its own serialization
format to describe the [`GstEncodeBin.profile`](encodebin:profile) property of the
encodebin.

The simplest serialized profile looks like:

    muxer_source_caps:videoencoder_source_caps:audioencoder_source_caps

For example to encode a stream into a WebM container, with an OGG audio
stream and a VP8 video stream, the serialized [GstEncodingProfile](GstEncodingProfile)
will look like:

    video/webm:video/x-vp8:audio/x-vorbis

You can also set the preset name of the encoding profile using the
caps+preset\_name syntax as in:

    video/webm:video/x-vp8+youtube-preset:audio/x-vorbis

Moreover, you can set the [presence](gst_encoding_profile_set_presence) property
of an encoding profile using the `|presence` syntax as in:

    video/webm:video/x-vp8|1:audio/x-vorbis

This field allows you to specify how many times maximum a
[GstEncodingProfile](GstEncodingProfile) can be used inside an encodebin.

You can also use the `restriction_caps->encoded_format_caps` syntax to
specify the [restriction caps](GstEncodingProfile:restriction-caps)
to be set on a [GstEncodingProfile](GstEncodingProfile). It
corresponds to the restriction [GstCaps](GstCaps) to apply before the encoder
that will be used in the profile. The fields present in restriction caps
are properties of the raw stream (that is, before encoding), such as
height and width for video and depth and sampling rate for audio. This
property does not make sense for muxers.

To force a video stream to be encoded with a Full HD resolution (using
WebM as the container format, VP8 as the video codec and Vorbis as the
audio codec), you should use:

    video/webm:video/x-raw,width=1920,height=1080->video/x-vp8:audio/x-vorbis

### Some serialized encoding formats examples:

MP3 audio and H264 in MP4:

<div class="informalexample">

    video/quicktime,variant=iso:video/x-h264:audio/mpeg,mpegversion=1,layer=3

</div>

Vorbis and theora in OGG:

<div class="informalexample">

    application/ogg:video/x-theora:audio/x-vorbis

</div>

AC3 and H264 in MPEG-TS:

<div class="informalexample">

    video/mpegts:video/x-h264:audio/x-ac3

</div>

# Invocation

`gst-validate-transcoding` takes and input URI and an output URI, plus a
few options to control how transcoding should be tested.

## Options

* `--set-scenario`: Let you set a scenario, it can be a full path to a scenario file or
  the name of the scenario (name of the file without the `.scenario`
  extension).
* `-l`, `--list-scenarios`: List the avalaible scenarios that can be run.
* `--scenarios-defs-output-file`: The output file to store scenarios details. Implies
  `--list-scenario`.
* `-t`, `--inspect-action-type`: Inspect the avalaible action types with which to write scenarios if
  no parameter passed, it will list all avalaible action types
  otherwize will print the full description of the wanted types.
* `--set-configs`: Let you set a config scenario. The scenario needs to be set as
  `config`. You can specify a list of scenarios separated by `:`. It
  will override the GST\_VALIDATE\_SCENARIO environment variable.
* `-e`, `--eos-on-shutdown`: If an EOS event should be sent to the pipeline if an interrupt is
  received, instead of forcing the pipeline to stop. Sending an EOS
  will allow the transcoding to finish the files properly before
  exiting.
* `-r`, `--force-reencoding`: Whether to try to force reencoding, meaning trying to only remux if
  possible, defaults to `TRUE`.
