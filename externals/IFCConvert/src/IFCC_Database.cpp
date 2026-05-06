#include "IFCC_Database.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_Instances.h"
#include "IFCC_ComponentInstance.h"
#include "IFCC_Logger.h"
#include "IFCC_Property.h"

namespace IFCC {

namespace {

// Knuth multiplicative hash → [0,1). Scatters adjacent ids across the lightness
// range so consecutive components look clearly different, not near-identical.
double idToUnit(int id) {
	uint32_t h = static_cast<uint32_t>(id) * 2654435761u;
	return (h >> 8) / double(0xFFFFFF);
}

// HSL (h in [0,360], s/l in [0,1]) → "#RRGGBB" hex string.
std::string hslToHex(double h, double s, double l) {
	double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
	double hp = h / 60.0;
	double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
	double r = 0, g = 0, b = 0;
	if      (hp < 1) { r = c; g = x; b = 0; }
	else if (hp < 2) { r = x; g = c; b = 0; }
	else if (hp < 3) { r = 0; g = c; b = x; }
	else if (hp < 4) { r = 0; g = x; b = c; }
	else if (hp < 5) { r = x; g = 0; b = c; }
	else             { r = c; g = 0; b = x; }
	double m = l - c / 2.0;
	int ri = static_cast<int>(std::round((r + m) * 255));
	int gi = static_cast<int>(std::round((g + m) * 255));
	int bi = static_cast<int>(std::round((b + m) * 255));
	char buf[10];
	std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", ri, gi, bi);
	return std::string(buf);
}

// Hue per main component type. Chosen so adjacent categories (all walls, all
// floors, all roofs) stay visually close while distinct categories stand apart.
double hueForComponent(Component::ComponentType t) {
	switch(t) {
		case Component::CT_OutsideWall:         return   0; // red
		case Component::CT_OutsideWallToGround: return  20; // red-orange
		case Component::CT_InsideWall:          return 210; // blue
		case Component::CT_FloorToCellar:       return  40; // orange
		case Component::CT_FloorToAir:          return  60; // yellow
		case Component::CT_FloorToGround:       return  30; // brown-orange
		case Component::CT_Ceiling:             return 260; // violet
		case Component::CT_SlopedRoof:          return 120; // green
		case Component::CT_FlatRoof:            return 140; // green-cyan
		case Component::CT_ColdRoof:            return 180; // cyan
		case Component::CT_WarmRoof:            return 100; // yellow-green
		case Component::CT_Miscellaneous:       return 300; // magenta
		case Component::NUM_CT:                 return 320;
	}
	return 320;
}

// Hue per subsurface type. Kept distinct from main-component hues so windows
// and doors visually pop against their parent walls.
double hueForSubSurface(SubSurfaceComponent::SubSurfaceComponentType t) {
	switch(t) {
		case SubSurfaceComponent::CT_Window:        return 220; // light blue
		case SubSurfaceComponent::CT_Door:          return 330; // magenta-pink
		case SubSurfaceComponent::CT_Miscellaneous: return 280; // purple
		case SubSurfaceComponent::NUM_CT:           return 290;
	}
	return 290;
}

std::string colorForComponent(int id, Component::ComponentType t) {
	double h = hueForComponent(t);
	double l = 0.30 + idToUnit(id) * 0.45; // [0.30, 0.75]
	return hslToHex(h, 0.65, l);
}

// Color for a Construction referenced by a given component type. Same hue as the
// component type so construction tint lines up with component tint; lightness
// scattered by the construction's own id so multiple constructions sharing a type
// still look distinct.
std::string colorForConstruction(int constructionId, Component::ComponentType t) {
	double h = hueForComponent(t);
	double l = 0.35 + idToUnit(constructionId) * 0.40;
	return hslToHex(h, 0.55, l);
}

std::string colorForSubSurfaceComponent(int id, SubSurfaceComponent::SubSurfaceComponentType t) {
	double h = hueForSubSurface(t);
	double l = 0.35 + idToUnit(id) * 0.40; // slightly tighter band for subsurfaces
	return hslToHex(h, 0.60, l);
}

} // end anonymous namespace


int Database::m_virtualConstructionId = -1;
int Database::m_missingConstructionId = -1;
int Database::m_virtualComponentId = -1;
int Database::m_missingComponentId = -1;
int Database::m_missingWindowId = -1;

Database::Database() {
	clear();
}

void Database::clear() {
	m_materials.clear();
	m_constructions.clear();
	m_windows.clear();
	m_windowGlazings.clear();
	m_components.clear();
	m_subSurfaceComponents.clear();

	Construction virtualConstuction;
	virtualConstuction.m_id = GUID_maker::instance().guid();
	virtualConstuction.m_name = "virtual";
	virtualConstuction.m_basictype = BT_Virtual;
	m_constructions[virtualConstuction.m_id] = virtualConstuction;
	m_virtualConstructionId = virtualConstuction.m_id;

	Construction missingConstuction;
	missingConstuction.m_id = GUID_maker::instance().guid();
	missingConstuction.m_name = "missing";
	missingConstuction.m_basictype = BT_Missing;
	m_constructions[missingConstuction.m_id] = missingConstuction;
	m_missingConstructionId = missingConstuction.m_id;

	Component virtualComponent;
	virtualComponent.m_id = GUID_maker::instance().guid();
	virtualComponent.m_name = "virtual";
	virtualComponent.m_basictype = BT_Virtual;
	virtualComponent.m_constructionId = m_virtualConstructionId;
	virtualComponent.m_type = Component::CT_Miscellaneous;
	m_components[virtualComponent.m_id] = virtualComponent;
	m_virtualComponentId = virtualComponent.m_id;

	Component missingComponent;
	missingComponent.m_id = GUID_maker::instance().guid();
	missingComponent.m_name = "missing";
	missingComponent.m_basictype = BT_Missing;
	missingComponent.m_constructionId = m_missingConstructionId;
	missingComponent.m_type = Component::CT_Miscellaneous;
	m_components[missingComponent.m_id] = missingComponent;
	m_missingComponentId = missingComponent.m_id;
}


TiXmlElement * Database::writeXML(TiXmlElement * parent) const {
	TiXmlElement * e = new TiXmlElement("EmbeddedDatabase");
	parent->LinkEndChild(e);

	if(!m_materials.empty()) {
		TiXmlElement * child = new TiXmlElement("Materials");
		e->LinkEndChild(child);

		for(const auto& mat : m_materials) {
			mat.second.writeXML(child);
		}
	}

	if(!m_constructions.empty()) {
		TiXmlElement * child = new TiXmlElement("Constructions");
		e->LinkEndChild(child);

		for(const auto& constr : m_constructions) {
			constr.second.writeXML(child);
		}
	}

	if(!m_windows.empty()) {
		TiXmlElement * child = new TiXmlElement("Windows");
		e->LinkEndChild(child);

		for(const auto& window : m_windows) {
			window.second.writeXML(child);
		}
	}

	if(!m_windowGlazings.empty()) {
		TiXmlElement * child = new TiXmlElement("WindowGlazingSystems");
		e->LinkEndChild(child);

		for(const auto& glazing : m_windowGlazings) {
			glazing.second.writeXML(child);
		}
	}

	if(!m_components.empty()) {
		TiXmlElement * child = new TiXmlElement("Components");
		e->LinkEndChild(child);

		for(const auto& comp : m_components) {
			comp.second.writeXML(child);
		}
	}

	if(!m_subSurfaceComponents.empty()) {
		TiXmlElement * child = new TiXmlElement("SubSurfaceComponents");
		e->LinkEndChild(child);

		for(const auto& comp : m_subSurfaceComponents) {
			comp.second.writeXML(child);
		}
	}

	return e;
}

void Database::collectData(BuildingElementsCollector& elements) {
	collectWindowsAndGlazings(elements.m_constructionElements);
	collectWindowsAndGlazings(elements.m_constructionSimilarElements);
	collectWindowsAndGlazings(elements.m_openingElements);
	collectWindowsAndGlazings(elements.m_otherElements);

	collectMaterialsAndConstructions(elements.m_constructionElements);
	collectMaterialsAndConstructions(elements.m_constructionSimilarElements);
	collectMaterialsAndConstructions(elements.m_openingElements);
	collectMaterialsAndConstructions(elements.m_otherElements);

	collectComponents(elements.m_constructionElements);
	collectComponents(elements.m_constructionSimilarElements);
	collectComponents(elements.m_openingElements);
	collectComponents(elements.m_otherElements);
}

/*! Function searches for the highest Id in the given vector.
	Type in vector must contain a variable m_id
*/
template<typename T>
int getHighestId(const std::vector<T>& vect) {
	int res = 1;
	if(vect.empty())
		return res;

	for(const auto& v : vect) {
		res = std::max<int>(res,v.m_id);
	}
	return res;
}

/*! Search for the given item in the given vector. It uses the comparison function equal() which is implemented in all child classes of VICUS::AbstractDBElement.
	\return Iterator to the found element or end() of the vector.
*/
template<typename T>
typename std::vector<T>::const_iterator findItem(const std::vector<T>& vect, const T& elem) {
	if(vect.empty())
		return vect.end();

	return std::find_if(vect.begin(), vect.end(),
							[elem](const auto& eit) {return eit.equal(&elem) == VICUS::AbstractDBElement::Equal; });
}

/*! Function for adding IFC convert database elements to the corresponding embedded database in a VICUS::Project.
	Two possible behaviours:
	- if the element is already included in the database only the id mapping map will be adopted
	- if the element is not in database it will be added
	The id of the new element will be higher than the highest existing id in the embedded database.
	It can be used for:
	- materials
	- constructions
	- windows
	- window glazings
	- subsurface components
*/
template<typename T, typename U>
void addItems(std::vector<T>& dbVect, const std::map<int,U>& sourceVect, std::map<int,int>& idMap) {
	if(sourceVect.empty())
		return;

	int maxMatId = getHighestId(dbVect);
	for(const auto& sourceIt : sourceVect) {
		T newItem = sourceIt.second.getVicusObject(idMap, maxMatId);
		auto fit = findItem(dbVect, newItem);
		if(fit == dbVect.end())
			dbVect.push_back(newItem);
		else
			idMap[sourceIt.second.id()] = fit->m_id;
	}
}

void Database::addToVicusProject(VICUS::Project* project, std::map<int,int>& idMap) const {
	addItems(project->m_embeddedDB.m_materials, m_materials, idMap);
	addItems(project->m_embeddedDB.m_constructions, m_constructions, idMap);
	addItems(project->m_embeddedDB.m_windowGlazingSystems, m_windowGlazings, idMap);
	addItems(project->m_embeddedDB.m_windows, m_windows, idMap);
	addItems(project->m_embeddedDB.m_subSurfaceComponents, m_subSurfaceComponents, idMap);
}


void Database::collectComponents(std::vector<std::shared_ptr<BuildingElement>>& elements) {
	for(auto& elem : elements) {
		BuildingElementTypes type = elem->type();
		if(elem->isSurfaceComponent()) {
			Component comp;
			if(type == BET_Wall)
				comp.m_type = Component::CT_OutsideWall;
			else if(type == BET_Roof)
				comp.m_type = Component::CT_SlopedRoof;
			else if(type == BET_Slab)
				comp.m_type = Component::CT_FloorToAir;
			else
				comp.m_type = Component::CT_Miscellaneous;
			comp.m_constructionId = elem->m_constructionId;
			comp.m_name = elem->m_name;
			comp.m_id = GUID_maker::instance().guid();
			comp.m_guid = elem->m_guid;
			comp.m_basictype = BT_Real;
			m_components[comp.m_id] = comp;
		}
		else if(elem->isSubSurfaceComponent()) {
			SubSurfaceComponent comp(GUID_maker::instance().guid(), elem->m_guid, elem->m_name);
			if(type == BET_Window) {
				comp.setWindow(elem->m_openingProperties.m_id);
			}
			else if(type == BET_Door) {
				comp.setDoor(elem->m_constructionId);
			}
			else {
				comp.setOther(elem->m_constructionId);
			}
			m_subSurfaceComponents[comp.id()] = comp;
		}
	}
}

void Database::collectMaterialsAndConstructions(std::vector<std::shared_ptr<BuildingElement>>& elements) {
	for(auto& elem : elements) {
		if(elem->m_materialLayers.empty()) {
			elem->m_constructionId = Database::m_missingConstructionId;
			continue;
		}

		Construction currentConst;
		for(size_t i=0; i<elem->m_materialLayers.size(); ++i) {
			std::string name = elem->m_materialLayers[i].second;
			auto fit = std::find_if(
						   m_materials.begin(),
						   m_materials.end(),
						   [name](const auto& mo) -> bool {return mo.second.m_name == name; });
			if(fit == m_materials.end()) {
				Material material;
				material.m_id  = GUID_maker::instance().guid();
				material.m_name = name;
				material.setPropertiesFromPropertyMap(elem->m_materialPropertyMap[i]);
				m_materials[material.m_id] = material;
				currentConst.m_layers.push_back(std::make_pair(material.m_id, elem->m_materialLayers[i].first));
			}
			else {
				currentConst.m_layers.push_back(std::make_pair(fit->first, elem->m_materialLayers[i].first));
			}
		}

		auto fit = std::find_if(
					   m_constructions.begin(),
					   m_constructions.end(),
					   [currentConst](const auto& mo) -> bool {return mo.second == currentConst; });
		if(fit == m_constructions.end()) {
			currentConst.m_id = GUID_maker::instance().guid();
			currentConst.m_name = "construction - " + std::to_string(currentConst.m_id);
			currentConst.m_basictype = BT_Real;
			m_constructions[currentConst.m_id] = currentConst;
			elem->m_constructionId = currentConst.m_id;
		}
		else {
			elem->m_constructionId = fit->first;
		}
	}
}

void Database::collectWindowsAndGlazings(std::vector<std::shared_ptr<BuildingElement>>& elements) {
	for(auto& elem : elements) {
		if(!elem->m_openingProperties.m_isWindow)
			continue;

		Window window;
		window.m_name = elem->m_name;

		WindowGlazing glazing;
		for(const std::string& str : elem->m_openingProperties.m_windowConstructionTypes) {
			glazing.m_notes += str + ",";
		}
		if(!glazing.m_notes.empty()) {
			glazing.m_notes.pop_back();
		}

		// Pull the two physical parameters that drive WindowGlazing equality.
		// U-value is already extracted onto the element in setThermalTransmittance();
		// SHGC (SolarHeatGainTransmittance) still needs to be pulled from the raw
		// IFC property map. Windows with matching U + SHGC collapse to one glazing.
		glazing.m_thermalTransmittance = elem->thermalTransmittance();
		double shgc = 0.0;
		if(getDoubleProperty(elem->propertyMap(), "Pset_DoorWindowGlazingCommon",
							 "SolarHeatGainTransmittance", shgc)) {
			glazing.m_shgc = shgc;
		}

		auto fitGl = std::find_if(
					   m_windowGlazings.begin(),
					   m_windowGlazings.end(),
					   [glazing](const auto& gl) -> bool {return gl.second == glazing; });
		if(fitGl == m_windowGlazings.end()) {
			glazing.m_id = GUID_maker::instance().guid();
			glazing.m_name = "window glazing - " + std::to_string(glazing.m_id);
			m_windowGlazings[glazing.m_id] = glazing;
			window.m_glazingSystemId = glazing.m_id;
		}
		else {
			window.m_glazingSystemId = fitGl->first;
		}
		auto fitWi = std::find_if(
					   m_windows.begin(),
					   m_windows.end(),
					   [window](const auto& wi) -> bool {return wi.second == window; });
		if(fitWi == m_windows.end()) {
			window.m_id = GUID_maker::instance().guid();
			if(window.m_name.empty())
				window.m_name = "window glazing - " + std::to_string(window.m_id);
			m_windows[window.m_id] = window;
			elem->m_openingProperties.m_id = window.m_id;
		}
		else {
			elem->m_openingProperties.m_id = fitWi->first;
		}
	}
}

void Database::unifyComponents(Instances& instances) {
	// ---- main Component ----
	std::map<int,int> componentRemap; // old id -> kept id
	std::vector<int> keptComponentIds;
	keptComponentIds.reserve(m_components.size());

	for(auto it = m_components.begin(); it != m_components.end(); ) {
		bool merged = false;
		for(int keptId : keptComponentIds) {
			const Component& kept = m_components.at(keptId);
			if(kept == it->second) {
				componentRemap[it->first] = keptId;
				it = m_components.erase(it);
				merged = true;
				break;
			}
		}
		if(!merged) {
			keptComponentIds.push_back(it->first);
			++it;
		}
	}

	// Assign colors to surviving main components.
	for(auto& kv : m_components) {
		kv.second.m_color = colorForComponent(kv.second.m_id, kv.second.m_type);
	}

	// Propagate per-type coloring to the Constructions referenced by each Component
	// so that VICUS's "Einfärbung: Konstruktionen" view picks up the same palette.
	// Multiple components may share one construction; prefer a non-Miscellaneous
	// type when picking the representative hue.
	std::map<int, Component::ComponentType> constructionRepType;
	for(const auto& kv : m_components) {
		int cid = kv.second.m_constructionId;
		if(cid < 0)
			continue;
		auto it = constructionRepType.find(cid);
		if(it == constructionRepType.end() ||
		   (it->second == Component::CT_Miscellaneous && kv.second.m_type != Component::CT_Miscellaneous))
		{
			constructionRepType[cid] = kv.second.m_type;
		}
	}
	for(auto& kv : m_constructions) {
		Component::ComponentType t = Component::CT_Miscellaneous;
		auto it = constructionRepType.find(kv.second.m_id);
		if(it != constructionRepType.end())
			t = it->second;
		kv.second.m_color = colorForConstruction(kv.second.m_id, t);
	}

	// ---- SubSurfaceComponent ----
	std::map<int,int> subRemap;
	std::vector<int> keptSubIds;
	keptSubIds.reserve(m_subSurfaceComponents.size());

	for(auto it = m_subSurfaceComponents.begin(); it != m_subSurfaceComponents.end(); ) {
		bool merged = false;
		for(int keptId : keptSubIds) {
			const SubSurfaceComponent& kept = m_subSurfaceComponents.at(keptId);
			if(kept == it->second) {
				subRemap[it->first] = keptId;
				it = m_subSurfaceComponents.erase(it);
				merged = true;
				break;
			}
		}
		if(!merged) {
			keptSubIds.push_back(it->first);
			++it;
		}
	}

	for(auto& kv : m_subSurfaceComponents) {
		kv.second.setColor(colorForSubSurfaceComponent(kv.second.id(), kv.second.typeValue()));
	}

	// Windows and WindowGlazings are already deduplicated on insertion (see
	// collectWindowsAndGlazings); here we only assign colors so VICUS's "Einfärbung"
	// views render distinct colors instead of black. Use the Window-subsurface hue
	// as the base and scatter lightness by the object's id.
	{
		const double winHue = hueForSubSurface(SubSurfaceComponent::CT_Window);
		for(auto& kv : m_windows) {
			double l = 0.35 + idToUnit(kv.second.m_id) * 0.40;
			kv.second.m_color = hslToHex(winHue, 0.60, l);
		}
		for(auto& kv : m_windowGlazings) {
			double l = 0.40 + idToUnit(kv.second.m_id) * 0.35;
			kv.second.m_color = hslToHex(winHue, 0.55, l);
		}
	}

	// Rewrite instance component-ids via the public remap API.
	instances.remapComponentIds(componentRemap, subRemap);

	Logger::instance() << "unifyComponents: components kept=" << m_components.size()
					   << " merged=" << componentRemap.size()
					   << " ; subsurface components kept=" << m_subSurfaceComponents.size()
					   << " merged=" << subRemap.size();
}


} // namespace IFCC
