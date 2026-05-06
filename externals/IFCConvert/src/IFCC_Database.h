#ifndef IFCC_DatabaseH
#define IFCC_DatabaseH

#include <tinyxml.h>

#include <VICUS_Project.h>

#include "IFCC_Material.h"
#include "IFCC_Construction.h"
#include "IFCC_Window.h"
#include "IFCC_WindowGlazing.h"
#include "IFCC_BuildingElement.h"
#include "IFCC_Component.h"
#include "IFCC_SubSurfaceComponent.h"
#include "IFCC_BuildingElementsCollector.h"


namespace IFCC {

class Instances;

/*! Class contains all objects for databases and functions for handling.*/
class Database
{
public:
	/*! Standard constructor.*/
	Database();

	/*! Clear the complete database.*/
	void clear();

	/*! Write all databases in vicus xml format.*/
	TiXmlElement * writeXML(TiXmlElement * parent) const;

	/*! Create all databases from given building element vector.
		Calls collectMaterialsAndConstructions, collectWindowsAndGlazings and collectComponents.
	*/
	void collectData(BuildingElementsCollector& elements);

	/*! Deduplicate m_components and m_subSurfaceComponents by their equality operator
		(same construction + type, resp. same type + window + construction), rewrite
		every ComponentInstance / SubSurfaceComponentInstance in `instances` to point
		at the surviving component, and assign a deterministic per-type display color
		to each survivor. Hue is chosen from the component type; lightness is scattered
		from the component id via a multiplicative hash so adjacent ids look clearly
		different, not clustered.
	*/
	void unifyComponents(Instances& instances);

	/*! Add all database items to the embedded database of the given vicus project.
		\param project Vicus project
		\param idMap Map for mapping current ids to new VICUS ids.
		It adds the elements only if they are not already included. Otherwise only the id mapping will be added.
	*/
	void addToVicusProject(VICUS::Project* project, std::map<int,int>& idMap) const;

	std::map<int, Material>				m_materials;			///< Material database as map of id and material object
	std::map<int, Construction>			m_constructions;		///< Construction database as map of id and construction object
	std::map<int, Window>				m_windows;				///< Window database as map of id and window object
	std::map<int, WindowGlazing>		m_windowGlazings;		///< Glazing system database as map of id and glazing object
	std::map<int, Component>			m_components;			///< Component database as map of id and component object
	std::map<int, SubSurfaceComponent>	m_subSurfaceComponents;	///< Subsurface component database as map of id and subsurface component object

	static int m_virtualConstructionId;
	static int m_missingConstructionId;
	static int m_virtualComponentId;
	static int m_missingComponentId;
	static int m_missingWindowId;

private:
	/*! Create material and construction database from given building element vector.*/
	void collectMaterialsAndConstructions(std::vector<std::shared_ptr<BuildingElement>>& elements);

	/*! Create window and glazing database from given building element vector.*/
	void collectWindowsAndGlazings(std::vector<std::shared_ptr<BuildingElement>>& elements);

	/*! Create components and subsurface components database from given building element vector.*/
	void collectComponents(std::vector<std::shared_ptr<BuildingElement>>& elements);

};

} // namespace IFCC

#endif // IFCC_DatabaseH
