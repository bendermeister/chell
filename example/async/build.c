#include "../../chell.h"

#define FLAGS LIST("-Wall", "-Wextra", "-g")

void all(void) {
  wg_t wg = {0};

  async_command(&wg, "clang",
                COMBINE(FLAGS, LIST("main.c", "-c", "-o", "main.o")),
                LIST("main.c", "lib.h", "main.o"));

  async_command(&wg, "clang",
                COMBINE(FLAGS, LIST("lib.c", "-c", "-o", "lib.o")),
                LIST("lib.c", "lib.h", "lib.o"));

  wg_wait(&wg);

  command("clang", COMBINE(FLAGS, LIST("main.o", "lib.o", "-o", "a.out")),
          LIST("main.o", "lib.o"));
}

void clean(void) { command("rm", LIST("-f", "*.o", "a.out"), NULL); }

int main(int argc, char *argv[argc]) {
  chell_init();

  if (argc == 1 || strcmp(argv[1], "all") == 0) {
    all();
  } else if (strcmp(argv[1], "clean") == 0) {
    clean();
  }

  chell_deinit();

  return 0;
}
