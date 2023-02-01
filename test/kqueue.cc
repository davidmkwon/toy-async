#include "async.h"

#include <iostream>
#include <thread>
#include <chrono>

#include <unistd.h>
#include <stdio.h>
#include <sys/event.h>

void die(const char *msg) {
  std::cout << msg << std::endl;
  exit(1);
}

void kqueue_pipe_test() {
  // create kqueue
  int kq;
  if ((kq = kqueue()) == -1) {
    die("kqueue didn't work");
  }

  // open a pipe
  int fds[2];
  if (pipe(fds) == -1) {
    die("pipe didn't work");
  }

  // make a kevent to listen for readability of pipe
  struct kevent ch_event;
  EV_SET(&ch_event, fds[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
  int ret;
  if ((ret = kevent(kq, &ch_event, 1, NULL, 0, NULL)) == -1) {
    die("couldn't add event to kq");
  }

  // spawn a thread that sleeps for a bit and then writes to the pipe
  std::thread t([&] {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(1000ms);

      write(fds[1], "hello", 5);

      std::this_thread::sleep_for(1000ms);
  });

  // block until pipe's readability is observed
  struct kevent tr_event;
  ret = kevent(kq, NULL, 0, &tr_event, 1, NULL);
  if (ret == -1) {
    die("couldn't observe triggered event");
  } else {
    char buf[6] = { '\0' };
    read(fds[0], buf, 5);
    std::cout << "read out: " << buf << std::endl;
  }

  t.join();
}

using namespace async::future;

class MyFuture : public Future<int> {
public:
  MyFuture() {}

  Result<int> poll(WakerFn wake) {
    wake();
    return Ready<int> { 3 };
  }
};

int main() {
}
