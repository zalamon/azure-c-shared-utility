// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <stdlib.h>

#include "azure_c_shared_utility/condition.h"
#include "windows.h"
#include "azure_c_shared_utility/iot_logging.h"

DEFINE_ENUM_STRINGS(COND_RESULT, COND_RESULT_VALUES);

COND_HANDLE Condition_Init(void)
{
    // Codes_SRS_CONDITION_18_002: [ Condition_Init shall create and return a CONDITION_HANDLE ]
    HANDLE hWait = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hWait == INVALID_HANDLE_VALUE)
    {
        // Codes_SRS_CONDITION_18_008: [ Condition_Init shall return NULL if it fails to allocate the CONDITION_HANDLE ]
        return NULL;
    }
    else
    {
        return (COND_HANDLE)hWait;
    }
}

COND_RESULT Condition_Post(COND_HANDLE handle)
{
    COND_RESULT result;
    if (handle == NULL)
    {
        // Codes_SRS_CONDITION_18_001: [ Condition_Post shall return COND_INVALID_ARG if handle is NULL ]
        result = COND_INVALID_ARG;
    }
    else
    {
        if (SetEvent((HANDLE)handle))
        {
            // Codes_SRS_CONDITION_18_003: [ Condition_Post shall return COND_OK if it succcessfully posts the condition ]
            result = COND_OK;
        }
        else
        {
            result = COND_ERROR;
        }
    }
    return result;
}

COND_RESULT Condition_Wait(COND_HANDLE handle, LOCK_HANDLE lock, int timeout_milliseconds)
{
    COND_RESULT result;
    // Codes_SRS_CONDITION_18_004: [ Condition_Wait shall return COND_INVALID_ARG if handle is NULL ]
    // Codes_SRS_CONDITION_18_005: [ Condition_Wait shall return COND_INVALID_ARG if lock is NULL and timeout_milliseconds is 0 ]
    // Codes_SRS_CONDITION_18_006: [ Condition_Wait shall return COND_INVALID_ARG if lock is NULL and timeout_milliseconds is not 0 ]
    if (handle == NULL || lock == NULL)
    {
        result = COND_INVALID_ARG;
    }
    else
    {
        DWORD wait_result;

        if (timeout_milliseconds == 0)
        {
            timeout_milliseconds = INFINITE;
        }

        Unlock(lock);

        // Codes_SRS_CONDITION_18_013: [ Condition_Wait shall accept relative timeouts ]
        wait_result = WaitForSingleObject((HANDLE)handle, timeout_milliseconds);

        Lock(lock);

        if (wait_result == WAIT_TIMEOUT)
        {
            // Codes_SRS_CONDITION_18_011: [ Condition_Wait shall return COND_TIMEOUT if the condition is NOT triggered and timeout_milliseconds is not 0 ]
            result = COND_TIMEOUT;
        }
        else if (wait_result == WAIT_OBJECT_0)
        {
            // Codes_SRS_CONDITION_18_012: [ Condition_Wait shall return COND_OK if the condition is triggered and timeout_milliseconds is not 0 ]
            result = COND_OK;
        }
        else
        {
            LogError("Failed to Condition_Wait");
            result = COND_ERROR;
        }
    }
    return result;
}

void Condition_Deinit(COND_HANDLE handle)
{
    // Codes_SRS_CONDITION_18_007: [ Condition_Deinit will not fail if handle is NULL ]
    if (handle != NULL)
    {
        // Codes_SRS_CONDITION_18_009: [ Condition_Deinit will deallocate handle if it is not NULL 
        CloseHandle((HANDLE)handle);
    }
}

