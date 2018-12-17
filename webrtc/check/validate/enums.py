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

class SignallingState(object):
    NEW = "new"                 # no connection has been made
    OPEN = "open"               # websocket connection is open
    ERROR = "error"             # and error was thrown.  overrides all others
    HELLO = "hello"             # hello was sent and received
    SESSION = "session"         # session setup was sent and received

class NegotiationState(object):
    NEW = "new"
    ERROR = "error"
    NEGOTIATION_NEEDED = "negotiation-needed"
    OFFER_CREATED = "offer-created"
    ANSWER_CREATED = "answer-created"
    OFFER_SET = "offer-set"
    ANSWER_SET = "answer-set"

class RemoteState(object):
    ERROR = "error"
    REMOTE_STREAM_RECEIVED = "remote-stream-received"
