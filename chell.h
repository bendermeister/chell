#ifndef CHELL_H
#define CHELL_H

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=================================UTIL========================================
#define ERROR(...)                                                             \
  do {                                                                         \
    fprintf(stderr, "[ERROR]: ");                                              \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
    exit(1);                                                                   \
  } while (0)

#define LOG(...)                                                               \
  do {                                                                         \
    fprintf(stdout, "[LOG]: ");                                                \
    fprintf(stdout, __VA_ARGS__);                                              \
    fprintf(stdout, "\n");                                                     \
  } while (0)

static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (!p) {
    ERROR("could not allocate %lu bytes: %s", size, strerror(errno));
  }
  return p;
}

static void *xrealloc(void *b, size_t size) {
  void *p = realloc(b, size);
  if (!p) {
    ERROR("could not reallocate %lu bytes: %s", size, strerror(errno));
  }
  return p;
}

//=================================STRBUILDER==================================

typedef struct str_builder_t str_builder_t;
struct str_builder_t {
  char *buf;
  int len;
  int cap;
};

void str_builder_catc(str_builder_t *b, char c) {
  if (b->len + 1 >= b->cap) {
    int new_cap = b->cap * 2 > 16 ? b->cap * 2 : 16;
    b->buf = xrealloc(b->buf, new_cap);
    memset(&b->buf[b->cap], 0, new_cap - b->cap);
    b->cap = new_cap;
  }
  b->buf[b->len++] = c;
}

void str_builder_cat(str_builder_t *b, char *s) {
  int len = strlen(s);
  if (b->len + len >= b->cap) {
    int new_cap = b->cap * 2 > b->cap + len ? b->cap * 2 : b->cap + len;
    b->buf = xrealloc(b->buf, new_cap);
    memset(&b->buf[b->cap], 0, new_cap - b->cap);
    b->cap = new_cap;
  }
  memcpy(&b->buf[b->len], s, len);
  b->len += len;
}

void str_builder_destroy(str_builder_t *b) { free(b->buf); }

//=================================LIST========================================

typedef struct list_t list_t;
struct list_t {
  char **buf;
  int len;
  int cap;
};

typedef struct cmd_t cmd_t;
struct cmd_t {
  char *prog;
  list_t args;
  list_t deps;
};

static void list_append(list_t *l, char *s) {
  if (l->len >= l->cap) {
    l->cap = l->cap * 2 > 8 ? l->cap * 2 : 8;
    l->buf = xrealloc(l->buf, l->cap * sizeof(*l->buf));
  }
  l->buf[l->len++] = s;
}

static list_t list_create(char **elements) {
  list_t list = {0};
  char **iter = elements;
  for (; *iter; ++iter) {
    list_append(&list, *iter);
  }
  return list;
}

static void run(cmd_t cmd) {
  str_builder_t b = {0};
  str_builder_cat(&b, cmd.prog);
  for (int i = 0; i < cmd.args.cap; ++i) {
    str_builder_catc(&b, ' ');
    str_builder_cat(&b, cmd.args.buf[i]);
  }
  LOG("running: %s", b.buf);
  str_builder_destroy(&b);
}

#endif // CHELL_H
