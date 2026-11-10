/*
 * The render thread: one job at a time, run on a thread of its own when
 * the build has threads, in place when it has not.
 */
#ifndef VK_THREAD_H
#define VK_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* starts the thread that runs jobs through run(); 0 when there is no
 * thread, in which case vkt_submit runs the job in place */
int  vkt_start(void (*run)(void *job));
/* hands over a job, after the last one is done; the handoff is
 * lock-free, the wait for the last one a park on an eventcount */
void vkt_submit(void *job);
/* waits for the current job */
void vkt_join(void);
void vkt_stop(void);
int  vkt_running(void);

/* whether a job is on the thread: from the submit to the join the render
 * thread owns the backend's frame state, and the uploads the main thread
 * queues meanwhile are the next frame's */
int  vkt_busy(void);
/* whether the caller is the render thread: what it queues or frees is
 * its own frame's, kept apart from the main thread's */
int  vkt_on_render_thread(void);

#ifdef __cplusplus
}
#endif

#endif
