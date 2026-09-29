@echo off
set QTDIR=C:\Qt\5.15.2\msvc2019_64
set PATH=c:\Qt\5.15.2\msvc2019_64\bin;%PATH%

:: setup VC environment variables
set VCVARSALL_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARSALL_PATH%

:: IFCConvert_de.ts is merged in: the plugin only installs ImportIFCPlugin_de.qm
lrelease ImportIFCPlugin_de.ts ..\..\..\IFCConvert\resources\translations\IFCConvert_de.ts -qm ImportIFCPlugin_de.qm

pause
