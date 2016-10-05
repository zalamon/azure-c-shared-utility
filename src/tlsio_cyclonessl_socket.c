// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include "azure_c_shared_utility/xlogging.h"
#include "socket.h"
#include "tls.h"
#include "azure_c_shared_utility/tlsio_cyclonessl_socket.h"

TlsSocket tlsio_cyclonessl_socket_create(const char* hostname, int port)
{
    TlsSocket result;
    if (hostname == NULL)
    {
        LogError("NULL hostname");
        result = NULL;
    }
    else
    {
        Socket* socket = socketOpen(SOCKET_TYPE_STREAM, SOCKET_IP_PROTO_TCP);
        if (!socket)
        {
            LogError("socketOpen failed, cannot create socket");
            result = NULL;
        }
        else
        {
            IpAddr ipAddr;

            if (getHostByName(NULL, hostname, &ipAddr, 0))
            {
                socketClose(socket);
                LogError("Cannot resolve host");
                result = NULL;
            }
            else if (socketConnect(socket, &ipAddr, port))
            {
                socketClose(socket);
                LogError("Failed to connect");
                result = NULL;
            }
            else
            {
                result = (TlsSocket)socket;
            }
        }
    }

    return result;
}

void tlsio_cyclonessl_socket_destroy(TlsSocket socket)
{
    if (socket == NULL)
    {
        LogError("tlsio_cyclonessl_socket_destroy: NULL socket");
    }
    else
    {
        socketClose(socket);
    }
}
