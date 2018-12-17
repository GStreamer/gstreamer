#!/usr/bin/env python3
#
# Copyright (c) 2018, Matthew Waters <matthew@centricular.com>
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

import os
import sys
import argparse
import json

from signalling import WebRTCSignallingClient
from actions import register_action_types, ActionObserver
from client import WebRTCClient
from browser import Browser, create_driver
from enums import SignallingState, NegotiationState, RemoteState

import gi
gi.require_version("GLib", "2.0")
from gi.repository import GLib
gi.require_version("Gst", "1.0")
from gi.repository import Gst
gi.require_version("GstWebRTC", "1.0")
from gi.repository import GstWebRTC
gi.require_version("GstSdp", "1.0")
from gi.repository import GstSdp
gi.require_version("GstValidate", "1.0")
from gi.repository import GstValidate

class WebRTCApplication(object):
    def __init__(self, server, id_, peerid, scenario_name, browser_name, html_source):
        self.server = server
        self.peerid = peerid
        self.html_source = html_source
        self.id = id_
        self.scenario_name = scenario_name
        self.browser_name = browser_name

    def _init_validate(self, scenario_file):
        self.runner = GstValidate.Runner.new()
        self.monitor = GstValidate.Monitor.factory_create(
            self.client.pipeline, self.runner, None)
        self._scenario = GstValidate.Scenario.factory_create(
            self.runner, self.client.pipeline, self.scenario_name)
        self._scenario.connect("done", self._on_scenario_done)
        self._scenario.props.execute_on_idle = True
        if not self._scenario.props.handles_states:
            self.client.pipeline.set_state(Gst.State.PLAYING)

    def _on_scenario_done(self, scenario):
        self.quit()

    def _connect_actions(self):
        def create_offer():
            self.client.create_offer(None)
            return GstValidate.ActionReturn.OK
        self.actions.create_offer.connect(create_offer)

        def wait_for_negotiation_state(state):
            states = [state, NegotiationState.ERROR]
            state = self.client.wait_for_negotiation_states(states)
            return GstValidate.ActionReturn.OK if state != RemoteState.ERROR else GstValidate.ActionReturn.ERROR
        self.actions.wait_for_negotiation_state.connect(wait_for_negotiation_state)

        def add_stream(pipeline):
            self.client.add_stream(pipeline)
            return GstValidate.ActionReturn.OK
        self.actions.add_stream.connect(add_stream)

    def _connect_client_observer(self):
        def on_offer_created(offer):
            text = offer.sdp.as_text()
            msg = json.dumps({'sdp': {'type': 'offer', 'sdp': text}})
            self.signalling.send(msg)
        self.client.on_offer_created.connect(on_offer_created)

        def on_ice_candidate(mline, candidate):
            msg = json.dumps({'ice': {'sdpMLineIndex': str(mline), 'candidate' : candidate}})
            self.signalling.send(msg)
        self.client.on_ice_candidate.connect(on_ice_candidate)

        def on_pad_added(pad):
            if pad.get_direction() != Gst.PadDirection.SRC:
                return
            self.client.add_stream_with_pad('fakesink', pad)
        self.client.on_pad_added.connect(on_pad_added)

    def _connect_signalling_observer(self):
        def have_json(msg):
            if 'sdp' in msg:
                sdp = msg['sdp']
                res, sdpmsg = GstSdp.SDPMessage.new()
                GstSdp.sdp_message_parse_buffer(bytes(sdp['sdp'].encode()), sdpmsg)
                sdptype = GstWebRTC.WebRTCSDPType.ANSWER if sdp['type'] == 'answer' else GstWebRTC.WebRTCSDPType.OFFER
                desc = GstWebRTC.WebRTCSessionDescription.new(sdptype, sdpmsg)
                self.client.set_remote_description(desc)
            elif 'ice' in msg:
                ice = msg['ice']
                candidate = ice['candidate']
                sdpmlineindex = ice['sdpMLineIndex']
                self.client.add_ice_candidate(sdpmlineindex, candidate)
        self.signalling.have_json.connect(have_json)

        def error(msg):
            # errors are unexpected
            GLib.idle_add(self.quit)
            GLib.idle_add(sys.exit, -20)
        self.signalling.error.connect(error)

    def _init(self):
        self.main_loop = GLib.MainLoop()

        self.client = WebRTCClient()
        self._connect_client_observer()

        self.actions = ActionObserver()
        register_action_types(self.actions)
        self._connect_actions()

        self.signalling = WebRTCSignallingClient(self.server, self.id)
        self._connect_signalling_observer()
        self.signalling.wait_for_states([SignallingState.OPEN])
        self.signalling.hello()

        self.browser = Browser(create_driver(self.browser_name), self.html_source)

        browser_id = self.browser.get_peer_id ()
        assert browser_id == self.peerid

        self.signalling.create_session(self.peerid)

        self._init_validate(self.scenario_name)
        print("app initialized")

    def quit(self):
        # Stop signalling first so asyncio doesn't keep us alive on weird failures
        self.signalling.stop()
        self.browser.driver.quit()
        self.client.stop()
        self.main_loop.quit()

    def run(self):
        try:
            self._init()
            self.main_loop.run()
        except:
            self.quit()
            raise

def parse_options():
    parser = argparse.ArgumentParser()
    parser.add_argument('id', help='ID of this client', type=int)
    parser.add_argument('--peer-id', help='ID of the peer to connect to', type=int)
    parser.add_argument('--server', help='Signalling server to connect to, eg "wss://127.0.0.1:8443"')
    parser.add_argument('--html-source', help='HTML page to open in the browser', default=None)
    parser.add_argument('--scenario', help='Scenario file to execute', default=None)
    parser.add_argument('--browser', help='Browser name to use', default=None)
    return parser.parse_args()

def init():
    Gst.init(None)
    GstValidate.init()

    args = parse_options()
    if not args.scenario:
        args.scenario = os.environ.get('GST_VALIDATE_SCENARIO', None)
    # if we have both manual scenario creation and env, then the scenario
    # is executed twice...
    if 'GST_VALIDATE_SCENARIO' in os.environ:
        del os.environ['GST_VALIDATE_SCENARIO']
    if not args.scenario:
        raise ValueError("No scenario file provided")
    if not args.server:
        raise ValueError("No server location provided")
    if not args.peer_id:
        raise ValueError("No peer id provided")
    if not args.html_source:
        raise ValueError("No HTML page provided")
    if not args.browser:
        raise ValueError("No Browser provided")

    return args

def run():
    args = init()
    w = WebRTCApplication (args.server, args.id, args.peer_id, args.scenario, args.browser, args.html_source)
    return w.run()

if __name__ == "__main__":
    sys.exit(run())
