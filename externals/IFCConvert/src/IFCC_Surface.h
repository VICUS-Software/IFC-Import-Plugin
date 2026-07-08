#ifndef IFCC_SurfaceH
#define IFCC_SurfaceH

#include <carve/mesh.hpp>

#include <tinyxml.h>

#include <ifcpp/IFC4X3/include/IfcInternalOrExternalEnum.h>

#include "IFCC_Types.h"
#include "IFCC_ConvertOptions.h"
#include "IFCC_Helper.h"
#include "IFCC_GeometricHelperClasses.h"
#include "IFCC_SubSurface.h"

namespace IFCC {

/*! Classe represents a surface which can be represented as splanar polygon.*/
class Surface
{
public:
	/*! Position of the surface relted to a building.*/
	enum PositionType {
		PT_Internal,
		PT_External,
		PT_External_Ground,
		PT_Unknown
	};

	/*! Can be this surface a side wall of a building element or an opening?*/
	enum SideType {
		ST_ProbableSide,	///< Its probable a side
		ST_UnProbableSide,	///< Being a side is unprobable
		ST_Unknown			///< We don't know anything
	};

	struct IntersectionResult;

	/*! Default constructor. Create a non valid object*/
	Surface();

	/*! Constructor create a surface from a carve Face object.
		\param face Face object in 3D from carve lib.
	*/
	Surface(carve::mesh::Face<3>* face);

	/*! Create a surface from a 3D polygon.
		\param polygon Single 3D polygon. It should be planar.
	*/
	Surface(const polygon3D_t& polygon);

	/*! Create a surface from a 3D polygon.
		\param polygon Single 3D polygon. It should be planar.
	*/
	Surface(const polygon3D_t& polygon, const std::vector<polygon3D_t>& childs);

	/*! Set the basic properties.
	 *  \param id GUID of the surface
	 *  \param elementId Id of the related element
	 *  \param name Name of the surface - used for write to xml
	 *  \param isVirtual If true its related to a virtual space boundary
	*/
	void set(int id, int elementId, const std::string& name, bool isVirtual);

	/*! Return the distance between two parallel surfaces in the unit of the coordinates.
		It return the maximum double number if the two surfaces are not parallel.
		\param other Other surface for checking.
	*/
	double distanceToParallelPlane(const Surface& other, double eps) const;

	/*! Return true if both surfaces are parallel.*/
	bool isParallelTo(const Surface& other, double eps) const;

	/*! Return true if both surfaces are equal.*/
	bool isEqualTo(const Surface& other, double eps) const;

	/*! Return the internal polygon.*/
	const std::vector<IBKMK::Vector3D>& polygon() const {
		return m_polyVect;
	}

	/*! Set a new in ternal üpolygon without changing the other parameter.*/
	void setNewPolygon(const std::vector<IBKMK::Vector3D>&);

	/*! Return true if the given surface intersects the original one.
		\param other Other surface for checking.
	*/
	bool isIntersected(const Surface& other) const;

	/*! Return the intersection area of the given surface to the original one.
		The returned surface is not valid in case of no intersection.
		\param other Other surface for intersecting.
	*/
	Surface intersect(const Surface& other) const;

	/*! Intersects the original surface with the given one.
		The result contains the intersection surfaces and the resulting difference surfaces ('org - other' and 'other - org').
		For more details \sa Surface::IntersectionResult.
		\param other Other surface for calculations.
		\return Object of type Surface::IntersectionResult contains all calculated surfaces and holes (if exist) of first level.
	*/
	Surface::IntersectionResult intersect2(const Surface& other) const;

	/*! Return the rest (difference) area of the original surface minus the given one.
		The result vector is empty in case of no intersection.
		\param other Vector of difference surfaces
	*/
	std::vector<Surface> difference(const Surface& other) const;

	/*! Perform an intersection with its own bonding rect.
	 *  Return the resulting intersections.
	 *  This can split the original surface into different ones in case of misfitted surface.
	*/
	std::vector<Surface> innerIntersection() const;

	/*! Merge the given subsurface into the current surface.
		If the original surface was intersected by this subsurface the resulting surface is complete without holes.
		The merging is performed in 2D level using clipper library (\sa mergePolygons).
		\return True if the subsurface has an intersection.
	*/
	bool merge(const Surface& subsurface);

	/*! Merge the given subsurface into the current surface.
		If the original surface was intersected by this subsurface the resulting surface is complete without holes.
		The merging is performed in 2D level using clipper library (\sa mergePolygons).
		\return True if the subsurface has an intersection.
	*/
	bool mergeOnlyThanPlanar(const Surface& subsurface, double eps);

	/*! Add the given surface as subsurface to the internal subsurface list.
		While this process the subsurface will be converted into SubSurface type which contains only a 2D polygon in the plane of the current surface.
		There are two kinds of subsurfaces, normal ones and holes. A normal subsurface has a corrsponding opening element.
		This function add the subsurface to the correct list depending on elementId of the subsurface.
		\param subsurface Surface which should be a subsurface of the current one
		\param virtualConnection Mark the subsurface as virtual air connection —
			   it is then exported as SubSurface, never as an (open) Hole.
		\return If false the resulting subsurface is not valid and nothing will be added
	*/
	bool addSubSurface(const Surface& subsurface, bool virtualConnection = false);

	/*! Check if current and given polygon contains similar points. If found it collects the indices of these points and return this vector.
		The function checks all points independent of the order.
		\param other Other surface for checking.
		\return vector of indices of similar points. First is the current polygon and second the other one.
	*/
	std::vector<std::pair<size_t,size_t>> samePoints(const Surface& other, double eps) const;

	/*! Return the area of the surface.*/
	double area() const;

	/*! Return the signed area of the main polygon.
		The sign represents the rotation type.
	*/
	double signedArea() const;

	/*! Create a 2D polygone which lays in the plane of the current 3D polygon.*/
	polygon2D_t polygon2DInPlane() const;

	/*! Flip the surface polygone.
		\param positive If true the subsurface polygones must have positive rotation otherwise negative.
	*/
	void flip(bool positive);

	/*! Clean and simplify the internal polygon.
	 *  This can lead to a splitting in more than one polygon.
	 *  The function return all created surfaces including the original one.
	 *  The internal surface will not be changed
	*/
	std::vector<Surface> getSimplified() const;

	/*! Write the surface in old vicus xml format including all subsurfaces.*/
	TiXmlElement * writeXML(TiXmlElement * parent, const ConvertOptions& options) const;

	/*! Set the position type of the surface.
		\param type Position type from IFC space boundary object.
	*/
	void setSurfaceType(IFC4X3::IfcInternalOrExternalEnum::IfcInternalOrExternalEnumEnum type);

	std::vector<SubSurface> subSurfaces() const {
		return m_subSurfaces;
	}

	std::vector<SubSurface> holes() const {
		return m_holes;
	}

	/*! Return the object id.*/
	int id() const {
		return m_id;
	}

	/*! Return the id of the corresponding building element.*/
	int elementId() const {
		return m_elementEntityId;
	}

	PositionType positionType() const {
		return m_positionType;
	}

	/*! Return if the object is valid.*/
	bool isValid(double eps) const;

	/*! Return name of the object.*/
	std::string name() const {
		return m_name;
	}

	bool isVirtual() const  {
		return m_virtualSurface;
	}

	bool isMissing() const {
		return m_elementEntityId == -1;
	}

	/*! Check if the basic 2D polygon have some line intersections.
	 *  Same method is used in SIM-VICUS in order to che validity of polygons.
	*/
	bool hasSimplePolygon() const;

	/*! Return side type.*/
	SideType sideType() const;

	/*! Set side type.*/
	void setSideType(SideType newSideType);

	/*! Check if the polygon is valid and ready to write or export.*/
	bool check(double epsilon) const;

	/*! Return cached axis-aligned bounding box minimum corner (lazy).
		Not thread-safe on first call — call once per surface serially before parallel use. */
	const IBKMK::Vector3D& aabbMin() const;

	/*! Return cached axis-aligned bounding box maximum corner (lazy). */
	const IBKMK::Vector3D& aabbMax() const;

	/*! True if this surface's AABB overlaps the other's AABB, expanded by eps on each side. */
	bool aabbOverlaps(const Surface& other, double eps) const;

	/*! Return the unit-length plane normal as a Vector3D (derived from the carve plane).
		Sign follows the carve plane convention — two co-planar surfaces facing each other
		have dot(n1, n2) < 0. */
	IBKMK::Vector3D planeNormalVec() const;

	/*! Return cached Hesse-normal-form plane representation (lazy, computed from m_polyVect). */
	const PlaneHesseNormal& cachedHesse() const;

	/*! Return the arithmetic mean of the polygon vertices (lazy). */
	const IBKMK::Vector3D& centroid() const;
private:
	/*! Compute and cache m_aabbMin / m_aabbMax from m_polyVect. Called on first access. */
	void ensureAabb() const;

	/*! Compute and cache the Hesse normal form. Called on first access. */
	void ensureHesse() const;

	/*! Compute and cache centroid (polygon vertex average). */
	void ensureCentroid() const;


	/*! Write the surface in old vicus xml format including all subsurfaces.*/
	TiXmlElement * writeXMLOld(TiXmlElement * parent, const ConvertOptions& options) const;

	/*! Write the surface in new vicus xml format including all subsurfaces.*/
	TiXmlElement * writeXMLNew(TiXmlElement * parent, const ConvertOptions& options) const;

	std::string								m_name;
	int										m_id;
	int										m_elementEntityId;
	PositionType							m_positionType;
	SideType								m_sideType;
	bool									m_virtualSurface;
	std::vector<IBKMK::Vector3D>			m_polyVect;
	std::vector<SubSurface>					m_subSurfaces;
	std::vector<SubSurface>					m_holes;
	std::vector<Surface>					m_childSurfaces;
	carve::geom::plane<3>					m_planeCarve;
	PlaneNormal								m_planeNormal;
	bool									m_canWriteSurface = false;

	mutable IBKMK::Vector3D					m_aabbMin;
	mutable IBKMK::Vector3D					m_aabbMax;
	mutable bool							m_aabbValid = false;

	mutable PlaneHesseNormal				m_hesseCached;
	mutable bool							m_hesseValid = false;

	mutable IBKMK::Vector3D					m_centroidCached;
	mutable bool							m_centroidValid = false;
};


/*! Contains results of function intersectPolygons2.
*/
struct Surface::IntersectionResult {

	/*! Result is only valid if at least one intersection exist and contains a valid polygon.*/
	bool isValid() const {
		if(m_intersections.empty())
			return false;
		for(const auto& poly : m_intersections)
			if(poly.polygon().size() > 2)
				return true;

		return false;
	}

	int holesWithChilds() const {
		return m_holesIntersectionChildCount + m_holesBaseMinusClipChildCount + m_holesClipMinusBaseChildCount;
	}

	/*! Vector of intersection surfaces.*/
	std::vector<Surface>				m_intersections;

	/*! Vector of hole polygones for each existing intersection-polygon.
		First dimension must be the same as m_intersections.
	*/
	std::vector<std::vector<Surface>>	m_holesIntersections;

	/*! Total number of childs of all holes in intersections.*/
	int									m_holesIntersectionChildCount = 0;

	/*! Vector of surfaces from operation 'BasePolygon - ClipPolygon'*/
	std::vector<Surface>				m_diffBaseMinusClip;

	/*! Vector of hole surfaces for each existing diffBaseMinusClip-polygon.
		First dimension must be the same as m_diffBaseMinusClip.
	*/
	std::vector<std::vector<Surface>>	m_holesBaseMinusClip;

	/*! Total number of childs of all holes in BaseMinusClip.*/
	int									m_holesBaseMinusClipChildCount = 0;

	/*! Vector of surfaces from operation 'ClipPolygon - BasePolygon'*/
	std::vector<Surface>				m_diffClipMinusBase;

	/*! Vector of hole surfaces for each existing diffClipMinusBase-polygon.
		First dimension must be the same as m_diffClipMinusBase.
	*/
	std::vector<std::vector<Surface>>	m_holesClipMinusBase;

	/*! Total number of childs of all holes in ClipMinusBase.*/
	int									m_holesClipMinusBaseChildCount = 0;
};

} // namespace IFCC

#endif // IFCC_SURFACE_H
