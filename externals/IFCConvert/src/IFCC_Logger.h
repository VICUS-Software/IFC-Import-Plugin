#ifndef LoggerH
#define LoggerH

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <QMutex>

namespace IFCC {

class Logger;

/*! Serializes concurrent log-line flushes and counter updates. Logger is used from
	inside OMP parallel regions (per-space matching, shell anchoring) — unsynchronized
	writes to the shared ofstreams are undefined behaviour and caused sporadic
	crashes that vanished under gdb. */
inline QMutex& logFlushMutex() {
	static QMutex m;
	return m;
}

/*! Accumulates a single log line across chained operator<< calls.
	The line is flushed (with timestamp) on destruction — typically at the
	end of the full-expression containing `Logger::instance() << ...`.
	An inactive line (level filtered out) ignores all input.
*/
class LogLine {
public:
	LogLine(std::ofstream* main, std::ofstream* step, const std::string& prefix, bool active, bool flush)
		: m_main(main), m_step(step), m_active(active), m_flush(flush)
	{
		if(m_active)
			m_buf << prefix;
	}

	LogLine(const LogLine&) = delete;
	LogLine& operator=(const LogLine&) = delete;

	LogLine(LogLine&& o) noexcept
		: m_main(o.m_main), m_step(o.m_step), m_buf(std::move(o.m_buf)), m_active(o.m_active), m_flush(o.m_flush)
	{
		o.m_active = false;
	}

	~LogLine() {
		if(!m_active)
			return;
		const std::string line = m_buf.str();
		QMutexLocker lock(&logFlushMutex());
		if(m_main) {
			*m_main << line << '\n';
			if(m_flush)
				m_main->flush();
		}
		if(m_step) {
			*m_step << line << '\n';
			if(m_flush)
				m_step->flush();
		}
	}

	template<typename T>
	LogLine& operator<<(const T& v) {
		if(m_active)
			m_buf << v;
		return *this;
	}

private:
	std::ofstream*		m_main;
	std::ofstream*		m_step;
	std::ostringstream	m_buf;
	bool				m_active = true;
	/*! Flush after writing (all but debug lines) — keeps the log complete on hangs/crashes. */
	bool				m_flush = true;
};


/*! Import logger for developer diagnosis.

	Output:
	- /tmp/ifc-import-main.log: whole import. After finish() it starts with a summary
	  (room status, unmatched openings, aggregated counters, step timing).
	- /tmp/ifc-import-NN-<step>.log: one file per pipeline step.

	Levels: error, warning, info (default), debug. Set via environment variable
	IFCC_LOG_LEVEL=error|warning|info|debug. Mass per-item diagnostics (mesh faces,
	per-candidate matching decisions, shading export skips) are debug-only; at info level
	they are aggregated via count() and reported per step and in the summary.

	Usage:
	\code
	Logger::instance() << "info line";
	Logger::instance().warning() << "something suspicious";
	Logger::instance().debug() << "per-item detail";
	Logger::instance().count("shading: ring below minimum area");
	Logger::instance().summary("Rooms", "Büro_123: warning, open edges 3.4 m");
	\endcode
*/
class Logger {
public:
	enum Level {
		L_Error,
		L_Warning,
		L_Info,
		L_Debug
	};

	static Logger& instance() {
		static Logger log;
		return log;
	}

	inline void set(const std::string& filename) {
		if(m_out.is_open())
			m_out.close();
		m_filename = filename;
		m_out.open(filename);
		m_opened = m_out.is_open();
	}

	/*! True if lines of the given level are written. Use it to skip expensive formatting. */
	inline bool enabled(Level level) const { return level <= m_level; }

	/*! Open a fresh per-step log file under /tmp for the given step name.
		Closes the previous step (duration and its counters are written).
		Filename is /tmp/ifc-import-NN-<name>.log.
	*/
	inline void beginStep(const std::string& name) {
		endStep();
		++m_stepCounter;
		std::ostringstream path;
		path << "/tmp/ifc-import-"
			 << std::setw(2) << std::setfill('0') << m_stepCounter
			 << "-" << name << ".log";
		m_stepOut.open(path.str());
		m_stepOpened = m_stepOut.is_open();
		// Ensure main log is available even if set() was never called.
		if(!m_opened)
			set("/tmp/ifc-import-main.log");
		m_stepName = name;
		m_stepStart = std::chrono::steady_clock::now();
		(*this) << "=== BEGIN STEP " << m_stepCounter << " " << name << " ===";
	}

	/*! Start of a new pipeline run: resets steps, counters and summary. */
	inline void resetSteps() {
		if(m_stepOut.is_open())
			m_stepOut.close();
		m_stepOpened = false;
		m_stepCounter = 0;
		m_stepName.clear();
		m_stepTimes.clear();
		m_stepCounters.clear();
		m_counters.clear();
		m_summary.clear();
		m_summarySections.clear();
		m_nWarnings = 0;
		m_nErrors = 0;
		m_runStart = std::chrono::steady_clock::now();
		const char* lvl = std::getenv("IFCC_LOG_LEVEL");
		m_level = L_Info;
		if(lvl != nullptr) {
			const std::string s(lvl);
			if(s == "error")		m_level = L_Error;
			else if(s == "warning")	m_level = L_Warning;
			else if(s == "debug")	m_level = L_Debug;
		}
	}

	/*! Info line (default level). */
	template<typename T>
	LogLine operator<<(const T& msg) {
		LogLine ll = line(L_Info);
		ll << msg;
		return ll;
	}

	/*! Line with explicit level. Warnings/errors are prefixed and counted for the summary. */
	inline LogLine line(Level level) {
		const bool active = enabled(level);
		if(level == L_Warning || level == L_Error) {
			QMutexLocker lock(&logFlushMutex());
			if(level == L_Warning)
				++m_nWarnings;
			else
				++m_nErrors;
		}
		std::string prefix;
		if(active) {
			prefix = "[" + timestamp() + "] ";
			if(level == L_Error)
				prefix += "ERROR ";
			else if(level == L_Warning)
				prefix += "WARNING ";
			else if(level == L_Debug)
				prefix += "debug ";
		}
		return LogLine(active && m_opened ? &m_out : nullptr,
					   active && m_stepOpened ? &m_stepOut : nullptr, prefix, active && (m_opened || m_stepOpened),
					   level != L_Debug);
	}

	inline LogLine error()		{ return line(L_Error); }
	inline LogLine warning()	{ return line(L_Warning); }
	inline LogLine info()		{ return line(L_Info); }
	inline LogLine debug()		{ return line(L_Debug); }

	/*! Aggregates a mass event (e.g. "shading: ring below minimum area"). Counters are
		written at the end of each step and in the summary. Thread safe. */
	inline void count(const std::string& key, long n = 1) {
		QMutexLocker lock(&logFlushMutex());
		m_stepCounters[key] += n;
		m_counters[key] += n;
	}

	/*! Adds a line to a named section of the summary at the top of the main log.
		Sections appear in the order of their first use. Thread safe. */
	inline void summary(const std::string& section, const std::string& text) {
		QMutexLocker lock(&logFlushMutex());
		if(m_summary.find(section) == m_summary.end())
			m_summarySections.push_back(section);
		m_summary[section].push_back(text);
	}

	/*! Ends the run: closes the last step and rewrites the main log so that it starts
		with the summary (the chronological log follows below). */
	inline void finish() {
		endStep();
		if(!m_opened)
			return;
		m_out.close();
		m_opened = false;

		std::ostringstream head;
		const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_runStart).count();
		head << "############################## IMPORT SUMMARY ##############################\n";
		head << "total time " << std::fixed << std::setprecision(1) << total << " s"
			 << ", peak memory " << peakMemoryMB() << " MB"
			 << ", warnings " << m_nWarnings << ", errors " << m_nErrors
			 << ", log level " << levelName(m_level) << " (IFCC_LOG_LEVEL=error|warning|info|debug)\n";
		for(const std::string& section : m_summarySections) {
			head << "\n--- " << section << " ---\n";
			for(const std::string& l : m_summary[section])
				head << l << "\n";
		}
		if(!m_counters.empty()) {
			head << "\n--- Counters (details with IFCC_LOG_LEVEL=debug) ---\n";
			writeCounters(head, m_counters);
		}
		head << "\n--- Steps ---\n";
		for(const auto& st : m_stepTimes)
			head << std::setw(8) << std::fixed << std::setprecision(1) << st.second << " s  " << st.first << "\n";
		head << "############################################################################\n\n";

		// prepend summary: write new file, append old body, replace
		const std::string tmpName = m_filename + ".tmp";
		{
			std::ofstream out(tmpName, std::ios::binary);
			std::ifstream in(m_filename, std::ios::binary);
			out << head.str();
			if(in)
				out << in.rdbuf();
		}
		std::remove(m_filename.c_str());
		std::rename(tmpName.c_str(), m_filename.c_str());
		// further lines (e.g. a second buildVicusProject call) are appended
		m_out.open(m_filename, std::ios::app);
		m_opened = m_out.is_open();
	}

private:
	Logger() {
		resetSteps();
	}

	inline void endStep() {
		if(m_stepName.empty())
			return;
		const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_stepStart).count();
		std::ostringstream os;
		if(!m_stepCounters.empty()) {
			os << "counters of step " << m_stepName << ":\n";
			writeCounters(os, m_stepCounters);
		}
		os << "=== END STEP " << m_stepCounter << " " << m_stepName << " ("
		   << std::fixed << std::setprecision(1) << dt << " s) ===";
		{
			std::string text = os.str();
			QMutexLocker lock(&logFlushMutex());
			if(m_opened)
				m_out << text << '\n';
			if(m_stepOpened)
				m_stepOut << text << '\n';
		}
		m_stepTimes.push_back({m_stepName, dt});
		m_stepCounters.clear();
		m_stepName.clear();
		if(m_stepOut.is_open())
			m_stepOut.close();
		m_stepOpened = false;
		if(m_opened)
			m_out.flush();
	}

	static void writeCounters(std::ostringstream& os, const std::map<std::string, long>& counters) {
		std::vector<std::pair<std::string, long>> sorted(counters.begin(), counters.end());
		std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
			return a.second > b.second || (a.second == b.second && a.first < b.first);
		});
		for(const auto& c : sorted)
			os << std::setw(9) << c.second << "  " << c.first << "\n";
	}

	static const char* levelName(Level l) {
		switch(l) {
			case L_Error:	return "error";
			case L_Warning:	return "warning";
			case L_Info:	return "info";
			case L_Debug:	return "debug";
		}
		return "info";
	}

	/*! Peak resident memory in MB (Linux: VmHWM), -1 if unknown. */
	static long peakMemoryMB() {
		std::ifstream status("/proc/self/status");
		std::string key;
		while(status >> key) {
			if(key == "VmHWM:") {
				long kb = 0;
				status >> kb;
				return kb / 1024;
			}
			std::string rest;
			std::getline(status, rest);
		}
		return -1;
	}

	inline std::string timestamp() const {
		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
		std::tm tm_local{};
#if defined(_WIN32)
		localtime_s(&tm_local, &t);
#else
		localtime_r(&t, &tm_local);
#endif
		std::ostringstream os;
		os << std::put_time(&tm_local, "%H:%M:%S") << '.'
		   << std::setw(3) << std::setfill('0') << ms.count();
		return os.str();
	}

	bool			m_opened = false;
	bool			m_stepOpened = false;
	int				m_stepCounter = 0;
	std::string		m_filename = "/tmp/ifc-import-main.log";
	std::ofstream	m_out;
	std::ofstream	m_stepOut;
	Level			m_level = L_Info;

	std::string										m_stepName;
	std::chrono::steady_clock::time_point			m_stepStart;
	std::chrono::steady_clock::time_point			m_runStart;
	std::vector<std::pair<std::string, double>>		m_stepTimes;
	std::map<std::string, long>						m_stepCounters;
	std::map<std::string, long>						m_counters;
	std::map<std::string, std::vector<std::string>>	m_summary;
	std::vector<std::string>						m_summarySections;
	long											m_nWarnings = 0;
	long											m_nErrors = 0;
};

} // end namespace

#endif // LoggerH
