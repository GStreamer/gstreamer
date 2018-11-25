# escape=`

FROM 'mcr.microsoft.com/windows/servercore:1607'

# Make sure any failure in PowerShell scripts is fatal
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop';"]

COPY prepare.ps1 cleanup.ps1 C:\
RUN C:\prepare.ps1 ; C:\cleanup.ps1
