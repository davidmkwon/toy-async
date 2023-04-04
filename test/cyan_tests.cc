#include "cyan.h"

#include <iostream>
#include <thread>
#include <unistd.h>

void die(const char *msg) {
  std::cout << msg << std::endl;
  exit(1);
}

int main() {
  cyan::Executor exec;

  // create a pipe to read/write
  int fds[2];
  if (pipe(fds) == -1) {
    die("pipe didn't work");
  }

  // create handler to print out message written to pipe
  int read_fd = fds[0];
  cyan::Handler h = [=]() {
    char buf[6] = { '\0' };
    read(read_fd, buf, 5);
    std::cout << "read out: " << buf << std::endl;
  };
  exec.register_handler(read_fd, cyan::Interest::Readable, h);

  // spawn a thread that waits and writes to pipe
  int write_fd = fds[1];
  std::thread t([=] {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(1000ms);

      write(write_fd, "hello", 5);

      std::this_thread::sleep_for(1000ms);

      write(write_fd, "world", 5);

      std::this_thread::sleep_for(1000ms);
  });

  exec.run();
}
