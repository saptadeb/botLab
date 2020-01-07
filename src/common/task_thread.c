#include <pthread.h>
#include <stdlib.h>

#include "zarray.h"
#include "task_thread.h"

typedef struct _task _task_t;
struct _task {
    void *arg;
    void (*f)(void * arg);

    pthread_cond_t cond; // signal when job is done
    pthread_mutex_t mutex;
};


struct task_thread
{
    pthread_mutex_t  mutex; // lock to access * tasks list
    pthread_cond_t  cond; // signal to notify when new element appears in *tasks
    pthread_t  thread;
    int running;

    zarray_t *tasks;
};



static void *
_thread_run(void *data)
{
    task_thread_t *tt = data;

    while (tt->running) {
        _task_t *task = NULL;
        pthread_mutex_lock (&tt->mutex);
        if (0==zarray_size (tt->tasks)) {
            pthread_cond_wait (&tt->cond, &tt->mutex);
            // make sure thread is still running after the wait()
            if(!tt->running) {
                pthread_mutex_unlock (&tt->mutex);
                break;
            }
        }
        else {
            // run the next pending task (index 0)
            zarray_get (tt->tasks, 0, &task);
            zarray_remove_index (tt->tasks, 0, 0);
        }
        pthread_mutex_unlock (&tt->mutex);

        if (task != NULL) { // protect against spurious wakeups
            task->f (task->arg);

            pthread_mutex_lock (&task->mutex);
            pthread_cond_signal (&task->cond);
            pthread_mutex_unlock (&task->mutex);
        }
    }
    pthread_exit (NULL);
}

task_thread_t *
task_thread_create(void)
{
    task_thread_t *tt = calloc (1, sizeof(*tt));
    tt->tasks = zarray_create (sizeof(_task_t *));

    pthread_mutex_init (&tt->mutex, NULL);
    pthread_cond_init (&tt->cond, NULL);
    tt->running = 1;

    pthread_attr_t tattr;
    pthread_attr_init (&tattr);
    pthread_attr_setdetachstate (&tattr, PTHREAD_CREATE_JOINABLE);

    pthread_create (&tt->thread, NULL, _thread_run, tt);

    pthread_attr_destroy (&tattr);
    return tt;
}

// scheduling a task after thread_destroy() happens results in undefined behavior
void
task_thread_schedule_blocking(task_thread_t *tt, void (*f)(void *arg), void *arg)
{
    // init
    _task_t task;
    _task_t *tp = &task;
    pthread_cond_init (&task.cond, NULL);
    pthread_mutex_init (&task.mutex, NULL);

    task.arg = arg;
    task.f = f;

    // lock early to ensure that we will be notified about the task being complete.
    pthread_mutex_lock (&task.mutex);

    // push the task, and notify
    pthread_mutex_lock (&tt->mutex);
    zarray_add (tt->tasks, &tp);
    pthread_cond_signal (&tt->cond);
    pthread_mutex_unlock (&tt->mutex);

    //wait for completion, and unlock
    pthread_cond_wait (&task.cond, &task.mutex);
    pthread_mutex_unlock (&task.mutex);

    // cleanup
    pthread_cond_destroy (&task.cond);
    pthread_mutex_destroy (&task.mutex);
}


static void
dump_task(void *ptr_ptr)
{
    // ensures any waiting tasks are flushed.
    // currently blocking calls to schedule_blocking() will handle the cleanup of the task_t objects
    _task_t *task = *(void**)ptr_ptr;
    pthread_mutex_lock (&task->mutex);
    pthread_cond_signal (&task->cond);
    pthread_mutex_unlock (&task->mutex);
}

void
task_thread_destroy (task_thread_t *tt)
{
    tt->running = 0;

    pthread_mutex_lock (&tt->mutex);
    pthread_cond_signal (&tt->cond);
    pthread_mutex_unlock (&tt->mutex);

    pthread_join (tt->thread, NULL);

    pthread_cond_destroy (&tt->cond);
    pthread_mutex_destroy (&tt->mutex);

    // cleanup remaining tasks

    zarray_map (tt->tasks, dump_task);
    zarray_destroy (tt->tasks);

    free (tt);
}

