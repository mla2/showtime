/*
 *  Wrappers for thread/mutex/conditional variables
 *  Copyright (C) 2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HTSTHREADS_H__
#define HTSTHREADS_H__

#include "config.h"

extern int get_system_concurrency(void);

#ifdef CONFIG_LIBPTHREAD

#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/**
 * Mutexes
 */
typedef pthread_mutex_t hts_mutex_t;

#define hts_mutex_init(m)            pthread_mutex_init((m), NULL)
#define hts_mutex_lock(m)            pthread_mutex_lock(m)
#define hts_mutex_unlock(m)          pthread_mutex_unlock(m)
#define hts_mutex_destroy(m)         pthread_mutex_destroy(m)
extern void hts_mutex_init_recursive(hts_mutex_t *m);


static inline void
hts_mutex_assert0(pthread_mutex_t *l, const char *file, int line)
{
  if(pthread_mutex_trylock(l) == EBUSY)
    return;

  fprintf(stderr, "Mutex not held at %s:%d\n", file, line);
  abort();
}

#define hts_mutex_assert(l) hts_mutex_assert0(l, __FILE__, __LINE__)

/**
 * Condition variables
 */
typedef pthread_cond_t hts_cond_t;
#define hts_cond_init(c, m)            pthread_cond_init((c), NULL)
#define hts_cond_signal(c)             pthread_cond_signal(c)
#define hts_cond_broadcast(c)          pthread_cond_broadcast(c)
#define hts_cond_wait(c, m)            pthread_cond_wait(c, m)
#define hts_cond_destroy(c)            pthread_cond_destroy(c)
extern int hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta);

/**
 * Threads
 */
// These are not really used on POSIX
#define THREAD_PRIO_LOW    0
#define THREAD_PRIO_NORMAL 0
#define THREAD_PRIO_HIGH   0


typedef pthread_t hts_thread_t;

extern void hts_thread_create_detached(const char *, void *(*)(void *), void *,
				       int);

extern void hts_thread_create_joinable(const char *, hts_thread_t *, 
				       void *(*)(void *), void *, int);

#define hts_thread_detach(t)                pthread_detach(*(t));

#define hts_thread_join(t)                  pthread_join(*(t), NULL)
#define hts_thread_current()                pthread_self()

#if !ENABLE_EMU_THREAD_SPECIFICS

typedef pthread_key_t hts_key_t;

#define hts_thread_key_create(k, f) pthread_key_create((pthread_key_t *)k, f)
#define hts_thread_key_delete(k)    pthread_key_delete(k)

#define hts_thread_set_specific(k, p) pthread_setspecific(k, p)
#define hts_thread_get_specific(k)    pthread_getspecific(k)

#endif


#elif CONFIG_LIBOGC


/**
 * libogc threads
 */

/**
 * Mutexes
 */

#include <ogc/mutex.h>
typedef mutex_t hts_mutex_t;

extern void hts_mutex_init(hts_mutex_t *m);
#define hts_mutex_lock(m)     LWP_MutexLock(*(m))
#define hts_mutex_unlock(m)   LWP_MutexUnlock(*(m))
#define hts_mutex_destroy(m)  LWP_MutexDestroy(*(m))
#define hts_mutex_assert(m)

/**
 * Condition variables
 */
#include <ogc/cond.h>
typedef cond_t hts_cond_t;

extern void hts_cond_init(hts_cond_t *c);
#define hts_cond_signal(c)             LWP_CondSignal(*(c))
#define hts_cond_broadcast(c)          LWP_CondBroadcast(*(c))
#define hts_cond_wait(c, m)            LWP_CondWait(*(c), *(m))
#define hts_cond_destroy(c)            LWP_CondDestroy(*(c))
extern int hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta);

/**
 * Threads
 */
#include <ogc/lwp.h>

typedef lwp_t hts_thread_t;

extern void hts_thread_create_detached(const char *, void *(*)(void *), void *);

extern void hts_thread_create_joinable(const char *, hts_thread_t *p, 
				       void *(*)(void *), void *);

#define hts_thread_join(t)       LWP_JoinThread(*(t), NULL)

#define hts_thread_detach(t)

#define hts_thread_current()     LWP_GetSelf()

#elif CONFIG_PSL1GHT

// #define PS3_LW_PRIMITIVES


#include <sys/thread.h>

/**
 * Mutexes
 */
// #define PS3_DEBUG_MUTEX


#ifndef PS3_DEBUG_MUTEX

#ifdef PS3_LW_PRIMITIVES
typedef sys_lwmutex_t hts_mutex_t;
#else
typedef sys_mutex_t hts_mutex_t;
#endif

extern void hts_mutex_init(hts_mutex_t *m);
extern void hts_mutex_init_recursive(hts_mutex_t *m);
extern void hts_mutex_lock(hts_mutex_t *m);
extern void hts_mutex_unlock(hts_mutex_t *m);
extern void hts_mutex_destroy(hts_mutex_t *m);
#define hts_mutex_assert(m)

#else

typedef struct {
#ifdef PS3_LW_PRIMITIVES
  sys_lwmutex_t mtx;
#else
  sys_mutex_t mtx;
#endif
  int dbg;
  int logptr;
  struct {
    enum {
      MTX_CREATE = 1,
      MTX_LOCK,
      MTX_UNLOCK,
      MTX_DESTROY,
      MTX_SAMPLE,
    } op;
    const char *file;
    int line;
    int thread;
#ifdef PS3_LW_PRIMITIVES
    uint64_t lock_var;
#endif
  } log[16];
} hts_mutex_t;

extern void hts_mutex_initx(hts_mutex_t *m, const char *file, int line, int dbg);
extern void hts_mutex_initx_recursive(hts_mutex_t *m, const char *file, int line);

extern void hts_mutex_lockx(hts_mutex_t *m, const char *file, int line);
extern void hts_mutex_unlockx(hts_mutex_t *m, const char *file, int line);
extern void hts_mutex_destroyx(hts_mutex_t *m, const char *file, int line);

#define hts_mutex_init(m) hts_mutex_initx(m, __FILE__, __LINE__, 0)
#define hts_mutex_init_recursive(m) hts_mutex_initx_recursive(m, __FILE__, __LINE__)
#define hts_mutex_dbg(m) hts_mutex_initx(m, __FILE__, __LINE__, 1)
#define hts_mutex_lock(m) hts_mutex_lockx(m, __FILE__, __LINE__)
#define hts_mutex_unlock(m) hts_mutex_unlockx(m, __FILE__, __LINE__)
#define hts_mutex_destroy(m) hts_mutex_destroyx(m, __FILE__, __LINE__)

#ifdef PS3_LW_PRIMITIVES
#define hts_mutex_assert(m) assert((m)->mtx.lock_var != 0xffffffff00000000LL);
#else
#define hts_mutex_assert(m)
#endif

#endif

/**
 * Condition variables
 */
// #define PS3_DEBUG_COND
#ifdef PS3_LW_PRIMITIVES
typedef sys_lwcond_t hts_cond_t;
#else
typedef sys_cond_t hts_cond_t;
#endif

#ifndef PS3_DEBUG_COND

extern void hts_cond_init(hts_cond_t *c, hts_mutex_t *m);
extern void hts_cond_destroy(hts_cond_t *c);
extern void hts_cond_signal(hts_cond_t *c);
extern void hts_cond_broadcast(hts_cond_t *c);
#define hts_cond_wait(c, m) hts_cond_wait_timeout(c, m, 0)
extern int hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delay);


#else // condvar debugging

extern void hts_cond_initx(hts_cond_t *c, hts_mutex_t *m, const char *file, int line);
extern void hts_cond_destroyx(hts_cond_t *c, const char *file, int line);
extern void hts_cond_signalx(hts_cond_t *c, const char *file, int line);
extern void hts_cond_broadcastx(hts_cond_t *c, const char *file, int line);
extern void hts_cond_waitx(hts_cond_t *c, hts_mutex_t *m, const char *file, int line);
extern int hts_cond_wait_timeoutx(hts_cond_t *c, hts_mutex_t *m, int delay, const char *file, int line);

#define hts_cond_init(c, m) hts_cond_initx(c, m, __FILE__, __LINE__)
#define hts_cond_destroy(c) hts_cond_destroyx(c, __FILE__, __LINE__)
#define hts_cond_signal(c)     hts_cond_signalx(c, __FILE__, __LINE__)
#define hts_cond_broadcast(c)  hts_cond_broadcastx(c, __FILE__, __LINE__)
#define hts_cond_wait(c, m) hts_cond_waitx(c, m,  __FILE__, __LINE__)
#define hts_cond_wait_timeout(c, m, d) hts_cond_wait_timeoutx(c, m, d, __FILE__, __LINE__) 

#endif


/**
 * Threads
 */
typedef sys_ppu_thread_t hts_thread_t;

#define THREAD_PRIO_LOW    3000
#define THREAD_PRIO_NORMAL 2999
#define THREAD_PRIO_HIGH   3

extern void hts_thread_create_detached(const char *, void *(*)(void *), void *,
				       int prio);

extern void hts_thread_create_joinable(const char *, hts_thread_t *p,
				       void *(*)(void *), void *,
				       int prio);

extern void hts_thread_join(hts_thread_t *id);

#define hts_thread_detach(t) sys_ppu_thread_detach(*(t))

extern hts_thread_t hts_thread_current(void);


#else

#error No threading support

#endif



#if ENABLE_EMU_THREAD_SPECIFICS

typedef int hts_key_t;

extern int hts_thread_key_create(unsigned int *k, void (*destrutor)(void *));
extern int hts_thread_key_delete(unsigned int k);
extern int hts_thread_set_specific(unsigned int k, void *p);
extern void *hts_thread_get_specific(unsigned int k);
extern void hts_thread_exit_specific(void);

#endif

#endif /* HTSTHREADS_H__ */
