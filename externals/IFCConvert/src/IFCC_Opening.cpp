#include "IFCC_Opening.h"

#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>

#include <IBKMK_Vector3D.h>

#include <carve/mesh.hpp>
#include <carve/matrix.hpp>
#include <Carve/src/include/carve/carve.hpp>

#include <IBKMK_3DCalculations.h>

#include "IFCC_GeometryInputData.h"
#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_BuildingElement.h"
#include "IFCC_RepresentationHelper.h"
#include "IFCC_CSG_Adapter.h"
#include "IFCC_Space.h"

namespace IFCC {

Opening::Opening(int id) :
	EntityBase(id)
{

}

bool Opening::set(std::shared_ptr<IFC4X3::IfcFeatureElementSubtraction> ifcElement) {
	if(!EntityBase::set(dynamic_pointer_cast<IFC4X3::IfcRoot>(ifcElement)))
		return false;

	m_guid = guidFromObject(ifcElement.get());

	return true;
}

void Opening::update(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors) {
	transform(productShape);
	fetchGeometry(productShape, errors);
}


void Opening::transform(std::shared_ptr<ProductShapeData> productShape) {
	if(productShape == nullptr)
		return;

	if(productShape->m_transformAppliedByIFCC)
		return;

	carve::math::Matrix transformMatrix = productShape->getTransform();
	if(transformMatrix != carve::math::Matrix::IDENT()) {
		// applyToChildren=false: same reasoning as Site/Space/BuildingElement.
		// Each product has its own composed placement chain in m_vec_transforms and
		// its own transform() call. Recursing here would double-apply.
		productShape->applyTransformToProduct(transformMatrix, true, false);
	}
	productShape->m_transformAppliedByIFCC = true;
}

void Opening::fetchGeometry(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors) {
	if(productShape == nullptr)
		return;

	m_originalMesh = surfacesFromRepresentation(productShape, m_surfaces, errors, OT_Opening, m_id);
}

const std::vector<int>& Opening::openingElementIds() const {
	return m_openingElementIds;
}

void Opening::checkSurfaceType(const BuildingElement &element, double eps) {
	int intersections = 0;
	for(const Surface& elemSurface : element.surfaces()) {
		for(Surface& opSurfaceNormal : m_surfaces) {
			if(opSurfaceNormal.sideType() != Surface::ST_Unknown)
				continue;

			bool parallel = elemSurface.isParallelTo(opSurfaceNormal, eps);
			bool intersected = false;
			if(elemSurface.isValid(eps) && opSurfaceNormal.isValid(eps)) {
				if(IBKMK::polyIntersect(elemSurface.polygon(), opSurfaceNormal.polygon()))
					intersected = true;
			}
			if(intersected && !parallel) {
				opSurfaceNormal.setSideType(Surface::ST_UnProbableSide);
				++intersections;
			}
		}

		for(Surface& opSurfaceCSG : m_surfacesCSGElement) {
			if(opSurfaceCSG.sideType() != Surface::ST_Unknown)
				continue;

			bool parallel = elemSurface.isParallelTo(opSurfaceCSG, eps);
			bool intersected = false;
			if(elemSurface.isValid(eps) && opSurfaceCSG.isValid(eps)) {
				if(IBKMK::polyIntersect(elemSurface.polygon(), opSurfaceCSG.polygon()))
					intersected = true;
			}
			if(intersected && !parallel) {
				opSurfaceCSG.setSideType(Surface::ST_UnProbableSide);
				++intersections;
			}
		}
	}

	if(!element.m_possibleSideSurfaces.empty()) {
		for(const auto& sideIndex : element.m_possibleSideSurfaces) {
			const Surface& surf = element.surfaces()[sideIndex];
			for(Surface& opSurfaceNormal : m_surfaces) {
				if(opSurfaceNormal.sideType() != Surface::ST_Unknown)
					continue;
				if(surf.isParallelTo(opSurfaceNormal, eps))
					opSurfaceNormal.setSideType(Surface::ST_ProbableSide);
			}
			for(Surface& opSurfaceCSG : m_surfacesCSGElement) {
				if(opSurfaceCSG.sideType() != Surface::ST_Unknown)
					continue;
				if(surf.isParallelTo(opSurfaceCSG, eps))
					opSurfaceCSG.setSideType(Surface::ST_ProbableSide);
			}
		}

	}
}

void Opening::createCSGSurfaces(const BuildingElement &element, double eps) {

	// create 3D intersection of opening and building element
	if(!element.m_originalMesh.empty() && !m_originalMesh.empty()) {
		shared_ptr<carve::mesh::MeshSet<3> > resultMesh;
		shared_ptr<carve::mesh::MeshSet<3> > firstMesh = element.m_originalMesh.front();
		//	meshSets.erase(meshSets.begin());
		shared_ptr<GeometrySettings> geom_settings = shared_ptr<GeometrySettings>( new GeometrySettings() );
		meshVector_t resultVect;
		try {
			CSG_Adapter::computeCSG(firstMesh, m_originalMesh, carve::csg::CSG::INTERSECTION, resultMesh, geom_settings);
			if(resultMesh) {
				resultVect.push_back(resultMesh);
				std::vector<Surface> tempCSG;
				surfacesFromMeshSets(resultVect, tempCSG);
				std::vector<int> matchIds;
				int index = 0;
				if(!element.surfaces().empty()) {
					for(const Surface& osurf : tempCSG) {
						for(const Surface& esurf : element.surfaces()) {
							if(osurf.distanceToParallelPlane(esurf, eps) < eps) {
								matchIds.push_back(index);
							}
						}
						++index;
					}
				}
				for(int id : matchIds) {
					m_surfacesCSGElement.push_back(tempCSG[id]);
				}
			}
		}
		catch (...) {
		}
	}
}

void Opening::createCSGSurfaces(const Space &space, double eps) {

	// create 3D intersection of opening and space
	if(!space.meshSets().empty() && !m_originalMesh.empty()) {
		shared_ptr<carve::mesh::MeshSet<3> > resultMesh;
		shared_ptr<carve::mesh::MeshSet<3> > firstMesh = space.meshSets().front();
		//	meshSets.erase(meshSets.begin());
		shared_ptr<GeometrySettings> geom_settings = shared_ptr<GeometrySettings>( new GeometrySettings() );
		meshVector_t resultVect;
		try {
			CSG_Adapter::computeCSG(firstMesh, m_originalMesh, carve::csg::CSG::INTERSECTION, resultMesh, geom_settings);
			if(resultMesh) {
				resultVect.push_back(resultMesh);
				std::vector<Surface> tempCSG;
				surfacesFromMeshSets(resultVect, tempCSG);
				std::vector<int> matchIds;
				int index = 0;
				if(!space.surfacesOrg().empty()) {
					for(const Surface& osurf : tempCSG) {
						for(const Surface& esurf : space.surfacesOrg()) {
							if(osurf.distanceToParallelPlane(esurf, eps) < eps) {
								matchIds.push_back(index);
							}
						}
						++index;
					}
				}
				for(int id : matchIds) {
					m_surfacesCSGElement.push_back(tempCSG[id]);
				}
			}
		}
		catch (...) {
		}
	}
}

const std::vector<Surface>& Opening::surfaces() const {
	return m_surfaces;
}

const std::vector<Surface> &Opening::surfacesCSGElement() const {
	return m_surfacesCSGElement;
}

const std::vector<Surface> &Opening::surfacesCSGSpace() const {
	return m_surfacesCSGSpace;
}

std::string Opening::guid() const {
	return m_guid;
}

void Opening::addOpeningElementId(int id) {
	m_openingElementIds.push_back(id);
}

void Opening::addContainingElementId(int id) {
	m_containedInElementIds.push_back(id);
}

void Opening::insertContainingElementId(std::vector<int>& other) const {
	if(!m_containedInElementIds.empty()) {
		other.insert(other.end(), m_containedInElementIds.begin(), m_containedInElementIds.end());
	}
}

void Opening::setSpaceBoundary(std::shared_ptr<SpaceBoundary> sb) {
	m_spaceBoundary = sb;
}

bool Opening::hasSpaceBoundary() const {
	return m_spaceBoundary.get() != nullptr;
}

} // namespace IFCC
