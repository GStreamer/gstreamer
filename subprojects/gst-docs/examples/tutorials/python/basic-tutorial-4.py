#!/usr/bin/env python3
import logging
import sys

import gi

gi.require_version("Gst", "1.0")

from gi.repository import Gst

logging.basicConfig(level=logging.DEBUG, format="[%(name)s] [%(levelname)8s] - %(message)s")
logger = logging.getLogger(__name__)


class Tutorial4:
    def __init__(self):
        self.playing = False
        self.terminate = False
        self.seek_enabled = False
        self.seek_done = False
        self.duration = Gst.CLOCK_TIME_NONE

        # Initialize GStreamer
        Gst.init(sys.argv[1:])

        # Create the elements
        self.playbin = Gst.ElementFactory.make("playbin", "playbin")

        if not self.playbin:
            logger.error("Not all elements could be created.")
            sys.exit(1)

        # Set the URI to play
        self.playbin.set_property("uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm")

        # Start playing
        ret = self.playbin.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            print("Unable to set the pipeline to the playing state.")
            sys.exit(1)

        # Listen to the bus
        bus = self.playbin.get_bus()
        while not self.terminate:
            msg = bus.timed_pop_filtered(100 * Gst.MSECOND,
                    Gst.MessageType.STATE_CHANGED | Gst.MessageType.ERROR
                    | Gst.MessageType.EOS | Gst.MessageType.DURATION_CHANGED)

            # Parse message
            if msg:
                self.handle_message(msg)
            else:
                if self.playing:
                    current = -1

                    # Query the current position of the stream
                    ok, current = self.playbin.query_position(Gst.Format.TIME)
                    if not ok:
                        logger.error("Could not query current position.")

                    # If we didn't know it yet, query the stream duration
                    if self.duration == Gst.CLOCK_TIME_NONE:
                        ok, self.duration = self.playbin.query_duration(Gst.Format.TIME)
                        if not ok:
                            logger.error("Could not query current duration.")

                    # Print current position and total duration
                    print(f"Position {current} / {self.duration}\r")

                    # If seeking is enabled, we have not done it yet, and the time is right, seek
                    if self.seek_enabled and not seek_done and current > 10 * Gst.SECOND:
                        print("\nReached 10s, performing seek...")
                        self.playbin.seek_simple(Gst.Format.TIME,
                            Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT, 30 * Gst.SECOND)
                        seek_done = True

        self.playbin.set_state(Gst.State.NULL)

    def handle_message(self, msg: Gst.Message):
        if msg.type == Gst.MessageType.ERROR:
            err, debug_info = msg.parse_error()
            logger.error(f"Error received from element {msg.src.get_name()}: {err.message}")
            logger.error(f"Debugging information: {debug_info if debug_info else 'none'}")
            self.terminate = True
        elif msg.type == Gst.MessageType.EOS:
            logger.info("End-Of-Stream reached.")
            self.terminate = True
        elif msg.type == Gst.MessageType.DURATION_CHANGED:
            # The duration has changed, mark the current one as invalid
            self.duration = Gst.CLOCK_TIME_NONE
        elif msg.type == Gst.MessageType.STATE_CHANGED:
            # We are only interested in state-changed messages from the pipeline
            if msg.src == self.playbin:
                old_state, new_state, self.pending_state = msg.parse_state_changed()
                logger.info("Pipeline state changed from %s to %s", old_state, new_state)
                # Remember whether we are in the PLAYING state or not
                playing = new_state == Gst.State.PLAYING
                if playing:
                    query = Gst.Query.new_seeking(Gst.Format.TIME)
                    if self.playbin.query(query):
                        f, seek_enabled, start, end = query.parse_seeking()
                        if seek_enabled:
                            logger.info(f"Seeking is ENABLED from {start} to {end}")
                        else:
                            logger.info("Seeking is DISABLED for this stream.")
                    else:
                        logger.error("Seeking query failed.")
        else:
            # We should not reach here
            logger.error("Unexpected message received.")


if __name__ == "__main__":
    Tutorial4()
