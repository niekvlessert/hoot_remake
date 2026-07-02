
	hoot reference version


ABOUT
=====

  This is a source code package of `hoot' (except drivers for the arcade 
games).  It can be compiled with Microsoft Visual C++ 6.0 (VC++).


USING SOURCE CODE
=================

  Because of the copyright issue, source codes of drivers for the arcade 
games are not included in this package.  I don't decide whether I'll 
distribute these drivers or not.

  These source codes are free for non-commercial purpose.  But making a 
derivative version of `hoot' which can play the sound of arcade games is 
not recommended.  The main purpose I make this source code be public is 
to give you information & reference for writing your own sound emulator. 
I hope I'll see your own emulator. :-)


  The source codes of `hoot' are written in C/C++.  And the most of 
codes for sound devices are written by refering to MAME and the other 
emulators.  My original code and structure of this software are quite 
dirty, sorry. :-(


HOW TO BUILD
============

  To build `hoot', you need to get the following software packages.

EMU2413 0.50	http://www.angel.ne.jp/~okazaki/ym2413/
fmgen 006	http://www.remus.dti.ne.jp/~cisc/m88/
MAME 0.54	http://www.mame.net/
RAZE 1.06	http://etc.home.dhs.org/
zlib 1.1.3

  Unpack these packages and copy needed files to the following 
directories.  Which files are needed is described in `hoot.dsp' (VC++ 
project file).

emu2413/*			from EMU2413 0.50
m88/*				from fmgen 006
				patch < opna.cpp.diff
				(modefied for ADPCM channel mask)
mame/cpu/h6280/*		from MAME 0.54 src/cpu/h6280/*
mame/cpu/m6800/*		from MAME 0.54 src/cpu/m6800/*
mame/cpu/m68000/*		from MAME 0.54 src/cpu/m68000/*
				compile & execute m68kmake
mame/cpu/m6809			from MAME 0.54 src/cpu/m6809/*
mame/sound/es5505.*		from MAME 0.54 src/sound/es5505.*
mame/sound/fmopl.*		from MAME 0.54 src/sound/fmopl.*
RAZE/*				from RAZE 1.06
zlib/*				from zlib 1.1.3

  Then open `hoot.dsp' with VC++ and select `hoot - Win32 Release' 
configuration and build it.


CONTACT
=======

  Don't ask me how to get ROM/DISK images and how to play the sound of 
arcade games with `hoot'.  Any other comments and contributes are welcome.

-- 
DMP SOFT. <dmpsoft@geocities.co.jp>
http://dmpsoft.virtualave.net/
