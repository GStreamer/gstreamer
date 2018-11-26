FROM ubuntu:18.04

COPY prepare.sh cleanup.sh android-download-ndk.sh android-create-toolchain.sh /root/

ENV DEBIAN_FRONTEND=noninteractive

RUN sh /root/prepare.sh && \
    sh /root/cleanup.sh
