#include "IFCC_BuildingStorey.h"

#include <ifcpp/IFC4X3/include/IfcRelDefinesByProperties.h>
#include <ifcpp/IFC4X3/include/IfcRelAggregates.h>
#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>

#include <Carve/src/include/carve/carve.hpp>

#include <algorithm>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_Surface.h"
#include "IFCC_BuildingElement.h"
#include "IFCC_Cancellation.h"

namespace IFCC {

BuildingStorey::BuildingStorey(int id) :
	EntityBase(id)
{}

bool BuildingStorey::set(std::shared_ptr<IFC4X3::IfcSpatialStructureElement> ifcElement) {
	if(!EntityBase::set(dynamic_pointer_cast<IFC4X3::IfcRoot>(ifcElement)))
		return false;

	const std::vector<weak_ptr<IFC4X3::IfcRelAggregates> >& vec_decomposedBy = ifcElement->m_IsDecomposedBy_inverse;
	for(const auto& contEleme : vec_decomposedBy) {
		if( contEleme.expired() ) {
			continue;
		}
		shared_ptr<IFC4X3::IfcRelAggregates> rel_aggregates( contEleme );
		if( rel_aggregates ) {
			const std::vector<shared_ptr<IFC4X3::IfcObjectDefinition> >& vec_related_objects = rel_aggregates->m_RelatedObjects;
			for(const auto& contObj : vec_related_objects) {
				if( contObj ) {
					shared_ptr<IFC4X3::IfcSpace> space = std::dynamic_pointer_cast<IFC4X3::IfcSpace>(contObj);
					if(space != nullptr)
						m_spacesOriginal.push_back(space);
				}
			}
		}
	}

	return true;
}

// add all storeys in project to this storey
// function should be used if no storey exist in project
bool BuildingStorey::set(const objectShapeGUIDMap_t& spaces) {
	for(const auto& contObj : spaces) {
		const shared_ptr<ProductShapeData>& shape = contObj.second;
		if( shape ) {
			std::shared_ptr<IFC4X3::IfcObjectDefinition> objdef(shape->m_ifc_object_definition);
			shared_ptr<IFC4X3::IfcSpace> space = std::dynamic_pointer_cast<IFC4X3::IfcSpace>(objdef);
			if(space != nullptr)
				m_spacesOriginal.push_back(space);
		}
	}
	return true;
}

bool BuildingStorey::set(const std::vector<std::shared_ptr<IFC4X3::IfcSpace>>& spaces) {
	for(const auto& space : spaces) {
		if(space != nullptr)
			m_spacesOriginal.push_back(space);
	}
	return true;
}


void BuildingStorey::fetchSpaces(const std::map<std::string,shared_ptr<ProductShapeData>>& shapes,
								 shared_ptr<UnitConverter>& unit_converter, std::vector<ConvertError>& errors) {
	for(const auto& shape : shapes) {
		for(const auto& opOrg : m_spacesOriginal) {
			if(shape.first == guidFromObject(opOrg.get())) {
				std::shared_ptr<Space> space = std::shared_ptr<Space>(new Space(GUID_maker::instance().guid()));
				if(space->set(opOrg, errors)) {
					m_spaces.push_back(space);
					m_spaces.back()->update(shape.second, errors);
				}
				break;
			}
		}
	}
}

void BuildingStorey::updateSpaces(const objectShapeTypeVector_t& shapes,
								  shared_ptr<UnitConverter>& unit_converter,
								  const BuildingElementsCollector& buildingElements,
								  std::vector<Opening>& openings,
								  bool useSpaceBoundaries,
								  std::vector<ConvertError>& errors,
								  const ConvertOptions& convertOptions,
								  IBK::NotificationHandler* notify) {

	// Classify spaces by processing path
	std::vector<size_t> ifcIndices, constructionIndices;
	for(size_t i = 0; i < m_spaces.size(); ++i) {
		if(useSpaceBoundaries && !m_spaces[i]->spaceBoundaries().empty())
			ifcIndices.push_back(i);
		else
			constructionIndices.push_back(i);
	}

	size_t totalSpaces = ifcIndices.size() + constructionIndices.size();
	size_t completed = 0;

	// IFC path: sequential (already fast, no heavy geometry matching)
	for(size_t i : ifcIndices) {
		m_spaces[i]->updateSpaceBoundaries(shapes, unit_converter, buildingElements,
										   openings, useSpaceBoundaries, errors, convertOptions);
		++completed;
		if(notify && totalSpaces > 0)
			notify->notify(double(completed) / double(totalSpaces));
	}

	// Construction path: parallel Phase 1, sequential Phase 2
	if(!constructionIndices.empty()) {
		size_t nSpaces = constructionIndices.size();

		// Per-space results for Phase 1
		std::vector<std::vector<std::shared_ptr<SpaceBoundary>>> perSpaceSBs(nSpaces);
		std::vector<std::vector<ConvertError>> perSpaceErrors(nSpaces);

		// Pre-warm AABB caches on all construction-element surfaces once, serially, so that
		// the parallel Phase 1 below reads the cached values race-free.
		for(const auto& construction : buildingElements.allConstructionElements()) {
			for(const Surface& s : construction->surfaces()) {
				s.aabbMin();
				s.aabbMax();
			}
		}

		// Phase 1: parallel construction space boundary creation, chunked so the main thread
		// can emit progress notifications between chunks (notify must not be called from
		// inside the OMP parallel region — it invokes Qt processEvents on worker threads).
		const int targetTicks = 20;
		int chunk = std::max(1, (int)((nSpaces + targetTicks - 1) / targetTicks));
		// "Phase 1 half credit" is distributed across chunks so the bar moves continuously.
		size_t phase1Budget = nSpaces / 2;
		for(int chunkStart = 0; chunkStart < (int)nSpaces; chunkStart += chunk) {
			if(Cancellation::isCancelled())
				break;
			int chunkEnd = std::min(chunkStart + chunk, (int)nSpaces);
			#pragma omp parallel for schedule(dynamic)
			for(int j = chunkStart; j < chunkEnd; ++j) {
				perSpaceSBs[j] = m_spaces[constructionIndices[j]]->createConstructionSpaceBoundaries(
					buildingElements, perSpaceErrors[j], convertOptions);
			}
			if(notify && totalSpaces > 0) {
				size_t chunkCompleted = completed + (size_t)chunkEnd * phase1Budget / nSpaces;
				notify->notify(double(chunkCompleted) / double(totalSpaces), "Matching constructions");
			}
		}
		completed += phase1Budget;

		// Merge Phase 1 errors
		for(auto& errs : perSpaceErrors)
			errors.insert(errors.end(), errs.begin(), errs.end());

		// Phase 2: sequential opening matching and finalization - per-space notify
		for(size_t j = 0; j < nSpaces; ++j) {
			if(Cancellation::isCancelled())
				break;
			m_spaces[constructionIndices[j]]->finalizeConstructionSpaceBoundaries(
				perSpaceSBs[j], buildingElements, openings, errors, convertOptions);
			++completed;
			if(notify && totalSpaces > 0)
				notify->notify(double(completed) / double(totalSpaces), "Finalizing space boundaries");
		}
	}
}

//void BuildingStorey::updateSpaceConnections(BuildingElementsCollector& buildingElements, std::vector<Opening>& openings) {
//	for(auto& space : m_spaces) {
//		space->updateSpaceConnections(buildingElements, openings);
//	}
//}


TiXmlElement * BuildingStorey::writeXML(TiXmlElement * parent, const ConvertOptions& convertOptions) const {
	if (m_id == -1)
		return nullptr;

	TiXmlElement * e = new TiXmlElement("BuildingLevel");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_ifcId));
//	e->SetAttribute("visible", IBK::val2string<bool>(true));
	TiXmlElement::appendSingleAttributeElement(e, "Elevation", nullptr, std::string(), IBK::val2string<double>(0));
	TiXmlElement::appendSingleAttributeElement(e, "Height", nullptr, std::string(), IBK::val2string<double>(3));

	if(!m_spaces.empty()) {
		TiXmlElement * child = new TiXmlElement("Rooms");
		e->LinkEndChild(child);

		for( const auto& space : m_spaces) {
			space->writeXML(child, convertOptions);
		}
	}
	return e;
}

VICUS::BuildingLevel BuildingStorey::getVicusObject(const ConvertOptions& options) const {
	VICUS::BuildingLevel res;
	res.m_id = m_id;
	if(!m_name.empty())
		res.m_displayName = QString::fromStdString(m_name + "_" + std::to_string(m_ifcId));
	res.m_ifcGUID = m_guid;
	res.m_elevation = 0;
	res.m_height = 3;
	for(const auto& space : m_spaces) {
		res.m_rooms.emplace_back(space->getVicusObject(options));
	}

	return res;
}


} // namespace IFCC
