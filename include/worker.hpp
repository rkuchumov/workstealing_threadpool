#ifndef WORKER_H_MIRBQTTK
#define WORKER_H_MIRBQTTK

#include <cstdlib>
#include <thread>
#include <limits>

#include "task_deque.hpp"
#include "lifo_allocator.hpp"
#include "constants.hpp"
#include "task.hpp"
#include "counter.hpp"

namespace staccato
{
namespace internal
{

template <typename T>
class worker
{
public:
	worker(
		size_t id,
		lifo_allocator *alloc,
		size_t taskgraph_degree,
		size_t taskgraph_height
	);

	~worker();

	void set_victim(worker<T> *victim);

	void stop();

	void local_loop(task_deque<T> *tail);

	void steal_loop();

	T *root_allocate();
	void root_commit();
	void root_wait();

#ifdef STACCATO_DEBUG
	void print_counters();
#endif

private:
	void init(size_t core_id, worker<T> *victim);

    void grow_tail(task_deque<T> *tail);

    task<T> *steal_task(task_deque<T> *tail, task_deque<T> **victim);

	const size_t m_id;
	const size_t m_taskgraph_degree;
	const size_t m_taskgraph_height;
	lifo_allocator *m_allocator;

#ifdef STACCATO_DEBUG
	counter m_counter;
#endif

	std::atomic_bool m_stopped;

	worker<T> *m_victim;

	task_deque<T> *m_victim_head;

	task_deque<T> *m_victim_tail;

	task_deque<T> *m_head_deque;
};

template <typename T>
worker<T>::worker(
	size_t id,
	lifo_allocator *alloc,
	size_t taskgraph_degree,
	size_t taskgraph_height
)
: m_id(id)
, m_taskgraph_degree(taskgraph_degree)
, m_taskgraph_height(taskgraph_height)
, m_allocator(alloc)
, m_stopped(false)
, m_victim(nullptr)
, m_victim_head(nullptr)
, m_victim_tail(nullptr)
, m_head_deque(nullptr)
{
	auto d = m_allocator->alloc<task_deque<T>>();
	auto t = m_allocator->alloc_array<T>(m_taskgraph_degree);
	new(d) task_deque<T>(m_taskgraph_degree, t);
	d->set_null(false);

	m_head_deque = d;

	for (size_t i = 1; i < m_taskgraph_height + 1; ++i) {
		auto n = m_allocator->alloc<task_deque<T>>();
		auto t = m_allocator->alloc_array<T>(m_taskgraph_degree);
		new(n) task_deque<T>(m_taskgraph_degree, t);

		n->set_prev(d);
		d->set_next(n);

		d = n;
	}
}

template <typename T>
worker<T>::~worker()
{
	delete m_allocator;
}

template <typename T>
void worker<T>::set_victim(worker<T> *victim)
{
	m_victim = victim;

	auto h = m_head_deque;
	m_victim_head = m_victim->m_head_deque;
	auto p = m_victim_head;

	while (h && p) {
		h->set_victim(p);

		p = p->get_next();
		h = h->get_next();
	}

	m_victim_tail = p;
}

template <typename T>
void worker<T>::stop()
{
	m_stopped = true;
}

template <typename T>
T *worker<T>::root_allocate()
{
	return m_head_deque->put_allocate();
}

template <typename T>
void worker<T>::root_commit()
{
	m_head_deque->put_commit();
}

template <typename T>
void worker<T>::root_wait()
{
	local_loop(m_head_deque);
}

template <typename T>
void worker<T>::grow_tail(task_deque<T> *tail)
{
	if (tail->get_next())
		return;

	auto d = m_allocator->alloc<task_deque<T>>();
	auto t = m_allocator->alloc_array<T>(m_taskgraph_degree);
	new(d) task_deque<T>(m_taskgraph_degree, t);

	d->set_prev(tail);
	tail->set_next(d);

	if (!m_victim_tail)
		return;

	auto v = m_victim_tail->get_next();
	if (!v) 
		return;

	d->set_victim(v);
	m_victim_tail = v;
}

template <typename T>
void worker<T>::steal_loop()
{
	auto vtail = m_victim_head;

	while (!load_relaxed(m_stopped)) {
		bool was_null = false;
		bool was_empty = false;

		auto t = vtail->steal(&was_empty, &was_null);

#ifdef STACCATO_DEBUG
		if (t)
			COUNT(steal);
		else if (was_null)
			COUNT(steal_empty);
		else if (was_empty)
			COUNT(steal_empty);
		else
			COUNT(steal_race);
#endif

		if (t) {
			t->process(this, m_head_deque);
			vtail->return_stolen();
			continue;
		}

		if (was_empty)
		{
			if (vtail->get_next())
				vtail = vtail->get_next();
		}

		if (was_null)
		{
			if (vtail->get_prev())
				vtail = vtail->get_prev();
		}
	}
}

template <typename T>
void worker<T>::local_loop(task_deque<T> *tail)
{
	task<T> *t = nullptr;
	task_deque<T> *victim = nullptr;

	while (true) { // Local tasks loop
		if (t) {
			grow_tail(tail);

			t->process(this, tail->get_next());

			if (victim) {
				victim->return_stolen();
				victim = nullptr;
			}
		}

		size_t nstolen = 0;

		t = tail->take(&nstolen);

#ifdef STACCATO_DEBUG
		if (t)
			COUNT(take);
		else if (nstolen == 0)
			COUNT(take_empty);
		else
			COUNT(take_stolen);
#endif

		if (t)
			continue;

		if (nstolen == 0)
			return;

#ifdef STACCATO_DEBUG
		bool was_null = false;
		bool was_empty = false;
#endif

		t = steal_task(tail, &victim);

#ifdef STACCATO_DEBUG
		if (t)
			COUNT(steal2);
		else if (was_null)
			COUNT(steal2_empty);
		else if (was_empty)
			COUNT(steal2_empty);
		else
			COUNT(steal2_race);
#endif
	}
}

template <typename T>
task<T> *worker<T>::steal_task(task_deque<T> *tail, task_deque<T> **victim)
{
	auto v = tail->get_victim();
	if (!v)
		return nullptr;

	bool was_null = false;
	bool was_empty = false;

	auto t = v->steal(&was_empty, &was_null);

	if (t) {
		*victim = v;
		return t;
	}

	return nullptr;
}

#ifdef STACCATO_DEBUG

template <typename T>
void worker<T>::print_counters()
{
	m_counter.print(m_id);
}

#endif

}
}

#endif /* end of include guard: WORKER_H_MIRBQTTK */
