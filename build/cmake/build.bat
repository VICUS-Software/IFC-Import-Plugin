@echo off

:: Modify environment variables only within this batch file - changes are not persistent after leaving batch
:: This allows calling this batch file several times within the same command window.
setlocal

:: setup VC environment variables - check multiple possible locations
set VC_FOUND=0

:: Check BuildTools (x86 path)
set VC_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist %VC_PATH% (
	echo Loading VCVars64 2022 BuildTools
	call %VC_PATH%
	set VC_FOUND=1
)

:: Check Community
if %VC_FOUND%==0 (
	set VC_PATH="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
	if exist %VC_PATH% (
		echo Loading VCVars64 2022 Community
		call %VC_PATH%
		set VC_FOUND=1
	)
)

:: Check Professional
if %VC_FOUND%==0 (
	set VC_PATH="C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
	if exist %VC_PATH% (
		echo Loading VCVars64 2022 Professional
		call %VC_PATH%
		set VC_FOUND=1
	)
)

:: Check Enterprise
if %VC_FOUND%==0 (
	set VC_PATH="C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
	if exist %VC_PATH% (
		echo Loading VCVars64 2022 Enterprise
		call %VC_PATH%
		set VC_FOUND=1
	)
)

if %VC_FOUND%==0 (
	echo ERROR: Could not find Visual Studio 2022 installation
	echo Checked: BuildTools, Community, Professional, Enterprise
	exit /b 1
)


:: Select Qt version
set QT_ROOT=C:\Qt\6.9.3\msvc2022_64

:: Wichtig für find_package(Qt6 ...)
set CMAKE_PREFIX_PATH=%QT_ROOT%;%VCPKG_ROOT%\installed\x64-windows
set Qt6_DIR=%QT_ROOT%\lib\cmake\Qt6

:: add search path for jom.exe
set PATH=%PATH%;%JOM_PATH%

:: create and change into build subdir
mkdir bb_VC_x64
pushd bb_VC_x64

:: configure makefiles and build
cmake -G "NMake Makefiles JOM" ../../.. -DCMAKE_BUILD_TYPE:String="Release"
jom
if ERRORLEVEL 1 GOTO fail

popd

:: copy executable to bin/release dir
:: (create the target dir first and use a trailing backslash + /I so xcopy
::  treats the destination as a directory instead of prompting
::  "F = file or D = directory?" — that prompt hangs/fails in CI, leaving
::  release empty and the artifact upload with nothing to upload)
if not exist ..\..\bin\release mkdir ..\..\bin\release
xcopy /Y /I .\bb_VC_x64\IFC2BESTest\IFC2BESTest.exe ..\..\bin\release\
xcopy /Y /I .\bb_VC_x64\ImportIFCPlugin\ImportIFCPlugin.dll ..\..\bin\release\

exit /b 0


:fail
echo ** Build Failed **
exit /b 1
