/*
 * vk_thread: the render thread.  The engine hands it a frame's command
 * list after the front end is done, draws nothing itself until the
 * join, and goes on with the frame's other work - the sound mix - while
 * the commands are recorded and submitted.  One job at a time: handing
 * over a new one waits for the last.
 *
 * No lock: the job is published through an atomic the two threads hand
 * back and forth, and each parks on an eventcount while the other has
 * it.  The render thread owns the backend from the submit to the join;
 * what the main thread may touch in between is said at each table
 * (vk_buffer.c, vk_image.c), and vkt_busy() is how a path that may not
 * asks.
 */
#include <stddef.h>

#include "vk_thread.h"

#ifdef HAVE_THREADS
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

typedef struct vkt_state
{
   sthread_t *thread;
   uintptr_t threadId;
   retro_eventcount_t wake;      /* a job arrived, or the thread is to stop */
   retro_eventcount_t done;      /* the job finished */
   void (*run)(void *job);
   void *job;                    /* written before busy is released, read after it is acquired */
   retro_atomic_int_t busy;      /* 1 from the submit to the end of the job */
   retro_atomic_int_t stop;
} vkt_state_t;

static vkt_state_t vkt;

static void vkt_main(void *data)
{
   vkt_state_t *t = (vkt_state_t *)data;
   for (;;)
   {
      void *job;
      for (;;)
      {
         int key;
         if (retro_atomic_load_acquire_int(&t->busy) || retro_atomic_load_acquire_int(&t->stop))
            break;
         key = retro_eventcount_prepare_wait(&t->wake);
         if (retro_atomic_load_acquire_int(&t->busy) || retro_atomic_load_acquire_int(&t->stop))
         {
            retro_eventcount_cancel_wait(&t->wake);
            break;
         }
         retro_eventcount_commit_wait(&t->wake, key);
      }
      if (!retro_atomic_load_acquire_int(&t->busy))
         break;
      job    = t->job;
      t->job = NULL;
      t->run(job);
      retro_atomic_store_release_int(&t->busy, 0);
      retro_eventcount_notify(&t->done);
   }
}

int vkt_start(void (*run)(void *job))
{
   if (vkt.thread)
      return 1;
   vkt.run  = run;
   vkt.job  = NULL;
   retro_atomic_int_init(&vkt.busy, 0);
   retro_atomic_int_init(&vkt.stop, 0);
   if (!retro_eventcount_init(&vkt.wake))
      return 0;
   if (!retro_eventcount_init(&vkt.done))
   {
      retro_eventcount_free(&vkt.wake);
      return 0;
   }
   vkt.thread = sthread_create(vkt_main, &vkt);
   if (!vkt.thread)
   {
      retro_eventcount_free(&vkt.wake);
      retro_eventcount_free(&vkt.done);
      return 0;
   }
   vkt.threadId = sthread_get_thread_id(vkt.thread);
   return 1;
}

int vkt_on_render_thread(void)
{
   return vkt.thread != NULL && sthread_get_current_thread_id() == vkt.threadId;
}

/* the frame's completion: the main thread parks until the render thread
 * has recorded and submitted the frame, which it must have before the
 * frontend is handed the image */
void vkt_join(void)
{
   if (!vkt.thread)
      return;
   for (;;)
   {
      int key;
      if (!retro_atomic_load_acquire_int(&vkt.busy))
         return;
      key = retro_eventcount_prepare_wait(&vkt.done);
      if (!retro_atomic_load_acquire_int(&vkt.busy))
      {
         retro_eventcount_cancel_wait(&vkt.done);
         return;
      }
      retro_eventcount_commit_wait(&vkt.done, key);
   }
}

void vkt_submit(void *job)
{
   if (!vkt.thread)
   {
      vkt.run(job);
      return;
   }
   vkt_join();
   vkt.job = job;
   retro_atomic_store_release_int(&vkt.busy, 1);
   retro_eventcount_notify(&vkt.wake);
}

void vkt_stop(void)
{
   if (vkt.thread)
   {
      vkt_join();
      retro_atomic_store_release_int(&vkt.stop, 1);
      retro_eventcount_notify(&vkt.wake);
      sthread_join(vkt.thread);
      vkt.thread = NULL;
      retro_eventcount_free(&vkt.wake);
      retro_eventcount_free(&vkt.done);
      retro_atomic_store_release_int(&vkt.stop, 0);
   }
}

int vkt_running(void)
{
   return vkt.thread != NULL;
}

int vkt_busy(void)
{
   return vkt.thread != NULL && retro_atomic_load_acquire_int(&vkt.busy) != 0;
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

int vkt_busy(void)
{
   return 0;
}

int vkt_on_render_thread(void)
{
   return 0;
}

#endif
