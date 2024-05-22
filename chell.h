#ifndef CHELL_H
#define CHELL_H

#ifndef CHELL_SRC
#define CHELL_SRC "chell.c"
#endif // CHEL_SRC

#ifndef CHELL_REBUILT_ARGS
#define CHELL_REBUILT_ARGS "-g -Wall -Wextra -std=c17"
#endif // CHELL_REBUILT_ARGS

#ifndef CHELL_CC
#define CHELL_CC "clang"
#endif // CHEL_CC

#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#ifndef CHELL_FILE
#define CHELL_FILE "chell.json"
#endif // CHELL_FILE

#ifndef THREADS
#define THREADS 1
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

typedef enum { DB_RUNNING, DB_COMPLETE } db_entry_state_t;

typedef struct db_entry_t db_entry_t;
struct db_entry_t {
  char *key;
  time_t value;
  db_entry_t *next;
  db_entry_state_t state;
};

struct {
  mtx_t mtx;
  cnd_t cnd;
  db_entry_t *db;
} db = {0};

static void list_destroy(list_t *l) {
  if (!l) {
    return;
  }
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

db_entry_t **db_find(db_entry_t **e, char *key);

static int db_fetch_should_run(char *key, char *dep) {
  mtx_lock(&db.mtx);
  db_entry_t **ee = db_find(&db.db, key);
  db_entry_t *e = *ee;

  if (!e) {
    int len = 0;
    int cap = 0;
    char *buf = NULL;
    scat(&buf, &len, &cap, key);
    e = xmalloc(sizeof(*e));
    e->key = buf;
    e->next = NULL;
    e->state = DB_RUNNING;
    *ee = e;
    mtx_unlock(&db.mtx);
    return 1;
  }

  while (e->state == DB_RUNNING) {
    cnd_wait(&db.cnd, &db.mtx);
  }

  struct stat s;
  int err = stat(dep, &s);
  if (err || s.st_mtime > e->value) {
    e->state = DB_RUNNING;
    mtx_unlock(&db.mtx);
    return 1;
  }

  mtx_unlock(&db.mtx);
  return 0;
}

static void db_update(char *key) {
  mtx_lock(&db.mtx);
  db_entry_t *e = *db_find(&db.db, key);
  e->state = DB_COMPLETE;
  e->value = time(NULL);
  mtx_unlock(&db.mtx);
  cnd_broadcast(&db.cnd);
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

  int should = deps == NULL;
  for (int i = 0; !should && i < deps->len; ++i) {
    if (db_fetch_should_run(buf, deps->buf[i])) {
      should = 1;
    }
  }

  if (should) {
    LOG("running `%s`", buf);
    int err = system(buf);
    if (err) {
      ERROR("command '%s' exited with non zero exit code", buf);
    }
    if (deps != NULL) {
      db_update(buf);
    }
  }

  list_destroy(deps);
  list_destroy(args);
  free(buf);
}
//=================================ACTOR=======================================

db_entry_t **db_find(db_entry_t **e, char *key) {
  db_entry_t *ee = *e;
  if (!ee) {
    return e;
  }
  if (strlen(ee->key) == strlen(key) && strcmp(ee->key, key) == 0) {
    return e;
  }
  return db_find(&ee->next, key);
}

void db_deinit_write(db_entry_t *e, FILE *f) {
  if (!e) {
    return;
  }
  fprintf(f, "%s %ld\n", e->key, e->value);
  free(e->key);
  db_deinit_write(e->next, f);
  free(e);
}

void db_deinit(void) {
  FILE *f = fopen(CHELL_FILE, "w");
  if (!f) {
    ERROR("could not deinitilize chell: %s", strerror(errno));
  }
  db_deinit_write(db.db, f);
  fclose(f);
}

char *read_file(char *path) {
  ssize_t cap = 1024;
  ssize_t len = 0;
  char *buffer = xmalloc(cap);
  FILE *f = fopen(path, "r+");
  if (!f) {
    ERROR("could not read file %s: %s", path, strerror(errno));
  }

  while (!ferror(f) && !feof(f)) {
    ssize_t nread = fread(buffer + len, 1, cap - len, f);
    if (nread < 0) {
      ERROR("could not read file %s: %s", path, strerror(errno));
    }
    len += nread;
    cap *= 2;
    buffer = xrealloc(buffer, cap);
  }
  buffer[len] = 0;
  fclose(f);
  return buffer;
}

void db_init(void) {
  int err;
  err = mtx_init(&db.mtx, mtx_plain);
  if (err != thrd_success) {
    ERROR("could not initilize actor: %s", strerror(errno));
  }

  err = cnd_init(&db.cnd);
  if (err != thrd_success) {
    ERROR("could not initilize actor: %s", strerror(errno));
  }

  db_entry_t **curr = &db.db;

  char *f = read_file(CHELL_FILE);
  char *line = f;
  while (line) {
    char *end = strchr(line, '\n');
    if (!end) {
      break;
    }
    *end = 0;
    db_entry_t *e = xmalloc(sizeof(*e));
    *e = (db_entry_t){
        .state = DB_COMPLETE,
        .key = line,
    };
    (*curr) = e;
    curr = &e->next;
    line = end;
    if (end) {
      line += 1;
    }
  }

  db_entry_t *iter = db.db;
  for (; iter; iter = iter->next) {
    int len = 0;
    int cap = 0;
    char *buf = NULL;
    scat(&buf, &len, &cap, iter->key);
    char *s = strrchr(buf, ' ');
    *s = 0;
    s += 1;
    iter->value = atoll(s);
    iter->key = buf;
  }
  free(f);
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
  atomic_fetch_sub_explicit(&w->counter, 1, memory_order_acq_rel);
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
      free(work);
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
    wg_done(work->wg);
    free(work);
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
  wg_add(wg, 1);
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
//=================================REBUILT=====================================
static void chell_rebuilt_yourself(int argc, char **argv) {
  struct stat d;
  struct stat s;
  stat(argv[0], &d);
  stat(CHELL_SRC, &s);
  if (d.st_mtime > s.st_mtime) {
    return;
  }

  LOG("rebuilding chell");
  char *buf = NULL;
  int len = 0;
  int cap = 0;

  scat(&buf, &len, &cap, CHELL_CC);
  scat(&buf, &len, &cap, " ");
  scat(&buf, &len, &cap, CHELL_REBUILT_ARGS);
  scat(&buf, &len, &cap, " ");
  scat(&buf, &len, &cap, CHELL_SRC);
  scat(&buf, &len, &cap, " ");
  scat(&buf, &len, &cap, " -o ");
  scat(&buf, &len, &cap, argv[0]);

  int err = system(buf);
  if (err) {
    ERROR("could not rebuilt chell");
  }
  err = execv(argv[0], argv);
  if (err) {
    ERROR("could not rerun chell");
  }
  free(buf);
  exit(0);
}

//=================================INIT========================================
static void chell_init(int argc, char *argv[argc]) {
  chell_rebuilt_yourself(argc, argv);
  db_init();
  int err;
  pool.alive = 1;
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
    if (err != 0) {
      ERROR("could not start worker thread: %s", strerror(errno));
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
  db_deinit();
}

#endif // CHELL_H
