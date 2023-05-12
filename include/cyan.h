#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <sys/event.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cyan {

// read vs write interest in a socket
enum Interest { Readable, Writable };

// function type for the handler functions invoked on an event
using Handler = std::function<void()>;

// responsible for tracking event changes and dispatching to the respective
// handlers
class Executor {

private:
  // Worker manages a thread that pulls from an Executor's work queue and runs it
  class Worker {
  public:
    // an empty worker, with the thread doing nothing
    Worker(std::size_t id) : _keep_running(true), _id(id) {
      //std::cout << "worker " << _id << " initialized: " << _keep_running.load() << std::endl;
    }

    // move constructs a worker, taking ownership of the underlying thread
    Worker(Worker &&other) {
      _keep_running = other._keep_running.load();
      _t = std::move(other._t);
      _id = other._id;
      //std::cout << "worker " << _id << " moved: " << _keep_running.load() << std::endl;
    }

    // safety of holding a reference to exec--Executor, upon destruction, calls the
    // destructor of all its Workers, which will stop the running thread (the referencer
    // of exec) and thus it will be safe to destroy exec
    void start(Executor &exec) {
      _t = std::thread([&](std::size_t _id) {
          Executor::Token tok;
          Handler handler;
          std::cout << (_keep_running.load() ? "true" : "false") << std::endl;

          // check kill signal
          while (_keep_running.load()) {
            //std::cout << "worker " << _id << " started " << std::endl;
            // wait until the cond var is notified about work to do
            std::unique_lock<std::mutex> wql(exec._wqm);
            exec._work_cv.wait(wql, [&]{ return !exec._wq.empty(); });

            // we now have the lock acquired and take the next task off the queue
            // check that the cv condition actually ran--that is, the work queue actually has work
            assert(!exec._wq.empty());
            tok = exec._wq.front();
            std::cout << "Worker " << _id << " processing tok " << tok << std::endl;
            exec._wq.pop_front();
            wql.unlock();

            // get read handle to handler map and call handler
            exec._hsm.lock_shared();
            handler = exec._handlers[tok];
            exec._hsm.unlock_shared();

            // call handler
            handler();
          }
      }, _id);
    }

    // stops the worker
    void stop() {
      _keep_running.store(false);
      if (_t.joinable()) {
        _t.join();
      }
    }

    ~Worker() {
      //std::cout << "worker " << _id << " destroyed" << std::endl;
      stop();
    }

  public:
    // keeps the thread loop running
    std::atomic<bool> _keep_running;

    // handle to thread
    std::thread _t;

    // worker id
    std::size_t _id;
  };


public:
  Executor() {
    // initialize a kqueue
    if ((_kq = kqueue()) == -1) {
      throw std::runtime_error("failed to create kqueue");
    }
  }

  // registers a handler with the executor for the given file descriptor and its
  // interest
  //
  // currently, the handler will simply be invoked as soon as there is a
  // read/write event for the fd. this means that it is the handler's
  // responsibility to appropriately read/write from the fd.
  void register_handler(int fd, Interest interest, Handler handler) {
    // fill in kevent struct
    struct kevent ke;
    auto flags = EV_CLEAR | EV_RECEIPT | EV_ADD;
    auto filter = (interest == Interest::Readable) ? EVFILT_READ : EVFILT_WRITE;
    auto tok = next_token();
    EV_SET(&ke, fd, filter, flags, 0, 0, (void *)tok);

    // map handler to generated token, get write lock
    _hsm.lock();
    _handlers[tok] = handler;
    _hsm.unlock();

    // register kevent with kqueue
    if (kevent(_kq, &ke, 1, nullptr, 0, nullptr) == -1) {
      throw std::runtime_error("could not add kevent to kqueue");
    }
  }

  // runs the executor, polling for event changes and calling the respective handlers
  void run() {
    // start one (arbitrary) worker
    //
    // TODO: make this number of workers dynamically scale
    for (std::size_t i = 0; i < 2; i++) {
      _workers.push_back(Worker(i));
      std::cout << _workers[i]._id << std::endl;
    }
    for (std::size_t i = 0; i < 2; i++) {
      _workers[i].start(*this);
    }

    while (true) {
      // wait for an event to trigger, blocking indefinitely
      int ret;
      if ((ret = kevent(_kq, nullptr, 0, _kevents, KEVENTS_SIZE, nullptr)) == -1) {
        throw std::runtime_error(std::string("kevent error: ") + strerror(errno));
      }

      // serially call the handlers for each of the events
      for (int i = 0; i < ret; i++) {
        // parse out token
        auto tok = (Token)_kevents[i].udata;

        // send to workers
        dispatch(tok);
      }
    }
  }

private:
  // identifier for an event
  using Token = std::size_t;

  // returns the next token for use
  //
  // atomically increments a counter to use as the token
  Token next_token() {
    static std::atomic<std::size_t> curr = 1;

    return curr.fetch_add(1, std::memory_order_relaxed);
  }

  // idk yet but this needs to call the handler for this token asynchronously,
  // probs cud add it to a shared worker queue? idk if you should add the token
  // or the handler function itself to the queue
  void dispatch(Token tok) {
    std::lock_guard<std::mutex> wql(_wqm);
    _wq.push_back(tok);
    _work_cv.notify_one();
  }

private:
  // handle to kqueue
  int _kq;

  // array of kevent structs to retrieve results of kevent syscall. 24 is arbitrary
  static constexpr std::size_t KEVENTS_SIZE = 24;
  struct kevent _kevents[KEVENTS_SIZE];

  // task queue for workers
  std::deque<Token> _wq;
  std::mutex _wqm;
  std::condition_variable _work_cv;

  // holds worker handles
  std::vector<Worker> _workers;

  // tracks the handler for each event registered in the executor. read/write lock
  // protection, should be read-heavy
  std::unordered_map<Token, Handler> _handlers;
  std::shared_mutex _hsm;
};


void on_event(int fd, Interest interest, Handler handler) {
  // reference to global executor
  static Executor _exec;
}

}; // namespace cyan
