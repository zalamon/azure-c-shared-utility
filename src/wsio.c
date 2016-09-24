 // Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/wsio.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/list.h"
#include "azure_c_shared_utility/optionhandler.h"
#include "azure_c_shared_utility/xio.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "azure_c_shared_utility/crt_abstractions.h"

typedef enum IO_STATE_TAG
{
    IO_STATE_NOT_OPEN,
    IO_STATE_OPENING,
    IO_STATE_OPEN,
    IO_STATE_CLOSING,
    IO_STATE_ERROR
} IO_STATE;

typedef struct PENDING_SOCKET_IO_TAG
{
    unsigned char* bytes;
    size_t size;
    ON_SEND_COMPLETE on_send_complete;
    void* callback_context;
    LIST_HANDLE pending_io_list;
    bool is_partially_sent;
} PENDING_SOCKET_IO;

typedef struct WSIO_INSTANCE_TAG
{
    ON_BYTES_RECEIVED on_bytes_received;
    void* on_bytes_received_context;
    ON_IO_OPEN_COMPLETE on_io_open_complete;
    void* on_io_open_complete_context;
    ON_IO_ERROR on_io_error;
    void* on_io_error_context;
    IO_STATE io_state;
    LIST_HANDLE pending_io_list;
    char* hostname;
    char* proxy_address;
    int proxy_port;
    XIO_HANDLE underlying_io;
    size_t received_byte_count;
    unsigned char* received_bytes;
} WSIO_INSTANCE;

static void indicate_error(WSIO_INSTANCE* wsio_instance)
{
    wsio_instance->io_state = IO_STATE_ERROR;
    if (wsio_instance->on_io_error != NULL)
    {
        wsio_instance->on_io_error(wsio_instance->on_io_error_context);
    }
}

static void indicate_open_complete(WSIO_INSTANCE* ws_io_instance, IO_OPEN_RESULT open_result)
{
    if (ws_io_instance->on_io_open_complete != NULL)
    {
        ws_io_instance->on_io_open_complete(ws_io_instance->on_io_open_complete_context, open_result);
    }
}

static int add_pending_io(WSIO_INSTANCE* ws_io_instance, const unsigned char* buffer, size_t size, ON_SEND_COMPLETE on_send_complete, void* callback_context)
{
    int result;
    PENDING_SOCKET_IO* pending_socket_io = (PENDING_SOCKET_IO*)malloc(sizeof(PENDING_SOCKET_IO));
    if (pending_socket_io == NULL)
    {
        result = __LINE__;
    }
    else
    {
        pending_socket_io->bytes = (unsigned char*)malloc(size);
        if (pending_socket_io->bytes == NULL)
        {
            free(pending_socket_io);
            result = __LINE__;
        }
        else
        {
            pending_socket_io->is_partially_sent = false;
            pending_socket_io->size = size;
            pending_socket_io->on_send_complete = on_send_complete;
            pending_socket_io->callback_context = callback_context;
            pending_socket_io->pending_io_list = ws_io_instance->pending_io_list;
            (void)memcpy(pending_socket_io->bytes, buffer, size);

            if (list_add(ws_io_instance->pending_io_list, pending_socket_io) == NULL)
            {
                free(pending_socket_io->bytes);
                free(pending_socket_io);
                result = __LINE__;
            }
            else
            {
                result = 0;
            }
        }
    }

    return result;
}

static int remove_pending_io(WSIO_INSTANCE* wsio_instance, LIST_ITEM_HANDLE item_handle, PENDING_SOCKET_IO* pending_socket_io)
{
    int result;

    free(pending_socket_io->bytes);
    free(pending_socket_io);
    if (list_remove(wsio_instance->pending_io_list, item_handle) != 0)
    {
        result = __LINE__;
    }
    else
    {
        result = 0;
    }

    return result;
}

static void send_pending_ios(WSIO_INSTANCE* wsio_instance)
{
    LIST_ITEM_HANDLE first_pending_io;

    first_pending_io = list_get_head_item(wsio_instance->pending_io_list);

    if (first_pending_io != NULL)
    {
        PENDING_SOCKET_IO* pending_socket_io = (PENDING_SOCKET_IO*)list_item_get_value(first_pending_io);
        if (pending_socket_io == NULL)
        {
            indicate_error(wsio_instance);
        }
        else
        {
            bool is_partially_sent = pending_socket_io->is_partially_sent;
            size_t frame_length = 6;
            if (pending_socket_io->size > 125)
            {
                frame_length += 2;
            }
            if (pending_socket_io->size > 65535)
            {
                frame_length += 6;
            }

            frame_length += pending_socket_io->size;

            unsigned char* ws_buffer = (unsigned char*)malloc(frame_length * 2);
            if (ws_buffer == NULL)
            {
                if (pending_socket_io->on_send_complete != NULL)
                {
                    pending_socket_io->on_send_complete(pending_socket_io->callback_context, IO_SEND_ERROR);
                }

                if (is_partially_sent)
                {
                    indicate_error(wsio_instance);
                }
                else
                {
                    if (list_get_head_item(wsio_instance->pending_io_list) != NULL)
                    {
                        /* continue ... */
                    }
                }

                if ((remove_pending_io(wsio_instance, first_pending_io, pending_socket_io) != 0) && !is_partially_sent)
                {
                    indicate_error(wsio_instance);
                }
            }
            else
            {
                /* fill in frame */
                size_t pos = 0;

                ws_buffer[0] = (1 << 7) +
                    2;
                ws_buffer[1] = (1 << 7);
                if (pending_socket_io->size < 126)
                {
                    ws_buffer[1] |= pending_socket_io->size;
                    pos = 2;
                }
                else if (pending_socket_io->size < 65536)
                {
                    ws_buffer[1] |= 126;
                    ws_buffer[2] |= (pending_socket_io->size & 0xFF);
                    ws_buffer[3] |= (pending_socket_io->size >> 8);
                    pos = 4;
                }
                else
                {
                    ws_buffer[1] |= 127;
                    ws_buffer[2] |= ((uint64_t)pending_socket_io->size & 0xFF);
                    ws_buffer[3] |= ((uint64_t)pending_socket_io->size >> 8) & 0xFF;
                    ws_buffer[4] |= ((uint64_t)pending_socket_io->size >> 16) & 0xFF;
                    ws_buffer[5] |= ((uint64_t)pending_socket_io->size >> 24) & 0xFF;
                    ws_buffer[6] |= ((uint64_t)pending_socket_io->size >> 32) & 0xFF;
                    ws_buffer[7] |= ((uint64_t)pending_socket_io->size >> 40) & 0xFF;
                    ws_buffer[8] |= ((uint64_t)pending_socket_io->size >> 48) & 0xFF;
                    ws_buffer[9] |= ((uint64_t)pending_socket_io->size >> 56) & 0xFF;
                    pos = 10;
                }

                /* mask key */
                ws_buffer[pos++] = 0x00;
                ws_buffer[pos++] = 0x00;
                ws_buffer[pos++] = 0x00;
                ws_buffer[pos++] = 0x00;

                ws_buffer[0] = 0x82;
                ws_buffer[1] = 0xFE;
                //ws_buffer[0] = 1 + (2 << 4);
                //ws_buffer[1] = 1 + (126 << 1);
                ws_buffer[2] = 0x00;
                ws_buffer[3] = 0x08;
                ws_buffer[4] = 0x00;
                ws_buffer[5] = 0x00;
                ws_buffer[6] = 0x00;
                ws_buffer[7] = 0x00;
                pos = 8;

                (void)memcpy(ws_buffer + pos, pending_socket_io->bytes, pending_socket_io->size);
                pos += pending_socket_io->size;

                /*unsigned char fake_one[] = 
                {
                    0x82,
                    0xFE,
                    0x0,
                    0x0,
                    0x0,
                    0x0,
                    0x0,
                    0x0,
                    0x41,
                    0x4D,
                    0x51,
                    0x50,
                    0x3,
                    0x1,
                    0x0,
                    0x0,
                    0x0,
                    0x0,
                    0x1,
                    0x44,
                    0x2,
                    0x1,
                    0x0,
                    0x0,
                    0x0,
                    0x53,
                    0x41,
                    0xD0,
                    0x0,
                    0x0,
                    0x1,
                    0x34,
                    0x0,
                    0x0,
                    0x0,
                    0x2,
                    0xA3,
                    0x5,
                    0x50,
                    0x4C,
                    0x41,
                    0x49,
                    0x4E,
                    0xB0,
                    0x0,
                    0x0,
                    0x1,
                    0x24,
                    0x0,
                    0x6A,
                    0x61,
                    0x76,
                    0x61,
                    0x2D,
                    0x64,
                    0x65,
                    0x76,
                    0x69,
                    0x63,
                    0x65,
                    0x2D,
                    0x63,
                    0x6C,
                    0x69,
                    0x65,
                    0x6E,
                    0x74,
                    0x2D,
                    0x65,
                    0x32,
                    0x65,
                    0x2D,
                    0x74,
                    0x65,
                    0x73,
                    0x74,
                    0x2D,
                    0x61,
                    0x6D,
                    0x71,
                    0x70,
                    0x73,
                    0x2D,
                    0x62,
                    0x34,
                    0x39,
                    0x39,
                    0x63,
                    0x34,
                    0x36,
                    0x64,
                    0x2D,
                    0x31,
                    0x66,
                    0x38,
                    0x30,
                    0x2D,
                    0x34,
                    0x35,
                    0x33,
                    0x39,
                    0x2D,
                    0x39,
                    0x36,
                    0x31,
                    0x65,
                    0x2D,
                    0x31,
                    0x62,
                    0x32,
                    0x36,
                    0x30,
                    0x35,
                    0x64,
                    0x34,
                    0x34,
                    0x35,
                    0x38,
                    0x38,
                    0x40,
                    0x73,
                    0x61,
                    0x73,
                    0x2E,
                    0x69,
                    0x6F,
                    0x74,
                    0x2D,
                    0x73,
                    0x64,
                    0x6B,
                    0x73,
                    0x2D,
                    0x74,
                    0x65,
                    0x73,
                    0x74,
                    0x0,
                    0x53,
                    0x68,
                    0x61,
                    0x72,
                    0x65,
                    0x64,
                    0x41,
                    0x63,
                    0x63,
                    0x65,
                    0x73,
                    0x73,
                    0x53,
                    0x69,
                    0x67,
                    0x6E,
                    0x61,
                    0x74,
                    0x75,
                    0x72,
                    0x65,
                    0x20,
                    0x73,
                    0x69,
                    0x67,
                    0x3D,
                    0x77,
                    0x65,
                    0x53,
                    0x54,
                    0x36,
                    0x31,
                    0x25,
                    0x32,
                    0x42,
                    0x31,
                    0x55,
                    0x6A,
                    0x48,
                    0x71,
                    0x50,
                    0x41,
                    0x38,
                    0x5A,
                    0x49,
                    0x4F,
                    0x72,
                    0x32,
                    0x70,
                    0x56,
                    0x73,
                    0x44,
                    0x52,
                    0x57,
                    0x74,
                    0x74,
                    0x4D,
                    0x51,
                    0x41,
                    0x61,
                    0x76,
                    0x4A,
                    0x76,
                    0x57,
                    0x52,
                    0x5A,
                    0x4D,
                    0x64,
                    0x6A,
                    0x33,
                    0x30,
                    0x25,
                    0x33,
                    0x44,
                    0x26,
                    0x73,
                    0x65,
                    0x3D,
                    0x31,
                    0x34,
                    0x37,
                    0x34,
                    0x34,
                    0x39,
                    0x32,
                    0x37,
                    0x32,
                    0x31,
                    0x26,
                    0x73,
                    0x72,
                    0x3D,
                    0x69,
                    0x6F,
                    0x74,
                    0x2D,
                    0x73,
                    0x64,
                    0x6B,
                    0x73,
                    0x2D,
                    0x74,
                    0x65,
                    0x73,
                    0x74,
                    0x2E,
                    0x61,
                    0x7A,
                    0x75,
                    0x72,
                    0x65,
                    0x2D,
                    0x64,
                    0x65,
                    0x76,
                    0x69,
                    0x63,
                    0x65,
                    0x73,
                    0x2E,
                    0x6E,
                    0x65,
                    0x74,
                    0x2F,
                    0x64,
                    0x65,
                    0x76,
                    0x69,
                    0x63,
                    0x65,
                    0x73,
                    0x2F,
                    0x6A,
                    0x61,
                    0x76,
                    0x61,
                    0x2D,
                    0x64,
                    0x65,
                    0x76,
                    0x69,
                    0x63,
                    0x65,
                    0x2D,
                    0x63,
                    0x6C,
                    0x69,
                    0x65,
                    0x6E,
                    0x74,
                    0x2D,
                    0x65,
                    0x32,
                    0x65,
                    0x2D,
                    0x74,
                    0x65,
                    0x73,
                    0x74,
                    0x2D,
                    0x61,
                    0x6D,
                    0x71,
                    0x70,
                    0x73,
                    0x2D,
                    0x62,
                    0x34,
                    0x39,
                    0x39,
                    0x63,
                    0x34,
                    0x36,
                    0x64,
                    0x2D,
                    0x31,
                    0x66,
                    0x38,
                    0x30,
                    0x2D,
                    0x34,
                    0x35,
                    0x33,
                    0x39,
                    0x2D,
                    0x39,
                    0x36,
                    0x31,
                    0x65,
                    0x2D,
                    0x31,
                    0x62,
                    0x32,
                    0x36,
                    0x30,
                    0x35,
                    0x64,
                    0x34,
                    0x34,
                    0x35,
                    0x38,
                    0x38
                };*/

                //if (xio_send(wsio_instance->underlying_io, fake_one, sizeof(fake_one), pending_socket_io->on_send_complete, pending_socket_io->callback_context) != 0)
                if (xio_send(wsio_instance->underlying_io, ws_buffer, pos, pending_socket_io->on_send_complete, pending_socket_io->callback_context) != 0)
                {
                    indicate_error(wsio_instance);
                }
                else
                {
                    LogInfo("sent");
                    if (remove_pending_io(wsio_instance, first_pending_io, pending_socket_io) != 0)
                    {
                        indicate_error(wsio_instance);
                    }
                }

                free(ws_buffer);
            }
        }
    }
}

CONCRETE_IO_HANDLE wsio_create(void* io_create_parameters)
{
    WSIO_CONFIG* ws_io_config = io_create_parameters;
    WSIO_INSTANCE* result;

    if ((ws_io_config == NULL) ||
        (ws_io_config->underlying_io == NULL))
    {
        result = NULL;
    }
    else
    {
        result = (WSIO_INSTANCE*)malloc(sizeof(WSIO_INSTANCE));
        if (result != NULL)
        {
            size_t hostname_length;

            result->on_bytes_received = NULL;
            result->on_bytes_received_context = NULL;
            result->on_io_open_complete = NULL;
            result->on_io_open_complete_context = NULL;
            result->on_io_error = NULL;
            result->on_io_error_context = NULL;
            result->proxy_address = NULL;
            result->proxy_port = 0;
            result->received_bytes = NULL;
            result->received_byte_count = 0;
            result->underlying_io = ws_io_config->underlying_io;

            hostname_length = strlen(ws_io_config->hostname);
            result->hostname = malloc(hostname_length + 1);
            if (result->hostname == NULL)
            {
                free(result);
                result = NULL;
            }
            else
            {
                (void)memcpy(result->hostname, ws_io_config->hostname, hostname_length + 1);

                result->pending_io_list = list_create();
                if (result->pending_io_list == NULL)
                {
                    free(result->hostname);
                    free(result);
                    result = NULL;
                }
                else
                {
                    result->io_state = IO_STATE_NOT_OPEN;
                }
            }
        }
    }

    return result;
}

static void on_underlying_io_open_complete(void* context, IO_OPEN_RESULT open_result)
{
    WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)context;
    (void)context, open_result;
    const char upgrade_request_format[] = "GET /$iothub/websocket HTTP/1.1\r\n"
        "Host: %s:443\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Protocol: AMQPWSB10\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    char upgrade_request[2048];
    size_t len = sprintf(upgrade_request, upgrade_request_format, wsio_instance->hostname);

    if (xio_send(wsio_instance->underlying_io, upgrade_request, len, NULL, NULL) != 0)
    {
        LogError("Error sending upgrade request");
    }
}

static void on_underlying_io_bytes_received(void* context, const unsigned char* buffer, size_t size)
{
    WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)context;

    (void)buffer, size;
    LogInfo("Received %zu bytes", size);

    unsigned char* new_received_bytes = (unsigned char*)realloc(wsio_instance->received_bytes, wsio_instance->received_byte_count + size);
    if (new_received_bytes == NULL)
    {
        /* error */
    }
    else
    {

        wsio_instance->received_bytes = new_received_bytes;
        (void)memcpy(wsio_instance->received_bytes + wsio_instance->received_byte_count, buffer, size);
        wsio_instance->received_byte_count += size;

        if (wsio_instance->io_state == IO_STATE_OPENING)
        {
            size_t pos = 0;
            size_t last_pos = 0;
            unsigned char done = 0;

            while (done == 0)
            {
                /* parse the Upgrade */
                while ((pos < wsio_instance->received_byte_count) &&
                    (wsio_instance->received_bytes[pos] != '\r'))
                {
                    pos++;
                }

                if (pos == wsio_instance->received_byte_count)
                {
                    break;
                }

                if (pos - last_pos == 0)
                {
                    pos++;
                    while ((pos < wsio_instance->received_byte_count) &&
                        (wsio_instance->received_bytes[pos] == '\n'))
                    {
                        pos++;
                    }

                    done = 1;
                }
                else
                {
                    pos++;

                    while ((pos < wsio_instance->received_byte_count) &&
                        (wsio_instance->received_bytes[pos] == '\n'))
                    {
                        pos++;
                    }

                    if (pos == wsio_instance->received_byte_count)
                    {
                        break;
                    }

                    last_pos = pos;
                }
            }

            if (done)
            {
                /* parsed the upgrade response ... we assume */
                LogInfo("Got WS upgrade response");
                wsio_instance->io_state = IO_STATE_OPEN;
                if (wsio_instance->received_byte_count - pos > 0)
                {
                    memmove(wsio_instance->received_bytes, wsio_instance->received_bytes + pos, wsio_instance->received_byte_count - pos);
                }
                wsio_instance->received_byte_count -= pos;
                indicate_open_complete(wsio_instance, IO_OPEN_OK);
            }
        }
        else if (wsio_instance->io_state == IO_STATE_OPEN)
        {
            size_t needed_bytes;

            if (wsio_instance->received_byte_count > 0)
            {
                /* parse each frame */
                LogInfo("Got a frame?");

                needed_bytes = 2;
                if (wsio_instance->received_byte_count >= needed_bytes)
                {
                    uint64_t payload_len = wsio_instance->received_bytes[1] & 0x7F;
                    if (payload_len == 126)
                    {
                        needed_bytes += 2;
                        if (wsio_instance->received_byte_count >= needed_bytes)
                        {
                            payload_len = wsio_instance->received_bytes[3];
                            payload_len += wsio_instance->received_bytes[2] << 8;
                            needed_bytes += (size_t)payload_len;
                        }
                    }
                    else if (payload_len == 126)
                    {
                        needed_bytes += 8;
                        if (wsio_instance->received_byte_count >= needed_bytes)
                        {
                            payload_len = (uint64_t)wsio_instance->received_bytes[2] << 56;
                            payload_len += (uint64_t)wsio_instance->received_bytes[3] << 48;
                            payload_len += (uint64_t)wsio_instance->received_bytes[4] << 40;
                            payload_len += (uint64_t)wsio_instance->received_bytes[5] << 32;
                            payload_len += (uint64_t)wsio_instance->received_bytes[6] << 24;
                            payload_len += (uint64_t)wsio_instance->received_bytes[7] << 16;
                            payload_len += (uint64_t)wsio_instance->received_bytes[8] << 8;
                            payload_len += (uint64_t)wsio_instance->received_bytes[9];
                            needed_bytes += (size_t)payload_len;
                        }
                    }
                    else
                    {
                        needed_bytes += (size_t)payload_len;
                    }

                    needed_bytes += 4;

                    if (wsio_instance->received_byte_count >= needed_bytes)
                    {
                        /* got the frame */
                        wsio_instance->on_bytes_received(wsio_instance->on_bytes_received_context, wsio_instance->received_bytes + 14, (size_t)payload_len);
                    }
                }
            }
        }
    }
}

static void on_underlying_io_error(void* context)
{
    (void)context;
}

int wsio_open(CONCRETE_IO_HANDLE ws_io, ON_IO_OPEN_COMPLETE on_io_open_complete, void* on_io_open_complete_context, ON_BYTES_RECEIVED on_bytes_received, void* on_bytes_received_context, ON_IO_ERROR on_io_error, void* on_io_error_context)
{
    int result = 0;

    if (ws_io == NULL)
    {
        result = __LINE__;
    }
    else
    {
        WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)ws_io;

        if (wsio_instance->io_state != IO_STATE_NOT_OPEN)
        {
            result = __LINE__;
        }
        else
        {
            wsio_instance->on_bytes_received = on_bytes_received;
            wsio_instance->on_bytes_received_context = on_bytes_received_context;
            wsio_instance->on_io_open_complete = on_io_open_complete;
            wsio_instance->on_io_open_complete_context = on_io_open_complete_context;
            wsio_instance->on_io_error = on_io_error;
            wsio_instance->on_io_error_context = on_io_error_context;

            wsio_instance->io_state = IO_STATE_OPENING;

            /* connect here */
            if (xio_open(wsio_instance->underlying_io, on_underlying_io_open_complete, wsio_instance, on_underlying_io_bytes_received, wsio_instance, on_underlying_io_error, wsio_instance) != 0)
            {
                /* Error */
                wsio_instance->io_state = IO_STATE_NOT_OPEN;
                result = __LINE__;
            }
            else
            {
                result = 0;
            }
        }
    }
    
    return result;
}

int wsio_close(CONCRETE_IO_HANDLE ws_io, ON_IO_CLOSE_COMPLETE on_io_close_complete, void* on_io_close_complete_context)
{
    int result = 0;

    if (ws_io == NULL)
    {
        result = __LINE__;
    }
    else
    {
        WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)ws_io;

        if (wsio_instance->io_state == IO_STATE_NOT_OPEN)
        {
            result = __LINE__;
        }
        else
        {
            if (wsio_instance->io_state == IO_STATE_OPENING)
            {
                indicate_open_complete(wsio_instance, IO_OPEN_CANCELLED);
            }
            else
            {
                /* cancel all pending IOs */
                LIST_ITEM_HANDLE first_pending_io;

                while ((first_pending_io = list_get_head_item(wsio_instance->pending_io_list)) != NULL)
                {
                    PENDING_SOCKET_IO* pending_socket_io = (PENDING_SOCKET_IO*)list_item_get_value(first_pending_io);

                    if (pending_socket_io != NULL)
                    {
                        if (pending_socket_io->on_send_complete != NULL)
                        {
                            pending_socket_io->on_send_complete(pending_socket_io->callback_context, IO_SEND_CANCELLED);
                        }

                        if (pending_socket_io != NULL)
                        {
                            free(pending_socket_io->bytes);
                            free(pending_socket_io);
                        }
                    }

                    (void)list_remove(wsio_instance->pending_io_list, first_pending_io);
                }
            }

            xio_close(wsio_instance->underlying_io, NULL, NULL);
            wsio_instance->io_state = IO_STATE_NOT_OPEN;

            if (on_io_close_complete != NULL)
            {
                on_io_close_complete(on_io_close_complete_context);
            }

            result = 0;
        }
    }

    return result;
}

void wsio_destroy(CONCRETE_IO_HANDLE ws_io)
{
    if (ws_io != NULL)
    {
        WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)ws_io;

        (void)wsio_close(wsio_instance, NULL, NULL);

        list_destroy(wsio_instance->pending_io_list);

        if (wsio_instance->hostname != NULL)
        {
            free(wsio_instance->hostname);
        }
        if (wsio_instance->received_bytes != NULL)
        {
            free(wsio_instance->received_bytes);
        }

        free(ws_io);
    }
}

int wsio_send(CONCRETE_IO_HANDLE ws_io, const void* buffer, size_t size, ON_SEND_COMPLETE on_send_complete, void* callback_context)
{
    int result;

    if ((ws_io == NULL) ||
        (buffer == NULL) ||
        (size == 0))
    {
        result = __LINE__;
    }
    else
    {
        WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)ws_io;

        if (wsio_instance->io_state != IO_STATE_OPEN)
        {
            result = __LINE__;
        }
        else
        {
            if (add_pending_io(wsio_instance, buffer, size, on_send_complete, callback_context) != 0)
            {
                result = __LINE__;
            }
            else
            {
                /* I guess send here */
                send_pending_ios(wsio_instance);

                result = 0;
            }
        }
    }

    return result;
}

void wsio_dowork(CONCRETE_IO_HANDLE ws_io)
{
    if (ws_io != NULL)
    {
        WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)ws_io;

        if ((wsio_instance->io_state == IO_STATE_OPEN) ||
            (wsio_instance->io_state == IO_STATE_OPENING))
        {
            xio_dowork(wsio_instance->underlying_io);
        }
    }
}

int wsio_setoption(CONCRETE_IO_HANDLE ws_io, const char* optionName, const void* value)
{
    int result;
    if (
        (ws_io == NULL) ||
        (optionName == NULL)
        )
    {
        /* Codes_SRS_WSIO_01_136: [ If any of the arguments ws_io or option_name is NULL wsio_setoption shall return a non-zero value. ] ]*/
        result = __LINE__;
        LogError("invalid parameter (NULL) passed to HTTPAPI_SetOption");
    }
    else
    {
        WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)ws_io;
        if (strcmp(OPTION_PROXY_ADDRESS, optionName) == 0)
        {
            if (wsio_instance->proxy_address != NULL)
            {
                free(wsio_instance->proxy_address);
            }
            result = mallocAndStrcpy_s(&wsio_instance->proxy_address, (const char*)value);
        }
        else if (strcmp(OPTION_PROXY_PORT, optionName) == 0)
        {
            result = *(int*)value;
        }
        else if (strcmp(OPTION_HTTP_PROXY, optionName) == 0)
        {
            HTTP_PROXY_OPTIONS* proxy_data = (HTTP_PROXY_OPTIONS*)value;
            if (proxy_data->host_address == NULL || (proxy_data->username != NULL && proxy_data->password == NULL))
            {
                result = __LINE__;
            }
            else
            {
                wsio_instance->proxy_port = proxy_data->port;
                if (proxy_data->username != NULL)
                {
                    size_t length = strlen(proxy_data->host_address) + strlen(proxy_data->username) + strlen(proxy_data->password) + 3 + 5;
                    wsio_instance->proxy_address = (char*)malloc(length + 1);
                    if (wsio_instance->proxy_address == NULL)
                    {
                        result = __LINE__;
                    }
                    else
                    {
                        if (sprintf(wsio_instance->proxy_address, "%s:%s@%s:%d", proxy_data->username, proxy_data->password, proxy_data->host_address, wsio_instance->proxy_port) <= 0)
                        {
                            result = __LINE__;
                            free(wsio_instance->proxy_address);
                        }
                        else
                        {
                            result = 0;
                        }
                    }
                }
                else
                {
                    size_t length = strlen(proxy_data->host_address) + 6 + 1;
                    wsio_instance->proxy_address = (char*)malloc(length + 1);
                    if (wsio_instance->proxy_address == NULL)
                    {
                        result = __LINE__;
                    }
                    else
                    {
                        if (sprintf(wsio_instance->proxy_address, "%s:%d", proxy_data->host_address, wsio_instance->proxy_port) <= 0)
                        {
                            result = __LINE__;
                            free(wsio_instance->proxy_address);
                        }
                        else
                        {
                            result = 0;
                        }
                    }
                }
            }
        }
        else
        {
            /* Codes_SRS_WSIO_01_137: [ If the option_name argument indicates an option that is not handled by wsio, then wsio_setoption shall return a non-zero value. ]*/
            result = __LINE__;
        }
    }

    return result;
}

/*this function will clone an option given by name and value*/
void* wsio_clone_option(const char* name, const void* value)
{
    void* result;
    if (
        (name == NULL) || (value == NULL)
       )
    {
        /* Codes_SRS_WSIO_01_140: [ If the name or value arguments are NULL, wsio_clone_option shall return NULL. ]*/
        LogError("invalid parameter detected: const char* name=%p, const void* value=%p", name, value);
        result = NULL;
    }
    else if (strcmp(OPTION_PROXY_ADDRESS, name) == 0)
    {
        if (mallocAndStrcpy_s((char**)&result, (const char*)value) != 0)
        {
            LogError("unable to mallocAndStrcpy_s proxy_address value");
            result = NULL;
        }
    }
    else if (strcmp(OPTION_PROXY_PORT, name) == 0)
    {
        int* temp = malloc(sizeof(int));
        if (temp == NULL)
        {
            LogError("unable to allocate port number");
            result = NULL;
        }
        else
        {
            *temp = *(const int*)value;
            result = temp;
        }
    }
    else if (strcmp("TrustedCerts", name) == 0)
    {
        /* Codes_SRS_WSIO_01_141: [ wsio_clone_option shall clone the option named `TrustedCerts` by calling mallocAndStrcpy_s. ]*/
        /* Codes_SRS_WSIO_01_143: [ On success it shall return a non-NULL pointer to the cloned option. ]*/
        if (mallocAndStrcpy_s((char**)&result, (const char*)value) != 0)
        {
            /* Codes_SRS_WSIO_01_142: [ If mallocAndStrcpy_s for `TrustedCerts` fails, wsio_clone_option shall return NULL. ]*/
            LogError("unable to mallocAndStrcpy_s TrustedCerts value");
            result = NULL;
        }
    }
    else
    {
        result = NULL;
    }

    return result;
}

/*this function destroys an option previously created*/
void wsio_destroy_option(const char* name, const void* value)
{
    if (
        (name == NULL) || (value == NULL)
       )
    {
        /* Codes_SRS_WSIO_01_147: [ If any of the arguments is NULL, wsio_destroy_option shall do nothing. ]*/
        LogError("invalid parameter detected: const char* name=%p, const void* value=%p", name, value);
    }
    else if (strcmp(name, OPTION_HTTP_PROXY) == 0)
    {
        HTTP_PROXY_OPTIONS* proxy_data = (HTTP_PROXY_OPTIONS*)value;
        free((char*)proxy_data->host_address);
        if (proxy_data->username)
        {
            free((char*)proxy_data->username);
        }
        if (proxy_data->password)
        {
            free((char*)proxy_data->password);
        }
        free(proxy_data);
    }
    else if ((strcmp(name, OPTION_PROXY_ADDRESS) == 0) ||
        (strcmp(name, OPTION_PROXY_PORT) == 0) ||
        /* Codes_SRS_WSIO_01_144: [ If the option name is `TrustedCerts`, wsio_destroy_option shall free the char\* option indicated by value. ]*/
        (strcmp(name, "TrustedCerts") == 0))
    {
        free((void*)value);
    }
}

OPTIONHANDLER_HANDLE wsio_retrieveoptions(CONCRETE_IO_HANDLE handle)
{
    OPTIONHANDLER_HANDLE result;
    if (handle == NULL)
    {
        LogError("parameter handle is NULL");
        result = NULL;
    }
    else
    {
        /*Codes_SRS_WSIO_02_002: [** `wsio_retrieveoptions` shall produce an OPTIONHANDLER_HANDLE. ]*/
        result = OptionHandler_Create(wsio_clone_option, wsio_destroy_option, wsio_setoption);
        if (result == NULL)
        {
            LogError("unable to OptionHandler_Create");
            /*return as is*/
        }
        else
        {
            /* Codes_SRS_WSIO_01_145: [ `wsio_retrieveoptions` shall add to it the options: ]*/
            WSIO_INSTANCE* wsio_instance = (WSIO_INSTANCE*)handle;
            if (
                (wsio_instance->proxy_address != NULL) && 
                (OptionHandler_AddOption(result, OPTION_PROXY_ADDRESS, wsio_instance->proxy_address) != 0)
               )
            {
                LogError("unable to save proxy_address option");
                OptionHandler_Destroy(result);
                result = NULL;
            }
            else if ( 
                (wsio_instance->proxy_port != 0) && 
                (OptionHandler_AddOption(result, OPTION_PROXY_PORT, &wsio_instance->proxy_port) != 0)
                )
            {
                LogError("unable to save proxy_port option");
                OptionHandler_Destroy(result);
                result = NULL;
            }
        }
    }

    return result;
}

static const IO_INTERFACE_DESCRIPTION ws_io_interface_description =
{
    wsio_retrieveoptions,
    wsio_create,
    wsio_destroy,
    wsio_open,
    wsio_close,
    wsio_send,
    wsio_dowork,
    wsio_setoption
};

const IO_INTERFACE_DESCRIPTION* wsio_get_interface_description(void)
{
    return &ws_io_interface_description;
}

