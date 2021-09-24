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

import logging
import threading

from enums import NegotiationState, DataChannelState

l = logging.getLogger(__name__)

class Signal(object):
    """
    A class for callback-based signal handlers.
    """
    def __init__(self, cont_func=None, accum_func=None):
        self._handlers = []
        if not cont_func:
            # by default continue when None/no return value is provided or
            # True is returned
            cont_func = lambda x: x is None or x
        self.cont_func = cont_func
        # default to accumulating truths
        if not accum_func:
            accum_func = lambda prev, v: prev and v
        self.accum_func = accum_func

    def connect(self, handler):
        self._handlers.append(handler)

    def disconnect(self, handler):
        self._handlers.remove(handler)

    def fire(self, *args):
        ret = None
        for handler in self._handlers:
            ret = self.accum_func(ret, handler(*args))
            if not self.cont_func(ret):
                break
        return ret


class StateObserver(object):
    """
    Observe some state.  Allows waiting for specific states to occur and
    notifying waiters of updated values.  Will hold previous states to ensure
    @update cannot change the state before @wait_for can look at the state.
    """
    def __init__(self, target, attr_name, cond):
        self.target = target
        self.attr_name = attr_name
        self.cond = cond
        # track previous states of the value so that the notification still
        # occurs even if the field has moved on to another state
        self.previous_states = []

    def wait_for(self, states):
        ret = None
        with self.cond:
            state = getattr (self.target, self.attr_name)
            l.debug (str(self.target) + " \'" + self.attr_name +
                    "\' waiting for " + str(states))
            while True:
                l.debug(str(self.target) + " \'" + self.attr_name +
                        "\' previous states: " + str(self.previous_states))
                for i, s in enumerate (self.previous_states):
                    if s in states:
                        l.debug(str(self.target) + " \'" + self.attr_name +
                                "\' " + str(s) + " found at position " +
                                str(i) + " of " + str(self.previous_states))
                        self.previous_states = self.previous_states[i:]
                        return s
                self.cond.wait()

    def update (self, new_state):
        with self.cond:
            self.previous_states += [new_state]
            setattr(self.target, self.attr_name, new_state)
            self.cond.notify_all()
            l.debug (str(self.target) + " updated \'" + self.attr_name + "\' to " + str(new_state))


class WebRTCObserver(object):
    """
    Base webrtc observer class.  Stores a lot of duplicated functionality
    between the local and remove peer implementations.
    """
    def __init__(self):
        self.state = NegotiationState.NEW
        self._state_observer = StateObserver(self, "state", threading.Condition())
        self.on_offer_created = Signal()
        self.on_answer_created = Signal()
        self.on_offer_set = Signal()
        self.on_answer_set = Signal()
        self.on_data_channel = Signal()
        self.data_channels = []
        self._xxxxxxxdata_channel_ids = None
        self._data_channels_observer = StateObserver(self, "_xxxxxxxdata_channel_ids", threading.Condition())

    def _update_negotiation_state(self, new_state):
        self._state_observer.update (new_state)

    def wait_for_negotiation_states(self, states):
        return self._state_observer.wait_for (states)

    def find_channel (self, ident):
        for c in self.data_channels:
            if c.ident == ident:
                return c

    def add_channel (self, channel):
        l.debug('adding channel ' + str (channel) + ' with name ' + str(channel.ident))
        self.data_channels.append (channel)
        self._data_channels_observer.update (channel.ident)
        self.on_data_channel.fire(channel)

    def wait_for_data_channel(self, ident):
        return self._data_channels_observer.wait_for (ident)

    def create_offer(self, options):
        raise NotImplementedError()

    def add_data_channel(self, ident):
        raise NotImplementedError()


class DataChannelObserver(object):
    """
    Base webrtc data channelobserver class.  Stores a lot of duplicated
    functionality between the local and remove peer implementations.
    """
    def __init__(self, ident, location):
        self.state = DataChannelState.NEW
        self._state_observer = StateObserver(self, "state", threading.Condition())
        self.ident = ident
        self.location = location
        self.message = None
        self._message_observer = StateObserver(self, "message", threading.Condition())

    def _update_state(self, new_state):
        self._state_observer.update (new_state)

    def wait_for_states(self, states):
        return self._state_observer.wait_for (states)

    def wait_for_message (self, msg):
        return self._message_observer.wait_for (msg)

    def got_message(self, msg):
        self._message_observer.update (msg)

    def close (self):
        raise NotImplementedError()

    def send_string (self, msg):
        raise NotImplementedError()
