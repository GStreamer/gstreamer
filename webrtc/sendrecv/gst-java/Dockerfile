# START BUILD PHASE
FROM gradle:5.1.1-jdk11 as builder
WORKDIR /home/gradle/work
COPY . /home/gradle/work/
USER root
RUN chown -R gradle:gradle /home/gradle/work
USER gradle
RUN gradle build
# END BUILD PHASE

FROM openjdk:10

# GStreamer dependencies
USER root
RUN apt-get update &&\
  apt-get install -yq \
  libgstreamer1.0-0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
  gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa \
  gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-pulseaudio gstreamer1.0-nice

# Seems to be a problem with GStreamer and lastest openssl in debian buster, so rolling back to working version
# https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/811
RUN curl -SL http://security-cdn.debian.org/debian-security/pool/updates/main/o/openssl/openssl_1.1.0j-1~deb9u1_amd64.deb -o openssl.deb && \
    dpkg -i openssl.deb

COPY --from=builder /home/gradle/work/build/libs/work.jar /gst-java.jar

CMD echo "Waiting a few seconds for you to open the browser at localhost:8080" \
    && sleep 10 \
    && java -jar /gst-java.jar \
    --peer-id=1 \
    --server=ws://signalling:8443



