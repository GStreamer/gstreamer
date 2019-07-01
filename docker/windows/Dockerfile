# escape=`

FROM 'mcr.microsoft.com/windows/servercore:1607'

# Make sure any failure in PowerShell scripts is fatal
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop';"]

COPY install_toolchain.ps1 prepare_gst_env.ps1 C:\

RUN C:\install_toolchain.ps1

RUN C:\prepare_gst_env.ps1