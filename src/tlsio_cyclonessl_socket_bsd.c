// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#if _WIN32
#define _WINERROR_
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <stdio.h>
#include "tls.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/tlsio_cyclonessl_socket.h"

int tlsio_cyclonessl_socket_create(const char* hostname, unsigned int port, TlsSocket* new_socket)
{
    TlsSocket result;
    if (hostname == NULL)
    {
        LogError("NULL hostname");
        result = (TlsSocket)NULL;
    }
    else
    {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0)
        {
            LogError("Error: Cannot create socket (%d)\r\n", WSAGetLastError());
            result = (TlsSocket)NULL;
        }
        else
        {
            char portString[16];
            ADDRINFO addrHint = { 0 };
            ADDRINFO* addrInfo = NULL;

            addrHint.ai_family = AF_INET;
            addrHint.ai_socktype = SOCK_STREAM;
            addrHint.ai_protocol = 0;
            if ((sprintf(portString, "%u", port) < 0) ||
                (getaddrinfo(hostname, portString, &addrHint, &addrInfo) != 0))
            {
                LogError("Failure: getaddrinfo failure %d.", WSAGetLastError());
                (void)closesocket(sock);
                result = __LINE__;
            }
            else
            {
                if (connect(sock, addrInfo->ai_addr, (int)addrInfo->ai_addrlen) < 0)
                {
                    LogError("Error: Failed to connect (%d)\r\n", WSAGetLastError());
                    closesocket(sock);
                    result = (TlsSocket)NULL;
                }
                else
                {
                    result = (TlsSocket)sock;
                }
            }
        }
    }

    return result;
}

void tlsio_cyclonessl_socket_destroy(TlsSocket socket)
{
    if (socket == INVALID_SOCKET)
    {
        LogError("Invalid socket\r\n");
    }
    else
    {
        (void)closesocket((SOCKET)socket);
    }
}
