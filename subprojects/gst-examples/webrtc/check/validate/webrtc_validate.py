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
import logging

from signalling import WebRTCSignallingClient, RemoteWebRTCObserver
from actions import ActionObserver
from client import WebRTCClient
from browser import Browser, create_driver
from enums import SignallingState, NegotiationState, DataChannelState, Actions

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

FORMAT = '%(asctime)-23s %(levelname)-7s  %(thread)d   %(name)-24s\t%(funcName)-24s %(message)s'
LEVEL = os.environ.get("LOGLEVEL", "DEBUG")
logging.basicConfig(level=LEVEL, format=FORMAT)
l = logging.getLogger(__name__)

class WebRTCApplication(object):
    def __init__(self, server, id_, peerid, scenario_name, browser_name, html_source, test_name=None):
        self.server = server
        self.peerid = peerid
        self.html_source = html_source
        self.id = id_
        self.scenario_name = scenario_name
        self.browser_name = browser_name
        self.test_name = test_name

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
        l.error ('scenario done')
        GLib.idle_add(self.quit)

    def _connect_actions(self, actions):
        def on_action(atype, action):
            """
            From a validate action, perform the action as required
            """
            if atype == Actions.CREATE_OFFER:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                c.create_offer()
                return GstValidate.ActionReturn.OK
            elif atype == Actions.CREATE_ANSWER:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                c.create_answer()
                return GstValidate.ActionReturn.OK
            elif atype == Actions.WAIT_FOR_NEGOTIATION_STATE:
                states = [NegotiationState(action.structure["state"]), NegotiationState.ERROR]
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                state = c.wait_for_negotiation_states(states)
                return GstValidate.ActionReturn.OK if state != NegotiationState.ERROR else GstValidate.ActionReturn.ERROR
            elif atype == Actions.ADD_STREAM:
                self.client.add_stream(action.structure["pipeline"])
                return GstValidate.ActionReturn.OK
            elif atype == Actions.ADD_DATA_CHANNEL:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                c.add_data_channel(action.structure["id"])
                return GstValidate.ActionReturn.OK
            elif atype == Actions.SEND_DATA_CHANNEL_STRING:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                channel = c.find_channel (action.structure["id"])
                channel.send_string (action.structure["msg"])
                return GstValidate.ActionReturn.OK
            elif atype == Actions.WAIT_FOR_DATA_CHANNEL_STATE:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                states = [DataChannelState(action.structure["state"]), DataChannelState.ERROR]
                channel = c.find_channel (action.structure["id"])
                state = channel.wait_for_states(states)
                return GstValidate.ActionReturn.OK if state != DataChannelState.ERROR else GstValidate.ActionReturn.ERROR
            elif atype == Actions.CLOSE_DATA_CHANNEL:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                channel = c.find_channel (action.structure["id"])
                channel.close()
                return GstValidate.ActionReturn.OK
            elif atype == Actions.WAIT_FOR_DATA_CHANNEL:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                state = c.wait_for_data_channel(action.structure["id"])
                return GstValidate.ActionReturn.OK
            elif atype == Actions.WAIT_FOR_DATA_CHANNEL_STRING:
                assert action.structure["which"] in ("local", "remote")
                c = self.client if action.structure["which"] == "local" else self.remote_client
                channel = c.find_channel (action.structure["id"])
                channel.wait_for_message(action.structure["msg"])
                return GstValidate.ActionReturn.OK
            elif atype == Actions.WAIT_FOR_NEGOTIATION_NEEDED:
                self.client.wait_for_negotiation_needed(action.structure["generation"])
                return GstValidate.ActionReturn.OK
            elif atype == Actions.SET_WEBRTC_OPTIONS:
                self.client.set_options (action.structure)
                self.remote_client.set_options (action.structure)
                return GstValidate.ActionReturn.OK
            else:
                assert "Not reached" == ""

        actions.action.connect (on_action)

    def _connect_client_observer(self):
        def on_offer_created(offer):
            text = offer.sdp.as_text()
            msg = json.dumps({'sdp': {'type': 'offer', 'sdp': text}})
            self.signalling.send(msg)
        self.client.on_offer_created.connect(on_offer_created)

        def on_answer_created(answer):
            text = answer.sdp.as_text()
            msg = json.dumps({'sdp': {'type': 'answer', 'sdp': text}})
            self.signalling.send(msg)
        self.client.on_answer_created.connect(on_answer_created)

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
                res, sdpmsg = GstSdp.SDPMessage.new_from_text(sdp['sdp'])
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
            l.error ('Unexpected error: ' + msg)
            GLib.idle_add(self.quit)
            GLib.idle_add(sys.exit, -20)
        self.signalling.error.connect(error)

    def _init(self):
        self.main_loop = GLib.MainLoop()

        self.client = WebRTCClient()
        self._connect_client_observer()

        self.signalling = WebRTCSignallingClient(self.server, self.id)
        self.remote_client = RemoteWebRTCObserver (self.signalling)
        self._connect_signalling_observer()

        actions = ActionObserver()
        actions.register_action_types()
        self._connect_actions(actions)

        # wait for the signalling server to start up before creating the browser
        self.signalling.wait_for_states([SignallingState.OPEN])
        self.signalling.hello()

        self.browser = Browser(create_driver(self.browser_name))
        self.browser.open(self.html_source)

        browser_id = self.browser.get_peer_id ()
        assert browser_id == self.peerid

        self.signalling.create_session(self.peerid)
        test_name = self.test_name if self.test_name else self.scenario_name
        self.remote_client.set_title (test_name)

        self._init_validate(self.scenario_name)

    def quit(self):
        # Stop signalling first so asyncio doesn't keep us alive on weird failures
        l.info('quiting')
        self.signalling.stop()
        l.info('signalling stopped')
        self.main_loop.quit()
        l.info('main loop stopped')
        self.client.stop()
        l.info('client stopped')
        self.browser.driver.quit()
        l.info('browser exitted')

    def run(self):
        try:
            self._init()
            l.info("app initialized")
            self.main_loop.run()
            l.info("loop exited")
        except:
            l.exception("Fatal error")
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
    parser.add_argument('--name', help='Name of the test', default=None)
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
    w = WebRTCApplication (args.server, args.id, args.peer_id, args.scenario, args.browser, args.html_source, test_name=args.name)
    return w.run()

if __name__ == "__main__":
    sys.exit(run())
