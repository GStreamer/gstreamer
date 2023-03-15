set -e
set -x
# Install dotnet-format
apt update -yqq
apt install -y gnupg apt-transport-https
curl https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor > microsoft.asc.gpg
mv microsoft.asc.gpg /etc/apt/trusted.gpg.d/
# FIXME: this is bullseye, but image is actually bookworm (testing at the time)
curl -O https://packages.microsoft.com/config/debian/11/prod.list
mv prod.list /etc/apt/sources.list.d/microsoft-prod.list
chown root:root /etc/apt/trusted.gpg.d/microsoft.asc.gpg
chown root:root /etc/apt/sources.list.d/microsoft-prod.list
apt update -yqq
apt install -y dotnet-sdk-7.0
dotnet tool install --global dotnet-format
ln -s ~/.dotnet/tools/dotnet-format /usr/local/bin/dotnet-format
