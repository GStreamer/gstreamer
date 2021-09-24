---
short-description: The GStreamer Editing Services prototyping tool
...

# ges-launch-1.0

**ges-launch-1.0** creates a multimedia
[timeline](https://phabricator.freedesktop.org/w/gstreamer/gst-editing-services/ges-timeline/)
and plays it back, or renders it to the specified format.

It can load a timeline from an existing project, or create one from the
specified commands.

Updating an existing project can be done through thanks to the
[GstValidate](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-validate/html/)
[scenarios](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-validate/html/scenarios.html)
using the `--set-scenario` argument, if ges-launch-1.0 has been compiled with
GstValidate support.

You can inspect action types with:

    ges-launch-1.0 --inspect-action-type

By default, ges-launch-1.0 is in "playback-mode".

## Synopsis

**ges-launch-1.0**  [-l <path>|--load=<path>] [-s <path>|--save=<path>]
  [-p <path>|--sample-path=<path>] [-r <path>|--sample-path-recurse=<path>]
  [-o <uri>|--outputuri=<uri>] [-f <profile>|--format=<profile>]
  [-e <profile-name>|--encoding-profile=<profile-name>]
  [-t <track-types>|--track-types=<track-types>]
  [-v <videosink>|--videosink=<videosink>]
  [-a <audiosink>---audiosink=<audiosink>]
  [-m|--mute] [--inspect-action-type[=<action-type>]]
  [--list-transitions] [--disable-mixing]
  [-r <times>|--repeat=<times>] [--set-scenario=<scenario-name]

## Define a timeline through the command line

The `ges-launch-1.0` tool allows you to simply build a timeline through a dedicated set of commands:

### +clip

Adds a clip to the timeline.

See documentation for the --track-types option to ges-launch-1.0, as it
will affect the result of this command.

#### Examples:

    ges-launch-1.0 +clip /path/to/media

This will simply play the sample from its beginning to its end.

    ges-launch-1.0 +clip /path/to/media inpoint=4.0

Assuming "media" is a 10 second long media sample, this will play the sample
from the 4th second to the 10th, resulting in a 6-seconds long playback.

    ges-launch-1.0 +clip /path/to/media inpoint=4.0 duration=2.0 start=4.0

Assuming "media" is an audio video sample longer than 6 seconds, this will play
a black frame and silence for 4 seconds, then the sample from its 4th second to
its sixth second, resulting in a 6-seconds long playback.

    ges-launch-1.0 --track-types=audio +clip /path/to/media

Assuming "media" is an audio video sample, this will only play the audio of the
sample in its entirety.

    ges-launch-1.0 +clip /path/to/media1 layer=1 set-alpha 0.9 +clip /path/to/media2 layer=0

Assume media1 and media2 both contain audio and video and last for 10 seconds.

This will first add media1 in a new layer of "priority" 1, thus implicitly
creating a layer of "priority" 0, the start of the clip will be 0 as no clip
had been added in that layer before.

It will then add media2 in the layer of "priority" 0 which was created
previously, the start of this new clip will also be 0 as no clip has been added
in this layer before.

Both clips will thus overlap on two layers for 10 seconds.

The "alpha" property of the second clip will finally be set to a value of 0.9.

All this will result in a 10 seconds playback, where media2 is barely visible
through media1, which is nearly opaque. If alpha was set to 0.5, both clips
would be equally visible, and if it was set to 0.0, media1 would be invisible
and media2 completely opaque.

#### Mandatory arguments

__path|uri:__

Specifies the location of the sample to make a clip from.

#### Options

__inpoint[i]=<inpoint>:__

Sets the inpoint of the clip, that is the
position in the original sample at which the clip will start outputting
data.

It is an error to have an inpoint superior to the actual duration of the original sample.

0 by default.


__duration[i]=<duration>:__

Sets the duration of the clip, that is the
duration of the media the clip will output.

It is an error to have inpoint + duration be superior to the duration of the
original sample.

The default is the duration of the original sample - the inpoint of the clip.


__start[s]=<start>:__


Sets the start of the clip, that is its position in
the timeline.

If not specified, it will be set to the duration of the layer the clip is added on,
as the expected default behaviour is to queue clips one after another.


__layer[l]=<layer>:__

Sets the layer of the clip. The video stream in
overlapping clips on different layers will be blended together according
to their alpha property, starting with the clip on the last layer. An
example is shown in the EXAMPLES section.

If not specified, it will be set to the last layer a clip has been added on, or
a first layer if no clip has been added yet.


#### Properties

##### Video properties

These have no effects if there is no video stream in the sample.

__alpha:__

This is the amount of transparency of the clip, ranging from 0.0
to 1.0 Clips overlapping on different layers will be composited
together, unless --disable-mixing has been specified, in the order of
the layers.


__posx:__

This is the x position (offset) of the clip in pixels, relatively
to the output frame of the timeline.


__posy:__

This is the y position (offset) of the clip in pixels, relatively
to the output frame of the timeline.


__width:__

This is the width in pixels that the clip will occupy in the
final output frame.


__height:__

This is the height in pixels that the clip will occupy in the final output frame.


##### Audio properties

__volume:__

This is the volume that will be set on the audio part of the
clip, ranging from 0.0 to 10.0, with 1.0 being the default.


__mute:__

Set to true to mute the audio of the clip. Default is false.


### +effect

#### Mandatory arguments

__bin-description:__

Specifies the description of a GStreamer a bin, in the gst-launch format.


#### Options

Properties can be set on the effect either directly in the bin-description, or
separately through the set-<property-name> command, which will lookup any
readable property in any of the children of the bin, and set the provided value
on it.

#### Examples

    ges-launch-1.0 +clip /path/to/media +effect "agingtv"

This will apply the agingtv effect to "media" and play it back.

### set-<property-name>

Sets the property of an object (for example a clip or an effect). Trying to set
a property than can't be looked up is considered an error.

By default, set-<property-name> will lookup the property on the last added
object.

#### Examples

    ges-launch-1.0 +clip /path/to/media set-alpha 0.3

This will set the alpha property on "media" then play it back, assuming "media"
contains a video stream.

    ges-launch-1.0 +clip /path/to/media +effect "agingtv" set-dusts false

This will set the "dusts" property of the agingtv to false and play the
timeline back.

### +title

Adds a title to the timeline.

#### Mandatory arguments

__text:__

The text to be used as a Title.


## Options

### Project-related options

__--load[-l]=<path>:__

Load project from file. The project be saved again with the --save option.

__-s --save=<path>:__
Save project to file before rendering. It can then be loaded with the --load option

__-p --sample-path:__
If some assets are missing when loading a project file, ges-launch-1.0 will try to
locate them in this path. It is especially useful when sharing a project.

__-r --sample-path-recurse:__

Identical to --sample-path, but ges-launch-1.0 will also recurse in the subfolders
to look for assets.

### Rendering options

__-o --outputuri=<uri>:__

If set, ges-launch-1.0 will render the specified timeline instead
of playing it back. The default rendering format is ogv, containing
theora and vorbis.

__-f --format=<profile>:__

Set an encoding profile on the command line. See ges-launch-1.0 help profile
for more information.
This will have no effect if no outputuri has been specified.

__-e --encoding-profile=<profile-name>:__

Set an encoding profile from a preset file. See ges-launch-1.0 help profile
for more information.
This will have no effect if no outputuri has been specified.

__-t --track-types=<track-types>:__

Specify the track types to be created. When loading a project, only relevant
tracks will be added to the timeline.

### Playback options

__-v --videosink=<videosink>:__

Set the videosink used for playback.

__-a --audiosink=<audiosink>:__

Set the audiosink used for playback.


__-m --mute:__

Mute playback output. This has no effect when rendering.


### Helpful options

__--inspect-action-type=<action-type>:__

Inspect the available action types that can be defined in a scenario set with
--set-scenario. Will list all action-types if action-type is empty.


__--list-transitions:__

List all valid transition types and exit. See ges-launch-1.0 help transition
for more information.


### Generic options

__--disable-mixing:__

Do not use mixing elements to mix layers together.


__-r --repeat=<times>:__

Set the number of times to repeat the timeline.


__--set-scenario:__

ges-launch-1.0 exposes gst-validate functionalities, such as scenarios.
Scenarios describe actions to execute, such as seeks or setting of properties.
GES implements editing-specific actions such as adding or removal of clips.
