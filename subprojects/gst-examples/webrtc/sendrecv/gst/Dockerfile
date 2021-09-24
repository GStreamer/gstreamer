FROM maxmcd/gstreamer:1.14-buster

RUN apt-get install -y libjson-glib-dev
# RUN apk update
# RUN apk add json-glib-dev libsoup-dev

WORKDIR /opt/
COPY . /opt/

RUN make

CMD echo "Waiting a few seconds for you to open the browser at localhost:8080" \
    && sleep 10 \
    && ./webrtc-sendrecv \
    --peer-id=1 \
    --server=ws://signalling:8443 \
    --disable-ssl

