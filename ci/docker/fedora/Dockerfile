FROM registry.fedoraproject.org/fedora:31

ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:$PATH

ARG DEFAULT_BRANCH="main"

COPY prepare.sh cleanup.sh /root/

RUN /usr/bin/sh /root/prepare.sh && \
    /usr/bin/sh /root/cleanup.sh
