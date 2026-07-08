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
#include "IFCC_Logger.h"
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
	repairOversizedBody();
}

void Opening::repairOversizedBody() {
	// Threshold above which a void body cannot be a plausible wall/roof opening —
	// doors reach ~2.5m, wall depths ~0.8m; the broken WSHH bodies are 7-10m.
	const double kMaxPlausibleExtent = 3.0;
	// Maximum believable wall slab depth for the reconstructed void [m].
	const double kMaxSlabDepth = 1.5;

	if(m_surfaces.size() < 4)
		return;

	// Extrusion axis from the two largest non-parallel face normals: their cross
	// product is the box axis. (AABB axes are wrong for rotated buildings.)
	std::vector<std::pair<double, IBKMK::Vector3D>> areasNormals;
	for(const Surface& surf : m_surfaces) {
		if(surf.polygon().size() < 3)
			continue;
		areasNormals.push_back({surf.area(), surf.planeNormalVec()});
	}
	if(areasNormals.size() < 4)
		return;
	std::sort(areasNormals.begin(), areasNormals.end(),
			  [](const auto& a, const auto& b) { return a.first > b.first; });
	IBKMK::Vector3D n1 = areasNormals.front().second;
	IBKMK::Vector3D axis(0,0,0);
	for(size_t i=1; i<areasNormals.size(); ++i) {
		IBKMK::Vector3D c = n1.crossProduct(areasNormals[i].second);
		if(c.magnitude() > 0.3) {
			axis = c.normalized();
			break;
		}
	}
	if(axis.magnitude() < 0.5)
		return;

	// Project all vertices onto the axis.
	std::vector<double> ts;
	for(const Surface& surf : m_surfaces)
		for(const IBKMK::Vector3D& v : surf.polygon())
			ts.push_back(axis.scalarProduct(v));
	if(ts.size() < 8)
		return;
	std::sort(ts.begin(), ts.end());
	double tmin = ts.front();
	double tmax = ts.back();
	if(tmax - tmin <= kMaxPlausibleExtent)
		return;		// plausible body — nothing to repair

	// Interior stations: distinct t-values clearly away from both inflated caps.
	const double kCapMargin = 0.5;
	double intMin = 1e20, intMax = -1e20;
	for(double t : ts) {
		if(t > tmin + kCapMargin && t < tmax - kCapMargin) {
			intMin = std::min(intMin, t);
			intMax = std::max(intMax, t);
		}
	}
	if(intMin > intMax)
		return;		// pure two-station box — no interior information, leave as is
	double depth = intMax - intMin;
	if(depth > kMaxSlabDepth)
		return;		// interior spread itself implausible — don't guess
	if(depth < 0.01) {
		// Single interior station (zero-depth slab): widen symmetrically a bit so
		// the two faces are distinct planes.
		intMin -= 0.02;
		intMax += 0.02;
	}

	// Cross-section outline: 2D convex hull of all vertices projected along axis.
	IBKMK::Vector3D up = std::fabs(axis.m_z) < 0.9 ? IBKMK::Vector3D(0,0,1) : IBKMK::Vector3D(1,0,0);
	IBKMK::Vector3D u = axis.crossProduct(up).normalized();
	IBKMK::Vector3D w = axis.crossProduct(u).normalized();
	std::vector<std::pair<double,double>> pts2;
	for(const Surface& surf : m_surfaces)
		for(const IBKMK::Vector3D& v : surf.polygon())
			pts2.push_back({u.scalarProduct(v), w.scalarProduct(v)});
	// Andrew's monotone chain.
	std::sort(pts2.begin(), pts2.end());
	pts2.erase(std::unique(pts2.begin(), pts2.end(), [](const auto& a, const auto& b){
		return std::fabs(a.first-b.first) < 1e-9 && std::fabs(a.second-b.second) < 1e-9;
	}), pts2.end());
	if(pts2.size() < 3)
		return;
	auto cross2 = [](const std::pair<double,double>& o, const std::pair<double,double>& a, const std::pair<double,double>& b){
		return (a.first-o.first)*(b.second-o.second) - (a.second-o.second)*(b.first-o.first);
	};
	std::vector<std::pair<double,double>> hull(2*pts2.size());
	size_t k = 0;
	for(size_t i=0; i<pts2.size(); ++i) {
		while(k >= 2 && cross2(hull[k-2], hull[k-1], pts2[i]) <= 0) --k;
		hull[k++] = pts2[i];
	}
	for(size_t i=pts2.size()-1, t0=k+1; i>0; --i) {
		while(k >= t0 && cross2(hull[k-2], hull[k-1], pts2[i-1]) <= 0) --k;
		hull[k++] = pts2[i-1];
	}
	hull.resize(k-1);
	if(hull.size() < 3)
		return;

	// Demote every original (inflated) face, then add the true cross-section faces.
	for(Surface& surf : m_surfaces)
		surf.setSideType(Surface::ST_UnProbableSide);
	for(double t : {intMin, intMax}) {
		polygon3D_t poly;
		for(const auto& p : hull)
			poly.push_back(u * p.first + w * p.second + axis * t);
		Surface surf(poly);
		surf.setSideType(Surface::ST_ProbableSide);
		m_surfaces.push_back(surf);
	}
	Logger::instance() << "opening-repair: id=" << m_id << " name='" << m_name << "'"
					   << " inflated extent=" << (tmax - tmin)
					   << " -> slab [" << intMin << "," << intMax << "] depth=" << depth
					   << " hullPts=" << hull.size();
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

void Opening::addSpaceBoundary(std::shared_ptr<SpaceBoundary> sb) {
	m_spaceBoundaries.push_back(sb);
}

bool Opening::hasSpaceBoundary() const {
	return !m_spaceBoundaries.empty();
}

bool Opening::hasSpaceBoundaryInSpace(const std::string& spaceGuid) const {
	for(const auto& sb : m_spaceBoundaries) {
		if(sb && sb->guidRelatedSpace() == spaceGuid)
			return true;
	}
	return false;
}

} // namespace IFCC
