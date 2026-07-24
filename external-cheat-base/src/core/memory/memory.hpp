#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <TlHelp32.h>
#include <cstring>
#include <type_traits>

namespace memory
{
	inline HANDLE gHandle = nullptr;
	inline HANDLE gWriteHandle = nullptr;

	inline uintptr_t pID = 0;
	inline bool gWritesAllowed = false;

	uintptr_t GetProcID(const wchar_t* process);
	uintptr_t GetModuleBaseAddress(uintptr_t procID, const wchar_t* module);
	void Close();
	void SetWritesAllowed(bool allowed);
	bool WritesAllowed();
	bool TryWriteRaw(uintptr_t address, const void* buffer, size_t size);

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
		static_assert(std::is_trivially_copyable_v<T>,
			"Remote memory writes require a trivially copyable type");
		return TryWriteRaw(address, &value, sizeof(T));
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
