#include "esp.hpp"
#include <iostream>
#include <iomanip>

// Debug function: print all entity info
void esp::debug_loop() {
	std::cout << "=== Entity Debug Mode ===" << std::endl;

	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	std::cout << "entity_list: 0x" << std::hex << entity_list << std::dec << std::endl;

	if (!entity_list) {
		std::cout << "ERROR: entity_list is NULL!" << std::endl;
		return;
	}

	uintptr_t localController = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController);
	uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);

	std::cout << "LocalController: 0x" << std::hex << localController << std::dec << std::endl;
	std::cout << "LocalPlayerPawn (direct): 0x" << std::hex << localPlayerPawn << std::dec << std::endl;

	if (!localPlayerPawn) {
		std::cout << "ERROR: localPlayerPawn is NULL!" << std::endl;
		return;
	}

	uint8_t myTeam = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
	int32_t myHealth = memory::Read<int32_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
	vec3 myPos = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);

	std::cout << "Local Player - Team: " << (int)myTeam << ", Health: " << myHealth << std::endl;
	std::cout << "Position: (" << myPos.x << ", " << myPos.y << ", " << myPos.z << ")" << std::endl;

	// ============================================
	// AUTO SEARCH: Find LocalPlayerPawn pointer in entity_list region
	// ============================================
	std::cout << "\n=== AUTO SEARCH: Finding LocalPlayerPawn in entity_list ===" << std::endl;
	std::cout << "Searching for pointer value 0x" << std::hex << localPlayerPawn << std::dec << "..." << std::endl;

	int foundLocations = 0;

	// Search in entity_list structure (first 0x100 bytes)
	std::cout << "\n[1] Searching entity_list + 0x00 to 0x100:" << std::endl;
	for (int offset = 0; offset < 0x100; offset += 8) {
		uintptr_t value = memory::Read<uintptr_t>(entity_list + offset);
		if (value == localPlayerPawn) {
			std::cout << "  FOUND at entity_list + 0x" << std::hex << offset << std::dec << std::endl;
			foundLocations++;
		}
	}

	// Get chunk pointers and search inside them
	std::cout << "\n[2] Searching inside chunk arrays:" << std::endl;
	for (int chunkOffset = 0; chunkOffset <= 0x40; chunkOffset += 8) {
		uintptr_t chunkPtr = memory::Read<uintptr_t>(entity_list + chunkOffset);
		if (chunkPtr && chunkPtr > 0x10000) {
			// Search in this chunk (check first 0x8000 bytes = 512 entries * 0x10 or more)
			for (int entryOffset = 0; entryOffset < 0x8000; entryOffset += 8) {
				uintptr_t value = memory::Read<uintptr_t>(chunkPtr + entryOffset);
				if (value == localPlayerPawn) {
					std::cout << "  FOUND at [entity_list+0x" << std::hex << chunkOffset
					          << "] + 0x" << entryOffset << std::dec << std::endl;
					std::cout << "    -> chunk addr: 0x" << std::hex << chunkPtr << std::dec << std::endl;
					std::cout << "    -> entry stride might be: 0x" << std::hex << entryOffset << std::dec << std::endl;
					foundLocations++;
				}
			}
		}
	}

	// Search with double dereference (entity_list -> chunk array -> chunk -> entry)
	std::cout << "\n[3] Searching with double dereference:" << std::endl;
	for (int firstOffset = 0; firstOffset <= 0x20; firstOffset += 8) {
		uintptr_t firstPtr = memory::Read<uintptr_t>(entity_list + firstOffset);
		if (!firstPtr || firstPtr < 0x10000) continue;

		// Check if this is an array of chunk pointers
		for (int chunkIdx = 0; chunkIdx < 8; chunkIdx++) {
			uintptr_t chunkPtr = memory::Read<uintptr_t>(firstPtr + chunkIdx * 8);
			if (!chunkPtr || chunkPtr < 0x10000) continue;

			// Search in this chunk
			for (int entryOffset = 0; entryOffset < 0x8000; entryOffset += 8) {
				uintptr_t value = memory::Read<uintptr_t>(chunkPtr + entryOffset);
				if (value == localPlayerPawn) {
					std::cout << "  FOUND at [[entity_list+0x" << std::hex << firstOffset
					          << "]+0x" << (chunkIdx * 8) << "]+0x" << entryOffset << std::dec << std::endl;
					std::cout << "    -> chunkIdx=" << chunkIdx << ", entryOffset=0x" << std::hex << entryOffset << std::dec << std::endl;

					// Calculate possible entry index
					for (int stride = 0x10; stride <= 0x120; stride += 0x8) {
						if (entryOffset % stride == 0) {
							int entryIdx = entryOffset / stride;
							std::cout << "    -> if stride=0x" << std::hex << stride
							          << ", entryIndex=" << std::dec << entryIdx << std::endl;
						}
					}
					foundLocations++;
				}
			}
		}
	}

	std::cout << "\n=== Total locations found: " << foundLocations << " ===" << std::endl;

	// ============================================
	// TEST NEW FORMULA: stride = 0x70, base = 0x08
	// ============================================
	std::cout << "\n=== Testing corrected formula ===" << std::endl;

	if (localController) {
		uint32_t pawnHandle = memory::Read<uint32_t>(localController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn);
		uint32_t index = pawnHandle & 0x7FFF;
		uint32_t chunkIndex = index >> 9;  // /512
		uint32_t indexInChunk = index & 0x1FF;  // %512

		std::cout << "Handle: 0x" << std::hex << pawnHandle << std::dec << std::endl;
		std::cout << "index=" << index << ", chunk=" << chunkIndex << ", indexInChunk=" << indexInChunk << std::endl;

		// Try different base offsets with stride 0x70
		for (int baseOffset = 0x08; baseOffset <= 0x20; baseOffset += 0x08) {
			uintptr_t chunkPtr = memory::Read<uintptr_t>(entity_list + baseOffset + 0x8 * chunkIndex);
			if (chunkPtr) {
				uintptr_t resolved = memory::Read<uintptr_t>(chunkPtr + 0x70 * indexInChunk);
				bool match = (resolved == localPlayerPawn);
				std::cout << "  base=0x" << std::hex << baseOffset << ": chunk=0x" << chunkPtr
				          << " -> resolved=0x" << resolved << (match ? " MATCH!" : "") << std::dec << std::endl;
			}
		}
	}

	// ============================================
	// SCAN ALL ENTITIES WITH CORRECT FORMULA
	// base=0x10, stride=0x70
	// ============================================
	std::cout << "\n=== Scanning entities with stride=0x70, base=0x10 ===" << std::endl;

	int entityCount = 0;

	for (uint32_t i = 1; i < 64; i++) {
		// Get controller: entity_list + 0x10 + 0x8 * chunkIndex, then + 0x70 * indexInChunk
		uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x10 + 0x8 * (i >> 9));
		if (!listEntry) continue;

		uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x70 * (i & 0x1FF));
		if (!entityController) continue;

		// Get pawn handle
		uint32_t pawnHandle = memory::Read<uint32_t>(entityController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn);
		if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

		uint32_t pawnIndex = pawnHandle & 0x7FFF;
		uint32_t pawnChunk = pawnIndex >> 9;
		uint32_t pawnInChunk = pawnIndex & 0x1FF;

		// Get pawn: same formula
		uintptr_t pawnListEntry = memory::Read<uintptr_t>(entity_list + 0x10 + 0x8 * pawnChunk);
		if (!pawnListEntry) continue;

		uintptr_t entity = memory::Read<uintptr_t>(pawnListEntry + 0x70 * pawnInChunk);
		if (!entity) continue;

		uint8_t team = memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		int32_t health = memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
		vec3 pos = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);

		std::cout << "[" << std::setw(2) << i << "] Pawn: 0x" << std::hex << entity << std::dec;
		std::cout << " | Team: " << (int)team << " | HP: " << health;
		std::cout << " | Pos: (" << std::fixed << std::setprecision(1) << pos.x << ", " << pos.y << ", " << pos.z << ")";
		if (entity == localPlayerPawn) std::cout << " <-- YOU";
		else if (team != myTeam && health > 0) std::cout << " <-- ENEMY";
		std::cout << std::endl;

		if (health > 0) entityCount++;
	}

	std::cout << "\n=== Total alive: " << entityCount << " ===" << std::endl;
}

void esp::loop() {

	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	while (true)
	{
		uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
		if (!localPlayerPawn) {
			Sleep(100);
			continue;
		}
		uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		player_position = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
			memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);

		std::vector<uintptr_t> buffer = {};

		// Expand to 1024
		for (uint32_t i = 0; i < 1024; i++)
		{
			uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);

			if (!listEntry) continue;

			uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x78 * (i & 0x1FF));

			if (!entityController) continue;

			uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

			if (!entityControllerPawn) continue;

			uintptr_t listEntry_2 = memory::Read<uintptr_t>(entity_list + 0x8 * ((entityControllerPawn & 0x7FFF) >> 9) + 0x10);

			if (!listEntry_2) continue;

			uintptr_t entity = memory::Read<uintptr_t>(listEntry_2 + 0x78 * (entityControllerPawn & 0x1FF));

			if (!entity) continue;

			uint8_t current_flag = memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
			int32_t health = memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
			bool is_diff_team = team != current_flag;

			if (is_diff_team && health > 0) {
				buffer.emplace_back(entity);
			}
		}
		entities = buffer;
		Sleep(10);
	}
}


void esp::auto_aim_bot()
{
	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	if (!entity_list)
	{
		return;
	}
	while (true) {
		if (!auto_aimBotEnabled)
		{
			Sleep(100);
			continue;
		}
		uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
		if (!localPlayerPawn) {
			Sleep(100);
			continue;
		}
		uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		vec3 localPlayerEyePos = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
			memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);

		for (uint32_t i = 0; i < 64; i++)
		{
			uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);
			if (!listEntry) continue;

			uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x78 * (i & 0x1FF));
			if (!entityController) continue;

			uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController +
				cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

			if (!entityControllerPawn) continue;

			uintptr_t listEntry_2 = memory::Read<uintptr_t>(entity_list + 0x8 * ((entityControllerPawn & 0x7FFF) >> 9) + 0x10);
			if (!listEntry_2) continue;

			uintptr_t entity = memory::Read<uintptr_t>(listEntry_2 + 0x78 * (entityControllerPawn & 0x1FF));
			if (!entity) continue;

			uint8_t current_flag = memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
			int32_t health = memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);

			bool is_diff_team = team != current_flag;
			if (!is_diff_team)
				continue;
			if (health <= 0)
				continue;
			vec3 entityEyePos = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin)
				+ memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
			vec3 relativeAngle = (entityEyePos - localPlayerEyePos).RelativeAngle();
			memory::Write<vec3>(modBase + cs2_dumper::offsets::client_dll::dwViewAngles, relativeAngle);
			Sleep(100);
			esp::auto_trigger();
		}
	}
}
void esp::aim_bot()
{
	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	if (!entity_list)
	{
		return;
	}

	uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
	if (!localPlayerPawn) {
		return;
	}
	uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
	vec3 localPlayerEyePos = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
		memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
	vec3 enemyPos;
	enemyPos.x = -1;
	enemyPos.y = -1;
	enemyPos.z = -1;
	vec2 headScreen;
	for (uint32_t i = 1; i < 64; i++)
	{
		uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);
		if (!listEntry) continue;

		uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x78 * (i & 0x1FF));
		if (!entityController) continue;

		uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController +
			cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

		if (!entityControllerPawn) continue;

		uintptr_t listEntry_2 = memory::Read<uintptr_t>(entity_list + 0x8 * ((entityControllerPawn & 0x7FFF) >> 9) + 0x10);
		if (!listEntry_2) continue;

		uintptr_t entity = memory::Read<uintptr_t>(listEntry_2 + 0x78 * (entityControllerPawn & 0x1FF));
		if (!entity) continue;

		uint8_t current_flag = memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		int32_t health = memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);

		bool is_diff_team = team != current_flag;
		if (is_diff_team && health > 0)
		{
			vec3 entityEyePos = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin)
				+ memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
			if (w2s(entityEyePos, headScreen, vm.m)) {
				if (headScreen.x > squareX && headScreen.y > squareY &&
					headScreen.x < squareX + squareSize &&
					headScreen.y < squareY + squareSize) {
					enemyPos = entityEyePos;
					break;
				}
			}
		}
	}
	if (enemyPos.x == -1 || enemyPos.y == -1 || enemyPos.z == -1) {
		return;
	}
	vec3 relativeAngle = (enemyPos - localPlayerEyePos).RelativeAngle();
	memory::Write<vec3>(modBase + cs2_dumper::offsets::client_dll::dwViewAngles, relativeAngle);
}

void esp::auto_trigger()
{
	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	if (!entity_list) return;

	uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
	if (!localPlayerPawn) return;

	uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);

	int32_t crosshair_entity_index = memory::Read<int32_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iIDEntIndex);
	if (crosshair_entity_index < 0)
	{
		return;
	}
	uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * ((crosshair_entity_index & 0x7FFF) >> 9) + 0x10);
	if (!listEntry) return;

	uintptr_t entity = memory::Read<uintptr_t>(listEntry + 0x78 * (crosshair_entity_index & 0x1FF));
	if (!entity) {
		return;
	}
	if (team == memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum))
		return;
	if (memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth) <= 0)
	{
		return;
	}
	Sleep(1);
	memory::Write<int>(modBase + cs2_dumper::buttons::attack, 65537);
	Sleep(20);
	memory::Write<int>(modBase + cs2_dumper::buttons::attack, 256);
	Sleep(20);
}

// D3D rendering disabled - using console debug mode
void esp::frame(bool isDrawAim)
{
	// Not used in debug mode
}

void esp::render(bool isDrawAim)
{
	// Not used in debug mode
}

bool esp::w2s(const vec3& world, vec2& screen, float m[16]) {


	vec4 clipCoords;
	clipCoords.x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
	clipCoords.y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
	clipCoords.z = world.x * m[8] + world.y * m[9] + world.z * m[10] + m[11];
	clipCoords.w = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];

	if (clipCoords.w < 0.1f)return false;

	vec3 ndc;

	ndc.x = clipCoords.x / clipCoords.w;
	ndc.y = clipCoords.y / clipCoords.w;

	screen.x = (WINDOW_W / 2.0f * ndc.x) + WINDOW_W / 2.0f;
	screen.y = -(WINDOW_H / 2.0f * ndc.y) + WINDOW_H / 2.0f;

	return true;
}

double esp::player_distance(const vec3& a, const vec3& b)
{
	return std::sqrt(
		std::pow(a.x - b.x, 2) +
		std::pow(a.y - b.y, 2) +
		std::pow(a.z - b.z, 2)
	);
}