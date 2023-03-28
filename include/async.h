#include <any>
#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#include <errno.h>
#include <string.h>
#include <sys/event.h>

namespace async {

// Dedicated exception for those thrown from `async`
class AsyncException : public std::exception {
public:
  AsyncException(const char *msg) : _msg(msg) { }

  // copy elision plz work
  AsyncException(std::string msg) : _msg(msg) { }

  const char * what() const throw() {
    return _msg.data();
  }

private:
  std::string _msg;
};


///
namespace selector {

// An identifier for an Event. Embedded within underlying kevent structure
struct Token {
  std::size_t id;

  bool operator==(const Token &other) const {
    return id == other.id;
  }
};

// Express read/write interest on a file descriptor
enum Interest {
  Readable = 0x01,
  Writable = 0x02
};

// Represents the source of an event
//
// Currently just wraps a `kevent`, but exists to serve as an abstraction for an event in
// other poll selectors (epoll, etc.)
class Event {

friend class Poll;

public:
  Event() = default;

  // Construct an Event with the given descriptor, filters, flags, and token identifier
  Event(int fd, uint32_t filter, uint32_t flags, Token t) {
    EV_SET(&_kevent, fd, filter, flags, 0, 0, (void *)t.id);
  }

  // Retrieve the token associated with this event
  Token get_token() {
    return Token{ (std::size_t)_kevent.udata };
  }

  // Retireve the specific readiness interests to the underlying IO source
  Interest get_interest() {
    switch (_kevent.filter) {
      case EVFILT_READ:
        return Interest::Readable;
      case EVFILT_WRITE:
        return Interest::Writable;
      default:
        throw async::AsyncException("only support read/write interests right now");
    }
  }

private:
  struct kevent _kevent;
};

// Wrapper over a list of events, helpful for re-use
template <std::size_t N>
struct Events {
  std::array<Event, N> events;
};

// An interface to interact with and wait on I/O events
//
// Uses kqueue/kevent syscall
class Poll {

public:
  // Creates a Poll backed by a kernel kqueue, throwing an error if syscall fails
  Poll() {
    _kq = kqueue();

    if (_kq == -1) {
      throw async::AsyncException("failed to create kqueue");
    }
  }

  // Blocks until any of the registered events trigger, with an optional timeout for the
  // blocking duration
  template <std::size_t N>
  unsigned int poll(Events<N> &events,
                    std::optional<std::chrono::nanoseconds> timeout) {

    // convert std duration to timespec
    struct timespec tm;
    if (timeout) {
      using namespace std::chrono;

      auto secs = duration_cast<seconds>(*timeout);
      *timeout -= secs;
      tm = timespec { secs.count(), timeout->count() };
    }

    // block until event occurs with timeout
    int ret = kevent(
        _kq,
        nullptr,
        0,
        reinterpret_cast<struct kevent *>(events.events.data()),
        N,
        (timeout) ? &tm : nullptr
    );

    if (ret == -1) {
      throw async::AsyncException(std::string("kevent error: ") + strerror(errno));
    }

    return ret;
  }

  // Register events for given file descriptor
  //
  // `token` is used as an identifier to the events as the output of poll
  void register_event(int fd, Token token, Interest interests) {
    uint32_t flags = EV_CLEAR | EV_RECEIPT | EV_ADD;

    // Might need two Event's for both readable/writable interest
    Event changes[2];
    auto n_changes = 0;

    // add event for readable/writable
    if (interests & Interest::Readable) {
      changes[n_changes] = Event(fd, EVFILT_READ, flags, token);
      n_changes++;
    }
    if (interests & Interest::Writable) {
      changes[n_changes] = Event(fd, EVFILT_WRITE, flags, token);
      n_changes++;
    }

    // add events to kqueue
    int ret = kevent(_kq, (struct kevent *)&changes, n_changes, nullptr, 0, nullptr);

    // check whether syscall was successful
    if (ret == -1) {
      throw async::AsyncException("could not add event to register");
    }

    // loop through errors (if any) and throw first one
    check_events(changes);
  }

private:
  // check if an array of Event's returned by kevent() for errors in order, throwing the
  // first one encountered
  template <std::size_t N>
  void check_events(Event (&events)[N]) {
    for (auto i = 0u; i < N; i++) {
      if (events[i]._kevent.flags & EV_ERROR) {
        throw async::AsyncException(
            std::string("kevent error: ") + strerror(events[i]._kevent.data));
      }
    }
  }

private:
  // handle to kqueue
  int _kq;
};

// Foreward declaration of a wrapper classs that wraps
class PollEvented;

}; // ns selector
}; // ns async

/*
 * random things that need to be done outside of namespaces
 */

// make selector::Token hashable to use in unordered_map
template <>
struct std::hash<async::selector::Token> {
  std::size_t operator()(async::selector::Token const &t) const noexcept {
    return std::hash<std::size_t>{}(t.id);
  }
};


///
namespace async {
namespace future {

// Function pointer type for waker function--a no-arg function that wakes up the task
//using WakerFn = void (*)();
using WakerFn = std::function<void()>;

// Holds information for future's parent task. Currently just holds waker to queue task
// back to executor
struct Context {
  WakerFn waker;
};

// Represents a not yet completed value
struct Pending { };

// Represents the completed value
template <typename T>
struct Ready { T val; };

// Empty struct for void return value
template <>
struct Ready<void> {};

// Polling a future returns either a pending status or a completed value
template <typename T>
using Result = std::variant<Ready<T>, Pending>;

// Represents an asynchronous computation that eventually produces a value
template <typename T>
class Future {
public:
  // Alias to access template type
  using Output = T;

  // Called by the runtime to progress the future. If the result is ready, poll returns
  // the completed value. If it is not, then this function is responsible for scheduling
  // the wake function to be called when it's possible to make more progress on this
  // future.
  virtual Result<T> poll(Context ctx) = 0;
};

}; // ns future


///
namespace task {

// TODO: right now the Type information of the future return type is lost. (not returned
// from poll() in Core). Thus we need a way for the Core to fill in some slot with the
// Type information present with the actual value. AKA when poll returns Ready(value),
// where is that value going to go? right now it just gets lost in poll()

class _Core {
public:
  virtual bool poll(future::Context ctx) = 0;
};

/// Holds the underlying future. This class is type-erased and held behind a generic
/// pointer, so operations to the underlying future are executed through this interface
///
/// Kind of acts as a vtable
//template <class F, class = typename std::enable_if_t<!std::is_lvalue_reference_v<F>>>
template <class F>
class Core : public _Core {
public:
  Core(F &&fut) : _fut(std::move(fut)) { }

  bool poll(future::Context ctx) {
    auto res = _fut.poll(ctx);

    // if future has completed
    if (res.index() == 0) {
      return true;
    }
    // if future is still pending
    else {
      return false;
    }
  }

private:
  // The actual future this task is driving
  F _fut;
};

// Represents an asynchronous task of execution.
//
// Wraps a future, while also driving...
class Task {
public:

  // TODO: need some API that returns a handle or something that implements the Future
  // trait, bedause a Task itself needs to be awaitable

  // TODO: technically all of this Core stuff right now can just be done through a simple
  // function pointer that Task holds on, and creates on the constructor. I guess the main
  // advantage of having a dedicated class is that there can be multiple methods (more
  // than just poll)

  // Create a task from a future
  //
  // The passed in future must inherit from future::Future. Task holds onto a pointer to
  // the Core wrapping the future.
  //template <class F, class = typename std::enable_if_t<!std::is_lvalue_reference_v<F>>>
  template <class F>
  Task(F &&fut) {
    // ensure that F is a future
    static_assert(std::is_base_of_v<future::Future<typename F::Output>, F>);

    Core<F> *c = new Core(std::move(fut));
    _core_ptr = (void *)c;
  }

  // Polls underlying future, returning whether is it ready
  bool poll(future::Context ctx) {
    return ((_Core *)_core_ptr)->poll(ctx);
  }

private:
  // A pointer to the Core holding the future
  void *_core_ptr;
};

} // ns task


///
namespace executor {

// Responsible for monitoring IO events and waking the respective tasks
class IODriver {
public:
  // block on the poll for IO events until an event occurs, or the timeout runs out,
  // whichever comes first.
  void turn(std::optional<std::chrono::nanoseconds> timeout) {
    // wait for events to occur
    auto num_events = _p.poll(_events, timeout);

    for (auto i = 0u; i < num_events; i++) {
      // call the waker associated with this event
      auto tok = _events.events[i].get_token();
      if (_resources.find(tok) != _resources.end()) {
        _resources[tok].waker();
      }
    }
  }

private:
  // for reuse in the poll
  selector::Events<256> _events;

  // The Poll to monitor I/O events
  selector::Poll _p;

  // Map from tokens to contexts
  std::unordered_map<selector::Token, future::Context> _resources;
};

// The executor is responsible for driving all the tasks and futures to completion. It can
// be run on both a single threaded or multiple threads
class LocalExecutor {
public:
  // Create the executor
  LocalExecutor() { }

  // Begin the executor, running the given future until completion
  template <class F>
  void block_on(F &&fut) {
    // ensure that F is a future
    static_assert(std::is_base_of_v<future::Future<typename F::Output>, F>);

    // create and push task for this future
    std::shared_ptr<task::Task> ptr(new task::Task(std::move(fut)));
    _tq.push_back(ptr);

    // drive tasks to completion, polling on them
    run();
  }

  // Run event loop, driving tasks to completion
  void run() {
    bool keep_running = true;

    // TODO: this can't be the loop condition. because the queue might be empty but there
    // might be events that cause tasks to be added back to the queue later, but if the
    // loop stops right away then those tasks never get processed. can't entirely rely on
    // IO driver either because in the case of a timer, it's another thread that will call
    // the waker, and this thread isn't going to be processed as an IO event
    while (keep_running) {
      // take next task and poll it
      auto t = _tq.front(); _tq.pop_front();

      // create contex from waker that adds this task back to the queue
      future::WakerFn waker = [&]() {
        _tq.push_back(t);
      };

      // poll this task
      bool is_ready = t->poll(future::Context { waker });

      // run an iteration of the IO driver
      _io_driver.turn(std::chrono::nanoseconds(1000));

      // continue looping?
    }
  }

private:
  // The future task queue--unguraded by any sync mechanism because this executor should
  // be purely single threaded
  std::deque<std::shared_ptr<task::Task>> _tq;

  // An IO driver to monitor events
  IODriver _io_driver;
};

}; // ns executor


//
namespace selector {

// TODO: okay so the way this needs to go down is you probs need some generic wrapper over
// different types of IO sources (tcp, udp, files, pipes, etc.), PollEvented is just what
// Tokio uses/calls but doesn't have to be like this. Either way though this wrapper
// structure should probs be generic over some event source, and also it should have a
// reference to the current IODriver. This way in the IODriver you can register this event
// in the Poll, and also add a waker to the corresponding ScheduledIO that wakes up this
// task. On that note, you probs need a way to get the current task? Might be as simple as
// ever over-arching task makes a waker that wakes itself up, then it passes this waker to
// all the futures it polls, which is thus also passed to this PollEvented thing
//
// TODO: one thing I just realized that is not resolved from the thing above is how to
// properly generate a token for an event. You can't just make a random one because then
// you can't have one token map to multiple wakers, which must be done (multiple diff
// people might be waiting on a single event). I don't see an easy way to get the token
// for an event (unless we are very roundabout and have a map from FD -> token) or
// something but that just seems wrong. It seems like Tokio does some scheme based on the
// driver tick, generation, etc. but need to investigate more.
class PollEvented {

private:
};

}; // ns selector

//
namespace sources {



}; // ns sources

}; // ns async


// polls on a future, immediately returning pending if the future is pending, and
// continuing with the result if not. this macro is specifically for when the return value
// is not needed
#define AWAIT(fut, ctx) { \
  auto res = fut.poll(ctx); \
  if (res.index() == 1) { \
    return async::future::Pending{}; \
  } \
}

// same as above but stores result in var, which must be a pointer
#define AWAIT_INTO(var, fut, ctx) { \
  auto res = fut.poll(ctx); \
  if (res.index() == 0) { \
    *var = std::get<1>(res)\
  } else { \
    return async::future::Pending{}; \
  } \
}
