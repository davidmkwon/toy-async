#include <any>
#include <array>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <errno.h>
#include <string.h>
#include <sys/event.h>

namespace async {

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

// Represents the source of an event
//
// Currently just wraps a `kevent`, but exists to serve as an abstraction for an event in
// other poll selectors (epoll, etc.)
class Event {

friend class Poll;

public:
  Event();

private:
  struct kevent _kevent;
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
  unsigned int poll(std::array<Event, N> events,
                    std::optional<std::chrono::nanoseconds> timeout) {
    // make sure that we can use vector of Events as a `struct kevent *`
    static_assert(sizeof(Event) == sizeof(struct kevent));

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
        reinterpret_cast<struct kevent *>(events.data()),
        N,
        (timeout) ? &tm : nullptr
    );

    if (ret == -1) {
      throw async::AsyncException(std::string("kevent error: ") + strerror(errno));
    }

    return ret;
  }

  // Add an event to listen for in the poll selector
  void add_event(Event &event) {
    int ret = kevent(_kq, &event._kevent, 1, nullptr, 0, nullptr);

    // check whether syscall was successful
    if (ret == -1) {
      throw async::AsyncException("could not add event to register");
    } else if (event._kevent.flags & EV_ERROR) {
      throw async::AsyncException(
          std::string("kevent error: ") + strerror(event._kevent.data));
    }
  }

private:
  // handle to kqueue
  int _kq;
};

}; // ns selector


///
namespace future {

// Function pointer type for waker function--a no-arg function that wakes up the task
using WakerFn = void (*)();

// Represents a not yet completed value
struct Pending { };

// Represents the completed value
template <typename T>
struct Ready { T val; };

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
  virtual Result<T> poll(WakerFn wake) = 0;
};

}; // ns future


///
namespace task {

// TODO: right now the Type information of the future return type is lost. (not returned
// from poll() in Core). Thus we need a way for the Core to fill in some slot with the
// Type information present with the actual value. AKA when poll returns Ready(value),
// where is that value going to go? right now it just gets lost in poll()

/// Holds the underlying future. This class is type-erased and held behind a generic
/// pointer, so operations to the underlying future are executed through this interface
///
/// Kind of acts as a vtable
template <class F>
class Core {
public:
  Core(F future) : _future(future) { }

  void poll(future::WakerFn wake) {
    auto res = _future.poll(wake);

    // if future has completed
    if (res.index() == 0) {
    }
    // if future is still pending
    else {
    }
  }

private:
  // The actual future this task is driving
  F _future;
};

// Represents an asynchronous task of execution.
//
// Wraps a future, while also driving...
class Task {
public:
  // Create a task from a future.
  //
  // The passed in future must inherit from future::Future. Task holds onto a pointer to
  // the Core wrapping the future.
  template <class F, class T,
            typename = std::enable_if<
              std::is_base_of_v<future::Future<T>, F>>>
  Task(F future) {
    Core<F> *c = new Core(future);
    _core_ptr = (void *)c;
  }

private:
  // A pointer to the Core holding the future
  void *_core_ptr;
};

} // ns task


///
namespace executor {

enum ExecutorType {
  MULTI_THREAD,
  SINGLE_THREAD,
};

class LocalExecutor {
public:
  LocalExecutor() { }

private:
  std::mutex _tq_guard;
  std::deque<task::Task> _tq;
};

class Executor;

}; // ns executor

}; // ns async

// TODO: figure out thread parking/unparking (probs need to do something based on
// conditional variables?)
