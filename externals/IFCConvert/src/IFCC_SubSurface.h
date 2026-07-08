#ifndef IFCC_SubSurfaceH
#define IFCC_SubSurfaceH

#include <tinyxml.h>

#include <IBKMK_Vector2D.h>

#include "IFCC_GeometricHelperClasses.h"

namespace IFCC {

class Surface;

/*! Represents a subsurface in a surface.
	The subsurface is defined as 2D polygon in a plane (of parent surface).
*/
class SubSurface
{
public:
	/*! Constructor.
		Create plane from parent polygon and convert given 3D polygon into 2D one in this plane.
		The object is only valid if the given polygon is also valid.
		\param polygon Polygon of the subsurface
		\param parentSurface Parent surface in whose plane the polygon will lie
	*/
	SubSurface(const std::vector<IBKMK::Vector3D>& polygon, const Surface& parentSurface);

	/*! Initialize the subsurface.*/
	void set(int id, const std::string& name, int elementId);

	/*! Replace the 2D polygon (used after merging with a coplanar same-element sub).*/
	void setPolygon2D(const std::vector<IBKMK::Vector2D>& polygon) {
		m_polyVect = polygon;
		m_valid = polygon.size() > 2;
	}

	/*! Return the 2D polygon.*/
	const std::vector<IBKMK::Vector2D>& polygon() const {
		return m_polyVect;
	}

	/*! Return the polygon converted back to 3D using the subsurface's own plane
		(the plane the 2D polygon was created in). Used for the VicIFC raw-geometry export. */
	std::vector<IBKMK::Vector3D> polygon3D() const;

	/*! Return if the object is valid.*/
	bool isValid() const {
		return m_valid;
	}

	/*! Return the object id.*/
	int id() const {
		return m_id;
	}

	/*! Return the id of the corresponding building element.*/
	int elementId() const {
		return m_elementEntityId;
	}

	/*! Return the name of the subsurface.*/
	std::string name() const {
		return m_name;
	}

	/*! Flip the surfrace polygone.
		\param positive Flip polygone if it doesn't fit to the given rotation type
	*/
	void flip(bool positive);

	/*! Return true if the subsurface don't have a corresponding opening element.
		Virtual connections (empty IfcOpeningElement passage/duct openings matched
		as 'breakout') are NOT holes: exported as VICUS::Hole they leave uncovered
		shell edges and flag every room they touch as open — they are air
		connections and are exported as SubSurfaces instead. */
	bool isHole() const { return m_elementEntityId < 0 && !m_virtualConnection; }

	/*! Mark this subsurface as a virtual air connection (see isHole()). */
	void setVirtualConnection(bool vc) { m_virtualConnection = vc; }

	/*! Write the subsurface in vicus xml format.*/
	TiXmlElement * writeXML(TiXmlElement * parent, bool isHole) const;

private:
	int										m_id;				///< Unique id of the object
	int										m_elementEntityId;	///< Id of the corresponding building element (opening)
	std::string								m_name;				///< Name of the subsurface
	bool									m_valid;			///< Validity of the object. Is set in constructor.
	bool									m_virtualConnection = false;	///< Virtual air connection (empty opening), never a hole
	std::vector<IBKMK::Vector2D>			m_polyVect;			///< 2D polygon of the subsurface
	PlaneNormal								m_planeNormal;		///< Plane in whose the polygone must lie
};

} // namespace IFCC

#endif // IFCC_SubSurface_H
