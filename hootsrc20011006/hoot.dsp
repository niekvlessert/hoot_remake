# Microsoft Developer Studio Project File - Name="hoot" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** 編集しないでください **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=hoot - Win32 Debug
!MESSAGE これは有効なﾒｲｸﾌｧｲﾙではありません。 このﾌﾟﾛｼﾞｪｸﾄをﾋﾞﾙﾄﾞするためには NMAKE を使用してください。
!MESSAGE [ﾒｲｸﾌｧｲﾙのｴｸｽﾎﾟｰﾄ] ｺﾏﾝﾄﾞを使用して実行してください
!MESSAGE 
!MESSAGE NMAKE /f "hoot.mak".
!MESSAGE 
!MESSAGE NMAKE の実行時に構成を指定できます
!MESSAGE ｺﾏﾝﾄﾞ ﾗｲﾝ上でﾏｸﾛの設定を定義します。例:
!MESSAGE 
!MESSAGE NMAKE /f "hoot.mak" CFG="hoot - Win32 Debug"
!MESSAGE 
!MESSAGE 選択可能なﾋﾞﾙﾄﾞ ﾓｰﾄﾞ:
!MESSAGE 
!MESSAGE "hoot - Win32 Release" ("Win32 (x86) Application" 用)
!MESSAGE "hoot - Win32 Debug" ("Win32 (x86) Application" 用)
!MESSAGE "hoot - Win32 Tune" ("Win32 (x86) Application" 用)
!MESSAGE "hoot - Win32 TunePre" ("Win32 (x86) Application" 用)
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "hoot - Win32 Release"

# PROP BASE Use_MFC 6
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_AFXDLL" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MD /W3 /GR /GX /O2 /Ob2 /I ".\\" /I ".\zlib" /I ".\libpng" /I ".\fftw" /I ".\mame\cpu" /D "NDEBUG" /D "_AFXDLL" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D INLINE="static __inline" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x411 /d "NDEBUG" /d "_AFXDLL"
# ADD RSC /l 0x409 /d "NDEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /machine:I386
# ADD LINK32 /nologo /subsystem:windows /machine:I386 /libpath:".\libpng"
# SUBTRACT LINK32 /debug

!ELSEIF  "$(CFG)" == "hoot - Win32 Debug"

# PROP BASE Use_MFC 6
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_AFXDLL" /Yu"stdafx.h" /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GR /GX /ZI /Od /I ".\\" /I ".\zlib" /I ".\libpng" /I ".\fftw" /I ".\mame\cpu" /D "_DEBUG" /D "_AFXDLL" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D INLINE="static __inline" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x411 /d "_DEBUG" /d "_AFXDLL"
# ADD RSC /l 0x409 /d "_DEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 /nologo /subsystem:windows /debug /machine:I386 /nodefaultlib:"libcmt.lib" /pdbtype:sept /libpath:".\zlib" /libpath:".\libpng" /libpath:".\fftw"
# SUBTRACT LINK32 /nodefaultlib

!ELSEIF  "$(CFG)" == "hoot - Win32 Tune"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "hoot___Win32_Tune"
# PROP BASE Intermediate_Dir "hoot___Win32_Tune"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Tune"
# PROP Intermediate_Dir "Tune"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /Zi /O2 /Ob2 /I ".\\" /I ".\zlib" /I ".\libpng" /I ".\fftw" /I ".\mame\cpu" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GR /GX /Zi /O2 /Ob2 /I ".\\" /I ".\zlib" /I ".\libpng" /I ".\fftw" /I ".\mame\cpu" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D INLINE="static __inline" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x411 /d "NDEBUG"
# ADD RSC /l 0x411 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /debug /machine:I386 /libpath:".\zlib" /libpath:".\libpng" /libpath:".\fftw"
# ADD LINK32 /nologo /subsystem:windows /debug /machine:I386 /libpath:".\zlib" /libpath:".\libpng" /libpath:".\fftw"

!ELSEIF  "$(CFG)" == "hoot - Win32 TunePre"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "hoot___Win32_TunePre"
# PROP BASE Intermediate_Dir "hoot___Win32_TunePre"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "TunePre"
# PROP Intermediate_Dir "TunePre"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /Ob2 /I ".\\" /I ".\zlib" /I ".\libpng" /I ".\fftw" /I ".\mame\cpu" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GR /GX /O2 /Ob2 /I ".\\" /I ".\zlib" /I ".\libpng" /I ".\fftw" /I ".\mame\cpu" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D INLINE="static __inline" /YX /FD /P /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x411 /d "NDEBUG"
# ADD RSC /l 0x411 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /machine:I386 /libpath:".\zlib" /libpath:".\libpng" /libpath:".\fftw"
# SUBTRACT BASE LINK32 /debug
# ADD LINK32 /nologo /subsystem:windows /machine:I386 /libpath:".\zlib" /libpath:".\libpng" /libpath:".\fftw"
# SUBTRACT LINK32 /debug

!ENDIF 

# Begin Target

# Name "hoot - Win32 Release"
# Name "hoot - Win32 Debug"
# Name "hoot - Win32 Tune"
# Name "hoot - Win32 TunePre"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\hoot.cpp
# End Source File
# Begin Source File

SOURCE=.\hoot.rc
# End Source File
# Begin Source File

SOURCE=.\MainFrm.cpp
# End Source File
# Begin Source File

SOURCE=.\ssBindConfig.cpp
# End Source File
# Begin Source File

SOURCE=.\ssConfig.cpp
# End Source File
# Begin Source File

SOURCE=.\ssConfigLoader.cpp
# End Source File
# Begin Source File

SOURCE=.\ssDisplay.cpp
# End Source File
# Begin Source File

SOURCE=.\ssDriverBinder.cpp
# End Source File
# Begin Source File

SOURCE=.\ssDriverConfig.cpp
# End Source File
# Begin Source File

SOURCE=.\ssDriverRegister.cpp
# End Source File
# Begin Source File

SOURCE=.\ssFft.cpp
# End Source File
# Begin Source File

SOURCE=.\ssFile.cpp
# End Source File
# Begin Source File

SOURCE=.\ssFolder.cpp
# End Source File
# Begin Source File

SOURCE=.\ssSoundChip.cpp
# End Source File
# Begin Source File

SOURCE=.\ssSoundDriverManager.cpp
# End Source File
# Begin Source File

SOURCE=.\ssSoundStream.cpp
# End Source File
# Begin Source File

SOURCE=.\ssTimer.cpp
# End Source File
# Begin Source File

SOURCE=.\ssTimerManager.cpp
# End Source File
# Begin Source File

SOURCE=.\ssUnZip.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\hoot.h
# End Source File
# Begin Source File

SOURCE=.\MainFrm.h
# End Source File
# Begin Source File

SOURCE=.\Resource.h
# End Source File
# Begin Source File

SOURCE=.\ssBindConfig.h
# End Source File
# Begin Source File

SOURCE=.\ssConfig.h
# End Source File
# Begin Source File

SOURCE=.\ssConfigLoader.h
# End Source File
# Begin Source File

SOURCE=.\ssDisplay.h
# End Source File
# Begin Source File

SOURCE=.\ssDriverBinder.h
# End Source File
# Begin Source File

SOURCE=.\ssDriverConfig.h
# End Source File
# Begin Source File

SOURCE=.\ssDriverDescription.h
# End Source File
# Begin Source File

SOURCE=.\ssDriverRegister.h
# End Source File
# Begin Source File

SOURCE=.\ssFft.h
# End Source File
# Begin Source File

SOURCE=.\ssFile.h
# End Source File
# Begin Source File

SOURCE=.\ssFolder.h
# End Source File
# Begin Source File

SOURCE=.\ssIfDriverConfig.h
# End Source File
# Begin Source File

SOURCE=.\ssIfFolder.h
# End Source File
# Begin Source File

SOURCE=.\ssMutex.h
# End Source File
# Begin Source File

SOURCE=.\ssSoundChip.h
# End Source File
# Begin Source File

SOURCE=.\ssSoundDriver.h
# End Source File
# Begin Source File

SOURCE=.\ssSoundDriverManager.h
# End Source File
# Begin Source File

SOURCE=.\ssSoundStream.h
# End Source File
# Begin Source File

SOURCE=.\ssTimer.h
# End Source File
# Begin Source File

SOURCE=.\ssTimerManager.h
# End Source File
# Begin Source File

SOURCE=.\ssTrackInfo.h
# End Source File
# Begin Source File

SOURCE=.\ssUnZip.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\hoot.bmp
# End Source File
# Begin Source File

SOURCE=.\res\hoot.ico
# End Source File
# Begin Source File

SOURCE=.\res\hoot.rc2
# End Source File
# Begin Source File

SOURCE=.\res\pointer.cur
# End Source File
# End Group
# Begin Group "MAME"

# PROP Default_Filter ""
# Begin Group "cpu"

# PROP Default_Filter ""
# Begin Group "m6809"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\mame\cpu\m6809\cpuintrf.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6809\m6809.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6809\m6809.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6809\mamedbg.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6809\memory.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6809\osd_cpu.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6809\state.h
# End Source File
# End Group
# Begin Group "m6800"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\mame\cpu\m6800\cpuintrf.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6800\m6800.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6800\m6800.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6800\mamedbg.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6800\memory.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6800\osd_cpu.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6800\state.h
# End Source File
# End Group
# Begin Group "m68000"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68000.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68k.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kconf.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kcpu.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kcpu.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kmame.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kmame.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kopac.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kopdm.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kopnz.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kops.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000\m68kops.h
# End Source File
# End Group
# Begin Group "h6280"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\mame\cpu\h6280\h6280.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\h6280\h6280.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\h6280\h6280ops.h
# End Source File
# End Group
# Begin Source File

SOURCE=.\mame\cpu\cpuintrf.c
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\cpuintrf.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\h6280.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6800.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m68000.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\m6809.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\memory.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\osd_cpu.h
# End Source File
# Begin Source File

SOURCE=.\mame\cpu\state.h
# End Source File
# End Group
# Begin Group "snd"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\mame\sound\driver.h
# End Source File
# Begin Source File

SOURCE=.\mame\sound\es5506.c
# End Source File
# Begin Source File

SOURCE=.\mame\sound\es5506.h
# End Source File
# Begin Source File

SOURCE=.\mame\sound\fakemame.cpp
# End Source File
# Begin Source File

SOURCE=.\mame\sound\fmopl.c
# End Source File
# Begin Source File

SOURCE=.\mame\sound\fmopl.h
# End Source File
# End Group
# End Group
# Begin Group "RAZE"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\raze\raze.h
# End Source File
# Begin Source File

SOURCE=.\raze\raze.txt
# End Source File
# Begin Source File

SOURCE=.\raze\raze.obj
# End Source File
# End Group
# Begin Group "sound"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\sound\ss005289.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ss005289.h
# End Source File
# Begin Source File

SOURCE=.\sound\ss007232.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ss007232.h
# End Source File
# Begin Source File

SOURCE=.\sound\ss051649.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ss051649.h
# End Source File
# Begin Source File

SOURCE=.\sound\ss053260.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ss053260.h
# End Source File
# Begin Source File

SOURCE=.\sound\ss054539.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ss054539.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssADPCM.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssADPCM.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssAY8910.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssAY8910.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssC140.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssC140.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssC30.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssC30.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssFMTimer.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssFMTimer.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssMemoryDump.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssMemoryDump.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssMono2151.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssMono2151.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssMSM6258V.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssMSM6258V.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssMSM6295.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssMSM6295.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssPCEPSG.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssPCEPSG.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssPCM8.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssPCM8.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssQSound.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssQSound.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssSEGAPCM.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssSEGAPCM.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssSEGAPCM2.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssSEGAPCM2.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssSN76496.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssSN76496.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssStereo2151.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssStereo2151.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssTaito2610.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssTaito2610.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2151.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2151.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2203.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2203.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2413.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2413.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2608.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2608.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2610.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM2610.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM3438.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM3438.h
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM3812.cpp
# End Source File
# Begin Source File

SOURCE=.\sound\ssYM3812.h
# End Source File
# End Group
# Begin Group "drivers"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\drivers\angelus.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\ds4.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\ed2.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\firehawk.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\hes.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\kss.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\midgarts.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\mistyblue.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\mucom2608.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\mucom88.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\mucomx1.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\mxdrv.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\revolter.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\scc.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\scheme.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\silpheed.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\snatcher.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\starcruiser.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\starship.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\tnmbox.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\trpscr.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\x68k.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\xtalsoft.cpp
# End Source File
# Begin Source File

SOURCE=.\drivers\zavas.cpp
# End Source File
# End Group
# Begin Group "ms"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\ms\ddutil.cpp
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=.\ms\ddutil.h
# End Source File
# End Group
# Begin Group "codeguru"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\codeguru\HyperLink.cpp
# End Source File
# Begin Source File

SOURCE=.\codeguru\HyperLink.h
# End Source File
# End Group
# Begin Group "M88"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\m88\diag.h
# End Source File
# Begin Source File

SOURCE=.\m88\file.cpp
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=.\m88\file.h
# End Source File
# Begin Source File

SOURCE=.\m88\fmgen.cpp
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=.\m88\fmgen.h
# End Source File
# Begin Source File

SOURCE=.\m88\fmgeninl.h
# End Source File
# Begin Source File

SOURCE=.\m88\fmgenx86.h
# End Source File
# Begin Source File

SOURCE=.\m88\fmtimer.cpp
# End Source File
# Begin Source File

SOURCE=.\m88\fmtimer.h
# End Source File
# Begin Source File

SOURCE=.\m88\headers.h
# End Source File
# Begin Source File

SOURCE=.\m88\misc.h
# End Source File
# Begin Source File

SOURCE=.\m88\opm.cpp
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=.\m88\opm.h
# End Source File
# Begin Source File

SOURCE=.\m88\opna.cpp
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=.\m88\opna.h
# End Source File
# Begin Source File

SOURCE=.\m88\psg.cpp
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=.\m88\psg.h
# End Source File
# Begin Source File

SOURCE=.\m88\types.h
# End Source File
# End Group
# Begin Group "emu2413"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\emu2413\emu2413.c
# SUBTRACT CPP /D INLINE="static __inline"
# End Source File
# Begin Source File

SOURCE=.\emu2413\emu2413.h
# End Source File
# Begin Source File

SOURCE=.\emu2413\fmtone.h
# End Source File
# End Group
# Begin Group "zlib"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\zlib\adler32.c
# End Source File
# Begin Source File

SOURCE=.\zlib\compress.c
# End Source File
# Begin Source File

SOURCE=.\zlib\crc32.c
# End Source File
# Begin Source File

SOURCE=.\zlib\deflate.c
# End Source File
# Begin Source File

SOURCE=.\zlib\deflate.h
# End Source File
# Begin Source File

SOURCE=.\zlib\infblock.c
# End Source File
# Begin Source File

SOURCE=.\zlib\infblock.h
# End Source File
# Begin Source File

SOURCE=.\zlib\infcodes.c
# End Source File
# Begin Source File

SOURCE=.\zlib\infcodes.h
# End Source File
# Begin Source File

SOURCE=.\zlib\inffast.c
# End Source File
# Begin Source File

SOURCE=.\zlib\inffast.h
# End Source File
# Begin Source File

SOURCE=.\zlib\inffixed.h
# End Source File
# Begin Source File

SOURCE=.\zlib\inflate.c
# End Source File
# Begin Source File

SOURCE=.\zlib\inftrees.c
# End Source File
# Begin Source File

SOURCE=.\zlib\inftrees.h
# End Source File
# Begin Source File

SOURCE=.\zlib\infutil.c
# End Source File
# Begin Source File

SOURCE=.\zlib\infutil.h
# End Source File
# Begin Source File

SOURCE=.\zlib\maketree.c
# End Source File
# Begin Source File

SOURCE=.\zlib\trees.c
# End Source File
# Begin Source File

SOURCE=.\zlib\trees.h
# End Source File
# Begin Source File

SOURCE=.\zlib\uncompr.c
# End Source File
# Begin Source File

SOURCE=.\zlib\unzip.c
# End Source File
# Begin Source File

SOURCE=.\zlib\unzip.h
# End Source File
# Begin Source File

SOURCE=.\zlib\zconf.h
# End Source File
# Begin Source File

SOURCE=.\zlib\zlib.h
# End Source File
# Begin Source File

SOURCE=.\zlib\zutil.c
# End Source File
# Begin Source File

SOURCE=.\zlib\zutil.h
# End Source File
# End Group
# Begin Source File

SOURCE=.\hoot.ini
# End Source File
# Begin Source File

SOURCE=.\hoot.txt
# End Source File
# Begin Source File

SOURCE=.\hootdev.ini
# End Source File
# Begin Source File

SOURCE=.\ReadMe.txt
# End Source File
# End Target
# End Project
