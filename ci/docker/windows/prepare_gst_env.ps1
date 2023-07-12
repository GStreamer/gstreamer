[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

# FIXME: Python fails to validate SSL certificates because of an incorrect
# schannel implementation. Windows downloads CA certs dynamically as required,
# and Python doesn't do the right thing to trigger that. So, add Mozilla's
# certs (via certifi) to the windows cert store manually. See:
# https://bugs.python.org/issue36137
# https://bugs.python.org/issue36011

python -m pip install certifi
$cert_pem = python -m certifi
$plaintext_pw = 'PASSWORD'
$secure_pw = ConvertTo-SecureString $plaintext_pw -AsPlainText -Force
C:\msys64\ucrt64\bin\openssl.exe pkcs12 -export -nokeys -out $env:TEMP\certs.pfx -in $cert_pem -passout pass:$plaintext_pw
Import-PfxCertificate -Password $secure_pw  -CertStoreLocation Cert:\LocalMachine\Root -FilePath $env:TEMP\certs.pfx

Write-Host "Cloning GStreamer"
git clone -b $env:DEFAULT_BRANCH https://gitlab.freedesktop.org/gstreamer/gstreamer.git C:\gstreamer

# download the subprojects to try and cache them
Write-Host "Downloading subprojects"
meson subprojects download --sourcedir C:\gstreamer

Write-Host "Caching subprojects into /subprojects/"
python C:/gstreamer/ci/scripts/handle-subprojects-cache.py --build C:/gstreamer/subprojects
Remove-Item -Recurse -Force C:\gstreamer
