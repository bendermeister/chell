#include "../../chell.h"

#define DEBUG_FLAGS LIST("-Wall -Wextra main.c")

int main(void) {
  command("clang",
          COMBINE(DEBUG_FLAGS, LIST("-fsanitize=address,leak"),
                  LIST("-fno-omit-frame-pointer")),
          LIST("main.c", "lib.h"));

  return 0;
}
