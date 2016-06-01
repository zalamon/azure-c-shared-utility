// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "azure_c_shared_utility/threadapi.h"
#include "rtos.h"
#include "azure_c_shared_utility/iot_logging.h"

DEFINE_ENUM_STRINGS(THREADAPI_RESULT, THREADAPI_RESULT_VALUES);

#define MAX_THREADS 4
#define STACK_SIZE  0x4000

typedef struct _thread
{
    Thread*       thrd;
    osThreadId    id;
    Queue<int, 1> result;
} mbedThread;
static mbedThread threads[MAX_THREADS] = { 0 };

typedef struct _create_param
{
    THREAD_START_FUNC func;
    const void* arg;
    mbedThread *p_thread;
} create_param;

static void thread_wrapper(const void* createParamArg)
{
    const create_param* p = (const create_param*)createParamArg;
    p->p_thread->id = Thread::gettid();
    (*(p->func))((void*)p->arg);
    free((void*)p);
}

/* SRS_THREADAPI_99_002: [ This API creates a new thread with passed in THREAD_START_FUNC entry point and context arg ]*/
THREADAPI_RESULT ThreadAPI_Create(THREAD_HANDLE* threadHandle, THREAD_START_FUNC func, void* arg)
{
    THREADAPI_RESULT result;
    if ((threadHandle == NULL) ||
        (func == NULL))
    {
        /* SRS_THREADAPI_99_003: [ The API returns THREADAPI_INVALID_ARG if threadhandle is NULL ]*/
        /* SRS_THREADAPI_99_004: [ The API returns THREADAPI_INVALID_ARG if entry point function is NULL ]*/
        result = THREADAPI_INVALID_ARG;
        LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
    }
    else
    {
        size_t slot;
        for (slot = 0; slot < MAX_THREADS; slot++)
        {
            if (threads[slot].id == NULL)
                break;
        }

        if (slot < MAX_THREADS)
        {
            create_param* param = (create_param*)malloc(sizeof(create_param));
            if (param != NULL)
            {
                param->func = func;
                param->arg = arg;
                param->p_thread = threads + slot;
                threads[slot].thrd = new Thread(thread_wrapper, param, osPriorityNormal, STACK_SIZE);
                if (threads[slot].thrd == NULL)
                {
                    free(param);
                    /* SRS_THREADAPI_99_005: [ If allocation of thread handle fails due to out of memory `THREADAPI_NO_MEMORY` shall be returned ] */
                    result = THREADAPI_NO_MEMORY;
                    LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
                }
                else
                {
                    *threadHandle = (THREAD_HANDLE)(threads + slot);
                    /* SRS_THREADAPI_99_006: [ If the new thread is running `THREADAPI_OK` shall be returned ]*/
                    result = THREADAPI_OK;
                }
            }
            else
            {
                /* SRS_THREADAPI_99_005: [ If allocation of thread handle fails due to out of memory `THREADAPI_NO_MEMORY` shall be returned ] */
                result = THREADAPI_NO_MEMORY;
                LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
            }
        }
        else
        {
            /* SRS_THREADAPI_99_005: [ If allocation of thread handle fails due to out of memory `THREADAPI_NO_MEMORY` shall be returned ] */
            result = THREADAPI_NO_MEMORY;
            LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
        }
    }

    return result;
}

/* SRS_THREADAPI_99_008: [ This API will block until the thread identified by threadHandle has exited ] */
THREADAPI_RESULT ThreadAPI_Join(THREAD_HANDLE thr, int *res)
{
    THREADAPI_RESULT result = THREADAPI_OK;
    mbedThread* p = (mbedThread*)thr;
    if (p)
    {
        osEvent evt = p->result.get();
        if (evt.status == osEventMessage) {
            Thread* t = p->thrd;
            if (res)
            {
                *res = (int)evt.value.p;
            }
            (void)t->terminate();
            
            /* SRS_THREADAPI_99_012: [ On return the threadHandle will be freed and thus invalid ] */
            // NULL p->thrd and ->id to reclaim slot?
        }
        else
        {
            /* SRS_THREADAPI_99_010: [ If joining fails, the API shall return `THREADAPI_ERROR` ] */
            result = THREADAPI_ERROR;
            LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
        }
    }
    else
    {
        /* SRS_THREADAPI_99_011: [ This API on `NULL` handle passed returns `THREADAPI_INVALID_ARG` ] */
        result = THREADAPI_INVALID_ARG;
        LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
    }
    return result;
}

/* SRS_THREADAPI_99_013: [ This API ends the current thread  ] */
void ThreadAPI_Exit(int res)
{
    mbedThread* p;
    for (p = threads; p < &threads[MAX_THREADS]; p++)
    {
        if (p->id == Thread::gettid())
        {
            p->result.put((int*)res);
            break;
        }
    }
}

/* SRS_THREADAPI_99_015: [ This API sleeps for the passed in number of milliseconds ] */
void ThreadAPI_Sleep(unsigned int millisec)
{
    //
    // The timer on mbed seems to wrap around 65 seconds. Hmmm.
    // So we will do our waits in increments of 30 seconds.
    //
    const int thirtySeconds = 30000;
    int numberOfThirtySecondWaits = millisec / thirtySeconds;
    int remainderOfThirtySeconds = millisec % thirtySeconds;
    int i;
    for (i = 1; i <= numberOfThirtySecondWaits; i++)
    {
        Thread::wait(thirtySeconds);
    }
    Thread::wait(remainderOfThirtySeconds);
}

/* SRS_THREADAPI_99_016: [ This API returns an unsigned long identifier of the current thread ] */
unsigned long ThreadAPI_Self(void)
{
    return (unsigned long)Thread::gettid();
}
