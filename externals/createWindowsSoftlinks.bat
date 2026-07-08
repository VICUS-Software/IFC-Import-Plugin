j@echo off

goto check_permissions

:create_symlinks
	set lib_list=IBK QtExt IBKMK clipper TiCPP Nandrad Vicus CCM DataIO glm VicOSM VicIFC QuaZIP HiGHS NandradFMUGenerator

	(for %%a in (%lib_list%) do (
	   echo -----------------------------
	   IF NOT EXIST "%~dp0..\SIM-VICUS\externals\%%a" (
		echo Verzeichnis '..\SIM-VICUS\externals\%%a' existiert nicht. Bitte submodules in git-repository pruefen.
		exit /b 1
	   )
	   if exist "%~dp0%%a" (
			echo Loesche vorhandenen Symlink der Bibliothek '%%a'
			:: Try removing as junction first (safe), then as real directory
			rmdir "%~dp0%%a" 2>nul
			if exist "%~dp0%%a" rmdir /s /q "%~dp0%%a"
	   )
	   echo Erzeuge Symlink fuer Bibliothek '%%a'
	   mklink /d "%~dp0%%a" "%~dp0..\SIM-VICUS\externals\%%a"
	))

	:: NandradCodeGenerator symlink (lives at repo root, not in externals)
	echo -----------------------------
	IF NOT EXIST "%~dp0..\SIM-VICUS\NandradCodeGenerator" (
		echo Verzeichnis '..\SIM-VICUS\NandradCodeGenerator' existiert nicht. Bitte submodules in git-repository pruefen.
		exit /b 1
	)
	if exist "%~dp0..\NandradCodeGenerator" (
		echo Loesche vorhandenen Symlink fuer NandradCodeGenerator
		rmdir "%~dp0..\NandradCodeGenerator" 2>nul
		if exist "%~dp0..\NandradCodeGenerator" rmdir /s /q "%~dp0..\NandradCodeGenerator"
	)
	echo Erzeuge Symlink fuer NandradCodeGenerator
	mklink /d "%~dp0..\NandradCodeGenerator" "%~dp0..\SIM-VICUS\NandradCodeGenerator"

exit /b 0

:check_permissions
    echo Adminrechte sind erforderlich!

    net session >nul 2>nul
    if %errorLevel% == 0 (
        echo Adminrechte wurden gewaehrt.
		goto create_symlinks
    ) else (
        echo Keine Adminrechte gefunden. Bitte erneut mit Adminrechten ausfuehren.
		exit /b 1
    )
