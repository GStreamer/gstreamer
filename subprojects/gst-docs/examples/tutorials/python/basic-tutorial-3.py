#!/usr/bin/env python3
import logging
import sys

import gi

gi.require_version("GLib", "2.0")

from gi.repository import Gst

logging.basicConfig(level=logging.DEBUG, format="[%(name)s] [%(levelname)8s] - %(message)s")
logger = logging.getLogger(__name__)


def pad_added_handler(src, new_pad):
    sink_pad = convert.get_static_pad("sink")
    logger.info(f"Received new pad '{new_pad.name}' from '{src.name}'.")

    # If our converter is already linked, we have nothing to do here
    if sink_pad.is_linked():
        logger.info("We are already linked. Ignoring.")
        sys.exit()

    # Check the new pad's type
    new_pad_caps = new_pad.get_current_caps()
    new_pad_struct = new_pad_caps.get_structure(0)
    new_pad_type = new_pad_struct.get_name()
    if not new_pad_type.startswith("audio/x-raw"):
        logger.info(f"It has type '{new_pad_type}' which is not raw audio. Ignoring.")
        return

    # Attempt the link
    ret = new_pad.link(sink_pad)
    if ret != Gst.PadLinkReturn.OK:
        logger.error(f"Type is '{new_pad_type}' but link failed.")
    else:
        logger.info(f"Link succeeded (type '{new_pad_type}').")


# Initialize GStreamer
Gst.init(sys.argv[1:])

# Create the elements
source = Gst.ElementFactory.make("uridecodebin", "source")
convert = Gst.ElementFactory.make("audioconvert", "convert")
resample = Gst.ElementFactory.make("audioresample", "resample")
sink = Gst.ElementFactory.make("autoaudiosink", "sink")

# Create the empty pipeline
pipeline = Gst.Pipeline.new("test-pipeline")

if not pipeline or not source or not convert or not resample or not sink:
    logger.error("Not all elements could be created.")
    sys.exit(1)

# Build the pipeline. Note that we are NOT linking the source at this
# point. We will do it later.
pipeline.add(source, convert, resample, sink)
if not convert.link(resample) or not resample.link(sink):
    logger.error("Elements could not be linked.")
    sys.exit(1)

# Set the URI to play
source.set_property("uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm")

# Connect to the pad-added signal
source.connect("pad-added", pad_added_handler)

# Start playing
ret = pipeline.set_state(Gst.State.PLAYING)
if ret == Gst.StateChangeReturn.FAILURE:
    logger.error("Unable to set the pipeline to the playing state.")
    sys.exit(1)

# Listen to the bus
bus = pipeline.get_bus()
terminate = False
while not terminate:
    msg = bus.timed_pop_filtered(1 * Gst.SECOND, Gst.MessageType.STATE_CHANGED | Gst.MessageType.ERROR | Gst.MessageType.EOS)

    # Parse message
    if msg:
        if msg.type == Gst.MessageType.ERROR:
            err, debug_info = msg.parse_error()
            logger.error(f"Error received from element {msg.src.get_name()}: {err.message}")
            logger.error(f"Debugging information: {debug_info if debug_info else 'none'}")
            terminate = True
        elif msg.type == Gst.MessageType.EOS:
            logger.info("End-Of-Stream reached.")
            terminate = True
        elif msg.type == Gst.MessageType.STATE_CHANGED:
          # We are only interested in state-changed messages from the pipeline
          if msg.src == pipeline:
            old_state, new_state, pending_state = msg.parse_state_changed()
            logger.info("Pipeline state changed from", old_state, "to", new_state)
        else:
            # We should not reach here
            logger.error("Unexpected message received.")

pipeline.set_state(Gst.State.NULL)
