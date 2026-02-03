#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#include "nuiouser.h"
#include "iocommon.h"

#define NDISPROT_DEVICE_PATH "\\\\.\\NdisProt"

static void print_usage(const char *exe_name)
{
    fprintf(stderr,
            "Usage: %s [--index N] <ipv4> [ipv4 ...]\n\n"
            "Examples:\n"
            "  %s --index 0 192.168.1.10 203.0.113.5\n"
            "  %s 10.0.0.2\n",
            exe_name, exe_name, exe_name);
}

static int compare_ulong(const void *a, const void *b)
{
    const ULONG lhs = *(const ULONG *)a;
    const ULONG rhs = *(const ULONG *)b;

    if (lhs < rhs)
    {
        return -1;
    }
    if (lhs > rhs)
    {
        return 1;
    }
    return 0;
}

static BOOL query_binding(HANDLE device, ULONG index, BYTE **out_buffer, DWORD *out_length)
{
    DWORD buffer_size = 4096;
    BYTE *buffer = (BYTE *)malloc(buffer_size);
    DWORD bytes_returned = 0;

    if (buffer == NULL)
    {
        return FALSE;
    }

    ZeroMemory(buffer, buffer_size);
    ((NDISPROT_QUERY_BINDING *)buffer)->BindingIndex = index;

    if (!DeviceIoControl(device,
                         IOCTL_NDISPROT_QUERY_BINDING,
                         buffer,
                         buffer_size,
                         buffer,
                         buffer_size,
                         &bytes_returned,
                         NULL))
    {
        free(buffer);
        return FALSE;
    }

    *out_buffer = buffer;
    *out_length = bytes_returned;
    return TRUE;
}

static BOOL open_binding(HANDLE device, const BYTE *binding_buffer)
{
    const NDISPROT_QUERY_BINDING *binding = (const NDISPROT_QUERY_BINDING *)binding_buffer;
    const BYTE *device_name = binding_buffer + binding->DeviceNameOffset;
    const ULONG device_name_length = binding->DeviceNameLength;
    DWORD bytes_returned = 0;

    return DeviceIoControl(device,
                           IOCTL_NDISPROT_OPEN_DEVICE,
                           (LPVOID)device_name,
                           device_name_length,
                           NULL,
                           0,
                           &bytes_returned,
                           NULL);
}

static void print_binding(ULONG index, const BYTE *binding_buffer)
{
    const NDISPROT_QUERY_BINDING *binding = (const NDISPROT_QUERY_BINDING *)binding_buffer;
    const WCHAR *device_name = (const WCHAR *)(binding_buffer + binding->DeviceNameOffset);
    const WCHAR *device_descr = (const WCHAR *)(binding_buffer + binding->DeviceDescrOffset);

    wprintf(L"[%lu] %.*s - %.*s\n",
            index,
            binding->DeviceNameLength / sizeof(WCHAR),
            device_name,
            binding->DeviceDescrLength / sizeof(WCHAR),
            device_descr);
}

int main(int argc, char **argv)
{
    HANDLE device = INVALID_HANDLE_VALUE;
    ULONG binding_index = 0;
    int arg_index = 1;
    BYTE *binding_buffer = NULL;
    DWORD binding_length = 0;
    ULONG *ip_addresses = NULL;
    ULONG ip_count = 0;
    DWORD bytes_returned = 0;
    int i = 0;

    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[arg_index], "--index") == 0)
    {
        if (argc < 4)
        {
            print_usage(argv[0]);
            return 1;
        }
        binding_index = (ULONG)strtoul(argv[arg_index + 1], NULL, 10);
        arg_index += 2;
    }

    ip_count = (ULONG)(argc - arg_index);
    if (ip_count == 0)
    {
        print_usage(argv[0]);
        return 1;
    }

    ip_addresses = (ULONG *)calloc(ip_count, sizeof(ULONG));
    if (ip_addresses == NULL)
    {
        fprintf(stderr, "Out of memory.\n");
        return 1;
    }

    for (i = 0; i < (int)ip_count; i++)
    {
        const char *ip_text = argv[arg_index + i];
        ULONG ip_value = inet_addr(ip_text);
        if (ip_value == INADDR_NONE)
        {
            fprintf(stderr, "Invalid IPv4 address: %s\n", ip_text);
            free(ip_addresses);
            return 1;
        }
        ip_addresses[i] = ip_value;
    }

    qsort(ip_addresses, ip_count, sizeof(ULONG), compare_ulong);

    device = CreateFileA(NDISPROT_DEVICE_PATH,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (device == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Failed to open %s (error %lu).\n", NDISPROT_DEVICE_PATH, GetLastError());
        free(ip_addresses);
        return 1;
    }

    (void)DeviceIoControl(device,
                          IOCTL_NDISPROT_BIND_WAIT,
                          NULL,
                          0,
                          NULL,
                          0,
                          &bytes_returned,
                          NULL);

    fprintf(stdout, "Available bindings:\n");
    for (ULONG index = 0; query_binding(device, index, &binding_buffer, &binding_length); index++)
    {
        print_binding(index, binding_buffer);
        free(binding_buffer);
        binding_buffer = NULL;
        binding_length = 0;
    }

    if (!query_binding(device, binding_index, &binding_buffer, &binding_length))
    {
        fprintf(stderr, "Failed to query binding %lu.\n", binding_index);
        CloseHandle(device);
        free(ip_addresses);
        return 1;
    }

    if (!open_binding(device, binding_buffer))
    {
        fprintf(stderr, "Failed to open binding %lu (error %lu).\n", binding_index, GetLastError());
        free(binding_buffer);
        CloseHandle(device);
        free(ip_addresses);
        return 1;
    }

    {
        const size_t buffer_size = sizeof(ULONG) + (sizeof(ULONG) * ip_count);
        IPv4BlockAddrArray *block_list = (IPv4BlockAddrArray *)malloc(buffer_size);

        if (block_list == NULL)
        {
            fprintf(stderr, "Out of memory.\n");
            free(binding_buffer);
            CloseHandle(device);
            free(ip_addresses);
            return 1;
        }

        block_list->NumberElements = ip_count;
        for (i = 0; i < (int)ip_count; i++)
        {
            block_list->IPAddrArray[i] = ip_addresses[i];
        }

        if (!DeviceIoControl(device,
                             IOCTL_PTUSERIO_SET_IPv4_BLOCK_FILTER,
                             block_list,
                             (DWORD)buffer_size,
                             NULL,
                             0,
                             &bytes_returned,
                             NULL))
        {
            fprintf(stderr, "Failed to set IPv4 block filter (error %lu).\n", GetLastError());
            free(block_list);
            free(binding_buffer);
            CloseHandle(device);
            free(ip_addresses);
            return 1;
        }

        fprintf(stdout, "IPv4 block filter updated for binding %lu.\n", binding_index);
        free(block_list);
    }

    free(binding_buffer);
    CloseHandle(device);
    free(ip_addresses);
    return 0;
}
