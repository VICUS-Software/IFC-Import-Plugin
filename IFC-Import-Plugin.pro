# Project for IFCConvert plugin session and all IBK libraries
# Note: sundials project is excluded because this library used different editor settings
#       Add sundials manually to the session and set build dependencies accordingly!

TEMPLATE=subdirs

# SUBDIRS lists all subprojects
SUBDIRS += ImportIFCPlugin \
#	GEGExportPlugin \
	IFCConvert \
	IFC2BESTest \
	Carve \
	Clipper \
	QtExt \
	IBK \
	IBKMK \
	ifcplusplus \
	Nandrad \
	Vicus \
	VicOSM \
	VicIFC \
	CCM \
	glm \
	DataIO \
	QuaZIP \
	NandradFMUGenerator \
	TiCPP

# where to find the sub projects
IFC2BESTest.file = IFC2BESTest/IFC2BESTest.pro

ImportIFCPlugin.file = externals/IFCImportPlugin/ImportIFCPlugin.pro
#GEGExportPlugin.file = externals/GEGExportPlugin/GEGExportPlugin.pro
IFCConvert.file = externals/IFCConvert/IFCConvert.pro
CCM.file = externals/CCM/CCM.pro
DataIO.file = externals/DataIO/DataIO.pro
Nandrad.file = externals/Nandrad/Nandrad.pro
NandradFMUGenerator.file = externals/NandradFMUGenerator/NandradFMUGenerator.pro
Vicus.file = externals/Vicus/Vicus.pro
VicOSM.file = externals/VicOSM/VicOSM.pro
VicIFC.file = externals/VicIFC/VicIFC.pro
Carve.file = externals/ifcplusplus/src/IfcPlusPlus/src/external/Carve/projects/Qt/Carve.pro
Clipper.file = externals/clipper/clipper.pro
IBK.file = externals/IBK/IBK.pro
IBKMK.file = externals/IBKMK/IBKMK.pro
TiCPP.file = externals/TiCPP/TiCPP.pro
QtExt.file = externals/QtExt/QtExt.pro
ifcplusplus.file = externals/ifcplusplus/ifcplusplus.pro
QuaZIP.file = externals/QuaZIP/QuaZIP.pro
glm.file = externals/glm/glm.pro

# dependencies
IFC2BESTest.depends = ImportIFCPlugin  IBK IBKMK TiCPP #GEGExportPlugin

CCM.depends = IBK TiCPP
DataIO.depends = IBK
QtExt.depends = IBK
Nandrad.depends = IBK TiCPP IBKMK
Vicus.depends = IBK TiCPP Nandrad IBKMK CCM glm VicOSM VicIFC Clipper QuaZIP NandradFMUGenerator
VicOSM.depends = IBK Clipper TiCPP IBKMK Nandrad
VicIFC.depends = IBK IBKMK TiCPP Nandrad
ImportIFCPlugin.depends = IBK IBKMK TiCPP QtExt Carve Clipper IFCConvert
#GEGExportPlugin.depends = IBK IBKMK TiCPP QtExt Nandrad Vicus CCM DataIO
IFCConvert.depends = IBK IBKMK TiCPP Carve Clipper Vicus VicOSM VicIFC glm
IBKMK.depends = IBK

linux-g++ | linux-g++-64 | linux-g++-32 {
	# IFC Debug
#    SUBDIRS += IfcDebug
#    IfcDebug.file = IfcDebug/projects/Qt/IfcDebug.pro
 #   IfcDebug.depends = ImportIFCPlugin IBK IBKMK TiCPP
}

#DEFINES += OUTPUT_AFTER_EACH_STEP

