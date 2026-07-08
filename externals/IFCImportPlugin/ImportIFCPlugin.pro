# Project file for ImportIFCPlugin
#
# remember to set DYLD_FALLBACK_LIBRARY_PATH on MacOSX
# set LD_LIBRARY_PATH on Linux

TARGET = ImportIFCPlugin

# this pri must be sourced from all our libraries,
# it contains all functions defined for casual libraries
include( ../../externals/IBK/IBK.pri )

QT += gui widgets

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

INCLUDEPATH = \
	src \
	../../externals/IBK/src \
	../../externals/IBKMK/src \
	../../externals/TiCPP/src \
	../../externals/QtExt/src \
	../../externals/Nandrad/src \
	../../externals/Vicus/src \
	../../externals/VicOSM/src \
	../../externals/VicIFC/src \
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


LIBS += \
	-lQtExt \
	-lIFCConvert \
	-lclipper \
	-lifcplusplus \
	-lCarve \
	-lTiCPP \
	-lIBKMK \
	-lVicus \
	-lVicOSM \
	-lVicIFC \
	-lNandrad \
	-lIBK


# Deploy the built plugin next to the bundled IFC2BESTest application so it can be
# loaded at runtime. IBK.pri's DESTDIR deploys the plugin into the SIM-VICUS plugin
# folder, so here we additionally copy libImportIFCPlugin.so into this project's
# bin/<cfg> dir.
CONFIG(debug, debug|release): APP_BIN_DIR = $$PWD/../../bin/debug
else: APP_BIN_DIR = $$PWD/../../bin/release
!win32 {
	QMAKE_POST_LINK += test -d $$APP_BIN_DIR || mkdir -p $$APP_BIN_DIR $$escape_expand(\\n\\t)
	QMAKE_POST_LINK += $$QMAKE_COPY $(DESTDIR)$(TARGET) $$APP_BIN_DIR/
}

FORMS += \
	src/ImportIFCDialog.ui

TRANSLATIONS += resources/translations/ImportIFCPlugin_de.ts
CODECFORSRC = UTF-8
