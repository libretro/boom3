/*
 * vk_thread: the render thread.  The engine hands it a frame's command
 * list after the front end is done, draws nothing itself until the
 * join, and goes on with the frame's other work - the sound mix - while
 * the commands are recorded and submitted.  One job at a time: handing
 * over a new one waits for the last.
 */
#include <stddef.h>

#include "vk_thread.h"

#ifdef HAVE_THREADS
#include <rthreads/rthreads.h>

typedef struct vkt_state
{
   sthread_t *thread;
   slock_t *lock;
   scond_t *wake;             /* a job arrived, or the thread is to stop */
   scond_t *done;             /* the job finished */
   void (*run)(void *job);
   void *job;
   int busy;
   int stop;
} vkt_state_t;

static vkt_state_t vkt;

static void vkt_main(void *data)
{
   vkt_state_t *t = (vkt_state_t *)data;
   slock_lock(t->lock);
   for (;;)
   {
      void *job;
      while (!t->job && !t->stop)
         scond_wait(t->wake, t->lock);
      if (t->stop && !t->job)
         break;
      job    = t->job;
      t->job = NULL;
      slock_unlock(t->lock);
      t->run(job);
      slock_lock(t->lock);
      t->busy = 0;
      scond_signal(t->done);
   }
   slock_unlock(t->lock);
}

int vkt_start(void (*run)(void *job))
{
   if (vkt.thread)
      return 1;
   vkt.run  = run;
   vkt.job  = NULL;
   vkt.busy = 0;
   vkt.stop = 0;
   vkt.lock = slock_new();
   vkt.wake = scond_new();
   vkt.done = scond_new();
   if (vkt.lock && vkt.wake && vkt.done)
      vkt.thread = sthread_create(vkt_main, &vkt);
   if (!vkt.thread)
   {
      vkt_stop();
      return 0;
   }
   return 1;
}

void vkt_join(void)
{
   if (!vkt.thread)
      return;
   slock_lock(vkt.lock);
   while (vkt.busy)
      scond_wait(vkt.done, vkt.lock);
   slock_unlock(vkt.lock);
}

void vkt_submit(void *job)
{
   if (!vkt.thread)
   {
      vkt.run(job);
      return;
   }
   vkt_join();
   slock_lock(vkt.lock);
   vkt.job  = job;
   vkt.busy = 1;
   scond_signal(vkt.wake);
   slock_unlock(vkt.lock);
}

void vkt_stop(void)
{
   if (vkt.thread)
   {
      vkt_join();
      slock_lock(vkt.lock);
      vkt.stop = 1;
      scond_signal(vkt.wake);
      slock_unlock(vkt.lock);
      sthread_join(vkt.thread);
      vkt.thread = NULL;
   }
   if (vkt.done)
      scond_free(vkt.done);
   if (vkt.wake)
      scond_free(vkt.wake);
   if (vkt.lock)
      slock_free(vkt.lock);
   vkt.done = vkt.wake = NULL;
   vkt.lock = NULL;
}

int vkt_running(void)
{
   return vkt.thread != NULL;
}

static slock_t *vktLock;

int vkt_lock_init(void)
{
   if (!vktLock)
      vktLock = slock_new();
   return vktLock != NULL;
}

void vkt_lock_free(void)
{
   if (vktLock)
      slock_free(vktLock);
   vktLock = NULL;
}

void vkt_lock(void)
{
   if (vktLock)
      slock_lock(vktLock);
}

void vkt_unlock(void)
{
   if (vktLock)
      slock_unlock(vktLock);
}

#else

static void (*vktRun)(void *job);

int vkt_start(void (*run)(void *job))
{
   vktRun = run;
   return 0;
}

void vkt_join(void) { }

void vkt_submit(void *job)
{
   if (vktRun)
      vktRun(job);
}

void vkt_stop(void) { }

int vkt_running(void)
{
   return 0;
}

int vkt_lock_init(void)
{
   return 1;
}

void vkt_lock_free(void) { }
void vkt_lock(void) { }
void vkt_unlock(void) { }

#endif
