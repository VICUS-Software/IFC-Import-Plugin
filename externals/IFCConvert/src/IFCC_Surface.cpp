#include "IFCC_Surface.h"

#include <limits>
#include <fstream>
#include <sstream>

#include <IBK_math.h>
#include <IBK_assert.h>
#include <IBK_Line.h>

#include <Carve/src/include/carve/carve.hpp>

//#include "IFCC_MeshUtils.h"
#include "IFCC_Clippertools.h"
#include "IFCC_Helper.h"

//#define SURFACE_DUMP

namespace IFCC {

Surface::Surface() :
	m_id(-1),
	m_elementEntityId(-1),
	m_positionType(PT_Unknown),
	m_sideType(ST_Unknown),
	m_virtualSurface(false)
{
}

Surface::Surface(carve::mesh::Face<3>* face) :
	m_id(-1),
	m_elementEntityId(-1),
	m_positionType(PT_Unknown),
	m_sideType(ST_Unknown),
	m_virtualSurface(false)
{
	IBK_ASSERT(face != nullptr);

	std::vector<carve::mesh::Vertex<3>* > vertices;
	face->getVertices(vertices);

	for(size_t vi=0; vi<vertices.size(); ++vi) {
		double x = vertices[vi]->v.x;
		double y = vertices[vi]->v.y;
		double z = vertices[vi]->v.z;
		m_polyVect.emplace_back(IBKMK::Vector3D(x,y,z));
	}
	m_planeCarve = face->plane;
	PlaneHesseNormal hesse(face->plane);
	m_planeNormal = PlaneNormal(hesse, m_polyVect);
}

Surface::Surface(const polygon3D_t& polygon) :
	m_id(-1),
	m_elementEntityId(-1),
	m_positionType(PT_Unknown),
	m_sideType(ST_Unknown),
	m_virtualSurface(false),
	m_polyVect(polygon)
{
	if(m_polyVect.size() > 2) {
		m_planeNormal = PlaneNormal(polygon);
		PlaneHesseNormal planeHesseNormal(polygon);
		m_planeCarve.N = carve::geom::VECTOR(planeHesseNormal.m_n0.m_x,planeHesseNormal.m_n0.m_y,planeHesseNormal.m_n0.m_z);
		m_planeCarve.d = planeHesseNormal.m_d;
	}
}

Surface::Surface(const polygon3D_t &polygon, const std::vector<polygon3D_t> &childs)  :
	m_id(-1),
	m_elementEntityId(-1),
	m_positionType(PT_Unknown),
	m_sideType(ST_Unknown),
	m_virtualSurface(false),
	m_polyVect(polygon)
{
	if(m_polyVect.size() > 2) {
		m_planeNormal = PlaneNormal(polygon);
		PlaneHesseNormal planeHesseNormal(polygon);
		m_planeCarve.N = carve::geom::VECTOR(planeHesseNormal.m_n0.m_x,planeHesseNormal.m_n0.m_y,planeHesseNormal.m_n0.m_z);
		m_planeCarve.d = planeHesseNormal.m_d;
	}
	if(!childs.empty()) {
		for(const auto& poly : childs) {
			m_childSurfaces.emplace_back(Surface(poly));
		}
	}
}

void Surface::set(int id, int elementId, const std::string& name, bool isVirtual) {
	m_id = id;
	m_elementEntityId = elementId;
	m_name = name;
	m_virtualSurface = isVirtual;
}

static bool isCoLinear(const IBKMK::Vector3D& v1, const IBKMK::Vector3D& v2, bool &antiParallel, double eps) {
	double k = 0;
	antiParallel = false;
	bool xZero = nearZero(v1.m_x, eps) && nearZero(v2.m_x, eps);
	bool yZero = nearZero(v1.m_y, eps) && nearZero(v2.m_y, eps);
	bool zZero = nearZero(v1.m_z, eps) && nearZero(v2.m_z, eps);
	if( xZero) {
		// vector on z-axis
		if(yZero) {
			antiParallel = (v1.m_z * v2.m_z) < 0;
			return true;
		}

		// vector on y-axis
		if(zZero) {
			antiParallel = (v1.m_y * v2.m_y) < 0;
			return true;
		}

		// vector on y-z plane
		k = v1.m_y / v2.m_y;
		antiParallel = k < 0;
		return IBK::near_equal(k, v1.m_z / v2.m_z);
	}
	else {
		k = v1.m_x / v2.m_x;
		antiParallel = k < 0;

		// vector on x-axis
		if(yZero && zZero) {
			antiParallel = (v1.m_x * v2.m_x) < 0;
			return true;
		}
		// vector on x-z plane
		if(yZero) {
			return nearEqual(k, v1.m_z / v2.m_z, eps);
		}

		// vector on x-y plane
		if(zZero) {
			return nearEqual(k, v1.m_y / v2.m_y, eps);
		}

		// normal vector
		return nearEqual(k, v1.m_y / v2.m_y, eps) && nearEqual(k, v1.m_z / v2.m_z, eps);
	}
}

double Surface::distanceToParallelPlane(const Surface& other, double eps) const {
	const PlaneHesseNormal& phn1 = cachedHesse();
	const PlaneHesseNormal& phn2 = other.cachedHesse();

	bool antiParallel;
	bool isCoL = isCoLinear(phn1.m_n0, phn2.m_n0, antiParallel, eps);

	if(!isCoL)
		return 1e20;

	double k = antiParallel ? -1 : 1;

	double dist = std::fabs(phn1.m_d * k - phn2.m_d);

	return dist;
}

bool Surface::isParallelTo(const Surface& other, double eps) const {
	double a;
	double b;
	double c;

	carve::geom::vector<3> v1 = m_planeCarve.N;
	carve::geom::vector<3> v2 = other.m_planeCarve.N;

	if(nearZero(v2.x, eps)) {
		if(!nearZero(v1.x, eps))
			return false;
		a = 1;
	}
	else {
		a = v1.x / v2.x;
	}
	if(nearZero(v2.y, eps)) {
		if(!nearZero(v1.y, eps))
			return false;
		b = 1;
	}
	else {
		b = v1.y / v2.y;
	}
	if(nearZero(v2.z, eps)) {
		if(!nearZero(v1.z, eps))
			return false;
		c = 1;
	}
	else {
		c = v1.z / v2.z;
	}
	if(nearEqual(a,b, eps) && nearEqual(a,c, eps)) {
		return true;
	}

	carve::geom::vector<3> negV1 = v1.negated();
	if(nearZero(v2.x, eps)) {
		if(!nearZero(negV1.x, eps))
			return false;
		a = 1;
	}
	else {
		a = negV1.x / v2.x;
	}
	if(nearZero(v2.y, eps)) {
		if(!nearZero(negV1.y, eps))
			return false;
		b = 1;
	}
	else {
		b = negV1.y / v2.y;
	}
	if(nearZero(v2.z, eps)) {
		if(!nearZero(negV1.z, eps))
			return false;
		c = 1;
	}
	else {
		c = negV1.z / v2.z;
	}
	if(nearEqual(a,b, eps) && nearEqual(a,c, eps)) {
		return true;
	}
	return false;
}

bool Surface::isEqualTo(const Surface& other, double eps) const {
	const std::vector<IBKMK::Vector3D>& otherPoly = other.polygon();
	if(m_polyVect.size() != otherPoly.size())
		return false;

	// search point in the other polygon which is equal to the first point of the current one
	// store the position in start2nd
	int start2nd = -1;
	for(size_t i=0; i<otherPoly.size(); ++i) {
		if(nearEqual(otherPoly[i],m_polyVect.front(), eps))
			start2nd = i;
	}
	// no equal point found - polygones are not equal
	if(start2nd == -1)
		return false;

	// first point of both polygons is the same - perform normal search for all other points
	if(start2nd == 0) {
		for(size_t i=1; i<m_polyVect.size(); ++i) {
			if(!nearEqual(m_polyVect[i], otherPoly[i], eps))
				return false;
		}
	}
	// first point of the current polygon is equal to a later point of the other one
	else {
		// copy the other point and rotate it so the first point is the same as in the current one
		std::vector<IBKMK::Vector3D> otherCopy = other.polygon();
		std::rotate(otherCopy.begin(), otherCopy.begin() + start2nd, otherCopy.end());
		// check if all other points are equal
		for(size_t i=1; i<m_polyVect.size(); ++i) {
			if(!nearEqual(m_polyVect[i], otherCopy[i], eps))
				return false;
		}
	}
	return true;
}

void Surface::setNewPolygon(const std::vector<IBKMK::Vector3D> & polygon) {
	m_polyVect = polygon;
	m_aabbValid = false;
	m_hesseValid = false;
	m_centroidValid = false;
}

void Surface::ensureHesse() const {
	if(m_hesseValid)
		return;
	m_hesseCached = PlaneHesseNormal(m_polyVect);
	m_hesseValid = true;
}

const PlaneHesseNormal& Surface::cachedHesse() const {
	ensureHesse();
	return m_hesseCached;
}

void Surface::ensureCentroid() const {
	if(m_centroidValid)
		return;
	if(m_polyVect.empty()) {
		m_centroidCached = IBKMK::Vector3D(0, 0, 0);
		m_centroidValid = true;
		return;
	}
	double sx = 0, sy = 0, sz = 0;
	for(const IBKMK::Vector3D& v : m_polyVect) {
		sx += v.m_x; sy += v.m_y; sz += v.m_z;
	}
	double n = double(m_polyVect.size());
	m_centroidCached = IBKMK::Vector3D(sx / n, sy / n, sz / n);
	m_centroidValid = true;
}

const IBKMK::Vector3D& Surface::centroid() const {
	ensureCentroid();
	return m_centroidCached;
}

void Surface::ensureAabb() const {
	if(m_aabbValid)
		return;
	if(m_polyVect.empty()) {
		m_aabbMin = IBKMK::Vector3D(0, 0, 0);
		m_aabbMax = IBKMK::Vector3D(0, 0, 0);
		m_aabbValid = true;
		return;
	}
	double minX = m_polyVect[0].m_x, maxX = minX;
	double minY = m_polyVect[0].m_y, maxY = minY;
	double minZ = m_polyVect[0].m_z, maxZ = minZ;
	for(size_t i=1; i<m_polyVect.size(); ++i) {
		const IBKMK::Vector3D& v = m_polyVect[i];
		if(v.m_x < minX) minX = v.m_x; else if(v.m_x > maxX) maxX = v.m_x;
		if(v.m_y < minY) minY = v.m_y; else if(v.m_y > maxY) maxY = v.m_y;
		if(v.m_z < minZ) minZ = v.m_z; else if(v.m_z > maxZ) maxZ = v.m_z;
	}
	m_aabbMin = IBKMK::Vector3D(minX, minY, minZ);
	m_aabbMax = IBKMK::Vector3D(maxX, maxY, maxZ);
	m_aabbValid = true;
}

const IBKMK::Vector3D& Surface::aabbMin() const {
	ensureAabb();
	return m_aabbMin;
}

const IBKMK::Vector3D& Surface::aabbMax() const {
	ensureAabb();
	return m_aabbMax;
}

bool Surface::aabbOverlaps(const Surface& other, double eps) const {
	ensureAabb();
	other.ensureAabb();
	if(m_aabbMax.m_x + eps < other.m_aabbMin.m_x) return false;
	if(other.m_aabbMax.m_x + eps < m_aabbMin.m_x) return false;
	if(m_aabbMax.m_y + eps < other.m_aabbMin.m_y) return false;
	if(other.m_aabbMax.m_y + eps < m_aabbMin.m_y) return false;
	if(m_aabbMax.m_z + eps < other.m_aabbMin.m_z) return false;
	if(other.m_aabbMax.m_z + eps < m_aabbMin.m_z) return false;
	return true;
}

IBKMK::Vector3D Surface::planeNormalVec() const {
	const carve::geom::vector<3>& n = m_planeCarve.N;
	return IBKMK::Vector3D(n.x, n.y, n.z);
}


std::vector<std::pair<size_t,size_t>> Surface::samePoints(const Surface& other, double eps) const {
	std::vector<std::pair<size_t,size_t>> equalPoints;
	const std::vector<IBKMK::Vector3D>& otherPoly = other.polygon();
	for(size_t i=0; i<m_polyVect.size(); ++i) {
		for(size_t j=0; j<otherPoly.size(); ++j) {
			if(nearEqual(m_polyVect[i], otherPoly[j], eps))
				equalPoints.push_back(std::make_pair(i,j));
		}
	}
	return equalPoints;
}

bool Surface::isIntersected(const Surface& other) const {
	polygon3D_t result = intersectPolygons(m_polyVect, other.polygon(), m_planeNormal);
	if(result.empty() || areaPolygon(result) < 1e-4)
		return false;
	return true;
//	return intersects(m_polyVectOrg, other.polygon());
}

Surface Surface::intersect(const Surface& other) const {
	polygon3D_t result = intersectPolygons(m_polyVect, other.polygon(), m_planeNormal);
	if(result.empty() || areaPolygon(result) < 1e-4)
		return Surface();

	return Surface(result);
}

Surface::IntersectionResult Surface::intersect2(const Surface& other) const {
	// try to intersect both polygons
	IFCC::IntersectionResult tmp = intersectPolygons2(m_polyVect, other.polygon(), m_planeNormal);

	Surface::IntersectionResult result;

	// add intersections and holes in intersections
	result.m_holesIntersectionChildCount = tmp.m_holesIntersectionChildCount;
	for(size_t i=0; i<tmp.m_intersections.size(); ++i) {
		const polygon3D_t& poly = tmp.m_intersections[i];
		if(poly.size() > 3 && areaPolygon(poly) > 1e-4) {
			Surface currentSurface(poly, tmp.m_holesIntersections[i]);
			std::vector<Surface> simpleSurfaces = currentSurface.getSimplified();
			for(size_t j=0; j<simpleSurfaces.size(); ++j) {
				result.m_intersections.push_back(simpleSurfaces[j]);
				result.m_holesIntersections.push_back(std::vector<Surface>());
				for(const polygon3D_t& hole : tmp.m_holesIntersections[i]) {
					if(hole.size() > 3 && areaPolygon(hole) > 1e-4)
						result.m_holesIntersections.back().push_back(hole);
				}
			}
		}
	}

	// add the difference polygon for base minus other and their holes
	result.m_holesBaseMinusClipChildCount = tmp.m_holesBaseMinusClipChildCount;
	for(size_t i=0; i<tmp.m_diffBaseMinusClip.size(); ++i) {
		const polygon3D_t& poly = tmp.m_diffBaseMinusClip[i];
		if(poly.size() > 3 && areaPolygon(poly) > 1e-4) {
			Surface currentSurface(poly, tmp.m_holesBaseMinusClip[i]);
			std::vector<Surface> simpleSurfaces = currentSurface.getSimplified();
			for(size_t j=0; j<simpleSurfaces.size(); ++j) {
				result.m_diffBaseMinusClip.push_back(simpleSurfaces[j]);
				result.m_holesBaseMinusClip.push_back(std::vector<Surface>());
				for(const polygon3D_t& hole : tmp.m_holesBaseMinusClip[i]) {
					if(hole.size() > 3 && areaPolygon(hole) > 1e-4)
						result.m_holesBaseMinusClip.back().push_back(hole);
				}
			}
		}
	}

	// add the difference polygon for other minus base and their holes
	result.m_holesClipMinusBaseChildCount = tmp.m_holesClipMinusBaseChildCount;
	for(size_t i=0; i<tmp.m_diffClipMinusBase.size(); ++i) {
		const polygon3D_t& poly = tmp.m_diffClipMinusBase[i];
		if(poly.size() > 3 && areaPolygon(poly) > 1e-4) {
			Surface currentSurface(poly, tmp.m_holesClipMinusBase[i]);
			std::vector<Surface> simpleSurfaces = currentSurface.getSimplified();
			for(size_t j=0; j<simpleSurfaces.size(); ++j) {
				result.m_diffClipMinusBase.push_back(simpleSurfaces[j]);
				result.m_holesClipMinusBase.push_back(std::vector<Surface>());
				for(const polygon3D_t& hole : tmp.m_holesClipMinusBase[i]) {
					if(hole.size() > 3 && areaPolygon(hole) > 1e-4)
						result.m_holesClipMinusBase.back().push_back(hole);
				}
			}
		}
	}

	return result;
}

std::vector<Surface> Surface::difference(const Surface& other) const {
	IFCC::IntersectionResult tmp = intersectPolygons2(m_polyVect, other.polygon(), m_planeNormal);
	std::vector<Surface> result;
	for(size_t i=0; i<tmp.m_diffBaseMinusClip.size(); ++i) {
		const polygon3D_t& poly = tmp.m_diffBaseMinusClip[i];
		if(poly.size() > 3 && areaPolygon(poly) > 1e-4) {
			result.push_back(Surface(poly));
		}
	}
	return result;
}

std::vector<Surface> Surface::innerIntersection() const {
	std::vector<polygon3D_t> polys = intersectBoundingRect(m_polyVect, m_planeNormal);
	std::vector<Surface> result;
	for(const polygon3D_t& poly : polys) {
		if(!poly.empty() && areaPolygon(poly) > 1e-4) {
			result.push_back(Surface(poly));
		}
	}
	return result;
}


bool Surface::merge(const Surface& subsurface) {
	polygon3D_t result = mergePolygons(m_polyVect, subsurface.polygon(), m_planeNormal);
	if(result.empty())
		return false;

	m_polyVect = result;
	m_aabbValid = false;
	m_hesseValid = false;
	m_centroidValid = false;
	return true;
}

bool Surface::mergeOnlyThanPlanar(const Surface& surface, double eps) {
	if(!IBK::near_zero(distanceToParallelPlane(surface, eps)))
		return false;

	polygon3D_t result = mergePolygons(m_polyVect, surface.polygon(), m_planeNormal);
	if(result.empty())
		return false;

	m_polyVect = result;
	m_aabbValid = false;
	m_hesseValid = false;
	m_centroidValid = false;
	return true;
}

bool Surface::addSubSurface(const Surface& subsurface) {
	// Clip the candidate subsurface polygon against this parent surface in the
	// parent's plane. IFC opening polygons commonly span beyond the wall
	// fragment they're attached to (interior doors that span two adjacent walls,
	// or windows whose IFC ConnectionGeometry uses the unsplit wall coordinates
	// while the matched constructionSB is one of the split fragments). Without
	// clipping the result has SubSurface vertices outside the parent polygon —
	// VICUS UI then renders the window past the wall edges or as garbage.
	// Empty clipping result means the candidate doesn't fit on this parent at all
	// — reject so the caller can attach the opening to a different parent.
	const polygon3D_t clipped = intersectPolygons(m_polyVect, subsurface.polygon(), m_planeNormal);
	if(clipped.size() < 3)
		return false;

	SubSurface sub(clipped, *this);
	if(!sub.isValid())
		return false;

	sub.set(subsurface.id(), subsurface.m_name, subsurface.m_elementEntityId);
	if(sub.isHole())
		m_holes.push_back(sub);
	else
		m_subSurfaces.push_back(sub);
	return true;
}

double Surface::area() const {
	return areaPolygon(m_polyVect);
}

double Surface::signedArea() const {
	polygon2D_t poly2D = polygon2DInPlane();
	return areaSignedPolygon(poly2D);
}

polygon2D_t Surface::polygon2DInPlane() const {
	polygon2D_t polygon2DBase;
	for(const IBKMK::Vector3D& vect : m_polyVect) {
		IBKMK::Vector2D p2 = m_planeNormal.convert3DPoint(vect);
		polygon2DBase.push_back(p2);
	}

	return polygon2DBase;
}

void Surface::flip(bool positive) {
	std::reverse(m_polyVect.begin(), m_polyVect.end());
	for(auto& sub : m_subSurfaces) {
		sub.flip(positive);
	}
}

std::vector<Surface> Surface::getSimplified() const {
	std::vector<Surface> res;
	std::vector<IBKMK::Vector3D> tmp = m_polyVect;
	cleanPolygon(tmp);
	std::vector<polygon3D_t> polysSimple = simplifyPolygon(tmp);

	if(polysSimple.empty())
		return res;

	for(size_t i=0; i<polysSimple.size(); ++i) {
		res.push_back(Surface(polysSimple[i]));
	}
	return res;
}

static bool checkSimVicusValid(const std::vector<IBKMK::Vector3D>& polygon, double epsilon) {
	IBKMK::Polygon3D ibkPoly;
	return ibkPoly.setVertexes(polygon, true);
}

bool Surface::check(double epsilon) const {
	if (m_id == -1)
		return false;

	try {
		IBKMK::Polygon3D poly3D(m_polyVect);

		if(poly3D.vertexes().empty())
			return false;

		if(!poly3D.isValid())
			return false;

		if(IBK::near_zero(poly3D.vertexes().front().m_x) || IBK::near_zero(poly3D.vertexes().front().m_y))
			return false;
	}
	catch(IBK::Exception& e) {
		return false;
	}
	return true;
}

TiXmlElement * Surface::writeXML(TiXmlElement * parent, const ConvertOptions& options) const {
	// don't write in case of no valid polygon
	if(!check(options.m_polygonEps))
		return nullptr;

	if(options.m_useOldPolygonWriting)
		return writeXMLOld(parent, options);

	return writeXMLNew(parent, options);
}

TiXmlElement * Surface::writeXMLOld(TiXmlElement * parent, const ConvertOptions& options) const {

	TiXmlElement * e = new TiXmlElement("Surface");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_id));
//	e->SetAttribute("visible", IBK::val2string<bool>(true));

	if(!m_polyVect.empty() && checkSimVicusValid(m_polyVect, options.m_polygonEps)) {
		TiXmlElement * child = new TiXmlElement("Polygon3D");
		e->LinkEndChild(child);

		std::stringstream vals;
		for (unsigned int i=0; i<m_polyVect.size(); ++i) {
			vals << m_polyVect[i].m_x << " " << m_polyVect[i].m_y << " " << m_polyVect[i].m_z;
			if (i<m_polyVect.size()-1)  vals << ", ";
		}
		TiXmlText * text = new TiXmlText( vals.str() );
		child->LinkEndChild( text );
	}
	if(!m_subSurfaces.empty()) {
		TiXmlElement * child = new TiXmlElement("SubSurfaces");
		e->LinkEndChild(child);

		for( const SubSurface& subsurface : m_subSurfaces) {
			subsurface.writeXML(child, false);
		}
	}
	if(!m_holes.empty()) {
		TiXmlElement * child = new TiXmlElement("Holes");
		e->LinkEndChild(child);

		for( const SubSurface& subsurface : m_holes) {
			subsurface.writeXML(child, true);
		}
	}
	return e;
}

TiXmlElement * Surface::writeXMLNew(TiXmlElement * parent, const ConvertOptions& options) const {
	try {

		IBKMK::Polygon3D poly3D(m_polyVect);

		TiXmlElement * e = new TiXmlElement("Surface");
		parent->LinkEndChild(e);

		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
		if (!m_name.empty())
			e->SetAttribute("displayName", m_name + "_" + std::to_string(m_id));


		TiXmlElement * polyChild = new TiXmlElement("Polygon3D");
		e->LinkEndChild(polyChild);

		// encode vectors
		polyChild->SetAttribute("offset", poly3D.offset().toString());
		polyChild->SetAttribute("normal", poly3D.normal().toString());
		polyChild->SetAttribute("localX", poly3D.localX().toString());

		std::stringstream vals;
		const std::vector<IBKMK::Vector2D> & polyVertexes = poly3D.polyline().vertexes();
		for (unsigned int i=0; i<polyVertexes.size(); ++i) {
			vals << polyVertexes[i].m_x << " " << polyVertexes[i].m_y;
			if (i<polyVertexes.size()-1)  vals << ", ";
		}
		TiXmlText * text = new TiXmlText( vals.str() );
		polyChild->LinkEndChild( text );

		if(!m_subSurfaces.empty()) {
			TiXmlElement * child = new TiXmlElement("SubSurfaces");
			e->LinkEndChild(child);

			for( const SubSurface& subsurface : m_subSurfaces) {
				subsurface.writeXML(child, false);
			}
		}
		if(!m_holes.empty()) {
			TiXmlElement * child = new TiXmlElement("Holes");
			e->LinkEndChild(child);

			for( const SubSurface& subsurface : m_holes) {
				subsurface.writeXML(child, true);
			}
		}
		return e;
	}
	catch(IBK::Exception& e) {
		return nullptr;
	}
}

void Surface::setSurfaceType(IFC4X3::IfcInternalOrExternalEnum::IfcInternalOrExternalEnumEnum type) {
	switch(type) {
		case IFC4X3::IfcInternalOrExternalEnum::ENUM_INTERNAL: m_positionType = Surface::PT_Internal; break;
		case IFC4X3::IfcInternalOrExternalEnum::ENUM_EXTERNAL: m_positionType = Surface::PT_External; break;
		case IFC4X3::IfcInternalOrExternalEnum::ENUM_EXTERNAL_EARTH: m_positionType = Surface::PT_External_Ground; break;
		case IFC4X3::IfcInternalOrExternalEnum::ENUM_EXTERNAL_WATER:
		case IFC4X3::IfcInternalOrExternalEnum::ENUM_EXTERNAL_FIRE:
		case IFC4X3::IfcInternalOrExternalEnum::ENUM_NOTDEFINED: m_positionType = Surface::PT_Unknown; break;
	}
}

bool Surface::isValid(double eps) const {
	std::vector<IBKMK::Vector3D> tempVect;
	for(auto v : m_polyVect) {
		if(tempVect.empty())
			tempVect.push_back(v);
		else {
			bool isThere = false;
			for(auto v2 : tempVect) {
				if(nearEqual(v, v2, eps)) {
					isThere = true;
					break;
				}
			}
			if(!isThere)
				tempVect.push_back(v);
		}
	}
	return tempVect.size() > 2;
}

bool Surface::hasSimplePolygon() const {
	polygon2D_t poly2D = polygon2DInPlane();


	std::vector<IBK::Line>	lines;
	for (unsigned int i=0, vertexCount = poly2D.size(); i<vertexCount; ++i) {
		double ax1 = poly2D[i].m_x;
		double ax2 = poly2D[(i+1) % vertexCount].m_x;
		double ay1 = poly2D[i].m_y;
		double ay2 = poly2D[(i+1) % vertexCount].m_y;
		double rx = ax2-ax1;
		double ry = ay2-ay1;

		// zero line length check
		if(rx*rx + ry*ry <= 0) {
			return false;
		}

		lines.emplace_back(IBK::Line(IBK::point2D<double>(ax1,ay1), IBK::point2D<double>(ax2,ay2)));
	}
	if (lines.size() < 4)
		return true;

	for (unsigned int i=0; i<lines.size();++i) {
		for (unsigned int j=0; j<lines.size()-2; ++j) {
			unsigned int k1 = (i+1)%lines.size();
			unsigned int k2 = (i-1);
			if(i==0)
				k2 = lines.size()-1;
			if(i==j || k1 == j || k2 == j )
				continue;
			//int k = (i+j+2)%lines.size();
			IBK::point2D<double> p;
			IBK::point2D<double> p2;
			if (lines[i].intersects(lines[j], p, p2) > 0)
				return false;
		}
	}

	return true;
}

Surface::SideType Surface::sideType() const {
	return m_sideType;
}

void Surface::setSideType(SideType newSideType) {
	m_sideType = newSideType;
}


} // namespace IFCC
