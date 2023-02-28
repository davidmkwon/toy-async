#include "async.h"

#include <iostream>
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
  p.register_event(fds[0], tok, Interests::Readable);

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

}; // ns tests

using namespace async::future;
class MyFuture1 : public Future<int> {
public:
  Result<int> poll(Context ctx) {
    return Pending{};
  }
};

class MyFuture2 : public Future<int> {
public:
  Result<int> poll(Context ctx) {
    MyFuture1 f;
    AWAIT(f, ctx);

    return Pending{};
  }
};

int main() {
  MyFuture1 f;
  async::task::Task t(f);
  tests::selector::test_pipe();
}
