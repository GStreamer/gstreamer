FROM nginx:latest

COPY . /usr/share/nginx/html

RUN sed -i 's/var default_peer_id;/var default_peer_id = 1;/g' \
    /usr/share/nginx/html/webrtc.js
RUN sed -i 's/wss/ws/g' \
    /usr/share/nginx/html/webrtc.js


