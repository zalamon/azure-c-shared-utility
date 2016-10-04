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
#include <stdlib.h>
#include <crtdbg.h>
#include <stdbool.h>
#include <stdint.h>
#include "azure_c_shared_utility/tlsio.h"
#include "azure_c_shared_utility/tlsio_cyclonessl.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "error.h"
#include "yarrow.h"
#include "tls.h"

typedef enum TLSIO_STATE_TAG
{
    TLSIO_STATE_NOT_OPEN,
    TLSIO_STATE_OPENING_UNDERLYING_IO,
    TLSIO_STATE_IN_HANDSHAKE,
    TLSIO_STATE_OPEN,
    TLSIO_STATE_CLOSING,
    TLSIO_STATE_ERROR
} TLSIO_STATE;

typedef struct TLS_IO_INSTANCE_TAG
{
    ON_BYTES_RECEIVED on_bytes_received;
    ON_IO_ERROR on_io_error;
    void* on_bytes_received_context;
    void* on_io_error_context;
    TLSIO_STATE tlsio_state;
    char* hostname;
    int port;
    char* certificate;
    YarrowContext yarrowContext;
    TlsContext *tlsContext;
    TlsSocket socket;
} TLS_IO_INSTANCE;

/*this function will clone an option given by name and value*/
void* tlsio_cyclonessl_clone_option(const char* name, const void* value)
{
    void* result;
    if(
        (name == NULL) || (value == NULL)
    )
    {
        LogError("invalid parameter detected: const char* name=%p, const void* value=%p", name, value);
        result = NULL;
    }
    else
    {
        if (strcmp(name, "TrustedCerts") == 0)
        {
            if(mallocAndStrcpy_s((char**)&result, value) != 0)
            {
                LogError("unable to mallocAndStrcpy_s TrustedCerts value");
                result = NULL;
            }
            else
            {
                /*return as is*/
            }
        }
        else
        {
            LogError("not handled option : %s", name);
            result = NULL;
        }
    }
    return result;
}

/*this function destroys an option previously created*/
void tlsio_cyclonessl_destroy_option(const char* name, const void* value)
{
    /*since all options for this layer are actually string copies., disposing of one is just calling free*/
    if (
        (name == NULL) || (value == NULL)
        )
    {
        LogError("invalid parameter detected: const char* name=%p, const void* value=%p", name, value);
    }
	else
	{
		if (strcmp(name, "TrustedCerts") == 0)
		{
			free((void*)value);
		}
        else
        {
            LogError("not handled option : %s", name);
        }
    }
}

static OPTIONHANDLER_HANDLE tlsio_cyclonessl_retrieve_options(CONCRETE_IO_HANDLE handle)
{
    OPTIONHANDLER_HANDLE result;
    if(handle == NULL)
    {
        LogError("invalid parameter detected: CONCRETE_IO_HANDLE handle=%p", handle);
        result = NULL;
    }
    else
    {
        result = OptionHandler_Create(tlsio_cyclonessl_clone_option, tlsio_cyclonessl_destroy_option, tlsio_cyclonessl_setoption);
        if (result == NULL)
        {
            LogError("unable to OptionHandler_Create");
            /*return as is*/
        }
        else
        {
            /*this layer cares about the certificates and the x509 credentials*/
            TLS_IO_INSTANCE* tls_io_instance = (TLS_IO_INSTANCE*)handle;
            if(
                (tls_io_instance->certificate != NULL) && 
                (OptionHandler_AddOption(result, "TrustedCerts", tls_io_instance->certificate) != 0)
            )
            {
                LogError("unable to save TrustedCerts option");
                OptionHandler_Destroy(result);
                result = NULL;
            }
            else
            {
                /*all is fine, all interesting options have been saved*/
                /*return as is*/
            }
        }
    }
    return result;
}

static const IO_INTERFACE_DESCRIPTION tlsio_cyclonessl_interface_description =
{
    tlsio_cyclonessl_retrieve_options,
    tlsio_cyclonessl_create,
    tlsio_cyclonessl_destroy,
    tlsio_cyclonessl_open,
    tlsio_cyclonessl_close,
    tlsio_cyclonessl_send,
    tlsio_cyclonessl_dowork,
    tlsio_cyclonessl_setoption
};

CONCRETE_IO_HANDLE tlsio_cyclonessl_create(void* io_create_parameters)
{
    TLSIO_CONFIG* tls_io_config = io_create_parameters;
    TLS_IO_INSTANCE* result;

    if (tls_io_config == NULL)
    {
        result = NULL;
        LogError("NULL tls_io_config.");
    }
    else
    {
        result = malloc(sizeof(TLS_IO_INSTANCE));
        if (result == NULL)
        {
            LogError("Failed allocating TLSIO instance.");
        }
        else
        {
            uint8_t seed[32];
            size_t i;

            memset(result, 0, sizeof(TLS_IO_INSTANCE));
            mallocAndStrcpy_s(&result->hostname, tls_io_config->hostname);
            result->port = tls_io_config->port;

            result->certificate = NULL;

            result->on_bytes_received = NULL;
            result->on_bytes_received_context = NULL;

            result->on_io_error = NULL;
            result->on_io_error_context = NULL;

            result->tlsio_state = TLSIO_STATE_NOT_OPEN;

            /* seed should be initialized with some random seed ... */
            for (i = 0; i < 32; i++)
            {
                seed[i] = rand() * 255 / RAND_MAX;
            }

            /* check for errors */
            yarrowInit(&result->yarrowContext);
            yarrowSeed(&result->yarrowContext, seed, sizeof(seed));
        }
    }

    return result;
}

void tlsio_cyclonessl_destroy(CONCRETE_IO_HANDLE tls_io)
{
    if (tls_io == NULL)
    {
        LogError("NULL tls_io.");
    }
    else
    {
        TLS_IO_INSTANCE* tls_io_instance = (TLS_IO_INSTANCE*)tls_io;
        tlsio_cyclonessl_close(tls_io, NULL, NULL);
        if (tls_io_instance->certificate != NULL)
        {
            free(tls_io_instance->certificate);
            tls_io_instance->certificate = NULL;
        }
        free(tls_io_instance->hostname);
        free(tls_io);
    }
}

static TlsSocket create_socket(const char* hostname, int port)
{
    TlsSocket result;

#if (TLS_BSD_SOCKET_SUPPORT == ENABLED)
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
#else
    Socket* socket = socketOpen(SOCKET_TYPE_STREAM, SOCKET_IP_PROTO_TCP);
    if (!socket)
    {
        LogError("Cannot create socket");
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
#endif

    return result;
}

static void destroy_socket(TlsSocket socket)
{
#if _WIN32
    closesocket((SOCKET)socket);
#else
    socketClose(socket);
#endif
}

int tlsio_cyclonessl_open(CONCRETE_IO_HANDLE tls_io, ON_IO_OPEN_COMPLETE on_io_open_complete, void* on_io_open_complete_context, ON_BYTES_RECEIVED on_bytes_received, void* on_bytes_received_context, ON_IO_ERROR on_io_error, void* on_io_error_context)
{
    int result;

    if (tls_io == NULL)
    {
        result = __LINE__;
        LogError("NULL tls_io.");
    }
    else
    {
        TLS_IO_INSTANCE* tls_io_instance = (TLS_IO_INSTANCE*)tls_io;

        if (tls_io_instance->tlsio_state != TLSIO_STATE_NOT_OPEN)
        {
            result = __LINE__;
            LogError("Invalid tlsio_state. Expected state is TLSIO_STATE_NOT_OPEN.");
        }
        else
        {
            tls_io_instance->on_bytes_received = on_bytes_received;
            tls_io_instance->on_bytes_received_context = on_bytes_received_context;

            tls_io_instance->on_io_error = on_io_error;
            tls_io_instance->on_io_error_context = on_io_error_context;

            tls_io_instance->tlsio_state = TLSIO_STATE_OPENING_UNDERLYING_IO;

            tls_io_instance->socket = create_socket(tls_io_instance->hostname, tls_io_instance->port);
            if (tls_io_instance->socket == (TlsSocket)NULL)
            {
                LogError("Error: Cannot open socket (%d)\r\n", WSAGetLastError());
                result = __LINE__;
            }
            else
            {
                //Initialize SSL/TLS context
                tls_io_instance->tlsContext = tlsInit();
                if (!tls_io_instance->tlsContext)
                {
                    destroy_socket(tls_io_instance->socket);
                    tls_io_instance->socket = (TlsSocket)NULL;
                    LogError("Error: tlsInit\r\n");
                    result = __LINE__;
                }
                else
                {
                    //Bind TLS to the relevant socket
                    if (tlsSetSocket(tls_io_instance->tlsContext, (TlsSocket)tls_io_instance->socket))
                    {
                        tlsFree(tls_io_instance->tlsContext);
                        destroy_socket(tls_io_instance->socket);
                        tls_io_instance->socket = (TlsSocket)NULL;
                        LogError("Error: tlsSetSocket\r\n");
                        result = __LINE__;
                    }
                    //Select client operation mode
                    else if (tlsSetConnectionEnd(tls_io_instance->tlsContext, TLS_CONNECTION_END_CLIENT))
                    {
                        tlsFree(tls_io_instance->tlsContext);
                        destroy_socket(tls_io_instance->socket);
                        tls_io_instance->socket = (TlsSocket)NULL;
                        LogError("tlsSetConnectionEnd failed\r\n");
                        result = __LINE__;
                    }
                    //Set the PRNG algorithm to be used
                    else if (tlsSetPrng(tls_io_instance->tlsContext, YARROW_PRNG_ALGO, &tls_io_instance->yarrowContext))
                    {
                        tlsFree(tls_io_instance->tlsContext);
                        destroy_socket(tls_io_instance->socket);
                        tls_io_instance->socket = (TlsSocket)NULL;
                        LogError("tlsSetPrng failed\r\n");
                        result = __LINE__;
                    }
                    else
                    {
                        unsigned char is_error = 0;

                        if (tls_io_instance->certificate != NULL)
                        {
                            if (tlsSetTrustedCaList(tls_io_instance->tlsContext, tls_io_instance->certificate, strlen(tls_io_instance->certificate)))
                            {
                                is_error = 1;
                            }
                        }

                        if (is_error)
                        {
                            tlsFree(tls_io_instance->tlsContext);
                            destroy_socket(tls_io_instance->socket);
                            tls_io_instance->socket = (TlsSocket)NULL;
                            LogError("tlsSetTrustedCaList failed\r\n");
                            result = __LINE__;
                        }
                        else
                        {
                            //Establish a secure session
                            if (tlsConnect(tls_io_instance->tlsContext))
                            {
                                tlsFree(tls_io_instance->tlsContext);
                                destroy_socket(tls_io_instance->socket);
                                tls_io_instance->socket = (TlsSocket)NULL;
                                LogError("tlsConnect failed\r\n");
                                result = __LINE__;
                            }
                            else
                            {
                                tls_io_instance->tlsio_state = TLSIO_STATE_OPEN;
                                on_io_open_complete(on_io_open_complete_context, IO_OPEN_OK);

                                result = 0;
                            }
                        }
                    }
                }
            }
        }
    }

    return result;
}

int tlsio_cyclonessl_close(CONCRETE_IO_HANDLE tls_io, ON_IO_CLOSE_COMPLETE on_io_close_complete, void* on_io_close_complete_context)
{
    int result = 0;

    if (tls_io == NULL)
    {
        result = __LINE__;
        LogError("NULL tls_io.");
    }
    else
    {
        TLS_IO_INSTANCE* tls_io_instance = (TLS_IO_INSTANCE*)tls_io;

        if ((tls_io_instance->tlsio_state == TLSIO_STATE_NOT_OPEN) ||
            (tls_io_instance->tlsio_state == TLSIO_STATE_CLOSING))
        {
            result = __LINE__;
            LogError("Invalid tlsio_state. Expected state is TLSIO_STATE_NOT_OPEN or TLSIO_STATE_CLOSING.");
        }
        else
        {
            tls_io_instance->tlsio_state = TLSIO_STATE_CLOSING;

            if (tlsShutdown(tls_io_instance->tlsContext))
            {
                LogError("tlsShutdown failed\r\n");
                result = __LINE__;
            }
            else
            {
                destroy_socket(tls_io_instance->socket);
                tls_io_instance->socket = (TlsSocket)NULL;
                tlsFree(tls_io_instance->tlsContext);
                tls_io_instance->tlsContext = NULL;

                if (on_io_close_complete != NULL)
                {
                    on_io_close_complete(on_io_close_complete_context);
                }

                tls_io_instance->tlsio_state = TLSIO_STATE_NOT_OPEN;
                result = 0;
            }
        }
    }

    return result;
}

static int tlsio_cyclonessl_send(CONCRETE_IO_HANDLE tls_io, const void* buffer, size_t size, ON_SEND_COMPLETE on_send_complete, void* on_send_complete_context)
{
    int result;

    if (tls_io == NULL)
    {
        result = __LINE__;
        LogError("NULL tls_io.");
    }
    else
    {
        TLS_IO_INSTANCE* tls_io_instance = (TLS_IO_INSTANCE*)tls_io;

        if (tls_io_instance->tlsio_state != TLSIO_STATE_OPEN)
        {
            result = __LINE__;
            LogError("Invalid tlsio_state. Expected state is TLSIO_STATE_OPEN.");
        }
        else
        {
            if (tlsWrite(tls_io_instance->tlsContext, buffer, size, 0) != 0)
            {
                result = __LINE__;
                LogError("SSL_write error.");
            }
            else
            {
                result = 0;
            }
        }
    }

    return result;
}

static void tlsio_cyclonessl_dowork(CONCRETE_IO_HANDLE tls_io)
{
    if (tls_io == NULL)
    {
        LogError("NULL tls_io.");
    }
    else
    {
        TLS_IO_INSTANCE* tls_io_instance = (TLS_IO_INSTANCE*)tls_io;

        if ((tls_io_instance->tlsio_state != TLSIO_STATE_NOT_OPEN) &&
            (tls_io_instance->tlsio_state != TLSIO_STATE_ERROR))
        {
            unsigned char buffer[64];

            size_t received;
            if (tlsRead(tls_io_instance->tlsContext, buffer, sizeof(buffer), &received, 0) != 0)
            {
                /* error receiving */
                LogError("Error received bytes");
                tls_io_instance->on_io_error(tls_io_instance->on_io_error_context);
                tls_io_instance->tlsio_state = TLSIO_STATE_ERROR;
            }
            else
            {
                if (received > 0)
                {
                    tls_io_instance->on_bytes_received(tls_io_instance->on_bytes_received_context, buffer, received);
                }
            }
        }
    }
}

static int tlsio_cyclonessl_setoption(CONCRETE_IO_HANDLE tls_io, const char* optionName, const void* value)
{
    int result;

    if (tls_io == NULL || optionName == NULL)
    {
        LogError("NULL tls_io");
        result = __LINE__;
    }
    else
    {
        TLS_IO_INSTANCE* tls_io_instance = (TLS_IO_INSTANCE*)tls_io;

        if (strcmp("TrustedCerts", optionName) == 0)
        {
            const char* cert = (const char*)value;

            if (tls_io_instance->certificate != NULL)
            {
                // Free the memory if it has been previously allocated
                free(tls_io_instance->certificate);
            }

            // Store the certificate
            size_t len = strlen(cert);
            tls_io_instance->certificate = malloc(len+1);
            if (tls_io_instance->certificate == NULL)
            {
                LogError("Error allocating memory for certificates");
                result = __LINE__;
            }
            else
            {
                strcpy(tls_io_instance->certificate, cert);
                result = 0;
            }
        }
        else
        {
            LogError("Unrecognized option");
            result = __LINE__;
        }
    }

    return result;
}

const IO_INTERFACE_DESCRIPTION* tlsio_cyclonessl_get_interface_description(void)
{
    return &tlsio_cyclonessl_interface_description;
}
