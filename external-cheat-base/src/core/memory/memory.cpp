#include "memory.hpp"
#include <cstring>

namespace
{
	HANDLE gHandle = nullptr;

	bool RpmReadRaw(uintptr_t address, void* buffer, size_t size)
	{
		return ReadProcessMemory(gHandle, (LPCVOID)address, buffer, size, nullptr) != 0;
	}

	bool RpmWriteRaw(uintptr_t address, const void* buffer, size_t size)
	{
		return WriteProcessMemory(gHandle, (LPVOID)address, buffer, size, nullptr) != 0;
	}

	bool RpmReadBatch(memory::BatchEntry* entries, size_t count)
	{
		bool allOk = true;
		for (size_t i = 0; i < count; ++i)
		{
			if (!entries[i].out || entries[i].size == 0) { allOk = false; continue; }
			if (!ReadProcessMemory(gHandle, (LPCVOID)(uintptr_t)entries[i].address,
			                       entries[i].out, entries[i].size, nullptr))
			{
				memset(entries[i].out, 0, entries[i].size);
				allOk = false;
			}
		}
		return allOk;
	}

	uintptr_t RpmGetProcID(const wchar_t* process)
	{
		HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

		PROCESSENTRY32 proc;
		proc.dwSize = sizeof(PROCESSENTRY32);
		Process32First(handle, &proc);

		do
		{
			if (!wcscmp(process, proc.szExeFile))
			{
				CloseHandle(handle);
				memory::pID = proc.th32ProcessID;
				gHandle = OpenProcess(PROCESS_ALL_ACCESS, NULL, memory::pID);
				return proc.th32ProcessID;
			}
		} while (Process32Next(handle, &proc));

		CloseHandle(handle);
		return 0;
	}

	uintptr_t RpmGetModuleBase(uintptr_t procID, const wchar_t* module)
	{
		HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procID);

		MODULEENTRY32 mod;
		mod.dwSize = sizeof(MODULEENTRY32);
		Module32First(handle, &mod);

		do
		{
			if (!wcscmp(module, mod.szModule))
			{
				CloseHandle(handle);
				return (uintptr_t)mod.modBaseAddr;
			}
		} while (Module32Next(handle, &mod));

		CloseHandle(handle);
		return 0;
	}

	const memory::Backend kRpmBackend = {
		&RpmReadRaw,
		&RpmWriteRaw,
		&RpmGetProcID,
		&RpmGetModuleBase,
		&RpmReadBatch,
	};

	const memory::Backend* gBackend = &kRpmBackend;
}

void memory::SetBackend(const Backend* backend)
{
	gBackend = backend ? backend : &kRpmBackend;
}

uintptr_t memory::GetProcID(const wchar_t* process)
{
	return gBackend->get_proc_id(process);
}

uintptr_t memory::GetModuleBaseAddress(uintptr_t procID, const wchar_t* module)
{
	return gBackend->get_module_base(procID, module);
}

bool memory::ReadRaw(uintptr_t address, void* buffer, size_t size)
{
	return gBackend->read_raw(address, buffer, size);
}

bool memory::WriteRaw(uintptr_t address, const void* buffer, size_t size)
{
	return gBackend->write_raw(address, buffer, size);
}

bool memory::ReadBatch(BatchEntry* entries, size_t count)
{
	return gBackend->read_batch(entries, count);
}
