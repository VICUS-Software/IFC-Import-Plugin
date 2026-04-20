#include "IFCC_ProgressHandler.h"

#include <algorithm>

namespace IFCC {

ProgressHandler::ProgressHandler(std::function<void(int, QString)> callback, double rangeStart, double rangeEnd) :
	m_callback(callback),
	m_rangeStart(rangeStart),
	m_rangeEnd(rangeEnd),
	m_lastReported(-1)
{
}

void ProgressHandler::notify() {
	notify(0.0);
}

void ProgressHandler::notify(double percentage) {
	notify(percentage, nullptr);
}

void ProgressHandler::notify(double percentage, const char* text) {
	percentage = std::max(0.0, std::min(1.0, percentage));
	double globalFraction = m_rangeStart + percentage * (m_rangeEnd - m_rangeStart);
	int globalPct = static_cast<int>(globalFraction * 100.0);
	globalPct = std::max(0, std::min(100, globalPct));

	bool hasText = (text != nullptr && text[0] != '\0');
	// throttle: skip only if percentage is unchanged AND no status text is provided
	if (globalPct == m_lastReported && !hasText)
		return;

	m_lastReported = globalPct;
	QString qtext = hasText ? QString::fromUtf8(text) : QString();
	if (m_callback)
		m_callback(globalPct, qtext);
}

void ProgressHandler::setRange(double rangeStart, double rangeEnd) {
	m_rangeStart = rangeStart;
	m_rangeEnd = rangeEnd;
	m_lastReported = -1;
}

} // namespace IFCC
