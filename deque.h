#ifndef DEQUE_H
#define DEQUE_H

#include <deque>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <future>

struct stamped_int
{
    int val;
    int stamp;
};

template <class T>
class std_deque
{
public:
    std_deque(): _deque() 
    { }

    virtual ~std_deque ()
    { };

    T pop_bottom()
    {
        _lock.lock();

        if (_deque.empty()) {
            _lock.unlock();
            return NULL;
        }

        T val = _deque.front();
        _deque.pop_front();

        _lock.unlock();

        return val;
    }

    T pop_top()
    {
        if (!_lock.try_lock())
            return NULL;

        if (_deque.empty()) {
            _lock.unlock();
            return NULL;
        }

        T val = _deque.back();
        _deque.pop_back();

        _lock.unlock();

        return val;
    }

    void push_bottom(const T &val)
    {
        std::unique_lock<std::mutex> l(_lock);
        _deque.push_front(val);
    }

    void push_bottom_safe(const T &val)
    {
        std::unique_lock<std::mutex> l(_lock);
        _deque.push_front(val);
    }

    bool empty()
    {
        std::unique_lock<std::mutex> l(_lock);
        return _deque.empty();
    }

private:
    std::deque<T> _deque;
    std::mutex _lock;
};

template <class T>
class b_deque
{
public:
    b_deque()
    { 
        // _deque = new T[50];
        _deque = std::make_shared<T *> (new T[30000]);
        _top = {0, 0};
        _bottom = 0;
    }

    virtual ~b_deque ()
    { 
        // delete _deque;
    };

    T pop_top()
    {
        stamped_int old = _top.load();

        if (_bottom <= old.val)
            return NULL;

        T r = (*_deque)[old.val];

        stamped_int new_st = {old.val + 1, old.stamp + 1};
        if (_top.compare_exchange_weak(old, new_st)) {
            return r;
        }

        return NULL;
    }

    T pop_bottom()
    {
        if (_bottom == 0)
            return NULL;

        _bottom--;

        T r = (*_deque)[_bottom];

        stamped_int old = _top.load();
        stamped_int new_st = {0, old.stamp + 1};

        if (_bottom > old.val)
            return r;
        if (_bottom == old.val) {
            _bottom = 0;
            if (_top.compare_exchange_weak(old, new_st))
                return r;
        }

        _top.store(new_st);
        return NULL;
    }

    void push_bottom(const T &val)
    {
        (*_deque)[_bottom] = val;
        _bottom++;
    }

    void push_bottom_safe(const T &val)
    {
        (*_deque)[_bottom] = val;
        _bottom++;
    }

    bool empty() const
    {
        return (_top.load().val < _bottom);
    }

private:
    std::shared_ptr<T *> _deque;
    int _bottom;
    std::atomic<stamped_int> _top;

    std::mutex _lock;
};

template <class T>
class circular_array
{
public:
    circular_array(int size) {
        _log_size = size;
        _array = new T[1 << size];
    }

    int size() {
        return 1 << _log_size;
    }

    T get(int i) {
        return _array[i % size()];
    }

    void put(int i, T item) {
        _array[i % size()] = item;
    }

    circular_array<T> *resize(int top, int bottom) {
        auto a = new circular_array<T>(_log_size + 1);

        for(int i = top; i < bottom; i++)
            a->put(i, get(i));

        return a;
    }

private:
    T *_array;
    int _log_size;

};

template <class T>
class u_deque
{
public:
    u_deque() {
        _tasks = (new circular_array<T>(7));
        _top = 0;
        _bottom = 0;
    }

    bool empty() const
    {
        int t = _top.load();
        int b = _bottom.load();
        return t >= b;
    }

    void push_bottom(const T &o)
    {
        int t = _top.load();
        int b = _bottom.load();

        circular_array<T> *a = _tasks.load();

        int size = b - t;
        if (size >= a->size() - 1) {
            a = a->resize(t, b);
            _tasks = a;
        }

        a->put(b, o);
        _bottom = b + 1;
    }

    T pop_bottom()
    {
        int b = _bottom.load();
        circular_array<T> *a = _tasks.load();

        b--;
        _bottom = b;

        int t = _top.load();

        int size = b - t;
        if (size < 0) {
            _bottom = t;
            return NULL;
        }

        T r = a->get(b);
        if (size > 0)
            return r;

        if (!_top.compare_exchange_strong(t, t + 1)) {
            r = NULL;
        }

        _bottom = t + 1;
        return r;
    }
    
    T pop_top()
    {
        int t = _top.load();
        circular_array<T> *a = _tasks.load();
        int b = _bottom.load();

        int size = b - t;
        if (size <= 0) {
            return NULL;
        }

        T r = a->get(t);
        if (!_top.compare_exchange_strong(t, t + 1)) {
            return NULL;
        }

        return r;
    }

private:
    std::atomic<circular_array<T> *> _tasks;
    std::atomic_int _top;
    std::atomic_int _bottom;
};

#endif /* end of include guard: DEQUE_H */
