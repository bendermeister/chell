#ifndef CHELL_H
#define CHELL_H

#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#ifndef CHELL_FILE
#define CHELL_FILE "chell.json"
#endif // CHELL_FILE

#ifndef THREADS
#define THREADS 8
#endif // THREADS

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

static void *xcalloc(size_t size) {
  void *p = calloc(1, size);
  if (!p) {
    ERROR("could not allocate %lu bytes: %s", size, strerror(errno));
  }
  return p;
}

#define MAX(A, B) ((A) > (B) ? (A) : (B));

static void *xrealloc(void *b, size_t size) {
  void *p = realloc(b, size);
  if (!p) {
    ERROR("could not reallocate %lu bytes: %s", size, strerror(errno));
  }
  return p;
}

static void scat(char **dest, int *len, int *cap, char *src) {
  int src_len = strlen(src);
  if (*len + src_len >= *cap) {
    int new_cap = MAX(*cap * 2, *cap + src_len + 1);
    *dest = xrealloc(*dest, new_cap);
    memset(&(*dest)[*cap], 0, new_cap - *cap);
    *cap = new_cap;
  }
  memcpy(&(*dest)[*len], src, src_len);
  *len += src_len;
}

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
  list_t *args;
  list_t *deps;
};

static void list_append(list_t *l, char *s) {
  if (l->len >= l->cap) {
    l->cap = l->cap * 2 > 8 ? l->cap * 2 : 8;
    l->buf = xrealloc(l->buf, l->cap * sizeof(*l->buf));
  }
  l->buf[l->len++] = s;
}

#define LIST(...) list_create((char *[]){__VA_ARGS__, NULL})

#define COMBINE(...) list_combine((list_t *[]){__VA_ARGS__, NULL})

static list_t *list_create(char **elements) {
  list_t *list = xcalloc(sizeof(*list));
  char **iter = elements;
  for (; *iter; ++iter) {
    list_append(list, *iter);
  }
  return list;
}

static void list_destroy(list_t *l) {
  free(l->buf);
  free(l);
}

static list_t *list_combine(list_t *lists[]) {
  list_t *l = xcalloc(sizeof(*l));
  list_t **iter = lists;

  for (; *iter; ++iter) {
    l->cap += (*iter)->len;
  }

  iter = lists;
  l->buf = xmalloc(l->cap * sizeof(*l->buf));
  for (; *iter; ++iter) {
    memcpy(&l->buf[l->len], (*iter)->buf, (*iter)->len * sizeof(*l->buf));
    l->len += (*iter)->len;
  }

  iter = lists;
  for (; *iter; ++iter) {
    list_destroy(*iter);
  }

  return l;
}

static void command(char *program, list_t *args, list_t *deps) {
  char *buf = NULL;
  int len = 0;
  int cap = 0;

  // TODO: check last time command was ran against modification time of deps

  scat(&buf, &len, &cap, program);

  for (int i = 0; i < args->len; ++i) {
    char *arg = args->buf[i];
    scat(&buf, &len, &cap, " ");
    scat(&buf, &len, &cap, arg);
  }

  LOG("running `%s`", buf);
  int err = system(buf);
  if (err) {
    ERROR("command '%s' exited with non zero exit code", buf);
  }

  list_destroy(args);
  list_destroy(deps);
  free(buf);
}

//=================================THREADING===================================
typedef struct wg_t wg_t;
struct wg_t {
  _Atomic int counter;
};

typedef struct work_t work_t;
struct work_t {
  char *program;
  list_t *args;
  list_t *deps;
  work_t *next;
  wg_t *wg;
};

struct {
  work_t *work;
  cnd_t cnd;
  mtx_t mtx;
  int alive;
  thrd_t tids[THREADS];
} pool = {0};

static void wg_done(wg_t *w) {
  atomic_fetch_sub_explicit(&w->counter, 1, memory_order_acquire);
}

static int worker_(void *a) {
  (void)a;
  work_t *work = NULL;

  while (1) {
    mtx_lock(&pool.mtx);
    while (pool.alive && !pool.work) {
      cnd_wait(&pool.cnd, &pool.mtx);
    }
    if (pool.work) {
      work = pool.work;
      pool.work = work->next;
      mtx_unlock(&pool.mtx);
      command(work->program, work->args, work->deps);
      wg_done(work->wg);
    } else {
      mtx_unlock(&pool.mtx);
      return 0;
    }
  }
}

void work_yield(void) {
  mtx_lock(&pool.mtx);
  work_t *work = pool.work;
  if (work) {
    pool.work = work->next;
  }
  mtx_unlock(&pool.mtx);

  if (work) {
    command(work->program, work->args, work->deps);
  }
}

void wg_add(wg_t *w, int a) {
  atomic_fetch_add_explicit(&w->counter, a, memory_order_acq_rel);
}

static void wg_wait(wg_t *w) {
  atomic_load_explicit(&w->counter, memory_order_acquire);
  while (w->counter) {
    work_yield();
    atomic_load_explicit(&w->counter, memory_order_acquire);
  }
}

static void async_command(wg_t *wg, char *program, list_t *args, list_t *deps) {
  work_t *work;
  size_t size = MAX(sizeof(*work), 64);
  work = xmalloc(size);
  *work = (work_t){
      .wg = wg,
      .deps = deps,
      .args = args,
      .program = program,
  };
  mtx_lock(&pool.mtx);
  work->next = pool.work;
  pool.work = work;
  mtx_unlock(&pool.mtx);
  cnd_signal(&pool.cnd);
}

//=================================INIT========================================
static void chell_init(void) {
  int err;
  err = cnd_init(&pool.cnd);
  if (err) {
    ERROR("could not initilize chell");
  }
  err = mtx_init(&pool.mtx, mtx_plain);
  if (err) {
    ERROR("could not initilize chell");
  }
  for (int i = 0; i < THREADS; ++i) {
    err = thrd_create(&pool.tids[i], worker_, NULL);
    if (err) {
      ERROR("could not start worker thread");
    }
  }
}

static void chell_deinit(void) {
  mtx_lock(&pool.mtx);
  pool.alive = 0;
  mtx_unlock(&pool.mtx);
  cnd_broadcast(&pool.cnd);
  for (int i = 0; i < THREADS; ++i) {
    thrd_join(pool.tids[i], NULL);
  }
}

#endif // CHELL_H
