FROM debian:stretch-slim

RUN apt update -yqq \
    && apt install -y curl indent git findutils \
    && rm -R /var/lib/apt/ /var/log/apt/