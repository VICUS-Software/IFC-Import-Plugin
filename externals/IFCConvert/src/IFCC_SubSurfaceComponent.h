#ifndef IFCC_SubSurfaceComponentH
#define IFCC_SubSurfaceComponentH

#include <vector>

#include <IBK_math.h>

#include <tinyxml.h>

// VICUS::SubSurfaceComponent has been removed in the new SIM-VICUS API
// (component layer flattened — SubSurfaceComponentInstance now references a
// Construction/Window directly via m_idConstruction). This class is now an
// importer-internal staging structure only and no longer maps to a VICUS type.

namespace IFCC {

/*! Class contains all informations for a subsurface component.
	A subsurface component connects a opening construction with boundary conditions and further informations.
*/
class SubSurfaceComponent
{
public:

	/*! SubSurfaceComponent types. */
	enum SubSurfaceComponentType {
		CT_Window,					///< Window
		CT_Door,					///< Door
		CT_Miscellaneous,			///< All others
		NUM_CT
	};

	/*! Standard constructor.*/
	SubSurfaceComponent();

	/*! Standard constructor with arguments.
		\param id Unique id of the object
		\param guid GUID of the connected building element
		\param name Name of the object
	*/
	SubSurfaceComponent(int id, const std::string guid, const std::string& name);

	/*! Set parameter for a window. The type will be set to CT_Window
		\param id Id of the window object
	*/
	void setWindow(int id);

	/*! Set parameter for a door. The type will be set to CT_Door
		\param id Id of the door construction object
	*/
	void setDoor(int id);

	/*! Set parameter for a object which is wether window nor door.
		The type will be set to CT_Miscellaneous
		\param id Id of the construction object
	*/
	void setOther(int id);

	/*! Return the object ID.*/
	int id() const {
		return m_id;
	}

	/*! Return the object ID.*/
	std::string guid() const {
		return m_guid;
	}

	/*! Assign a display color (hex string, e.g. "#7AB3FF"). Used by
		Database::unifyComponents to color components after deduplication.
	*/
	void setColor(const std::string& color) {
		m_color = color;
	}

	/*! Current display color (hex string, empty if unset). */
	const std::string& color() const {
		return m_color;
	}

	/*! Return the subsurface component type (window/door/misc). */
	SubSurfaceComponentType typeValue() const {
		return m_type;
	}

	/*! Write the component in vicus xml format.*/
	TiXmlElement * writeXML(TiXmlElement * parent) const;

	/*! Window-construction ID set via setWindow(). Returns -1 if this component
		does not represent a window (use constructionId() for doors/misc). */
	int windowId() const { return m_windowId; }

	/*! Construction ID set via setDoor()/setOther(). Returns -1 for windows
		(use windowId() instead). */
	int constructionId() const { return m_constructionId; }

	/*! Comparison operator.*/
	friend bool operator==(const SubSurfaceComponent& left, const SubSurfaceComponent& right) {
		if(left.m_type != right.m_type)
			return false;
		if(left.m_windowId != right.m_windowId)
			return false;
		if(left.m_constructionId != right.m_constructionId)
			return false;
		return true;
	}

private:
	/*! Create name of subsurface component type for writing.*/
	std::string type2String(SubSurfaceComponentType type) const;

	int									m_id;
	std::string							m_color;
	std::string							m_name;
	std::string							m_guid;
	SubSurfaceComponentType				m_type;
	int									m_windowId;
	int									m_constructionId;
};


} // namespace IFCC

#endif // IFCC_SubSurfaceComponent_H
