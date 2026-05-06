#ifndef IFCC_WindowGlazingH
#define IFCC_WindowGlazingH

#include <vector>

#include <IBK_math.h>

#include <tinyxml.h>

#include <VICUS_WindowGlazingSystem.h>

namespace IFCC {

/*! Class represents a glazing system (mainly for windows).*/
class WindowGlazing
{
public:
	/*! Standard constructor.
		The default model type is 'Simple' (0).
	*/
	WindowGlazing();

	/*! Return the object ID.*/
	int id() const {
		return m_id;
	}

	/*! Write the component in vicus xml format.*/
	TiXmlElement * writeXML(TiXmlElement * parent) const;

	/*! Comparison operator.
		Equality is defined by the physical glazing parameters only — thermal
		transmittance (U-value) and solar-heat-gain coefficient (SHGC). Two
		glazings that match on those two values are merged into one entry by
		Database::collectWindowsAndGlazings, regardless of name or notes.
	*/
	friend bool operator==(const WindowGlazing& left, const WindowGlazing& right) {
		if(!IBK::near_equal(left.m_thermalTransmittance, right.m_thermalTransmittance))
			return false;
		if(!IBK::near_equal(left.m_shgc, right.m_shgc))
			return false;
		return true;
	}

	/*! Create a VICUS window glazing object and return this.
		The returned object contains all transferable data.
	*/
	VICUS::WindowGlazingSystem getVicusObject(std::map<int,int>& idMap, int idOffset) const;

	int			m_id;					///< Window object id
	std::string	m_color;				///< Color for using in tables and views
	std::string	m_name;					///< Window name
	int			m_modelType;			///< Model type
	std::string	m_notes;				///< Some remarks
	std::string	m_manufacturer;			///< Producer of glazing system
	std::string	m_dataSource;			///< Source of window data
	double		m_thermalTransmittance;	///< Thermal transmittance U-value in [W/m2K]
	double		m_shgc;					///< Solar heat gain coefficient (perpendicular / hemispherical) in [---]
};


} // namespace IFCC

#endif // IFCC_Window_H
