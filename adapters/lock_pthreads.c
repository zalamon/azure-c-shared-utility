// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include <pthread.h>
#include "azure_c_shared_utility/lock.h"
#include "azure_c_shared_utility/iot_logging.h"

DEFINE_ENUM_STRINGS(LOCK_RESULT, LOCK_RESULT_VALUES);

/*SRS_LOCK_99_002:[ This API on success will return a valid lock handle which should be a non NULL value]*/
LOCK_HANDLE Lock_Init(void)
{
	pthread_mutex_t* lock_mtx;
	pthread_mutexattr_t attr;

	/* Locks are re-entrant on Windows et.al, attempt to set same behavior with posix */
	if (pthread_mutexattr_init(&attr) != 0)
	{
		LogError("Failed to initialize recursive mutex attribute");
		return NULL;
	}
	else if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
	{
		(void)pthread_mutexattr_destroy(&attr);
		
		LogError("Failed to set recursive type on mutex attribute");
		return NULL;
	}
	
	lock_mtx = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	if (NULL != lock_mtx)
	{
		if (pthread_mutex_init(lock_mtx, &attr) != 0)
		{
			/*SRS_LOCK_99_003:[ On Error Should return NULL]*/
			free(lock_mtx);
			lock_mtx = NULL;
			LogError("Failed to initialize mutex");
		}
	}
	
	(void)pthread_mutexattr_destroy(&attr);
	
	return (LOCK_HANDLE)lock_mtx;
}


LOCK_RESULT Lock(LOCK_HANDLE handle)
{
	LOCK_RESULT result;
	if (handle == NULL)
	{
		/*SRS_LOCK_99_007:[ This API on NULL handle passed returns LOCK_ERROR]*/
		result = LOCK_ERROR;
		LogError("(result = %s)", ENUM_TO_STRING(LOCK_RESULT, result));
	}
	else
	{
		if ( pthread_mutex_lock((pthread_mutex_t*)handle) == 0)
		{
			/*SRS_LOCK_99_005:[ This API on success should return LOCK_OK]*/
			result = LOCK_OK;
		}
		else
		{
			/*SRS_LOCK_99_006:[ This API on error should return LOCK_ERROR]*/
			result = LOCK_ERROR;
			LogError("(result = %s)", ENUM_TO_STRING(LOCK_RESULT, result));
		}
	}
	return result;
}
LOCK_RESULT Unlock(LOCK_HANDLE handle)
{
	LOCK_RESULT result;
	if (handle == NULL)
	{
		/*SRS_LOCK_99_011:[ This API on NULL handle passed returns LOCK_ERROR]*/
		result = LOCK_ERROR;
		LogError("(result = %s)", ENUM_TO_STRING(LOCK_RESULT, result));
	}
	else
	{
		if (pthread_mutex_unlock((pthread_mutex_t*)handle) == 0)
		{
			/*SRS_LOCK_99_009:[ This API on success should return LOCK_OK]*/
			result = LOCK_OK;
		}
		else
		{
			/*SRS_LOCK_99_010:[ This API on error should return LOCK_ERROR]*/
			result = LOCK_ERROR;
			LogError("(result = %s)", ENUM_TO_STRING(LOCK_RESULT, result));
		}
	}
	return result;
}

LOCK_RESULT Lock_Deinit(LOCK_HANDLE handle)
{
	LOCK_RESULT result=LOCK_OK ;
	if (NULL == handle)
	{
		/*SRS_LOCK_99_013:[ This API on NULL handle passed returns LOCK_ERROR]*/
		result = LOCK_ERROR;
		LogError("(result = %s)", ENUM_TO_STRING(LOCK_RESULT, result));
	}
	else
	{
		/*SRS_LOCK_99_012:[ This API frees the memory pointed by handle]*/
		if(pthread_mutex_destroy((pthread_mutex_t*)handle)==0)
		{
			free(handle);
			handle = NULL;
		}
		else
		{
			result = LOCK_ERROR;
			LogError("(result = %s)", ENUM_TO_STRING(LOCK_RESULT, result));
		}
	}
	
	return result;
}


