// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#ifdef _MSC_VER
#undef DECLSPEC_IMPORT

#pragma warning(disable: 4273 4189)

#include <winsock2.h>
#include <mstcpip.h>
#include <ws2tcpip.h>
#endif

#include <stddef.h>

#include "testrunnerswitcher.h"
#include "umock_c.h"

#define ENABLE_MOCKS

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
