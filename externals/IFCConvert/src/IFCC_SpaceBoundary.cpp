#include "IFCC_SpaceBoundary.h"

#include <ifcpp/IFC4X3/include/IfcConnectionCurveGeometry.h>
#include <ifcpp/IFC4X3/include/IfcConnectionPointEccentricity.h>
#include <ifcpp/IFC4X3/include/IfcConnectionPointGeometry.h>
#include <ifcpp/IFC4X3/include/IfcConnectionSurfaceGeometry.h>
#include <ifcpp/IFC4X3/include/IfcConnectionVolumeGeometry.h>
#include <ifcpp/IFC4X3/include/IfcSurface.h>
#include <ifcpp/IFC4X3/include/IfcSpace.h>
#include <ifcpp/IFC4X3/include/IfcExternalSpatialElement.h>
#include <ifcpp/IFC4X3/include/IfcFaceBasedSurfaceModel.h>
#include <ifcpp/IFC4X3/include/IfcFaceSurface.h>
#include <ifcpp/IFC4X3/include/IfcRelSpaceBoundary1stLevel.h>
#include <ifcpp/IFC4X3/include/IfcRelSpaceBoundary2ndLevel.h>


#include <sstream>

#include "IFCC_Helper.h"
#include "IFCC_Types.h"
#include "IFCC_RepresentationConverter.h"
#include "IFCC_Database.h"
#include "IFCC_Logger.h"
#include "IFCC_Space.h"


namespace IFCC {

SpaceBoundary::SpaceBoundary(int id) :
	EntityBase(id),
	m_elementEntityId(-1),
	m_openingId(-1),
	m_componentInstanceId(-1),
	m_typeRelatedElement(BET_All),
	m_physicalOrVirtual(IfcPhysicalOrVirtualEnum::ENUM_NOTDEFINED),
	m_internalOrExternal(IfcInternalOrExternalEnum::ENUM_NOTDEFINED),
	m_type(CT_Unknown),
	m_spaceBoundaryType(SBT_Unknown),
	m_levelType(SBLT_NoLevel)
{

}

bool SpaceBoundary::setFromIFC(std::shared_ptr<IfcRelSpaceBoundary> ifcSpaceBoundary, std::vector<ConvertError>& errors) {
	if(!EntityBase::set(dynamic_pointer_cast<IfcRoot>(ifcSpaceBoundary)))
		return false;

	if(ifcSpaceBoundary->m_RelatedBuildingElement != nullptr) {
		m_guidRelatedElement = guidFromObject(ifcSpaceBoundary->m_RelatedBuildingElement.get());
		m_nameRelatedElement = label2s(ifcSpaceBoundary->m_RelatedBuildingElement->m_Name);
	}

	if(ifcSpaceBoundary->m_RelatingSpace != nullptr) {
		shared_ptr<IfcSpace> space = dynamic_pointer_cast<IfcSpace>(ifcSpaceBoundary->m_RelatingSpace);
		if(space) {
			m_guidRelatedSpace = guidFromObject(space.get());
			m_nameRelatedSpace = label2s(space->m_Name);
		}
		else {
			shared_ptr<IfcExternalSpatialElement> externalSpace = dynamic_pointer_cast<IfcExternalSpatialElement>(ifcSpaceBoundary->m_RelatingSpace);
			if(externalSpace) {
				m_guidRelatedSpace = guidFromObject(externalSpace.get());
				m_nameRelatedSpace = label2s(externalSpace->m_Name);
			}
			else {
				m_nameRelatedSpace = "No space";
				errors.push_back(ConvertError{OT_SpaceBoundary, m_id, "No related space found for space boundary"});
			}
		}
	}

	if(ifcSpaceBoundary->m_PhysicalOrVirtualBoundary != nullptr) {
		m_physicalOrVirtual = ifcSpaceBoundary->m_PhysicalOrVirtualBoundary->m_enum;
		if(m_physicalOrVirtual == IfcPhysicalOrVirtualEnum::ENUM_VIRTUAL) {
			m_nameRelatedElement = "Virtual";
		}
	}
	if(ifcSpaceBoundary->m_InternalOrExternalBoundary != nullptr) {
		m_internalOrExternal = ifcSpaceBoundary->m_InternalOrExternalBoundary->m_enum;
	}

	if(m_name == "2ndLevel")
		m_levelType = SBLT_2ndLevel;
	if(m_name == "1stLevel")
		m_levelType = SBLT_1stLevel;
	if(m_description == "2a")
		m_spaceBoundaryType = SBT_A;
	if(m_description == "2b")
		m_spaceBoundaryType = SBT_B;

	shared_ptr<IfcRelSpaceBoundary1stLevel> sb1stLevel = std::dynamic_pointer_cast<IfcRelSpaceBoundary1stLevel>(ifcSpaceBoundary);
	if(sb1stLevel) {
		m_levelType = SBLT_1stLevel;
		if(sb1stLevel->m_ParentBoundary != nullptr) {
			m_spaceBoundaryType = SBT_Inner;
		}
	}

	shared_ptr<IfcRelSpaceBoundary2ndLevel> sb2ndLevel = std::dynamic_pointer_cast<IfcRelSpaceBoundary2ndLevel>(ifcSpaceBoundary);
	if(sb2ndLevel) {
		m_levelType = SBLT_2ndLevel;
		if(sb2ndLevel->m_CorrespondingBoundary != nullptr) {
			m_guidCorrespondingBoundary = guidFromObject(sb2ndLevel->m_CorrespondingBoundary.get());
			if(m_spaceBoundaryType != SBT_Inner)
				m_spaceBoundaryType = SBT_A;
		}
		else {
			if(m_spaceBoundaryType != SBT_Inner)
				m_spaceBoundaryType = SBT_B;
		}
	}

	m_connectionGeometry = ifcSpaceBoundary->m_ConnectionGeometry;
	if(!m_connectionGeometry) {
		errors.push_back(ConvertError{OT_SpaceBoundary, m_id, "Space boundary don't have a geometry"});
		return false;
	}
	return true;
}

void SpaceBoundary::setFromBuildingElement(const std::string& name, const std::shared_ptr<BuildingElement>& elem,
										   const Space& space) {
	m_name = name;
	setRelatingElementType(elem->type());
	m_physicalOrVirtual = IfcPhysicalOrVirtualEnum::ENUM_PHYSICAL;
	m_guidRelatedElement = elem->m_guid;
	m_nameRelatedElement = elem->m_name;
	m_guidRelatedSpace = space.m_guid;
	m_nameRelatedSpace = space.m_name;
}

void SpaceBoundary::setFromSpaceBoundary(const SpaceBoundary& sb, size_t surfaceIndex) {
	m_name = sb.m_name;
	m_description = sb.m_description;
	m_elementEntityId = sb.m_elementEntityId;
	m_guidRelatedElement = sb.m_guidRelatedElement;
	m_guidRelatedSpace = sb.m_guidRelatedSpace;
	m_guidCorrespondingBoundary = sb.m_guidCorrespondingBoundary;
	m_nameRelatedElement = sb.m_nameRelatedElement;
	m_nameRelatedSpace = sb.m_nameRelatedSpace;
	m_typeRelatedElement = sb.m_typeRelatedElement;
	m_physicalOrVirtual = sb.m_physicalOrVirtual;
	m_internalOrExternal = sb.m_internalOrExternal;
	m_type = sb.m_type;
	m_spaceBoundaryType = sb.m_spaceBoundaryType;
	m_levelType = sb.m_levelType;
	IBK_ASSERT(surfaceIndex > 0);
	IBK_ASSERT(surfaceIndex < sb.m_surfaces.size());
	m_surface = sb.m_surfaces[surfaceIndex];
	std::string name = m_nameRelatedElement;
	if(IBK::trim_copy(name).empty()) {
		name = m_name;
	}
	if(IBK::trim_copy(name).empty()) {
		if(!m_description.empty())
			name = m_description;
		else
			name = "surface";
	}
	m_surface.set(GUID_maker::instance().guid(), m_elementEntityId, name, isVirtual());
}

void SpaceBoundary::setForMissingElement(const std::string& name, const Space& space, bool isOpening) {
	m_name = name;
	m_typeRelatedElement = BET_None;
	if(isOpening)
		m_type = CT_OpeningElement;
	else
		m_type = CT_ConstructionElement;
	m_spaceBoundaryType = SBT_Unknown;
	m_levelType = SBLT_NoLevel;
	m_elementEntityId = -1;
	m_physicalOrVirtual = IfcPhysicalOrVirtualEnum::ENUM_PHYSICAL;
	m_guidRelatedElement.clear();
	m_nameRelatedElement.clear();
	m_guidRelatedSpace = space.m_guid;
	m_nameRelatedSpace = space.m_name;
}

void SpaceBoundary::setForVirtualElement(const std::string& name, const Space& space, bool isOpening) {
	m_name = name;
	m_typeRelatedElement = BET_None;
	if(isOpening)
		m_type = CT_OpeningElement;
	else
		m_type = CT_Others;
	m_spaceBoundaryType = SBT_Unknown;
	m_levelType = SBLT_NoLevel;
	m_elementEntityId = -1;
	m_physicalOrVirtual = IfcPhysicalOrVirtualEnum::ENUM_VIRTUAL;
	m_guidRelatedElement.clear();
	m_nameRelatedElement.clear();
	m_guidRelatedSpace = space.m_guid;
	m_nameRelatedSpace = space.m_name;
}

void SpaceBoundary::setRelatingElementType(BuildingElementTypes type) {
	m_typeRelatedElement = type;
	if(isConstructionType(type))
		m_type = CT_ConstructionElement;
	else if(isConstructionSimilarType(type))
		m_type = CT_ConstructionElement;
	else if(isOpeningType(type))
		m_type = CT_OpeningElement;
	else m_type = CT_Others;
}

bool SpaceBoundary::fetchGeometryFromIFC(shared_ptr<UnitConverter>& unit_converter,
										 const carve::math::Matrix& spaceTransformation, std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {

	polyVector_t polylines = polylinesFromConnectionGeometry(m_connectionGeometry, unit_converter, spaceTransformation,
															 m_id, errors);
	if(polylines.empty())
		return false;

	createSurfaceVect(polylines, m_ifcId, convertOptions);
	return true;
}

bool SpaceBoundary::fetchGeometryFromBuildingElement(const Surface& surface, const ConvertOptions& convertOptions) {
	m_surfaces.clear();
	if(surface.isValid(convertOptions.m_distanceEps)) {
		std::string name = m_nameRelatedElement;
		if(IBK::trim_copy(name).empty()) {
			name = m_name;
			if(IBK::trim_copy(name).empty()) {
				if(!m_description.empty())
					name = m_description;
				else
					name = "surface";
			}
		}
		m_surface = surface;
		m_surface.set(GUID_maker::instance().guid(), m_elementEntityId, name,
							  m_physicalOrVirtual == IfcPhysicalOrVirtualEnum::ENUM_VIRTUAL);
		m_surface.setSurfaceType(m_internalOrExternal);
		return true;
	}
	return false;
}

std::vector<std::shared_ptr<SpaceBoundary>> SpaceBoundary::splitBySurfaces() {
	std::vector<std::shared_ptr<SpaceBoundary>> res;
	if(m_surfaces.empty())
		return res;

	for(size_t i=1; i<m_surfaces.size(); ++i) {
		res.emplace_back(std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid())));
		res.back()->setFromSpaceBoundary(*this, i);
	}
	m_surface = m_surfaces.front();
	m_surfaces.clear();
	return res;
}

void SpaceBoundary::mergeSurface(const Surface& surf) {
	m_surface.merge(surf);
}

bool SpaceBoundary::checkAndHealSurface(bool healing, double epsilon) {
	bool res = m_surface.hasSimplePolygon();
	if(!res && healing) {
		std::vector<Surface> resSurfaces = m_surface.innerIntersection();
		if(resSurfaces.empty())
			return false;

		Surface copySurf = m_surface;
		m_surface = resSurfaces.front();
		m_surface.set(copySurf.id(), copySurf.elementId(), copySurf.name(), copySurf.isVirtual());

		if(resSurfaces.size() > 1)
			return false;

		res = m_surface.hasSimplePolygon();
	}
	// if checking before was true make an additional check if the surface is ready for export
	if(res)
		return m_surface.check(epsilon);

	return res;
}

bool SpaceBoundary::checkSurface(double epsilon) const {
	return m_surface.check(epsilon);
}

void SpaceBoundary::createSurfaceVect(const polyVector_t& polylines, int ifcid, const ConvertOptions& convertOptions) {
	m_surfaces.clear();
	std::string name;
	if(!m_nameRelatedElement.empty()) {
		name = "E:" + m_nameRelatedElement + "_S:" + m_nameRelatedSpace;
	}
	else {
		if(isVirtual())
			name = "Virtual SB";
		else if(isMissing())
			name = "SB with missing construction";
		else
			name = "Non valid SB";
	}
	IBK::trim(name);
	name += " : " + std::to_string(ifcid);

	for(const auto& polyvect3 : polylines) {
		for(const auto& polyvect2 : polyvect3) {
			for(const auto& polyvect1 : polyvect2) {
				if(!polyvect1.empty()) {
					m_surfaces.emplace_back(Surface(polyvect1));
					m_surfaces.back().set(GUID_maker::instance().guid(), m_elementEntityId, name,
										  m_physicalOrVirtual == IfcPhysicalOrVirtualEnum::ENUM_VIRTUAL);
					m_surfaces.back().setSurfaceType(m_internalOrExternal);
				}
			}
		}
	}

	// try to heal non valid surfaces
	std::vector<Surface> additionalSurfaces;
	for(auto& surf : m_surfaces) {
		if(!surf.hasSimplePolygon()) {
			std::vector<Surface> tmp = surf.innerIntersection();
			if(tmp.size() >= 1) {
				surf.setNewPolygon(tmp.front().polygon());
				for(size_t i=1; i<tmp.size(); ++i) {
					additionalSurfaces.push_back(tmp[i]);
					additionalSurfaces.back().set(GUID_maker::instance().guid(), m_elementEntityId, name,
												  m_physicalOrVirtual == IfcPhysicalOrVirtualEnum::ENUM_VIRTUAL);
					additionalSurfaces.back().setSurfaceType(m_internalOrExternal);
				}
			}
		}
	}
	if(!additionalSurfaces.empty()) {
		m_surfaces.insert(m_surfaces.end(), additionalSurfaces.begin(), additionalSurfaces.end());
	}

	// if have more than one surface try to merge
	if(m_surfaces.size() > 1) {
		std::vector<size_t> indicesToRemove;
		Surface baseSurf = m_surfaces.front();
		for(size_t i=1; i<m_surfaces.size(); ++i) {
			if(baseSurf.mergeOnlyThanPlanar(m_surfaces[i], convertOptions.m_distanceEps)) {
				indicesToRemove.push_back(i);
			}
		}
		if(!indicesToRemove.empty()) {
			std::vector<Surface> newSurfaces;
			newSurfaces.push_back(baseSurf);
			for(size_t i=1; i<m_surfaces.size(); ++i) {
				if(std::find(indicesToRemove.begin(), indicesToRemove.end(), i) == indicesToRemove.end())
					newSurfaces.push_back(m_surfaces[i]);
			}
			m_surfaces = newSurfaces;
		}
	}
}

const std::vector<std::shared_ptr<SpaceBoundary> >& SpaceBoundary::containedOpeningSpaceBoundaries() const {
	return m_containedOpeningSpaceBoundaries;
}

static std::vector<IBKMK::Vector2D> mapToParentPlane(const SpaceBoundary& sb, const Surface& surface) {
	std::vector<IBKMK::Vector2D> polyVect2D;
	PlaneNormal plane(sb.surface().polygon());
	for(const IBKMK::Vector3D& vect : surface.polygon()) {
		IBKMK::Vector2D p2 = plane.convert3DPoint(vect);
		polyVect2D.push_back(p2);
	}
	return polyVect2D;
}

void SpaceBoundary::addContainedOpeningSpaceBoundaries(const std::shared_ptr<SpaceBoundary>& containedOpeningSpaceBoundaries) {
	// check if opening is at wrong position or too big
	Surface::IntersectionResult res = m_surface.intersect2(containedOpeningSpaceBoundaries->surface());
	const std::vector<Surface>& sBmO = res.m_diffBaseMinusClip;
	const std::vector<Surface>& sOmB = res.m_diffClipMinusBase;
	int resIndex = 0;
	std::vector<SubSurface> subs;
	// opening is too big
	if(sBmO.empty()) {
		resIndex = 1;
	}
	// opening split the original surface into pices
	if(sBmO.size() > 1) {
		for(const Surface& surf : sBmO) {
			subs.emplace_back(SubSurface(surf.polygon(), m_surface.polygon()));
		}
		resIndex = 2;
	}
	// opening is too small
	if(sOmB.empty()) {
		resIndex = 3;
	}
	// opening create more than one hole
	if(sOmB.size() > 1) {
		for(const Surface& surf : sBmO) {
			subs.emplace_back(SubSurface(surf.polygon(), m_surface.polygon()));
		}
		resIndex = 4;
	}
	m_subsurfaces.push_back(mapToParentPlane(*this, containedOpeningSpaceBoundaries->surface()));
	m_containedOpeningSpaceBoundaries.push_back(containedOpeningSpaceBoundaries);
}

VICUS::Surface SpaceBoundary::getVicusSurface(const ConvertOptions& options) const {
	VICUS::Surface vsurf;
	if(isOpeningElement())
		return vsurf;

	Surface s = surfaceWithSubsurfaces();
	if(!s.check(options.m_polygonEps))
		return vsurf;

	// Create 3D polygon from the IFCC surface polygon - validate before proceeding
	const std::vector<IBKMK::Vector3D>& polyVect = s.polygon();
	if(polyVect.size() < 3) {
		Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
			<< ") has less than 3 vertices - skipping";
		return vsurf;
	}

	IBKMK::Polygon3D poly3D(polyVect);
	if(!poly3D.isValid()) {
		Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
			<< ") has invalid 3D polygon - skipping";
		return vsurf;
	}

	// Simulate the complete VICUS XML round-trip to detect precision-related issues.
	// writeXML uses default ostream precision (~6 significant digits) for 2D polyline
	// vertices and offset/normal/localX vectors. readXML reconstructs a Polygon2D from
	// the deserialized values, which runs eliminateCollinearPoints() and can change vertex
	// count or remove the (0,0) origin. It also reconstructs Polygon3D which validates
	// unit-length normals, orthogonality, and that first vertex is (0,0).
	// Any failure that would occur in readXML is detected here and the surface is skipped.
	{
		// Round-trip offset/normal/localX through toString()/fromString()
		IBKMK::Vector3D rtOffset, rtNormal, rtLocalX;
		try {
			rtOffset = IBKMK::Vector3D::fromString(poly3D.offset().toString());
			rtNormal = IBKMK::Vector3D::fromString(poly3D.normal().toString());
			rtLocalX = IBKMK::Vector3D::fromString(poly3D.localX().toString());
		}
		catch (...) {
			Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
				<< ") offset/normal/localX vectors fail serialization round-trip - skipping";
			return vsurf;
		}

		// Round-trip 2D polyline vertices through ostream/istream (matching writeXML precision)
		const std::vector<IBKMK::Vector2D>& polylineVerts = poly3D.polyline().vertexes();
		std::vector<IBKMK::Vector2D> rtVerts(polylineVerts.size());
		for(size_t i = 0; i < polylineVerts.size(); ++i) {
			std::stringstream ss;
			ss << polylineVerts[i].m_x << " " << polylineVerts[i].m_y;
			ss >> rtVerts[i].m_x >> rtVerts[i].m_y;
		}

		// Replicate the exact readXML validation sequence:
		// 1) Polygon2D(verts) + isValid check
		IBKMK::Polygon2D rtPoly2D(rtVerts);
		if(!rtPoly2D.isValid()) {
			Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
				<< ") polygon will not survive XML round-trip (invalid polyline) - skipping";
			return vsurf;
		}

		// 2) Polygon3D(Polygon2D(verts), offset, normal, localX) + isValid check
		//    This checks first vertex == (0,0), unit normals, orthogonality, and setRotation.
		IBKMK::Polygon3D rtPoly3D(rtPoly2D, rtOffset, rtNormal, rtLocalX);
		if(!rtPoly3D.isValid()) {
			Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
				<< ") polygon will not survive XML round-trip (invalid polygon3D) - skipping";
			return vsurf;
		}
	}

	// Set id, displayName, ifcGUID
	vsurf.m_id = s.id();
	vsurf.m_displayName = QString::fromStdString(s.name());
	vsurf.m_ifcGUID = m_guid;
	vsurf.setPolygon3D(poly3D);

	// Convert subsurfaces
	std::vector<VICUS::SubSurface> vicusSubSurfaces;
	for(const auto& sub : s.subSurfaces()) {
		if(!sub.isValid())
			continue;
		if(sub.isHole())
			continue;

		// Validate 2D polygon before creating VICUS subsurface
		const std::vector<IBKMK::Vector2D>& poly2D = sub.polygon();
		if(poly2D.size() < 3) {
			Logger::instance() << "Warning: SubSurface '" << sub.name() << "' (id " << sub.id()
				<< ") has less than 3 vertices - skipping";
			continue;
		}

		VICUS::Polygon2D vicusPoly2D(poly2D);
		if(!vicusPoly2D.isValid()) {
			Logger::instance() << "Warning: SubSurface '" << sub.name() << "' (id " << sub.id()
				<< ") has invalid 2D polygon - skipping";
			continue;
		}

		VICUS::SubSurface vsub;
		vsub.m_id = sub.id();
		vsub.m_displayName = QString::fromStdString(sub.name());
		vsub.m_polygon2D = vicusPoly2D;
		vicusSubSurfaces.push_back(vsub);
	}
	if(!vicusSubSurfaces.empty())
		vsurf.setSubSurfaces(vicusSubSurfaces);

	return vsurf;
}

TiXmlElement *SpaceBoundary::writeXML(TiXmlElement *parent, const ConvertOptions& convertOptions) const {
	if(!isOpeningElement()) {
		Surface s = surfaceWithSubsurfaces();
		if(!s.check(convertOptions.m_polygonEps))
			return nullptr;

		s.writeXML(parent, convertOptions);
	}
	return parent;
}

} // namespace IFCC
