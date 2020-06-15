FROM debian:stretch-slim

RUN apt update -yqq \
    && apt install -y curl indent git findutils \
    && rm -R /var/lib/apt/ /var/log/apt/ \
    && curl -L -o /usr/local/bin/gst-indent https://gitlab.freedesktop.org/gstreamer/gstreamer/raw/master/tools/gst-indent \
    && chmod a+x /usr/local/bin/gst-indent
