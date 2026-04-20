#include "ImportIFCDialog.h"
#include "ui_ImportIFCDialog.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>

#include <IFCC_IFCReader.h>
#include <IFCC_Helper.h>
#include <IFCC_Cancellation.h>

ImportIFCDialog::ImportIFCDialog(QWidget *parent, IFCC::IFCReader* reader) :
	QDialog(parent),
	ui(new Ui::ImportIFCDialog),
	m_reader(reader)
{
	ui->setupUi(this);
	ui->widgetConvert->setVisible(false);
	ui->pushButtonConvert->setEnabled(true);
	ui->checkBoxWriteBuildingElements->setChecked(m_reader->convertOptions().m_writeBuildingElements);
	ui->checkBoxWriteConstructions->setChecked(m_reader->convertOptions().m_writeConstructionElements);
	ui->checkBoxWriteOpenings->setChecked(m_reader->convertOptions().m_writeOpeningElements);
	ui->checkBoxWriteAllOthers->setChecked(m_reader->convertOptions().m_writeOtherElements);

	for(auto type : IFCC::constructionTypes()) {
		QListWidgetItem *item = new QListWidgetItem(elementTypeText(type));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
		item->setData(Qt::UserRole, (int)type);
		ui->listWidgetConstructionTypes->addItem(item);
	}

	for(auto type : IFCC::constructionSimilarTypes()) {
		QListWidgetItem *item = new QListWidgetItem(elementTypeText(type));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
		item->setData(Qt::UserRole, (int)type);
		ui->listWidgetConstructionTypes->addItem(item);
	}

	for(auto type : IFCC::constructionTypes()) {
		QListWidgetItem *item = new QListWidgetItem(elementTypeText(type));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
		item->setData(Qt::UserRole, (int)type);
		ui->listWidgetOpeningSearchElements->addItem(item);
	}

	// Set default scenario
	setScenario(CS_MediumMatching);

	ui->doubleSpinBoxMinimumArea->setValue(m_reader->convertOptions().m_minimumSurfaceArea*10000);
	ui->doubleSpinBoxMinimumDistance->setValue(m_reader->convertOptions().m_distanceEps*1000);
	ui->doubleSpinBoxPolygonEpsilon->setValue(m_reader->convertOptions().m_polygonEps*1000);
	ui->doubleSpinBoxMatchOpeningDistance->setValue(m_reader->convertOptions().m_openingDistance);
	ui->doubleSpinBoxStandardWallThickness->setValue(m_reader->convertOptions().m_standardWallThickness);

	ui->checkBoxAdvanced->setChecked(false);
	ui->tabWidgetAdvanced->setVisible(false);

	ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Import"));
	ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ImportIFCDialog::onAccepted);

	m_timer = new QTimer(this);
	// Connect to timer
	connect(m_timer, &QTimer::timeout, this, &ImportIFCDialog::onUpdateUi);

	m_timer->setInterval(100);
	m_timer->start();

	m_progressDialog = new QProgressDialog(tr("IFC Reader..."), tr("Abort"), 0, 100, this);
	m_progressDialog->setWindowModality(Qt::WindowModal);
	m_progressDialog->reset();
}

ImportIFCDialog::~ImportIFCDialog() {
	delete ui;
}

void ImportIFCDialog::setFilename(QString filename) {
	ui->lineEditIFCFile->setText(filename);
}

void ImportIFCDialog::on_toolButtonOpenIFCFile_clicked() {
	QString filename = QFileDialog::getOpenFileName(this, tr("Open ifc file"), QString(), tr("ifc STEP file (*.ifc)"));
	if(!filename.isEmpty()) {
		ui->lineEditIFCFile->setText(filename);
	}
	else {
		ui->lineEditIFCFile->clear();
		ui->textEdit->setHtml(tr("No filename."));

	}
}

void ImportIFCDialog::on_lineEditIFCFile_textChanged(const QString &arg1) {
	QFileInfo finfo(arg1);
	if(finfo.isFile()) {
		ui->textEdit->setHtml(tr("Filename is valid. Start reading."));
		qApp->processEvents();
		bool res = read();
		ui->widgetConvert->setVisible(res);
		if(res) {
			initConvertOptions();
			ui->textEdit->setText(tr("Reading successful"));
		}
	}
	else {
		ui->textEdit->setHtml(tr("This is not a valid filename."));
	}
}

void ImportIFCDialog::on_pushButtonConvert_clicked() {
	ui->textEdit->setText(tr("Converting ..."));

	initElements();
	bool useSpaceBoundaries = (m_scenario == CS_UseSpaceBoundaries);

	IFCC::Cancellation::reset();
	IFCC::ProgressHandler convertHandler([this](int v, QString t) { setProgress(v, t); }, 0.0, 1.0);
	m_progressDialog->show();
	m_convertSuccessfully = m_reader->convert(useSpaceBoundaries, &convertHandler);
	m_progressDialog->reset();
	setText();

	if(ui->checkBoxIgnorErrors->isChecked())
		m_convertSuccessfully = true;
	ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_convertSuccessfully);
}


void ImportIFCDialog::on_checkBoxRemoveDoubleSBs_clicked() {
	m_reader->setRemoveDoubledSBs(ui->checkBoxRemoveDoubleSBs->isChecked());
}


void ImportIFCDialog::on_radioButtonMatchingFull_clicked() {
	ui->listWidgetConstructionTypes->setEnabled(true);
	ui->spinBoxMatchingNConstructions->setEnabled(false);
}


void ImportIFCDialog::on_radioButtonMatchingFirst_clicked() {
	ui->listWidgetConstructionTypes->setEnabled(true);
	ui->spinBoxMatchingNConstructions->setEnabled(false);
}


void ImportIFCDialog::on_radioButtonMatchingNConstructions_clicked() {
	ui->listWidgetConstructionTypes->setEnabled(true);
	ui->spinBoxMatchingNConstructions->setEnabled(true);
}


void ImportIFCDialog::on_radioButtonMatchingNo_clicked() {
	ui->listWidgetConstructionTypes->setEnabled(false);
	ui->spinBoxMatchingNConstructions->setEnabled(false);
}


void ImportIFCDialog::on_radioButtonScenarioSB_clicked() {
	setScenario(CS_UseSpaceBoundaries);
}

void ImportIFCDialog::on_radioButtonScenarioFull_clicked() {
	setScenario(CS_FullMatching);
}

void ImportIFCDialog::on_radioButtonScenarioMedium_clicked() {
	setScenario(CS_MediumMatching);
}

void ImportIFCDialog::on_radioButtonScenarioNone_clicked() {
	setScenario(CS_NoMatching);
}

bool ImportIFCDialog::read() {
	if(ui->lineEditIFCFile->text().isEmpty()) {
		ui->textEdit->setText(tr("No IFC file."));
		return false;
	}


	ui->textEdit->setText(tr("Reading ..."));
	ui->textEdit->update();
	IBK::Path ifcfilename(ui->lineEditIFCFile->text().toStdString());
	bool ignoreError = ui->checkBoxIgnorErrors->isChecked();

	IFCC::ProgressHandler readHandler([this](int v, QString t) { setProgress(v, t); }, 0.0, 1.0);
	m_progressDialog->show();
	bool res = m_reader->read(ifcfilename, ignoreError, &readHandler);
	m_progressDialog->reset();
	int buildings = 0;
	int spaces = 0;
	QString fatalError;
	bool hasFatalErrors = !m_reader->checkEssentialIFCs(fatalError, buildings, spaces);
	if(res && !hasFatalErrors) {
		int number = m_reader->totalNumberOfIFCEntities();
		ui->textEdit->clear();
		QStringList text;
		text << tr("File read successfully with %1 IFC entities.").arg(number);
		text << tr("Project contains %1 buildings and %2 spaces.").arg(buildings).arg(spaces);
		ui->textEdit->setHtml(text.join("<br>"));
		qApp->processEvents();
	}
	else {
		QStringList text;
		if(hasFatalErrors) {
			text << tr("<font color=\"#FF0000\">Fatal error</font>");
			text << tr("Missing objects");
			text << fatalError;
		}
		if(!res){
			if(!m_reader->m_errorText.empty()) {
				QString errorText = QString::fromStdString(m_reader->m_errorText);
				errorText.replace("\n", "<br>");
				if(!ignoreError)
					text << tr("<font color=\"#FF0000\">Read Errors:</font>");
				else
					text << tr("Read Errors:");
				text << errorText;
			}
			if(!m_reader->m_warningText.empty()) {
				text << tr("Read Warnings:");
				QString warningText = QString::fromStdString(m_reader->m_warningText);
				warningText.replace("\n", "<br>");
				text << warningText;
			}
		}
		ui->textEdit->setHtml(text.join("<br>"));
		qApp->processEvents();
		if(!ignoreError || hasFatalErrors)
			return false;
	}
	return true;
}

void ImportIFCDialog::setText() {

	// check for equal space boundaries
	std::vector<std::pair<int,int>> equalSBs;
	int equalSBCount = 0;
	equalSBCount = m_reader->checkForEqualSpaceBoundaries(equalSBs);

	//check for unique sub surfaces
	std::vector<std::pair<int,std::vector<int>>> multiSubsurfaces;
	int multiSubCount = 0;
	multiSubCount = m_reader->checkForUniqueSubSurfacesInSpaces(multiSubsurfaces);

	// check for intersected spaces
	std::set<std::pair<int,int>> intersectedSpaceIds;
	std::map<int,size_t> instersectionCounts;
	for(auto isId : intersectedSpaceIds) {
		++instersectionCounts[isId.first];
	}
	int spaceIntersectCount = intersectedSpaceIds.size();

	// check for shared space boundaries
	std::set<std::pair<int,int>> sharedSpaceBoundaries;
	sharedSpaceBoundaries = m_reader->checkForSpaceWithSameSpaceBoundaries();
	std::map<int,size_t> sharedSpaceBoundaryCounts;
	for(auto isId : sharedSpaceBoundaries) {
		++sharedSpaceBoundaryCounts[isId.first];
	}
	int sharedSpaceBoundaryCount = sharedSpaceBoundaries.size();

	int notRelatedOpenings = m_reader->checkForNotRelatedOpenings();


	if(m_convertSuccessfully && (equalSBCount > 0 || multiSubCount > 0 || spaceIntersectCount > 0 || sharedSpaceBoundaryCount > 0)) {
		m_convertSuccessfully = false;
	}

	std::vector<int> wrongInstances = m_reader->checkForWrongSurfaceIds();
	if(!wrongInstances.empty())
		m_convertSuccessfully = false;

	// Determine if the result is still importable (conversion ok or errors ignored)
	bool importable = m_convertSuccessfully || ui->checkBoxIgnorErrors->isChecked();

	ui->textEdit->clear();
	QStringList text;

	// Print out convert errors/warnings
	const std::vector<IFCC::ConvertError>& errors = m_reader->convertErrors();
	if(!errors.empty()) {
		if(importable)
			text << tr("<font color=\"#CC7700\">Conversion warnings:</font>");
		else
			text << tr("<font color=\"#FF0000\">Conversion errors:</font>");
		for(const auto& err : errors) {
			text << tr("%1 for object '%2' with id: %3").arg(QString::fromStdString(err.m_errorText))
					.arg(QString::fromStdString(IFCC::objectTypeToString(err.m_objectType))).arg(err.m_objectID);
		}
		text << "<br>";
	}

	if(m_convertSuccessfully) {
		text << tr("Converting was successful.");
		if(!m_reader->m_errorText.empty()) {
			text << tr("<font color=\"#CC7700\">Warnings while converting:</font>");
			QString errTxt = QString::fromStdString(m_reader->m_errorText);
			errTxt.replace("\n","<br>");
			text << errTxt;
			text << "";
		}
		text << tr("File converted successfully.");
		text << "";
	}
	else {
		if(importable)
			text << tr("<font color=\"#CC7700\">Warnings while converting IFC file:</font>");
		else
			text << tr("<font color=\"#FF0000\">Error while converting IFC file.</font>");
		if(spaceIntersectCount > 0) {
			text << tr("%1 spaces intersections found.").arg(spaceIntersectCount);
			if(ui->checkBoxSpaceIntersectionDetails->isChecked()) {
				text << tr("Space intersections.<br>");
				for(const auto& it : intersectedSpaceIds) {
					QString name1 = m_reader->nameForId(it.first, IFCC::IFCReader::NIT_Space);
					QString name2 = m_reader->nameForId(it.second, IFCC::IFCReader::NIT_Space);
					text << tr("'%1' with '%2'.").arg(name1).arg(name2);
				}
			}
			else {
				for(const auto& it : instersectionCounts) {
					text << tr("Space %1 with %2 spaces intersected.").arg(it.first).arg(it.second);
				}
			}
		}
		if(sharedSpaceBoundaryCount > 0) {
			text << tr("%1 spaces with shared space boundaries found.").arg(sharedSpaceBoundaryCount);
			for(const auto& it : sharedSpaceBoundaryCounts) {
				text << tr("Space %1 shares with %2 spaces at least one space boundary.").arg(it.first).arg(it.second);
			}
		}
		if(equalSBCount > 0) {
			text << tr("%1 space boundaries with identical surfaces found.").arg(equalSBCount);
		}
		if(multiSubCount > 0) {
			text << tr("%1 opening space boundaries found which are used more than once in one space.").arg(multiSubCount);
		}
		if(wrongInstances.size() > 0) {
			text << tr("%1 component instances found which have non valid surface ids.").arg(wrongInstances.size());
		}
		if(notRelatedOpenings > 0) {
			text << tr("%1 openings don't have a connection to a space boundary.").arg(notRelatedOpenings);
		}
		if(!m_reader->m_errorText.empty()) {
			text << QString::fromStdString(m_reader->m_errorText);
		}
	}
	ui->textEdit->setHtml(text.join("<br>"));
}

void ImportIFCDialog::initElements() {
	m_reader->clearElementsForSpaceBoundaries();
	for(int i=0; i<ui->listWidgetConstructionTypes->count(); ++i) {
		QListWidgetItem* item = ui->listWidgetConstructionTypes->item(i);
		if(item->checkState() == Qt::Checked)
			m_reader->setElementsForSpaceBoundaries(static_cast<IFCC::BuildingElementTypes>(item->data(Qt::UserRole).toInt()), true);
	}

	QSet<IFCC::BuildingElementTypes> noSearchForOpeningsInTypes;
	for(int i=0; i<ui->listWidgetOpeningSearchElements->count(); ++i) {
		QListWidgetItem* item = ui->listWidgetOpeningSearchElements->item(i);
		if(item->checkState() != Qt::Checked)
			noSearchForOpeningsInTypes << static_cast<IFCC::BuildingElementTypes>(item->data(Qt::UserRole).toInt());
	}
	m_reader->addNoSearchForOpenings(noSearchForOpeningsInTypes);

	// Map scenario to matching type
	switch(m_scenario) {
		case CS_UseSpaceBoundaries:
			// Space boundaries mode - matching type is irrelevant
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_NoMatching);
			break;
		case CS_FullMatching:
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_MatchEachConstruction);
			break;
		case CS_MediumMatching:
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_MatchOnlyFirstConstruction);
			break;
		case CS_NoMatching:
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_NoMatching);
			break;
	}

	// Allow advanced tab radio buttons to override if details are shown
	if(ui->checkBoxAdvanced->isChecked() && m_scenario != CS_UseSpaceBoundaries) {
		if(ui->radioButtonMatchingFull->isChecked())
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_MatchEachConstruction);
		else if(ui->radioButtonMatchingFirst->isChecked())
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_MatchOnlyFirstConstruction);
		else if(ui->radioButtonMatchingNConstructions->isChecked())
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_MatchFirstNConstructions);
		else
			m_reader->setConvertMatchingType(IFCC::ConvertOptions::CM_NoMatching);
	}

	m_reader->setMatchingDistances(ui->doubleSpinBoxMatchConstructionFactor->value(),
								   ui->doubleSpinBoxStandardWallThickness->value(),
								   ui->doubleSpinBoxMatchOpeningDistance->value());
	m_reader->setWritingBuildingElements(ui->checkBoxWriteConstructions->isChecked(),
										 ui->checkBoxWriteBuildingElements->isChecked(),
										 ui->checkBoxWriteOpenings->isChecked(),
										 ui->checkBoxWriteAllOthers->isChecked());

	// with conversions into basic SI units m and m2
	m_reader->setMinimumCheckValues(ui->doubleSpinBoxMinimumDistance->value() / 1000.0, ui->doubleSpinBoxMinimumArea->value() / 10000.0,
									ui->doubleSpinBoxPolygonEpsilon->value() / 1000.0);
	m_reader->setUseCSGForOpenings(ui->checkBoxOpeningCSGSearch->isChecked());
	m_reader->setSurfaceWritingMode(ui->checkBoxSurfaceWritingMethod->isChecked());
}

void ImportIFCDialog::setScenario(ConversionScenario scenario) {
	m_scenario = scenario;

	switch(scenario) {
		case CS_UseSpaceBoundaries: {
			ui->radioButtonScenarioSB->setChecked(true);
			ui->labelScenarioDescription->setText(
				tr("Use space boundaries from the IFC file directly. No construction matching needed."));
			// Disable advanced tabs when using space boundaries
			ui->tabWidgetAdvanced->setEnabled(false);
			ui->checkBoxAdvanced->setEnabled(false);
			break;
		}
		case CS_FullMatching: {
			ui->radioButtonScenarioFull->setChecked(true);
			ui->radioButtonMatchingFull->setChecked(true);
			ui->labelScenarioDescription->setText(
				tr("Check every construction element for matches with space surfaces. Most thorough but slowest."));
			ui->tabWidgetAdvanced->setEnabled(true);
			ui->checkBoxAdvanced->setEnabled(true);
			break;
		}
		case CS_MediumMatching: {
			ui->radioButtonScenarioMedium->setChecked(true);
			ui->radioButtonMatchingFirst->setChecked(true);
			ui->labelScenarioDescription->setText(
				tr("Check only the highest-priority construction for each space surface. Good balance of speed and quality."));
			ui->tabWidgetAdvanced->setEnabled(true);
			ui->checkBoxAdvanced->setEnabled(true);
			break;
		}
		case CS_NoMatching: {
			ui->radioButtonScenarioNone->setChecked(true);
			ui->radioButtonMatchingNo->setChecked(true);
			ui->labelScenarioDescription->setText(
				tr("Create space boundaries without construction matching. No windows or doors. Fastest."));
			ui->tabWidgetAdvanced->setEnabled(true);
			ui->checkBoxAdvanced->setEnabled(true);
			break;
		}
	}
}

QString ImportIFCDialog::elementTypeText(IFCC::BuildingElementTypes type) const {
	switch(type) {
		case IFCC::BET_Beam: return tr("Beam");
		case IFCC::BET_Chimney: return tr("Chimney");
		case IFCC::BET_Column: return tr("Column");
		case IFCC::BET_Covering: return tr("Covering");
		case IFCC::BET_CurtainWall: return tr("Curtain Wall");
		case IFCC::BET_Door: return tr("Door");
		case IFCC::BET_Footing: return tr("Footing");
		case IFCC::BET_Member: return tr("Member");
		case IFCC::BET_Pile: return tr("Pile");
		case IFCC::BET_Plate: return tr("Plate");
		case IFCC::BET_Railing: return tr("Railing");
		case IFCC::BET_Ramp: return tr("Ramp");
		case IFCC::BET_RampFlight: return tr("RampFlight");
		case IFCC::BET_Roof: return tr("Roof");
		case IFCC::BET_ShadingDevice: return tr("Shading Device");
		case IFCC::BET_Slab: return tr("Slab");
		case IFCC::BET_Stair: return tr("Stair");
		case IFCC::BET_StairFlight: return tr("Stair Flight");
		case IFCC::BET_Wall: return tr("Wall");
		case IFCC::BET_Window: return tr("Window");
		case IFCC::BET_CivilElement: return tr("Civil Element");
		case IFCC::BET_DistributionElement: return tr("Distribution Element");
		case IFCC::BET_ElementAssembly: return tr("Element Assembly");
		case IFCC::BET_ElementComponent: return tr("Element Component");
		case IFCC::BET_FeatureElement: return tr("Feature Element");
		case IFCC::BET_FurnishingElement: return tr("Furnishing Element");
		case IFCC::BET_GeographicalElement: return tr("Geographical Element");
		case IFCC::BET_TransportElement: return tr("Transport Element");
		case IFCC::BET_VirtualElement: return tr("VirtualElement");
		case IFCC::BET_BuildingElementPart: return tr("Building Element Part");
		case IFCC::BET_All: return tr("All");
		case IFCC::BET_None: return tr("None");
	}
	return QString();
}

void ImportIFCDialog::initConvertOptions() {
	ui->textEdit->clear();
	int sbCount = m_reader->numberOfIFCSpaceBoundaries();
	bool hasSpaceBoundaries = sbCount > 0;

	// Enable/disable space boundary radio button based on file content
	ui->radioButtonScenarioSB->setEnabled(hasSpaceBoundaries);
	if(hasSpaceBoundaries) {
		ui->radioButtonScenarioSB->setText(tr("Use IFC Space Boundaries (%1 found)").arg(sbCount));
		setScenario(CS_UseSpaceBoundaries);
	}
	else {
		ui->radioButtonScenarioSB->setText(tr("Use IFC Space Boundaries (not available)"));
		ui->radioButtonScenarioSB->setToolTip(tr("No space boundaries found in the IFC file"));
		setScenario(CS_MediumMatching);
	}

	ui->doubleSpinBoxMatchConstructionFactor->setValue(m_reader->convertOptions().m_distanceFactor);
	ui->doubleSpinBoxMatchOpeningDistance->setValue(m_reader->convertOptions().m_openingDistance);
	ui->checkBoxWriteBuildingElements->setChecked(m_reader->convertOptions().m_writeBuildingElements);
	ui->checkBoxWriteConstructions->setChecked(m_reader->convertOptions().m_writeConstructionElements);
	ui->checkBoxWriteOpenings->setChecked(m_reader->convertOptions().m_writeOpeningElements);
	ui->checkBoxWriteAllOthers->setChecked(m_reader->convertOptions().m_writeOtherElements);
	ui->checkBoxSurfaceWritingMethod->setChecked(m_reader->convertOptions().m_useOldPolygonWriting);
}


void ImportIFCDialog::on_checkBoxAdvanced_clicked() {
	ui->tabWidgetAdvanced->setVisible(ui->checkBoxAdvanced->isChecked());
}

void ImportIFCDialog::onAccepted() {
	if(!m_convertSuccessfully) {
		QMessageBox::critical(this, QString(), tr("Conversion has not been completed successfully!"));
		return;
	}
	accept();
}

void ImportIFCDialog::onUpdateUi() {
	qApp->processEvents();
	m_timer->start();
}

void ImportIFCDialog::setProgress(int val, QString text) {
	if(m_progressDialog) {
		if(!text.isEmpty())
			m_progressDialog->setLabelText(text);
		m_progressDialog->setValue(val);
		if(val >= 100)
			m_progressDialog->reset();
		QApplication::processEvents();
		// Relay user's Abort click to worker code via the pipeline-wide cancellation flag.
		if(m_progressDialog->wasCanceled())
			IFCC::Cancellation::set(true);
	}
}
