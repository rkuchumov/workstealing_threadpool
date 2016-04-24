#ifndef STACCATO_TASK_H
#define STACCATO_TASK_H

#include <cstdlib>
#include <atomic>

#include "constants.h"

namespace staccato
{

class task
{
public:
	task();
	virtual ~task();

	void spawn(task *t);
	void wait_for_all();

#if STACCATO_DEBUG
	enum {
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
	const char *get_state_str();
#endif // STACCATO_DEBUG

	virtual void execute() = 0;

private:
	friend class scheduler;

	task *parent;

	std::atomic_size_t subtask_count;

#if STACCATO_DEBUG
	std::atomic_uint state;
#endif // STACCATO_DEBUG
};

}

#endif /* end of include guard: STACCATO_TASK_H */
