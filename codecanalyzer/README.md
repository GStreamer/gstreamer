codecanalyzer
=============

An analyzer for doing in-depth analysis on compressed media.
It is built on top of gstreamer, gtk+ and libxml2.
The goal of the codecanalyzer is to support the follwoing
features:

-- unpack the elementary stream from a container
-- do packetization for the non-packetized stream
-- Parse all the syntax elements from the elementary video stream
-- A simple UI to navigate through all the headers of each frame separately
-- Users would be able to analyze the media files residing in the local machine
and the remote streams via http or rtp.

Supported codecs:
-- mpeg2
