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
/* hands over a job, after the last one is done */
void vkt_submit(void *job);
/* waits for the current job */
void vkt_join(void);
void vkt_stop(void);
int  vkt_running(void);

/* the backend's lock: the image, buffer and upload tables the engine's
 * thread and the render thread both reach */
int  vkt_lock_init(void);
void vkt_lock_free(void);
void vkt_lock(void);
void vkt_unlock(void);

#ifdef __cplusplus
}
#endif

#endif
