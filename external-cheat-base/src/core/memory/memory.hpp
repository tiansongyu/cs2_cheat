#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstddef>

namespace memory
{
	inline uintptr_t pID;

	struct BatchEntry
	{
		uint64_t address;
		uint32_t size;
		uint32_t output_offset;
		void*    out;          // user-mode destination (filled by ReadBatch)
	};

	struct Backend
	{
		bool (*read_raw)(uintptr_t address, void* buffer, size_t size);
		bool (*write_raw)(uintptr_t address, const void* buffer, size_t size);
		uintptr_t (*get_proc_id)(const wchar_t* process);
		uintptr_t (*get_module_base)(uintptr_t procID, const wchar_t* module);
		bool (*read_batch)(BatchEntry* entries, size_t count);
	};

	void SetBackend(const Backend* backend);

	// Driver backend (talks to cs2cheat-driver via DeviceIoControl).
	// Returns false if the driver is not loaded; caller falls back to RPM.
	bool InitDriverBackend();
	void ShutdownDriverBackend();
	extern const Backend kDriverBackend;

	uintptr_t GetProcID(const wchar_t* process);
	uintptr_t GetModuleBaseAddress(uintptr_t procID, const wchar_t* module);

	bool ReadRaw(uintptr_t address, void* buffer, size_t size);
	bool WriteRaw(uintptr_t address, const void* buffer, size_t size);
	bool ReadBatch(BatchEntry* entries, size_t count);

	template <typename T> T Read(uintptr_t address)
	{
		T ret;
		ReadRaw(address, &ret, sizeof(T));
		return ret;
	}
	template <typename T> bool Write(uintptr_t address, T value)
	{
		return WriteRaw(address, &value, sizeof(T));
	}
}
