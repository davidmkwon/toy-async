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

  // create two pipes to read/write
  int fds1[2];
  int fds2[2];
  if (pipe(fds1) == -1) {
    die("pipe didn't work");
  }
  if (pipe(fds2) == -1) {
    die("pipe didn't work");
  }

  // create handlers to print out message written to pipes
  int read_fd1 = fds1[0];
  cyan::Handler h1 = [=]() {
    char buf[6] = { '\0' };
    read(read_fd1, buf, 5);
    std::cout << "read from pipe1: " << buf << std::endl;
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1000ms);
  };
  exec.register_handler(read_fd1, cyan::Interest::Readable, h1);

  int read_fd2 = fds2[0];
  cyan::Handler h2 = [=]() {
    char buf[6] = { '\0' };
    read(read_fd2, buf, 5);
    std::cout << "read from pipe2: " << buf << std::endl;
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1000ms);
  };
  exec.register_handler(read_fd2, cyan::Interest::Readable, h2);

  // spawn a thread that waits and writes to the two pipes
  int write_fd1 = fds1[1];
  int write_fd2 = fds2[1];
  std::thread t([=] {
      using namespace std::chrono_literals;
      while (true) {
        std::this_thread::sleep_for(500ms);
        write(write_fd1, "hello", 5);
        write(write_fd2, "world", 5);
      }
  });

  exec.run();
}
