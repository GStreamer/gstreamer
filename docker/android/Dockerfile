FROM registry.fedoraproject.org/fedora:29

ENV ANDROID_HOME=/android/sdk
ENV ANDROID_NDK_HOME=/android/ndk

COPY prepare.sh cleanup.sh /root/

RUN /usr/bin/sh /root/prepare.sh && \
    /usr/bin/sh /root/cleanup.sh
