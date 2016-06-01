// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "azure_c_shared_utility/threadapi.h"
#include "windows.h"
#include "azure_c_shared_utility/iot_logging.h"

DEFINE_ENUM_STRINGS(THREADAPI_RESULT, THREADAPI_RESULT_VALUES);

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
        *threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
        if(threadHandle == NULL)
        {
            /* SRS_THREADAPI_99_005: [ If allocation of thread handle fails due to out of memory `THREADAPI_NO_MEMORY` shall be returned ] */
            /* SRS_THREADAPI_99_007: [ If the new thread is not running `THREADAPI_ERROR` shall be returned ] */
            result = (GetLastError() == ERROR_OUTOFMEMORY) ? THREADAPI_NO_MEMORY : THREADAPI_ERROR;

            LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
        }
        else
        {
            /* SRS_THREADAPI_99_006: [ If the new thread is running `THREADAPI_OK` shall be returned ]*/
            result = THREADAPI_OK;
        }
    }

    return result;
}

/* SRS_THREADAPI_99_008: [ This API will block until the thread identified by threadHandle has exited ] */
THREADAPI_RESULT ThreadAPI_Join(THREAD_HANDLE threadHandle, int *res)
{
    THREADAPI_RESULT result = THREADAPI_OK;

    if (threadHandle == NULL)
    {
        /* SRS_THREADAPI_99_011: [ This API on `NULL` handle passed returns `THREADAPI_INVALID_ARG` ] */
        result = THREADAPI_INVALID_ARG;
        LogError("(result = %s)", ENUM_TO_STRING(THREADAPI_RESULT, result));
    }
    else
    {
        DWORD returnCode = WaitForSingleObject(threadHandle, INFINITE);
        
        if( returnCode != WAIT_OBJECT_0)
        {
            /* SRS_THREADAPI_99_010: [ If joining fails, the API shall return `THREADAPI_ERROR` ] */
            result = THREADAPI_ERROR;
            LogError("Error waiting for Single Object. Return Code: %d. Error Code: %d", returnCode, result);
        }
        /* SRS_THREADAPI_99_009: [ On success, res argument receives the exit code of the thread and the API returns THREADAPI_OK ] */
        else if((res != NULL) && !GetExitCodeThread(threadHandle, res)) //If thread end is signaled we need to get the Thread Exit Code;
        {
            /* SRS_THREADAPI_99_010: [ If joining fails, the API shall return `THREADAPI_ERROR` ] */
            DWORD errorCode = GetLastError();
            result = THREADAPI_ERROR;
            LogError("Error Getting Exit Code. Error Code: %d.", errorCode);
        }
        /* SRS_THREADAPI_99_012: [ On return the threadHandle will be freed and thus invalid ] */
        CloseHandle(threadHandle);
    }

    return result;
}

/* SRS_THREADAPI_99_013: [ This API ends the current thread  ] */
void ThreadAPI_Exit(int res)
{
    ExitThread(res);
}

/* SRS_THREADAPI_99_015: [ This API sleeps for the passed in number of milliseconds ] */
void ThreadAPI_Sleep(unsigned int milliseconds)
{
    Sleep(milliseconds);
}

/* SRS_THREADAPI_99_016: [ This API returns an unsigned long identifier of the current thread ] */
unsigned long ThreadAPI_Self(void)
{
    return (unsigned long)GetCurrentThreadId();
}
