#include <atomic>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/event.h>
#include <unordered_map>

namespace cyan {

// read vs write interest in a socket
enum Interest { Readable, Writable };

// function type for the handler functions invoked on an event
using Handler = std::function<void()>;

// responsible for tracking event changes and dispatching to the respective handlers
class Executor {
public:
  Executor() {
    // initialize a kqueue
    if ((_kq = kqueue()) == -1) {
      throw std::runtime_error("failed to create kqueue");
    }
  }

  // registers a handler with the executor for the given file descriptor and its interest
  //
  // currently, the handler will simply be invoked as soon as there is a read/write event
  // for the fd. this means that it is the handler's responsibility to appropriately
  // read/write from the fd.
  void register_handler(int fd, Interest interest, Handler handler) {
    // fill in kevent struct
    struct kevent ke;
    auto flags = EV_CLEAR | EV_RECEIPT | EV_ADD;
    auto filter = (interest == Interest::Readable) ? EVFILT_READ : EVFILT_WRITE;
    auto tok = next_token();
    EV_SET(&ke, fd, filter, flags, 0, 0, (void *)tok);

    // map handler to generated token
    _handlers[tok] = handler;

    // register kevent with kqueue
    int ret;
    if ((ret = kevent(_kq, &ke, 1, nullptr, 0, nullptr)) == -1) {
      throw std::runtime_error("could not add kevent to kqueue");
    }
  }

  // runs the executor, polling for event changes and calling the respective handlers
  void run() {
    int ret;

    while (true) {
      // wait for an event to trigger, blocking indefinitely
      if ((ret = kevent(_kq, nullptr, 0, _kevents, KEVENTS_SIZE, nullptr)) == -1) {
        throw std::runtime_error(std::string("kevent error: ") + strerror(errno));
      }

      // serially call the handlers for each of the events
      for (int i = 0; i < ret; i++) {
        // parse out token
        auto tok = (Token)_kevents[i].udata;
        _handlers[tok]();
      }
    }
  }

private:
  // identifier for an event
  using Token = std::size_t;

  // returns the next token for use
  //
  // simply atomically increments a counter to use as the token
  Token next_token() {
    static std::atomic<std::size_t> curr = 1;

    return curr.fetch_add(1, std::memory_order_relaxed);
  }

private:
  // handle to kqueue
  int _kq;

  // array of kevent structs to retrieve results of kevent syscall
  //
  // TODO: make this size dynamic, based on the number of events registered?
  static constexpr std::size_t KEVENTS_SIZE = 24;
  struct kevent _kevents[KEVENTS_SIZE];

  // tracks the handler for each event registered in the executor
  std::unordered_map<Token, Handler> _handlers;
};

void on_event(int fd, Interest interest, Handler handler) {
  // reference to global executor
  static Executor _exec;
}

};