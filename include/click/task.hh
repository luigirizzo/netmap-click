// -*- c-basic-offset: 4; related-file-name: "../../lib/task.cc" -*-
#ifndef CLICK_TASK_HH
#define CLICK_TASK_HH
#include <click/element.hh>
#include <click/sync.hh>
#if __MTCLICK__
# include <click/atomic.hh>
# include <click/ewma.hh>
#endif
CLICK_DECLS

#ifdef CLICK_BSDMODULE
#include <machine/cpu.h>
#include <assert.h>	/* MARKO XXX */
#endif

#ifdef CLICK_BSDMODULE
#define SPLCHECK				\
	int s = splimp();			\
	if (s == 0)				\
	    panic("not spl'ed: %d\n", s);	\
	splx(s);
#else
#define SPLCHECK
#endif

#define PASS_GT(a, b)	((int)(a - b) > 0)

typedef bool (*TaskHook)(Task *, void *);
class RouterThread;
class TaskList;
class Master;

class Task { public:

#ifdef HAVE_STRIDE_SCHED
    enum { STRIDE1 = 1U<<16, MAX_STRIDE = 1U<<31 };
    enum { MAX_TICKETS = 1<<15, DEFAULT_TICKETS = 1<<10 };
#endif
#ifdef HAVE_ADAPTIVE_SCHEDULER
    enum { MAX_UTILIZATION = 1000 };
#endif

    inline Task(TaskHook, void *);
    inline Task(Element *);		// call element->run_task()
    ~Task();

    bool initialized() const		{ return _router != 0; }
    bool scheduled() const		{ return _prev != 0; }

    TaskHook hook() const		{ return _hook; }
    void *thunk() const			{ return _thunk; }
    inline Element *element() const;

    Task *scheduled_next() const	{ return _next; }
    Task *scheduled_prev() const	{ return _prev; }
    RouterThread *scheduled_list() const { return _thread; }
    Master *master() const;
 
#ifdef HAVE_STRIDE_SCHED
    int tickets() const			{ return _tickets; }
    void set_tickets(int);
    void adj_tickets(int);
#endif

    void initialize(Router *, bool scheduled);
    void initialize(Element *, bool scheduled);
    void cleanup();
    void uninitialize()			{ cleanup(); } // deprecated

    void unschedule();
    inline void reschedule();

    inline int fast_unschedule();
    inline void fast_reschedule();

    void strong_unschedule();
    void strong_reschedule();

    int thread_preference() const	{ return _thread_preference; }
    void change_thread(int);

#ifdef CLICK_BSDMODULE
    inline void wakeup();
#endif
  
    inline void call_hook();

#ifdef HAVE_ADAPTIVE_SCHEDULER
    unsigned runs() const		{ return _runs; }
    unsigned work_done() const		{ return _work_done; }
    inline unsigned utilization() const;
    void clear_runs()			{ _runs = _work_done = 0; }
#endif
#if __MTCLICK__
    inline int cycles() const;
    inline void update_cycles(unsigned c);
#endif

  private:

    /* if gcc keeps this ordering, we may get some cache locality on a 16 or 32
     * byte cache line: the first three fields are used in list traversal */

    Task *_prev;
    Task *_next;
#ifdef HAVE_STRIDE_SCHED
    unsigned _pass;
    unsigned _stride;
    int _tickets;
#endif
  
    TaskHook _hook;
    void *_thunk;
  
#ifdef HAVE_ADAPTIVE_SCHEDULER
    unsigned _runs;
    unsigned _work_done;
#endif
#if __MTCLICK__
    DirectEWMA _cycles;
    unsigned _cycle_runs;
#endif

    RouterThread *_thread;
    int _thread_preference;
  
    Router *_router;

    enum { RESCHEDULE = 1, CHANGE_THREAD = 2 };
    unsigned _pending;
    Task *_pending_next;

    Task(const Task &);
    Task &operator=(const Task &);

    void add_pending(int);
    void process_pending(RouterThread *);
    inline void fast_schedule();
    void true_reschedule();
    inline void lock_tasks();
    inline bool attempt_lock_tasks();

    void make_list();
    static bool error_hook(Task *, void *);
  
    friend class RouterThread;
    friend class Master;
  
};


// need RouterThread's definition for inline functions
CLICK_ENDDECLS
#include <click/routerthread.hh>
CLICK_DECLS


inline
Task::Task(TaskHook hook, void *thunk)
    : _prev(0), _next(0),
#ifdef HAVE_STRIDE_SCHED
      _pass(0), _stride(0), _tickets(-1),
#endif
      _hook(hook), _thunk(thunk),
#ifdef HAVE_ADAPTIVE_SCHEDULER
      _runs(0), _work_done(0),
#endif
#if __MTCLICK__
      _cycle_runs(0),
#endif
      _thread(0), _thread_preference(-1),
      _router(0), _pending(0), _pending_next(0)
{
}

inline
Task::Task(Element *e)
    : _prev(0), _next(0),
#ifdef HAVE_STRIDE_SCHED
      _pass(0), _stride(0), _tickets(-1),
#endif
      _hook(0), _thunk(e),
#ifdef HAVE_ADAPTIVE_SCHEDULER
      _runs(0), _work_done(0),
#endif
#if __MTCLICK__
      _cycle_runs(0),
#endif
      _thread(0), _thread_preference(-1),
      _router(0), _pending(0), _pending_next(0)
{
}

inline Element *
Task::element()	const
{ 
    return _hook ? 0 : reinterpret_cast<Element*>(_thunk); 
}

inline int
Task::fast_unschedule()
{
#if CLICK_LINUXMODULE
    assert(!in_interrupt());
#endif
#if CLICK_BSDMODULE
    assert(!intr_nesting_level);
    SPLCHECK
#endif
    if (_prev) {
	_next->_prev = _prev;
	_prev->_next = _next;
	_next = _prev = 0;
    }
#if CLICK_BSDMODULE
    splx(s);
#endif
#if __MTCLICK__
    return _cycle_runs;
#else
    return 0;
#endif
}

#ifdef HAVE_STRIDE_SCHED

inline void 
Task::set_tickets(int n)
{
    if (n > MAX_TICKETS)
	n = MAX_TICKETS;
    else if (n < 1)
	n = 1;
    _tickets = n;
    _stride = STRIDE1 / n;
    assert(_stride < MAX_STRIDE);
}

inline void 
Task::adj_tickets(int delta)
{
    set_tickets(_tickets + delta);
}

inline void
Task::fast_reschedule()
{
    // should not be scheduled at this point
    assert(_thread);
#if CLICK_LINUXMODULE
    // tasks never run at interrupt time
    assert(!in_interrupt());
#endif
#if CLICK_BSDMODULE
    // assert(!intr_nesting_level); it happens all the time from fromdevice!
    SPLCHECK
#endif

    if (!scheduled()) {
	// increase pass
	_pass += _stride;

#if 0
	// look for 'n' immediately before where we should be scheduled
	Task *n = _thread->_prev;
	while (n != _thread && PASS_GT(n->_pass, _pass))
	    n = n->_prev;

	// schedule after 'n'
	_next = n->_next;
	_prev = n;
	n->_next = this;
	_next->_prev = this;
#else
	// look for 'n' immediately after where we should be scheduled
	Task *n = _thread->_next;
#ifdef CLICK_BSDMODULE /* XXX MARKO a race occured here when not spl'ed */
	while (n->_next != NULL && n != _thread && !PASS_GT(n->_pass, _pass))
#else
	while (n != _thread && !PASS_GT(n->_pass, _pass))
#endif
	    n = n->_next;
    
	// schedule before 'n'
	_prev = n->_prev;
	_next = n;
	_prev->_next = this;
	n->_prev = this;
#endif
    }
}

inline void
Task::fast_schedule()
{
    SPLCHECK
    assert(_tickets >= 1);
    _pass = _thread->_next->_pass;
    fast_reschedule();
}

#else /* !HAVE_STRIDE_SCHED */

inline void
Task::fast_reschedule()
{
    assert(_thread);
#if CLICK_LINUXMODULE
    // tasks never run at interrupt time
    assert(!in_interrupt());
#endif
#if CLICK_BSDMODULE
    assert(!intr_nesting_level);
    SPLCHECK
#endif
    if (!scheduled()) {
	_prev = _thread->_prev;
	_next = _thread;
	_thread->_prev = this;
	_thread->_next = this;
    }
}

inline void
Task::fast_schedule()
{
    fast_reschedule();
}

#endif /* HAVE_STRIDE_SCHED */

inline void
Task::reschedule()
{
    SPLCHECK
    assert(_thread);
    if (!scheduled())
	true_reschedule();
}

#ifdef CLICK_BSDMODULE
// XXX FreeBSD specific
// put tasks on the list of tasks to wakeup.
inline void
Task::wakeup()
{
    assert(_thread && !_prev);
    int s = splimp();
printf("Task::wakeup() - how did we get here?\n"); /* XXX MARKO */
    _next = _thread->_wakeup_list;
    _thread->_wakeup_list = this;
    splx(s);
}
#endif

inline void
Task::call_hook()
{
#if __MTCLICK__
    _cycle_runs++;
#endif
#ifdef HAVE_ADAPTIVE_SCHEDULER
    _runs++;
    if (!_hook)
	_work_done += ((Element *)_thunk)->run_task();
    else
	_work_done += _hook(this, _thunk);
#else
    if (!_hook)
	(void) ((Element *)_thunk)->run_task();
    else
	(void) _hook(this, _thunk);
#endif
}

#ifdef HAVE_ADAPTIVE_SCHEDULER
inline unsigned
Task::utilization() const
{
    return (_runs ? (MAX_UTILIZATION * _work_done) / _runs : 0);
}
#endif

#if __MTCLICK__
inline int
Task::cycles() const
{
    return _cycles.average() >> _cycles.scale;
}

inline void
Task::update_cycles(unsigned c) 
{
    _cycles.update_with(c);
    _cycle_runs = 0;
}
#endif

CLICK_ENDDECLS
#endif
