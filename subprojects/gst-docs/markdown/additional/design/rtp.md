# RTP

These design docs detail some of the lower-level mechanism of certain parts
of GStreamer's RTP stack. For a higher-level overview see the [RTP and RTSP
support](additional/rtp.md) section.

# RTP auxiliary stream design

## Auxiliary elements

There are two kind of auxiliary elements, sender and receiver. Let's
call them rtpauxsend and rtpauxreceive.

rtpauxsend has always one sink pad and can have unlimited requested src
pads. If only src pad then it works in SSRC-multiplexed mode, if several
src pads then it works in session multiplexed mode.

rtpauxreceive has always one ssrc pad and can have unlimited requested
sink pads. If only one sink pad then it works in SSRC-multiplexed mode,
if several sink pads then it works in session multiplexed mode.

## Rtpbin and auxiliary elements

### Basic mechanism

rtpbin knows for which session ids the given auxiliary element belong
to. It's done through "set-aux-send", for rtpauxsend kind, and through
"set-aux-receive" for rtpauxreceive kind. You can call those signals as
much as needed for each auxiliary element. So for aux elements that work
in SSRC-multiplexed mode this signal action is called only one time.

The user has to call those action signals before to request the
differents rtpbin pads. rtpbin is in charge to link those auxiliary
elements with the sessions, and on receiver side, rtpbin has also to
handle the link with ssrcdemux.

rtpbin never knows if the given rtpauxsend is actually a rtprtxsend
element or another aux element. rtpbin never knows if the given
rtpauxreceive is actually a rtprtxreceive element or another aux
element. rtpbin has to be kept generic so that more aux elements can be
added later without changing rtpbin.

It's currently not possible to use rtpbin with auxiliary stream from
gst-launch. We can discuss about having the ability for rtpbin to
instanciate itself the special aux elements rtprtxsend and rtprtxreceive
but they need to be configured ("payload-type" and "payload-types"
properties) to make retransmission work. So having several rtprtxsend
and rtprtxreceive in a rtpbin would require a lot of properties to
manage them form rtpbin. And for each auxiliary elements.

If you want to use rtprtxreceive and rtprtpsend from gst-launch you have
to use rtpsession, ssrcdemux and rtpjitterbuffer elements yourself. See
gtk-doc of rtprtxreceive for an example.

### Requesting the rtpbin's pads on the pipeline receiver side

If rtpauxreceive is set for session, i, j, k then it has to call
rtpbin::"set-aux-receive" 3 times giving those ids and this aux element.
It has to be done before requesting the `recv_rtp_sink_i`,
`recv_rtp_sink_j`, `recv_rtp_sink_k`. For a concrete case
rtprtxreceive, if the user wants it for session i, then it has to call
rtpbin::"set-aux-receive" one time giving i and this aux element. Then
the user can request `recv_rtp_sink_i` pad.

Calling rtpbin::"set-aux-receive" does not create the session. It add
the given session id and aux element to a hashtable(key:session id,
value: aux element). Then when the user ask for
`rtpbin.recv_rtp_sink_i`, rtpbin lookup if there is an aux element for
this i session id. If yes it requests a sink pad to this aux element and
links it with the `recv_rtp_src` pad of the new gstrtpsession. rtpbin
also checks that this aux element is connected only one time to
ssrcdemux. Because rtpauxreceive has only one source pad. Each call to
request `rtpbin.recv_rtp_sink_k` will also creates
`rtpbin.recv_rtp_src_k_ssrc_pt` as usual. So that the user have it
when then it requests rtpbin. (from gst-launch) or using
`on_rtpbinreceive_pad_added` callback from an application.

### Requesting the rtpbin's pads on the pipeline sender side

For the sender this is similar but a bit more complicated to implement.
When the user asks for `rtpbin.send_rtp_sink_i`, rtpbin will lookup in
its second map (key:session id, value: aux send element). If there is
one aux element, then it will set the sink pad of this aux sender
element to be the ghost pad `rtpbin.send_rtp_sink_i` that the user
asked. rtpbin will also request a src pad of this aux element to connect
it to `gstrtpsession_i`. It will automatically create
`rtpbin.send_rtp_src_i` the usuall way. Then if the user asks
`rtpbin.send_rtp_src_k`, then rtpbin will also lookup in that map and
request another source pad of the aux element and connect it to the new
`gstrtpsession_k`.

# RTP collision design

## GstRTPCollision

Custon upstream event which contains the ssrc marked as collided.

This event is generated on both pipeline sender and receiver side by the
gstrtpsession element when it detects a conflict between ssrc. (same
session id and same ssrc)

It's an upstream event so that means this event is for now only useful
on pipeline sender side. Because elements generating packets with the
collided SSRC are placed upstream from the gstrtpsession.

## rtppayloader

When handling a `GstRTPCollision` event, the rtppayloader has to choose
another ssrc.

## BYE only the corresponding source, not the whole session.

When a collision happens for the given ssrc, the associated source is
marked bye. But we make sure that the whole session is not itself set
bye. Because internally, gstrtpsession can manages several sources and
all have their own distinct ssrc.

# RTP retransmission design

## GstRTPRetransmissionRequest

Custom upstream event which mainly contains the ssrc and the seqnum of
the packet which is asked to be retransmitted.

On the pipeline receiver side this event is generated by the
gstrtpjitterbuffer element. Then it is translated to a NACK to be sent
over the network.

On the pipeline sender side, this event is generated by the
gstrtpsession element when it receives a NACK from the network.

## rtprtxsend element

### Basic mechanism

rtprtxsend keeps a history of rtp packets that it has already sent. When
it receives the event `GstRTPRetransmissionRequest` from the downstream
gstrtpsession element, it looks up the requested seqnum in its stored
packets. If the packet is present in its history, it will create an RTX
packet according to RFC 4588. Then this rtx packet is pushed to its src
pad like other packets.

rtprtxsend works in SSRC-multiplexed mode, so it always has one sink and
src pad.

### Building retransmission packet from original packet

An rtx packet is mostly the same as an orignal packet, except it has its
own `ssrc` and its own `seqnum`. That's why rtprtxsend works in
SSRC-multiplexed mode. It also means that the same session is used.
Another difference between an rtx packet and its original is that it
inserts the original seqnum (OSN: 2 bytes) at the beginning of the
payload. Also rtprtxsend builds rtx packet without padding, to let other
elements do that. The last difference is the payload type. For now the
user has to set it through the `rtx-payload-type` property. Later it will
automatically retreive this information from SDP. See `fmtp` field as
specified in RFC 4588 (a=fmtp:99 apt=98): `fmtp` is the payload type of
the retransmission stream and `apt` the payload type of its associated
master stream.

### Retransmission ssrc and seqnum

To choose `rtx_ssrc` it randomly selects a number between 0 and 2^32-1
until it is different from `master_ssrc`. `rtx_seqnum` is randomly
selected between 0 and 2^16-1.

### Deeper in the stored buffer history

For the history it uses a GSequence with 2^15-1 as its maximum size.
Which is resonable as the default value is 100. It contains the packets
in reverse order they have been sent (head:newest, tail:oldest).
GSequence allows to add and remove an element in constant time (like a
queue). Also GSequence allows to do a binary search when rtprtxsend
does a lookup in its history. It's important if it receives a lot of requests
or if the history is large.

### Pending rtx packets

When looking up in its history, if seqnum is found then it pushes the
buffer into a GQueue to its tail. Before sending the current master
stream packet, rtprtxsend sends all the buffers which are in this
GQueue, taking care of converting them to rtx packets. This way, rtx
packets are sent in the same order they have been requested.
(`g_list_foreach` traverses the queue from head to tail) The `GQueue` is
cleared between sending 2 master stream packets. So when this `GQueue`
contains more than one element, it means that rtprtxsend had received more
than one rtx request between sending 2 master packets.

### Collision

When handling a `GstRTPCollision` event, if the ssrc is its rtx ssrc then
rtprtxsend clears its history and its pending retransmission queue. Then
it chooses a `rtx_ssrc` until it's different than master ssrc. If the
`GstRTPCollision` event does not contain its rtx ssrc, for example its
master ssrc or other, then it just forwards the event upstream, so
that it can be handled by the rtppayloader.

## Rtprtxreceive element

### Basic mechanism

The same rtprtxreceive instance can receive several master streams and
several retransmission streams. So it will try to dynamically associate
an rtx ssrc with its master ssrc, so that it can reconstruct the original
from the proper rtx packet.

The algorithm is based on the fact that seqnums of different streams
(considering all master and all rtx streams) evolve at a different rate.
It means that the initial seqnum is random for each one and the offset
could also be different. So that they are statistically all different at
a given time. If bad luck then the association is delayed to the next
rtx request.

The algorithm also needs to know if a given packet is an rtx packet or
not. To know this information there is the `rtx-payload-types` property.
For now the user has to configure it but later it will automatically
retreive this information from SDP. It needs to know if the current
packet is rtx or not in order to know if it can extract the OSN from the
payload. Otherwise it would extract the OSN even on master streams which
means nothing and so it could do bad things. In theory maybe it could
work but we have this information in SDP so why not use it to avoid
bad associations.

Note that it also means that several master streams can have the same
payload type. And also several rtx streams can have the same payload
type. So the information from SDP which gives us which rtx payload type
belongs to a given master payload type is not enough to do the association
between rtx ssrc and master ssrc.

rtprtxreceive works in SSRC-multiplexed mode, so it always has one sink
and src pad.

### Deeper in the association algorithm

When it receives a `GstRTPRetransmissionRequest` event it will remember
the ssrc and the seqnum from this request.

On incoming packets, if the packet has its ssrc already associated then
it knows if the ssrc is an rtx ssrc or a master stream ssrc. If this is
a rtx packet then it recontructs the original and pushes the result to
the src pad as if it was a master packet.

If the ssrc is not yet associated rtprtxreceive checks the payload type.
if the packet has its payload type marked as rtx then it will extract
the OSN (original seqnum number) and lookup in its stored requests if a
seqnum matches. If found, then it associates the current ssrc to the
master ssrc marked in the request. If not found it just drops the
packet. Then it removes the request from the stored requests.

If there are 2 requests with the same seqnum and different ssrc, then
the couple seqnum,ssrc is removed from the stored requests. A stored
request actually means that actually the couple seqnum,ssrc is stored.
If it happens the request is dropped but it avoids to do bad
associations. In this case the association is just delayed to the next
request.

### Building original packet from rtx packet

Header, extensions, payload and padding are mostly the same. Except that
the OSN is removed from the payload. Then ssrc, seqnum, and original
payload type are correctly set. Original payload type is actually also
stored when the rtx request is handled.
