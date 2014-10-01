echo off

set HEADER="..\include\COLLADAMaxVersionInfo.h"

set F="$(IntDir)COLLADAMaxVersionInfo.h"

echo #ifndef __COLLADAMAX_VERSIONINFO_H__ > %F%
echo #define __COLLADAMAX_VERSIONINFO_H__ >> %F%

echo.  >> %F%

echo #include "COLLADAMaxPrerequisites.h"   >> %F%

echo.  >> %F%

echo namespace COLLADAMax >> %F%

echo { >> %F%


FOR /f  %%A in ('git describe  --always') do set REVISION=%%A


echo     const String CURRENT_REVISION = "%REVISION%"; >> %F%
echo     const String CURRENT_PLATFORM = "$(Platform)"; >> %F%
echo     const String CURRENT_CONFIGURATION = "$(Configuration)"; >> %F%

echo } >> %F%

echo.  >> %F%

echo #endif // __COLLADAMAX_VERSIONINFO_H__ >> %F%


if exist %HEADER%  (
fc %HEADER% %F%    > nul
if NOT ERRORLEVEL 1 EXIT /B 0
)


echo Generating %HEADER%

copy %F%  %HEADER%


