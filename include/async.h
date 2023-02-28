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

// Attached to `kevent`s to identify them
struct Token {
  std::size_t id;
};

// Express read/write interest on a file descriptor
enum Interests {
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
  void register_event(int fd, Token token, Interests interests) {
    uint32_t flags = EV_CLEAR | EV_RECEIPT | EV_ADD;

    // Might need two Event's for both readable/writable interest
    Event changes[2];
    auto n_changes = 0;

    // add event for readable/writable
    if (interests & Interests::Readable) {
      changes[n_changes] = Event(fd, EVFILT_READ, flags, token);
      n_changes++;
    }
    if (interests & Interests::Writable) {
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

// make selector::Token equalable
inline bool operator==(const async::selector::Token &lhs,
                       const async::selector::Token &rhs) {
  return lhs.id == rhs.id;
}


///
namespace async {
namespace future {

// Function pointer type for waker function--a no-arg function that wakes up the task
using WakerFn = void (*)();

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

/// Holds the underlying future. This class is type-erased and held behind a generic
/// pointer, so operations to the underlying future are executed through this interface
///
/// Kind of acts as a vtable
template <class F>
class Core {
public:
  Core(F fut) : _fut(fut) { }

  void poll(future::WakerFn wake) {
    auto res = _fut.poll(wake);

    // if future has completed
    if (res.index() == 0) {
    }
    // if future is still pending
    else {
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
  // Create a task from a future.
  //
  // The passed in future must inherit from future::Future. Task holds onto a pointer to
  // the Core wrapping the future.
  template <class F>
  Task(F fut) {
    // ensure that F is a future
    static_assert(std::is_base_of_v<future::Future<typename F::Output>, F>);

    Core<F> *c = new Core(fut);
    _core_ptr = (void *)c;
  }

private:
  // A pointer to the Core holding the future
  void *_core_ptr;
};

} // ns task


///
namespace executor {

class IODriver {
public:
  IODriver() { }

  // block on the poll for IO events until an event occurs, or the timeout runs out,
  // whichever comes first.
  void turn(std::optional<std::chrono::nanoseconds> timeout) {
  }

private:
  // for reuse in the poll
  selector::Events<256> _events;

  // The Poll to monitor I/O events
  selector::Poll _p;

  // Map from tokens to wakers
  std::unordered_map<selector::Token, future::WakerFn> _scheduled_io;
};

enum ExecutorType {
  MULTI_THREAD,
  SINGLE_THREAD,
};

template <ExecutorType>
class Executor;

template<>
class Executor<ExecutorType::SINGLE_THREAD> {
public:
  // Create the executor
  Executor() { }

  // Begin the executor, running the given future until completion
  template <class F>
  void block_on(F fut) {
    // ensure that F is a future
    static_assert(std::is_base_of_v<future::Future<typename F::Output>, F>);

    // create and push task from this future
    _tq.push_back(std::make_shared(fut));

    // drive tasks to completion, polling on them
    while (true) {
    }
  }

private:
  // The future task queue, guarded by a mutex for read/writes
  std::mutex _tq_guard;
  std::deque<std::shared_ptr<task::Task>> _tq;
};

template <>
class Executor<ExecutorType::MULTI_THREAD> {
};

}; // ns executor

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

// TODO: figure out thread parking/unparking (probs need to do something based on
// conditional variables?)
