@echo off
rem This BAT file updates the ZIP file that is to be uploaded to www.zezula.net
rem Only use when both 32-bit and 64-bit are properly compiled

echo Creating stormlib.zip ...
cd \Ladik\Appdir
zip.exe -r9 ..\WWW\www.zezula.net\download\stormlib.zip StormLib\doc\*
zip.exe -r9 ..\WWW\www.zezula.net\download\stormlib.zip StormLib\src\*
zip.exe -r9 ..\WWW\www.zezula.net\download\stormlib.zip StormLib\storm_dll\*
zip.exe -r9 ..\WWW\www.zezula.net\download\stormlib.zip StormLib\StormLib.xcodeproj\*
zip.exe -r9 ..\WWW\www.zezula.net\download\stormlib.zip StormLib\stormlib_dll\*
zip.exe -r9 ..\WWW\www.zezula.net\download\stormlib.zip StormLib\test\*
zip.exe -9  ..\WWW\www.zezula.net\download\stormlib.zip StormLib\makefile*
zip.exe -9  ..\WWW\www.zezula.net\download\stormlib.zip StormLib\*.bat
zip.exe -9  ..\WWW\www.zezula.net\download\stormlib.zip StormLib\*.sln
zip.exe -9  ..\WWW\www.zezula.net\download\stormlib.zip StormLib\*.vcproj
zip.exe -9  ..\WWW\www.zezula.net\download\stormlib.zip StormLib\*.plist
echo.

echo Press any key to exit ...
pause >nul
