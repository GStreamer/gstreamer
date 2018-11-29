set -eu

echo "Removing apt cache"
rm -R /root/*
rm -R /var/lib/apt/ /var/log/apt/
