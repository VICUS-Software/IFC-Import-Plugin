#include "IFCC_Site.h"

#include <ifcpp/IFC4X3/include/IfcRelAggregates.h>
#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>
#include <ifcpp/IFC4X3/include/IfcObjectDefinition.h>

#include <IBKMK_Vector3D.h>

#include <carve/mesh.hpp>
#include <carve/matrix.hpp>

#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_RepresentationHelper.h"
#include "IFCC_Project.h"

namespace IFCC {

Site::Site(int id) :
	EntityBase(id)
{

}

bool Site::set(std::shared_ptr<IFC4X3::IfcSpatialStructureElement> ifcElement) {
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
					shared_ptr<IFC4X3::IfcBuilding> building = std::dynamic_pointer_cast<IFC4X3::IfcBuilding>(contObj);
					if(building != nullptr)
						m_buildingsOriginal.push_back(building);
				}
			}
		}
	}

	return true;
}

bool Site::set(std::shared_ptr<IFC4X3::IfcSpatialStructureElement> ifcElement, std::shared_ptr<ProductShapeData> productShape,
			   const std::map<std::string,shared_ptr<ProductShapeData>>& buildings, std::vector<ConvertError>& errors) {
	if(!set(ifcElement))
		return false;

	transform(productShape);
	fetchGeometry(productShape, errors);
	fetchBuildings(buildings);
	return true;
}

bool Site::set(const std::map<std::string,shared_ptr<ProductShapeData>>& buildings, std::vector<ConvertError>& errors) {
	fetchBuildings(buildings);
	return true;
}

bool Site::set(const Project &project) {
	m_buildingsOriginal = project.buildingsOriginal();
	// for(const auto& bo : project.buildingsOriginal()) {
	// 	m_buildingsOriginal.push_back(bo);
	// }
	return !m_buildingsOriginal.empty();
}


void Site::transform(std::shared_ptr<ProductShapeData> productShape) {
	if(productShape == nullptr)
		return;

	if(productShape->m_transformAppliedByIFCC)
		return;

	carve::math::Matrix transformMatrix = productShape->getTransform();
	if(transformMatrix != carve::math::Matrix::IDENT()) {
		// applyToChildren=false: Site's m_vec_children contains Buildings (populated by
		// GeometryConverter::resolveProjectStructure via IfcRelContainedInSpatialStructure
		// and IfcRelAggregates). Building's children contain Storeys; Storey's children
		// contain Walls, Slabs, Spaces, etc. Each of those products has its own composed
		// placement chain in m_vec_transforms (root→leaf), and its own transform() call
		// applies that full chain. Recursively re-applying Site's matrix here would
		// double-apply the site placement to every descendant's mesh.
		productShape->applyTransformToProduct(transformMatrix, true, false);
	}
	productShape->m_transformAppliedByIFCC = true;
}

void Site::fetchGeometry(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors) {
	if(productShape == nullptr)
		return;

	surfacesFromRepresentation(productShape, m_surfaces, errors, OT_Site, m_id);
}

void Site::fetchBuildings(const std::map<std::string,shared_ptr<ProductShapeData>>& buildings) {
	if(m_buildingsOriginal.empty()) {
		for(const auto& shape : buildings) {
			std::shared_ptr<Building> building = std::shared_ptr<Building>(new Building(GUID_maker::instance().guid()));
			std::shared_ptr<IFC4X3::IfcBuilding> buildingOrg = dynamic_pointer_cast<IFC4X3::IfcBuilding>(shape.second->m_ifc_object_definition.lock());
			if(buildingOrg && building->set(buildingOrg)) {
				m_buildings.push_back(building);
			}
			break;
		}
	}
	else {
		for(const auto& shape : buildings) {
			for(const auto& opOrg : m_buildingsOriginal) {
				std::string guid = opOrg->m_GlobalId->m_value;
				if(shape.first == guid) {
					std::shared_ptr<Building> building = std::shared_ptr<Building>(new Building(GUID_maker::instance().guid()));
					if(building->set(opOrg)) {
						m_buildings.push_back(building);
					}
					break;
				}
			}
		}
	}
}

std::vector<std::shared_ptr<SpaceBoundary>> Site::allSpaceBoundaries() const {
	std::vector<std::shared_ptr<SpaceBoundary>> res;
	for(const std::shared_ptr<Building>& building : m_buildings) {
		const std::vector<std::shared_ptr<BuildingStorey>>& storeys = building->storeys();
		for(const std::shared_ptr<BuildingStorey>& storey : storeys) {
			const std::vector<std::shared_ptr<Space>>& spaces = storey->spaces();
			for(const std::shared_ptr<Space>& space : spaces) {
				const std::vector<std::shared_ptr<SpaceBoundary>>& sbs = space->spaceBoundaries();
				res.insert(res.begin(), sbs.begin(), sbs.end());
			}
		}
	}
	return res;
}

std::vector<std::shared_ptr<Space>> Site::allSpaces() const {
	std::vector<std::shared_ptr<Space>> res;
	for(const std::shared_ptr<Building>& building : m_buildings) {
		const std::vector<std::shared_ptr<BuildingStorey>>& storeys = building->storeys();
		for(const std::shared_ptr<BuildingStorey>& storey : storeys) {
			const std::vector<std::shared_ptr<Space>>& spaces = storey->spaces();
			res.insert(res.begin(), spaces.begin(), spaces.end());
		}
	}
	return res;
}

const Space* Site::spaceWithId(int id) const {
	for(const std::shared_ptr<Building>& building : m_buildings) {
		const std::vector<std::shared_ptr<BuildingStorey>>& storeys = building->storeys();
		for(const std::shared_ptr<BuildingStorey>& storey : storeys) {
			const std::vector<std::shared_ptr<Space>>& spaces = storey->spaces();
			for(const auto& space : spaces) {
				if(space->m_id == id)
					return space.get();
			}
		}
	}
	return nullptr;
}

const Space* Site::spaceWithIfcId(int id) const {
	for(const std::shared_ptr<Building>& building : m_buildings) {
		const std::vector<std::shared_ptr<BuildingStorey>>& storeys = building->storeys();
		for(const std::shared_ptr<BuildingStorey>& storey : storeys) {
			const std::vector<std::shared_ptr<Space>>& spaces = storey->spaces();
			for(const auto& space : spaces) {
				if(space->m_ifcId == id)
					return space.get();
			}
		}
	}
	return nullptr;
}
const SpaceBoundary* Site::spaceBoundaryWithId(int id) const {
	std::vector<std::shared_ptr<SpaceBoundary>> res;
	for(const std::shared_ptr<Building>& building : m_buildings) {
		const std::vector<std::shared_ptr<BuildingStorey>>& storeys = building->storeys();
		for(const std::shared_ptr<BuildingStorey>& storey : storeys) {
			const std::vector<std::shared_ptr<Space>>& spaces = storey->spaces();
			for(const std::shared_ptr<Space>& space : spaces) {
				const std::vector<std::shared_ptr<SpaceBoundary>>& sbs = space->spaceBoundaries();
				for(const auto& sb : sbs) {
					if(space->m_id == id)
						return sb.get();
				}
			}
		}
	}
	return nullptr;
}

///*! Return a vector of all space connection in project.*/
//std::vector<Space::SurfaceConnectionVectors> Site::allSurfaceConnectionVectors() const {
//	std::vector<Space::SurfaceConnectionVectors> res;
//	for(const std::shared_ptr<Building>& building : m_buildings) {
//		const std::vector<std::shared_ptr<BuildingStorey>>& storeys = building->storeys();
//		for(const std::shared_ptr<BuildingStorey>& storey : storeys) {
//			const std::vector<std::shared_ptr<Space>>& spaces = storey->spaces();
//			for(const std::shared_ptr<Space>& space : spaces) {
//				res.push_back(space->surfaceConnectionVectors());
//			}
//		}
//	}
//	return res;
//}


void Site::addToVicusProject(VICUS::Project* project, const ConvertOptions& options) const {
	for(const auto& building : m_buildings) {
		project->m_buildings.emplace_back(building->getVicusObject(options));
	}
}

TiXmlElement * Site::writeXML(TiXmlElement * parent, const ConvertOptions& convertOptions) const {
	if(m_buildings.empty())
		return nullptr;

	TiXmlElement * e = new TiXmlElement("Buildings");
	parent->LinkEndChild(e);

	for( const std::shared_ptr<Building>& building : m_buildings) {
		building->writeXML(e, convertOptions);
	}
	return e;
}

const std::vector<Surface>& Site::surfaces() const {
	return m_surfaces;
}

} // namespace IFCC
