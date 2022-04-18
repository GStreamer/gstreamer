# Sparse Streams

## Introduction

In 0.8, there was some support for sparse streams through the use of
`FILLER` events. These were used to mark gaps between buffers so that
downstream elements could know not to expect any more data for that gap.

In 0.10, segment information conveyed through `SEGMENT` events can be used
for the same purpose.

In 1.0, there is a `GAP` event that works in a similar fashion as the
`FILLER` event in 0.8.

## Use cases

### Sub-title streams

Sub-title information from muxed formats such as
Matroska or MPEG consist of irregular buffers spaced far apart compared
to the other streams (audio and video). Since these usually only appear
when someone speaks or some other action in the video/audio needs
describing, they can be anywhere from 1-2 seconds to several minutes
apart. Downstream elements that want to mix sub-titles and video (and muxers)
have no way of knowing whether to process a video packet or wait a moment
for a corresponding sub-title to be delivered on another pad.

### Still frame/DVD menues

In DVDs and other formats, there are still-frame regions where the current
video frame should be retained and no audio played for a period. In DVD,
these are described either as a fixed duration, or infinite duration still
frame.

### Avoiding processing silence from audio generators

Imagine a source that, from time to time, produces empty buffers (silence or
blank images). If the pipeline has many elements next, it is better to
optimise the absolute data processing in this case. Examples for such sources
are sound-generators (simsyn in gst-buzztard) or a source in a voip
application that uses noise-gating (to save bandwith).

## Details

### Sub-title streams

The main requirement here is to avoid stalling the
pipeline between sub-title packets, and is effectively updating the
minimum-timestamp for that
stream.

A demuxer can do this by sending an 'update' SEGMENT with a new start time
to the subtitle pad. For example, every time the SCR in MPEG data
advances more than 0.5 seconds, the MPEG demuxer can issue a SEGMENT with
(update=TRUE, start=SCR ). Downstream elements can then be aware not to
expect any data older than the new start time.

The same holds true for any element that knows the current position in the
stream - once the element knows that there is no more data to be presented
until time 'n' it can advance the start time of the current segment to 'n'.

This technique can also be used, for example, to represent a stream of
MIDI events spaced to a clock period. When there is no event present for
a clock time, a SEGMENT update can be sent in its place.

### Still frame/menu support

Still frames in DVD menus are different because they do not introduce a gap
in the data timestamps. Instead, they represent a pause in the presentation
of a stream. Correctly performing the wait requires some synchronisation with
downstream elements.

In this scenario, an upstream element that wants to execute a still frame
performs the following steps:

  - Send all data before the still frame wait

  - Send a `DRAIN` event to ensure that all data has been played
    downstream.

  - wait on the clock for the required duration, possibly interrupting
    if necessary due to an intervening activity (such as a user
    navigation)

  - FLUSH the pipeline using a normal flush sequence (`FLUSH_START`,
    chain-lock, `FLUSH_STOP`)

  - Send a SEGMENT to restart playback with the next timestamp in the
    stream.

The upstream element performing the wait must only do so when in the `PLAYING`
state. During `PAUSED`, the clock will not be running, and may not even have
been distributed to the element yet.

`DRAIN` is a new event that will block on a src pad until all data downstream
has been played out.

Flushing after completing the still wait is to ensure that data after the wait
is played correctly. Without it, sinks will consider the first buffers
(x seconds, where x is the duration of the wait that occurred) to be
arriving late at the sink, and they will be discarded instead of played.

### For audio

It is the same case as the first one - there is a *gap* in the audio
data that needs to be presented, and this can be done by sending a
SEGMENT update that moves the start time of the segment to the next
timestamp when data will be sent.

For video, however, it is slightly different. Video frames are typically
treated at the moment as continuing to be displayed after their indicated
duration if no new frame arrives. Here, it is desired to display a blank
frame instead, in which case at least one blank frame should be sent before
updating the start time of the segment.
