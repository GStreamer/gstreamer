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

import gi
gi.require_version("GstValidate", "1.0")
from gi.repository import GstValidate

from observer import Signal
from enums import Actions

import logging

l = logging.getLogger(__name__)

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

        self.action = Signal(_action_continue, _action_accum)

    def _action(self, scenario, action):
        l.debug('executing action: ' + str(action.structure))
        return self.action.fire (Actions(action.structure.get_name()), action)


    def register_action_types(observer):
        if not isinstance(observer, ActionObserver):
            raise TypeError

        GstValidate.register_action_type(Actions.CREATE_OFFER.value,
                                        "webrtc", observer._action, None,
                                         "Instruct a create-offer to commence",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.CREATE_ANSWER.value,
                                        "webrtc", observer._action, None,
                                         "Create answer",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.WAIT_FOR_NEGOTIATION_STATE.value,
                                        "webrtc", observer._action, None,
                                         "Wait for a specific negotiation state to be reached",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.ADD_STREAM.value,
                                        "webrtc", observer._action, None,
                                         "Add a stream to the webrtcbin",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.ADD_DATA_CHANNEL.value,
                                        "webrtc", observer._action, None,
                                         "Add a data channel to the webrtcbin",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.SEND_DATA_CHANNEL_STRING.value,
                                        "webrtc", observer._action, None,
                                         "Send a message using a data channel",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.WAIT_FOR_DATA_CHANNEL_STATE.value,
                                        "webrtc", observer._action, None,
                                         "Wait for data channel to reach state",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.CLOSE_DATA_CHANNEL.value,
                                        "webrtc", observer._action, None,
                                         "Close a data channel",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.WAIT_FOR_DATA_CHANNEL.value,
                                        "webrtc", observer._action, None,
                                         "Wait for a data channel to appear",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.WAIT_FOR_DATA_CHANNEL_STRING.value,
                                        "webrtc", observer._action, None,
                                         "Wait for a data channel to receive a message",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.WAIT_FOR_NEGOTIATION_NEEDED.value,
                                        "webrtc", observer._action, None,
                                         "Wait for a the on-negotiation-needed signal to fire",
                                         GstValidate.ActionTypeFlags.NONE)
        GstValidate.register_action_type(Actions.SET_WEBRTC_OPTIONS.value,
                                        "webrtc", observer._action, None,
                                         "Set some webrtc options",
                                         GstValidate.ActionTypeFlags.NONE)
