#ifndef IFCC_ConstructionH
#define IFCC_ConstructionH

#include <vector>

#include <IBK_math.h>

#include <tinyxml.h>

#include <VICUS_Construction.h>

#include "IFCC_Types.h"

namespace IFCC {

/*! Class contains a construction as list of layers (material id + thickness) with additional informations.
*/
class Construction
{
public:
	/*! Standard constructor.*/
	Construction();

	int									m_id;			///< Unique id of the construction
	std::vector<std::pair<int, double>>	m_layers;		///< Vector of layers with material id and thickness in m
	std::string							m_name;			///< Name of the construction
	std::string							m_color;		///< Display color as "#RRGGBB" hex string (assigned by Database::unifyComponents)
	BasicType							m_basictype;	///< Basic type of component

	/*! Return the object ID.*/
	int id() const {
		return m_id;
	}

	/*! Write the component in vicus xml format including storeys.*/
	TiXmlElement * writeXML(TiXmlElement * parent) const;

	/*! Create a VICUS construction object and return this.
		The returned object contains all transferable data.
	*/
	VICUS::Construction getVicusObject(std::map<int,int>& idMap, int idOffset) const;

	/*! Comparison operator.*/
	friend bool operator==(const Construction& left, const Construction& right) {
		if(left.m_layers.size() != right.m_layers.size())
			return false;

		for(size_t i=0; i<left.m_layers.size(); ++i) {
			if(left.m_layers[i].first != right.m_layers[i].first)
				return false;
			if(!IBK::near_equal(left.m_layers[i].second,right.m_layers[i].second))
				return false;
		}
		return true;
	}
};


} // namespace IFCC

#endif // IFCC_CONSTRUCTION_H
