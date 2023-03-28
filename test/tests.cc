#include "async.h"

#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>

#include <unistd.h>
#include <stdio.h>
#include <sys/event.h>

namespace tests {

void die(const char *msg) {
  std::cout << msg << std::endl;
  exit(1);
}

namespace selector {

// tests whether the poll properly returns when a pipe is written to
void test_pipe() {
  using namespace async::selector;

  // create poll
  Poll p;

  // open a pipe
  int fds[2];
  if (pipe(fds) == -1) {
    die("pipe didn't work");
  }

  // register readability event with read-end of pipe
  Token tok = { 1 };
  p.register_event(fds[0], tok, Interest::Readable);

  // spawn a thread that waits and writes to pipe
  std::thread t([&] {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(1000ms);

      std::cout << "writing hello" << std::endl;
      write(fds[1], "hello", 5);

      std::this_thread::sleep_for(1000ms);
  });

  // poll, waiting for write
  Events<1> e;
  p.poll(e, {});
  char buf[6] = { '\0' };
  read(fds[0], buf, 5);
  std::cout << "read out: " << buf << std::endl;

  assert(e.events[0].get_token() == tok);

  t.join();
}

void test_socket() {
}

}; // ns selector

namespace executor {

// TODO(3/25): make this a working example, also the next thing to do is integrate sources
// with the IODriver so that the runtime is actually aware of the sources of IO that the
// tasks depend on
class OneSecondTimer : public async::future::Future<void> {
public:
  OneSecondTimer() : _timer_finished(false) {
    // spawn a thread that sleeps for a second and wakes this future up. sets the boolean
    // flag to true.
    std::thread t([this]() {
        std::this_thread::sleep_for(std::chrono::nanoseconds(1000000000));

        std::lock_guard<std::mutex> guard(_m);
        _timer_finished = true;
        if (_waker != nullptr) {
          _waker();
        }
    });
  }

  OneSecondTimer(OneSecondTimer &&other) {
    std::lock_guard<std::mutex> guard(other._m);
    _timer_finished = other._timer_finished;
    _waker = other._waker;
  }

  async::future::Result<void> poll(async::future::Context ctx) {
    std::lock_guard<std::mutex> guard(_m);
    if (_timer_finished) {
      return async::future::Ready<void>();
    } else {
      _waker = ctx.waker;

      return async::future::Pending{};
    }
  }

private:
  std::mutex _m;
  bool _timer_finished;
  async::future::WakerFn _waker;
};

void test_basic() {
  using namespace async;

  async::executor::LocalExecutor exec;
  exec.block_on(OneSecondTimer());
}

};

}; // ns tests

int main() {
  tests::executor::test_basic();
}
