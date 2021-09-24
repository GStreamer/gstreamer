FROM registry.fedoraproject.org/fedora:31

RUN dnf install -y \
    python3-pytest \
    python3-requests \
    python3-pytest-cov \
    git-core \
    && rm -rf /var/cache/dnf /var/log/dnf*
