#include "IFCC_Space.h"

#include <ifcpp/IFC4X3/include/IfcRelSpaceBoundary.h>
#include <ifcpp/IFC4X3/include/IfcLengthMeasure.h>
#include <ifcpp/IFC4X3/include/IfcElementCompositionEnum.h>

#include <numeric>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <atomic>
#include <deque>
#include <limits>
#include <map>

#include <IBK_math.h>
#include <IBK_FormatString.h>

#include <IBKMK_3DCalculations.h>

#include <VICUS_Polygon2D.h>

#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_RepresentationHelper.h"
#include "IFCC_Cancellation.h"
#include "IFCC_Clippertools.h"
#include "IFCC_Logger.h"

namespace IFCC {

Space::Space(int id) :
	EntityBase(id)
{
}

bool Space::set(std::shared_ptr<IFC4X3::IfcSpace> ifcSpace, std::vector<ConvertError>& errors) {
	if(!EntityBase::set(dynamic_pointer_cast<IFC4X3::IfcRoot>(ifcSpace)))
		return false;

	m_longName = label2s(ifcSpace->m_LongName);
	if(ifcSpace->m_PredefinedType != nullptr)
		m_spaceType = ifcSpace->m_PredefinedType->m_enum;

	if(ifcSpace->m_CompositionType != nullptr) {
		switch(ifcSpace->m_CompositionType->m_enum) {
			case IFC4X3::IfcElementCompositionEnum::ENUM_COMPLEX: m_compositionType = CT_Complex; break;
			case IFC4X3::IfcElementCompositionEnum::ENUM_ELEMENT: m_compositionType = CT_Element; break;
			case IFC4X3::IfcElementCompositionEnum::ENUM_PARTIAL: m_compositionType = CT_Partial; break;
		}
	}

	// look for space boundaries from IFC
	for( const auto& bound : ifcSpace->m_BoundedBy_inverse) {
		auto boundP = bound.lock();
		std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
		bool res = sb->setFromIFC(boundP, errors);
		if(res) {
			m_spaceBoundaries.push_back(sb);
			m_spaceBoundaryGUIDs.push_back(sb->m_guid);
		}
	}

	for( const auto& cover : ifcSpace->m_HasCoverings_inverse) {
		auto coverP = cover.lock();
	}

	getSpaceProperties(ifcSpace, m_properties);

	return true;
}

void Space::update(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors) {
	transform(productShape);
	fetchGeometry(productShape, errors);
}


void Space::transform(std::shared_ptr<ProductShapeData> productShape) {
	if(productShape == nullptr)
		return;

	m_transformMatrix = productShape->getTransform();
	if(productShape->m_transformAppliedByIFCC)
		return;

	if(m_transformMatrix != carve::math::Matrix::IDENT()) {
		// applyToChildren=false: Space's m_vec_children may contain aggregate children
		// (e.g. IfcSpace composed of sub-spaces). Those sub-products have their own
		// transform() calls with their own composed placement chains, so recursing here
		// would double-apply this space's chain to their meshes.
		productShape->applyTransformToProduct(m_transformMatrix, true, false);
	}
	productShape->m_transformAppliedByIFCC = true;
}

void Space::fetchGeometry(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors) {
	if(productShape == nullptr)
		return;

	surfacesFromRepresentation(productShape, m_surfacesOrg, errors, OT_Space, m_id);

	m_meshSets = meshSetsFromBodyRepresentation(productShape);

	if(m_surfacesOrg.empty()) {
		// Diagnose why the space solid is missing — these rooms cannot be anchored
		// or orientation-fixed and stay red in the VICUS room validation.
		size_t nReps = productShape->m_vec_representations.size();
		size_t nItems = 0, nMeshsets = 0, nOpen = 0;
		std::string repIds;
		for(const auto& rep : productShape->m_vec_representations) {
			if(!rep) continue;
			if(!repIds.empty()) repIds += ",";
			repIds += rep->m_representation_identifier;
			nItems += rep->m_vec_item_data.size();
			for(const auto& item : rep->m_vec_item_data) {
				nMeshsets += item->m_meshsets.size();
				nOpen += item->m_meshsets_open.size();
			}
		}
		Logger::instance() << "Space::fetchGeometry: NO-SHELL space=" << m_id
						   << " ifcTag=#" << m_ifcId
						   << " name='" << m_name << "'"
						   << " reps=" << nReps << " [" << repIds << "]"
						   << " items=" << nItems
						   << " meshsets=" << nMeshsets << " open=" << nOpen;
	}
}


static bool divideSurface(const Surface::IntersectionResult& intRes, std::vector<Surface>& spaceSurfaces, int ssIndex, std::vector<Surface>& subsurfaces) {
	// we don't have any intersections
	if(intRes.m_intersections.empty())
		return false;

	// construction element covers complete space surface
	if(intRes.m_diffBaseMinusClip.empty()) {
//		spaceSurfaces.erase(spaceSurfaces.begin() + ssIndex);
		return true;
	}

	// we have a rest of the space surface without matching construction element
	// we have only one resulting space surface - change original surface to it
	std::vector<Surface> diffSurfaces;
	for(size_t i=0; i<intRes.m_diffBaseMinusClip.size(); ++i) {
		Surface surf = intRes.m_diffBaseMinusClip[i];
		std::vector<Surface> tmp = surf.getSimplified();

		// should never happen
		if(tmp.empty())
			return false;

		// we have some holes - add these to subsurface list
		if(!intRes.m_holesBaseMinusClip[i].empty()) {
			for(const Surface& subsurf : intRes.m_holesBaseMinusClip[i])
				subsurfaces.push_back(subsurf);
		}
		for(const auto& s : tmp) {
			diffSurfaces.push_back(s);
		}
	}

	spaceSurfaces[ssIndex] = diffSurfaces.front();
	for(size_t i=1; i<diffSurfaces.size(); ++i) {
		spaceSurfaces.push_back(diffSurfaces[i]);
	}
	return false;
}

/*! True if the polygon contains the same vertex twice at non-adjacent positions
	(self-touching ring). The CDT triangulation rejects such polygons with a
	"Duplicate vertex detected" exception — VICUS then cannot triangulate the
	surface at all. Adjacent duplicates are handled by cleanPolygon. */
static bool hasNonAdjacentDuplicateVertex(const polygon3D_t& poly, double eps = 1e-8) {
	const size_t n = poly.size();
	for(size_t i=0; i<n; ++i) {
		for(size_t j=i+2; j<n; ++j) {
			if(i == 0 && j == n-1)
				continue; // first and last are neighbors via the closing edge
			if(std::fabs(poly[i].m_x-poly[j].m_x) < eps &&
			   std::fabs(poly[i].m_y-poly[j].m_y) < eps &&
			   std::fabs(poly[i].m_z-poly[j].m_z) < eps)
				return true;
		}
	}
	return false;
}

/*! Unnormalized Newell normal of a polygon (orientation-sensitive). */
static IBKMK::Vector3D newellNormal(const polygon3D_t& poly) {
	IBKMK::Vector3D n(0,0,0);
	const size_t cnt = poly.size();
	for(size_t i=0; i<cnt; ++i) {
		const IBKMK::Vector3D& a = poly[i];
		const IBKMK::Vector3D& b = poly[(i+1)%cnt];
		n.m_x += (a.m_y - b.m_y) * (a.m_z + b.m_z);
		n.m_y += (a.m_z - b.m_z) * (a.m_x + b.m_x);
		n.m_z += (a.m_x - b.m_x) * (a.m_y + b.m_y);
	}
	return n;
}

/*! Return the surface with its winding aligned to the given reference normal
	(rebuilt with reversed vertex order when the Newell normal points the other
	way, so the cached plane data stays consistent with the vertex order).
	Clipper output has arbitrary winding — VICUS computes the room volume from
	oriented surfaces, and mixed orientations blow the volume up beyond the
	bounding box ("Volume inconsistency" -> room status error). Must be applied
	BEFORE holes/subsurfaces are attached (their 2D coordinates depend on the
	parent plane orientation). */
static Surface alignedWinding(const Surface& s, const IBKMK::Vector3D& refNormal) {
	IBKMK::Vector3D n = newellNormal(s.polygon());
	double dot = n.m_x*refNormal.m_x + n.m_y*refNormal.m_y + n.m_z*refNormal.m_z;
	if(dot >= 0.0)
		return s;
	polygon3D_t rev(s.polygon().rbegin(), s.polygon().rend());
	return Surface(rev);
}

/*! Mean width of a polygon (4*A/U — exact for rectangles). Clipper residuals along
	wall junctions are often millimeter-wide strips; they survive area thresholds
	(5 m x 5 mm = 0.025 m2) but collapse under the XML coordinate rounding, producing
	surfaces VICUS rejects at read time ("Error reading Polygon3D") — visible as
	rooms with missing faces. */
static double meanPolygonWidth(const Surface& s) {
	const polygon3D_t& poly = s.polygon();
	if(poly.size() < 3)
		return 0.0;
	double perimeter = 0.0;
	for(size_t i=0; i<poly.size(); ++i) {
		const IBKMK::Vector3D& a = poly[i];
		const IBKMK::Vector3D& b = poly[(i+1) % poly.size()];
		perimeter += std::sqrt((a.m_x-b.m_x)*(a.m_x-b.m_x) + (a.m_y-b.m_y)*(a.m_y-b.m_y) + (a.m_z-b.m_z)*(a.m_z-b.m_z));
	}
	if(perimeter <= 0.0)
		return 0.0;
	return 4.0 * s.area() / perimeter;
}

/*! Struct contains result of the function findFirstSurfaceMatchIndex.
	It is used for matching a construction surface to a space surface.*/
struct MatchResult {
	/*! Default constructor creates a non valid object.*/
	MatchResult() :
		m_wallSurfaceIndex(-1),
		m_spaceSurfaceIndex(-1)
	{}

	/*! Standard constructor.*/
	MatchResult(int wallSurfaceIndex, int spaceSurfaceIndex) :
		m_wallSurfaceIndex(wallSurfaceIndex),
		m_spaceSurfaceIndex(spaceSurfaceIndex)
	{}

	/*! Return true if both indices are valid (greater than -1).*/
	bool isValid() const {
		return m_wallSurfaceIndex > -1 && m_spaceSurfaceIndex > -1;
	}

	int m_wallSurfaceIndex;		///< Index of the construction surface
	int m_spaceSurfaceIndex;	///< Index of the space surface
};

static MatchResult findFirstSurfaceMatchIndex(const std::vector<Surface>& wallSurfaces, const std::vector<Surface>& spaceSurfaces, double minDist,
											  const ConvertOptions& convertOptions) {
	const double EPS = convertOptions.m_distanceEps;
	for(size_t wi=0; wi<wallSurfaces.size(); ++wi) {
		const Surface& wallSurf = wallSurfaces[wi];
		for(size_t si=0; si<spaceSurfaces.size(); ++si) {
			const Surface& spaceSurf = spaceSurfaces[si];
			if(spaceSurf.isParallelTo(wallSurf, convertOptions.m_distanceEps)) {
				double dist = spaceSurf.distanceToParallelPlane(wallSurf, convertOptions.m_distanceEps);
				if(dist < minDist * (1+EPS)) {
					if(wallSurf.isIntersected(spaceSurf))
						return MatchResult(wi,si);
				}
			}
		}
	}
	return MatchResult();
}


std::vector<std::shared_ptr<SpaceBoundary>> Space::createSpaceBoundaries(const BuildingElementsCollector& buildingElements, std::vector<ConvertError>& errors,
																		 const ConvertOptions& convertOptions) {
	std::vector<Surface> surfaces(m_surfacesOrg);
	std::vector<std::shared_ptr<SpaceBoundary>> spaceBoundaries;
	std::vector<std::shared_ptr<BuildingElement>> constructionElements = buildingElements.allConstructionElements();


	for(const auto& construction : constructionElements) {
		double dist = construction->thickness();
		double maxConstructionDist = 0;
		if(construction->isSubSurfaceComponent() && !construction->m_openingProperties.m_constructionThicknesses.empty()) {
			maxConstructionDist = *std::max_element(construction->m_openingProperties.m_constructionThicknesses.begin(),
												   construction->m_openingProperties.m_constructionThicknesses.end());
		}
		if(dist < convertOptions.m_distanceEps) {
			if(maxConstructionDist > convertOptions.m_distanceEps)
				dist = maxConstructionDist;
			else
				dist = convertOptions.m_standardWallThickness;
		}

		// try to find a construction surface an a space surface which matches together
		MatchResult indices = findFirstSurfaceMatchIndex(construction->surfaces(), surfaces, dist*convertOptions.m_distanceFactor, convertOptions);
		if(indices.isValid()) {
			int loopCount = 0;
			do {
				const Surface& currSpaceSurf = surfaces[indices.m_spaceSurfaceIndex];
				const Surface& currConstSurf = construction->surfaces()[indices.m_wallSurfaceIndex];
				++loopCount;
				Surface::IntersectionResult intersectionResult = currSpaceSurf.intersect2(currConstSurf);
				// no intersections found
				if(intersectionResult.m_intersections.empty())
					break;

				for(size_t i=0; i<intersectionResult.m_intersections.size(); ++i) {
					if(!intersectionResult.m_holesIntersections[i].empty()) {
//						errors.push_back(ConvertError{OT_Space, m_id, IBK::FormatString("intersection from space surface and building element surface has %1 holes")
//													  .arg(intersectionResult.m_holesIntersections[i].size()).str()});
					}
				}

				if(intersectionResult.holesWithChilds() > 0) {
					errors.push_back(ConvertError{OT_Space, m_id, "one or more holes in intersections or diff surface has childs"});
				}

				const Surface& firstISurf = intersectionResult.m_intersections.front();
				if(intersectionResult.m_intersections.size() == 1 && IBK::nearly_equal<2>(firstISurf.area(),currSpaceSurf.area())) {
					int id = GUID_maker::instance().guid();
					std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(id));
					std::string name = m_name + ":" + construction->m_name + " - " +
							std::to_string(indices.m_spaceSurfaceIndex) +
							" : " + std::to_string(indices.m_wallSurfaceIndex);
					sb->setFromBuildingElement(name, construction, *this);
					sb->m_elementEntityId = construction->m_id;
					sb->fetchGeometryFromBuildingElement(firstISurf, convertOptions);
					spaceBoundaries.push_back(sb);
					surfaces.erase(surfaces.begin() + indices.m_spaceSurfaceIndex);
				}
				else {
					for(const Surface& surf : intersectionResult.m_intersections) {
						int id = GUID_maker::instance().guid();
						std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(id));
						std::string name = m_name + ":" + construction->m_name + " - " +
								std::to_string(indices.m_spaceSurfaceIndex) + " : " + std::to_string(indices.m_wallSurfaceIndex);
						sb->setFromBuildingElement(name, construction, *this);
						sb->m_elementEntityId = construction->m_id;
						sb->fetchGeometryFromBuildingElement(surf, convertOptions);
						spaceBoundaries.push_back(sb);
					}
					std::vector<Surface> subsurfaces;
					// add difference surface - intersections to the surface list and remove the original one
					divideSurface(intersectionResult, surfaces, indices.m_spaceSurfaceIndex, subsurfaces);
					if(!subsurfaces.empty()) {
						// what should we do with the holes?
						errors.push_back(ConvertError{OT_Space, m_id, "rest surface from intersection from space surface and building element surface has holes"});
					}
				}
				indices = findFirstSurfaceMatchIndex(construction->surfaces(), surfaces, dist*convertOptions.m_distanceFactor, convertOptions);

				if(loopCount > 1000) {
					errors.push_back(ConvertError{OT_Space, m_id, "more than 1000 intersections found"});
					break;
				}
			} while(indices.isValid());
		}
	}

	for(const Surface& surf : surfaces) {
		if(surf.area() < 0.01)
			continue;

		std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
		std::string name = "Missing";
		sb->setForMissingElement(name, *this, false);
		sb->fetchGeometryFromBuildingElement(surf, convertOptions);
		spaceBoundaries.push_back(sb);
	}

	return spaceBoundaries;
}

struct ConstructionSurfaceInfo {
	int								m_id = -1;
	int								m_surfaceIndex = -1;
	BuildingElementTypes			m_type = IFCC::BET_None;
	double							m_distance = 0;
	double							m_intersectionArea = 0;

	/*! Cached result of intersect2 from the prefilter. Reused by the caller to avoid
		running the expensive Boolean operation twice on the same pair. */
	Surface::IntersectionResult		m_intersectionResult;
	bool							m_hasCachedIntersection = false;

	bool isValid() const { return m_id > -1; }
};

struct SpaceSurfaceMatches {
	int										m_spaceSurfaceIndex;
	std::vector<ConstructionSurfaceInfo>	m_constructions;
};

// Orientation bucket for a plane normal. 0=X-dominant, 1=Y-dominant, 2=Z-dominant, 3=sloped.
static int classifyOrientation(const IBKMK::Vector3D& n) {
	double ax = std::fabs(n.m_x);
	double ay = std::fabs(n.m_y);
	double az = std::fabs(n.m_z);
	// Threshold 0.9 keeps only near-axis-aligned surfaces in X/Y/Z buckets.
	// Anything more tilted lands in the sloped bucket and is tested against everything.
	const double AXIS_THRESHOLD = 0.9;
	if(ax >= AXIS_THRESHOLD && ax >= ay && ax >= az) return 0;
	if(ay >= AXIS_THRESHOLD && ay >= ax && ay >= az) return 1;
	if(az >= AXIS_THRESHOLD && az >= ax && az >= ay) return 2;
	return 3;
}

// Flat index entry over all construction surfaces — populated once per createSpaceBoundaries_2 call.
// Stores (constrElementIdx, surfaceIdx) rather than a raw Surface pointer so the entry
// remains valid even if construction surface vectors are copied/moved elsewhere.
struct IndexedConstrSurface {
	int							m_constructionElementIdx;	///< Index into the outer constructionElements vector
	int							m_constructionSurfaceIdx;	///< Surface index within construction->surfaces()
	int							m_constructionId;			///< Real id used by buildingElements.fromID
	double						m_threshDist;				///< Plane-distance threshold for this construction (thickness × factor)
	BuildingElementTypes		m_type;
};

// Weighted score used to pick the single best matching construction surface for a given space surface.
// Lower score = better match. Combines normalized area (bigger = better), distance (smaller = better),
// and a type-priority penalty (walls/roofs preferred over beams/coverings).
static double matchScore(const ConstructionSurfaceInfo& c, double maxArea, double maxDist) {
	double areaScore = (maxArea > 0.0) ? (maxArea - c.m_intersectionArea) / maxArea : 0.0;
	double distScore = (maxDist > 0.0) ? (c.m_distance / maxDist) : 0.0;
	double typeScore;
	switch(c.m_type) {
		case BET_Wall:
		case BET_Roof:		typeScore = 0.0; break;
		case BET_Slab:		typeScore = 0.1; break;
		case BET_Beam:
		case BET_Covering:	typeScore = 0.5; break;
		default:			typeScore = 0.8; break;
	}
	return 1.0 * areaScore + 0.5 * distScore + 0.3 * typeScore;
}

// For each space surface, find the single best matching construction surface using
// the pre-built orientation-bucketed index. Candidate filtering order: AABB overlap,
// plane parallelism, optional opposite-normal + side-of-space check, plane distance,
// full Boolean intersection (intersect2). The IntersectionResult is cached inside the
// returned ConstructionSurfaceInfo so the caller can skip the expensive second call.
static ConstructionSurfaceInfo findBestMatchUsingIndex(const Surface& spaceSurface,
													   const IBKMK::Vector3D& spaceCentroid,
													   const std::array<std::vector<IndexedConstrSurface>, 4>& buckets,
													   const std::vector<std::shared_ptr<BuildingElement>>& constructionElements,
													   const ConvertOptions& convertOptions) {
	const double EPS = convertOptions.m_distanceEps;
	IBKMK::Vector3D ssNormal = spaceSurface.planeNormalVec();
	int ssBucket = classifyOrientation(ssNormal);

	std::vector<ConstructionSurfaceInfo> candidates;
	// Visit the matching axis bucket plus the sloped bucket. When the space surface is itself
	// sloped, it's already in bucket 3 and we skip the redundant re-visit.
	std::array<int, 2> bucketsToVisit = { ssBucket, 3 };
	int visitCount = (ssBucket == 3) ? 1 : 2;

	// Pass 1 — cheap filtering: collect candidates that pass AABB / parallel /
	// distance checks, stash their cheap-distance for ordering.
	struct CheapCandidate {
		const IndexedConstrSurface* ics;
		const Surface*              cs;
		double                      dist;
	};
	std::vector<CheapCandidate> cheap;
	for(int vi = 0; vi < visitCount; ++vi) {
		int bi = bucketsToVisit[vi];
		const std::vector<IndexedConstrSurface>& bucket = buckets[bi];
		for(const IndexedConstrSurface& ics : bucket) {
			const Surface& cs = constructionElements[ics.m_constructionElementIdx]->surfaces()[ics.m_constructionSurfaceIdx];

			double aabbEps = convertOptions.m_aabbExpandEps + ics.m_threshDist;
			if(!spaceSurface.aabbOverlaps(cs, aabbEps))
				continue;

			if(!spaceSurface.isParallelTo(cs, EPS))
				continue;

			if(convertOptions.m_requireOppositeNormals) {
				IBKMK::Vector3D csN = cs.planeNormalVec();
				double dot = ssNormal.m_x * csN.m_x + ssNormal.m_y * csN.m_y + ssNormal.m_z * csN.m_z;
				if(dot >= 0.0)
					continue;
				const IBKMK::Vector3D& csCenter = cs.centroid();
				double sideDot = csN.m_x * (spaceCentroid.m_x - csCenter.m_x)
							   + csN.m_y * (spaceCentroid.m_y - csCenter.m_y)
							   + csN.m_z * (spaceCentroid.m_z - csCenter.m_z);
				if(sideDot >= 0.0)
					continue;
			}

			double dist = spaceSurface.distanceToParallelPlane(cs, EPS);
			if(dist >= ics.m_threshDist * (1.0 + EPS))
				continue;

			cheap.push_back({ &ics, &cs, dist });
		}
	}

	// Sort cheap candidates by distance ASC so we test the innermost layer first.
	// This matters for IFCs (THO_optimized) where a single wall is split into Putz /
	// Beton / Mineralwoll layer parts: opening-bearing surface is the innermost
	// (smallest dist) one, and the short-circuit below is supposed to pick it.
	std::sort(cheap.begin(), cheap.end(),
			  [](const CheapCandidate& a, const CheapCandidate& b){ return a.dist < b.dist; });

	// Pass 2 — expensive intersect2: short-circuit on first candidate that fully
	// covers the space surface. Saves O(N) intersect2 calls on dense bucket sets.
	const double spaceArea = spaceSurface.area();
	for(const CheapCandidate& cc : cheap) {
		Surface::IntersectionResult result = spaceSurface.intersect2(*cc.cs);
		if(!result.isValid())
			continue;

		double area = 0.0;
		for(const Surface& s : result.m_intersections)
			area += s.area();
		if(area <= convertOptions.m_minimumSurfaceArea)
			continue;

		ConstructionSurfaceInfo c;
		c.m_id = cc.ics->m_constructionId;
		c.m_type = cc.ics->m_type;
		c.m_distance = cc.dist;
		c.m_intersectionArea = area;
		c.m_surfaceIndex = cc.ics->m_constructionSurfaceIdx;
		c.m_intersectionResult = std::move(result);
		c.m_hasCachedIntersection = true;
		candidates.push_back(std::move(c));

		if(spaceArea > 0.0 && area >= 0.99 * spaceArea)
			break;
	}

	if(candidates.empty())
		return ConstructionSurfaceInfo();

	// Single-pass weighted scoring replaces the old double-sort.
	double maxArea = 0.0, maxDist = 0.0;
	for(const ConstructionSurfaceInfo& c : candidates) {
		if(c.m_intersectionArea > maxArea) maxArea = c.m_intersectionArea;
		if(c.m_distance > maxDist) maxDist = c.m_distance;
	}
	size_t bestIdx = 0;
	double bestScore = matchScore(candidates[0], maxArea, maxDist);
	for(size_t i = 1; i < candidates.size(); ++i) {
		double s = matchScore(candidates[i], maxArea, maxDist);
		if(s < bestScore) { bestScore = s; bestIdx = i; }
	}
	return std::move(candidates[bestIdx]);
}

static std::shared_ptr<SpaceBoundary> createSpaceBoundary(const std::shared_ptr<BuildingElement>& constr, const SpaceSurfaceMatches& match,
														  int constrSurfIndex, const Space& space, const Surface& intersection, const ConvertOptions& convertOptions ) {
	int id = GUID_maker::instance().guid();
	std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(id));
	std::string name = space.m_name + ":" + constr->m_name + " - " +
			std::to_string(match.m_spaceSurfaceIndex) +
			" : " + std::to_string(constrSurfIndex);
	sb->setFromBuildingElement(name, constr, space);
	sb->m_elementEntityId = constr->m_id;
	sb->fetchGeometryFromBuildingElement(intersection, convertOptions);
	return sb;
}

std::vector<std::shared_ptr<SpaceBoundary>> Space::createSpaceBoundaries_2(const BuildingElementsCollector& buildingElements, std::vector<ConvertError>& errors,
																		   const ConvertOptions& convertOptions) {
	std::vector<Surface> surfaces(m_surfacesOrg);
	std::vector<std::shared_ptr<SpaceBoundary>> spaceBoundaries;
	std::vector<std::shared_ptr<BuildingElement>> constructionElements = buildingElements.allConstructionElements();

	// Per-slot reference normal (from the original carve space face, which is
	// consistently oriented outward). All emitted SBs align their winding to it —
	// clipper output has arbitrary winding and mixed orientations make VICUS'
	// room volume inconsistent (matcher-path counterpart of the anchoring fix).
	std::vector<IBKMK::Vector3D> slotNormal(surfaces.size());
	for(size_t i=0; i<surfaces.size(); ++i)
		slotNormal[i] = newellNormal(surfaces[i].polygon());

	ConvertOptions::ConstructionMatching matchType = convertOptions.m_matchingType;

	if(matchType != ConvertOptions::CM_NoMatching) {

		// Pre-warm Surface AABB caches for the space surfaces — safe here because this runs
		// from the outer per-space OMP region; each space owns its own surfaces exclusively.
		for(Surface& s : surfaces) {
			s.aabbMin();
			s.aabbMax();
		}

		// Build the orientation-bucketed construction surface index ONCE.
		// Buckets: 0 = X-dominant normal, 1 = Y-dominant, 2 = Z-dominant, 3 = sloped.
		// A space surface only needs to check its own axis bucket plus the sloped bucket.
		std::array<std::vector<IndexedConstrSurface>, 4> buckets;

		// When BuildingElementPart matching is enabled, a wall that aggregates parts
		// (e.g. a layered IfcWall holding "Mineralwolldämmung Prio 1" parts in
		// THO_optimized) is replaced by its parts in the index — both keeps the index
		// size from doubling AND lets openings attach to the actual layer surface
		// the user expects to see in the 3D view, rather than the bare wall.
		const bool partMatchingEnabled = convertOptions.hasElementsForSpaceBoundaries(BET_BuildingElementPart);
		std::set<std::string> wallGuidsWithParts;
		std::set<std::string> excludedPartGuids;
		if(partMatchingEnabled) {
			for(const auto& construction : constructionElements) {
				if(!construction->hasElementParts())
					continue;
				if(construction->type() == BET_Wall)
					wallGuidsWithParts.insert(construction->m_guid);
				// Parts of excluded parents must not enter the envelope either: steel
				// columns with EPS fire casing (WSHH 'S-T-I | Profil' with 'EPS 1'/
				// 'Stahl lackiert' parts) otherwise spray dozens of interior fragments
				// into the room shell although columns themselves are excluded.
				if(construction->type() == BET_Column) {
					for(const auto& p : construction->elementParts())
						excludedPartGuids.insert(guidFromObject(p.get()));
				}
			}
		}

		for(size_t ci = 0; ci < constructionElements.size(); ++ci) {
			const auto& construction = constructionElements[ci];
			BuildingElementTypes type = construction->type();
			if(!convertOptions.hasElementsForSpaceBoundaries(type))
				continue;
			// Columns never become part of the room envelope (see evaluateSpaceBoundaryTypes).
			if(type == BET_Column)
				continue;
			// Same for their aggregated parts (EPS casing etc.).
			if(type == BET_BuildingElementPart && excludedPartGuids.count(construction->m_guid))
				continue;
			// Drop walls whose layer-parts are also matched — keeps openings from
			// attaching to the bare wall when the parts carry the visible surface.
			if(type == BET_Wall && wallGuidsWithParts.count(construction->m_guid))
				continue;

			double dist = construction->thickness();
			double maxConstructionDist = 0;
			if(construction->isSubSurfaceComponent() && !construction->m_openingProperties.m_constructionThicknesses.empty()) {
				maxConstructionDist = *std::max_element(construction->m_openingProperties.m_constructionThicknesses.begin(),
														construction->m_openingProperties.m_constructionThicknesses.end());
			}
			// Only clamp to the standard-wall-thickness fallback when thickness is
			// effectively missing (< 1cm). Thin elements like IfcBuildingElementPart
			// layers (e.g. 5cm Mineralwolldämmung) have a valid thickness and don't
			// need the 0.5m fallback — clamping there inflates the matching threshold
			// to ~1.5m and turns hundreds of distant parallel walls into candidates.
			const double NEAR_ZERO_THICKNESS = 0.01;
			const double MIN_THICKNESS = convertOptions.m_standardWallThickness;
			if(dist < NEAR_ZERO_THICKNESS) {
				if(maxConstructionDist > NEAR_ZERO_THICKNESS)
					dist = maxConstructionDist;
				else
					dist = MIN_THICKNESS;
			}
			dist *= convertOptions.m_distanceFactor;

			const std::vector<Surface>& csList = construction->surfaces();
			for(size_t si=0; si<csList.size(); ++si) {
				const Surface& cs = csList[si];
				// Populate AABB + centroid caches serially before any parallel reads.
				cs.aabbMin();
				cs.aabbMax();
				cs.centroid();
				int bi = classifyOrientation(cs.planeNormalVec());
				buckets[bi].push_back(IndexedConstrSurface{
					(int)ci, (int)si, construction->m_id, dist, type
				});
			}
		}

		// Compute this space's centroid (average of all original surface centroids) once —
		// used as a stable point for the side-of-plane geometric test.
		IBKMK::Vector3D spaceCentroid(0, 0, 0);
		{
			size_t npts = 0;
			for(const Surface& s : m_surfacesOrg) {
				const IBKMK::Vector3D& c = s.centroid();
				spaceCentroid = IBKMK::Vector3D(spaceCentroid.m_x + c.m_x,
												spaceCentroid.m_y + c.m_y,
												spaceCentroid.m_z + c.m_z);
				++npts;
			}
			if(npts > 0)
				spaceCentroid = IBKMK::Vector3D(spaceCentroid.m_x / double(npts),
												spaceCentroid.m_y / double(npts),
												spaceCentroid.m_z / double(npts));
		}

		const int PER_SURFACE_MAX = matchType == ConvertOptions::CM_MatchOnlyFirstConstruction
				? 1
				: (matchType == ConvertOptions::CM_MatchFirstNConstructions
					? std::max(1, convertOptions.m_matchedConstructionNumbers)
					: std::numeric_limits<int>::max());

		// Total-work safety net to guarantee termination on pathological inputs.
		const int TOTAL_MAX_ITER = std::max(1, convertOptions.m_maxMatchIterations)
				* std::max<int>(1, (int)m_surfacesOrg.size());

		// Work queue of surface indices. Residuals produced by divideSurface are appended
		// to `surfaces` and pushed into the queue so they're matched in the same pass —
		// no more outer do-while rebuilding `matches` from scratch.
		std::deque<size_t> workQueue;
		for(size_t i=0; i<surfaces.size(); ++i)
			workQueue.push_back(i);

		// Per-surface-slot match counter. Residuals inherit their parent slot's count.
		std::vector<int> matchCount(surfaces.size(), 0);

		int iters = 0;
		while(!workQueue.empty() && iters < TOTAL_MAX_ITER) {
			// Honor user cancellation — bail out leaving remaining surfaces to be marked "Missing".
			if(Cancellation::isCancelled())
				break;
			++iters;
			size_t ssi = workQueue.front();
			workQueue.pop_front();

			// Skip entries that were consumed (polygon cleared) or are too small.
			if(surfaces[ssi].polygon().empty() || surfaces[ssi].area() < convertOptions.m_minimumSurfaceArea)
				continue;

			if(matchCount[ssi] >= PER_SURFACE_MAX)
				continue;

			ConstructionSurfaceInfo best = findBestMatchUsingIndex(
				surfaces[ssi], spaceCentroid, buckets, constructionElements, convertOptions);
			if(!best.isValid())
				continue;

			++matchCount[ssi];

			std::shared_ptr<BuildingElement> constr = buildingElements.fromID(best.m_id);
			int bestConstrSurfaceIndex = best.m_surfaceIndex;

			// Reuse the intersection result from the prefilter.
			Surface::IntersectionResult intersectionResult;
			if(best.m_hasCachedIntersection)
				intersectionResult = std::move(best.m_intersectionResult);
			else
				intersectionResult = surfaces[ssi].intersect2(constr->surfaces()[bestConstrSurfaceIndex]);

			if(intersectionResult.m_intersections.empty())
				continue;

			if(intersectionResult.holesWithChilds() > 0)
				errors.push_back(ConvertError{OT_Space, m_id, "one or more holes in intersections or diff surface has childs"});

			SpaceSurfaceMatches matchStub;
			matchStub.m_spaceSurfaceIndex = (int)ssi;

			// CM_MatchOnlyFirstConstruction: one SB for the whole remaining surface, no subdivision.
			if(convertOptions.m_matchingType == ConvertOptions::CM_MatchOnlyFirstConstruction) {
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, alignedWinding(surfaces[ssi], slotNormal[ssi]), convertOptions));
				surfaces[ssi].setNewPolygon({});
				continue;
			}

			// CM_MatchFirstNConstructions: on the Nth (final) allowed match, emit a whole-surface SB
			// instead of subdividing — preserves original algorithm's end-of-budget behavior.
			bool finalBudgetReached = (convertOptions.m_matchingType == ConvertOptions::CM_MatchFirstNConstructions)
					&& (matchCount[ssi] >= PER_SURFACE_MAX);
			if(finalBudgetReached) {
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, alignedWinding(surfaces[ssi], slotNormal[ssi]), convertOptions));
				surfaces[ssi].setNewPolygon({});
				continue;
			}

			// Full-coverage shortcut: intersection equals the space surface exactly.
			const Surface& firstISurf = intersectionResult.m_intersections.front();
			double spaceArea = surfaces[ssi].area();
			if(intersectionResult.m_intersections.size() == 1
					&& IBK::nearly_equal<2>(firstISurf.area(), spaceArea)) {
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, alignedWinding(firstISurf, slotNormal[ssi]), convertOptions));
				surfaces[ssi].setNewPolygon({});
				continue;
			}

			// Partial coverage: emit SB per intersection, subdivide, enqueue residuals.
			for(const Surface& surf : intersectionResult.m_intersections)
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, alignedWinding(surf, slotNormal[ssi]), convertOptions));

			std::vector<Surface> holeSubsurfaces;
			size_t nSurfacesBefore = surfaces.size();
			bool fullyConsumed = divideSurface(intersectionResult, surfaces, (int)ssi, holeSubsurfaces);

			if(fullyConsumed) {
				surfaces[ssi].setNewPolygon({});
			}
			else {
				// divideSurface replaced surfaces[ssi] with the first residual and appended the rest.
				// Residuals inherit matchCount from parent (same budget down the subdivision chain).
				int parentCount = matchCount[ssi];
				if(matchCount.size() < surfaces.size())
					matchCount.resize(surfaces.size(), parentCount);
				if(slotNormal.size() < surfaces.size())
					slotNormal.resize(surfaces.size(), slotNormal[ssi]);
				for(size_t newIdx = nSurfacesBefore; newIdx < surfaces.size(); ++newIdx) {
					matchCount[newIdx] = parentCount;
					slotNormal[newIdx] = slotNormal[ssi];
					workQueue.push_back(newIdx);
				}
				// The parent slot itself now holds a residual — requeue it for further matching.
				workQueue.push_back(ssi);
			}

			if(!holeSubsurfaces.empty())
				errors.push_back(ConvertError{OT_Space, m_id, "rest surface from intersection from space surface and building element surface has holes"});
		}

		if(iters >= TOTAL_MAX_ITER)
			errors.push_back(ConvertError{OT_Space, m_id, "space-boundary matching hit iteration cap — some surfaces may be marked missing"});
	}

	for(size_t si=0; si<surfaces.size(); ++si) {
		const Surface& surf = surfaces[si];
		if(surf.area() < convertOptions.m_minimumSurfaceArea)
			continue;

		std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
		std::string name = "Missing";
		sb->setForMissingElement(name, *this, false);
		sb->fetchGeometryFromBuildingElement(
			si < slotNormal.size() ? alignedWinding(surf, slotNormal[si]) : surf, convertOptions);
		spaceBoundaries.push_back(sb);
	}

	return spaceBoundaries;
}

/*! Opening id selected via IFCC_DEBUG_OPENING_ID environment variable: all match
	attempts for this opening are logged unthrottled with reject reasons. -1 = off. */
static int debugOpeningId() {
	static int id = [](){
		const char* e = std::getenv("IFCC_DEBUG_OPENING_ID");
		return e ? std::atoi(e) : -1;
	}();
	return id;
}

/*! Openings hosted (IfcRelVoidsElement) in a WALL never punch holes into
	near-horizontal surfaces. Broken WSHH bodies (windows authored lying flat or
	extruded through the storey) otherwise per-face-match ceiling/floor fills —
	the trusted IFC topology says the hole belongs into a wall. Roof/slab-hosted
	openings (skylights, floor hatches) are unaffected. */
static bool rejectHorizontalForWallOpening(const Opening& op, const std::shared_ptr<SpaceBoundary>& sb,
										   const BuildingElementsCollector& buildingElements) {
	IBKMK::Vector3D n = newellNormal(sb->surface().polygon());
	double len = n.magnitude();
	if(len < 1e-9)
		return false;
	if(std::fabs(n.m_z / len) <= 0.75)
		return false; // vertical or sloped surface — always allowed
	std::vector<int> hosts;
	op.insertContainingElementId(hosts);
	for(int id : hosts) {
		auto elem = buildingElements.fromID(id);
		if(elem && elem->type() == BET_Wall)
			return true;
	}
	return false;
}

/*! Extent of the opening body along the patch plane normal [m] (-1 = unknown).
	See OpeningMatchCandidate::bodySpan. */
static double openingSpanAlongPatch(const Opening& op, const Surface& patch) {
	IBKMK::Vector3D n = newellNormal(patch.polygon());
	double len = n.magnitude();
	if(len < 1e-9)
		return -1.0;
	n *= 1.0/len;
	const IBKMK::Vector3D& p0 = patch.centroid();
	const std::vector<Surface>& surfs = !op.surfaces().empty() ? op.surfaces() : op.surfacesCSGElement();
	double mn = 1e20, mx = -1e20;
	bool have = false;
	for(const Surface& s : surfs) {
		for(const IBKMK::Vector3D& v : s.polygon()) {
			double d = n.scalarProduct(v - p0);
			mn = std::min(mn, d);
			mx = std::max(mx, d);
			have = true;
		}
	}
	return have ? mx - mn : -1.0;
}

/*! True when the opening body is upright (vertical extent dominates the horizontal
	footprint) — a facade window/door. Such openings must never attach to a
	near-horizontal surface (glass floors, ceiling fills); flat skylight bodies in
	roofs/slabs keep matching horizontal planes. */
static bool openingBodyIsUpright(const Opening& op) {
	// Use the plain body surfaces, fall back to the CSG element surfaces — the
	// matcher itself picks between the two lists, so whichever is filled describes
	// the body extent.
	const std::vector<Surface>& surfs = !op.surfaces().empty() ? op.surfaces() : op.surfacesCSGElement();
	IBKMK::Vector3D mn(1e20,1e20,1e20), mx(-1e20,-1e20,-1e20);
	bool have = false;
	for(const Surface& s : surfs) {
		const IBKMK::Vector3D& a = s.aabbMin();
		const IBKMK::Vector3D& b = s.aabbMax();
		mn.m_x = std::min(mn.m_x, a.m_x); mn.m_y = std::min(mn.m_y, a.m_y); mn.m_z = std::min(mn.m_z, a.m_z);
		mx.m_x = std::max(mx.m_x, b.m_x); mx.m_y = std::max(mx.m_y, b.m_y); mx.m_z = std::max(mx.m_z, b.m_z);
		have = true;
	}
	if(!have)
		return false;
	const double ez = mx.m_z - mn.m_z;
	const double eh = std::max(mx.m_x - mn.m_x, mx.m_y - mn.m_y);
	return ez > 0.8 * eh && ez > 0.5;
}

static Surface matchingOpeningSurface(const Surface& currentOpeningSurf, const std::shared_ptr<SpaceBoundary> spaceBoundary,
									  const ConvertOptions& convertOptions, double maxDistance,
									  bool allowCoplanarAccept = false, bool debug = false) {
	// NOTE: an AABB prefilter was tried here but removed — opening polygons and wall SBs
	// that legitimately contain each other can have surprising AABB gaps (very thin openings,
	// inconsistent coordinate conventions), and the distanceToParallelPlane test below is
	// already cheap enough that the prefilter is not worth the risk of silently dropping windows.

	double dist = currentOpeningSurf.distanceToParallelPlane(spaceBoundary->surface(), convertOptions.m_distanceEps);
	if(dist > maxDistance) {
		if(debug)
			Logger::instance() << "  dbg-open: REJECT dist=" << dist << " > " << maxDistance
							   << " sb='" << spaceBoundary->m_name << "' sbArea=" << spaceBoundary->surface().area()
							   << " openSurfArea=" << currentOpeningSurf.area();
		return Surface();
	}

	Surface intersectionResult = spaceBoundary->surface().intersect(currentOpeningSurf);
	if(!intersectionResult.isValid(convertOptions.m_distanceEps)) {
		// Coplanar-accept fallback. For curtainwall-like walls each room sees only a
		// partial slice of the full wall face; an opening can lie in the wall's plane
		// but miss the current room's SB slice in 2D, producing an empty intersection
		// even though the opening genuinely belongs to this wall. Accept the match
		// using the opening's own polygon when the planes coincide and the opening is
		// much smaller than the SB (guard against huge/spurious openings).
		// Only enabled by the cross-space Building-level fallback, so per-space matching
		// doesn't prematurely commit an opening in the wrong (first-processed) room.
		const double kMinOpeningArea = 0.05;
		double openingArea = currentOpeningSurf.area();
		double sbArea      = spaceBoundary->surface().area();
		if(allowCoplanarAccept &&
		   dist < convertOptions.m_distanceEps &&
		   openingArea >= kMinOpeningArea &&
		   sbArea > 0.0 &&
		   openingArea < 0.5 * sbArea) {
			Logger::instance() << "matchingOpeningSurface: coplanar-accept"
							   << " sb='" << spaceBoundary->m_name << "'"
							   << " openingArea=" << openingArea
							   << " sbArea=" << sbArea;
			return currentOpeningSurf;
		}
		if(debug) {
			const IBKMK::Vector3D& sc = spaceBoundary->surface().centroid();
			const IBKMK::Vector3D& oc = currentOpeningSurf.centroid();
			Logger::instance() << "  dbg-open: REJECT empty-intersection dist=" << dist
							   << " sb='" << spaceBoundary->m_name << "' sbArea=" << sbArea
							   << " sbC=(" << sc.m_x << "," << sc.m_y << "," << sc.m_z << ")"
							   << " openSurfArea=" << openingArea
							   << " opC=(" << oc.m_x << "," << oc.m_y << "," << oc.m_z << ")"
							   << " coplanarAccept=" << allowCoplanarAccept;
		}
		return Surface();
	}

	// Reject tiny grazing intersections. Catches cases where an opening face barely
	// touches the corner of an SB surface. Kept loose (0.10) so we don't false-reject
	// windows straddling corners. Big-vs-small SB disambiguation is handled by sorting
	// SBs by area in the caller, so the largest wall face wins first-match.
	double interArea   = intersectionResult.area();
	double openingArea = currentOpeningSurf.area();
	double sbArea      = spaceBoundary->surface().area();
	double minInput    = std::min(openingArea, sbArea);
	if(minInput > 0.0 && interArea / minInput < 0.10) {
		Logger::instance() << "matchingOpeningSurface: reject thin-strip"
						   << " sb='" << spaceBoundary->m_name << "'"
						   << " interArea=" << interArea
						   << " openingArea=" << openingArea
						   << " sbArea=" << sbArea
						   << " ratio=" << (interArea / minInput);
		return Surface();
	}

	if(debug)
		Logger::instance() << "  dbg-open: MATCH dist=" << dist
						   << " sb='" << spaceBoundary->m_name << "' interArea=" << interArea;
	return intersectionResult;
}

static Surface mergeSurfaces(const std::vector<Surface>& surfaces, double eps) {
	Surface res = surfaces.front();
	for(size_t i=1; i<surfaces.size(); ++i) {
		res.mergeOnlyThanPlanar(surfaces[i], eps);
	}
	return res;
}

/*! 2D convex hull (Andrew's monotone chain), CCW, without repeated last point. */
static std::vector<IBKMK::Vector2D> convexHull2D(std::vector<IBKMK::Vector2D> pts) {
	if(pts.size() < 3)
		return pts;
	std::sort(pts.begin(), pts.end(), [](const IBKMK::Vector2D& a, const IBKMK::Vector2D& b){
		return a.m_x < b.m_x || (a.m_x == b.m_x && a.m_y < b.m_y);
	});
	pts.erase(std::unique(pts.begin(), pts.end(), [](const IBKMK::Vector2D& a, const IBKMK::Vector2D& b){
		return std::fabs(a.m_x-b.m_x) < 1e-9 && std::fabs(a.m_y-b.m_y) < 1e-9;
	}), pts.end());
	if(pts.size() < 3)
		return pts;
	auto cross = [](const IBKMK::Vector2D& o, const IBKMK::Vector2D& a, const IBKMK::Vector2D& b){
		return (a.m_x-o.m_x)*(b.m_y-o.m_y) - (a.m_y-o.m_y)*(b.m_x-o.m_x);
	};
	std::vector<IBKMK::Vector2D> hull(2*pts.size());
	size_t k = 0;
	for(size_t i=0; i<pts.size(); ++i) {
		while(k >= 2 && cross(hull[k-2], hull[k-1], pts[i]) <= 0) --k;
		hull[k++] = pts[i];
	}
	for(size_t i=pts.size()-1, t=k+1; i>0; --i) {
		while(k >= t && cross(hull[k-2], hull[k-1], pts[i-1]) <= 0) --k;
		hull[k++] = pts[i-1];
	}
	hull.resize(k-1);
	return hull;
}

static bool addOpeningSpaceBoundary(const Surface& surface, Opening& currOp, const std::shared_ptr<SpaceBoundary> spaceBoundary, std::shared_ptr<BuildingElement> openingElem,
				  const std::string& spaceName, std::vector<std::shared_ptr<SpaceBoundary>>& openingSpaceBoundaries, const Space& space, const ConvertOptions& convertOptions) {
	if(!surface.isValid(convertOptions.m_distanceEps))
		return false;

	std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
	if(openingElem) {
		std::string name = spaceName + ":" + spaceBoundary->m_name+ ":" + openingElem->m_name + " - O" +
				std::to_string(openingElem->m_id) + " : OS" +
				std::to_string(surface.id());
		sb->setFromBuildingElement(name, openingElem, space);
		sb->m_elementEntityId = openingElem->m_id;
		sb->m_openingId = currOp.m_id;
		sb->fetchGeometryFromBuildingElement(surface, convertOptions);
		openingSpaceBoundaries.push_back(sb);
		currOp.addSpaceBoundary(sb);
	}
	else {
		std::string name = spaceName + spaceBoundary->m_name+ ":" + ": breakout - O" +
				std::to_string(-1) + " : OS" +
				std::to_string(surface.id());
		sb->setForVirtualElement(name, space, true);
		sb->m_openingId = currOp.m_id;
		sb->fetchGeometryFromBuildingElement(surface, convertOptions);
		openingSpaceBoundaries.push_back(sb);
		currOp.addSpaceBoundary(sb);
	}
	spaceBoundary->addContainedOpeningSpaceBoundaries(sb);
	// we found a connection therfore we can end searching
	return true;
}

/*! Try to match an opening to a given space boundary and return the merged candidate
	surface. Does NOT commit the match. Returns an invalid Surface if nothing matched.
	Caller is expected to pick the best candidate across SBs (largest area) and commit via
	addOpeningSpaceBoundary. This replaces the previous "first-match-wins" iteration that
	could produce windows attached to small inward-facing SBs simply because those SBs
	appeared earlier in the list than the correct big wall face.
*/
static Surface computeOpeningMatchSurface(Opening& currOp, const std::shared_ptr<SpaceBoundary>& spaceBoundary,
										  const ConvertOptions& convertOptions, double maxDistance,
										  bool allowCoplanarAccept = false, double* matchDist = nullptr) {
	std::vector<Surface> openingSurfaces;
	const std::vector<Surface>& openingSurfces = convertOptions.m_useCSGForOpenings && !currOp.surfacesCSGElement().empty() ? currOp.surfacesCSGElement() :
																													   currOp.surfaces();

	// Diagnostics — why does the match fail when it does.
	int probableSideTried = 0;
	int fallbackTried = 0;
	double bestDistProbable = 1e20;
	double bestDistFallback = 1e20;

	// Debug tracing for one selected opening (IFCC_DEBUG_OPENING_ID): log near-miss
	// candidates (parallel and within 2x search distance) unthrottled.
	const bool dbg = (currOp.m_id == debugOpeningId());
	if(dbg) {
		static std::atomic<bool> headerDone(false);
		if(!headerDone.exchange(true)) {
			Logger::instance() << "dbg-open: opening id=" << currOp.m_id
							   << " guid=" << currOp.guid()
							   << " name='" << currOp.m_name << "'"
							   << " nSurfaces=" << currOp.surfaces().size();
			for(size_t i=0; i<currOp.surfaces().size() && i<40; ++i) {
				const Surface& s = currOp.surfaces()[i];
				const IBKMK::Vector3D& c = s.centroid();
				Logger::instance() << "dbg-open:   surf[" << i << "] area=" << s.area()
								   << " sideType=" << (int)s.sideType()
								   << " centroid=(" << c.m_x << "," << c.m_y << "," << c.m_z << ")";
			}
		}
	}

	for(size_t cosi=0; cosi<openingSurfces.size(); ++cosi) {
		const Surface& currentOpeningSurf = openingSurfces[cosi];
		if(currentOpeningSurf.sideType() != Surface::ST_ProbableSide)
			continue;
		++probableSideTried;
		double d = currentOpeningSurf.distanceToParallelPlane(spaceBoundary->surface(), convertOptions.m_distanceEps);
		if(d < bestDistProbable)
			bestDistProbable = d;

		Surface surf = matchingOpeningSurface(currentOpeningSurf, spaceBoundary, convertOptions, maxDistance,
											  allowCoplanarAccept, dbg && d < 2.0*maxDistance);
		if(surf.isValid(convertOptions.m_distanceEps))
			openingSurfaces.push_back(surf);
	}
	if(openingSurfaces.empty()) {
		for(size_t cosi=0; cosi<currOp.surfaces().size(); ++cosi) {
			const Surface& currentOpeningSurf = currOp.surfaces()[cosi];
			// Skip reveal/side faces of the opening hole — these lead to thin-strip
			// matches against wall edges/ledges. Kept as fallback so openings whose
			// sideType stayed ST_Unknown can still match.
			if(currentOpeningSurf.sideType() == Surface::ST_UnProbableSide)
				continue;
			++fallbackTried;
			double d = currentOpeningSurf.distanceToParallelPlane(spaceBoundary->surface(), convertOptions.m_distanceEps);
			if(d < bestDistFallback)
				bestDistFallback = d;

			Surface surf = matchingOpeningSurface(currentOpeningSurf, spaceBoundary, convertOptions, maxDistance,
												  false, dbg && d < 2.0*maxDistance);
			if(surf.isValid(convertOptions.m_distanceEps))
				openingSurfaces.push_back(surf);
		}
	}
	if(openingSurfaces.empty() && !currOp.surfaces().empty()) {
		// Whole-body projection fallback. Arched/curved opening bodies (WSHH round-arch
		// windows are revolved solids) have NO planar face parallel to the wall — every
		// per-face test above fails the parallelism gate. Project ALL body vertices onto
		// the SB plane, build the 2D convex hull (= the opening outline as seen from the
		// wall) and intersect it with the SB polygon.
		const Surface& sbSurf = spaceBoundary->surface();
		// Cheap AABB pre-gate: skip SBs nowhere near the opening body (this fallback
		// runs for every candidate SB of every unmatched opening).
		IBKMK::Vector3D omin(1e20,1e20,1e20), omax(-1e20,-1e20,-1e20);
		for(const Surface& os : currOp.surfaces()) {
			const IBKMK::Vector3D& a = os.aabbMin();
			const IBKMK::Vector3D& b = os.aabbMax();
			omin.m_x = std::min(omin.m_x, a.m_x); omin.m_y = std::min(omin.m_y, a.m_y); omin.m_z = std::min(omin.m_z, a.m_z);
			omax.m_x = std::max(omax.m_x, b.m_x); omax.m_y = std::max(omax.m_y, b.m_y); omax.m_z = std::max(omax.m_z, b.m_z);
		}
		const IBKMK::Vector3D& smin = sbSurf.aabbMin();
		const IBKMK::Vector3D& smax = sbSurf.aabbMax();
		const double GAP = maxDistance;
		bool aabbNear = omin.m_x <= smax.m_x + GAP && omax.m_x >= smin.m_x - GAP
					 && omin.m_y <= smax.m_y + GAP && omax.m_y >= smin.m_y - GAP
					 && omin.m_z <= smax.m_z + GAP && omax.m_z >= smin.m_z - GAP;
		PlaneNormal plane(sbSurf.polygon());
		if(aabbNear && plane.m_valid) {
			IBKMK::Vector3D n = sbSurf.planeNormalVec();
			const IBKMK::Vector3D& p0 = sbSurf.centroid();
			double minAbsD = 1e20;
			double maxAbsD = 0.0;
			double minSignedD = 1e20;
			double maxSignedD = -1e20;
			std::vector<IBKMK::Vector2D> pts2;
			for(const Surface& os : currOp.surfaces()) {
				for(const IBKMK::Vector3D& v : os.polygon()) {
					double ds = n.m_x*(v.m_x-p0.m_x) + n.m_y*(v.m_y-p0.m_y) + n.m_z*(v.m_z-p0.m_z);
					double d = std::fabs(ds);
					if(d < minAbsD)
						minAbsD = d;
					if(d > maxAbsD)
						maxAbsD = d;
					if(ds < minSignedD)
						minSignedD = ds;
					if(ds > maxSignedD)
						maxSignedD = ds;
					pts2.push_back(plane.convert3DPoint(v));
				}
			}
			// The body must reach the SB plane region (closest vertex within search
			// dist) AND be FLAT against the plane: projected onto the correct wall the
			// perpendicular extent is just the wall/reveal depth, while a vertical
			// window projected onto a floor slab spans its full height — that produced
			// bogus window strips inside storey slabs ("Missing" ceiling SBs).
			const double kMaxBodyDepth = 0.8; // [m] max wall depth incl. reveal
			bool nearAndFlat = minAbsD <= maxDistance && (maxAbsD - minAbsD) <= kMaxBodyDepth;
			// Through-going body: WSHH-class models extrude some IfcOpeningElement
			// bodies meters through the room, so the wall plane cuts the box mid-way
			// (no vertex is near the plane — nearAndFlat fails). Accept when the body
			// clearly CROSSES the plane and its span ALONG the SB normal is a dominant
			// dimension of the body (a through-wall extrusion crosses mostly along the
			// normal). A vertical window box grazing a floor slab crosses the
			// horizontal plane only over its height while being far longer in the
			// blown-up extrusion direction — small ratio, stays excluded. NOTE: do not
			// compare the longest AABB axis against the normal instead — window boxes
			// are often TALLER than their extrusion depth (1.5x2.0m arch window with
			// 1.74m extrusion), which made the longest axis the vertical one and
			// wrongly rejected genuine through-wall crossings (align=0).
			bool crosses = false;
			if(!nearAndFlat && minSignedD < -maxDistance && maxSignedD > maxDistance) {
				const double ex = omax.m_x - omin.m_x;
				const double ey = omax.m_y - omin.m_y;
				const double ez = omax.m_z - omin.m_z;
				const double maxExtent = std::max(ex, std::max(ey, ez));
				const double spanAlongNormal = maxSignedD - minSignedD;
				double align = maxExtent > 1e-9 ? spanAlongNormal / maxExtent : 0.0;
				crosses = align >= 0.7;
				// A window/door box must never straddle-cut a near-HORIZONTAL surface:
				// vertical openings live in walls — a box passing through a ceiling or
				// floor fill (Missing) would punch a bogus hole into it (roughly cubic
				// window bodies pass the span ratio above). Sloped roofs (skylights)
				// stay allowed: |n_z| of a 45-degree roof is ~0.7.
				if(crosses && std::fabs(n.m_z) > 0.75) {
					crosses = false;
					if(dbg)
						Logger::instance() << "  dbg-open: HULL-CROSS reject horizontal SB sb='"
										   << spaceBoundary->m_name << "' nz=" << n.m_z;
				}
				if(dbg)
					Logger::instance() << "  dbg-open: HULL-CROSS check sb='" << spaceBoundary->m_name << "'"
									   << " minSignedD=" << minSignedD << " maxSignedD=" << maxSignedD
									   << " align=" << align << " -> " << (crosses ? "ACCEPT" : "reject");
			}
			if((nearAndFlat || crosses) && pts2.size() >= 3) {
				std::vector<IBKMK::Vector2D> hull = convexHull2D(pts2);
				if(hull.size() >= 3) {
					polygon3D_t hullPoly;
					for(const IBKMK::Vector2D& p : hull)
						hullPoly.push_back(plane.convert3DPointInv(p));
					Surface hullSurf(hullPoly);
					double hullArea = hullSurf.area();
					// Guards: real window/door outlines; oversized hulls are bogus geometry.
					if(hullArea >= 0.05 && hullArea <= 40.0) {
						Surface inter = sbSurf.intersect(hullSurf);
						if(inter.isValid(convertOptions.m_distanceEps)) {
							double ia = inter.area();
							double minInput = std::min(hullArea, sbSurf.area());
							if(minInput > 0.0 && ia/minInput >= 0.10) {
								if(dbg)
									Logger::instance() << "  dbg-open: MATCH-HULL minAbsD=" << minAbsD
													   << " sb='" << spaceBoundary->m_name << "'"
													   << " hullArea=" << hullArea << " interArea=" << ia;
								if(minAbsD < bestDistFallback)
									bestDistFallback = minAbsD;
								openingSurfaces.push_back(inter);
							}
							else if(dbg) {
								Logger::instance() << "  dbg-open: REJECT hull-thin-strip minAbsD=" << minAbsD
												   << " sb='" << spaceBoundary->m_name << "'"
												   << " hullArea=" << hullArea << " interArea=" << ia;
							}
						}
						else if(dbg) {
							Logger::instance() << "  dbg-open: REJECT hull-empty minAbsD=" << minAbsD
											   << " sb='" << spaceBoundary->m_name << "'"
											   << " sbArea=" << sbSurf.area() << " hullArea=" << hullArea;
						}
					}
				}
			}
		}
	}
	if(openingSurfaces.empty()) {
		// Throttle: WSHH-class models emit this for every opening x space boundary pair,
		// producing multi-GB step logs. Log the first 200 occurrences, then only every
		// 10000th as a heartbeat.
		static std::atomic<long> noMatchCount(0);
		const long n = ++noMatchCount;
		if(n <= 200 || n % 10000 == 0) {
			Logger::instance() << "computeOpeningMatchSurface: NO-MATCH"
							   << " opening id=" << currOp.m_id
							   << " name='" << currOp.m_name << "'"
							   << " sb='" << spaceBoundary->m_name << "'"
							   << " maxDistance=" << maxDistance
							   << " probableSideTried=" << probableSideTried
							   << " bestDistProbable=" << bestDistProbable
							   << " fallbackTried=" << fallbackTried
							   << " bestDistFallback=" << bestDistFallback
							   << " sbSurfaceArea=" << spaceBoundary->surface().area()
							   << (n == 200 ? " [further NO-MATCH lines throttled: 1 per 10000]" : "")
							   << (n > 200 ? " [throttled sample]" : "");
		}
		return Surface();
	}
	if(matchDist != nullptr)
		*matchDist = std::min(bestDistProbable, bestDistFallback);
	return mergeSurfaces(openingSurfaces, convertOptions.m_distanceEps);
}

/*! Distance from the opening ELEMENT's (window/door frame) geometry center to the
	centroid of the matched surface patch [m]. WSHH-class models extrude
	IfcOpeningElement bodies through the whole room (7m boxes with the window
	outline on BOTH end faces and huge side faces running along partition walls),
	so bogus matches appear far from the actual window. The filling element sits at
	the true position — its distance to the matched PATCH separates the real wall
	from the phantom one. (Distance to the SB *plane* is useless here: the phantom
	partition plane runs alongside the box, so even the true window is near it.)
	Returns 1e20 when no element geometry is available.
*/
static double openingElemDistanceToMatch(const std::shared_ptr<BuildingElement>& openingElem,
										 const Surface& matchedSurface, const Opening* opening = nullptr) {
	IBKMK::Vector3D c;
	bool have = false;
	// Priority: real element geometry (surfaces/mesh) > opening body centroid >
	// element placement point. WSHH windows without any representation get a
	// placement point of (0,0,0) (ifcpp never resolves the placement transform for
	// representation-less products) — the opening void body is at the true window
	// location and even for the symmetrically blown-up boxes its centroid stays
	// near the real wall.
	if(openingElem && openingElem->hasGeometricCenterFromGeometry(c))
		have = true;
	if(!have && opening != nullptr) {
		IBKMK::Vector3D mn(1e20,1e20,1e20), mx(-1e20,-1e20,-1e20);
		for(const Surface& os : opening->surfaces()) {
			const IBKMK::Vector3D& a = os.aabbMin();
			const IBKMK::Vector3D& b = os.aabbMax();
			mn.m_x = std::min(mn.m_x, a.m_x); mn.m_y = std::min(mn.m_y, a.m_y); mn.m_z = std::min(mn.m_z, a.m_z);
			mx.m_x = std::max(mx.m_x, b.m_x); mx.m_y = std::max(mx.m_y, b.m_y); mx.m_z = std::max(mx.m_z, b.m_z);
			have = true;
		}
		if(have)
			c.set((mn.m_x+mx.m_x)*0.5, (mn.m_y+mx.m_y)*0.5, (mn.m_z+mx.m_z)*0.5);
	}
	if(!have && openingElem && openingElem->geometricCenter(c))
		have = true;
	if(!have)
		return 1e20;
	const IBKMK::Vector3D& mc = matchedSurface.centroid();
	return (c - mc).magnitude();
}

bool Space::isBetterOpeningMatch(const OpeningMatchCandidate& cand, const OpeningMatchCandidate& best) {
	if(!cand.parentSB || cand.area <= 0.0)
		return false;
	if(!best.parentSB || best.area <= 0.0)
		return true;
	// Vertical patches beat near-horizontal ones for window/door openings unless
	// far smaller: WSHH extrudes some opening bodies meters through the storey —
	// their FOOTPRINT stamped into a glass ceiling (1.5x1.49m at z=15.27) ranks
	// above the true facade patch by raw area. Skylights are unaffected (their
	// only candidates are horizontal, so both sides classify equal).
	{
		auto horiz = [](const Surface& s) -> bool {
			IBKMK::Vector3D n = newellNormal(s.polygon());
			double len = n.magnitude();
			return len > 1e-9 && std::fabs(n.m_z)/len > 0.75;
		};
		const bool candHoriz = horiz(cand.mergedSurface);
		const bool bestHoriz = horiz(best.mergedSurface);
		if(candHoriz != bestHoriz) {
			if(!candHoriz && cand.area > 0.4 * best.area)
				return true;
			if(candHoriz && best.area > 0.4 * cand.area)
				return false;
		}
	}
	double areaCap = 1e20;
	if(cand.openingElem) {
		double elemArea = cand.openingElem->openingArea();
		if(elemArea > 0.1)
			areaCap = elemArea;
	}
	const double rankArea = std::min(cand.area, areaCap);
	const double bestRankArea = std::min(best.area, areaCap);
	// The true wall is CROSSED by the opening body (span along the patch normal =
	// extrusion depth), phantom patches on partitions the blown-up body slides
	// along have a small span (WSHH: Gipskaartonplatte patches 3.6-4.5 m² > window
	// area, body runs alongside). Prefer the crossed wall even when its (partial)
	// patch is smaller — down to 40% of the rival.
	if(cand.bodySpan >= 0.0 && best.bodySpan >= 0.0) {
		if(cand.bodySpan > 2.0 * best.bodySpan + 0.01 && rankArea > 0.4 * bestRankArea)
			return true;
		if(best.bodySpan > 2.0 * cand.bodySpan + 0.01 && bestRankArea > 0.4 * rankArea)
			return false;
	}
	// A patch far from the window element (>4m) loses against a near one (<2m)
	// even when bigger: blown-up boxes cross interior walls PARALLEL to the facade
	// (corridor walls) with full-size phantom patches — the spans tie there and
	// only the element position tells the walls apart.
	if(cand.dist < 2.0 && best.dist > 4.0 && rankArea > 0.4 * bestRankArea)
		return true;
	if(best.dist < 2.0 && cand.dist > 4.0 && bestRankArea > 0.4 * rankArea)
		return false;
	if(rankArea > bestRankArea * 1.05)
		return true;
	if(rankArea > bestRankArea * 0.95 && cand.dist < best.dist)
		return true;
	return false;
}

void Space::createSpaceBoundariesForOpeningsFromSpaceBoundaries(std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries,
																const BuildingElementsCollector& buildingElements,
																std::vector<Opening>& openings, std::vector<ConvertError>& errors,
																const ConvertOptions& convertOptions) {
	if(openings.empty())
		return;

	const std::string spaceTag = m_longName.empty() ? m_name : m_longName;
	Logger::instance() << "space-openings: BEGIN space='" << spaceTag << "' id=" << m_id
					   << " totalOpenings=" << openings.size()
					   << " totalSBs=" << spaceBoundaries.size();

	std::vector<std::shared_ptr<SpaceBoundary>> openingSpaceBoundaries;

	// Two-pass matching: for each opening, evaluate ALL candidate SBs first and keep
	// only the match with the largest intersection area. Committing immediately on
	// first match attached openings to the first geometrically-compatible SB — which
	// could be a small inward-facing ledge/niche rather than the main wall face.
	std::map<int, Space::OpeningMatchCandidate> bestByOp;
	std::map<int, std::vector<Space::OpeningMatchCandidate>> candsByOp;

	auto resolveOpeningElem = [&](const Opening& op) -> std::shared_ptr<BuildingElement> {
		std::shared_ptr<BuildingElement> oe;
		if(op.openingElementIds().size() == 1) {
			oe = buildingElements.fromID(op.openingElementIds().front());
		}
		else if(op.openingElementIds().size() > 1) {
			for(int id : op.openingElementIds()) {
				auto cand = buildingElements.fromID(id);
				if(cand && isOpeningType(cand->type())) { oe = cand; break; }
			}
			if(!oe)
				oe = buildingElements.fromID(op.openingElementIds().front());
		}
		return oe;
	};

	// Track every opening that had at least one candidate evaluated (matched or not),
	// so we can report per-space: "opening X was considered against N SBs, Y returned a
	// valid match, best was SB Z with area A." Crucial for diagnosing rooms where
	// windows vanish entirely — the log will tell us whether no SB was tried at all,
	// or they were all rejected by the thin-strip / intersection guards.
	struct OpeningTrace {
		int considered = 0;
		int validMatches = 0;
		std::string triedSbNames; // comma-separated list of SBs attempted
	};
	std::map<int, OpeningTrace> traceByOp;

	auto considerCandidate = [&](Opening& currOp, const std::shared_ptr<SpaceBoundary>& sb,
								 const std::shared_ptr<BuildingElement>& openingElem, double searchDist) {
		OpeningTrace& trace = traceByOp[currOp.m_id];
		++trace.considered;
		if(!trace.triedSbNames.empty())
			trace.triedSbNames += ", ";
		trace.triedSbNames += sb->m_name;
		if(rejectHorizontalForWallOpening(currOp, sb, buildingElements)) {
			if(currOp.m_id == debugOpeningId())
				Logger::instance() << "  dbg-open: REJECT wall-hosted-vs-horizontal sb='" << sb->m_name << "'";
			return;
		}
		double matchDist = 1e20;
		Surface merged = computeOpeningMatchSurface(currOp, sb, convertOptions, searchDist, false, &matchDist);
		if(!merged.isValid(convertOptions.m_distanceEps))
			return;
		++trace.validMatches;
		double area = merged.area();
		// Sanity: reject candidates wildly larger than the window/door itself —
		// broken opening geometry (curved bodies with bogus face areas) can produce
		// wall-sized "matches" that overwrite the room's facade with one SubSurface.
		if(openingElem) {
			double elemArea = openingElem->openingArea();
			if(elemArea > 0.1 && area > 3.0 * elemArea) {
				Logger::instance() << "space-openings: REJECT oversized candidate opening id=" << currOp.m_id
								   << " name='" << currOp.m_name << "' area=" << area
								   << " elementArea=" << elemArea << " sb='" << sb->m_name << "'";
				return;
			}
		}
		auto it = bestByOp.find(currOp.m_id);
		double elemDist = openingElemDistanceToMatch(openingElem, merged, &currOp);
		if(currOp.m_id == debugOpeningId()) {
			Logger::instance() << "  dbg-open: CANDIDATE sb='" << sb->m_name << "' area=" << area
							   << " elemDist=" << elemDist
							   << " elemSurfaces=" << (openingElem ? (int)openingElem->surfaces().size() : -1);
		}
		if(elemDist < 1e19) {
			// Hard gate: a window/door cannot sit meters away from the surface patch
			// it is supposedly mounted in. Kills matches of an opening box's side
			// face sliced along a partition wall far from the actual window.
			const double kMaxElemDist = 2.0;
			if(elemDist > kMaxElemDist) {
				Logger::instance() << "space-openings: REJECT far-element opening id=" << currOp.m_id
								   << " name='" << currOp.m_name << "' elemDist=" << elemDist
								   << " sb='" << sb->m_name << "' area=" << area;
				return;
			}
			matchDist = elemDist;
		}
		Space::OpeningMatchCandidate cand{sb, openingElem, merged, area, matchDist,
										  openingSpanAlongPatch(currOp, merged)};
		if(it == bestByOp.end() || isBetterOpeningMatch(cand, it->second)) {
			bestByOp[currOp.m_id] = cand;
		}
		// Keep ALL per-SB candidates: an opening spanning several coplanar wall
		// fragments needs a split commit (one hole piece per fragment) — see pass 2.
		std::vector<Space::OpeningMatchCandidate>& all = candsByOp[currOp.m_id];
		bool replaced = false;
		for(Space::OpeningMatchCandidate& c : all) {
			if(c.parentSB == sb) {
				if(isBetterOpeningMatch(cand, c))
					c = cand;
				replaced = true;
				break;
			}
		}
		if(!replaced)
			all.push_back(cand);
	};

	// Pass 1a: SB-first iteration over openings linked via m_containedOpenings.
	for(const auto& spaceBoundary : spaceBoundaries) {
		if(!spaceBoundary->isConstructionElement())
			continue;

		std::string elemGUID = spaceBoundary->guidRelatedElement();
		const std::shared_ptr<BuildingElement> elem = buildingElements.fromGUID(elemGUID);
		if(elem.get() == nullptr)
			continue;
		if(elem->m_containedOpenings.empty())
			continue;
		if(convertOptions.noSearchForOpenings(spaceBoundary->typeRelatedElement()))
			continue;
		// Columns never host windows/doors — hard exclusion independent of the
		// dialog blocklist (user requirement).
		if(spaceBoundary->typeRelatedElement() == BET_Column)
			continue;

		double searchDist = convertOptions.m_openingDistance;
		searchDist = std::max(elem->thickness(), searchDist);
		searchDist *= 1.1;

		for(int opid : elem->m_containedOpenings) {
			auto fitOp = std::find_if(openings.begin(), openings.end(),
									  [opid](const auto& op) -> bool { return op.m_id == opid; });
			if(fitOp == openings.end())
				continue;
			Opening& currOp = *fitOp;
			// Per-space check: an internal door/window connects two rooms and must be
			// matchable in BOTH — only skip when this space already attached it.
			if(currOp.hasSpaceBoundaryInSpace(m_guid))
				continue; // already linked in this space (e.g. via IFC relations)

			considerCandidate(currOp, spaceBoundary, resolveOpeningElem(currOp), searchDist);
		}
	}

	// Pass 1b: opening-first iteration for openings not matched above — try every SB
	// whose building element explicitly lists this opening in m_containedOpenings.
	for(Opening& currOp : openings) {
		if(currOp.hasSpaceBoundaryInSpace(m_guid))
			continue;
		if(bestByOp.count(currOp.m_id))
			continue; // already has a candidate from pass 1a

		for(const auto& spaceBoundary : spaceBoundaries) {
			// Virtual SBs are valid opening parents too: WSHH-class models author the
			// facade of rooms whose wall has only an axis representation as a VIRTUAL
			// space boundary — the windows in that facade must attach to it.
			if(!spaceBoundary->isConstructionElement() && !spaceBoundary->isVirtual())
				continue;
			// Virtual SBs bypass the type blocklist (their related type is
			// BET_VirtualElement/BET_None, which the blocklist contains).
			if(!spaceBoundary->isVirtual() && convertOptions.noSearchForOpenings(spaceBoundary->typeRelatedElement()))
				continue;
			if(spaceBoundary->typeRelatedElement() == BET_Column)
				continue;

			std::string elemGUID = spaceBoundary->guidRelatedElement();
			const std::shared_ptr<BuildingElement> sbElem = buildingElements.fromGUID(elemGUID);
			if(sbElem) {
				auto& ops = sbElem->m_containedOpenings;
				if(std::find(ops.begin(), ops.end(), currOp.m_id) == ops.end())
					continue;
			}

			double searchDist = convertOptions.m_openingDistance;
			if(sbElem)
				searchDist = std::max(sbElem->thickness(), searchDist);
			searchDist *= 1.1;

			considerCandidate(currOp, spaceBoundary, resolveOpeningElem(currOp), searchDist);
		}
	}

	// Pass 2: commit best candidate per opening.
	int committed = 0;
	for(auto& entry : bestByOp) {
		int opid = entry.first;
		OpeningMatchCandidate& cand = entry.second;
		auto fitOp = std::find_if(openings.begin(), openings.end(),
								  [opid](const auto& op) -> bool { return op.m_id == opid; });
		if(fitOp == openings.end())
			continue;
		if(fitOp->hasSpaceBoundaryInSpace(m_guid))
			continue;
		// An opening frequently spans SEVERAL coplanar wall fragments (walls split by
		// carve/anchoring) — committing only the best fragment cuts a partial hole
		// and the window visually overlaps the untouched neighbor fragment. Collect
		// all coplanar, non-overlapping sibling candidates as split pieces.
		std::vector<OpeningMatchCandidate> pieces;
		{
			auto candsIt = candsByOp.find(opid);
			if(candsIt != candsByOp.end())
				pieces = collectSplitPieces(cand, candsIt->second, convertOptions);
		}
		if(pieces.empty())
			pieces.push_back(cand);
		double combinedArea = 0.0;
		for(const OpeningMatchCandidate& p : pieces)
			combinedArea += p.area;
		// Defer low-coverage matches to the building-level cross-space fallback.
		// Per-space matching only sees SBs whose element hosts the opening (plus
		// synthetic Missing SBs) — committing a sliver here (e.g. 0.86 m² of a
		// 3.87 m² window grazing a Missing shell fill) permanently blocks the
		// cross-space pass from attaching the full-size match on the correct wall.
		// The same candidate stays reachable there, so nothing is lost by waiting.
		// The coverage test uses the COMBINED area of all split pieces.
		if(cand.openingElem) {
			double elemArea = cand.openingElem->openingArea();
			if(elemArea > 0.1 && combinedArea < 0.5 * elemArea) {
				Logger::instance() << "space-openings: DEFER low-coverage space='" << spaceTag << "'"
								   << " opening id=" << opid << " name='" << fitOp->m_name << "'"
								   << " area=" << combinedArea << " elemArea=" << elemArea
								   << " sb='" << cand.parentSB->m_name << "'";
				continue;
			}
		}
		for(size_t pci=0; pci<pieces.size(); ++pci) {
			const OpeningMatchCandidate& p = pieces[pci];
			addOpeningSpaceBoundary(p.mergedSurface, *fitOp, p.parentSB, p.openingElem,
									m_longName, openingSpaceBoundaries, *this, convertOptions);
			if(pci > 0) {
				Logger::instance() << "space-openings: SPLIT-COMMIT space='" << spaceTag << "'"
								   << " opening id=" << fitOp->m_id << " name='" << fitOp->m_name << "'"
								   << " -> sb='" << p.parentSB->m_name << "' area=" << p.area;
			}
		}
		++committed;
		Logger::instance() << "space-openings: COMMIT space='" << spaceTag << "'"
						   << " opening id=" << fitOp->m_id << " name='" << fitOp->m_name << "'"
						   << " -> sb='" << cand.parentSB->m_name << "' area=" << cand.area;
	}

	// Per-opening diagnostic: anything considered but not committed tells us the match
	// was rejected by guards (thin-strip) or computeOpeningMatchSurface returned invalid.
	for(const auto& tr : traceByOp) {
		int opid = tr.first;
		if(bestByOp.count(opid))
			continue;
		auto fitOp = std::find_if(openings.begin(), openings.end(),
								  [opid](const auto& op) -> bool { return op.m_id == opid; });
		std::string opName = fitOp != openings.end() ? fitOp->m_name : std::string("<missing>");
		Logger::instance() << "space-openings: NO-MATCH space='" << spaceTag << "'"
						   << " opening id=" << opid << " name='" << opName << "'"
						   << " candidatesConsidered=" << tr.second.considered
						   << " validMatches=" << tr.second.validMatches
						   << " triedSBs=[" << tr.second.triedSbNames << "]";
	}

	Logger::instance() << "space-openings: END space='" << spaceTag << "' id=" << m_id
					   << " committed=" << committed
					   << " considered=" << traceByOp.size();

	if(!openingSpaceBoundaries.empty()) {
		spaceBoundaries.insert(spaceBoundaries.end(), openingSpaceBoundaries.begin(), openingSpaceBoundaries.end());
	}
}

std::vector<Space::OpeningMatchCandidate> Space::collectSplitPieces(const OpeningMatchCandidate& best,
																	const std::vector<OpeningMatchCandidate>& all,
																	const ConvertOptions& convertOptions) {
	std::vector<OpeningMatchCandidate> pieces;
	if(!best.parentSB || best.area <= 0.0)
		return pieces;
	pieces.push_back(best);
	IBKMK::Vector3D nBest = newellNormal(best.mergedSurface.polygon());
	double nBestLen = nBest.magnitude();
	if(nBestLen < 1e-9)
		return pieces;
	nBest *= 1.0/nBestLen;
	const double dBest = nBest.scalarProduct(best.mergedSurface.centroid());
	double combinedArea = best.area;
	for(const OpeningMatchCandidate& other : all) {
		if(other.parentSB == best.parentSB || !other.parentSB || other.area < 0.05)
			continue;
		IBKMK::Vector3D nO = newellNormal(other.mergedSurface.polygon());
		double nOLen = nO.magnitude();
		if(nOLen < 1e-9)
			continue;
		nO *= 1.0/nOLen;
		double dot = nBest.scalarProduct(nO);
		if(std::fabs(dot) < 0.99)
			continue;
		double dO = nO.scalarProduct(other.mergedSurface.centroid());
		if(dot < 0) dO = -dO;
		if(std::fabs(dBest - dO) > 0.05)
			continue;
		bool overlaps = false;
		for(const OpeningMatchCandidate& p : pieces) {
			Surface inter = p.mergedSurface.intersect(other.mergedSurface);
			if(inter.isValid(convertOptions.m_distanceEps) && inter.area() > 0.2 * other.area) {
				overlaps = true;
				break;
			}
		}
		if(overlaps)
			continue;
		pieces.push_back(other);
		combinedArea += other.area;
	}
	// cap: combined pieces must stay within the window/door outline
	if(best.openingElem) {
		double elemArea = best.openingElem->openingArea();
		while(elemArea > 0.1 && combinedArea > 1.2 * elemArea && pieces.size() > 1) {
			combinedArea -= pieces.back().area;
			pieces.pop_back();
		}
	}
	return pieces;
}

Space::OpeningMatchCandidate Space::findBestOpeningMatch(Opening& opening,
														 const BuildingElementsCollector& buildingElements,
														 const ConvertOptions& convertOptions,
														 bool ignoreContainedOpeningsFilter,
														 bool allowCoplanarAccept,
														 std::vector<OpeningMatchCandidate>* allCandidates) const {
	OpeningMatchCandidate best;

	// Resolve the window/door element the opening is filled by, if any.
	std::shared_ptr<BuildingElement> openingElem;
	if(opening.openingElementIds().size() == 1) {
		openingElem = buildingElements.fromID(opening.openingElementIds().front());
	}
	else if(opening.openingElementIds().size() > 1) {
		for(int id : opening.openingElementIds()) {
			auto cand = buildingElements.fromID(id);
			if(cand && isOpeningType(cand->type())) { openingElem = cand; break; }
		}
		if(!openingElem)
			openingElem = buildingElements.fromID(opening.openingElementIds().front());
	}

	// Iterate this space's construction AND virtual SBs, trying each one that (by
	// m_containedOpenings) accepts this opening. Pick the match with the biggest
	// intersection area. Virtual SBs are included: WSHH-class models author the
	// facade of rooms whose wall has only an axis representation as a VIRTUAL
	// boundary — the windows in that facade must attach to it.
	for(const auto& sb : m_spaceBoundaries) {
		if(!sb->isConstructionElement() && !sb->isVirtual())
			continue;
		if(!sb->isVirtual() && convertOptions.noSearchForOpenings(sb->typeRelatedElement()))
			continue;
		if(sb->typeRelatedElement() == BET_Column)
			continue;

		std::string elemGUID = sb->guidRelatedElement();
		const std::shared_ptr<BuildingElement> sbElem = buildingElements.fromGUID(elemGUID);
		if(!ignoreContainedOpeningsFilter && sbElem) {
			auto& ops = sbElem->m_containedOpenings;
			if(std::find(ops.begin(), ops.end(), opening.m_id) == ops.end())
				continue;
		}
		// If sbElem is null (synthetic Missing SB) we still allow the attempt —
		// the building-level fallback calls us precisely for orphan openings.
		if(rejectHorizontalForWallOpening(opening, sb, buildingElements)) {
			if(opening.m_id == debugOpeningId())
				Logger::instance() << "  dbg-open: REJECT(fb) wall-hosted-vs-horizontal sb='" << sb->m_name << "'";
			continue;
		}

		double searchDist = convertOptions.m_openingDistance;
		if(sbElem)
			searchDist = std::max(sbElem->thickness(), searchDist);
		searchDist *= 1.1;

		double matchDist = 1e20;
		Surface merged = computeOpeningMatchSurface(opening, sb, convertOptions, searchDist, allowCoplanarAccept, &matchDist);
		if(!merged.isValid(convertOptions.m_distanceEps))
			continue;
		double area = merged.area();
		// Sanity: reject candidates wildly larger than the window/door itself (see
		// createSpaceBoundariesForOpeningsFromSpaceBoundaries::considerCandidate).
		if(openingElem) {
			double elemArea = openingElem->openingArea();
			if(elemArea > 0.1 && area > 3.0 * elemArea) {
				Logger::instance() << "space-openings: REJECT oversized candidate opening id=" << opening.m_id
								   << " name='" << opening.m_name << "' area=" << area
								   << " elementArea=" << elemArea << " sb='" << sb->m_name << "'";
				continue;
			}
		}
		double elemDist = openingElemDistanceToMatch(openingElem, merged, &opening);
		if(opening.m_id == debugOpeningId()) {
			Logger::instance() << "  dbg-open: CANDIDATE(fb) sb='" << sb->m_name << "' area=" << area
							   << " elemDist=" << elemDist
							   << " elemSurfaces=" << (openingElem ? (int)openingElem->surfaces().size() : -1);
		}
		if(elemDist < 1e19) {
			// Loose sanity cap only: the element position may legitimately be a couple
			// of meters from the patch centroid (placement origin at a corner of the
			// oversized WSHH opening boxes). The cross-space fallback compares the
			// candidates of all passes RELATIVELY via isBetterOpeningMatch, so a truly
			// wrong-wall candidate loses to the closer one instead of being hard-gated.
			const double kMaxElemDistHard = 10.0;
			if(elemDist > kMaxElemDistHard) {
				Logger::instance() << "space-openings: REJECT far-element opening id=" << opening.m_id
								   << " name='" << opening.m_name << "' elemDist=" << elemDist
								   << " sb='" << sb->m_name << "' area=" << area;
				continue;
			}
			matchDist = elemDist;
		}
		Space::OpeningMatchCandidate cand{sb, openingElem, merged, area, matchDist,
										  openingSpanAlongPatch(opening, merged)};
		if(allCandidates != nullptr)
			allCandidates->push_back(cand);
		if(isBetterOpeningMatch(cand, best))
			best = cand;
	}
	return best;
}

void Space::commitOpeningMatch(Opening& opening,
							   const OpeningMatchCandidate& candidate,
							   const ConvertOptions& convertOptions) {
	if(!candidate.parentSB || candidate.area <= 0.0)
		return;
	std::vector<std::shared_ptr<SpaceBoundary>> tmp;
	if(addOpeningSpaceBoundary(candidate.mergedSurface, opening, candidate.parentSB, candidate.openingElem,
							   m_longName, tmp, *this, convertOptions)) {
		m_spaceBoundaries.insert(m_spaceBoundaries.end(), tmp.begin(), tmp.end());
		Logger::instance() << "space-openings: CROSS-COMMIT space='" << (m_longName.empty() ? m_name : m_longName) << "'"
						   << " opening id=" << opening.m_id << " name='" << opening.m_name << "'"
						   << " -> sb='" << candidate.parentSB->m_name << "' area=" << candidate.area;
	}
}

bool Space::expandMissingHostToOpeningOutline(Opening& opening,
											  const std::shared_ptr<SpaceBoundary>& openingSB,
											  const BuildingElementsCollector& buildingElements,
											  const ConvertOptions& convertOptions) {
	if(!openingSB)
		return false;
	// The host must be a SYNTHETIC fill of this space — real construction
	// surfaces are never modified.
	std::shared_ptr<SpaceBoundary> host;
	for(const auto& sb : m_spaceBoundaries) {
		const std::vector<std::shared_ptr<SpaceBoundary>>& contained = sb->containedOpeningSpaceBoundaries();
		if(std::find(contained.begin(), contained.end(), openingSB) != contained.end()) {
			host = sb;
			break;
		}
	}
	if(!host || !host->isMissing()) {
		if(host && opening.m_id == debugOpeningId())
			Logger::instance() << "  dbg-open: EXPAND reject host not Missing sb='" << host->m_name << "'";
		return false;
	}

	std::shared_ptr<BuildingElement> elem;
	for(int id : opening.openingElementIds()) {
		auto cand = buildingElements.fromID(id);
		if(cand && isOpeningType(cand->type())) {
			elem = cand;
			break;
		}
		if(cand && !elem)
			elem = cand;
	}
	double elemArea = elem ? elem->openingArea() : 0.0;
	if(elemArea < 0.1) {
		if(opening.m_id == debugOpeningId())
			Logger::instance() << "  dbg-open: EXPAND reject no element area";
		return false;
	}
	double committedArea = openingSB->surface().area();
	if(committedArea >= 0.85 * elemArea)
		return false;

	const Surface& hostSurf = host->surface();
	if(!hostSurf.isValid(convertOptions.m_distanceEps))
		return false;
	PlaneNormal plane(hostSurf.polygon());
	if(!plane.m_valid)
		return false;

	// Full outline: opening-body vertices projected onto the host plane, then the
	// 2D convex hull. Prefer the probable front/back faces — projecting the FULL
	// body onto a host plane that is slightly tilted against the opening smears
	// the (meters-long) extrusion into a blown-up hull.
	const std::vector<Surface>& bodySurfs = !opening.surfaces().empty() ? opening.surfaces()
																		: opening.surfacesCSGElement();
	std::vector<IBKMK::Vector2D> pts2;
	for(const Surface& os : bodySurfs) {
		if(os.sideType() != Surface::ST_ProbableSide)
			continue;
		for(const IBKMK::Vector3D& v : os.polygon())
			pts2.push_back(plane.convert3DPoint(v));
	}
	if(pts2.size() < 3) {
		for(const Surface& os : bodySurfs)
			for(const IBKMK::Vector3D& v : os.polygon())
				pts2.push_back(plane.convert3DPoint(v));
	}
	if(pts2.size() < 3)
		return false;
	std::vector<IBKMK::Vector2D> hull = convexHull2D(pts2);
	if(hull.size() < 3)
		return false;
	polygon3D_t hull3;
	for(const IBKMK::Vector2D& p : hull)
		hull3.push_back(plane.convert3DPointInv(p));
	Surface hullSurf(hull3);
	double hullArea = hullSurf.area();
	// Sanity: real window/door outline, clearly more than the committed part.
	// The tight upper cap keeps oversized projections out — expanding a hole
	// beyond the element outline recreates the oversized-door-hole problem
	// (opening hulls are frequently wider than the door leaf).
	if(hullArea < committedArea + 0.05 || hullArea > 1.05 * elemArea) {
		if(opening.m_id == debugOpeningId())
			Logger::instance() << "  dbg-open: EXPAND reject hull gate hullArea=" << hullArea
							   << " committed=" << committedArea << " elemArea=" << elemArea;
		return false;
	}

	// The uncovered remainder is attached as a self-contained zero-depth FLAP:
	// front face (outward, carries the remaining hole part) plus an identical
	// inversely wound back plate. Every flap edge is shared by exactly those two
	// faces, so the room stays closed; the host fill and with it every existing
	// shell edge pairing stay UNTOUCHED (replacing the fill contour broke the
	// exact edge matching with the old neighbor faces — T-vertices count as open).
	// The flap volume contribution cancels out.
	const double kHoleMargin = 0.05; // [m] hole must stay strictly inside the flap
	IBKMK::Vector2D hc(0.0, 0.0);
	for(const IBKMK::Vector2D& p : hull) {
		hc.m_x += p.m_x;
		hc.m_y += p.m_y;
	}
	hc.m_x /= double(hull.size());
	hc.m_y /= double(hull.size());
	auto scaledHull = [&hull, &hc, &plane](double offset) -> Surface {
		polygon3D_t poly;
		for(const IBKMK::Vector2D& p : hull) {
			IBKMK::Vector2D d(p.m_x - hc.m_x, p.m_y - hc.m_y);
			double len = std::sqrt(d.m_x*d.m_x + d.m_y*d.m_y);
			if(len < 1e-9 || len + offset < 1e-9) {
				poly.push_back(plane.convert3DPointInv(p));
				continue;
			}
			double f = (len + offset) / len;
			poly.push_back(plane.convert3DPointInv(IBKMK::Vector2D(hc.m_x + d.m_x*f, hc.m_y + d.m_y*f)));
		}
		return Surface(poly);
	};
	Surface grownSurf = scaledHull(kHoleMargin);
	Surface shrunkSurf = scaledHull(-0.02);
	if(!grownSurf.isValid(convertOptions.m_distanceEps) || !shrunkSurf.isValid(convertOptions.m_distanceEps))
		return false;

	const IBKMK::Vector3D hostN = newellNormal(hostSurf.polygon());
	const IBKMK::Vector3D backN(-hostN.m_x, -hostN.m_y, -hostN.m_z);
	// Flap regions: grown outline minus the existing fill.
	Surface::IntersectionResult ir = hostSurf.intersect2(grownSurf);
	size_t flapsAdded = 0;
	double holeAreaAdded = 0.0;
	for(size_t di=0; di<ir.m_diffClipMinusBase.size(); ++di) {
		const Surface& flapRegion = ir.m_diffClipMinusBase[di];
		if(!flapRegion.isValid(convertOptions.m_distanceEps) || flapRegion.area() < 0.02)
			continue;
		if(di < ir.m_holesClipMinusBase.size() && !ir.m_holesClipMinusBase[di].empty())
			continue;
		if(flapRegion.area() > elemArea)
			continue; // runaway region — not a window remainder
		// Remaining hole part inside this flap (shrunk so it never touches the rim).
		Surface holePiece = flapRegion.intersect(shrunkSurf);
		if(!holePiece.isValid(convertOptions.m_distanceEps) || holePiece.area() < 0.05)
			continue;

		std::shared_ptr<SpaceBoundary> flapSB(new SpaceBoundary(GUID_maker::instance().guid()));
		flapSB->setForMissingElement("Missing", *this, false);
		if(!flapSB->fetchGeometryFromBuildingElement(alignedWinding(flapRegion, hostN), convertOptions))
			continue;
		// 'MissingBack' (not 'Missing'): the healer must neither coalesce the back
		// plate with other fills (pass 1b matches on the name) nor drop it as a
		// covered coplanar duplicate (pass 1 protects the name) — losing it would
		// re-open every flap edge.
		std::shared_ptr<SpaceBoundary> backSB(new SpaceBoundary(GUID_maker::instance().guid()));
		backSB->setForMissingElement("MissingBack", *this, false);
		if(!backSB->fetchGeometryFromBuildingElement(alignedWinding(flapRegion, backN), convertOptions))
			continue;

		std::shared_ptr<SpaceBoundary> holeSB(new SpaceBoundary(GUID_maker::instance().guid()));
		holeSB->setFromSpaceBoundaryWithSurface(*openingSB, alignedWinding(holePiece, hostN));
		holeSB->m_openingId = openingSB->m_openingId;
		flapSB->addContainedOpeningSpaceBoundaries(holeSB);
		opening.addSpaceBoundary(holeSB);

		m_spaceBoundaries.push_back(flapSB);
		m_spaceBoundaries.push_back(backSB);
		++flapsAdded;
		holeAreaAdded += holePiece.area();
	}
	if(flapsAdded == 0) {
		if(opening.m_id == debugOpeningId())
			Logger::instance() << "  dbg-open: EXPAND no usable flap region (diffs="
							   << ir.m_diffClipMinusBase.size() << ")";
		return false;
	}

	Logger::instance() << "space-openings: EXPAND-FLAP space='" << (m_longName.empty() ? m_name : m_longName) << "'"
					   << " opening id=" << opening.m_id << " name='" << opening.m_name << "'"
					   << " committed=" << committedArea << " outline=" << hullArea
					   << " elemArea=" << elemArea << " flaps=" << flapsAdded
					   << " holeAreaAdded=" << holeAreaAdded;
	return true;
}

bool Space::attachOrphanOpeningFlap(Opening& opening,
									const BuildingElementsCollector& buildingElements,
									const ConvertOptions& convertOptions) {
	if(opening.hasSpaceBoundary())
		return false;
	const std::vector<Surface>& bodySurfs = !opening.surfaces().empty() ? opening.surfaces()
																		: opening.surfacesCSGElement();
	if(bodySurfs.empty())
		return false;

	std::shared_ptr<BuildingElement> elem;
	for(int id : opening.openingElementIds()) {
		auto cand = buildingElements.fromID(id);
		if(cand && isOpeningType(cand->type())) {
			elem = cand;
			break;
		}
		if(cand && !elem)
			elem = cand;
	}
	double elemArea = elem ? elem->openingArea() : 0.0;

	const double kMaxPlaneDist  = 0.10;	// [m] hole outline must lie in the fill plane
	const double kMaxBodyDepth  = 0.80;	// [m] body must be flat against the plane
	const double kHoleMargin    = 0.05;	// [m] hole stays strictly inside the flap

	// Find the Missing fill whose plane carries the opening outline and whose rim
	// the (slightly grown) outline overlaps — the fill the hole was carved out of.
	std::shared_ptr<SpaceBoundary> bestFill;
	double bestRim = 0.0;
	for(const auto& sb : m_spaceBoundaries) {
		if(!sb->isMissing())
			continue;
		const Surface& fill = sb->surface();
		if(!fill.isValid(convertOptions.m_distanceEps) || fill.area() < 0.05)
			continue;
		PlaneNormal plane(fill.polygon());
		if(!plane.m_valid)
			continue;
		IBKMK::Vector3D n = newellNormal(fill.polygon());
		double nl = n.magnitude();
		if(nl < 1e-9)
			continue;
		n *= 1.0/nl;
		const IBKMK::Vector3D& p0 = fill.centroid();
		double minAbsD = 1e20, maxAbsD = 0.0;
		std::vector<IBKMK::Vector2D> pts2;
		for(const Surface& os : bodySurfs) {
			for(const IBKMK::Vector3D& v : os.polygon()) {
				double d = std::fabs(n.scalarProduct(v - p0));
				minAbsD = std::min(minAbsD, d);
				maxAbsD = std::max(maxAbsD, d);
				pts2.push_back(plane.convert3DPoint(v));
			}
		}
		if(pts2.size() < 3 || minAbsD > kMaxPlaneDist || maxAbsD - minAbsD > kMaxBodyDepth)
			continue;
		std::vector<IBKMK::Vector2D> hull2 = convexHull2D(pts2);
		if(hull2.size() < 3)
			continue;
		polygon3D_t hull3;
		for(const IBKMK::Vector2D& p : hull2)
			hull3.push_back(plane.convert3DPointInv(p));
		Surface hullSurf(hull3);
		double hullArea = hullSurf.area();
		if(hullArea < 0.05 || hullArea > 20.0)
			continue;
		if(elemArea > 0.1 && hullArea > 1.05 * elemArea)
			continue;
		// Carved-out hole: the outline itself is mostly OUTSIDE the fill — a hole
		// substantially covered by the fill is a job for the regular matching.
		Surface inner = fill.intersect(hullSurf);
		double innerArea = inner.isValid(convertOptions.m_distanceEps) ? inner.area() : 0.0;
		if(innerArea > 0.3 * hullArea)
			continue;
		// Rim contact: the grown outline must overlap the fill boundary.
		IBKMK::Vector2D hc(0.0, 0.0);
		for(const IBKMK::Vector2D& p : hull2) { hc.m_x += p.m_x; hc.m_y += p.m_y; }
		hc.m_x /= double(hull2.size());
		hc.m_y /= double(hull2.size());
		polygon3D_t grown3;
		for(const IBKMK::Vector2D& p : hull2) {
			IBKMK::Vector2D d(p.m_x - hc.m_x, p.m_y - hc.m_y);
			double len = std::sqrt(d.m_x*d.m_x + d.m_y*d.m_y);
			double f = len > 1e-9 ? (len + kHoleMargin) / len : 1.0;
			grown3.push_back(plane.convert3DPointInv(IBKMK::Vector2D(hc.m_x + d.m_x*f, hc.m_y + d.m_y*f)));
		}
		Surface rim = fill.intersect(Surface(grown3));
		double rimArea = rim.isValid(convertOptions.m_distanceEps) ? rim.area() : 0.0;
		if(rimArea < 0.001)
			continue;
		if(rimArea > bestRim) {
			bestRim = rimArea;
			bestFill = sb;
		}
	}
	if(!bestFill)
		return false;

	// Recompute outline in the winning fill's plane and attach the flap.
	const Surface& fill = bestFill->surface();
	PlaneNormal plane(fill.polygon());
	IBKMK::Vector3D hostN = newellNormal(fill.polygon());
	hostN *= 1.0/hostN.magnitude();
	std::vector<IBKMK::Vector2D> pts2;
	for(const Surface& os : bodySurfs)
		for(const IBKMK::Vector3D& v : os.polygon())
			pts2.push_back(plane.convert3DPoint(v));
	std::vector<IBKMK::Vector2D> hull2 = convexHull2D(pts2);
	IBKMK::Vector2D hc(0.0, 0.0);
	for(const IBKMK::Vector2D& p : hull2) { hc.m_x += p.m_x; hc.m_y += p.m_y; }
	hc.m_x /= double(hull2.size());
	hc.m_y /= double(hull2.size());
	auto scaledHull = [&hull2, &hc, &plane](double offset) -> Surface {
		polygon3D_t poly;
		for(const IBKMK::Vector2D& p : hull2) {
			IBKMK::Vector2D d(p.m_x - hc.m_x, p.m_y - hc.m_y);
			double len = std::sqrt(d.m_x*d.m_x + d.m_y*d.m_y);
			double f = (len > 1e-9 && len + offset > 1e-9) ? (len + offset) / len : 1.0;
			poly.push_back(plane.convert3DPointInv(IBKMK::Vector2D(hc.m_x + d.m_x*f, hc.m_y + d.m_y*f)));
		}
		return Surface(poly);
	};
	Surface hullSurf = scaledHull(0.0);
	if(!hullSurf.isValid(convertOptions.m_distanceEps))
		return false;

	// Duplicate openings authored on top of each other (AS7 stacks three
	// identical duct openings) must not each get their own plate — stacked
	// plates break the edge pairing and re-open the room. If an existing
	// opening plate already covers this region, treat this orphan as handled.
	for(const auto& sb : m_spaceBoundaries) {
		if(sb->m_openingId < 0 || !sb->isVirtual())
			continue;
		const Surface& other = sb->surface();
		if(!other.isValid(convertOptions.m_distanceEps))
			continue;
		IBKMK::Vector3D no = newellNormal(other.polygon());
		double nol = no.magnitude();
		if(nol < 1e-9)
			continue;
		no *= 1.0/nol;
		if(std::fabs(no.scalarProduct(hostN)) < 0.99)
			continue;
		if(std::fabs(hostN.scalarProduct(other.centroid() - fill.centroid())) > 0.10)
			continue;
		Surface inter = other.intersect(hullSurf);
		if(inter.isValid(convertOptions.m_distanceEps) && inter.area() > 0.5 * hullSurf.area()) {
			Logger::instance() << "space-openings: ORPHAN-FLAP skip duplicate opening id=" << opening.m_id
							   << " name='" << opening.m_name << "' covered by sb='" << sb->m_name << "'";
			return true;
		}
	}

	// Full VIRTUAL plate closing the carved hole (an empty opening IS an air
	// connection) plus the inversely wound back plate. No subsurface ring — a
	// few-cm frame around the hole fails the surface validation at write time,
	// and the surviving back plate alone re-opened the room.
	const IBKMK::Vector3D backN(-hostN.m_x, -hostN.m_y, -hostN.m_z);
	std::shared_ptr<SpaceBoundary> front(new SpaceBoundary(GUID_maker::instance().guid()));
	std::string vname = (m_longName.empty() ? m_name : m_longName) + ":" + opening.m_name + " - virtual opening";
	front->setForVirtualElement(vname, *this, false);
	front->m_openingId = opening.m_id;
	if(!front->fetchGeometryFromBuildingElement(alignedWinding(hullSurf, hostN), convertOptions))
		return false;
	std::shared_ptr<SpaceBoundary> backSB(new SpaceBoundary(GUID_maker::instance().guid()));
	backSB->setForMissingElement("MissingBack", *this, false);
	if(!backSB->fetchGeometryFromBuildingElement(alignedWinding(hullSurf, backN), convertOptions))
		return false;
	m_spaceBoundaries.push_back(front);
	m_spaceBoundaries.push_back(backSB);
	opening.addSpaceBoundary(front);

	Logger::instance() << "space-openings: ORPHAN-FLAP space='" << (m_longName.empty() ? m_name : m_longName) << "'"
					   << " opening id=" << opening.m_id << " name='" << opening.m_name << "'"
					   << " fill='" << bestFill->m_name << "' rim=" << bestRim
					   << " area=" << hullSurf.area();
	return true;
}

std::vector<Surface> Space::surfacesOrg() const {
	return m_surfacesOrg;
}

meshVector_t Space::meshSets() const {
	return m_meshSets;
}


static int typeFromElementShape(const shared_ptr<SpaceBoundary>& sb, const objectShapeTypeVector_t& shapes) {
	for(const auto& elemType : shapes) {
		for(const auto& elem : elemType.second) {
			if(sb->guidRelatedElement() == elem->m_entity_guid) {
				return elemType.first;
			}
		}
	}
	return -1;
}

int constructionId(const shared_ptr<SpaceBoundary>& sb, const BuildingElementsCollector& buildingElements) {
	int id = -1;
	std::vector<std::shared_ptr<BuildingElement>> constructionElements = buildingElements.allConstructionElements();
	for(const auto& construction : constructionElements) {
		if(construction->m_guid == sb->guidRelatedElement()) {
			id = construction->m_id;
		}
	}
	if(id == -1) {
		for(const auto& opening : buildingElements.m_openingElements) {
			if(opening->m_guid == sb->guidRelatedElement()) {
				id = opening->m_id;
			}
		}
	}
	return id;
}

void Space::evaluateSpaceBoundaryTypes(const objectShapeTypeVector_t& shapes,
								 const BuildingElementsCollector& buildingElements,
								 const ConvertOptions& convertOptions) {
	// Drop IFC-authored space boundaries whose related element type was filtered
	// out via the dialog's element-type list (m_elementsForSpaceBoundaries).
	// This makes the dialog checkbox cover BOTH the matcher path AND IFC-authored
	// IfcRelSpaceBoundary entries — e.g. unchecking "Column" reliably removes
	// columns from the resulting model, regardless of how the SB was created.
	// Virtual space boundaries (SB with no real element behind them) are kept,
	// they don't represent a building element type.
	std::vector<std::shared_ptr<SpaceBoundary>> filtered;
	filtered.reserve(m_spaceBoundaries.size());

	for(size_t sbI=0; sbI<m_spaceBoundaries.size(); ++sbI) {
		auto& sb = m_spaceBoundaries[sbI];

		int type = typeFromElementShape(sb, shapes);

		int id = constructionId(sb, buildingElements);

		if(type > -1) {
			const BuildingElementTypes betype = static_cast<BuildingElementTypes>(type);
			// Apply the dialog filter only to types the dialog actually exposes
			// (constructions and similar constructions). Anything else (openings,
			// BET_All catch-all, etc.) keeps its previous behaviour.
			// Columns never become part of the room envelope: free-standing columns
			// produce interior boundary fragments that break the room volume and add
			// no thermal value. They stay visible in the VicIFC 3D model.
			if(betype == BET_Column) {
				Logger::instance() << "Dropping IFC-authored space boundary id=" << sb->m_id
								   << " (column boundaries are excluded from rooms)";
				continue;
			}
			const bool dialogControls = isConstructionType(betype) || isConstructionSimilarType(betype);
			if(dialogControls && !convertOptions.hasElementsForSpaceBoundaries(betype)) {
				Logger::instance() << "Dropping IFC-authored space boundary id=" << sb->m_id
								   << " (related element type " << (int)betype
								   << " disabled in dialog)";
				continue;
			}
			sb->setRelatingElementType(betype);
			sb->m_elementEntityId = id;
		}
		else {
			if(sb->isVirtual()) {
				sb->setRelatingElementType(static_cast<BuildingElementTypes>(BET_None));
			}
			else {
				sb->setRelatingElementType(static_cast<BuildingElementTypes>(BET_None));
			}
			sb->m_elementEntityId = -1;
		}

		filtered.push_back(sb);
	} // end loop over space boundaries

	m_spaceBoundaries.swap(filtered);
}

bool Space::evaluateSpaceBoundaryGeometry(shared_ptr<UnitConverter>& unit_converter,
									   std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	if(m_spaceBoundaries.empty())
		return false;


	bool foundOne = false;
	std::vector<int> wrongSBIds;
	for(size_t sbI=0; sbI<m_spaceBoundaries.size(); ++sbI) {
		auto& sb = m_spaceBoundaries[sbI];

		bool res = sb->fetchGeometryFromIFC(unit_converter, m_transformMatrix, errors, convertOptions);
		if(res) {
			foundOne = true;
		}
		else {
			wrongSBIds.push_back(sb->m_id);
		}

	} // end loop over space boundaries

	if(!foundOne) {
		errors.push_back({OT_Space, m_id, "Cannot find at least one connected space boundary"});
	}

	// remove all space boundaries from the list which have non valid surfaces
	if(!wrongSBIds.empty()) {
		for(auto it = m_spaceBoundaries.begin(); it!=m_spaceBoundaries.end();) {
			if(std::find_if(wrongSBIds.begin(), wrongSBIds.end(), [it](int id) -> bool { return id == (*it)->m_id;}) != wrongSBIds.end()) {
				it = m_spaceBoundaries.erase(it);
			}
			else {
				++it;
			}
		}
	}
	return foundOne;
}

bool Space::evaluateSpaceBoundaryFromIFC(const objectShapeTypeVector_t& shapes,
										 const BuildingElementsCollector& buildingElements,
										 shared_ptr<UnitConverter>& unit_converter,
										 std::vector<ConvertError>& errors,
										 const ConvertOptions& convertOptions) {
	// get space boundary types and set element id connections
	evaluateSpaceBoundaryTypes(shapes, buildingElements, convertOptions);

	// convert geometry and create surfaces
	bool res = evaluateSpaceBoundaryGeometry(unit_converter, errors, convertOptions);
	if(!res)
		return false;

	std::vector<std::shared_ptr<SpaceBoundary>> splittedSBs;
	for(auto sb : m_spaceBoundaries) {
		std::vector<std::shared_ptr<SpaceBoundary>> ssbs = sb->splitBySurfaces();
		if(!ssbs.empty()) {
			splittedSBs.insert(splittedSBs.end(), ssbs.begin(), ssbs.end());
		}
	}
	if(!splittedSBs.empty()) {
		m_spaceBoundaries.insert(m_spaceBoundaries.end(), splittedSBs.begin(), splittedSBs.end());
	}

	int wrongSurfaces = 0;
	for(auto sb : m_spaceBoundaries) {
		if(!sb->checkAndHealSurface(true, convertOptions.m_polygonEps)) {
			++wrongSurfaces;
		}
	}
	if(wrongSurfaces > 0) {
		errors.push_back(ConvertError{OT_Space, m_id, "Space contains " + std::to_string(wrongSurfaces) + " space boundaries with non valid surface."});
	}

	// Coalesce coplanar construction SBs of the same building element. IFC files like
	// BBW_Haus_D fragment one wall/slab into many SBs (e.g. 30 'Virtual', 16 'Dach-003',
	// 15 'Wand-054' in one room). Merging same-element coplanar fragments closes
	// internal artificial edges and gives the room a clean perimeter for the
	// VICUS::Room::isVolumeOpen() check. Opening SBs stay untouched — they get
	// re-attached to the merged constrSB by the matching loop below.
	{
		auto computePlaneKey = [](const Surface& s) {
			IBKMK::Vector3D n = s.planeNormalVec();
			// Canonicalize sign: flip so first nonzero component is positive.
			const double eps = 1e-8;
			bool flip = false;
			if(std::abs(n.m_x) > eps) flip = n.m_x < 0;
			else if(std::abs(n.m_y) > eps) flip = n.m_y < 0;
			else flip = n.m_z < 0;
			if(flip) n = IBKMK::Vector3D(-n.m_x, -n.m_y, -n.m_z);
			// Distance via centroid · normal (centroid lies on plane).
			double d = s.centroid().scalarProduct(n);
			// Quantize to 1cm for grouping
			auto qd = static_cast<long long>(std::round(d / 0.01));
			auto qx = static_cast<long long>(std::round(n.m_x * 1000));
			auto qy = static_cast<long long>(std::round(n.m_y * 1000));
			auto qz = static_cast<long long>(std::round(n.m_z * 1000));
			return std::make_tuple(qx, qy, qz, qd);
		};
		using PlaneKey = std::tuple<long long,long long,long long,long long>;
		// Group by (elementEntityId, planeKey)
		std::map<std::pair<int,PlaneKey>, std::vector<std::shared_ptr<SpaceBoundary>>> groups;
		for(auto& sb : m_spaceBoundaries) {
			if(!sb->isConstructionElement() && !sb->isVirtual())
				continue;
			if(sb->m_elementEntityId < 0)
				continue;
			groups[{sb->m_elementEntityId, computePlaneKey(sb->surface())}].push_back(sb);
		}
		size_t coalescedGroups = 0, droppedSBs = 0;
		for(auto& kv : groups) {
			auto& group = kv.second;
			if(group.size() < 2)
				continue;
			std::vector<polygon3D_t> polys;
			polys.reserve(group.size());
			for(auto& sb : group)
				polys.push_back(sb->surface().polygon());
			PlaneNormal plane(group.front()->surface().polygon());
			if(!plane.m_valid)
				continue; // degenerate reference polygon (e.g. empty SB fragment) — cannot merge
			std::vector<CoplanarUnionRing> rings = unionCoplanarPolygons(polys, plane);
			if(rings.size() != 1 || !rings.front().m_holes.empty())
				continue; // disjoint or has holes — skip merge
			// Replace the group: keep the first SB, swap its surface for the merged one,
			// drop the others from m_spaceBoundaries.
			auto& kept = group.front();
			Surface mergedSurf(rings.front().m_outer);
			mergedSurf.set(kept->surface().id(), kept->surface().elementId(),
						   kept->surface().name(), kept->surface().isVirtual());
			kept->fetchGeometryFromBuildingElement(mergedSurf, convertOptions);
			++coalescedGroups;
			std::set<int> dropIds;
			for(size_t i = 1; i < group.size(); ++i) {
				dropIds.insert(group[i]->m_id);
				++droppedSBs;
			}
			m_spaceBoundaries.erase(
				std::remove_if(m_spaceBoundaries.begin(), m_spaceBoundaries.end(),
					[&dropIds](const std::shared_ptr<SpaceBoundary>& sb){
						return dropIds.count(sb->m_id);
					}),
				m_spaceBoundaries.end());
		}
		if(coalescedGroups > 0)
			Logger::instance() << "coalesce: space=" << m_id
							   << " merged " << coalescedGroups << " same-element coplanar groups"
							   << " (dropped " << droppedSBs << " fragmented SBs)";
	}

	// create two temporary vectors for construction space boundaries and opening space boundaries
	std::vector<std::shared_ptr<SpaceBoundary>> constructionSBs;
	std::vector<std::shared_ptr<SpaceBoundary>> openingSBs;
	for(auto sb : m_spaceBoundaries) {
		if(sb->isConstructionElement() || sb->isVirtual())
			constructionSBs.push_back(sb);
		if(sb->isOpeningElement())
			openingSBs.push_back(sb);
	}

	if(openingSBs.empty()) {
		// No authored opening SBs to attach — but the shell anchoring must STILL
		// run. Rooms without windows/doors boundaries (WCs, corridors, cellars)
		// otherwise keep their raw authored SB set: unanchored, unfilled, with
		// arbitrary winding — exactly the red "volume inconsistent" rooms in the
		// VICUS structure view.
		if(convertOptions.m_anchorSBsToSpaceShell)
			anchorSpaceBoundariesToShell(convertOptions);
		return true;
	}

//	std::map<int,std::vector<int>> parallelOpeningSBs;
//	for(size_t ci=0; ci<constructionSBs.size(); ++ci) {
//		const Surface& constrSurf = constructionSBs[ci]->surface();
//		for(size_t oi=0; oi<openingSBs.size(); ++oi) {
//			const Surface& openingSurf = openingSBs[oi]->surface();
//			if(constrSurf.isParallelTo(openingSurf))
//				parallelOpeningSBs[ci].push_back(oi);
//		}
//	}

	// Some IFC files (BBW_Haus D, etc.) have one IfcOpeningElement split into many
	// IfcRelSpaceBoundary fragments — up to 100+ per (opening, wall) pair. Each
	// fragment becomes a separate SubSurface in VICUS, blowing up the match rate
	// and producing visually wrong geometry. Within a single wall SB, keep only the
	// first opening SB per related-element GUID; drop subsequent fragments.
	auto dedupAddContainedOpening = [](const std::shared_ptr<SpaceBoundary>& constrSB,
									   const std::shared_ptr<SpaceBoundary>& openingSB) -> bool {
		const std::string& guid = openingSB->guidRelatedElement();
		if(!guid.empty()) {
			for(const auto& sb : constrSB->containedOpeningSpaceBoundaries()) {
				if(sb->guidRelatedElement() == guid)
					return false; // duplicate — drop
			}
		}
		constrSB->addContainedOpeningSpaceBoundaries(openingSB);
		return true;
	};

	// try to find out which opening sb is related to which construction sb
	std::vector<int> addedOpeningIds;
	for(auto openingSB : openingSBs) {
		// check if the opening sb is already added
		int id = openingSB->m_id;

		const Surface& opSurf = openingSB->surface();
		int notParallel = 0;
		int wrongDistance = 0;
		int notIntersected = 0;
		for(auto constrSB : constructionSBs ) {
			const Surface& constrSurf = constrSB->surface();

			if(std::find_if(addedOpeningIds.begin(),addedOpeningIds.end(), [id](int addedId) -> bool { return id == addedId; }) != addedOpeningIds.end())
				continue;

			// fetch thickness of construction element if exist
			int elementId = constrSB->m_elementEntityId;
			std::shared_ptr<BuildingElement> element = buildingElements.fromID(elementId);
			double searchDist = convertOptions.m_openingDistance;
			if(element)
				searchDist = std::max(element->thickness(),searchDist);

			// find subsurfaces in surfaces which have already cutted openings
			// in this case surface and subsurface must have same points
			std::vector<std::pair<size_t,size_t>> samepoints = constrSurf.samePoints(opSurf, convertOptions.m_distanceEps);
			if(samepoints.size() > 2) {
				// heal the construction surface by merging with subsurface
				constrSB->mergeSurface(opSurf);
				dedupAddContainedOpening(constrSB, openingSB);
				addedOpeningIds.push_back(openingSB->m_id);
				continue;
			}
			// check for parallel and intersected surfaces
			else {
				if(constrSurf.isParallelTo(opSurf, convertOptions.m_distanceEps)) {
					double dist = constrSurf.distanceToParallelPlane(opSurf, convertOptions.m_distanceEps);
					bool isIntersected = constrSurf.isIntersected(opSurf);
					if(dist <= searchDist*1.1 && isIntersected) {
						dedupAddContainedOpening(constrSB, openingSB);
						addedOpeningIds.push_back(openingSB->m_id);
						continue;
					}
					else {
						if(dist > searchDist*1.1) {
							++wrongDistance;
						}
						else if(!isIntersected) {
							++notIntersected;
						}
					}
				}
				else {
					++notParallel;
				}
			}
		}
		// check if and why the opening isn't found
		int constructionCount = constructionSBs.size();
		int failures = notParallel + wrongDistance + notIntersected;
		std::string failureReasonString;
		if(notParallel > 0)
			failureReasonString += "NP: " + std::to_string(notParallel) +  " : ";
		if(wrongDistance > 0)
			failureReasonString += "WD: " + std::to_string(wrongDistance) +  " : ";
		if(notIntersected > 0)
			failureReasonString += "NI: " + std::to_string(notIntersected);
		if(failures == constructionCount) {
			errors.push_back(ConvertError{OT_Space, m_id, "Opening space boundary id '" + std::to_string(openingSB->m_id) + "' has no connection because " + failureReasonString});
		}

	}

	// NOTE: linking Openings to their attached IFC openingSBs happens AFTER the
	// parallel per-space phase, in Space::linkOpeningsToSpaceBoundaries() — called
	// serially from BuildingStorey::updateSpaces. It writes into the shared
	// `openings` vector, which is not thread-safe from inside the OMP region.

	std::vector<std::shared_ptr<SpaceBoundary>> missingOpeningSBs;
	for(auto openingSB : openingSBs) {
		int id = openingSB->m_id;
		if(std::find_if(addedOpeningIds.begin(),addedOpeningIds.end(), [id](int addedId) -> bool { return id == addedId; }) == addedOpeningIds.end()) {
			missingOpeningSBs.push_back(openingSB);
		}
	}

	if(missingOpeningSBs.size() > 0) {
		for(auto sb : missingOpeningSBs) {
			std::map<int,int> distIndexMap;
			errors.push_back(ConvertError{OT_Space, m_id, "Opening space boundary id '" + std::to_string(sb->m_ifcId) + "' is not connected to a construction space boundary"});
//			for(size_t ci=0; ci<constructionSBs.size(); ++ci) {
//				const Surface& constrSurf = constructionSBs[ci]->surface();
//				if(constrSurf.isParallelTo(sb->surface()) && constrSurf.isIntersected(sb->surface())) {
//					double dist = constrSurf.distanceToParallelPlane(sb->surface());
//					distIndexMap[int(dist*10000)] = ci;
//				}
//			}
//			if(!distIndexMap.empty()) {
//				double dist = distIndexMap.begin()->first / 10000.0;
//				int smallestIndex = distIndexMap.begin()->second;
//			}
		}
	}

	// Re-anchor SBs onto the space's own solid shell and close remaining gaps.
	// Runs AFTER opening SBs are attached (the samePoints-based heal above needs the
	// original vertices) — attached opening SBs are re-clipped against the snapped
	// parent at write time anyway.
	if(convertOptions.m_anchorSBsToSpaceShell)
		anchorSpaceBoundariesToShell(convertOptions);

	return true;
}

void Space::linkOpeningsToSpaceBoundaries(const BuildingElementsCollector& buildingElements,
										  std::vector<Opening>& openings) {
	// IFC-path attaches opening SBs to construction SBs but doesn't update the
	// matching Opening. Without this, Building::updateStoreys' cross-space fallback
	// re-matches the opening and creates a SECOND attachment (named differently and
	// with different geometry source), producing duplicate SubSurfaces in the output.
	auto linkOpeningSBToOpening = [&](const std::shared_ptr<SpaceBoundary>& openingSB) {
		const std::string& sbGuid = openingSB->guidRelatedElement();
		if(sbGuid.empty())
			return;
		for(Opening& op : openings) {
			// Per-space check: internal doors/windows collect one SB per room side.
			if(op.hasSpaceBoundaryInSpace(m_guid))
				continue;
			// IFC4-style: SB references IfcWindow/IfcDoor — match window/door GUID via fill.
			bool matched = false;
			for(int eid : op.openingElementIds()) {
				std::shared_ptr<BuildingElement> be = buildingElements.fromID(eid);
				if(be && be->m_guid == sbGuid) {
					matched = true;
					break;
				}
			}
			// IFC2x3 / abstractBIM-style: SB references the IfcOpeningElement itself —
			// match against the Opening's own GUID (BBW_Haus D and similar).
			if(!matched && op.guid() == sbGuid)
				matched = true;
			if(matched) {
				openingSB->m_openingId = op.m_id;
				op.addSpaceBoundary(openingSB);
				return;
			}
		}
	};

	// Link Openings to their attached IFC openingSBs only if the resulting
	// SubSurface will be valid. Validation uses the FINAL constrSB->surface() (after
	// all merges) and matches what getVicusSurface() will do at write time. Openings
	// whose IFC SBs would produce invalid SubSurfaces stay unlinked, so the cross-space
	// fallback in Building::updateStoreys can attempt them with its own geometry.
	for(const auto& constrSB : m_spaceBoundaries) {
		if(!constrSB->isConstructionElement() && !constrSB->isVirtual())
			continue;
		for(const auto& openingSB : constrSB->containedOpeningSpaceBoundaries()) {
			// Replicate the validation chain from SpaceBoundary::getVicusSurface so the
			// link decision matches what's actually rendered. Skip if any check fails —
			// the cross-space fallback then has another chance to attach the opening.
			Surface probe = constrSB->surface();
			if(!probe.addSubSurface(openingSB->surface()))
				continue;
			const auto& subs = probe.subSurfaces();
			if(subs.empty() || !VICUS::Polygon2D(subs.back().polygon()).isValid())
				continue;
			linkOpeningSBToOpening(openingSB);
		}
	}
}



void Space::anchorSpaceBoundariesToShell(const ConvertOptions& convertOptions) {
	if(m_surfacesOrg.empty())
		return;
	// Perf guard: spaces with huge triangulated shells would make the
	// per-SB x per-face intersection sweep explode. Snap/fill (pass 1+2) is
	// skipped for those, but the orientation pass 3 below still runs — large
	// cellar/corridor rooms otherwise keep arbitrary windings and report
	// inconsistent volumes.
	const bool doAnchor = m_surfacesOrg.size() <= 2000;

	const double EPS = convertOptions.m_distanceEps;
	const double SNAP_TOL = convertOptions.m_shellSnapTolerance;
	// Unanchored SBs still shadow the fill up to this plane distance, so SBs authored
	// at the wall center plane (1st-level style) don't get doubled by a Missing SB
	// created on the shell face right in front of them.
	const double SHADOW_TOL = std::max(0.3, SNAP_TOL);

	// State shared between the anchoring passes and the always-on orientation pass.
	std::vector<int> anchoredFace(m_spaceBoundaries.size(), -1);
	std::vector<std::shared_ptr<SpaceBoundary>> fillSBs;
	double filledArea = 0.0;
	int snapped = 0;

	if(doAnchor) {
	// --- Pass 1: clip each construction/virtual SB against ALL shell faces it is
	// close to. One authored SB frequently spans several shell faces (e.g. an SB
	// covering a wall face plus the adjacent slab edge, or shells fragmented by
	// carve) — a single-best-face snap would lose most of its area. The largest
	// piece stays on the original SB, additional pieces become cloned SBs, and
	// attached opening SBs are redistributed to the piece they intersect.
	std::vector<std::shared_ptr<SpaceBoundary>> clonedSBs;
	std::vector<int> clonedFace;
	for(size_t sbi=0; sbi<m_spaceBoundaries.size(); ++sbi) {
		auto& sb = m_spaceBoundaries[sbi];
		if(!sb->isConstructionElement() && !sb->isVirtual())
			continue;
		const Surface& s = sb->surface();
		if(!s.isValid(EPS) || s.area() < convertOptions.m_minimumSurfaceArea)
			continue;

		struct Piece {
			int		m_faceIdx;
			Surface	m_clip;
		};
		std::vector<Piece> pieces;
		double totalClipArea = 0.0;
		// Authored SB normal (unit) — used for the angle-based candidate test below.
		IBKMK::Vector3D nS = newellNormal(s.polygon());
		double nSlen = std::sqrt(nS.m_x*nS.m_x + nS.m_y*nS.m_y + nS.m_z*nS.m_z);
		if(nSlen < 1e-10)
			continue;
		nS.m_x /= nSlen; nS.m_y /= nSlen; nS.m_z /= nSlen;

		for(size_t fi=0; fi<m_surfacesOrg.size(); ++fi) {
			const Surface& face = m_surfacesOrg[fi];
			// Angle-based candidate test instead of the strict isParallelTo(1e-3):
			// authored SBs are frequently tilted a fraction of a degree against the
			// space solid (WSHH cellar walls) and would never snap otherwise.
			IBKMK::Vector3D nF = newellNormal(face.polygon());
			double nFlen = std::sqrt(nF.m_x*nF.m_x + nF.m_y*nF.m_y + nF.m_z*nF.m_z);
			if(nFlen < 1e-10)
				continue;
			nF.m_x /= nFlen; nF.m_y /= nFlen; nF.m_z /= nFlen;
			double cosAngle = std::fabs(nS.m_x*nF.m_x + nS.m_y*nF.m_y + nS.m_z*nF.m_z);
			if(cosAngle < 0.999) // ~2.5 degrees
				continue;
			// Plane offset via centroid projection.
			IBKMK::Vector3D diff(s.centroid().m_x - face.centroid().m_x,
								 s.centroid().m_y - face.centroid().m_y,
								 s.centroid().m_z - face.centroid().m_z);
			double off = nF.m_x*diff.m_x + nF.m_y*diff.m_y + nF.m_z*diff.m_z;
			if(std::fabs(off) > SNAP_TOL)
				continue;
			// TRUE per-vertex projection onto the face plane (a pure translation is
			// not enough for tilted SBs — the vertices must land IN the plane).
			const IBKMK::Vector3D& p0 = face.centroid();
			polygon3D_t moved = s.polygon();
			for(IBKMK::Vector3D& v : moved) {
				double d = nF.m_x*(v.m_x-p0.m_x) + nF.m_y*(v.m_y-p0.m_y) + nF.m_z*(v.m_z-p0.m_z);
				v.m_x -= nF.m_x * d;
				v.m_y -= nF.m_y * d;
				v.m_z -= nF.m_z * d;
			}
			Surface clip = face.intersect(Surface(moved));
			if(!clip.isValid(EPS))
				continue;
			// Clean the clip result: clipper output can carry spikes / nearly-collinear
			// throwback vertices that VICUS' XML reader rejects after rounding.
			const IBKMK::Vector3D faceRefN = newellNormal(face.polygon());
			for(const Surface& part : clip.getSimplified()) {
				if(hasNonAdjacentDuplicateVertex(part.polygon()))
					continue; // self-touching ring — CDT would reject it
				double a = part.area();
				if(a < std::max(0.01, convertOptions.m_minimumSurfaceArea))
					continue;
				if(meanPolygonWidth(part) < 0.02)
					continue; // sliver strip — would collapse under XML rounding
				// Winding follows the shell face (carve solid faces are consistently
				// oriented outward) so the VICUS room volume stays consistent.
				pieces.push_back(Piece{(int)fi, alignedWinding(part, faceRefN)});
				totalClipArea += a;
			}
		}
		// Snap only when the shell keeps a substantial part of the SB — grazing
		// overlaps (e.g. an SB from the room above touching this shell) stay put.
		if(pieces.empty() || totalClipArea < 0.3 * s.area()) {
			Logger::instance() << "anchorShell: space=" << m_id
							   << " NOT-SNAPPED sb='" << sb->m_name << "'"
							   << " area=" << s.area()
							   << " pieces=" << pieces.size()
							   << " clippedArea=" << totalClipArea;
			continue;
		}
		std::sort(pieces.begin(), pieces.end(),
				  [](const Piece& a, const Piece& b){ return a.m_clip.area() > b.m_clip.area(); });

		// Save attached opening SBs, then redistribute them onto the pieces below.
		std::vector<std::shared_ptr<SpaceBoundary>> attachedOpenings = sb->containedOpeningSpaceBoundaries();
		sb->clearContainedOpeningSpaceBoundaries();

		// Largest piece keeps the original SB.
		Surface mainClip = pieces.front().m_clip;
		mainClip.set(s.id(), s.elementId(), s.name(), s.isVirtual());
		sb->fetchGeometryFromBuildingElement(mainClip, convertOptions);
		anchoredFace[sbi] = pieces.front().m_faceIdx;
		++snapped;

		std::vector<std::shared_ptr<SpaceBoundary>> pieceSBs{sb};
		for(size_t pi=1; pi<pieces.size(); ++pi) {
			std::shared_ptr<SpaceBoundary> clone(new SpaceBoundary(GUID_maker::instance().guid()));
			clone->setFromSpaceBoundaryWithSurface(*sb, pieces[pi].m_clip);
			clonedSBs.push_back(clone);
			clonedFace.push_back(pieces[pi].m_faceIdx);
			pieceSBs.push_back(clone);
		}

		// Reattach opening SBs. An opening frequently overlaps SEVERAL pieces (walls
		// fragmented by carve/anchoring) — cutting its hole only into the best piece
		// leaves the window poking through the neighbor piece without a hole there.
		// Split the opening SB: every piece it substantially intersects receives the
		// clipped part; the original keeps the largest part.
		for(const auto& openingSB : attachedOpenings) {
			struct OpeningPart {
				std::shared_ptr<SpaceBoundary> m_piece;
				Surface m_inter;
			};
			std::vector<OpeningPart> parts;
			for(const auto& piece : pieceSBs) {
				Surface inter = piece->surface().intersect(openingSB->surface());
				if(!inter.isValid(EPS))
					continue;
				if(inter.area() < 0.02)
					continue;
				parts.push_back(OpeningPart{piece, inter});
			}
			if(parts.empty()) {
				pieceSBs.front()->addContainedOpeningSpaceBoundaries(openingSB);
				continue;
			}
			std::sort(parts.begin(), parts.end(), [](const OpeningPart& a, const OpeningPart& b) {
				return a.m_inter.area() > b.m_inter.area();
			});
			if(parts.size() == 1) {
				parts[0].m_piece->addContainedOpeningSpaceBoundaries(openingSB);
				continue;
			}
			// original keeps the largest clipped part
			Surface mainInter = parts[0].m_inter;
			mainInter.set(openingSB->surface().id(), openingSB->surface().elementId(),
						  openingSB->surface().name(), openingSB->surface().isVirtual());
			openingSB->fetchGeometryFromBuildingElement(mainInter, convertOptions);
			parts[0].m_piece->addContainedOpeningSpaceBoundaries(openingSB);
			for(size_t opi=1; opi<parts.size(); ++opi) {
				std::shared_ptr<SpaceBoundary> clone(new SpaceBoundary(GUID_maker::instance().guid()));
				clone->setFromSpaceBoundaryWithSurface(*openingSB, parts[opi].m_inter);
				clone->m_openingId = openingSB->m_openingId;
				parts[opi].m_piece->addContainedOpeningSpaceBoundaries(clone);
			}
			Logger::instance() << "anchor-shell: SPLIT opening SB '" << openingSB->m_name
							   << "' across " << parts.size() << " wall pieces";
		}
	}
	if(!clonedSBs.empty()) {
		for(size_t ci=0; ci<clonedSBs.size(); ++ci) {
			m_spaceBoundaries.push_back(clonedSBs[ci]);
			anchoredFace.push_back(clonedFace[ci]);
		}
	}

	// --- Pass 1c: de-overlap the anchored SBs per shell face. Fragmented authored
	// boundary sets (WSHH cellars: dozens of Virtual fragments) snap several SBs
	// onto the SAME face region — stacked coplanar surfaces multiply into the room
	// volume ("volume exceeds bounding box") and litter the shell with open edges.
	// Opening-carrying SBs claim their region first, then larger ones; later SBs
	// are clipped to the unclaimed remainder (or emptied entirely).
	{
		std::map<int, std::vector<size_t>> sbsByFace;
		for(size_t sbi=0; sbi<anchoredFace.size() && sbi<m_spaceBoundaries.size(); ++sbi)
			if(anchoredFace[sbi] >= 0)
				sbsByFace[anchoredFace[sbi]].push_back(sbi);
		size_t deoverlapped = 0, emptied = 0;
		for(auto& kv : sbsByFace) {
			std::vector<size_t>& list = kv.second;
			if(list.size() < 2)
				continue;
			std::sort(list.begin(), list.end(), [this](size_t a, size_t b){
				bool oa = !m_spaceBoundaries[a]->containedOpeningSpaceBoundaries().empty();
				bool ob = !m_spaceBoundaries[b]->containedOpeningSpaceBoundaries().empty();
				if(oa != ob) return oa;
				return m_spaceBoundaries[a]->surface().area() > m_spaceBoundaries[b]->surface().area();
			});
			std::vector<Surface> claimed;
			const IBKMK::Vector3D refN = newellNormal(m_surfacesOrg[(size_t)kv.first].polygon());
			for(size_t sbi : list) {
				auto& sb = m_spaceBoundaries[sbi];
				Surface cur = sb->surface();
				bool changed = false;
				for(const Surface& c : claimed) {
					if(!cur.isValid(EPS))
						break;
					Surface::IntersectionResult ir = cur.intersect2(c);
					if(ir.m_intersections.empty())
						continue;
					// keep the largest remainder outside the claimed region
					Surface bestRem;
					double bestA = 0.0;
					for(const Surface& d : ir.m_diffBaseMinusClip) {
						double a = d.area();
						if(a > bestA) { bestA = a; bestRem = d; }
					}
					changed = true;
					if(bestA < std::max(0.01, convertOptions.m_minimumSurfaceArea)) {
						cur = Surface();
						break;
					}
					cur = alignedWinding(bestRem, refN);
				}
				if(!changed) {
					claimed.push_back(cur);
					continue;
				}
				++deoverlapped;
				if(!cur.isValid(EPS) || cur.area() < std::max(0.01, convertOptions.m_minimumSurfaceArea)
						|| meanPolygonWidth(cur) < 0.02) {
					// fully covered by earlier SBs — empty it (skipped by fill/export)
					Surface empty;
					empty.set(sb->surface().id(), sb->surface().elementId(), sb->surface().name(), sb->surface().isVirtual());
					sb->fetchGeometryFromBuildingElement(empty, convertOptions);
					anchoredFace[sbi] = -2; // no longer occupies the face, no shadow
					++emptied;
					continue;
				}
				cur.set(sb->surface().id(), sb->surface().elementId(), sb->surface().name(), sb->surface().isVirtual());
				sb->fetchGeometryFromBuildingElement(cur, convertOptions);
				claimed.push_back(sb->surface());
			}
		}
		if(deoverlapped > 0)
			Logger::instance() << "anchorShell: space=" << m_id
							   << " de-overlapped " << deoverlapped << " SBs (" << emptied << " emptied)";
	}

	// --- Pass 2: fill uncovered shell parts with Missing SBs. The shell faces come
	// from the space solid (closed by construction), so full coverage closes the
	// room volume.
	for(size_t fi=0; fi<m_surfacesOrg.size(); ++fi) {
		const Surface& face = m_surfacesOrg[fi];
		if(face.area() < convertOptions.m_minimumSurfaceArea)
			continue;
		std::vector<Surface> residuals{face};
		for(size_t sbi=0; sbi<m_spaceBoundaries.size() && !residuals.empty(); ++sbi) {
			auto& sb = m_spaceBoundaries[sbi];
			if(!sb->isConstructionElement() && !sb->isVirtual())
				continue;
			const Surface& s = sb->surface();
			if(!s.isValid(EPS))
				continue;
			bool subtract = false;
			if(anchoredFace[sbi] == (int)fi)
				subtract = true;
			else if(anchoredFace[sbi] < 0) {
				// Unanchored SB: shadows the fill if it lies (roughly) over this face.
				if(s.isParallelTo(face, EPS) &&
				   s.distanceToParallelPlane(face, EPS) <= SHADOW_TOL)
					subtract = true;
			}
			if(!subtract)
				continue;
			std::vector<Surface> next;
			for(const Surface& r : residuals) {
				Surface::IntersectionResult ir = r.intersect2(s);
				if(ir.m_intersections.empty()) {
					next.push_back(r);
					continue;
				}
				for(size_t di=0; di<ir.m_diffBaseMinusClip.size(); ++di) {
					// Align winding to the shell face BEFORE holes are attached
					// (their 2D coordinates depend on the parent plane orientation).
					Surface d = alignedWinding(ir.m_diffBaseMinusClip[di], newellNormal(face.polygon()));
					if(d.area() < convertOptions.m_minimumSurfaceArea)
						continue;
					// Keep hole rings (SB strictly inside the residual): the hole edge
					// matches the interior SB's outline, so the shell stays closed and
					// the fill doesn't double-cover the SB region. Holes need a valid
					// id — the raw clipper result carries id=-1, which the VICUS
					// writer rejects with an exception.
					for(const Surface& hole : ir.m_holesBaseMinusClip[di]) {
						if(hasNonAdjacentDuplicateVertex(hole.polygon()))
							continue; // degenerate hole ring — CDT would reject it
						Surface h = hole;
						h.set(GUID_maker::instance().guid(), -1, "Hole", false);
						d.addSubSurface(h);
					}
					next.push_back(d);
				}
			}
			residuals.swap(next);
		}
		for(const Surface& r : residuals) {
			// Clean spikes/collinear throwbacks from the clipper difference (VICUS'
			// reader rejects them after coordinate rounding). Residuals with holes
			// keep their geometry — getSimplified() would lose the hole rings —
			// UNLESS the outer ring is self-touching: then the ring split is forced
			// and the holes are dropped (a slight double-cover of the interior SB
			// beats emitting a CDT-breaking duplicate-vertex polygon).
			std::vector<Surface> parts;
			bool realign = false;
			if(r.holes().empty() || hasNonAdjacentDuplicateVertex(r.polygon())) {
				parts = r.getSimplified();
				realign = true; // getSimplified rebuilds rings with arbitrary winding
			}
			else
				parts.push_back(r); // already aligned in the diff step; keeps its holes
			const IBKMK::Vector3D fillRefN = newellNormal(face.polygon());
			for(Surface& part : parts) {
				if(realign)
					part = alignedWinding(part, fillRefN);
				if(hasNonAdjacentDuplicateVertex(part.polygon()))
					continue; // still self-touching after cleanup — skip
				if(part.area() < std::max(0.01, convertOptions.m_minimumSurfaceArea))
					continue;
				if(meanPolygonWidth(part) < 0.02)
					continue; // sliver strip — would collapse under XML rounding
				std::shared_ptr<SpaceBoundary> sb(new SpaceBoundary(GUID_maker::instance().guid()));
				sb->setForMissingElement("Missing", *this, false);
				if(sb->fetchGeometryFromBuildingElement(part, convertOptions)) {
					fillSBs.push_back(sb);
					filledArea += part.area();
				}
			}
		}
	}
	if(!fillSBs.empty())
		m_spaceBoundaries.insert(m_spaceBoundaries.end(), fillSBs.begin(), fillSBs.end());
	} // doAnchor
	if(anchoredFace.size() < m_spaceBoundaries.size())
		anchoredFace.resize(m_spaceBoundaries.size(), -1);

	// --- Pass 3: orientation heuristic for SBs that could NOT be anchored to a
	// shell face. Their authored winding is arbitrary; VICUS needs outward-facing
	// normals for a consistent room volume. Flip when the Newell normal points
	// towards the space centroid (exact for convex rooms, good default otherwise).
	// SBs carrying hole rings are skipped — their cached 2D hole coordinates
	// depend on the current plane orientation.
	{
		IBKMK::Vector3D spaceCentroid(0, 0, 0);
		size_t npts = 0;
		for(const Surface& sorg : m_surfacesOrg) {
			const IBKMK::Vector3D& c = sorg.centroid();
			spaceCentroid.m_x += c.m_x; spaceCentroid.m_y += c.m_y; spaceCentroid.m_z += c.m_z;
			++npts;
		}
		if(npts > 0) {
			spaceCentroid.m_x /= double(npts); spaceCentroid.m_y /= double(npts); spaceCentroid.m_z /= double(npts);
			for(size_t sbi=0; sbi<anchoredFace.size() && sbi<m_spaceBoundaries.size(); ++sbi) {
				if(anchoredFace[sbi] >= 0)
					continue; // snapped SBs already follow the shell face orientation
				auto& sb = m_spaceBoundaries[sbi];
				if(!sb->isConstructionElement() && !sb->isVirtual())
					continue;
				const Surface& surf = sb->surface();
				if(!surf.isValid(EPS) || !surf.holes().empty())
					continue;
				IBKMK::Vector3D n = newellNormal(surf.polygon());
				double nlen = std::sqrt(n.m_x*n.m_x + n.m_y*n.m_y + n.m_z*n.m_z);
				if(nlen < 1e-8)
					continue;
				const IBKMK::Vector3D& c = surf.centroid();
				double d = (n.m_x*(spaceCentroid.m_x-c.m_x) + n.m_y*(spaceCentroid.m_y-c.m_y)
							+ n.m_z*(spaceCentroid.m_z-c.m_z)) / nlen;
				if(d > 0.01) {
					// normal points into the room -> flip to outward
					polygon3D_t rev(surf.polygon().rbegin(), surf.polygon().rend());
					Surface flipped(rev);
					flipped.set(surf.id(), surf.elementId(), surf.name(), surf.isVirtual());
					sb->fetchGeometryFromBuildingElement(flipped, convertOptions);
				}
			}
		}
	}

	// --- Pass 4: chain remaining uncovered boundary edges into closing polygons.
	// Rooms whose IfcSpace tessellation solid is itself leaky (WSHH cellars) stay
	// open even after the shell fill — the shell faces simply do not cover the
	// gaps. Analogous to VICUS::Room::closingPolygons: collect all SB polygon
	// edges, find sub-segments not covered by any collinear counter-edge, chain
	// them into loops, and add each planar loop as a Missing SB.
	{
		struct GapEdge { IBKMK::Vector3D m_a, m_b; };
		std::vector<GapEdge> edges;
		for(const auto& sb : m_spaceBoundaries) {
			if(!sb->isConstructionElement() && !sb->isVirtual())
				continue;
			const Surface& surf = sb->surface();
			if(!surf.isValid(EPS))
				continue;
			const polygon3D_t& poly = surf.polygon();
			for(size_t i=0; i<poly.size(); ++i)
				edges.push_back(GapEdge{poly[i], poly[(i+1)%poly.size()]});
		}
		size_t gapLoops = 0;
		double gapArea = 0.0;
		if(edges.size() >= 3 && edges.size() <= 1500) {
			const double PT_TOL = 0.01;    // collinearity distance [m]
			const double CHAIN_TOL = 0.025; // loop chaining endpoint distance [m]
			const double MIN_SEG = 0.05;   // ignore stray segments below [m]

			auto sub = [](const IBKMK::Vector3D& a, const IBKMK::Vector3D& b){
				return IBKMK::Vector3D(a.m_x-b.m_x, a.m_y-b.m_y, a.m_z-b.m_z);
			};
			auto len3 = [](const IBKMK::Vector3D& v){
				return std::sqrt(v.m_x*v.m_x + v.m_y*v.m_y + v.m_z*v.m_z);
			};
			auto dot3 = [](const IBKMK::Vector3D& a, const IBKMK::Vector3D& b){
				return a.m_x*b.m_x + a.m_y*b.m_y + a.m_z*b.m_z;
			};

			struct GapSeg { IBKMK::Vector3D m_a, m_b; };
			std::vector<GapSeg> openSegs;
			for(size_t i=0; i<edges.size(); ++i) {
				IBKMK::Vector3D d = sub(edges[i].m_b, edges[i].m_a);
				double elen = len3(d);
				if(elen < MIN_SEG)
					continue;
				IBKMK::Vector3D dir(d.m_x/elen, d.m_y/elen, d.m_z/elen);

				std::vector<std::pair<double,double>> cov;
				for(size_t j=0; j<edges.size(); ++j) {
					if(j == i)
						continue;
					IBKMK::Vector3D dj = sub(edges[j].m_b, edges[j].m_a);
					double jlen = len3(dj);
					if(jlen < 1e-6)
						continue;
					// parallel? |cross(dir, dj/jlen)| = sin(angle)
					IBKMK::Vector3D cr(dir.m_y*dj.m_z - dir.m_z*dj.m_y,
									   dir.m_z*dj.m_x - dir.m_x*dj.m_z,
									   dir.m_x*dj.m_y - dir.m_y*dj.m_x);
					if(len3(cr)/jlen > 0.05)
						continue;
					// both endpoints of edge j close to line i?
					double t1 = dot3(sub(edges[j].m_a, edges[i].m_a), dir);
					IBKMK::Vector3D f1(edges[i].m_a.m_x + dir.m_x*t1, edges[i].m_a.m_y + dir.m_y*t1, edges[i].m_a.m_z + dir.m_z*t1);
					if(len3(sub(edges[j].m_a, f1)) > PT_TOL)
						continue;
					double t2 = dot3(sub(edges[j].m_b, edges[i].m_a), dir);
					IBKMK::Vector3D f2(edges[i].m_a.m_x + dir.m_x*t2, edges[i].m_a.m_y + dir.m_y*t2, edges[i].m_a.m_z + dir.m_z*t2);
					if(len3(sub(edges[j].m_b, f2)) > PT_TOL)
						continue;
					double lo = std::min(t1, t2), hi = std::max(t1, t2);
					lo = std::max(lo, 0.0); hi = std::min(hi, elen);
					if(hi - lo > 1e-4)
						cov.push_back({lo, hi});
				}
				std::sort(cov.begin(), cov.end());
				double cur = 0.0;
				auto emitSeg = [&](double a, double b){
					if(b - a < MIN_SEG)
						return;
					openSegs.push_back(GapSeg{
						IBKMK::Vector3D(edges[i].m_a.m_x + dir.m_x*a, edges[i].m_a.m_y + dir.m_y*a, edges[i].m_a.m_z + dir.m_z*a),
						IBKMK::Vector3D(edges[i].m_a.m_x + dir.m_x*b, edges[i].m_a.m_y + dir.m_y*b, edges[i].m_a.m_z + dir.m_z*b)});
				};
				for(const auto& c : cov) {
					if(c.first > cur)
						emitSeg(cur, c.first);
					cur = std::max(cur, c.second);
					if(cur >= elen)
						break;
				}
				if(cur < elen)
					emitSeg(cur, elen);
			}

			// chain open segments into closed loops
			auto ptsClose = [&](const IBKMK::Vector3D& a, const IBKMK::Vector3D& b){
				return len3(sub(a, b)) < CHAIN_TOL;
			};
			std::vector<char> used(openSegs.size(), 0);
			for(size_t i0=0; i0<openSegs.size(); ++i0) {
				if(used[i0])
					continue;
				std::vector<IBKMK::Vector3D> chain{openSegs[i0].m_a, openSegs[i0].m_b};
				used[i0] = 1;
				bool extended = true;
				while(extended && chain.size() < 120) {
					extended = false;
					for(size_t j=0; j<openSegs.size(); ++j) {
						if(used[j])
							continue;
						if(ptsClose(chain.back(), openSegs[j].m_a)) {
							chain.push_back(openSegs[j].m_b); used[j] = 1; extended = true; break;
						}
						if(ptsClose(chain.back(), openSegs[j].m_b)) {
							chain.push_back(openSegs[j].m_a); used[j] = 1; extended = true; break;
						}
					}
				}
				if(chain.size() < 4 || !ptsClose(chain.front(), chain.back()))
					continue;
				chain.pop_back();

				// planarity: all points close to the Newell plane through the centroid
				IBKMK::Vector3D n = newellNormal(chain);
				double nlen = len3(n);
				if(nlen < 1e-8)
					continue;
				IBKMK::Vector3D nu(n.m_x/nlen, n.m_y/nlen, n.m_z/nlen);
				IBKMK::Vector3D c(0,0,0);
				for(const IBKMK::Vector3D& v : chain) { c.m_x += v.m_x; c.m_y += v.m_y; c.m_z += v.m_z; }
				c.m_x /= chain.size(); c.m_y /= chain.size(); c.m_z /= chain.size();
				bool planar = true;
				for(const IBKMK::Vector3D& v : chain)
					if(std::fabs(dot3(sub(v, c), nu)) > 0.05) { planar = false; break; }
				if(!planar)
					continue;
				if(hasNonAdjacentDuplicateVertex(chain))
					continue;

				// outward winding: normal must point away from the space centroid
				{
					IBKMK::Vector3D sc(0,0,0);
					size_t np = 0;
					for(const Surface& sorg : m_surfacesOrg) {
						const IBKMK::Vector3D& oc = sorg.centroid();
						sc.m_x += oc.m_x; sc.m_y += oc.m_y; sc.m_z += oc.m_z;
						++np;
					}
					if(np > 0) {
						sc.m_x /= double(np); sc.m_y /= double(np); sc.m_z /= double(np);
						if(dot3(sub(sc, c), nu) > 0.0)
							std::reverse(chain.begin(), chain.end());
					}
				}

				Surface loopSurf(chain);
				double loopArea = loopSurf.area();
				if(loopArea < 0.05 || loopArea > 500.0 || meanPolygonWidth(loopSurf) < 0.02)
					continue;

				// NOTE: an overlap guard against existing coplanar surfaces was tried
				// here (rejecting loops >70% covered). It prevented 13 rooms from
				// flipping warning->error on WSHH, but blocked 47 rooms from turning
				// valid — user chose "more green": loops are always accepted.
				std::shared_ptr<SpaceBoundary> sb(new SpaceBoundary(GUID_maker::instance().guid()));
				sb->setForMissingElement("Missing", *this, false);
				if(sb->fetchGeometryFromBuildingElement(loopSurf, convertOptions)) {
					m_spaceBoundaries.push_back(sb);
					++gapLoops;
					gapArea += loopSurf.area();
				}
			}
		}
		if(gapLoops > 0)
			Logger::instance() << "anchorShell: space=" << m_id
							   << " closed " << gapLoops << " gap loops (area=" << gapArea << ")";
	}

	if(snapped > 0 || !fillSBs.empty())
		Logger::instance() << "anchorShell: space=" << m_id
						   << " snapped=" << snapped << "/" << m_spaceBoundaries.size() - fillSBs.size()
						   << " filled=" << fillSBs.size()
						   << " fillArea=" << filledArea;
}


std::vector<std::shared_ptr<SpaceBoundary>> Space::createConstructionSpaceBoundaries(
	const BuildingElementsCollector& buildingElements,
	std::vector<ConvertError>& errors,
	const ConvertOptions& convertOptions) {
	return createSpaceBoundaries_2(buildingElements, errors, convertOptions);
}

bool Space::finalizeConstructionSpaceBoundaries(
	std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries,
	const BuildingElementsCollector& buildingElements,
	std::vector<Opening>& openings,
	std::vector<ConvertError>& errors,
	const ConvertOptions& convertOptions) {
	createSpaceBoundariesForOpeningsFromSpaceBoundaries(spaceBoundaries, buildingElements, openings, errors, convertOptions);
	m_spaceBoundaries = spaceBoundaries;
	if(m_spaceBoundaries.empty()) {
		errors.push_back({OT_Space, m_id, "Cannot evaluate any space boundary for this space"});
		return false;
	}
	return true;
}

bool Space::evaluateSpaceBoundariesFromConstruction(const BuildingElementsCollector& buildingElements, std::vector<Opening>& openings,
									std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	std::vector<ConvertError> phase1Errors;
	auto sbs = createConstructionSpaceBoundaries(buildingElements, phase1Errors, convertOptions);
	errors.insert(errors.end(), phase1Errors.begin(), phase1Errors.end());
	return finalizeConstructionSpaceBoundaries(sbs, buildingElements, openings, errors, convertOptions);
}

bool Space::updateSpaceBoundaries(const objectShapeTypeVector_t& shapes,
								  shared_ptr<UnitConverter>& unit_converter,
								  const BuildingElementsCollector& buildingElements,
								  std::vector<Opening>& openings,
								  bool useSpaceBoundaries,
								  std::vector<ConvertError>& errors,
								  const ConvertOptions& convertOptions) {

	bool success;
	// update existing space boundaries from IFC
	if(useSpaceBoundaries && !m_spaceBoundaries.empty()) {
		// get space boundary types and set element id connections
		// convert geometry and create surfaces
		success = evaluateSpaceBoundaryFromIFC(shapes, buildingElements, unit_converter, errors, convertOptions);
	}
	// try to evaluate space boundaries from building element entities
	else {
		success = evaluateSpaceBoundariesFromConstruction(buildingElements, openings, errors, convertOptions);
	}
	return success;
}

const std::vector<std::shared_ptr<SpaceBoundary>>& Space::spaceBoundaries() const {
	return m_spaceBoundaries;
}

void Space::removeDublicatedSpaceBoundaries(const ConvertOptions& convertOptions) {
	if(m_spaceBoundaries.size() < 2)
		return;

	std::set<size_t> indicesToRemove;
	for(size_t i=0; i<m_spaceBoundaries.size()-1; ++i) {
		auto sb1 = m_spaceBoundaries[i];
		for(size_t j=i+1; j<m_spaceBoundaries.size(); ++j) {
			auto sb2 = m_spaceBoundaries[j];
			if(sb1->surface().isEqualTo(sb2->surface(), convertOptions.m_distanceEps))
				indicesToRemove.insert(j);
		}
	}
	if(!indicesToRemove.empty()) {
		std::vector<std::shared_ptr<SpaceBoundary>> tempSbs;
		for(size_t i=0; i<m_spaceBoundaries.size(); ++i) {
			if(indicesToRemove.count(i) == 0)
				tempSbs.push_back(m_spaceBoundaries[i]);
		}
		m_spaceBoundaries = tempSbs;
	}
}

std::vector<int> Space::checkUniqueSubSurfaces() const {
	std::vector<int> res;
	std::set<int> usedSubSurfaceIds;
	for(auto sb : m_spaceBoundaries) {
		for(auto subsb : sb->containedOpeningSpaceBoundaries()) {
			if(usedSubSurfaceIds.count(subsb->surface().id()) > 0) {
				res.push_back(subsb->m_id);
			}
			else {
				usedSubSurfaceIds.insert(subsb->surface().id());
			}
		}
	}
	return res;
}

void Space::checkForEqualSpaceBoundaries(std::vector<std::pair<int,int>>& equalSBs, const ConvertOptions& convertOptions) const {
	if(m_spaceBoundaries.size() < 2)
		return;

	for(size_t i=0; i<m_spaceBoundaries.size()-1; ++i) {
		auto sb1 = m_spaceBoundaries[i];
		for(size_t j=i+1; j<m_spaceBoundaries.size(); ++j) {
			auto sb2 = m_spaceBoundaries[j];
			if(sb1->surface().isEqualTo(sb2->surface(), convertOptions.m_distanceEps))
				equalSBs.push_back({sb1->m_id, sb2->m_id});
		}
	}
}

bool Space::isIntersected(const Space& other, const ConvertOptions& convertOptions) const {
	for(const auto& p1 : m_meshSets) {
		for(const auto& p2 : other.m_meshSets) {
			if(IFCC::isIntersected(p1.get(), p2.get()))
				return true;
		}
	}
	for(const auto& p1 : m_spaceBoundaries) {
		for(const auto& p2 : other.m_spaceBoundaries) {
			const Surface & surf1 = p1->surface();
			const Surface & surf2 = p2->surface().polygon();
			if(surf1.isValid(convertOptions.m_polygonEps) && surf2.isValid(convertOptions.m_polygonEps)) {
				if(IBKMK::polyIntersect(surf1.polygon(), surf2.polygon()))
					return true;
			}
		}
	}
	return false;
}

VICUS::Room Space::getVicusObject(const ConvertOptions& options) const {
	VICUS::Room vroom;
	vroom.m_id = m_id;
	if(!m_longName.empty())
		vroom.m_displayName = QString::fromStdString(m_longName + "_" + std::to_string(m_ifcId));
	else if(!m_name.empty())
		vroom.m_displayName = QString::fromStdString(m_name + "_" + std::to_string(m_ifcId));
	vroom.m_ifcGUID = m_guid;

	std::vector<VICUS::Surface> surfaces;
	for(const auto& sb : m_spaceBoundaries) {
		VICUS::Surface vsurf = sb->getVicusSurface(options);
		if(vsurf.m_id != INVALID_ID)
			surfaces.push_back(vsurf);
	}
	if(!surfaces.empty())
		vroom.setSurfaces(surfaces);

	return vroom;
}

TiXmlElement * Space::writeXML(TiXmlElement * parent, const ConvertOptions& convertOptions) const {
	if (m_id == -1)
		return nullptr;

	TiXmlElement * e = new TiXmlElement("Room");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_longName.empty())
		e->SetAttribute("displayName", m_longName + "_" + std::to_string(m_ifcId));
	else if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_ifcId));
//	e->SetAttribute("visible", IBK::val2string<bool>(true));

	bool sbsReady = false;
	for(auto sb : m_spaceBoundaries) {
		if(sb->checkSurface(convertOptions.m_polygonEps))
			sbsReady = true;
	}
	if(sbsReady) {
		TiXmlElement * child = new TiXmlElement("Surfaces");
		e->LinkEndChild(child);
		for(auto sb : m_spaceBoundaries) {
			sb->writeXML(child, convertOptions);
		}
	}
	return e;
}

bool Space::hasSpaceBoundary(const std::string &guid) const {
	for(const std::string& currguid : m_spaceBoundaryGUIDs) {
		if(currguid == guid)
			return true;
	}
	return false;
}

bool Space::shareSameSpaceBoundary(const Space &space) const {
	for(const std::string& currguid : m_spaceBoundaryGUIDs) {
		if(space.hasSpaceBoundary(currguid))
			return true;
	}
	return false;
}


} // namespace IFCC
