#include "IFCC_Building.h"

#include <ifcpp/IFC4X3/include/IfcRelAggregates.h>
#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>
#include <ifcpp/IFC4X3/include/IfcBuilding.h>
#include <ifcpp/IFC4X3/include/IfcSpatialStructureElement.h>


#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_ProgressHandler.h"
#include "IFCC_Space.h"
#include "IFCC_BuildingStorey.h"
#include "IFCC_Logger.h"

namespace IFCC {

Building::Building(int id) :
	EntityBase(id)
{}

bool Building::set(std::shared_ptr<IFC4X3::IfcSpatialStructureElement> ifcElement) {
	if(!EntityBase::set(dynamic_pointer_cast<IFC4X3::IfcRoot>(ifcElement)))
		return false;

	const std::vector<weak_ptr<IFC4X3::IfcRelAggregates> >& vec_decomposedBy = ifcElement->m_IsDecomposedBy_inverse;
	for(const auto& contentElement : vec_decomposedBy) {
		if( contentElement.expired() ) {
			continue;
		}
		shared_ptr<IFC4X3::IfcRelAggregates> rel_aggregates( contentElement );
		if( rel_aggregates ) {
			const std::vector<shared_ptr<IFC4X3::IfcObjectDefinition> >& vec_related_objects = rel_aggregates->m_RelatedObjects;
			for(const auto& contObj : vec_related_objects) {
				if( contObj ) {
					shared_ptr<IFC4X3::IfcBuildingStorey> storey = std::dynamic_pointer_cast<IFC4X3::IfcBuildingStorey>(contObj);
					if(storey != nullptr)
						m_storeysOriginal.push_back(storey);

					shared_ptr<IFC4X3::IfcSpace> space = std::dynamic_pointer_cast<IFC4X3::IfcSpace>(contObj);
					if(space != nullptr)
						m_spacesOriginal.push_back(space);
				}
			}
		}
	}

	return true;
}

void Building::fetchStoreys(const objectShapeGUIDMap_t& storeys, const objectShapeGUIDMap_t& spaces, bool onlyOne) {
	if(storeys.empty()) {
		std::shared_ptr<BuildingStorey> storey = std::shared_ptr<BuildingStorey>(new BuildingStorey(GUID_maker::instance().guid()));
		if(m_spacesOriginal.empty()) {
			if(storey->set(spaces)) {
				storey->m_name = "Only one storey";
				m_storeys.push_back(storey);
			}
		}
		else {
			if(storey->set(m_spacesOriginal)) {
				storey->m_name = "Only one storey";
				m_storeys.push_back(storey);
			}
		}
	}
	else {
		for(const auto& shape : storeys) {
			if(!m_storeysOriginal.empty()) {
				for(const auto& opOrg : m_storeysOriginal) {
					std::string guid = guidFromObject(opOrg.get());
					if(shape.first == guid) {
						std::shared_ptr<BuildingStorey> storey = std::shared_ptr<BuildingStorey>(new BuildingStorey(GUID_maker::instance().guid()));
						if(storey->set(opOrg)) {
							m_storeys.push_back(storey);
						}
						break;
					}
				}
			}
			else {
				if(onlyOne) {
					std::shared_ptr<BuildingStorey> storey = std::shared_ptr<BuildingStorey>(new BuildingStorey(GUID_maker::instance().guid()));
					std::shared_ptr<IFC4X3::IfcSpatialStructureElement> se = std::dynamic_pointer_cast<IFC4X3::IfcSpatialStructureElement>(shape.second->m_ifc_object_definition.lock());
					if(se != nullptr) {
						storey->set(se);
						m_storeys.push_back(storey);
					}
				}
			}
		}
	}
}

bool Building::updateStoreys(const objectShapeTypeVector_t& elementShapes,
							 const objectShapeGUIDMap_t& spaceShapes,
							 shared_ptr<UnitConverter>& unit_converter,
							 const BuildingElementsCollector& buildingElements,
							 std::vector<Opening>& openings,
							 bool useSpaceBoundaries,
							 std::vector<ConvertError>& errors,
							 const ConvertOptions& convertOptions,
							 IBK::NotificationHandler* notify) {

	if(m_storeys.empty()) {
		errors.push_back(ConvertError{OT_Building, m_id, "Building id '" + std::to_string(m_ifcId) + "' has no storeys"});
		return false;
	}
	size_t n = m_storeys.size();
	for(size_t si = 0; si < n; ++si) {
		// Give each storey its own sub-range so progress never jumps backwards
		double rangeStart = double(si) / double(n);
		double rangeEnd   = double(si + 1) / double(n);
		std::string label = "Storey " + std::to_string(si+1) + "/" + std::to_string(n);

		if(notify)
			notify->notify(rangeStart, label.c_str());

		m_storeys[si]->fetchSpaces(spaceShapes, unit_converter, errors);

		if(notify) {
			ProgressHandler storeyHandler([notify](int v, QString t) {
				// Keep the QByteArray alive for the duration of the notify call —
				// otherwise its backing buffer is freed before notify reads from it.
				QByteArray utf8 = t.toUtf8();
				const char* text = t.isEmpty() ? nullptr : utf8.constData();
				notify->notify(double(v) / 100.0, text);
			}, rangeStart, rangeEnd);
			m_storeys[si]->updateSpaces(elementShapes, unit_converter, buildingElements,
										openings, useSpaceBoundaries, errors, convertOptions, &storeyHandler);
		}
		else {
			m_storeys[si]->updateSpaces(elementShapes, unit_converter, buildingElements,
										openings, useSpaceBoundaries, errors, convertOptions, nullptr);
		}
	}

	// Cross-space fallback. Per-space matching sees only its own SBs and commits on
	// real geometric intersection. Openings whose geometry is in a wall shared across
	// rooms (curtainwalls) may miss every room's SB slice, or their IFC relationship
	// may point to a wall whose SB's polygon doesn't contain the opening. This global
	// sweep considers every construction SB in the building.
	//
	// Pass A: strict 2D intersection only (no coplanar-accept). Picks the room whose
	// SB polygon actually geometrically contains the opening.
	// Pass B: if Pass A finds nothing anywhere in the building, fall back to coplanar-
	// accept — the opening lies in some wall's plane but no room's slice contains it.
	// Coplanar-accept is deferred to this global scope so first-processed-room bias
	// can't win over a better geometric match.
	size_t matchedStrict = 0, matchedCoplanar = 0;
	for(Opening& op : openings) {
		if(op.hasSpaceBoundary())
			continue;

		std::shared_ptr<Space> bestSpace;
		Space::OpeningMatchCandidate best;

		// Pass A — strict intersect across every space.
		for(const auto& storey : m_storeys) {
			for(const auto& space : storey->spaces()) {
				Space::OpeningMatchCandidate c = space->findBestOpeningMatch(op, buildingElements, convertOptions,
					/*ignoreContainedOpeningsFilter=*/true, /*allowCoplanarAccept=*/false);
				if(c.area > best.area) {
					best = c;
					bestSpace = space;
				}
			}
		}

		if(bestSpace && best.area > 0.0) {
			bestSpace->commitOpeningMatch(op, best, convertOptions);
			++matchedStrict;
			continue;
		}

		// Pass B — coplanar-accept fallback.
		for(const auto& storey : m_storeys) {
			for(const auto& space : storey->spaces()) {
				Space::OpeningMatchCandidate c = space->findBestOpeningMatch(op, buildingElements, convertOptions,
					/*ignoreContainedOpeningsFilter=*/true, /*allowCoplanarAccept=*/true);
				if(c.area > best.area) {
					best = c;
					bestSpace = space;
				}
			}
		}

		if(bestSpace && best.area > 0.0) {
			bestSpace->commitOpeningMatch(op, best, convertOptions);
			++matchedCoplanar;
		}
	}
	Logger::instance() << "Building::updateStoreys: cross-space fallback matched "
					   << matchedStrict << " strict + " << matchedCoplanar << " coplanar"
					   << " of " << openings.size() << " openings";

	return true;
}


TiXmlElement * Building::writeXML(TiXmlElement * parent, const ConvertOptions& convertOptions) const {
	if (m_id == -1)
		return nullptr;

	TiXmlElement * e = new TiXmlElement("Building");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_ifcId));
//	e->SetAttribute("visible", IBK::val2string<bool>(true));

	if(!m_storeys.empty()) {
		TiXmlElement * child = new TiXmlElement("BuildingLevels");
		e->LinkEndChild(child);

		for( const auto& storey : m_storeys) {
			storey->writeXML(child, convertOptions);
		}
	}
	return e;
}

VICUS::Building Building::getVicusObject(const ConvertOptions& options) const {
	VICUS::Building res;
	res.m_id = m_id;
	if(!m_name.empty())
		res.m_displayName = QString::fromStdString(m_name + "_" + std::to_string(m_ifcId));
	res.m_ifcGUID = m_guid;
	for(const auto& storey : m_storeys) {
		res.m_buildingLevels.emplace_back(storey->getVicusObject(options));
	}

	return res;
}

} // namespace IFCC
