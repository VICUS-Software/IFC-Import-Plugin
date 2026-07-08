#include "IFCC_SubSurface.h"

#include <Carve/src/include/carve/carve.hpp>

#include <IBKMK_Polygon3D.h>

#include "IFCC_MeshUtils.h"
#include "IFCC_Surface.h"


namespace IFCC {

SubSurface::SubSurface(const std::vector<IBKMK::Vector3D>& polygon, const Surface& parentSurface) :
	m_id(-1),
	m_elementEntityId(-1),
	m_valid(false),
	m_planeNormal(parentSurface.polygon())
{
	m_valid = polygon.size() > 2;
	if(!m_valid)
		return;

	// Project to 2D using the SAME frame that IBKMK::Polygon3D will pick for the
	// parent at write-out time (offset = parent's first vertex, localX = first edge,
	// localY = normal × localX). The previous IFCC PlaneNormal::convert3DPoint used
	// a different frame, so 2D SubSurface coords didn't line up with the parent's
	// 2D coords as written to the VICUS XML — VICUS UI rendered windows in the
	// wrong place even when 3D geometry was correct.
	IBKMK::Polygon3D parentPoly3D(parentSurface.polygon());
	if(!parentPoly3D.isValid()) {
		m_valid = false;
		return;
	}
	const IBKMK::Vector3D offset = parentPoly3D.offset();
	const IBKMK::Vector3D normal = parentPoly3D.normal();
	const IBKMK::Vector3D localX = parentPoly3D.localX();
	IBKMK::Vector3D localY;
	normal.crossProduct(localX, localY);

	for(const IBKMK::Vector3D& vect : polygon) {
		IBKMK::Vector3D rel = vect - offset;
		IBKMK::Vector2D p2(rel.scalarProduct(localX), rel.scalarProduct(localY));
		m_polyVect.push_back(p2);
	}
}

void SubSurface::set(int id, const std::string& name, int elementId) {
	m_id = id;
	m_name = name;
	m_elementEntityId = elementId;
}

void SubSurface::flip(bool positive) {
	double area = areaSignedPolygon(m_polyVect);
	bool isPositive = area >= 0;
	if((isPositive && !positive) || (!isPositive && positive))
		std::reverse(m_polyVect.begin(), m_polyVect.end());
}


TiXmlElement * SubSurface::writeXML(TiXmlElement * parent, bool isHole) const {
	if (m_id == -1)
		return nullptr;

	std::string text = isHole ? "Hole" : "SubSurface";
	TiXmlElement * e = new TiXmlElement(text);
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_id));
//	e->SetAttribute("visible", IBK::val2string<bool>(true));

	writeXMLPolygon2D(m_polyVect, e);
	return e;
}

std::vector<IBKMK::Vector3D> SubSurface::polygon3D() const {
	std::vector<IBKMK::Vector3D> res;
	res.reserve(m_polyVect.size());
	for(const IBKMK::Vector2D& p : m_polyVect)
		res.push_back(m_planeNormal.convert3DPointInv(p));
	return res;
}

} // namespace IFCC
