#CONFIG += debug
CONFIG += release

PREFIX = /usr/local

LIBVERSION = 0.13

INCLUDEPATH += $${PREFIX}/include
INCLUDEPATH *= /usr/local/include

DEFINES += _GNU_SOURCE
DEFINES += _FILE_OFFSET_BITS=64
DEFINES += PREFIX=\"$${PREFIX}\"

release {
        DEFINES += MMX
        QMAKE_CXXFLAGS_RELEASE = -O3 -march=pentiumpro -fomit-frame-pointer
        QMAKE_CFLAGS_RELEASE = $${QMAKE_CXXFLAGS_RELEASE}
}

EXTRA_LIBS = -L/usr/X11R6/lib -lXinerama -lXv -lX11 -lXext -lXxf86vm -lfreetype

# LCDProc support
#DEFINES += LCD_DEVICE

# Native ALSA support
#CONFIG += using_alsa
#ALSA_LIBS = -lasound

# DVB support
#CONFIG += using_dvb
#DEFINES += USING_DVB
#INCLUDEPATH += /usr/src/linux/include/linux/dvb/

# Native lirc support
#CONFIG += using_lirc
#LIRC_LIBS = -llirc_client

# XvMC support, modify as necessary.
#CONFIG += using_xvmc
#EXTRA_LIBS += -lXvMCNVIDIA -lXvMC

# VIA cle266 support
#CONFIG += using_viahwslice
#EXTRA_LIBS += -lddmpeg

# DirectFB support
#CONFIG += using_directfb
#EXTRA_LIBS = `directfb-config --libs` -lfreetype
#QMAKE_CXXFLAGS += `directfb-config --cflags`

