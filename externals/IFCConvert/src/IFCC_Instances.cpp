#include "IFCC_Instances.h"

#include <set>

#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include <VICUS_Project.h>

#include "IFCC_Site.h"
#include "IFCC_Database.h"
#include "IFCC_Logger.h"

namespace IFCC {

Instances::Instances()
{
}

void Instances::clear() {
	m_componentInstances.clear();
	m_subSurfaceComponentInstances.clear();
}


TiXmlElement * Instances::writeXML(TiXmlElement * parent) const {
	if(!m_componentInstances.empty()) {
		TiXmlElement * child = new TiXmlElement("ComponentInstances");
		parent->LinkEndChild(child);

		for(const auto& comInst : m_componentInstances) {
//			if(comInst.second.m_sideASurfaceId > 0)
				comInst.second.writeXML(child);
		}
	}

	if(!m_subSurfaceComponentInstances.empty()) {
		TiXmlElement * child = new TiXmlElement("SubSurfaceComponentInstances");
		parent->LinkEndChild(child);

		for(const auto& comInst : m_subSurfaceComponentInstances) {
//			if(comInst.second.m_sideASurfaceId > 0)
				comInst.second.writeXML(child);
		}
	}

	return parent;
}

void Instances::collectComponentInstances(BuildingElementsCollector& elements, Database& database, const Site& site,
										 std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	collectNormalComponentInstances(elements, database, site, errors, convertOptions);
	collectSubSurfaceComponentInstances(elements, database, site, errors, convertOptions);
}

void Instances::collectNormalComponentInstances(BuildingElementsCollector& elements, Database& database,
										 const Site& site, std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	std::vector<std::shared_ptr<BuildingElement>> constructionElements = elements.allConstructionElements();
	for(const auto& building : site.m_buildings) {
		for(const auto& storey : building->storeys()) {
			for(const auto& space : storey->spaces()) {
				for(const auto& sb : space->spaceBoundaries()) {
					// first loop is only for normal component instances
					if(!sb->isConstructionElement())
						continue;

					// it makes no sense to add a space boundary with a non valid surface
					if(!sb->surface().check(convertOptions.m_polygonEps))
						continue;

					// don't go further if the space boundary is already assigned
					if(sb->m_componentInstanceId > -1)
						continue;

					if(sb->isMissing()) {
						ComponentInstance ci(GUID_maker::instance().guid(), Database::m_missingComponentId, sb->surface().id());
						if(sb->surface().id() >= 0) {
							m_componentInstances[ci.id()] = ci;
							sb->m_componentInstanceId = ci.id();
						}
						else {
							errors.push_back(ConvertError{OT_Instance, sb->m_id, "Creating component instance - space boundary with non valid surface Id found"});
						}
					}
					else if(sb->isVirtual()) {
						ComponentInstance ci(GUID_maker::instance().guid(), Database::m_virtualComponentId, sb->surface().id());
						if(sb->surface().id() >= 0) {
							m_componentInstances[ci.id()] = ci;
							sb->m_componentInstanceId = ci.id();
						}
						else {
							errors.push_back(ConvertError{OT_Instance, sb->m_id, "Creating component instance - space boundary with non valid surface Id found"});
						}
					}
					else {
						auto fitElem = std::find_if(
										   constructionElements.begin(),
										   constructionElements.end(),
										   [sb](const auto& elem) -> bool {return elem->m_id == sb->m_elementEntityId; });
						if(fitElem != constructionElements.end()) {
							const std::shared_ptr<BuildingElement>& elem = *fitElem;
							auto fitComp = std::find_if(
											   database.m_components.begin(),
											   database.m_components.end(),
											   [elem](const auto& comp) -> bool {return comp.second.m_guid == elem->m_guid; });
							if(fitComp != database.m_components.end()) {
								ComponentInstance ci(GUID_maker::instance().guid(), fitComp->first, sb->surface().id());
								fitComp->second.updateComponentType(*sb);
								if(sb->surface().id() >= 0 ) {
									m_componentInstances[ci.id()] = ci;
									sb->m_componentInstanceId = ci.id();
								}
								else {
									errors.push_back(ConvertError{OT_Instance, sb->m_id, "Creating component instance - space boundary with non valid surface Id found"});
								}
							}
							else {
								ConvertError err;
								err.m_objectType = OT_Component;
								err.m_objectID = sb->m_id;
								err.m_errorText = "Component of element for space boundary not found in components list";
								errors.push_back(err);
							}
						}
						else {
							ConvertError err;
							err.m_objectType = OT_Instance;
							err.m_objectID = sb->m_id;
							err.m_errorText = "Element ID in space boundary not found in element list";
							errors.push_back(err);
						}
					}
				}
			}
		}
	}
}

void Instances::collectSubSurfaceComponentInstances(BuildingElementsCollector& elements, Database& database,
										 const Site& site, std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	for(const auto& building : site.m_buildings) {
		for(const auto& storey : building->storeys()) {
			for(const auto& space : storey->spaces()) {
				for(const auto& subsb : space->spaceBoundaries()) {
					if(!subsb->isConstructionElement())
						continue;

					if(!subsb->surface().check(convertOptions.m_polygonEps))
						continue;

					// go through all subsurfaces of the current space boundary
					for(const auto& subsb : subsb->containedOpeningSpaceBoundaries()) {

						// don't go further if the space boundary is already assigned
						if(subsb->m_componentInstanceId > -1)
							continue;

						if(subsb->isMissing()) {
							ComponentInstance ci(GUID_maker::instance().guid(), Database::m_missingComponentId, subsb->surface().id());
							ci.setSubSurface(true);
							if(subsb->surface().id() >= 0) {
								m_subSurfaceComponentInstances[ci.id()] = ci;
								subsb->m_componentInstanceId = ci.id();
							}
							else {
								errors.push_back(ConvertError{OT_Instance, subsb->m_id, "Creating subsurface component instance - space boundary with non valid surface Id found"});
							}
						}
						else if(subsb->isVirtual()) {
							// ComponentInstance ci(GUID_maker::instance().guid(), Database::m_virtualComponentId, subsb->surface().id());
							// ci.setSubSurface(true);
							// if(subsb->surface().id() >= 0) {
							// 	m_subSurfaceComponentInstances[ci.id()] = ci;
							// 	subsb->m_componentInstanceId = ci.id();
							// }
							// else {
							// 	errors.push_back(ConvertError{OT_Instance, subsb->m_id, "Creating subsurface component instance - space boundary with non valid surface Id found"});
							// }
						}
						else {
							auto fitElem = std::find_if(
										elements.m_openingElements.begin(),
										elements.m_openingElements.end(),
										[subsb](const auto& elem) -> bool {return elem->m_id == subsb->m_elementEntityId; });
							if(fitElem != elements.m_openingElements.end()) {
								const std::shared_ptr<BuildingElement>& elem = *fitElem;
								auto fitComp = std::find_if(
											database.m_subSurfaceComponents.begin(),
											database.m_subSurfaceComponents.end(),
											[elem](const auto& comp) -> bool {return comp.second.guid() == elem->m_guid; });
								if(fitComp != database.m_subSurfaceComponents.end()) {
									ComponentInstance ci(GUID_maker::instance().guid(), fitComp->first, subsb->surface().id());
									ci.setSubSurface(true);
									if(subsb->surface().id() >= 0) {
										m_subSurfaceComponentInstances[ci.id()] = ci;
										subsb->m_componentInstanceId = ci.id();
									}
									else {
										errors.push_back(ConvertError{OT_Instance, subsb->m_id, "Creating subsurface component instance - space boundary with non valid surface Id found"});
									}
								}
								else {
									ConvertError err;
									err.m_objectType = OT_Instance;
									err.m_objectID = subsb->m_id;
									err.m_errorText = "Element for opening space boundary not found in components list";
									errors.push_back(err);
								}
							}
							else {
								ConvertError err;
								err.m_objectType = OT_Instance;
								err.m_objectID = subsb->m_id;
								err.m_errorText = "Element ID in opening space boundary not found in element list";
								errors.push_back(err);
							}
						}
					}
				}
			}
		}
	}
}

static bool hasSurfaceId(SpaceBoundary* sb, int id) {
	if(sb == nullptr)
		return false;
	if(sb->surface().id() == id)
		return true;
	for(const auto& ss : sb->surface().subSurfaces()) {
		if(ss.id() == id)
			return true;
	}
	return false;
}

static bool hasSubSurfaceId(SpaceBoundary* sb, int id) {
	if(sb == nullptr)
		return false;
	for(const auto& ss : sb->surface().subSurfaces()) {
		if(ss.id() == id)
			return true;
	}
	return false;
}

static bool hasSurfaceId(const std::vector<std::shared_ptr<SpaceBoundary>>& sbs, int id) {
	for(const auto& sb : sbs) {
		if(hasSurfaceId(sb.get(), id))
			return true;
	}
	return false;
}

static bool hasSubSurfaceId(const std::vector<std::shared_ptr<SpaceBoundary>>& sbs, int id) {
	for(const auto& sb : sbs) {
		if(hasSubSurfaceId(sb.get(), id))
			return true;
	}
	return false;
}

std::vector<int> Instances::checkForWrongSurfaceIds(const Site& site) {
	std::vector<int> res;
	std::vector<std::shared_ptr<SpaceBoundary>> allSBs = site.allSpaceBoundaries();
	for(const auto& ci : m_componentInstances) {
		if(!hasSurfaceId(allSBs, ci.second.sideASurfaceId()))
			res.push_back(ci.second.id());
	}
	for(const auto& ci : m_subSurfaceComponentInstances) {
		if(!hasSubSurfaceId(allSBs, ci.second.sideASurfaceId()))
			res.push_back(ci.second.id());
	}
	return res;
}


void Instances::addToVicusProject(VICUS::Project* project, const Database& database, const std::map<int,int>& idMap) const {
	// Collect all valid surface and subsurface IDs from the VICUS project.
	// Surfaces may have been skipped during validation in getVicusSurface(),
	// so component instances referencing those surfaces must also be skipped.
	std::set<unsigned int> validSurfaceIds;
	std::set<unsigned int> validSubSurfaceIds;
	for(const auto& building : project->m_buildings) {
		for(const auto& level : building.m_buildingLevels) {
			for(const auto& room : level.m_rooms) {
				for(const auto& surf : room.surfaces()) {
					validSurfaceIds.insert(surf.m_id);
					for(const auto& sub : surf.subSurfaces())
						validSubSurfaceIds.insert(sub.m_id);
				}
			}
		}
	}

	// For normal component instances: map IFCC component → IFCC construction → VICUS construction
	std::map<int,int> componentToConstructionMap;
	for(const auto& comp : database.m_components) {
		auto fit = idMap.find(comp.second.m_constructionId);
		if(fit != idMap.end())
			componentToConstructionMap[comp.first] = fit->second;
	}

	int skippedNormal = 0;
	for(const auto& ci : m_componentInstances) {
		int sideA = ci.second.sideASurfaceId();
		int sideB = ci.second.sideBSurfaceId();
		// Skip instances with two invalid surface references.
		if(sideA < 0 && sideB < 0) {
			++skippedNormal;
			continue;
		}
		if(sideA >= 0 && validSurfaceIds.find((unsigned int)sideA) == validSurfaceIds.end()) {
			++skippedNormal;
			continue;
		}
		if(sideB >= 0 && validSurfaceIds.find((unsigned int)sideB) == validSurfaceIds.end()) {
			++skippedNormal;
			continue;
		}
		VICUS::ComponentInstance vci = ci.second.getVicusComponentInstance(componentToConstructionMap);
		project->m_componentInstances.push_back(vci);
	}

	// For sub-surface component instances: map IFCC subsurface component → VICUS subsurface component
	int skippedSub = 0;
	for(const auto& sci : m_subSurfaceComponentInstances) {
		int sideA = sci.second.sideASurfaceId();
		int sideB = sci.second.sideBSurfaceId();
		// Skip instances with two invalid surface references.
		if(sideA < 0 && sideB < 0) {
			++skippedSub;
			continue;
		}
		if(sideA >= 0 && validSubSurfaceIds.find((unsigned int)sideA) == validSubSurfaceIds.end()) {
			++skippedSub;
			continue;
		}
		if(sideB >= 0 && validSubSurfaceIds.find((unsigned int)sideB) == validSubSurfaceIds.end()) {
			++skippedSub;
			continue;
		}
		VICUS::SubSurfaceComponentInstance vsci = sci.second.getVicusSubSurfaceComponentInstance(idMap);
		project->m_subSurfaceComponentInstances.push_back(vsci);
	}

	if(skippedNormal > 0 || skippedSub > 0) {
		Logger::instance() << "Warning: Skipped " << skippedNormal << " component instances and "
			<< skippedSub << " sub-surface component instances referencing non-existing surfaces";
	}
}


} // namespace IFCC
