set -eux

echo "Removing DNF cache"
dnf clean all

rm -R /root/*
rm -rf /var/cache/dnf /var/log/dnf*
