// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#define _DEFAULT_SOURCE

#include "azure_c_shared_utility/threadapi.h"

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#ifdef TI_RTOS
#include <ti/sysbios/knl/Task.h>
#else
#include <unistd.h>
#endif

#include <pthread.h>
#include <time.h>
#include "azure_c_shared_utility/iot_logging.h"

DEFINE_ENUM_STRINGS(THREADAPI_RESULT, THREADAPI_RESULT_VALUES);

typedef struct THREAD_INSTANCE_TAG
{
    pthread_t Pthread_handle;
    THREAD_START_FUNC ThreadStartFunc;
    void* Arg;
} THREAD_INSTANCE;

static void* ThreadWrapper(void* threadInstanceArg)
{
    THREAD_INSTANCE* threadInstance = (THREAD_INSTANCE*)threadInstanceArg;
    int result = threadInstance->ThreadStartFunc(threadInstance->Arg);
    return (void*)(intptr_t)result;
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
        THREAD_INSTANCE* threadInstance = malloc(sizeof(THREAD_INSTANCE));
        if (threadInstance == NULL)
        {
            /* SRS_THREADAPI_99_005: [ If allocation of thread handle fails due to out of memory `THREADAPI_NO_MEMORY` shall be returned ] */
            result = THREADAPI_NO_MEMORY;
            LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
        }
        else
        {
            threadInstance->ThreadStartFunc = func;
            threadInstance->Arg = arg;
            int createResult = pthread_create(&threadInstance->Pthread_handle, NULL, ThreadWrapper, threadInstance);
            switch (createResult)
            {
            default:
                free(threadInstance);

                /* SRS_THREADAPI_99_007: [ If the new thread is not running `THREADAPI_ERROR` shall be returned ] */
                result = THREADAPI_ERROR;
                LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
                break;

            case 0:
                *threadHandle = threadInstance;
                /* SRS_THREADAPI_99_006: [ If the new thread is running `THREADAPI_OK` shall be returned ]*/
                result = THREADAPI_OK;
                break;

            case EAGAIN:
                free(threadInstance);

                /* SRS_THREADAPI_99_005: [ If allocation of thread handle fails due to out of memory `THREADAPI_NO_MEMORY` shall be returned ] */
                result = THREADAPI_NO_MEMORY;
                LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
                break;
            }
        }
    }

    return result;
}

/* SRS_THREADAPI_99_008: [ This API will block until the thread identified by threadHandle has exited ] */
THREADAPI_RESULT ThreadAPI_Join(THREAD_HANDLE threadHandle, int* res)
{
    THREADAPI_RESULT result;

    THREAD_INSTANCE* threadInstance = (THREAD_INSTANCE*)threadHandle;
    if (threadInstance == NULL)
    {
        /* SRS_THREADAPI_99_011: [ This API on `NULL` handle passed returns `THREADAPI_INVALID_ARG` ] */
        result = THREADAPI_INVALID_ARG;
        LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
    }
    else
    {
        void* threadResult;
        if (pthread_join(threadInstance->Pthread_handle, &threadResult) != 0)
        {
            /* SRS_THREADAPI_99_010: [ If joining fails, the API shall return `THREADAPI_ERROR` ] */
            result = THREADAPI_ERROR;
            LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
        }
        else
        {
            /* SRS_THREADAPI_99_009: [ On success, res argument receives the exit code of the thread and the API returns THREADAPI_OK ] */
            if (res != NULL)
            {
                *res = (int)(intptr_t)threadResult;
            }

            result = THREADAPI_OK;
        }

        /* SRS_THREADAPI_99_012: [ On return the threadHandle will be freed and thus invalid ] */
        free(threadInstance);
    }

    return result;
}

/* SRS_THREADAPI_99_013: [ This API ends the current thread  ] */
void ThreadAPI_Exit(int res)
{
    pthread_exit((void*)(intptr_t)res);
}

/* SRS_THREADAPI_99_015: [ This API sleeps for the passed in number of milliseconds ] */
void ThreadAPI_Sleep(unsigned int milliseconds)
{
#ifdef TI_RTOS
    Task_sleep(milliseconds);
#else
    time_t seconds = milliseconds / 1000;
    long nsRemainder = (milliseconds % 1000) * 1000000;
    struct timespec timeToSleep = { seconds, nsRemainder };
    (void)nanosleep(&timeToSleep, NULL);
#endif
}

/* SRS_THREADAPI_99_016: [ This API returns an unsigned long identifier of the current thread ] */
unsigned long ThreadAPI_Self(void)
{
    return (unsigned long)pthread_self();
}
