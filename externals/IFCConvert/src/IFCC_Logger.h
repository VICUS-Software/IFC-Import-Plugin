#ifndef LoggerH
#define LoggerH

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <QMutex>

namespace IFCC {

class Logger;

/*! Serializes concurrent log-line flushes. Logger is used from inside OMP
	parallel regions (per-space matching, shell anchoring) — unsynchronized
	writes to the shared ofstreams are undefined behaviour and caused sporadic
	crashes that vanished under gdb. */
inline QMutex& logFlushMutex() {
	static QMutex m;
	return m;
}

/*! Accumulates a single log line across chained operator<< calls.
	The line is flushed (with timestamp) on destruction — typically at the
	end of the full-expression containing `Logger::instance() << ...`.
*/
class LogLine {
public:
	LogLine(std::ofstream* main, std::ofstream* step, const std::string& ts)
		: m_main(main), m_step(step), m_ts(ts) {}

	LogLine(const LogLine&) = delete;
	LogLine& operator=(const LogLine&) = delete;

	LogLine(LogLine&& o) noexcept
		: m_main(o.m_main), m_step(o.m_step), m_ts(std::move(o.m_ts)), m_buf(std::move(o.m_buf)), m_active(o.m_active)
	{
		o.m_active = false;
	}

	~LogLine() {
		if(!m_active)
			return;
		std::string line = "[" + m_ts + "] " + m_buf.str();
		QMutexLocker lock(&logFlushMutex());
		if(m_main)
			*m_main << line << std::endl;
		if(m_step)
			*m_step << line << std::endl;
	}

	template<typename T>
	LogLine& operator<<(const T& v) {
		m_buf << v;
		return *this;
	}

private:
	std::ofstream* m_main;
	std::ofstream* m_step;
	std::string    m_ts;
	std::ostringstream m_buf;
	bool m_active = true;
};


class Logger {
public:
	static Logger& instance() {
		static Logger log;
		return log;
	}

	inline void set(const std::string& filename) {
		m_out.open(filename);
		m_opened = m_out.is_open();
	}

	/*! Open a fresh per-step log file under /tmp for the given step name.
		Subsequent << writes are tee'd to this file and the main log.
		Step counter is auto-incremented; filename is /tmp/ifc-import-NN-<name>.log.
	*/
	inline void beginStep(const std::string& name) {
		if(m_stepOut.is_open())
			m_stepOut.close();
		++m_stepCounter;
		std::ostringstream path;
		path << "/tmp/ifc-import-"
			 << std::setw(2) << std::setfill('0') << m_stepCounter
			 << "-" << name << ".log";
		m_stepOut.open(path.str());
		m_stepOpened = m_stepOut.is_open();
		// Ensure main log is available even if set() was never called.
		if(!m_opened) {
			m_out.open("/tmp/ifc-import-main.log");
			m_opened = m_out.is_open();
		}
		(*this) << "=== BEGIN STEP " << m_stepCounter << " " << name << " ===";
	}

	/*! Reset step counter so a re-run of the pipeline starts at step 1 again. */
	inline void resetSteps() {
		if(m_stepOut.is_open())
			m_stepOut.close();
		m_stepOpened = false;
		m_stepCounter = 0;
	}

	/*! Begin a new log line. The returned LogLine accumulates all chained <<
		arguments and writes them as a single timestamped line on destruction.
	*/
	template<typename T>
	LogLine operator<<(const T& msg) {
		LogLine ll(m_opened ? &m_out : nullptr,
				   m_stepOpened ? &m_stepOut : nullptr,
				   timestamp());
		ll << msg;
		return ll;
	}

private:
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

	bool m_opened = false;
	bool m_stepOpened = false;
	int m_stepCounter = 0;
	std::ofstream m_out;
	std::ofstream m_stepOut;

};

} // end namespace

#endif // LogggerH
