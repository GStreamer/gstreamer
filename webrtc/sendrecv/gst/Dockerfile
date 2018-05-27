FROM maxmcd/gstreamer:1.14-buster

RUN apt-get install -y libjson-glib-dev
# RUN apk update
# RUN apk add json-glib-dev libsoup-dev

WORKDIR /opt/
COPY . /opt/

RUN sed -i 's/wss:\/\/webrtc.nirbheek.in:8443/ws:\/\/signalling:8443/g' \
    /opt/webrtc-sendrecv.c
RUN sed -i 's/strict_ssl = TRUE/strict_ssl = FALSE/g' \
    /opt/webrtc-sendrecv.c

RUN make

CMD echo "Waiting a few seconds for you to open the browser at localhost:8080" \
    && sleep 10 \
    && ./webrtc-sendrecv --peer-id=1

