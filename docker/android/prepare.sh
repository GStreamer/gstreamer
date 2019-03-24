set -eu

dnf install -y \
    file \
    git-core \
    java-1.8.0-openjdk-devel \
    lbzip2 \
    make \
    pkg-config \
    unzip \
    which

mkdir -p /android/sources

curl -o /android/sources/android-ndk.zip https://dl.google.com/android/repository/android-ndk-r18b-linux-x86_64.zip
unzip /android/sources/android-ndk.zip -d ${ANDROID_NDK_HOME}/
# remove the intermediate versioned directory
mv ${ANDROID_NDK_HOME}/*/* ${ANDROID_NDK_HOME}/

curl -o /android/sources/android-sdk-tools.zip https://dl.google.com/android/repository/sdk-tools-linux-4333796.zip
unzip /android/sources/android-sdk-tools.zip -d ${ANDROID_HOME}/
mkdir -p ${ANDROID_HOME}/licenses

rm -rf /android/sources

# Accept licenses. Values taken from:
# ANDROID_HOME=/path/to/android/sdk-tools $ANDROID_HOME/tools/bin/sdkmanager --licenses
echo "601085b94cd77f0b54ff86406957099ebe79c4d6" > ${ANDROID_HOME}/licenses/android-googletv-license
echo "24333f8a63b6825ea9c5514f83c2829b004d1fee" > ${ANDROID_HOME}/licenses/android-sdk-license
echo "84831b9409646a918e30573bab4c9c91346d8abd" > ${ANDROID_HOME}/licenses/android-sdk-preview-license
echo "33b6a2b64607f11b759f320ef9dff4ae5c47d97a" > ${ANDROID_HOME}/licenses/google-gdk-license
echo "e9acab5b5fbb560a72cfaecce8946896ff6aab9d" > ${ANDROID_HOME}/licenses/mips-android-sysimage-license
