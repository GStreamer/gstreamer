---
title: Writing a Demuxer or Parser
...

# Writing a Demuxer or Parser

Demuxers are the 1-to-N elements that need very special care. They are
responsible for timestamping raw, unparsed data into elementary video or
audio streams, and there are many things that you can optimize or do
wrong. Here, several culprits will be mentioned and common solutions
will be offered. Parsers are demuxers with only one source pad. Also,
they only cut the stream into buffers, they don't touch the data
otherwise.

As mentioned previously in [Caps negotiation][negotiation],
demuxers should use fixed caps, since their data type will not change.

As discussed in [Different scheduling modes][scheduling],
demuxer elements can be written in multiple ways:

  - They can be the driving force of the pipeline, by running their own
    task. This works particularly well for elements that need random
    access, for example an AVI demuxer.

  - They can also run in push-based mode, which means that an upstream
    element drives the pipeline. This works particularly well for
    streams that may come from network, such as Ogg.

In addition, audio parsers with one output can, in theory, also be
written in random access mode. Although simple playback will mostly work
if your element only accepts one mode, it may be required to implement
multiple modes to work in combination with all sorts of applications,
such as editing. Also, performance may become better if you implement
multiple modes. See [Different scheduling modes][scheduling]
to see how an element can accept multiple scheduling modes.

[negotiation]: plugin-development/advanced/negotiation.md
[scheduling]: plugin-development/advanced/scheduling.md
