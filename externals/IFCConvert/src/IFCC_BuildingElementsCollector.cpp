#include "IFCC_BuildingElementsCollector.h"

#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_BuildingElement.h"

namespace IFCC {

void BuildingElementsCollector::buildLookupCaches() const {
	// Caller must hold m_cacheMutex.
	const std::vector<const std::vector<std::shared_ptr<BuildingElement>>*> all = {
		&m_constructionElements,
		&m_constructionSimilarElements,
		&m_openingElements,
		&m_otherElements,
		&m_elementsWithoutSurfaces
	};
	size_t total = 0;
	for(const std::vector<std::shared_ptr<BuildingElement>>* v : all)
		total += v->size();
	m_byGUID.reserve(total);
	m_byID.reserve(total);
	for(const std::vector<std::shared_ptr<BuildingElement>>* v : all) {
		for(const std::shared_ptr<BuildingElement>& elem : *v) {
			if(!elem)
				continue;
			// First entry wins on duplicate GUIDs, mirroring the original
			// vector-scan order (m_constructionElements first).
			m_byGUID.emplace(elem->m_guid, elem);
			m_byID.emplace(elem->m_id, elem);
		}
	}
	m_cachesBuilt = true;
}


const std::shared_ptr<BuildingElement> BuildingElementsCollector::fromGUID(const std::string& guid) const {
	QMutexLocker lock(&m_cacheMutex);
	if(!m_cachesBuilt)
		buildLookupCaches();
	auto it = m_byGUID.find(guid);
	if(it == m_byGUID.end())
		return {};
	return it->second;
}


std::shared_ptr<BuildingElement> BuildingElementsCollector::fromID(int id) const {
	QMutexLocker lock(&m_cacheMutex);
	if(!m_cachesBuilt)
		buildLookupCaches();
	auto it = m_byID.find(id);
	if(it == m_byID.end())
		return {};
	return it->second;
}

} // namespace IFCC
