#include "IFCImportPlugin.h"

#include <iostream>

#include <IBK_messages.h>
#include <IBK_Path.h>

#include <IFCC_IFCReader.h>
#include <IFCC_Helper.h>

#include <QtExt_Directories.h>
#include <QtExt_LanguageHandler.h>

#include <QDir>
#include <QFileDialog>

#include <tinyxml.h>


//#include "ImportWizard.h"
#include "ImportIFCDialog.h"

IFCImportPlugin::IFCImportPlugin(QObject *)
{
}

bool IFCImportPlugin::import(QWidget * parent, QString& projectText) {
	IFCC::IFCReader			reader;

	QString filename = QFileDialog::getOpenFileName(parent, tr("Open ifc file"), QString(), tr("ifc STEP file (*.ifc)"));

	ImportIFCDialog importDlg(parent, &reader);
	importDlg.setFilename(filename);
	if (importDlg.exec() == QDialog::Rejected)
			return false;

	if(reader.m_convertCompletedSuccessfully) {
		reader.setVicusProjectText(projectText);
		m_ifcFileName = QString::fromStdString(reader.filename().str());
		return true;
	}

	m_ifcFileName.clear();
	return false;
}

QString IFCImportPlugin::title() const {
	return tr("Import IFC file");
}

QString IFCImportPlugin::importMenuCaption() const {
	return tr("IFC file ...");
}

void IFCImportPlugin::setLanguage(QString langId, QString appname) {
	QtExt::Directories::appname = appname;
	QtExt::Directories::devdir = appname;

	// initialize resources in dependent libraries
	Q_INIT_RESOURCE(QtExt);

	// *** Create log file directory and setup message handler ***
	QDir baseDir;
	baseDir.mkpath(QtExt::Directories::userDataDir());

	IBK::MessageHandlerRegistry::instance().setMessageHandler( &m_msgHandler );
	std::string errmsg;
	std::string logfile = QtExt::Directories::userDataDir().toStdString();
	logfile += "/IFCImportPlugin.log";
	m_msgHandler.openLogFile(logfile, false, errmsg);

	// adjust log file verbosity
	m_msgHandler.setLogfileVerbosityLevel( IBK::VL_DEVELOPER );

	// reset appname to find correct translation file
	QtExt::Directories::appname = "ImportIFCPlugin";
	QtExt::LanguageHandler::instance().installTranslator(langId);
}

QString IFCImportPlugin::IFCFileName() const {
	return m_ifcFileName;
}

bool IFCImportPlugin::runCLI(const QString & ifcPath, const QString & vicusPath,
							 bool useSpaceBoundaries, bool writeShading) {
	IFCC::IFCReader reader;
	reader.setWriteShadingObjects(writeShading, writeShading, writeShading, false);

	// Default element-type filter: walls/slabs/roofs PLUS IfcBuildingElementPart so
	// openings attach to the actual layer surface (e.g. "Mineralwolldämmung Prio 1"
	// in THO_optimized). createSpaceBoundaries_2 transparently drops walls that have
	// part-children when parts are matched.
	for(IFCC::BuildingElementTypes t : IFCC::constructionTypes())
		reader.setElementsForSpaceBoundaries(t, true);
	reader.setElementsForSpaceBoundaries(IFCC::BET_BuildingElementPart, true);

	// Matching mode override for batch tests (mirrors the GUI scenarios):
	// IFCC_MATCHING = each | first | n | none
	if(const char* m = std::getenv("IFCC_MATCHING")) {
		std::string mv(m);
		if(mv == "each")
			reader.setConvertMatchingType(IFCC::ConvertOptions::CM_MatchEachConstruction);
		else if(mv == "first")
			reader.setConvertMatchingType(IFCC::ConvertOptions::CM_MatchOnlyFirstConstruction);
		else if(mv == "n")
			reader.setConvertMatchingType(IFCC::ConvertOptions::CM_MatchFirstNConstructions);
		else if(mv == "none")
			reader.setConvertMatchingType(IFCC::ConvertOptions::CM_NoMatching);
	}

	IBK::Path inPath(ifcPath.toStdString());
	// ignoreReadError=true: keep going even if the STEP parser reports broken
	// forward references — most files still produce usable geometry for the
	// entities that DID resolve, which is more useful than hard-failing in batch.
	if(!reader.read(inPath, true)) {
		std::cerr << "[CLI] read failed hard: " << reader.m_errorText << std::endl;
		return false;
	}
	if(!reader.convert(useSpaceBoundaries)) {
		std::cerr << "[CLI] convert reported errors: " << reader.m_errorText << std::endl;
		// Still write what we have — partial results are useful for batch quality analysis.
	}
	if(!reader.m_convertCompletedSuccessfully) {
		std::cerr << "[CLI] convert did not complete successfully" << std::endl;
	}
	IBK::Path outPath(vicusPath.toStdString());
	reader.writeXML(outPath);
	m_ifcFileName = ifcPath;
	return reader.m_convertCompletedSuccessfully;
}

