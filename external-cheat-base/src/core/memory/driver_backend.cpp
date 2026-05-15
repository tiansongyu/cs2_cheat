#include "memory.hpp"
#include "../../../shared/ioctl_protocol.h"

#include <Windows.h>
#include <memory>
#include <cstring>

namespace
{
    HANDLE gDriver = INVALID_HANDLE_VALUE;

    bool DrvReadRaw(uintptr_t address, void* buffer, size_t size)
    {
        if (gDriver == INVALID_HANDLE_VALUE || size == 0 || size > CS2CHEAT_READ_MAX)
            return false;

        CS2CHEAT_READ_REQUEST req{};
        req.pid = static_cast<uint32_t>(memory::pID);
        req.address = static_cast<uint64_t>(address);
        req.size = static_cast<uint32_t>(size);

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            gDriver,
            IOCTL_CS2CHEAT_READ_MEMORY,
            &req, sizeof(req),
            buffer, static_cast<DWORD>(size),
            &bytesReturned,
            nullptr);
        return ok && bytesReturned == size;
    }

    bool DrvWriteRaw(uintptr_t address, const void* buffer, size_t size)
    {
        if (gDriver == INVALID_HANDLE_VALUE || size == 0 || size > CS2CHEAT_READ_MAX)
            return false;

        const size_t headerSize = offsetof(CS2CHEAT_WRITE_REQUEST, data);
        const size_t totalSize = headerSize + size;

        std::unique_ptr<uint8_t[]> packet(new uint8_t[totalSize]);
        auto* req = reinterpret_cast<CS2CHEAT_WRITE_REQUEST*>(packet.get());
        req->pid = static_cast<uint32_t>(memory::pID);
        req->address = static_cast<uint64_t>(address);
        req->size = static_cast<uint32_t>(size);
        memcpy(req->data, buffer, size);

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            gDriver,
            IOCTL_CS2CHEAT_WRITE_MEMORY,
            packet.get(), static_cast<DWORD>(totalSize),
            nullptr, 0,
            &bytesReturned,
            nullptr);
        return ok != FALSE;
    }

    uintptr_t DrvGetProcID(const wchar_t* process)
    {
        if (gDriver == INVALID_HANDLE_VALUE || !process)
            return 0;

        CS2CHEAT_GET_PID_REQUEST req{};
        wcsncpy_s(req.name, process, CS2CHEAT_NAME_MAX - 1);

        CS2CHEAT_GET_PID_RESPONSE resp{};
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            gDriver,
            IOCTL_CS2CHEAT_GET_PID_BY_NAME,
            &req, sizeof(req),
            &resp, sizeof(resp),
            &bytesReturned,
            nullptr);

        if (!ok || bytesReturned != sizeof(resp) || resp.pid == 0)
            return 0;

        memory::pID = resp.pid;
        return resp.pid;
    }

    uintptr_t DrvGetModuleBase(uintptr_t procID, const wchar_t* module)
    {
        if (gDriver == INVALID_HANDLE_VALUE || !module)
            return 0;

        CS2CHEAT_GET_MODULE_REQUEST req{};
        req.pid = static_cast<uint32_t>(procID);
        wcsncpy_s(req.module, module, CS2CHEAT_NAME_MAX - 1);

        CS2CHEAT_GET_MODULE_RESPONSE resp{};
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            gDriver,
            IOCTL_CS2CHEAT_GET_MODULE_BASE,
            &req, sizeof(req),
            &resp, sizeof(resp),
            &bytesReturned,
            nullptr);

        if (!ok || bytesReturned != sizeof(resp))
            return 0;
        return static_cast<uintptr_t>(resp.base);
    }

    bool DrvReadBatch(memory::BatchEntry* entries, size_t count)
    {
        if (gDriver == INVALID_HANDLE_VALUE) return false;
        if (count == 0) return true;
        if (count > CS2CHEAT_BATCH_MAX_ENTRIES) return false;

        // Pack request: header + entries; allocate output buffer to hold all
        // results contiguously, then scatter to caller's BatchEntry::out.
        const size_t headerSize = offsetof(CS2CHEAT_BATCH_READ_REQUEST, entries);
        const size_t reqSize = headerSize + count * sizeof(CS2CHEAT_BATCH_ENTRY);

        std::unique_ptr<uint8_t[]> reqBuf(new uint8_t[reqSize]);
        auto* req = reinterpret_cast<CS2CHEAT_BATCH_READ_REQUEST*>(reqBuf.get());
        req->pid = static_cast<uint32_t>(memory::pID);
        req->entry_count = static_cast<uint32_t>(count);
        req->reserved = 0;

        uint32_t cursor = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!entries[i].out || entries[i].size == 0) return false;
            req->entries[i].address = entries[i].address;
            req->entries[i].size = entries[i].size;
            req->entries[i].output_offset = cursor;
            req->entries[i].status = 0;
            entries[i].output_offset = cursor;
            cursor += entries[i].size;
            if (cursor > CS2CHEAT_READ_MAX) return false;
        }
        req->output_size = cursor;

        std::unique_ptr<uint8_t[]> outBuf(new uint8_t[cursor]);

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            gDriver,
            IOCTL_CS2CHEAT_BATCH_READ,
            reqBuf.get(), static_cast<DWORD>(reqSize),
            outBuf.get(),  static_cast<DWORD>(cursor),
            &bytesReturned,
            nullptr);
        if (!ok || bytesReturned != cursor) return false;

        for (size_t i = 0; i < count; ++i) {
            memcpy(entries[i].out, outBuf.get() + entries[i].output_offset, entries[i].size);
        }
        return true;
    }
}

namespace memory
{
    const Backend kDriverBackend = {
        &DrvReadRaw,
        &DrvWriteRaw,
        &DrvGetProcID,
        &DrvGetModuleBase,
        &DrvReadBatch,
    };

    bool InitDriverBackend()
    {
        if (gDriver != INVALID_HANDLE_VALUE)
            return true;

        gDriver = CreateFileW(
            CS2CHEAT_USER_PATH,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        return gDriver != INVALID_HANDLE_VALUE;
    }

    void ShutdownDriverBackend()
    {
        if (gDriver != INVALID_HANDLE_VALUE)
        {
            CloseHandle(gDriver);
            gDriver = INVALID_HANDLE_VALUE;
        }
    }
}
