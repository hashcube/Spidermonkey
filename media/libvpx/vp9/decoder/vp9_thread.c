// Copyright 2013 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// Multi-threaded worker
//
// Original source:
//  http://git.chromium.org/webm/libwebp.git
//  100644 blob eff8f2a8c20095aade3c292b0e9292dac6cb3587  src/utils/thread.c


#include <assert.h>
#include <string.h>   // for memset()
#include "./vp9_thread.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#if CONFIG_MULTITHREAD

#if defined(_WIN32)

//------------------------------------------------------------------------------
// simplistic pthread emulation layer

#include <process.h>  // NOLINT

// _beginthreadex requires __stdcall
#define THREADFN unsigned int __stdcall
#define THREAD_RETURN(val) (unsigned int)((DWORD_PTR)val)

static int pthread_create(pthread_t* const thread, const void* attr,
                          unsigned int (__stdcall *start)(void*), void* arg) {
  (void)attr;
  *thread = (pthread_t)_beginthreadex(NULL,   /* void *security */
                                      0,      /* unsigned stack_size */
                                      start,
                                      arg,
                                      0,      /* unsigned initflag */
                                      NULL);  /* unsigned *thrdaddr */
  if (*thread == NULL) return 1;
  SetThreadPriority(*thread, THREAD_PRIORITY_ABOVE_NORMAL);
  return 0;
}

static int pthread_join(pthread_t thread, void** value_ptr) {
  (void)value_ptr;
  return (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0 ||
          CloseHandle(thread) == 0);
}

// Mutex
static int pthread_mutex_init(pthread_mutex_t* const mutex, void* mutexattr) {
  (void)mutexattr;
  InitializeCriticalSection(mutex);
  return 0;
}

static int pthread_mutex_lock(pthread_mutex_t* const mutex) {
  EnterCriticalSection(mutex);
  return 0;
}

static int pthread_mutex_unlock(pthread_mutex_t* const mutex) {
  LeaveCriticalSection(mutex);
  return 0;
}

static int pthread_mutex_destroy(pthread_mutex_t* const mutex) {
  DeleteCriticalSection(mutex);
  return 0;
}

// Condition
static int pthread_cond_destroy(pthread_cond_t* const condition) {
  int ok = 1;
  ok &= (CloseHandle(condition->waiting_sem_) != 0);
  ok &= (CloseHandle(condition->received_sem_) != 0);
  ok &= (CloseHandle(condition->signal_event_) != 0);
  return !ok;
}

static int pthread_cond_init(pthread_cond_t* const condition, void* cond_attr) {
  (void)cond_attr;
  condition->waiting_sem_ = CreateSemaphore(NULL, 0, 1, NULL);
  condition->received_sem_ = CreateSemaphore(NULL, 0, 1, NULL);
  condition->signal_event_ = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (condition->waiting_sem_ == NULL ||
      condition->received_sem_ == NULL ||
      condition->signal_event_ == NULL) {
    pthread_cond_destroy(condition);
    return 1;
  }
  return 0;
}

static int pthread_cond_signal(pthread_cond_t* const condition) {
  int ok = 1;
  if (WaitForSingleObject(condition->waiting_sem_, 0) == WAIT_OBJECT_0) {
    // a thread is waiting in pthread_cond_wait: allow it to be notified
    ok = SetEvent(condition->signal_event_);
    // wait until the event is consumed so the signaler cannot consume
    // the event via its own pthread_cond_wait.
    ok &= (WaitForSingleObject(condition->received_sem_, INFINITE) !=
           WAIT_OBJECT_0);
  }
  return !ok;
}

static int pthread_cond_wait(pthread_cond_t* const condition,
                             pthread_mutex_t* const mutex) {
  int ok;
  // note that there is a consumer available so the signal isn't dropped in
  // pthread_cond_signal
  if (!ReleaseSemaphore(condition->waiting_sem_, 1, NULL))
    return 1;
  // now unlock the mutex so pthread_cond_signal may be issued
  pthread_mutex_unlock(mutex);
  ok = (WaitForSingleObject(condition->signal_event_, INFINITE) ==
        WAIT_OBJECT_0);
  ok &= ReleaseSemaphore(condition->received_sem_, 1, NULL);
  pthread_mutex_lock(mutex);
  return !ok;
}

#else  // _WIN32
# define THREADFN void*
# define THREAD_RETURN(val) val
#endif

//------------------------------------------------------------------------------

static THREADFN thread_loop(void *ptr) {    // thread loop
  VP9Worker* const worker = (VP9Worker*)ptr;
  int done = 0;
  while (!done) {
    pthread_mutex_lock(&worker->mutex_);
    while (worker->status_ == OK) {   // wait in idling mode
      pthread_cond_wait(&worker->condition_, &worker->mutex_);
    }
    if (worker->status_ == WORK) {
      vp9_worker_execute(worker);
      worker->status_ = OK;
    } else if (worker->status_ == NOT_OK) {   // finish the worker
      done = 1;
    }
    // signal to the main thread that we're done (for Sync())
    pthread_cond_signal(&worker->condition_);
    pthread_mutex_unlock(&worker->mutex_);
  }
  return THREAD_RETURN(NULL);    // Thread is finished
}

// main thread state control
static void change_state(VP9Worker* const worker,
                         VP9WorkerStatus new_status) {
  // no-op when attempting to change state on a thread that didn't come up
  if (worker->status_ < OK) return;

  pthread_mutex_lock(&worker->mutex_);
  // wait for the worker to finish
  while (worker->status_ != OK) {
    pthread_cond_wait(&worker->condition_, &worker->mutex_);
  }
  // assign new status and release the working thread if needed
  if (new_status != OK) {
    worker->status_ = new_status;
    pthread_cond_signal(&worker->condition_);
  }
  pthread_mutex_unlock(&worker->mutex_);
}

#endif  // CONFIG_MULTITHREAD

//------------------------------------------------------------------------------

void vp9_worker_init(VP9Worker* const worker) {
  memset(worker, 0, sizeof(*worker));
  worker->status_ = NOT_OK;
}

int vp9_worker_sync(VP9Worker* const worker) {
#if CONFIG_MULTITHREAD
  change_state(worker, OK);
#endif
  assert(worker->status_ <= OK);
  return !worker->had_error;
}

int vp9_worker_reset(VP9Worker* const worker) {
  int ok = 1;
  worker->had_error = 0;
  if (worker->status_ < OK) {
#if CONFIG_MULTITHREAD
    if (pthread_mutex_init(&worker->mutex_, NULL) ||
        pthread_cond_init(&worker->condition_, NULL)) {
      return 0;
    }
    pthread_mutex_lock(&worker->mutex_);
    ok = !pthread_create(&worker->thread_, NULL, thread_loop, worker);
    if (ok) worker->status_ = OK;
    pthread_mutex_unlock(&worker->mutex_);
#else
    worker->status_ = OK;
#endif
  } else if (worker->status_ > OK) {
    ok = vp9_worker_sync(worker);
  }
  assert(!ok || (worker->status_ == OK));
  return ok;
}

void vp9_worker_execute(VP9Worker* const worker) {
  if (worker->hook != NULL) {
    worker->had_error |= !worker->hook(worker->data1, worker->data2);
  }
}

void vp9_worker_launch(VP9Worker* const worker) {
#if CONFIG_MULTITHREAD
  change_state(worker, WORK);
#else
  vp9_worker_execute(worker);
#endif
}

void vp9_worker_end(VP9Worker* const worker) {
  if (worker->status_ >= OK) {
#if CONFIG_MULTITHREAD
    change_state(worker, NOT_OK);
    pthread_join(worker->thread_, NULL);
    pthread_mutex_destroy(&worker->mutex_);
    pthread_cond_destroy(&worker->condition_);
#else
    worker->status_ = NOT_OK;
#endif
  }
  assert(worker->status_ == NOT_OK);
}

//------------------------------------------------------------------------------

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
