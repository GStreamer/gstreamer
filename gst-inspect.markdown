# gst-inspect

This page last changed on May 30, 2012 by xartigas.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><p><span>This is the Linux man page for the </span><code>gst-inspect</code><span> tool. As such, it is very Linux-centric regarding path specification and plugin names. Please be patient while it is rewritten to be more generic.</span></p></td>
</tr>
</tbody>
</table>

## Name

gst-inspect - print info about a GStreamer plugin or element

## Synopsis

**gst-inspect \[OPTION...\] \[PLUGIN|ELEMENT\]**

## Description

*gst-inspect* is a tool that prints out information on
available *GStreamer* plugins, information about a particular plugin,
or information about a particular element. When executed with no PLUGIN
or ELEMENT argument, *gst-inspect* will print a list of all plugins and
elements together with a sumary. When executed with a PLUGIN or ELEMENT
argument, *gst-inspect* will print information about that plug-in or
element.

## Options

*gst-inspect* accepts the following arguments and options:

**PLUGIN**

Name of a plugin. This is a file name
like `%GSTREAMER_SDK_ROOT_X86%\lib\gstreamer-0.10\libgstaudiotestsrc.dll`
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

 **--gst-plugin-spew**

*GStreamer* info flags to set Enable printout of errors while
loading *GStreamer* plugins

 **--gst-plugin-path=PATH**

Add directories separated with ':' to the plugin search path

## Example

```
gst-inspect-0.10 audiotestsrc
```

should produce:

```
Factory Details:
  Long name:    Audio test source
  Class:        Source/Audio
  Description:  Creates audio test signals of given frequency and volume
  Author(s):    Stefan Kost <ensonic@users.sf.net>
  Rank:         none (0)
Plugin Details:
  Name:                 audiotestsrc
  Description:          Creates audio test signals of given frequency and volume
  Filename:             I:\gstreamer-sdk\2012.5\x86\lib\gstreamer-0.10\libgstaudiotestsrc.dll
  Version:              0.10.36
  License:              LGPL
  Source module:        gst-plugins-base
  Source release date:  2012-02-20
  Binary package:       GStreamer Base Plug-ins source release
  Origin URL:           Unknown package origin
GObject
 +----GstObject
       +----GstElement
             +----GstBaseSrc
                   +----GstAudioTestSrc
Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      audio/x-raw-int
             endianness: 1234
                 signed: true
                  width: 16
                  depth: 16
                   rate: [ 1, 2147483647 ]
               channels: [ 1, 2 ]
      audio/x-raw-int
             endianness: 1234
                 signed: true
                  width: 32
                  depth: 32
                   rate: [ 1, 2147483647 ]
               channels: [ 1, 2 ]
      audio/x-raw-float
             endianness: 1234
                  width: { 32, 64 }
                   rate: [ 1, 2147483647 ]
               channels: [ 1, 2 ]

Element Flags:
  no flags set
Element Implementation:
  Has change_state() function: gst_base_src_change_state
  Has custom save_thyself() function: gst_element_save_thyself
  Has custom restore_thyself() function: gst_element_restore_thyself
Element has no clocking capabilities.
Element has no indexing capabilities.
Element has no URI handling capabilities.
Pads:
  SRC: 'src'
    Implementation:
      Has getrangefunc(): gst_base_src_pad_get_range
      Has custom eventfunc(): gst_base_src_event_handler
      Has custom queryfunc(): gst_base_src_query
      Has custom iterintlinkfunc(): gst_pad_iterate_internal_links_default
      Has getcapsfunc(): gst_base_src_getcaps
      Has setcapsfunc(): gst_base_src_setcaps
      Has acceptcapsfunc(): gst_pad_acceptcaps_default
      Has fixatecapsfunc(): 62B82E10
    Pad Template: 'src'
Element Properties:
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "audiotestsrc0"
  blocksize           : Size in bytes to read per buffer (-1 = default)
                        flags: readable, writable
                        Unsigned Long. Range: 0 - 4294967295 Default: 4294967295
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
  freq                : Frequency of test signal
                        flags: readable, writable, controllable
                        Double. Range:               0 -           20000 Default:             440
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
```

Document generated by Confluence on Oct 08, 2015 10:28
