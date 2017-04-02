#ifndef STACCATO_TASK_H
#define STACCATO_TASK_H

#include <cstdlib>
#include <atomic>
#include <ostream>

#include "constants.h"

namespace staccato
{

namespace internal {
class worker;
}

class task
{
public:
	task();
	virtual ~task();

	void spawn(task *t);
	void wait_for_all();

#if STACCATO_DEBUG
	enum task_state {
		undefined    = 0,
		initializing = 1,
		spawning     = 2,
		ready        = 3,
		taken        = 4,
		stolen       = 5,
		executing    = 6,
		finished     = 7
	};
	void set_state(unsigned s);
	unsigned get_state();
	std::atomic_uint state;
#endif // STACCATO_DEBUG

	virtual void execute() = 0;

private:
	friend class internal::worker;

	task *parent;

	internal::worker *executer;

	std::atomic_size_t subtask_count;

};

#if STACCATO_DEBUG
std::ostream& operator<<(std::ostream & os, task::task_state &state);
#endif // STACCATO_DEBUG

}

#endif /* end of include guard: STACCATO_TASK_H */
