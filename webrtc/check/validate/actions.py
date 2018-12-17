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

import gi
gi.require_version("GstValidate", "1.0")
from gi.repository import GstValidate

from observer import Signal

class ActionObserver(object):
    def __init__(self):
        def _action_continue(val):
            return val not in [GstValidate.ActionReturn.ERROR, GstValidate.ActionReturn.ERROR_REPORTED]
        def _action_accum(previous, val):
            # we want to always keep any errors propagated
            if val in [GstValidate.ActionReturn.ERROR, GstValidate.ActionReturn.ERROR_REPORTED]:
                return val
            if previous in [GstValidate.ActionReturn.ERROR, GstValidate.ActionReturn.ERROR_REPORTED]:
                return previous

            # we want to always prefer async returns
            if previous in [GstValidate.ActionReturn.ASYNC, GstValidate.ActionReturn.INTERLACED]:
                return previous
            if val in [GstValidate.ActionReturn.ASYNC, GstValidate.ActionReturn.INTERLACED]:
                return val

            return val

        self.create_offer = Signal(_action_continue, _action_accum)
        self.wait_for_negotiation_state = Signal(_action_continue, _action_accum)
        self.add_stream = Signal(_action_continue, _action_accum)
        self.wait_for_remote_state = Signal(_action_continue, _action_accum)

    def _create_offer(self, scenario, action):
        print("action create-offer")
        return self.create_offer.fire()
    def _wait_for_negotiation_state(self, scenario, action):
        state = action.structure["state"]
        print("action wait-for-negotiation-state", state)
        return self.wait_for_negotiation_state.fire(state)
    def _add_stream(self, scenario, action):
        pipeline = action.structure["pipeline"]
        print("action add-stream", pipeline)
        return self.add_stream.fire(pipeline)

def register_action_types(observer):
    if not isinstance(observer, ActionObserver):
        raise TypeError

    GstValidate.register_action_type("create-offer", "webrtc",
                                     observer._create_offer, None,
                                     "Instruct a create-offer to commence",
                                     GstValidate.ActionTypeFlags.NONE)
    GstValidate.register_action_type("wait-for-negotiation-state", "webrtc",
                                     observer._wait_for_negotiation_state, None,
                                     "Wait for a specific negotiation state to be reached",
                                     GstValidate.ActionTypeFlags.NONE)
    GstValidate.register_action_type("add-stream", "webrtc",
                                     observer._add_stream, None,
                                     "Add a stream to the webrtcbin",
                                     GstValidate.ActionTypeFlags.NONE)
