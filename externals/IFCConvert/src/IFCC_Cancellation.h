#ifndef IFCC_CancellationH
#define IFCC_CancellationH

#include <atomic>

namespace IFCC {

/*! Lightweight pipeline-wide cancellation flag for IFC import.
	Set from the UI thread (e.g. when the progress dialog's Abort button is pressed)
	and checked from worker code at safe boundaries to terminate early.
	All accesses go through std::atomic so it is safe to read from OMP worker threads
	while the main thread writes. */
class Cancellation {
public:
	/*! Set the cancellation state. */
	static void set(bool v) { instance().store(v, std::memory_order_relaxed); }

	/*! True if cancellation has been requested. */
	static bool isCancelled() { return instance().load(std::memory_order_relaxed); }

	/*! Clear the cancellation state — call at the start of a new convert run. */
	static void reset() { instance().store(false, std::memory_order_relaxed); }

private:
	static std::atomic<bool>& instance() {
		static std::atomic<bool> s_flag{false};
		return s_flag;
	}
};

} // namespace IFCC

#endif // IFCC_CancellationH
