lock requirements
================


## Overview

The Thread API adapter component is implemented to expose threading functionality across different platforms. This component will expose some generic APIs so that it can be extended to platform specific implementation.  Current implementations include posix, win32, and mbed.

## Exposed API
**SRS_THREADAPI_99_001: [** Thread API interface exposes the following APIs **]**
```c
typedef enum THREADAPI_RESULT_TAG
{
    THREADAPI_OK,
    THREADAPI_INVALID_ARG,
    THREADAPI_NO_MEMORY,
    THREADAPI_ERROR
    
} THREADAPI_RESULT;
```

```c
typedef void* THREAD_HANDLE; 
```
**SRS_THREADAPI_99_001: [** This is the handle to a thread instances, and valid until thread is joined. **]**

```c
THREADAPI_RESULT ThreadAPI_Create(THREAD_HANDLE* threadHandle, THREAD_START_FUNC func, void* arg) ; 
```
**SRS_THREADAPI_99_002: [** This API creates a new thread with passed in THREAD_START_FUNC entry point and context arg **]**

**SRS_THREADAPI_99_003: [** The API returns THREADAPI_INVALID_ARG if threadhandle is NULL **]**

**SRS_THREADAPI_99_004: [** The API returns THREADAPI_INVALID_ARG if entry point is NULL **]**

**SRS_THREADAPI_99_005: [** If a new thread handle cannot be allocated the API returns `THREADAPI_NO_MEMORY` **]**

**SRS_THREADAPI_99_006: [** If the new thread is running `THREADAPI_OK` shall be returned **]**

**SRS_THREADAPI_99_007: [** If the new thread is not running `THREADAPI_ERROR` shall be returned **]**

```c
THREADAPI_RESULT ThreadAPI_Join(THREAD_HANDLE threadHandle, int *res) ; 
```
**SRS_THREADAPI_99_008: [** This API will block until the thread identified by threadHandle has exited **]**

**SRS_THREADAPI_99_009: [** On success, res argument receives the exit code of the thread and the API returns THREADAPI_OK **]**

**SRS_THREADAPI_99_010: [** This API on out of memory should return `THREADAPI_NO_MEMORY` **]**

**SRS_THREADAPI_99_011: [** This API on `NULL` handle passed returns `THREADAPI_INVALID_ARG` **]**

**SRS_THREADAPI_99_012: [** On return the threadHandle will be freed and thus invalid **]**

```c
void ThreadAPI_Exit(int res) ; 
```
**SRS_THREADAPI_99_013: [** This API ends the current thread  **]**

**SRS_THREADAPI_99_014: [** The current thread returns with the passed in result code **]**


```c
void ThreadAPI_Sleep(unsigned int milliseconds); 
```
**SRS_THREADAPI_99_015: [** This API sleeps for the passed in number of milliseconds **]**

```c
unsigned long ThreadAPI_Self(void);
```
**SRS_THREADAPI_99_016: [** This API returns an unsigned long identifier of the current thread **]**

