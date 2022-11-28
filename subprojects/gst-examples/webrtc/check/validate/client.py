# Copyright (c) 2020, Matthew Waters <matthew@centricular.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

import threading
import copy

from observer import Signal, WebRTCObserver, DataChannelObserver, StateObserver
from enums import NegotiationState, DataChannelState

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst
gi.require_version("GstWebRTC", "1.0")
from gi.repository import GstWebRTC
gi.require_version("GstSdp", "1.0")
from gi.repository import GstSdp
gi.require_version("GstValidate", "1.0")
from gi.repository import GstValidate


class WebRTCBinObserver(WebRTCObserver):
    """
    Observe a webrtcbin element.
    """
    def __init__(self, element):
        WebRTCObserver.__init__(self)
        self.element = element
        self.signal_handlers = []
        self.signal_handlers.append(element.connect("on-negotiation-needed", self._on_negotiation_needed))
        self.signal_handlers.append(element.connect("on-ice-candidate", self._on_ice_candidate))
        self.signal_handlers.append(element.connect("pad-added", self._on_pad_added))
        self.signal_handlers.append(element.connect("on-new-transceiver", self._on_new_transceiver))
        self.signal_handlers.append(element.connect("on-data-channel", self._on_data_channel))
        self.negotiation_needed = 0
        self._negotiation_needed_observer = StateObserver(self, "negotiation_needed", threading.Condition())
        self.on_negotiation_needed = Signal()
        self.on_ice_candidate = Signal()
        self.on_pad_added = Signal()
        self.on_new_transceiver = Signal()

    def _on_negotiation_needed(self, element):
        self.negotiation_needed += 1
        self._negotiation_needed_observer.update(self.negotiation_needed)
        self.on_negotiation_needed.fire()

    def _on_ice_candidate(self, element, mline, candidate):
        self.on_ice_candidate.fire(mline, candidate)

    def _on_pad_added(self, element, pad):
        self.on_pad_added.fire(pad)

    def _on_description_set(self, promise, desc):
        new_state = self._update_negotiation_from_description_state(desc)
        if new_state == NegotiationState.OFFER_SET:
            self.on_offer_set.fire (desc)
        elif new_state == NegotiationState.ANSWER_SET:
            self.on_answer_set.fire (desc)

    def _on_new_transceiver(self, element, transceiver):
        self.on_new_transceiver.fire(transceiver)

    def _on_data_channel(self, element, channel):
        observer = WebRTCBinDataChannelObserver(channel, channel.props.label, 'remote')
        self.add_channel(observer)

    def _update_negotiation_from_description_state(self, desc):
        new_state = None
        if desc.type == GstWebRTC.WebRTCSDPType.OFFER:
            new_state = NegotiationState.OFFER_SET
        elif desc.type == GstWebRTC.WebRTCSDPType.ANSWER:
            new_state = NegotiationState.ANSWER_SET
        assert new_state is not None
        self._update_negotiation_state(new_state)
        return new_state

    def _deepcopy_session_description(self, desc):
        _, new_sdp = GstSdp.SDPMessage.new_from_text(desc.sdp.as_text())
        return GstWebRTC.WebRTCSessionDescription.new(desc.type, new_sdp)

    def _on_offer_created(self, promise, element):
        self._update_negotiation_state(NegotiationState.OFFER_CREATED)
        reply = promise.get_reply()
        offer = reply['offer']

        new_offer = self._deepcopy_session_description(offer)
        promise = Gst.Promise.new_with_change_func(self._on_description_set, new_offer)

        new_offer = self._deepcopy_session_description(offer)
        self.element.emit('set-local-description', new_offer, promise)
        self.on_offer_created.fire(offer)

    def _on_answer_created(self, promise, element):
        self._update_negotiation_state(NegotiationState.ANSWER_CREATED)
        reply = promise.get_reply()
        offer = reply['answer']

        new_offer = self._deepcopy_session_description(offer)
        promise = Gst.Promise.new_with_change_func(self._on_description_set, new_offer)

        new_offer = self._deepcopy_session_description(offer)
        self.element.emit('set-local-description', new_offer, promise)
        self.on_answer_created.fire(offer)

    def create_offer(self, options=None):
        promise = Gst.Promise.new_with_change_func(self._on_offer_created, self.element)
        self.element.emit('create-offer', options, promise)

    def create_answer(self, options=None):
        promise = Gst.Promise.new_with_change_func(self._on_answer_created, self.element)
        self.element.emit('create-answer', options, promise)

    def set_remote_description(self, desc):
        promise = Gst.Promise.new_with_change_func(self._on_description_set, desc)
        self.element.emit('set-remote-description', desc, promise)

    def add_ice_candidate(self, mline, candidate):
        self.element.emit('add-ice-candidate', mline, candidate)

    def add_data_channel(self, ident):
        channel = self.element.emit('create-data-channel', ident, None)
        observer = WebRTCBinDataChannelObserver(channel, ident, 'local')
        self.add_channel(observer)

    def wait_for_negotiation_needed(self, generation):
        self._negotiation_needed_observer.wait_for ((generation,))

class WebRTCStream(object):
    """
    An stream attached to a webrtcbin element
    """
    def __init__(self):
        self.bin = None

    def set_description(self, desc):
        assert self.bin is None
        self.bin = Gst.parse_bin_from_description(desc, True)

    def add_and_link(self, parent, link):
        assert self.bin is not None
        self.bin.set_locked_state(True)
        parent.add(self.bin)
        src = self.bin.get_static_pad("src")
        sink = self.bin.get_static_pad("sink")
        assert src is None or sink is None
        if src:
            self.bin.link(link)
        if sink:
            link.link(self.bin)
        self.bin.set_locked_state(False)
        self.bin.sync_state_with_parent()

    def add_and_link_to(self, parent, link, pad):
        assert self.bin is not None
        self.bin.set_locked_state(True)
        parent.add(self.bin)
        src = self.bin.get_static_pad("src")
        sink = self.bin.get_static_pad("sink")
        assert src is None or sink is None
        if pad.get_direction() == Gst.PadDirection.SRC:
            assert sink is not None
            pad.link(sink)
        if pad.get_direction() == Gst.PadDirection.SINK:
            assert src is not None
            src.link(pad)
        self.bin.set_locked_state(False)
        self.bin.sync_state_with_parent()

class WebRTCClient(WebRTCBinObserver):
    """
    Client for performing webrtc operations.  Controls the pipeline that
    contains a webrtcbin element.
    """
    def __init__(self):
        self.pipeline = Gst.Pipeline(None)
        self.webrtcbin = Gst.ElementFactory.make("webrtcbin")
        super().__init__(self.webrtcbin)
        self.pipeline.add(self.webrtcbin)
        self._streams = []

    def stop(self):
        self.pipeline.set_state (Gst.State.NULL)

    def add_stream(self, desc):
        stream = WebRTCStream()
        stream.set_description(desc)
        stream.add_and_link (self.pipeline, self.webrtcbin)
        self._streams.append(stream)

    def add_stream_with_pad(self, desc, pad):
        stream = WebRTCStream()
        stream.set_description(desc)
        stream.add_and_link_to (self.pipeline, self.webrtcbin, pad)
        self._streams.append(stream)

    def set_options (self, opts):
        if opts.has_field("local-bundle-policy"):
            self.webrtcbin.props.bundle_policy = opts["local-bundle-policy"]


class WebRTCBinDataChannelObserver(DataChannelObserver):
    """
    Data channel observer for a webrtcbin data channel.
    """
    def __init__(self, target, ident, location):
        super().__init__(ident, location)
        self.target = target
        self.signal_handlers = []
        self.signal_handlers.append(target.connect("on-open", self._on_open))
        self.signal_handlers.append(target.connect("on-close", self._on_close))
        self.signal_handlers.append(target.connect("on-error", self._on_error))
        self.signal_handlers.append(target.connect("on-message-data", self._on_message_data))
        self.signal_handlers.append(target.connect("on-message-string", self._on_message_string))
        self.signal_handlers.append(target.connect("on-buffered-amount-low", self._on_buffered_amount_low))

    def _on_open(self, channel):
        self._update_state (DataChannelState.OPEN)
    def _on_close(self, channel):
        self._update_state (DataChannelState.CLOSED)
    def _on_error(self, channel):
        self._update_state (DataChannelState.ERROR)
    def _on_message_data(self, channel, data):
        self.data.append(msg)
    def _on_message_string(self, channel, msg):
        self.got_message (msg)
    def _on_buffered_amount_low(self, channel):
        pass

    def close(self):
        self.target.emit('close')

    def send_string (self, msg):
        self.target.emit('send-string', msg)
