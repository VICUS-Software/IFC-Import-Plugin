# Project file for ImportIFCPlugin
#
# remember to set DYLD_FALLBACK_LIBRARY_PATH on MacOSX
# set LD_LIBRARY_PATH on Linux

TARGET = ImportIFCPlugin

QT += gui
greaterThan(QT_MAJOR_VERSION, 4):  QT += widgets

TEMPLATE = lib
CONFIG += plugin
CONFIG += shared

CONFIG += c++17

# Required because we pull in ifcplusplus/glm headers that use stock GLM's guarded
# gtx extensions (see IFCConvert.pro for details).
DEFINES += GLM_ENABLE_EXPERIMENTAL

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# check if 32 or 64 bit version and set prefix variable for using in output paths
contains(QT_ARCH, i386): {
	DIR_PREFIX =
} else {
	DIR_PREFIX = _x64
}

INCLUDEPATH = \
	src \
	../../externals/IBK/src \
	../../externals/IBKMK/src \
	../../externals/TiCPP/src \
	../../externals/QtExt/src \
	../ifcplusplus/src/IfcPlusPlus/src/ifcpp/IFC4X3/include \
	../ifcplusplus/src/IfcPlusPlus/src/ifcpp/reader \
	../ifcplusplus/src/IfcPlusPlus/src \
	../ifcplusplus/src/IfcPlusPlus/src/external/Carve/src/include \
	../ifcplusplus/src/external \
	../ifcplusplus/src/IfcPlusPlus/src/external \
	../glm-compat \
	../glm/src \
	../ifcplusplus/src/IfcPlusPlus/src/external/manifold/src/utilities/include \
	../ifcplusplus/src/IfcPlusPlus/src/external/Carve/src/common \
	../IFCConvert/src

CONFIG(debug, debug|release) {
	OBJECTS_DIR = debug
	windows {
	    DESTDIR = ../lib$${DIR_PREFIX}
	    DLLDESTDIR = ../../bin/debug$${DIR_PREFIX}
	}
	else {
	    DESTDIR = ../../bin/debug$${DIR_PREFIX}
	}
}
else {
	OBJECTS_DIR = release
	windows {
	    DESTDIR = ../lib$${DIR_PREFIX}
	    DLLDESTDIR = ../../bin/release$${DIR_PREFIX}
	}
	else {
	    DESTDIR = ../../bin/release$${DIR_PREFIX}
	}
}

!windows {
#    QMAKE_POST_LINK += ../lib$${DIR_PREFIX}/libImportIFCPlugin.so ../../bin/debug$${DIR_PREFIX}/libImportIFCPlugin.so
}

MOC_DIR = moc
UI_DIR = ui

SOURCES += \
	src/IFCImportPlugin.cpp \
	src/ImportIFCDialog.cpp \
	src/ImportIFCMessageHandler.cpp

HEADERS += \
	src/ImportIFCDialog.h \
	src/ImportIFCMessageHandler.h \
	src/SVCommonPluginInterface.h \
	src/SVImportPluginInterface.h \
	src/IFCImportPlugin.h



CONFIG(debug, debug|release) {
    QMAKE_LIBDIR += ../lib$${DIR_PREFIX} -L../lib/debug
    LIBS += -L../lib$${DIR_PREFIX} -L../lib/debug
}
else {
    QMAKE_LIBDIR += ../lib$${DIR_PREFIX} -L../lib/release
    LIBS += -L../lib$${DIR_PREFIX} -L../lib/release
}

windows {
	LIBS += \
		-lQtExt \
		-lIFCConvert \
		-lclipper \
		-lifcplusplus \
		-lCarve \
		-lTiCPP \
		-lIBKMK \
		-lIBK
}
else {
    CONFIG(debug, debug|release) {
	LIBS += \
	../lib/debug/libIFCConvert.a \
	../lib/debug/libclipper.a \
	../lib/debug/libifcplusplus.a \
	../lib$${DIR_PREFIX}/libCarve.a \
	../lib/debug/libQtExt.a \
	../lib/debug/libTiCPP.a \
	../lib/debug/libIBKMK.a \
	../lib/debug/libIBK.a
    }
    else {
	LIBS += \
	../lib/release/libIFCConvert.a \
	../lib/release/libclipper.a \
	../lib/release/libifcplusplus.a \
	../lib$${DIR_PREFIX}/libCarve.a \
	../lib/release/libQtExt.a \
	../lib/release/libTiCPP.a \
	../lib/release/libIBKMK.a \
	../lib/release/libIBK.a
    }
}

win32:LIBS += -liphlpapi
win32:LIBS += -lshell32

win32-msvc* {
	QMAKE_CXXFLAGS += /std:c++17
}

# Default rules for deployment.
# unix {
# 	target.path = $$[QT_INSTALL_PLUGINS]/generic
# }
# !isEmpty(target.path): INSTALLS += target

FORMS += \
	src/ImportIFCDialog.ui

TRANSLATIONS += resources/translations/ImportIFCPlugin_de.ts
CODECFORSRC = UTF-8
