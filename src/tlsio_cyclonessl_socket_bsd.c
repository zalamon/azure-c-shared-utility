// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#if _WIN32
#define _WINERROR_
#include <winsock2.h>
#endif

#include <stdio.h>
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/tlsio_cyclonessl_socket.h"
#include "tls.h"

int tlsio_cyclonessl_socket_create(const char* hostname, int port, TlsSocket* socket)
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
            HOSTENT *host;
            host = gethostbyname(hostname);
            if (!host)
            {
                LogError("Error: Cannot resolve server name (%d)\r\n", WSAGetLastError());
                closesocket(sock);
                result = (TlsSocket)NULL;
            }
            else
            {
                SOCKADDR_IN addr;

                //Destination address
                addr.sin_family = host->h_addrtype;
                memcpy(&addr.sin_addr, host->h_addr, host->h_length);
                addr.sin_port = htons(port);

                if (connect(sock, (PSOCKADDR)&addr, sizeof(addr)) < 0)
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
