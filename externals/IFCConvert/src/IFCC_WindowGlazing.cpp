#include "IFCC_WindowGlazing.h"

#include <QColor>
#include <QString>

namespace IFCC {

WindowGlazing::WindowGlazing() :
	m_id(-1),
	m_modelType(0),
	m_thermalTransmittance(0),
	m_shgc(0)
{

}

TiXmlElement * WindowGlazing::writeXML(TiXmlElement * parent) const {
	TiXmlElement * e = new TiXmlElement("WindowGlazingSystem");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_id));
	e->SetAttribute("modelType", "Simple");
	if (!m_notes.empty())
		TiXmlElement::appendSingleAttributeElement(e, "Notes", nullptr, std::string(), m_notes);
	if (!m_dataSource.empty())
		TiXmlElement::appendSingleAttributeElement(e, "DataSource", nullptr, std::string(), m_dataSource);
	if (!m_manufacturer.empty())
		TiXmlElement::appendSingleAttributeElement(e, "Manufacturer", nullptr, std::string(), m_manufacturer);


	return e;
}

VICUS::WindowGlazingSystem WindowGlazing::getVicusObject(std::map<int,int>& idMap, int idOffset) const {
	VICUS::WindowGlazingSystem vwg;
	vwg.m_id = m_id + idOffset;
	idMap[m_id] = vwg.m_id;
	vwg.m_displayName.setString(m_name,"de");
	vwg.m_dataSource.setString(m_dataSource,"de");
	vwg.m_manufacturer.setString(m_manufacturer,"de");
	vwg.m_notes.setString(m_notes,"de");
	vwg.m_modelType = static_cast<VICUS::WindowGlazingSystem::modelType_t>(m_modelType);
	if(m_thermalTransmittance > 0)
		vwg.m_para[VICUS::WindowGlazingSystem::P_ThermalTransmittance].set("ThermalTransmittance", m_thermalTransmittance, "W/m2K");
	if(m_shgc > 0)
		vwg.m_para[VICUS::WindowGlazingSystem::P_SHGCHemis].set("SHGCHemis", m_shgc, "---");

	if(!m_color.empty()) {
		QColor c(QString::fromStdString(m_color));
		if(c.isValid())
			vwg.m_color = c;
	}

	return vwg;
}

} // namespace IFCC
