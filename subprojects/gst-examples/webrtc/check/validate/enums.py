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

from enum import Enum, unique

@unique
class SignallingState(Enum):
    """
    State of the signalling protocol.
    """
    NEW = "new"                 # no connection has been made
    ERROR = "error"             # an error was thrown.  overrides all others
    OPEN = "open"               # websocket connection is open
    HELLO = "hello"             # hello was sent and received
    SESSION = "session"         # session setup was sent and received

@unique
class NegotiationState(Enum):
    """
    State of the webrtc negotiation.  Both peers have separate states and are
    tracked separately.
    """
    NEW = "new"                                 # No negotiation has been performed
    ERROR = "error"                             # an error occured
    OFFER_CREATED = "offer-created"             # offer was created
    ANSWER_CREATED = "answer-created"           # answer was created
    OFFER_SET = "offer-set"                     # offer has been set
    ANSWER_SET = "answer-set"                   # answer has been set

@unique
class DataChannelState(Enum):
    """
    State of a data channel.  Each data channel is tracked individually
    """
    NEW = "new"                 # data channel created but not connected
    OPEN = "open"               # data channel is open, data can flow
    CLOSED = "closed"           # data channel is closed, sending data will fail
    ERROR = "error"             # data channel encountered an error

@unique
class Actions(Enum):
    """
    Action names that we implement.  Each name is the structure name for each
    action as stored in the scenario file.
    """
    CREATE_OFFER = "create-offer"                                   # create an offer and send it to the peer
    CREATE_ANSWER = "create-answer"                                 # create an answer and send it to the peer
    WAIT_FOR_NEGOTIATION_STATE = "wait-for-negotiation-state"       # wait for the @NegotiationState to reach a certain value
    ADD_STREAM = "add-stream"                                       # add a stream to send to the peer. local only
    ADD_DATA_CHANNEL = "add-data-channel"                           # add a stream to send to the peer. local only
    WAIT_FOR_DATA_CHANNEL = "wait-for-data-channel"                 # wait for a data channel to appear
    WAIT_FOR_DATA_CHANNEL_STATE = "wait-for-data-channel-state"     # wait for a data channel to have a certain state
    SEND_DATA_CHANNEL_STRING = "send-data-channel-string"           # send a string over the data channel
    WAIT_FOR_DATA_CHANNEL_STRING = "wait-for-data-channel-string"   # wait for a string on the data channel
    CLOSE_DATA_CHANNEL = "close-data-channel"                       # close a data channel
    WAIT_FOR_NEGOTIATION_NEEDED = "wait-for-negotiation-needed"     # wait for negotiation needed to fire
    SET_WEBRTC_OPTIONS = "set-webrtc-options"                       # set some options
