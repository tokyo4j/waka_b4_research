#include "types.h"
#include "stat.h"
#include "user.h"

void hello(void) {
  printf(2, "Hello, World!\n");
  return;
}

int main(int argc, char *argv[]) {
  hello();
  exit();
}
