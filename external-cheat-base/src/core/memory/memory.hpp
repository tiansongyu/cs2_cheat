#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <cstring>
#include <type_traits>

namespace memory
{
	inline HANDLE gHandle;

	inline uintptr_t pID;
	

	uintptr_t GetProcID(const wchar_t* process);
	uintptr_t GetModuleBaseAddress(uintptr_t procID, const wchar_t* module);

	template <typename T> bool TryRead(uintptr_t address, T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"Remote memory reads require a trivially copyable type");

		value = T{};
		if (!gHandle || address == 0) {
			return false;
		}

		SIZE_T bytesRead = 0;
		if (!ReadProcessMemory(
				gHandle,
				reinterpret_cast<LPCVOID>(address),
				&value,
				sizeof(T),
				&bytesRead) ||
			bytesRead != sizeof(T)) {
			value = T{};
			return false;
		}

		return true;
	}

	template <typename T> T Read(uintptr_t address)
	{
		T value{};
		TryRead(address, value);
		return value;
	}

	template <typename T> bool Write(uintptr_t address, T value)
	{
		if (!gHandle || address == 0) {
			return false;
		}

		SIZE_T bytesWritten = 0;
		return WriteProcessMemory(
			gHandle,
			reinterpret_cast<LPVOID>(address),
			&value,
			sizeof(T),
			&bytesWritten) &&
			bytesWritten == sizeof(T);
	}

	inline bool ReadRaw(uintptr_t address, void* buffer, size_t size)
	{
		if (!buffer || size == 0) {
			return false;
		}

		std::memset(buffer, 0, size);
		if (!gHandle || address == 0) {
			return false;
		}

		SIZE_T bytesRead = 0;
		if (!ReadProcessMemory(
				gHandle,
				reinterpret_cast<LPCVOID>(address),
				buffer,
				size,
				&bytesRead) ||
			bytesRead != size) {
			std::memset(buffer, 0, size);
			return false;
		}

		return true;
	}


}
