#ifndef IFCC_BuildingElementsCollectorH
#define IFCC_BuildingElementsCollectorH

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <QMutex>

namespace IFCC {

class BuildingElement;

/*! Struct collects all types of building elements.*/
struct BuildingElementsCollector {
	/*! Primary building elements which are part of the construction and can contain openings.
		This can be: Wall, Roof, Slab
	*/
	std::vector<std::shared_ptr<BuildingElement>>		m_constructionElements;

	/*! Secondary building elements which are part of the construction and can contain openings.
		This can be: Beam, Column, Covering, Footing, CurtainWall
	*/
	std::vector<std::shared_ptr<BuildingElement>>		m_constructionSimilarElements;

	/*! All building elements which can be a opening.
		This can be: Window, Door
	*/
	std::vector<std::shared_ptr<BuildingElement>>		m_openingElements;

	/*! All other building elements.*/
	std::vector<std::shared_ptr<BuildingElement>>		m_otherElements;

	/*! Building elements without surfaces.*/
	std::vector<std::shared_ptr<BuildingElement>>		m_elementsWithoutSurfaces;

	void clear() {
		m_constructionElements.clear();
		m_constructionSimilarElements.clear();
		m_openingElements.clear();
		m_otherElements.clear();
		m_elementsWithoutSurfaces.clear();
		invalidateLookupCaches();
	}

	std::vector<std::shared_ptr<BuildingElement>> allConstructionElements() const {
		std::vector<std::shared_ptr<BuildingElement>> constructionElements(m_constructionElements);
		constructionElements.insert(constructionElements.begin(), m_constructionSimilarElements.begin(),
									m_constructionSimilarElements.end());
		return constructionElements;
	}

	/*! Returns the building element with the given IFC GUID, or nullptr if not found.
		Backed by a hash-map cache (lazily built on first lookup) so calls are O(1)
		instead of the original O(N) linear scan over all 5 element vectors.
	*/
	const std::shared_ptr<BuildingElement> fromGUID(const std::string& guid) const;

	/*! Returns the building element with the given internal ID, or nullptr if not found.
		Same lazy hash-map cache as fromGUID().
	*/
	std::shared_ptr<BuildingElement> fromID(int id) const;

	/*! Drops the cached GUID/ID lookup maps. Must be called whenever the
		underlying element vectors are mutated (e.g. push_back). The clear()
		method invokes this automatically.
	*/
	void invalidateLookupCaches() const {
		QMutexLocker lock(&m_cacheMutex);
		m_byGUID.clear();
		m_byID.clear();
		m_cachesBuilt = false;
	}

private:
	/*! Builds the m_byGUID and m_byID maps from the current contents of all
		element vectors. Caller must hold m_cacheMutex. */
	void buildLookupCaches() const;

	mutable std::unordered_map<std::string, std::shared_ptr<BuildingElement>>	m_byGUID;
	mutable std::unordered_map<int, std::shared_ptr<BuildingElement>>			m_byID;
	mutable bool																m_cachesBuilt = false;
	mutable QMutex																m_cacheMutex;
};

} // namespace IFCC

#endif // IFCC_BuildingElementsCollectorH
