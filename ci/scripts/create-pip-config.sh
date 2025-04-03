# We are currently running pip as root which will cause it
# to install into /root/.local cause it wants to do a user install
# Set the user-base in the gloabl config, /etc/pip.conf, so pip will
# both install there and subsequently look there if we invoke it
# from any other user.
# This makes pip install ofc require elevated permissions, as it
# will be writting into /usr/local from now on
python3 -m pip config --global set global.user-base /usr/local/
# Disable the cache as we'd be removing it at the end of the image build anyway
python3 -m pip config --global set global.no-cache-dir true
