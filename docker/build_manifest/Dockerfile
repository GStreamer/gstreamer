FROM  python:3.7.1-alpine

ARG DEFAULT_BRANCH="master"

RUN pip install requests
RUN apk add git
RUN cd / && git clone -b ${DEFAULT_BRANCH:=master} https://gitlab.freedesktop.org/gstreamer/gst-ci.git
