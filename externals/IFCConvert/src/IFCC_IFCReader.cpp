#include "IFCC_IFCReader.h"

#include <omp.h>

#include "IFCC_Helper.h"

#include <QDebug>


#include <ifcpp/IFC4X3/include/IfcRelSpaceBoundary.h>
#include <ifcpp/IFC4X3/include/IfcRelContainedInSpatialStructure.h>
#include <ifcpp/IFC4X3/include/IfcRelAggregates.h>
#include <ifcpp/IFC4X3/include/IfcSpatialElement.h>
#include <ifcpp/IFC4X3/include/IfcWall.h>
#include <ifcpp/IFC4X3/include/IfcBeam.h>
#include <ifcpp/IFC4X3/include/IfcChimney.h>
#include <ifcpp/IFC4X3/include/IfcColumn.h>
#include <ifcpp/IFC4X3/include/IfcCovering.h>
#include <ifcpp/IFC4X3/include/IfcCurtainWall.h>
#include <ifcpp/IFC4X3/include/IfcDoor.h>
#include <ifcpp/IFC4X3/include/IfcFooting.h>
#include <ifcpp/IFC4X3/include/IfcMember.h>
#include <ifcpp/IFC4X3/include/IfcPile.h>
#include <ifcpp/IFC4X3/include/IfcPlate.h>
#include <ifcpp/IFC4X3/include/IfcRailing.h>
#include <ifcpp/IFC4X3/include/IfcRamp.h>
#include <ifcpp/IFC4X3/include/IfcRampFlight.h>
#include <ifcpp/IFC4X3/include/IfcRoof.h>
#include <ifcpp/IFC4X3/include/IfcShadingDevice.h>
#include <ifcpp/IFC4X3/include/IfcSlab.h>
#include <ifcpp/IFC4X3/include/IfcStair.h>
#include <ifcpp/IFC4X3/include/IfcStairFlight.h>
#include <ifcpp/IFC4X3/include/IfcWall.h>
#include <ifcpp/IFC4X3/include/IfcCivilElement.h>
#include <ifcpp/IFC4X3/include/IfcDistributionElement.h>
#include <ifcpp/IFC4X3/include/IfcElementAssembly.h>
#include <ifcpp/IFC4X3/include/IfcElementComponent.h>
#include <ifcpp/IFC4X3/include/IfcFurnishingElement.h>
#include <ifcpp/IFC4X3/include/IfcGeographicElement.h>
#include <ifcpp/IFC4X3/include/IfcTransportElement.h>
#include <ifcpp/IFC4X3/include/IfcVirtualElement.h>
#include <ifcpp/IFC4X3/include/IfcExternalSpatialElement.h>
#include <ifcpp/IFC4X3/include/IfcSpatialZone.h>
#include <ifcpp/IFC4X3/include/IfcBuildingElementPart.h>

#include <Carve/src/include/carve/carve.hpp>

#include <IBK_Exception.h>
#include <IBK_FileUtils.h>
#include <IBK_StringUtils.h>

#include <tinyxml.h>

//#include "IFCC_MeshUtils.h"
#include "IFCC_Logger.h"
#include "IFCC_RoomHealer.h"
#include "IFCC_ProgressHandler.h"
#include "IFCC_CSG_Adapter.h"
#include "IFCC_GeometrySettings.h"
#include "IFCC_RepresentationHelper.h"
#include "IFCC_Opening.h"

namespace IFCC {

/*! Creates a ProgressHandler that maps local [0,1] progress to [rangeStart, rangeEnd]
	of the given parent IBK::NotificationHandler.
*/
static ProgressHandler makeSubRange(IBK::NotificationHandler* parent, double rangeStart, double rangeEnd) {
	return ProgressHandler([parent](int v, QString t) {
		if(parent) {
			// Keep the QByteArray alive for the duration of the notify call —
			// otherwise its backing buffer is freed before notify reads from it.
			QByteArray utf8 = t.toUtf8();
			const char* text = t.isEmpty() ? nullptr : utf8.constData();
			parent->notify(double(v) / 100.0, text);
		}
	}, rangeStart, rangeEnd);
}

IFCReader::IFCReader() :
	m_hasError(false),
	m_hasWarning(false),
	m_readCompletedSuccessfully(false),
	m_convertCompletedSuccessfully(false),
	m_model(new BuildingModel),
	m_geometryConverter(m_model),
	m_project(0),
	m_site(0)
{
	m_geometryConverter.clearMessagesCallback();
	m_geometryConverter.resetModel();
	m_geometryConverter.getGeomSettings()->setNumVerticesPerCircle(16);
	m_geometryConverter.getGeomSettings()->setMinNumVerticesPerArc(4);

	Logger::instance().set("/tmp/ifc-import-main.log");
}

IFCReader::~IFCReader() {
}


void IFCReader::clear() {
	m_model.reset(new BuildingModel);
	m_geometryConverter.setModel(m_model);
	m_hasError = false;
	m_hasWarning = false;
	m_geometryConverter.clearMessagesCallback();
	m_geometryConverter.resetModel();
	m_geometryConverter.getGeomSettings()->setNumVerticesPerCircle(16);
	m_geometryConverter.getGeomSettings()->setMinNumVerticesPerArc(4);

	m_errorText.clear();
	m_warningText.clear();
	m_progressText.clear();
	m_readCompletedSuccessfully = false;

	clearConvertData();
}

void IFCReader::clearConvertData() {
	m_convertCompletedSuccessfully = false;

	m_site = Site(0);
	m_elementEntitesShape.clear();
	m_spatialEntitesShape.clear();
	m_spaceEntitesShape.clear();
	m_unknownEntitesShape.clear();
	m_buildingsShape.clear();
	m_storeysShape.clear();
	m_openingsShape.clear();
	m_externalSpatialShapes.clear();
	m_spatialZoneShapes.clear();
	m_siteShape.reset();
	m_buildingElements.clear();
	m_openings.clear();
	m_database.clear();
	m_instances.clear();
	m_ifcModel.m_objects.clear();
}

bool IFCReader::loadModelFromSTEPFile( const IBK::Path& filePath, shared_ptr<BuildingModel>& targetModel ) {
	m_hasError = false;
	// if file content needs to be loaded into a plain model, call resetModel() before loadModelFromFile
	std::string ext = filePath.extension();

	if( ext != "ifc" ) {
		m_errorText = "Wrong file format";
		m_hasError = true;
		return false;
	}

	// open file
	// STEP files are pure ASCII with '.' as decimal separator, so use the
	// classic "C" locale. Using std::locale("") would inherit the user's
	// environment locale, which on minimal systems may be invalid and throw
	// std::runtime_error("locale::facet::_S_create_c_locale name not valid").
	std::ifstream infile;
	bool res = IBK::open_ifstream(infile, filePath, std::ios_base::in);

	if( !res ) {
		m_errorText = "Could not open file: " + filePath.str();
		m_hasError = true;
		return false;
	}

	// get length of file content
	infile.imbue(std::locale::classic());
	infile.seekg( 0, std::ios::end );
	std::streampos file_end_pos = infile.tellg();
	infile.seekg( 0, std::ios::beg );

	ReaderSTEP readerStep;
	readerStep.setMessageCallBack(this, &IFCReader::messageTarget);
	readerStep.loadModelFromStream(infile, file_end_pos, targetModel);
	return true;
}


bool IFCReader::read(const IBK::Path& filename, bool ignoreReadError, IBK::NotificationHandler* notify) {
	clear();
	Logger::instance().resetSteps();

	Logger::instance().beginStep("read-ifc-file");
	Logger::instance() << "file: " << filename.str();

	if(notify)
		notify->notify(0.01, "Read IFC file");

	m_filename = filename;
	m_readCompletedSuccessfully = true;

	// Map ReaderSTEP's [0,1] parse progress into [0.01, 0.99] of the parent so
	// messageTarget() relays progress events from the parser (which fires every
	// few percent during the STEP load).
	ProgressHandler readProgress = makeSubRange(notify, 0.01, 0.99);
	m_currentSubProgress = &readProgress;

	try {
		bool res = loadModelFromSTEPFile(m_filename, m_geometryConverter.getBuildingModel());
		m_currentSubProgress = nullptr;
		if(!ignoreReadError && !res) {
			m_readCompletedSuccessfully = false;
			Logger::instance() << "loadModelFromSTEPFile returned false; errorText=" << m_errorText;
		}

		if(notify)
			notify->notify(1.0, "Read complete");
		Logger::instance() << "read complete; hasError=" << (m_hasError ? 1 : 0);
		return !m_hasError;
	}
	catch (std::exception& e) {
		m_currentSubProgress = nullptr;
		m_errorText = e.what();
		Logger::instance() << "read exception: " << e.what();
		if(!ignoreReadError) {
			m_readCompletedSuccessfully = false;
			m_hasError = true;
		}

		if(notify)
			notify->notify(1.0, "Read failed");
		return false;
	}
	return true;
}

void IFCReader::splitShapeData() {
	const std::map<std::string,shared_ptr<ProductShapeData>>& shapeDataMap = m_geometryConverter.getShapeInputData();

	for(const auto& shapeData : shapeDataMap) {
		const shared_ptr<ProductShapeData>& data = shapeData.second;
		std::string id = shapeData.first;
		std::string guid = data->m_entity_guid;
		const std::shared_ptr<IfcObjectDefinition> od = data->m_ifc_object_definition.lock();
		if(dynamic_pointer_cast<IfcElement>(od) != nullptr) {
			if(dynamic_pointer_cast<IfcWall>(od) != nullptr) {
				m_elementEntitesShape[BET_Wall].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcBeam>(od) != nullptr) {
				m_elementEntitesShape[BET_Beam].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcChimney>(od) != nullptr) {
				m_elementEntitesShape[BET_Chimney].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcColumn>(od) != nullptr) {
				m_elementEntitesShape[BET_Column].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcCovering>(od) != nullptr) {
				m_elementEntitesShape[BET_Covering].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcCurtainWall>(od) != nullptr) {
				m_elementEntitesShape[BET_CurtainWall].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcDoor>(od) != nullptr) {
				m_elementEntitesShape[BET_Door].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcFooting>(od) != nullptr) {
				m_elementEntitesShape[BET_Footing].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcMember>(od) != nullptr) {
				m_elementEntitesShape[BET_Member].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcPile>(od) != nullptr) {
				m_elementEntitesShape[BET_Pile].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcPlate>(od) != nullptr) {
				m_elementEntitesShape[BET_Plate].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcRailing>(od) != nullptr) {
				m_elementEntitesShape[BET_Railing].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcRamp>(od) != nullptr) {
				m_elementEntitesShape[BET_Ramp].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcRampFlight>(od) != nullptr) {
				m_elementEntitesShape[BET_RampFlight].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcRoof>(od) != nullptr) {
				m_elementEntitesShape[BET_Roof].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcShadingDevice>(od) != nullptr) {
				m_elementEntitesShape[BET_ShadingDevice].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcSlab>(od) != nullptr) {
				m_elementEntitesShape[BET_Slab].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcStair>(od) != nullptr) {
				m_elementEntitesShape[BET_Stair].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcStairFlight>(od) != nullptr) {
				m_elementEntitesShape[BET_StairFlight].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcWindow>(od) != nullptr) {
				m_elementEntitesShape[BET_Window].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcFeatureElement>(od) != nullptr) {
				if(dynamic_pointer_cast<IfcOpeningElement>(od) != nullptr) {
					std::string guid = guidFromObject(od.get());
					m_openingsShape[guid] = data;
				}

				m_elementEntitesShape[BET_FeatureElement].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcCivilElement>(od) != nullptr) {
				m_elementEntitesShape[BET_CivilElement].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcDistributionElement>(od) != nullptr) {
				m_elementEntitesShape[BET_DistributionElement].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcElementAssembly>(od) != nullptr) {
				m_elementEntitesShape[BET_ElementAssembly].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcElementComponent>(od) != nullptr) {
				if(dynamic_pointer_cast<IfcBuildingElementPart>(od) != nullptr) {
					m_elementEntitesShape[BET_BuildingElementPart].push_back(data);
				}
				else {
					m_elementEntitesShape[BET_ElementComponent].push_back(data);
				}
			}
			else if(dynamic_pointer_cast<IfcFurnishingElement>(od) != nullptr) {
				m_elementEntitesShape[BET_FurnishingElement].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcGeographicElement>(od) != nullptr) {
				m_elementEntitesShape[BET_GeographicalElement].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcTransportElement>(od) != nullptr) {
				m_elementEntitesShape[BET_TransportElement].push_back(data);
			}
			else if(dynamic_pointer_cast<IfcVirtualElement>(od) != nullptr) {
				m_elementEntitesShape[BET_VirtualElement].push_back(data);
			}
			else {
				m_elementEntitesShape[BET_All].push_back(data);
			}
		}
		else if(dynamic_pointer_cast<IfcSpatialStructureElement>(od) != nullptr) {
			std::string guid = guidFromObject(od.get());
			if(dynamic_pointer_cast<IfcSite>(od) != nullptr) {
				if(m_siteShape != nullptr)
					throw IBK::Exception("Second site found.", "IFCReader::splitShapeData");
				m_siteShape = data;
			}
			else if(dynamic_pointer_cast<IfcBuilding>(od) != nullptr) {
				m_buildingsShape[guid] = data;
			}
			else if(dynamic_pointer_cast<IfcBuildingStorey>(od) != nullptr) {
				m_storeysShape[guid] = data;
			}
			else if(dynamic_pointer_cast<IfcSpace>(od) != nullptr) {
				m_spaceEntitesShape[guid] = data;
			}
			else {
				m_spatialEntitesShape[guid] = data;
			}
		}
		else if(dynamic_pointer_cast<IfcExternalSpatialElement>(od) != nullptr) {
			std::string guid = guidFromObject(od.get());
			m_externalSpatialShapes[guid] = data;
		}
		else if(dynamic_pointer_cast<IfcSpatialZone>(od) != nullptr) {
			std::string guid = guidFromObject(od.get());
			m_spatialZoneShapes[guid] = data;
		}
		else {
			std::string guid = guidFromObject(od.get());
			m_unknownEntitesShape[guid] = data;
		}
	}
}

void IFCReader::updateBuildingElements(IBK::NotificationHandler* notify) {
	Logger::instance() << "IFCReader::updateBuildingElements start";
	size_t elemCount = 0;
	for(auto& elems : m_elementEntitesShape) {
		elemCount += elems.second.size();
	}
	size_t currCount = 0;
	size_t setFailConstr = 0, setFailSimilar = 0, setFailOpening = 0;
	size_t noSurfConstr = 0, noSurfSimilar = 0, noSurfOpening = 0;

	// Length factor for converting project units into [m]. Needed for
	// IfcMaterialLayer thicknesses, which are NOT converted by the geometry
	// pipeline — mm-based files (e.g. LENGTHUNIT .MILLI.) otherwise produce
	// wall thicknesses of 300 m and break all distance-based matching.
	double lengthFactor = 1.0;
	if(m_geometryConverter.getBuildingModel() && m_geometryConverter.getBuildingModel()->getUnitConverter())
		lengthFactor = m_geometryConverter.getBuildingModel()->getUnitConverter()->getLengthInMeterFactor();

	m_buildingElements.clear();
	for(auto& elems : m_elementEntitesShape) {
		for(auto& elem : elems.second) {
			++currCount;
			if(notify != nullptr && elemCount > 0)
				notify->notify(double(currCount) / double(elemCount));
			if(elem.get() == nullptr)
				continue;

			std::shared_ptr<IfcElement> e = dynamic_pointer_cast<IfcElement>(elem->m_ifc_object_definition.lock());
			if(e == nullptr)
				continue;

			if(isConstructionType(elems.first)) {
				std::shared_ptr<BuildingElement> bElem(new BuildingElement(GUID_maker::instance().guid()));
				if(!bElem->set(e, elems.first, lengthFactor)) {
					++setFailConstr;
					Logger::instance() << "set() FAILED constr type=" << (int)elems.first
									   << " ifcTag=#" << e->m_tag << " guid=" << elem->m_entity_guid;
					continue;
				}

				m_buildingElements.m_constructionElements.push_back( bElem);
				BuildingElement& currbElem = *m_buildingElements.m_constructionElements.back();

				currbElem.getShapeOfParts(m_elementEntitesShape[BET_BuildingElementPart], m_convertErrors);
				currbElem.update(elem, m_openings, m_convertErrors, m_convertOptions);
				if(currbElem.surfaces().empty()) {
					++noSurfConstr;
					Logger::instance() << "no surfaces constr type=" << (int)elems.first
									   << " id=" << currbElem.m_id
									   << " ifcTag=#" << e->m_tag
									   << " name='" << currbElem.m_name << "'";
					m_buildingElements.m_elementsWithoutSurfaces.push_back(m_buildingElements.m_constructionElements.back());
				}
			}
			else if(isConstructionSimilarType(elems.first)) {
				std::shared_ptr<BuildingElement> bElem(new BuildingElement(GUID_maker::instance().guid()));
				if(!bElem->set(e, elems.first, lengthFactor)) {
					++setFailSimilar;
					Logger::instance() << "set() FAILED similar type=" << (int)elems.first
									   << " ifcTag=#" << e->m_tag << " guid=" << elem->m_entity_guid;
					continue;
				}

				m_buildingElements.m_constructionSimilarElements.push_back(bElem);
				BuildingElement& currbElem = *m_buildingElements.m_constructionSimilarElements.back();

				currbElem.update(elem, m_openings, m_convertErrors, m_convertOptions);
				if(currbElem.surfaces().empty()) {
					++noSurfSimilar;
					Logger::instance() << "no surfaces similar type=" << (int)elems.first
									   << " id=" << currbElem.m_id
									   << " ifcTag=#" << e->m_tag
									   << " name='" << currbElem.m_name << "'";
					m_buildingElements.m_elementsWithoutSurfaces.push_back(m_buildingElements.m_constructionSimilarElements.back());
				}
			}
			else if(isOpeningType(elems.first)) {
				std::shared_ptr<BuildingElement> bElem(new BuildingElement(GUID_maker::instance().guid()));
				if(!bElem->set(e, elems.first, lengthFactor)) {
					++setFailOpening;
					Logger::instance() << "set() FAILED opening type=" << (int)elems.first
									   << " ifcTag=#" << e->m_tag << " guid=" << elem->m_entity_guid;
					continue;
				}

				m_buildingElements.m_openingElements.push_back(bElem);
				BuildingElement& currbElem = *m_buildingElements.m_openingElements.back();

				currbElem.update(elem, m_openings, m_convertErrors, m_convertOptions);
				if(currbElem.surfaces().empty()) {
					++noSurfOpening;
					Logger::instance() << "no surfaces opening type=" << (int)elems.first
									   << " id=" << currbElem.m_id
									   << " ifcTag=#" << e->m_tag
									   << " name='" << currbElem.m_name << "'";
					m_buildingElements.m_elementsWithoutSurfaces.push_back(m_buildingElements.m_openingElements.back());
				}
			}
			else {
				// All remaining element classes (stairs, railings, ramps, furniture,
				// distribution/transport elements, element components, feature elements,
				// BET_All catch-all, ...) become m_otherElements so the VicIFC 3D model
				// contains every IFC class with geometry. They are NOT construction
				// elements — the matcher and the opening search ignore them.
				// IfcOpeningElement voids are excluded: they are cut geometry, not
				// visible objects (handled via m_openingsShape).
				if(dynamic_pointer_cast<IfcOpeningElement>(e) != nullptr)
					continue;

				std::shared_ptr<BuildingElement> bElem(new BuildingElement(GUID_maker::instance().guid()));
				if(!bElem->set(e, elems.first, lengthFactor))
					continue;

				m_buildingElements.m_otherElements.push_back(bElem);
				BuildingElement& currbElem = *m_buildingElements.m_otherElements.back();

				currbElem.update(elem, m_openings, m_convertErrors, m_convertOptions);
				if(currbElem.surfaces().empty()) {
					m_buildingElements.m_elementsWithoutSurfaces.push_back(m_buildingElements.m_otherElements.back());
				}
			}
		}
	}
	Logger::instance() << "updateBuildingElements summary:"
					   << " set()-fails constr=" << setFailConstr
					   << " similar=" << setFailSimilar
					   << " opening=" << setFailOpening
					   << " ; no-surface constr=" << noSurfConstr
					   << " similar=" << noSurfSimilar
					   << " opening=" << noSurfOpening;
}

namespace {

/*! Render a single IFC property value as a string. */
std::string propertyValueString(const Property& prop) {
	switch(prop.m_valueType) {
		case Property::VT_String:	return prop.m_stringValue;
		case Property::VT_Double:	return IBK::val2string(prop.m_doubleValue);
		case Property::VT_Boolean:	return prop.m_boolValue ? "true" : "false";
		case Property::VT_INT:		return IBK::val2string(prop.m_intValue);
		case Property::VT_Bounded:	return IBK::val2string(prop.m_boundedValue.m_setPoint);
		default:					return std::string();
	}
}

} // anonymous namespace

void IFCReader::buildIFCModel() {
	m_ifcModel.m_objects.clear();

	// gather all building elements (spaces are handled separately and excluded here)
	std::vector<std::shared_ptr<BuildingElement> > allElements;
	auto addAll = [&allElements](const std::vector<std::shared_ptr<BuildingElement> >& v) {
		allElements.insert(allElements.end(), v.begin(), v.end());
	};
	addAll(m_buildingElements.m_constructionElements);
	addAll(m_buildingElements.m_constructionSimilarElements);
	addAll(m_buildingElements.m_openingElements);
	addAll(m_buildingElements.m_otherElements);
	// elements without own geometry are still emitted (geometry-less) — layered walls are
	// composite IfcWall objects whose parts (IfcBuildingElementPart) aggregate into them,
	// so the composite is needed as topology parent in the navigation tree
	addAll(m_buildingElements.m_elementsWithoutSurfaces);

	// Lookup from opening id to its geometry: element surfaces carry no openings, so we
	// project each construction element's contained openings onto its faces to cut holes.
	std::map<int, const Opening*> openingById;
	for(const Opening& op : m_openings)
		openingById[op.m_id] = &op;

	std::set<int> emittedIds;
	for(const std::shared_ptr<BuildingElement>& elem : allElements) {
		if(elem == nullptr)
			continue;
		if(!emittedIds.insert(elem->m_id).second)
			continue;	// element listed in more than one collector bucket

		VicIFC::IFCObject obj;
		obj.m_id = (uint64_t)elem->m_id;
		obj.m_guid = elem->m_guid;
		obj.m_ifcType = !elem->m_ifcClassName.empty() ? elem->m_ifcClassName
													  : objectTypeToString(elem->type());
		obj.m_name = elem->m_name;
		// original IFC appearance color (invalid QColor -> stays default in VicIFC::IFCObject)
		if(elem->color().isValid())
			obj.m_color = elem->color();

		// Windows render semi-transparent (glass): keep the authored IFC transparency
		// when present (alpha < 255 from IfcSurfaceStyleRendering), otherwise apply a
		// default glass alpha so windows don't occlude the room behind them.
		if(elem->type() == BET_Window) {
			QColor c = elem->color().isValid() ? elem->color() : QColor(150, 195, 220);
			if(c.alpha() == 255)
				c.setAlpha(120);
			obj.m_color = c;
		}

		// Build the triangle mesh from the element faces with the contained window/door
		// openings cut out as holes (cheap 2D triangulation-with-holes; no 3D CSG, which
		// carve skips on the large (>2000-vertex) wall meshes of real buildings).
		// Elements without geometry are kept as geometry-less structure nodes — they can
		// be topology parents of aggregated parts (layered walls).
		if(!elem->m_originalMesh.empty())
			elem->appendMeshWithOpenings(openingById, m_convertOptions, obj.m_vertexes, obj.m_normals, obj.m_indices);

		// flatten property sets into tags with key "<pset>.<property>"
		for(const auto& psetPair : elem->propertyMap()) {
			for(const auto& propPair : psetPair.second) {
				VicIFC::Tag tag;
				tag.m_key = psetPair.first + "." + propPair.first;
				tag.m_value = propertyValueString(propPair.second);
				obj.m_tags.push_back(tag);
			}
		}

		m_ifcModel.m_objects.push_back(obj);
	}

	Logger::instance() << "buildIFCModel done; objects=" << m_ifcModel.m_objects.size();
}


void IFCReader::updateIFCModelTopology() {
	// --- spatial topology: Site -> Building -> Storey -> Element ---
	// Called AFTER updateStoreys (m_site is populated only then; buildIFCModel itself must
	// run earlier, while the element meshes are still unmodified). Emits one geometry-less
	// IFCObject per spatial structure element so the navigation tree can show the IFC
	// hierarchy; elements reference their storey (or containing wall, for windows/doors)
	// via m_parentId.

	// element GUID -> spatial structure GUID from IfcRelContainedInSpatialStructure,
	// and part GUID -> parent element GUID from IfcRelAggregates (building element
	// parts, e.g. wall layers, are aggregated into their element, not contained in a
	// storey — without this WSHH-class models show thousands of parts as a flat list)
	std::map<std::string, std::string> containedInGuid;
	std::map<std::string, std::string> aggregatedInGuid;
	const std::map<int, shared_ptr<BuildingEntity> >& mapEntities = m_model->getMapIfcEntities();
	for(const auto& entPair : mapEntities) {
		shared_ptr<IFC4X3::IfcRelContainedInSpatialStructure> rel =
				dynamic_pointer_cast<IFC4X3::IfcRelContainedInSpatialStructure>(entPair.second);
		if(rel != nullptr && rel->m_RelatingStructure != nullptr) {
			std::string structGuid = guidFromObject(rel->m_RelatingStructure.get());
			for(const auto& prod : rel->m_RelatedElements) {
				if(prod != nullptr)
					containedInGuid[guidFromObject(prod.get())] = structGuid;
			}
			continue;
		}
		shared_ptr<IFC4X3::IfcRelAggregates> agg =
				dynamic_pointer_cast<IFC4X3::IfcRelAggregates>(entPair.second);
		if(agg != nullptr && agg->m_RelatingObject != nullptr) {
			std::string parentGuid = guidFromObject(agg->m_RelatingObject.get());
			for(const auto& child : agg->m_RelatedObjects) {
				if(child != nullptr)
					aggregatedInGuid[guidFromObject(child.get())] = parentGuid;
			}
		}
	}

	// spatial structure GUID -> IFCObject id, filled while emitting site/buildings/storeys
	std::map<std::string, uint64_t> spatialIdByGuid;
	auto addSpatialObject = [this,&spatialIdByGuid](int id, const std::string& guid, const std::string& name,
			const std::string& ifcType, uint64_t parentId) -> uint64_t {
		VicIFC::IFCObject obj;
		obj.m_id = (uint64_t)id;
		obj.m_parentId = parentId;
		obj.m_guid = guid;
		obj.m_ifcType = ifcType;
		obj.m_name = name.empty() ? ifcType : name;
		m_ifcModel.m_objects.push_back(obj);
		if(!guid.empty())
			spatialIdByGuid[guid] = obj.m_id;
		return obj.m_id;
	};
	const uint64_t siteObjId = addSpatialObject(m_site.m_id, m_site.m_guid, m_site.m_name,
												"site", VicIFC::INVALID_ID_64);
	for(const std::shared_ptr<Building>& building : m_site.m_buildings) {
		if(building == nullptr)
			continue;
		const uint64_t buildingObjId = addSpatialObject(building->m_id, building->m_guid,
														building->m_name, "building", siteObjId);
		for(const std::shared_ptr<BuildingStorey>& storey : building->storeys()) {
			if(storey == nullptr)
				continue;
			addSpatialObject(storey->m_id, storey->m_guid, storey->m_name, "storey", buildingObjId);
		}
	}

	// fallback for windows/doors without own containment: parent = containing wall
	std::map<uint64_t, uint64_t> openingElementParent;	// opening element id -> construction element id
	for(const Opening& op : m_openings) {
		std::vector<int> containing;
		op.insertContainingElementId(containing);
		if(containing.empty())
			continue;
		for(int oeId : op.openingElementIds())
			openingElementParent[(uint64_t)oeId] = (uint64_t)containing.front();
	}

	// GUID -> object id over ALL emitted objects (needed for part -> parent element links)
	std::map<std::string, uint64_t> objectIdByGuid;
	for(const VicIFC::IFCObject& obj : m_ifcModel.m_objects) {
		if(!obj.m_guid.empty())
			objectIdByGuid[obj.m_guid] = obj.m_id;
	}

	int assigned = 0;
	for(VicIFC::IFCObject& obj : m_ifcModel.m_objects) {
		if(obj.m_parentId != VicIFC::INVALID_ID_64)
			continue;	// spatial objects already carry their parent
		// 1. storey containment
		std::map<std::string, std::string>::const_iterator cit = containedInGuid.find(obj.m_guid);
		if(cit != containedInGuid.end()) {
			std::map<std::string, uint64_t>::const_iterator sit = spatialIdByGuid.find(cit->second);
			if(sit != spatialIdByGuid.end() && sit->second != obj.m_id) {
				obj.m_parentId = sit->second;
				++assigned;
				continue;
			}
		}
		// 2. aggregation (building element parts -> their element)
		std::map<std::string, std::string>::const_iterator ait = aggregatedInGuid.find(obj.m_guid);
		if(ait != aggregatedInGuid.end()) {
			std::map<std::string, uint64_t>::const_iterator oit = objectIdByGuid.find(ait->second);
			if(oit != objectIdByGuid.end() && oit->second != obj.m_id) {
				obj.m_parentId = oit->second;
				++assigned;
				continue;
			}
		}
		// 3. windows/doors without own containment -> containing wall
		std::map<uint64_t, uint64_t>::const_iterator pit = openingElementParent.find(obj.m_id);
		if(pit != openingElementParent.end()) {
			obj.m_parentId = pit->second;
			++assigned;
		}
	}
	Logger::instance() << "updateIFCModelTopology done; objects=" << m_ifcModel.m_objects.size()
					   << " parentsAssigned=" << assigned;
}

const ConvertOptions &IFCReader::convertOptions() const {
	return m_convertOptions;
}

IBK::Path IFCReader::filename() const {
	if(m_readCompletedSuccessfully)
		return m_filename;

	return IBK::Path();
}

bool IFCReader::hasElementsForSpaceBoundaries(BuildingElementTypes type) const {
	return m_convertOptions.m_elementsForSpaceBoundaries.find(type) != m_convertOptions.m_elementsForSpaceBoundaries.end();
}

void IFCReader::setElementsForSpaceBoundaries(BuildingElementTypes type, bool set) {
	if(set) {
		m_convertOptions.m_elementsForSpaceBoundaries.insert(type);
	}
	else {
		m_convertOptions.m_elementsForSpaceBoundaries.erase(type);
	}
}

void IFCReader::setMatchingDistances(double constructionFactor, double standardWallThickness, double openingDistance) {
	m_convertOptions.m_distanceFactor = constructionFactor;
	m_convertOptions.m_openingDistance = openingDistance;
	m_convertOptions.m_standardWallThickness = standardWallThickness;
}

void IFCReader::addNoSearchForOpenings(const QSet<BuildingElementTypes>& types) {
	m_convertOptions.addElementsForOpenings(types);
}

void IFCReader::setWritingBuildingElements(bool constructions, bool buildingElements, bool openings, bool other) {
	m_convertOptions.m_writeConstructionElements = constructions;
	m_convertOptions.m_writeBuildingElements = buildingElements;
	m_convertOptions.m_writeOpeningElements = openings;
	m_convertOptions.m_writeOtherElements = other;
}

void IFCReader::setWriteShadingObjects(bool construction, bool similar, bool opening, bool other) {
	m_convertOptions.m_writeShadingConstruction = construction;
	m_convertOptions.m_writeShadingSimilar = similar;
	m_convertOptions.m_writeShadingOpening = opening;
	m_convertOptions.m_writeShadingOther = other;
}

void IFCReader::setMergeShadingCoplanarFaces(bool enabled) {
	m_convertOptions.m_mergeShadingCoplanarFaces = enabled;
}

void IFCReader::setMinimumCheckValues(double minimumDistance, double minimumArea, double polygonEpsilon) {
	m_convertOptions.m_distanceEps = minimumDistance;
	m_convertOptions.m_minimumSurfaceArea = minimumArea;
	m_convertOptions.m_polygonEps = polygonEpsilon;
}

void IFCReader::setUseCSGForOpenings(bool useCSG) {
	m_convertOptions.m_useCSGForOpenings = useCSG;
}

void IFCReader::setSurfaceWritingMode(bool oldStyle) {
	m_convertOptions.m_useOldPolygonWriting = oldStyle;
}


bool IFCReader::convert(bool useSpaceBoundaries, IBK::NotificationHandler* notify) {

	if(!m_readCompletedSuccessfully) {
		m_errorText = "Cannot convert data because file not readed";
		return false;
	}

	if(notify)
		notify->notify(0.0, "Start converting");

	Logger::instance().beginStep("convert-start");
	Logger::instance() << "start convert; useSpaceBoundaries=" << (useSpaceBoundaries ? 1 : 0);

	clearConvertData();

	m_useSpaceBoundaries = useSpaceBoundaries;

	bool subtractOpenings = false;

	// get project
	Logger::instance().beginStep("get-project");
	const std::map<int, shared_ptr<BuildingEntity> >& map_entities = m_model->getMapIfcEntities();
	Logger::instance() << "map_entities size=" << map_entities.size();
	for (auto it = map_entities.begin(); it != map_entities.end(); ++it) {
		shared_ptr<BuildingEntity> obj = it->second;
		if (obj) {
			shared_ptr<IfcObjectDefinition> object_def = dynamic_pointer_cast<IfcObjectDefinition>(obj);
			if (object_def) {
				if( object_def->classID() == IFC4X3::IFCPROJECT ) {
					shared_ptr<IfcProject> ifc_project = dynamic_pointer_cast<IfcProject>(object_def);
					if( ifc_project ) {
						m_project.set(ifc_project);
					}
				}
			}
		}
	}

	try {

		if(notify)
			notify->notify(0.05, "Convert geometry");
		Logger::instance().beginStep("convert-geometry");
		// convert IFC geometric representations into Carve geometry
		const double length_in_meter = m_geometryConverter.getBuildingModel()->getUnitConverter()->getLengthInMeterFactor();
		Logger::instance() << "length_in_meter=" << length_in_meter
						   << " minimumSurfaceArea=" << m_convertOptions.m_minimumSurfaceArea;
		m_geometryConverter.getGeomSettings()->setMinimumSurfaceArea(m_convertOptions.m_minimumSurfaceArea);
		m_geometryConverter.setCsgEps(1.5e-08 * length_in_meter);
		{
			// Relay GeometryConverter progress (StatusCallback PROGRESS_VALUE
			// events emitted from thread 0 every ~2% of objects) to notify, so
			// the GUI thread keeps running processEvents() during this long pass.
			// Without the setMessageCallBack hookup the converter's progress
			// events go nowhere and the dialog freezes at 5%.
			ProgressHandler geomProgress = makeSubRange(notify, 0.05, 0.20);
			m_currentSubProgress = &geomProgress;
			m_geometryConverter.setMessageCallBack(this, &IFCReader::messageTarget);
			try {
				m_geometryConverter.convertGeometry(subtractOpenings, m_convertErrors);
			}
			catch(...) {
				m_geometryConverter.unsetMessageCallBack();
				m_currentSubProgress = nullptr;
				throw;
			}
			m_geometryConverter.unsetMessageCallBack();
			m_currentSubProgress = nullptr;
		}
		Logger::instance() << "convertGeometry done; convertErrors=" << m_convertErrors.size();

		if(notify)
			notify->notify(0.20, "Split shape data");

		Logger::instance().beginStep("split-shape-data");
		splitShapeData();
		Logger::instance() << "shapes: elements=" << m_elementEntitesShape.size()
						   << " buildings=" << m_buildingsShape.size()
						   << " storeys=" << m_storeysShape.size()
						   << " spaces=" << m_spaceEntitesShape.size()
						   << " openings=" << m_openingsShape.size();

		if(notify)
			notify->notify(0.25, "Create openings");

		Logger::instance().beginStep("create-openings");
		m_openings.clear();
		{
			ProgressHandler openingsProgress = makeSubRange(notify, 0.25, 0.35);
			size_t totalOpeningShapes = m_openingsShape.size();
			size_t currOpeningShape = 0;
			for(auto& openShape : m_openingsShape) {
				++currOpeningShape;
				if(notify && totalOpeningShapes > 0)
					openingsProgress.notify(double(currOpeningShape) / double(totalOpeningShapes));

				std::shared_ptr<IfcOpeningElement> o = dynamic_pointer_cast<IfcOpeningElement>(openShape.second->m_ifc_object_definition.lock());
				if(o == nullptr)
					continue;

				Opening opening(GUID_maker::instance().guid());
				if(opening.set(o)) {
					m_openings.push_back(opening);
					m_openings.back().update(openShape.second, m_convertErrors);
				}
			}
		}
		Logger::instance() << "openings created=" << m_openings.size();

		try {
			if(notify)
				notify->notify(0.35, "Update building elements");
			Logger::instance().beginStep("update-building-elements");
			ProgressHandler buildElemProgress = makeSubRange(notify, 0.35, 0.55);
			updateBuildingElements(&buildElemProgress);

			Logger::instance() << "updateBuildingElements done"
							   << " construction=" << m_buildingElements.m_constructionElements.size()
							   << " similar=" << m_buildingElements.m_constructionSimilarElements.size()
							   << " opening=" << m_buildingElements.m_openingElements.size()
							   << " withoutSurfaces=" << m_buildingElements.m_elementsWithoutSurfaces.size();
		}
		catch (std::exception& e) {
			ConvertError err;
			err.m_objectType = OT_Unknown;
			err.m_errorText = "Exception: '" + std::string(e.what()) + "' while converting ifc file.";
			m_convertErrors.push_back(err);
			m_hasError = true;

			return false;
		}
		catch (...) {
			ConvertError err;
			err.m_objectType = OT_Unknown;
			err.m_errorText = "Unknown exception: while converting ifc file.";
			m_convertErrors.push_back(err);
			m_hasError = true;

			return false;
		}

		if(notify)
			notify->notify(0.55, "Set containing elements");
		Logger::instance().beginStep("set-containing-elements");
		{
			ProgressHandler containProgress = makeSubRange(notify, 0.55, 0.60);
			size_t totalOpeningElems = m_buildingElements.m_openingElements.size();
			size_t currOpeningElem = 0;
			for(std::shared_ptr<BuildingElement>& openingElement : m_buildingElements.m_openingElements) {
				++currOpeningElem;
				if(notify && totalOpeningElems > 0)
					containProgress.notify(double(currOpeningElem) / double(totalOpeningElems));
				openingElement->setContainingElements(m_openings);
				openingElement->setContainedConstructionThickesses(m_buildingElements.m_constructionElements);
				openingElement->setContainedConstructionThickesses(m_buildingElements.m_constructionSimilarElements);
			}
		}

		Logger::instance() << "setContainingElements done; openingElements processed="
						   << m_buildingElements.m_openingElements.size();

		for(const auto& elem : m_buildingElements.m_elementsWithoutSurfaces) {
			m_convertErrors.push_back({OT_BuildingElement, elem->m_id, "Building element has no surface"});
		}

		// build the raw IFC model (all building elements with their original mesh) while
		// the element meshes are still available and unmodified
		Logger::instance().beginStep("build-ifc-model");
		buildIFCModel();

		if(notify)
			notify->notify(0.60, "Match openings");
		Logger::instance().beginStep("match-openings");
		{
			ProgressHandler matchProgress = makeSubRange(notify, 0.60, 0.70);
			checkAndMatchOpeningsToConstructions(&matchProgress);
		}
		Logger::instance() << "match-openings done";

		if(notify)
			notify->notify(0.70, "Collect data");
		Logger::instance().beginStep("collect-data");
		m_database.collectData(m_buildingElements);
		Logger::instance() << "collectData done; materials=" << m_database.m_materials.size()
						   << " constructions=" << m_database.m_constructions.size()
						   << " windows=" << m_database.m_windows.size()
						   << " windowGlazings=" << m_database.m_windowGlazings.size();

		if(notify)
			notify->notify(0.72, "Update storeys");
		Logger::instance().beginStep("update-storeys");

		bool siteExist = m_siteShape != nullptr;
		if(m_siteShape == nullptr) {
		// create own site

			// project has buildings --> create new site from this
			if(!m_project.buildingsOriginal().empty()) {
				bool res = m_site.set(m_project);
				if(res)
					res = m_site.set(m_buildingsShape, m_convertErrors);
				siteExist = res;
			}
			else {
				// create side without having a original buildings list directly from shape data
				siteExist = m_site.set(m_buildingsShape, m_convertErrors);
			}
		}
		else {
			std::shared_ptr<IfcSpatialStructureElement> se = std::dynamic_pointer_cast<IfcSpatialStructureElement>(m_siteShape->m_ifc_object_definition.lock());
			m_site.set(se, m_siteShape, m_buildingsShape, m_convertErrors);
		}

		if(siteExist) {
			ProgressHandler storeyProgress = makeSubRange(notify, 0.72, 0.97);
			for(auto& building : m_site.m_buildings) {
				building->fetchStoreys(m_storeysShape, m_spaceEntitesShape, m_site.m_buildings.size() == 1);
				bool res = building->updateStoreys(m_elementEntitesShape, m_spaceEntitesShape, m_geometryConverter.getBuildingModel()->getUnitConverter(),
									   m_buildingElements, m_openings, m_useSpaceBoundaries, m_convertErrors, m_convertOptions, &storeyProgress);
				if( !res) {
					m_convertErrors.push_back({OT_Building, -1, "No connection between building and storeys"});
					m_errorText = "No connection between building and storeys";
					m_hasError = true;
					return false;
				}
			}


		}
		else {
			m_convertErrors.push_back({OT_Site, -1, "No site"});
			m_errorText = "No site";
			m_hasError = true;
			return false;
		}

		Logger::instance() << "updateStoreys done; buildings=" << m_site.m_buildings.size();

		// site/buildings/storeys exist now — attach the spatial hierarchy to the IFC model
		updateIFCModelTopology();

		if(m_repairFlags.m_removeDoubledSBs) {
			Logger::instance().beginStep("remove-doubled-sbs");
			std::vector<std::shared_ptr<Space>> spaces = m_site.allSpaces();

			for(auto space : spaces) {
				space->removeDublicatedSpaceBoundaries(m_convertOptions);
			}
			Logger::instance() << "removeDoubledSBs done; spaces=" << spaces.size();
		}

		if(notify)
			notify->notify(0.97, "Collect component instances");
		Logger::instance().beginStep("collect-component-instances");
		m_instances.collectComponentInstances(m_buildingElements, m_database, m_site, m_convertErrors, m_convertOptions);

		Logger::instance() << "collectComponentInstances done";

		if(notify)
			notify->notify(0.98, "Unify components");
		Logger::instance().beginStep("unify-components");
		m_database.unifyComponents(m_instances);

		if(!m_convertErrors.empty()) {
			m_hasError = true;
//			return false;
		}

		m_convertCompletedSuccessfully = true;

		if(notify)
			notify->notify(1.0, "Convert completed successfully");
		Logger::instance().beginStep("convert-done");
		Logger::instance() << "convert completed successfully; errors=" << m_convertErrors.size()
						   << " hasError=" << (m_hasError ? 1 : 0);

		return true;

	}
	catch (std::exception& e) {
		ConvertError err;
		err.m_objectType = OT_Unknown;
		err.m_errorText = "Exception: '" + std::string(e.what()) + "' while converting ifc file.";
		m_convertErrors.push_back(err);
		m_hasError = true;
		Logger::instance() << "convert exception: " << e.what();

		return false;
	}

	return false;
}

int IFCReader::totalNumberOfIFCEntities() const {
	if(!m_readCompletedSuccessfully)
		return 0;

	return m_model->getMapIfcEntities().size();
}

int IFCReader::numberOfIFCSpaceBoundaries() const {
	if(!m_readCompletedSuccessfully)
		return 0;

	const auto& ifcMap = m_model->getMapIfcEntities();
	int count = 0;
	for(const auto& item : ifcMap) {
		shared_ptr<IfcRelSpaceBoundary> sb = dynamic_pointer_cast<IfcRelSpaceBoundary>(item.second);
		if(sb)
			++count;
	}
	return count;
}

bool IFCReader::checkEssentialIFCs(QString& errmsg, int& buildings, int& spaces) {
	buildings = 0;
	spaces = 0;
	int unknown = 0;
	int storeys = 0;
	bool siteExist = false;
	for(const auto& item : m_model->getMapIfcEntities()) {
		if(dynamic_pointer_cast<IfcSpatialStructureElement>(item.second) != nullptr) {
			if(dynamic_pointer_cast<IfcSite>(item.second) != nullptr) {
				siteExist = true;
			}
			else if(dynamic_pointer_cast<IfcBuilding>(item.second) != nullptr) {
				++buildings;
			}
			else if(dynamic_pointer_cast<IfcSpace>(item.second) != nullptr) {
				++spaces;
			}
			else if(dynamic_pointer_cast<IfcBuildingStorey>(item.second) != nullptr) {
				++storeys;
			}
			else {
				++unknown;
			}
		}
	}
	// if(!siteExist) {
	// 	errmsg = tr("No building site.");
	// 	return false;
	// }
	if(buildings == 0) {
		errmsg = tr("No buildings.");
		return false;
	}
	if(spaces == 0) {
		errmsg = tr("No spaces.");
		return false;
	}
	return true;
}

int IFCReader::checkForEqualSpaceBoundaries(std::vector<std::pair<int,int>>& equalSBs) const {
	equalSBs.clear();
	std::vector<std::shared_ptr<Space>> spaces = m_site.allSpaces();

	for(const auto& space : spaces) {
		space->checkForEqualSpaceBoundaries(equalSBs, m_convertOptions);
	}
	return equalSBs.size();
}

int IFCReader::checkForUniqueSubSurfacesInSpaces(std::vector<std::pair<int,std::vector<int>>>& res) const {
	res.clear();
	std::vector<std::shared_ptr<Space>> spaces = m_site.allSpaces();

	for(const auto& space : spaces) {
		std::vector<int> subRes = space->checkUniqueSubSurfaces();
		if(!subRes.empty())
			res.push_back({space->m_ifcId, subRes});
	}
	return res.size();
}

std::set<std::pair<int,int>> IFCReader::checkForIntersectedSpace() const {
	std::set<std::pair<int,int>> res;
	std::vector<std::shared_ptr<Space>> spaces = m_site.allSpaces();
	if(spaces.size() < 2)
		return res;

	for(size_t i=0; i<spaces.size()-1; ++i) {
		for(size_t j=i+1; j<spaces.size(); ++j) {
			if(spaces[i]->isIntersected(*spaces[j], m_convertOptions))
				res.insert({spaces[i]->m_ifcId,spaces[j]->m_ifcId});
		}
	}
	return res;
}

std::set<std::pair<int, int> > IFCReader::checkForSpaceWithSameSpaceBoundaries() const {
	std::set<std::pair<int,int>> res;
	std::vector<std::shared_ptr<Space>> spaces = m_site.allSpaces();
	if(spaces.size() < 2)
		return res;

	for(size_t i=0; i<spaces.size()-1; ++i) {
		for(size_t j=i+1; j<spaces.size(); ++j) {
			if(spaces[i]->shareSameSpaceBoundary(*spaces[j]))
				res.insert({spaces[i]->m_ifcId,spaces[j]->m_ifcId});
		}
	}
	return res;
}

std::vector<int> IFCReader::checkForWrongSurfaceIds() {
	return m_instances.checkForWrongSurfaceIds(m_site);
}

int IFCReader::checkForNotRelatedOpenings() const {
	int count = 0;
	for(const Opening& op : m_openings) {
		if(!op.hasSpaceBoundary())
			++count;
	}
	return count;
}

bool IFCReader::removeDoubledSBs() const {
	return m_repairFlags.m_removeDoubledSBs;
}

void IFCReader::setRemoveDoubledSBs(bool removeDoubledSBs) {
	m_repairFlags.m_removeDoubledSBs = removeDoubledSBs;
}

QString IFCReader::nameForId(int id, Name_Id_Type type) const {
	switch(type) {
		case NIT_Space: {
			const Space* sp = m_site.spaceWithIfcId(id);
			if(sp != nullptr) {
				if(sp->m_longName.empty())
						return QString::fromStdString(sp->m_name);
				return QString::fromStdString(sp->m_longName);
			}
		}
		case NIT_SpaceBoundary: {
			const SpaceBoundary* sp = m_site.spaceBoundaryWithId(id);
			if(sp != nullptr)
				return QString::fromStdString(sp->m_name);
		}
	}
	return QString();
}


VICUS::Project IFCReader::buildVicusProject() const {
	VICUS::Project project;
	std::map<int,int> idMap;
	m_database.addToVicusProject(&project, idMap);
	m_site.addToVicusProject(&project, m_convertOptions);
	m_instances.addToVicusProject(&project, m_database, idMap);

	const bool anyShading = m_convertOptions.m_writeShadingConstruction
							|| m_convertOptions.m_writeShadingSimilar
							|| m_convertOptions.m_writeShadingOpening
							|| m_convertOptions.m_writeShadingOther;
	if(anyShading) {
		const size_t shadingStart = project.m_shadingObjects.size();
		auto appendShadingObjects = [&](const std::vector<std::shared_ptr<BuildingElement>>& elements,
										const char* label) {
			size_t addedObjects = 0;
			size_t addedSurfaces = 0;
			for(const std::shared_ptr<BuildingElement>& be : elements) {
				if(!be)
					continue;
				VICUS::ShadingObject so = be->getVicusShadingObject(m_convertOptions);
				if(so.m_id == INVALID_ID)
					continue;
				addedSurfaces += so.m_surfaces.size();
				project.m_shadingObjects.push_back(so);
				++addedObjects;
			}
			Logger::instance() << "shadingExport " << label
							   << " elements=" << elements.size()
							   << " exported=" << addedObjects
							   << " surfaces=" << addedSurfaces;
		};
		if(m_convertOptions.m_writeShadingConstruction)
			appendShadingObjects(m_buildingElements.m_constructionElements, "construction");
		if(m_convertOptions.m_writeShadingSimilar)
			appendShadingObjects(m_buildingElements.m_constructionSimilarElements, "similar");
		if(m_convertOptions.m_writeShadingOpening)
			appendShadingObjects(m_buildingElements.m_openingElements, "opening");
		if(m_convertOptions.m_writeShadingOther)
			appendShadingObjects(m_buildingElements.m_otherElements, "other");
		Logger::instance() << "shadingExport total shading objects added="
						   << (project.m_shadingObjects.size() - shadingStart);
	}

	// Hand the raw IFC geometry over to VICUS: write the collected VicIFC model to a
	// side-car .vicifc file next to the imported IFC file and reference it from a
	// VICUS::IFCDrawing. The plugin interface only transfers the project as XML text,
	// so the mesh travels as the .vicifc file that SIM-VICUS loads via IFCDrawing::m_filepath.
	if(!m_ifcModel.m_objects.empty() && !m_filename.str().empty()) {
		// Populate the object-id map first: nextUnusedID() reads m_objectPtr, which is only
		// filled by updatePointers(). Without this the IFC nav-tree node ids start too low
		// and collide with the shading-object / surface ids (Duplicate ID on load).
		project.updatePointers();
		unsigned int nextID = project.nextUnusedID();

		VICUS::IFCDrawing drawing;
		drawing.m_id = nextID++;
		// copy the raw model into the drawing's PImpl wrapper (VicIFC::ModelForward)
		*drawing.m_data = m_ifcModel;

		// write the mesh to a side-car .vicifc file alongside the IFC source file
		IBK::Path vicifcFile(m_filename.withoutExtension().str() + ".vicifc");
		drawing.m_filepath = vicifcFile;
		drawing.writeVicIFC(vicifcFile);

		// create the nav-tree nodes for the individual IFC objects
		drawing.syncObjectNodes(nextID);
		drawing.updateParents();

		project.m_ifc.m_id = nextID++;
		project.m_ifc.m_drawings.push_back(drawing);

		Logger::instance() << "IFC geometry handover: wrote " << vicifcFile.str()
						   << " (" << m_ifcModel.m_objects.size() << " objects)";
	}

	// Structured room-geometry post-processing: drop duplicate surfaces, repair
	// winding, close remaining shell holes (per-room safety net inside).
	healRooms(project);

	return project;
}

void IFCReader::writeXML(const IBK::Path & filename) const {
	VICUS::Project project = buildVicusProject();
	project.writeXML(filename);
}


void IFCReader::setVicusProjectText(QString& projectText) {
	VICUS::Project project = buildVicusProject();
	projectText = project.writeXMLText();
}


struct SpaceBoundaryEvaluation {
	enum Type {
		Construction,
		Opening,
		Virtual,
		Missing,
		Unknown
	};
	Type		m_type;
	QString	m_name;
	QString	m_nameRelatedElement;
	QString	m_nameRelatedSpace;
	BuildingElementTypes	m_typeRelatedElement;

	static QString typeString(Type type) {
		switch(type) {
			case Construction: return "Construction element";
			case Opening: return "Openening element";
			case Virtual: return "Virtual";
			case Missing: return "Missing";
			case Unknown: return "Unknown connection";
		}
	}
};

QStringList IFCReader::messages() const {
	QStringList result;
	result << tr("Messages:");
	std::vector<std::shared_ptr<SpaceBoundary>> spaceBoundaries = m_site.allSpaceBoundaries();

	size_t sbCount = spaceBoundaries.size();
	if(sbCount > 0) {
		int sbConstruction = 0;
		int sbOpenings = 0;
		int sbMissing = 0;
		int sbVirtual = 0;
		for(const auto& sb : spaceBoundaries) {
			if(sb->isConstructionElement())
				++sbConstruction;
			if(sb->isOpeningElement())
				++sbOpenings;
			if(sb->isMissing())
				++sbMissing;
			if(sb->isVirtual())
				++sbVirtual;
		}
		result << tr("%1 space boundaries.").arg(sbCount);
		if(sbConstruction > 0)
			result << tr("%1 connected with construction elements").arg(sbConstruction);
		if(sbOpenings > 0)
			result << tr("%1 connected with opening elements").arg(sbOpenings);
		if(sbMissing > 0)
			result << tr("%1 without connection").arg(sbMissing);
		if(sbVirtual > 0)
			result << tr("%1 virtual surfaces").arg(sbVirtual);
	}
	else {

	}
	return result;
}

QStringList IFCReader::statistic() const {
	QStringList result;

	// --- Overview ---
	result << tr("Overview:<br>");
	result << tr("%1 buildings.<br>").arg(m_site.m_buildings.size());
	int totalStoreys = 0;
	int totalSpaces = 0;
	int totalSpaceBoundaries = 0;
	for(const auto& building : m_site.m_buildings) {
		totalStoreys += (int)building->storeys().size();
		for(const auto& storey : building->storeys()) {
			totalSpaces += (int)storey->spaces().size();
			for(const auto& space : storey->spaces())
				totalSpaceBoundaries += (int)space->spaceBoundaries().size();
		}
	}
	result << tr("%1 storeys.<br>").arg(totalStoreys);
	result << tr("%1 spaces.<br>").arg(totalSpaces);
	result << tr("%1 space boundaries.<br>").arg(totalSpaceBoundaries);
	result << tr("%1 materials.<br>").arg(m_database.m_materials.size());
	result << tr("%1 constructions.<br>").arg(m_database.m_constructions.size());
	result << tr("%1 windows.<br>").arg(m_database.m_windows.size());
	result << tr("%1 window glazings.<br>").arg(m_database.m_windowGlazings.size());

	// --- Detailed building structure ---
	result << tr("<br>Details:<br>");
	for(const auto& building : m_site.m_buildings) {
		result << tr("Building %1 with %2 storeys.<br>").arg(QString::fromStdString(building->m_name))
				.arg(building->storeys().size());
		for(const auto& storey : building->storeys()) {
			result << tr("  Storey %1 with %2 spaces.<br>").arg(QString::fromStdString(storey->m_name))
					.arg(storey->spaces().size());
			for(const auto& space : storey->spaces()) {
				result << tr("    Space %1 with %2 space boundaries.<br>")
						  .arg(QString::fromStdString(space->m_name+" - "+space->m_longName))
						  .arg(space->spaceBoundaries().size());
				for(const auto& sb : space->spaceBoundaries()) {
					if(sb->isMissing()) {
						result << tr("    Space boundary %1 with missing connection.<br>").arg(QString::fromStdString(sb->m_name));
					}
					else if(sb->isVirtual()) {
						result << tr("    Space boundary %1 is virtual.<br>").arg(QString::fromStdString(sb->m_name));
					}
					else {
						result << tr("    Space boundary %1 with %2 subsurfaces.<br>").arg(QString::fromStdString(sb->m_name))
								.arg(sb->containedOpeningSpaceBoundaries().size());
					}
				}
			}
		}
	}

	// --- Detailed database contents ---
	result << tr("<br>Databases:<br>");
	result << tr("  Materials:<br>");
	for(const auto& mat : m_database.m_materials) {
		result << tr("    %1 - id %2<br>").arg(QString::fromStdString(mat.second.m_name)).arg(mat.first);
	}
	result << tr("  Constructions:<br>");
	for(const auto& con : m_database.m_constructions) {
		result << tr("    Construction id %1 with %2 layers<br>").arg(con.first).arg(con.second.m_layers.size());
	}
	result << tr("  Windows:<br>");
	for(const auto& win : m_database.m_windows) {
		result << tr("    Window %1 id %2<br>").arg(QString::fromStdString(win.second.m_name)).arg(win.first);
	}
	result << tr("  Window glazings:<br>");
	for(const auto& wgl : m_database.m_windowGlazings) {
		result << tr("    Window glazing %1 id %2<br>").arg(QString::fromStdString(wgl.second.m_name)).arg(wgl.first);
	}

	// --- Space boundary list ---
	result << QString() << tr("<br>Space boundary list:<br>") << QString();

	std::vector<std::shared_ptr<SpaceBoundary>> spaceBoundaries = m_site.allSpaceBoundaries();
	for(const auto& sb : spaceBoundaries) {
		QString text = QString::fromStdString(sb->m_name)
			+ "\tis a " + QString::fromStdString(objectTypeToString(sb->typeRelatedElement()))
			+ "  connected with: " + QString::fromStdString(sb->nameRelatedElement())
			+ "  contained in: " + QString::fromStdString(sb->nameRelatedSpace());
		result << text + "<br>";
	}

	return result;
}

const std::vector<ConvertError>& IFCReader::convertErrors() const {
	return m_convertErrors;
}

void IFCReader::messageTarget( void* ptr, shared_ptr<StatusCallback::Message> m ) {
	if(ptr == nullptr || !m)
		return;

	IFCReader* myself = reinterpret_cast<IFCReader*>(ptr);

	// Relay progress events to the active sub-range handler so the GUI keeps
	// pumping events during long ifcplusplus passes (STEP parsing, geometry
	// conversion). Without this, those phases run without any notify callback
	// and the progress dialog freezes (e.g. stuck at 5% "Convert geometry").
	if(m->m_message_type == StatusCallback::MESSAGE_TYPE_PROGRESS_VALUE) {
		if(myself->m_currentSubProgress != nullptr && m->m_progress_value >= 0.0)
			myself->m_currentSubProgress->notify(m->m_progress_value);
		return;
	}

	std::string reporting_function_str( m->m_reporting_function );
	std::string position;
	if( m->m_entity ) {
		position = "IFC entity: #" + std::to_string(m->m_entity->m_tag) + "=" + std::to_string(m->m_entity->classID());
	}
	if(m->m_message_type == StatusCallback::MESSAGE_TYPE_ERROR) {
		myself->m_hasError = true;
		myself->m_errorText = "Error from: " + reporting_function_str + " in " + position;
		myself->m_errorText += m->m_message_text + "\n";
	}
	if(m->m_message_type == StatusCallback::MESSAGE_TYPE_WARNING) {
		myself->m_hasWarning = true;
		myself->m_warningText = "Error from: " + reporting_function_str + " in " + position;
		myself->m_warningText += m->m_message_text + "\n";
	}
}

bool IFCReader::typeByGuid(const std::string& guid, std::pair<BuildingElementTypes,std::shared_ptr<ProductShapeData>>& res) {
	for(const auto& elemType : m_elementEntitesShape) {
		for(const auto& elem : elemType.second) {
			if(guid == elem->m_entity_guid) {
				res = std::pair<BuildingElementTypes,std::shared_ptr<ProductShapeData>>(elemType.first,elem);
				return true;
			}
		}
	}
	return false;
}

void IFCReader::checkAndMatchOpeningsToConstructions(IBK::NotificationHandler* notify) {
	Logger::instance() << "checkAndMatchOpeningsToConstructions;"
					   << " openings=" << m_openings.size()
					   << " candidateElements=" << m_buildingElements.m_openingElements.size()
					   << " openingDistance=" << m_convertOptions.m_openingDistance
					   << " distanceEps=" << m_convertOptions.m_distanceEps;

	const std::ptrdiff_t totalOpenings = static_cast<std::ptrdiff_t>(m_openings.size());

	// Match-outcome tallies (tell us *why* matching succeeds or fails). Aggregated
	// via OpenMP reduction so the counters are race-free without atomics.
	size_t preMatched = 0;         // already linked via IFC relations
	size_t matched = 0;            // matched by distance+intersection here
	size_t unmatched = 0;          // no candidate accepted
	size_t emptyOpeningSurfs = 0;  // opening has no surfaces -> cannot match
	size_t noCandidates = 0;       // unmatched AND no element's plane was parallel within eps
	size_t distTooFar = 0;         // unmatched AND best parallel-plane distance > openingDistance
	size_t notIntersected = 0;     // unmatched AND a parallel candidate existed within distance but none intersected
	size_t processed = 0;          // for progress notification

	// Precompute one AABB per candidate window/door element BEFORE the parallel loop
	// (also avoids concurrent lazy-init of the Surface AABB caches). Composite
	// elements without own body (WSHH windows: IfcWindow aggregating Aluminium/
	// Normalglas parts) get their AABB from the aggregated part geometry.
	std::map<int, std::pair<IBKMK::Vector3D, IBKMK::Vector3D>> elementAabbs;
	for(const auto& elem : m_buildingElements.m_openingElements) {
		IBKMK::Vector3D emin(1e20,1e20,1e20), emax(-1e20,-1e20,-1e20);
		bool any = false;
		auto expand = [&](const std::vector<Surface>& surfs) {
			for(const Surface& s : surfs) {
				const IBKMK::Vector3D& a = s.aabbMin();
				const IBKMK::Vector3D& b = s.aabbMax();
				emin.m_x = std::min(emin.m_x, a.m_x); emin.m_y = std::min(emin.m_y, a.m_y); emin.m_z = std::min(emin.m_z, a.m_z);
				emax.m_x = std::max(emax.m_x, b.m_x); emax.m_y = std::max(emax.m_y, b.m_y); emax.m_z = std::max(emax.m_z, b.m_z);
				any = true;
			}
		};
		expand(elem->surfaces());
		for(const auto& part : elem->elementParts()) {
			if(!part)
				continue;
			std::shared_ptr<BuildingElement> partElem = m_buildingElements.fromGUID(guidFromObject(part.get()));
			if(partElem)
				expand(partElem->surfaces());
		}
		if(any)
			elementAabbs[elem->m_id] = {emin, emax};
	}

	// Each iteration only mutates the current Opening (addOpeningElementId), shared
	// data (m_buildingElements.m_openingElements, m_convertOptions) is read-only.
	// Thread budget: leave 2 cores free for OS/GUI on machines with >=4 cores.
	const int numProcs = omp_get_num_procs();
	const int numThreads = (numProcs >= 4) ? (numProcs - 2)
	                     : (numProcs >= 2) ? (numProcs - 1)
	                                       : 1;

	#pragma omp parallel for schedule(dynamic) num_threads(numThreads) \
		reduction(+:preMatched,matched,unmatched,emptyOpeningSurfs, \
		            noCandidates,distTooFar,notIntersected,processed)
	for(std::ptrdiff_t i = 0; i < totalOpenings; ++i) {
		Opening& opening = m_openings[static_cast<size_t>(i)];

		++processed;
		// Notify must run on the GUI thread (Qt processEvents); serialize.
		#pragma omp critical(ifcc_match_openings_notify)
		{
			if(notify != nullptr && totalOpenings > 0)
				notify->notify(double(processed) / double(totalOpenings));
		}

		if(opening.isConnectedToOpeningElement()) {
			++preMatched;
			continue;
		}

		if(opening.surfaces().empty()) {
			++emptyOpeningSurfs;
			++unmatched;
			#pragma omp critical(ifcc_logger)
			{
				Logger::instance() << "opening has no surfaces; id=" << opening.m_id
								   << " ifcTag=#" << opening.m_ifcId
								   << " name='" << opening.m_name << "'";
			}
			continue;
		}

		double currDist = 1e20;
		int constructionId = -1;
		// Track why we rejected candidates, so unmatched openings are diagnosable:
		double bestParallelDist = 1e20; // min distance seen for any parallel pair
		bool anyParallelWithinRange = false;
		bool anyIntersectedWithinRange = false;
		// loop over all opening element constructions
		for(const auto& elem : m_buildingElements.m_openingElements) {
			for(size_t cosi=0; cosi<opening.surfaces().size(); ++cosi) {
				const Surface& currentOpeningSurf = opening.surfaces()[cosi];

				for(const Surface& constructionSurf : elem->surfaces()) {
					double dist = currentOpeningSurf.distanceToParallelPlane(constructionSurf, m_convertOptions.m_distanceEps);
					// dist == 1e20 means planes are not parallel — ignore for diagnostics
					if(dist < bestParallelDist)
						bestParallelDist = dist;

					if(dist > m_convertOptions.m_openingDistance)
						continue;

					anyParallelWithinRange = true;
					bool intersected = constructionSurf.isIntersected(currentOpeningSurf);
					if(intersected) {
						anyIntersectedWithinRange = true;
						if(dist < currDist) {
							currDist = dist;
							constructionId = elem->m_id;
						}
					}
				} // construction surface loop
			} // opening surface loop

		} // building element id loop

		if(constructionId == -1) {
			// AABB containment fallback. Arched/curved opening bodies (revolved solids,
			// e.g. WSHH round-arch windows) have no planar face parallel to the window
			// element, so the per-face tests above fail even though the window sits
			// geometrically inside the opening. Match by axis-aligned bounding boxes:
			// accept the element whose AABB overlaps the opening AABB the most,
			// requiring at least half of the element volume inside.
			IBKMK::Vector3D omin(1e20,1e20,1e20), omax(-1e20,-1e20,-1e20);
			for(const Surface& os : opening.surfaces()) {
				const IBKMK::Vector3D& a = os.aabbMin();
				const IBKMK::Vector3D& b = os.aabbMax();
				omin.m_x = std::min(omin.m_x, a.m_x); omin.m_y = std::min(omin.m_y, a.m_y); omin.m_z = std::min(omin.m_z, a.m_z);
				omax.m_x = std::max(omax.m_x, b.m_x); omax.m_y = std::max(omax.m_y, b.m_y); omax.m_z = std::max(omax.m_z, b.m_z);
			}
			double bestScore = 0.0;
			int bestId = -1;
			for(const auto& ea : elementAabbs) {
				const IBKMK::Vector3D& emin = ea.second.first;
				const IBKMK::Vector3D& emax = ea.second.second;
				double ox = std::min(omax.m_x, emax.m_x) - std::max(omin.m_x, emin.m_x);
				double oy = std::min(omax.m_y, emax.m_y) - std::max(omin.m_y, emin.m_y);
				double oz = std::min(omax.m_z, emax.m_z) - std::max(omin.m_z, emin.m_z);
				if(ox <= 0.0 || oy <= 0.0 || oz <= 0.0)
					continue;
				double ex = std::max(1e-4, emax.m_x - emin.m_x);
				double ey = std::max(1e-4, emax.m_y - emin.m_y);
				double ez = std::max(1e-4, emax.m_z - emin.m_z);
				double score = (ox*oy*oz) / (ex*ey*ez);
				if(score > bestScore) {
					bestScore = score;
					bestId = ea.first;
				}
			}
			if(bestScore >= 0.5) {
				constructionId = bestId;
				#pragma omp critical(ifcc_logger)
				{
					Logger::instance() << "opening matched by AABB containment; id=" << opening.m_id
									   << " name='" << opening.m_name << "'"
									   << " elementId=" << bestId
									   << " overlapRatio=" << bestScore;
				}
			}
		}

		if(constructionId > -1) {
			opening.addOpeningElementId(constructionId);
			++matched;
		}
		else {
			++unmatched;
			const char* reason;
			if(!anyParallelWithinRange && bestParallelDist >= 1e19) {
				reason = "no-parallel-candidate";
				++noCandidates;
			}
			else if(!anyParallelWithinRange) {
				reason = "parallel-but-distance-too-far";
				++distTooFar;
			}
			else {
				reason = "parallel-in-range-but-no-intersection";
				++notIntersected;
			}
			// Recompute the best AABB score purely for diagnostics (the fallback above
			// already rejected it as < 0.5) — tells us whether the element geometry is
			// missing entirely (score 0) or the boxes merely overlap too little.
			double dbgBestScore = 0.0;
			int dbgBestId = -1;
			{
				IBKMK::Vector3D omin2(1e20,1e20,1e20), omax2(-1e20,-1e20,-1e20);
				for(const Surface& os : opening.surfaces()) {
					const IBKMK::Vector3D& a = os.aabbMin();
					const IBKMK::Vector3D& b = os.aabbMax();
					omin2.m_x = std::min(omin2.m_x, a.m_x); omin2.m_y = std::min(omin2.m_y, a.m_y); omin2.m_z = std::min(omin2.m_z, a.m_z);
					omax2.m_x = std::max(omax2.m_x, b.m_x); omax2.m_y = std::max(omax2.m_y, b.m_y); omax2.m_z = std::max(omax2.m_z, b.m_z);
				}
				for(const auto& ea : elementAabbs) {
					const IBKMK::Vector3D& emin = ea.second.first;
					const IBKMK::Vector3D& emax = ea.second.second;
					double ox = std::min(omax2.m_x, emax.m_x) - std::max(omin2.m_x, emin.m_x);
					double oy = std::min(omax2.m_y, emax.m_y) - std::max(omin2.m_y, emin.m_y);
					double oz = std::min(omax2.m_z, emax.m_z) - std::max(omin2.m_z, emin.m_z);
					if(ox <= 0.0 || oy <= 0.0 || oz <= 0.0)
						continue;
					double ex = std::max(1e-4, emax.m_x - emin.m_x);
					double ey = std::max(1e-4, emax.m_y - emin.m_y);
					double ez = std::max(1e-4, emax.m_z - emin.m_z);
					double score = (ox*oy*oz) / (ex*ey*ez);
					if(score > dbgBestScore) { dbgBestScore = score; dbgBestId = ea.first; }
				}
			}
			#pragma omp critical(ifcc_logger)
			{
				Logger::instance() << "unmatched opening id=" << opening.m_id
								   << " ifcTag=#" << opening.m_ifcId
								   << " name='" << opening.m_name << "'"
								   << " surfaces=" << opening.surfaces().size()
								   << " bestParallelDist=" << bestParallelDist
								   << " reason=" << reason
								   << " bestAabbScore=" << dbgBestScore
								   << " bestAabbElemId=" << dbgBestId;
			}
		}
	} // opening loop

	Logger::instance() << "checkAndMatchOpeningsToConstructions summary:"
					   << " preMatched=" << preMatched
					   << " matched=" << matched
					   << " unmatched=" << unmatched
					   << " (emptySurfs=" << emptyOpeningSurfs
					   << " noParallelCandidate=" << noCandidates
					   << " distTooFar=" << distTooFar
					   << " notIntersected=" << notIntersected << ")";
}

} // end namespace
