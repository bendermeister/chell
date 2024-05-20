#include "../../chell.h"

int main(void) {
  command("clang", LIST("-Wall -Wextra main.c"), LIST("main.c"));
  return 0;
}
