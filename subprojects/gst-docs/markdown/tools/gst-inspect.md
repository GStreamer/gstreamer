# gst-inspect-1.0

> ![information] This is the Linux man page for
> the `gst-inspect-1.0` tool. As such, it is very Linux-centric
> regarding path specification and plugin names. Please be patient while
> it is rewritten to be more generic.

## Name

gst-inspect-1.0 - print info about a GStreamer plugin or element

## Synopsis

**gst-inspect-1.0 \[OPTION...\] \[PLUGIN|ELEMENT\]**

## Description

*gst-inspect-1.0* is a tool that prints out information on
available *GStreamer* plugins, information about a particular plugin, or
information about a particular element. When executed with no PLUGIN or
ELEMENT argument, *gst-inspect-1.0* will print a list of all plugins and
elements together with a sumary. When executed with a PLUGIN or ELEMENT
argument, *gst-inspect-1.0* will print information about that plug-in or
element.

## Options

*gst-inspect-1.0* accepts the following arguments and options:

**PLUGIN**

Name of a plugin. This is a file name
like `%GSTREAMER_ROOT_X86%\lib\gstreamer-1.0\libgstaudiotestsrc.dll`
for example.

**ELEMENT**

Name of an element. This is the name of an element, like
`audiotestsrc` for example

**--help**

Print help synopsis and available FLAGS

**--gst-info-mask=FLAGS**

*GStreamer* info flags to set (list with --help)

 **-a, --print-all**

Print all plugins and elements

 **--print-plugin-auto-install-info**

Print a machine-parsable list of features the specified plugin provides.
Useful in connection with external automatic plugin installation
mechanisms.

 **--gst-debug-mask=FLAGS**

*GStreamer* debugging flags to set (list with --help)

 **--gst-mask=FLAGS**

*GStreamer* info and debugging flags to set (list with --help)

 **--gst-plugin-path=PATH**

Add directories separated with ':' to the plugin search path

## Example

    gst-inspect-1.0 audiotestsrc

should produce:

    Factory Details:
      Rank                     none (0)
      Long-name                Audio test source
      Klass                    Source/Audio
      Description              Creates audio test signals of given frequency and volume
      Author                   Stefan Kost <ensonic@users.sf.net>

    Plugin Details:
      Name                     audiotestsrc
      Description              Creates audio test signals of given frequency and volume
      Filename                 /usr/lib/gstreamer-1.0/libgstaudiotestsrc.so
      Version                  1.8.1
      License                  LGPL
      Source module            gst-plugins-base
      Source release date      2016-04-20
      Binary package           GStreamer Base Plugins (Arch Linux)
      Origin URL               http://www.archlinux.org/

    GObject
     +----GInitiallyUnowned
           +----GstObject
                 +----GstElement
                       +----GstBaseSrc
                             +----GstAudioTestSrc

    Pad Templates:
      SRC template: 'src'
        Availability: Always
        Capabilities:
          audio/x-raw
                     format: { S16LE, S16BE, U16LE, U16BE, S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE, S8, U8 }
                     layout: interleaved
                       rate: [ 1, 2147483647 ]
                   channels: [ 1, 2147483647 ]

    Element Flags:
      no flags set

    Element Implementation:
      Has change_state() function: gst_base_src_change_state

    Element has no clocking capabilities.
    Element has no URI handling capabilities.

    Pads:
      SRC: 'src'
        Pad Template: 'src'

    Element Properties:
      name                : The name of the object
                            flags: readable, writable
                            String. Default: "audiotestsrc0"
      parent              : The parent of the object
                            flags: readable, writable
                            Object of type "GstObject"
      blocksize           : Size in bytes to read per buffer (-1 = default)
                            flags: readable, writable
                            Unsigned Integer. Range: 0 - 4294967295 Default: 4294967295
      num-buffers         : Number of buffers to output before sending EOS (-1 = unlimited)
                            flags: readable, writable
                            Integer. Range: -1 - 2147483647 Default: -1
      typefind            : Run typefind before negotiating
                            flags: readable, writable
                            Boolean. Default: false
      do-timestamp        : Apply current stream time to buffers
                            flags: readable, writable
                            Boolean. Default: false
      samplesperbuffer    : Number of samples in each outgoing buffer
                            flags: readable, writable
                            Integer. Range: 1 - 2147483647 Default: 1024
      wave                : Oscillator waveform
                            flags: readable, writable, controllable
                            Enum "GstAudioTestSrcWave" Default: 0, "sine"
                               (0): sine             - Sine
                               (1): square           - Square
                               (2): saw              - Saw
                               (3): triangle         - Triangle
                               (4): silence          - Silence
                               (5): white-noise      - White uniform noise
                               (6): pink-noise       - Pink noise
                               (7): sine-table       - Sine table
                               (8): ticks            - Periodic Ticks
                               (9): gaussian-noise   - White Gaussian noise
                               (10): red-noise        - Red (brownian) noise
                               (11): blue-noise       - Blue noise
                               (12): violet-noise     - Violet noise
      freq                : Frequency of test signal. The sample rate needs to be at least 4 times higher.
                            flags: readable, writable, controllable
                            Double. Range:               0 -    5.368709e+08 Default:             440
      volume              : Volume of test signal
                            flags: readable, writable, controllable
                            Double. Range:               0 -               1 Default:             0.8
      is-live             : Whether to act as a live source
                            flags: readable, writable
                            Boolean. Default: false
      timestamp-offset    : An offset added to timestamps set on buffers (in ns)
                            flags: readable, writable
                            Integer64. Range: -9223372036854775808 - 9223372036854775807 Default: 0
      can-activate-push   : Can activate in push mode
                            flags: readable, writable
                            Boolean. Default: true
      can-activate-pull   : Can activate in pull mode
                            flags: readable, writable
                            Boolean. Default: false

  [information]: images/icons/emoticons/information.svg
