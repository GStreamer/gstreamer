A lot of data streams need a few initial buffers to make sense of the
data stream.  With these initial buffers, it can pick up at any point in
the rest of the data stream.

Examples:
- Vorbis and Theora have three initial Ogg packets
- MPEG4 has codec data
- GDP protocol serializes the initial new_segment event and the initial caps

It is important that elements that get connected after the stream starts,
receive these initial buffers.  Also, sink elements should know that these
initial buffers are necessary for every new "client" of the sink element;
for example, multifdsink needs to send these initial buffers before the
normal buffers.

Currently, this is done by putting a 'streamheader' value on the caps.
'streamheader' is a GST_TYPE_ARRAY of GstBuffer, ie. an ordered list of
buffers.

The buffers themselves still get pushed as normal buffers, but with the
IN_CAPS flag set.  This allows elements that do not know about streamheader
to still function correctly (ie. filesink and others)

It is of course important that these streamheader buffers are not sent twice
(once because they're in the caps, and once because they're received as
buffers).

So, an element that is aware of streamheader in the caps should probably best
drop all incoming IN_CAPS buffers.

Rules for sending streamheaders:
- all streamheader buffers should have IN_CAPS set
- from this list of streamheader buffers, a "streamheader" caps field should
  be created as a GST_TYPE_ARRAY of GstBuffer.  This should be done by
  copying the buffers.
  (the only important thing about the buffers in this list is the data;
   other attributes of the buffers get ignored)
- each of these buffers should then have these caps set on them.
- when all streamheader buffers are collected in the element, pad caps should
  be set, including this streamheader
- streamheader buffers should be sent consecutively, and before any of the
  data (non-IN_CAPS) buffers they apply to.  If necessary, the element
  should internally queue non-IN_CAPS buffers until the streamheaders
  are completely assembled.
- when new streamheader buffers need to be pushed out, this process is
  repeated.  Receiving a new IN_CAPS buffer after a non-IN_CAPS buffer
  signifies resetting streamheader, as does the new set_caps with different
  streamheader right before. (FIXME: it's probably better to explicitly
  have an event/buffer that clears streamheaders; consider the case of
  an element like GDP that has created streamheader from the first newsegment
  and the first caps, and then receives a new tag event that it also wants
  to put on the streamheader - it should be able to invalidate the previous
  ones)

Elements that can send streamheader caps:
- vorbisenc
- theoraenc
- gdppay

Elements that can receive streamheader caps:
- multifdsink

Elements that can receive and send streamheader caps:
- oggmux

Elements that could/should use this:
- ffenc_mpeg4 (currently uses/sets codec_data)
- theoradec, vorbisdec (currently only look at incoming buffers)
- oggdemux

FUTURE PLANS
------------
The concept of streamheader is more generally applicable.  We may want to
find a better way of implementing this than having two ways of telling
downstream elements about them.  Currently an element that is
streamheader-aware needs to look both at caps and incoming buffers.

One option would be to only rely on the buffer flow, and make all elements
aware of a HEADER flag, causing them to keep these buffers around and push
them for each new pad link.  This needs to be done in every element; a
scenario like
  videotestsrc ! theoraenc
where later on an oggmux gets connected, should still work without dropping
these header buffers.  But similarly,
  videotestsrc ! theoraenc ! identity
with a later connection of oggmux to identity should work.

This could tie in with an idea to have pads store some initial data (the
first new_segment event, tag events, ...), which at the moment also get
dropped on dynamic in-stream links.
