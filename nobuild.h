#ifdef NOBUILD_H
#error "NOBUILD can only be included once."
#endif // NOBUILD_H
#define NOBUILD_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

//=================================DEBUG=FLAGS=================================
#ifndef GCC_DEBUG_FLAGS
#define GCC_DEBUG_FLAGS "-Wall -Wextra -g"
#endif // GCC_DEBUG_FLAGS

#ifndef MSVC_DEBUG_FLAGS
#define MSVC_DEBUG_FLAGS "" // TODO
#endif                      // MSVC_DEBUF_FLAGS

//=================================RELEASE=FLAGS===============================
#ifndef GCC_RELEASE_FLAGS
#define GCC_RELEASE_FLAGS "-O3 -march=native -mtune=native"
#endif // GCC_RELEASE_FLAGS

#ifndef MSVC_RELEASE_FLAGS
#define MSVC_RELEASE_FLAGS "" // TODO
#endif                        // MSVC_RELEASE_FLAGS

//=================================COMPLILER===================================
typedef enum {
  PART_SRC,
  PART_TARGET,
  PART_LINK,
  PART_FLAG,
  PART_SANITIZER,
} part_type_t;

typedef enum {
  SAN_UBSAN,
  SAN_ASAN,
  SAN_TSAN,
} sanitizer_t;

typedef struct target_t target_t;
struct target_t {
  struct slice_t *src;
  struct slice_t *target;
  struct slice_t *sanitizer;
  struct slice_t *link;
  struct slice_t *flag;
};

typedef struct part_t part_t;
struct part_t {
  union {
    char *src;
    char *link;
    char *flag;
    sanitizer_t sanitizer;
    target_t *target;
  } as;
  part_type_t type;
};

//=================================UTIL========================================
#define ERROR(...)                                                             \
  do {                                                                         \
    fprintf(stderr, "[ERROR]: ");                                              \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
    exit(1);                                                                   \
  } while (0)

// returns a valid pointer to a block of at least `size` bytes or aborts the
// program with an error message if no block could be allocated
static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (p == NULL) {
    ERROR("could not allocate %lu bytes: %s", size, strerror(errno));
  }
  return p;
}

// returns a valid pointer to a block of at least `size` bytes or aborts the
// program with an error message if no block could be allocated
static void *xrealloc(void *b, size_t size) {
  void *p = realloc(b, size);
  if (p == NULL) {
    ERROR("could not reallocate %lu bytes: %s", size, strerror(errno));
  }
  return p;
}

//=================================SLICE=======================================
typedef struct slice_t slice_t;
struct slice_t {
  void *buf;
  int len;
  int cap;
  int obj_size;
};

static slice_t *slice_create(int obj_size) {
  slice_t *s = xmalloc(sizeof(*s));
  *s = (slice_t){.obj_size = obj_size};
  return s;
}

static void *slice_more(slice_t *s) {
  if (s->len >= s->cap) {
    s->cap = s->cap * 2 > 8 ? s->cap * 2 : 8;
    s->buf = xrealloc(s->buf, s->cap * s->obj_size);
  }
  return (uint8_t *)s->buf + s->len++ * s->obj_size;
}

// returns `true` if the last modification time of `dest_path` is lower than the
// last modification time of `src_path`
static bool should_rebuild(char *dest_path, char *src_path) {
  struct stat dest;
  struct stat src;
  int err;

  err = stat(dest_path, &dest);
  if (err < 0) {
    ERROR("could not check last modification time of '%s' because: '%s'",
          dest_path, strerror(errno));
  }
  err = stat(src_path, &src);
  if (err < 0) {
    ERROR("could not check last modification time of '%s' because: '%s'",
          src_path, strerror(errno));
  }
  return dest.st_mtime < src.st_mtime;
}

//=================================PARTS=======================================
// returns a build_part for a src
static part_t *SRC(char *path) {
  part_t *p = xmalloc(sizeof(*p));
  *p = (part_t){
      .type = PART_SRC,
      .as.src = path,
  };
  return p;
}

// returns a build_part for a library that should be linked
static part_t *LINK(char *link) {
  part_t *p = xmalloc(sizeof(*p));
  *p = (part_t){
      .type = PART_LINK,
      .as.link = link,
  };
  return p;
}

static part_t *UBSAN(void) {
  part_t *p = xmalloc(sizeof(*p));
  *p = (part_t){
      .type = PART_SANITIZER,
      .as.sanitizer = SAN_UBSAN,
  };
  return p;
}

static part_t *TSAN(void) {
  part_t *p = xmalloc(sizeof(*p));
  *p = (part_t){
      .type = PART_SANITIZER,
      .as.sanitizer = SAN_TSAN,
  };
  return p;
}

static part_t *ASAN(void) {
  part_t *p = xmalloc(sizeof(*p));
  *p = (part_t){
      .type = PART_SANITIZER,
      .as.sanitizer = SAN_ASAN,
  };
  return p;
}

#define TARGET(...) TARGET_((part_t[]){__VA_ARGS__, NULL})

static part_t *TARGET_(part_t *parts) {
  part_t *p = xmalloc(sizeof(*p));
  target_t *t = xmalloc(sizeof(*t));
  *p = (part_t){
      .type = PART_TARGET,
      .as.target = t,
  };
  *t = (target_t){
      .sanitizer = slice_create(sizeof(sanitizer_t)),
      .src = slice_create(sizeof(char *)),
      .link = slice_create(sizeof(char *)),
      .target = slice_create(sizeof(target_t)),
      .flag = slice_create(sizeof(char *)),
  };

  part_t *iter = parts;
  while (iter != NULL) {
    switch (iter->type) {
    case PART_SRC:
      *(char **)slice_more(t->src) = iter->as.src;
      break;
    case PART_TARGET:
      // TODO
      break;
    case PART_LINK:
      *(char **)slice_more(t->link) = iter->as.link;
      break;
    case PART_FLAG:
      *(char **)slice_more(t->flag) = iter->as.flag;
      break;
    case PART_SANITIZER:
      *(sanitizer_t *)slice_more(t->sanitizer) = iter->as.sanitizer;
      break;
    }
  }

  return p;
}
