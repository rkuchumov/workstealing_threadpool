#ifndef STACCATO_SCEDULER_H
#define STACCATO_SCEDULER_H

#include <cstdlib>
#include <thread>
#include <atomic>

#include "constants.h"

namespace staccato
{

class task;

namespace internal
{
class worker;
}

class scheduler
{
public:
	static void initialize(size_t nthreads = 0, size_t deque_log_size = 7);
	static void terminate();

	static void spawn_and_wait(task *t);

private:
	friend class task;
	friend class internal::worker;

	scheduler();
	~scheduler();

	static std::atomic_bool is_active;

	task *root;

	static size_t workers_count;
	static internal::worker **workers;

	static void wait_workers_fork();

	static internal::worker *get_victim(internal::worker *thief);
};

} /* namespace:staccato */ 

#endif /* end of include guard: STACCATO_SCEDULER_H */

