@REM The MIT License (MIT)
@REM Copyright (c) Microsoft Corporation

@REM Permission is hereby granted, free of charge, to any person obtaining a copy of this software and 
@REM associated documentation files (the "Software"), to deal in the Software without restriction, 
@REM including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
@REM and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
@REM subject to the following conditions:

@REM The above copyright notice and this permission notice shall be included in all copies or substantial 
@REM portions of the Software.

@REM THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT 
@REM NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
@REM IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
@REM WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
@REM SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

@if not defined _echo echo off
setlocal enabledelayedexpansion

call %*
if "%ERRORLEVEL%"=="3010" (
    exit /b 0
) else (
    if not "%ERRORLEVEL%"=="0" (
        set ERR=%ERRORLEVEL%
        if "%CI_PROJECT_PATH%"=="" (
                call C:\TEMP\collect.exe -zip:C:\vslogs.zip
        ) else (
                call C:\TEMP\collect.exe -zip:%CI_PROJECT_PATH%\vslogs.zip
        )
        exit /b !ERR!
    )
)
