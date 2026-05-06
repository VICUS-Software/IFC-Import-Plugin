#ifndef IFCC_InstancesH
#define IFCC_InstancesH

#include <tinyxml.h>

#include "IFCC_BuildingElement.h"
#include "IFCC_ComponentInstance.h"
#include "IFCC_BuildingElementsCollector.h"
#include "IFCC_ConvertOptions.h"

namespace VICUS {
	class Project;
}

namespace IFCC {

class Site;
class Database;

/*! Class contains and collects component instances.*/
class Instances
{
public:
	/*! Standard constructor.*/
	Instances();

	/*! Clear all the content.*/
	void clear();

	/*! Write the instances in vicus xml format.*/
	TiXmlElement * writeXML(TiXmlElement * parent) const;

	/*! Collect component and subsurface component instances from building elements and other databases.
		\param elements Collection of all building element vectors
		\param database Collection of all databases
		\param site Building site which contains all geometric data (including space boundaries)
		\param errors Global convert error list
	*/
	void collectComponentInstances(BuildingElementsCollector& elements, Database& database,
								  const Site& site, std::vector<ConvertError>& errors, const ConvertOptions& convertOptions);

	std::vector<int> checkForWrongSurfaceIds(const Site& site);

	/*! Add component instances and sub-surface component instances to the given VICUS project.
		\param project VICUS project to populate
		\param database Database for looking up component → construction mapping
		\param idMap Map of IFCC IDs to VICUS IDs
	*/
	void addToVicusProject(VICUS::Project* project, const Database& database, const std::map<int,int>& idMap) const;

	/*! Rewrite componentId on every stored instance per the given old→new remap
		tables. Used by Database::unifyComponents after duplicate components have
		been merged so instances continue to reference a valid component id.
	*/
	void remapComponentIds(const std::map<int,int>& mainRemap, const std::map<int,int>& subRemap);

private:

	/*! Collect component and subsurface component instances from building elements and other databases.
		\param elements Building element vector
		\param database Collection of all databases
		\param site Building site which contains all geometric data (including space boundaries)
		\param errors Global convert error list
	*/
	void collectNormalComponentInstances(BuildingElementsCollector& elements, Database& database,
								  const Site& site, std::vector<ConvertError>& errors, const ConvertOptions& convertOptions);


	/*! Collect subsurface component instances from opening elements and component database.
		\param elements Building element vector
		\param database Collection of all databases
		\param site Building site which contains all geometric data (including space boundaries)
		\param errors Global convert error list
	*/
	void collectSubSurfaceComponentInstances(BuildingElementsCollector& elements, Database& database,
											 const Site& site, std::vector<ConvertError>& errors, const ConvertOptions& convertOptions);
	std::map<int, ComponentInstance>		m_componentInstances;			///< Map of id and component instance
	std::map<int, ComponentInstance>		m_subSurfaceComponentInstances;	///< map of id and subsurface component instance
};

} // namespace IFCC

#endif // IFCC_InstancesH
