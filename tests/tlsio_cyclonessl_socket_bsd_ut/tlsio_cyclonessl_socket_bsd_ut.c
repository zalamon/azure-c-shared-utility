// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#ifdef _MSC_VER
#pragma warning(disable: 4200 4201 4244 4100 4267 4701 4703 4389 4005 4996)
#endif

#include "tls.h"

#undef htonl
#undef htons
#undef ntohl
#undef ntohs

#if _WIN32
#undef DECLSPEC_IMPORT

#pragma warning(disable: 4273 4189)

#include <winsock2.h>
#include <mstcpip.h>
#include <ws2tcpip.h>
#endif

#include <stddef.h>

#include "testrunnerswitcher.h"
#include "umock_c.h"
#include "umocktypes_charptr.h"

static TEST_MUTEX_HANDLE g_testByTest;
static TEST_MUTEX_HANDLE g_dllByDll;

void* my_gballoc_malloc(size_t size)
{
    return malloc(size);
}

void my_gballoc_free(void* ptr)
{
    free(ptr);
}

#include "azure_c_shared_utility/tlsio_cyclonessl_socket.h"

#define ENABLE_MOCKS

#include "azure_c_shared_utility/gballoc.h"

SOCKET TEST_SOCKET = (SOCKET)0x4243;
struct hostent* TEST_HOST_ENT = (struct hostent*)0x4244;

MOCK_FUNCTION_WITH_CODE(WSAAPI, SOCKET, socket, int, af, int, type, int, protocol)
MOCK_FUNCTION_END(TEST_SOCKET)
MOCK_FUNCTION_WITH_CODE(WSAAPI, int, closesocket, SOCKET, s)
MOCK_FUNCTION_END(0)
MOCK_FUNCTION_WITH_CODE(WSAAPI, int, connect, SOCKET, s, const struct sockaddr*, name, int, namelen)
MOCK_FUNCTION_END(0)
MOCK_FUNCTION_WITH_CODE(WSAAPI, int, WSAGetLastError)
MOCK_FUNCTION_END(0)
MOCK_FUNCTION_WITH_CODE(WSAAPI, struct hostent*, gethostbyname, const char*, name);
MOCK_FUNCTION_END(TEST_HOST_ENT)
MOCK_FUNCTION_WITH_CODE(WSAAPI, INT, getaddrinfo, PCSTR, pNodeName, PCSTR, pServiceName, const ADDRINFOA*, pHints, PADDRINFOA*, ppResult)
MOCK_FUNCTION_END(0)

#undef ENABLE_MOCKS

DEFINE_ENUM_STRINGS(UMOCK_C_ERROR_CODE, UMOCK_C_ERROR_CODE_VALUES)

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code)
{
    char temp_str[256];
    (void)snprintf(temp_str, sizeof(temp_str), "umock_c reported error :%s", ENUM_TO_STRING(UMOCK_C_ERROR_CODE, error_code));
    ASSERT_FAIL(temp_str);
}

BEGIN_TEST_SUITE(tlsio_cyclonessl_socket_bsd_unittests)

TEST_SUITE_INITIALIZE(suite_init)
{
    int result;

    TEST_INITIALIZE_MEMORY_DEBUG(g_dllByDll);
    g_testByTest = TEST_MUTEX_CREATE();
    ASSERT_IS_NOT_NULL(g_testByTest);

    result = umock_c_init(on_umock_c_error);
    ASSERT_ARE_EQUAL(int, 0, result);
    result = umocktypes_charptr_register_types();
    ASSERT_ARE_EQUAL(int, 0, result);

    REGISTER_UMOCK_ALIAS_TYPE(TlsSocket, void*);
    REGISTER_UMOCK_ALIAS_TYPE(PCSTR, const char*);

    REGISTER_GLOBAL_MOCK_HOOK(gballoc_malloc, my_gballoc_malloc);
    REGISTER_GLOBAL_MOCK_HOOK(gballoc_free, my_gballoc_free);
}

TEST_SUITE_CLEANUP(suite_cleanup)
{
    umock_c_deinit();

    TEST_MUTEX_DESTROY(g_testByTest);
    TEST_DEINITIALIZE_MEMORY_DEBUG(g_dllByDll);
}

TEST_FUNCTION_INITIALIZE(TestMethodInitialize)
{
    if (TEST_MUTEX_ACQUIRE(g_testByTest))
    {
        ASSERT_FAIL("our mutex is ABANDONED. Failure in test framework");
    }

    umock_c_reset_all_calls();
}

TEST_FUNCTION_CLEANUP(TestMethodCleanup)
{
    TEST_MUTEX_RELEASE(g_testByTest);
}

/* tlsio_cyclonessl_socket_create */

/* Tests_SRS_TLSIO_CYCLONESSL_SOCKET_BSD_01_001: [ tlsio_cyclonessl_socket_create shall create a new socket to be used by CycloneSSL. ]*/
TEST_FUNCTION(tlsio_cyclonessl_socket_create_succeeds)
{
    ///arrange
    TlsSocket socket;

    STRICT_EXPECTED_CALL(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));

    ///act
    int result = tlsio_cyclonessl_socket_create("testhostname", 4242, &socket);

    ///assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    ASSERT_ARE_EQUAL(int, 0, result);
}

END_TEST_SUITE(tlsio_cyclonessl_socket_bsd_unittests)
