#include "esp.hpp"
#include <iostream>

void esp::loop() {

	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	while (true)
	{
		uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
		if (!localPlayerPawn) {
			Sleep(100);
			continue;
		}
		// 修复: 直接读取正确的类型 uint8_t
		uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		player_position = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
			memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);

		std::vector<uintptr_t> buffer = {};

		for (uint32_t i = 0; i < 64; i++)
		{
			// 修复: 统一使用正确的 EntityList 遍历公式
			uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);

			if (!listEntry) continue;

			uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x78 * (i & 0x1FF));

			if (!entityController) continue;

			uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

			if (!entityControllerPawn) continue;

			// 修复: 使用 entityControllerPawn 的索引获取正确的 listEntry_2
			uintptr_t listEntry_2 = memory::Read<uintptr_t>(entity_list + 0x8 * ((entityControllerPawn & 0x7FFF) >> 9) + 0x10);

			if (!listEntry_2) continue;

			// 修复: entity 应该使用 listEntry_2 而不是 listEntry
			uintptr_t entity = memory::Read<uintptr_t>(listEntry_2 + 0x78 * (entityControllerPawn & 0x1FF));

			if (!entity) continue;

			// 修复: 直接读取正确的类型
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
			continue;  // 修复: 使用 continue 而不是 return，避免线程退出
		}
		// 修复: 直接读取正确的类型 uint8_t
		uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		vec3 localPlayerEyePos = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
			memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);

		for (uint32_t i = 0; i < 64; i++)
		{
			// 修复: 统一使用正确的 EntityList 遍历公式
			uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);
			if (!listEntry) continue;

			uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x78 * (i & 0x1FF));
			if (!entityController) continue;

			uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController +
				cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

			if (!entityControllerPawn) continue;

			// 修复: 使用 listEntry_2 获取正确的 entity
			uintptr_t listEntry_2 = memory::Read<uintptr_t>(entity_list + 0x8 * ((entityControllerPawn & 0x7FFF) >> 9) + 0x10);
			if (!listEntry_2) continue;

			uintptr_t entity = memory::Read<uintptr_t>(listEntry_2 + 0x78 * (entityControllerPawn & 0x1FF));
			if (!entity) continue;

			// 修复: 直接读取正确的类型
			uint8_t current_flag = memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
			int32_t health = memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);

			bool is_diff_team = team != current_flag;
			if (!is_diff_team)
				continue;
			if (health <= 0)
				continue;
			// 修复: 使用 entity 的 m_vecViewOffset 而不是 localPlayerPawn 的
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
	// 修复: 直接读取正确的类型 uint8_t
	uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
	vec3 localPlayerEyePos = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
		memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
	vec3 enemyPos;
	enemyPos.x = -1;
	enemyPos.y = -1;
	enemyPos.z = -1;
	vec2 headScreen;
	for (uint32_t i = 1; i < 64; i++)  // 修复: 遍历64个玩家而不是32个
	{
		// 修复: 统一使用正确的 EntityList 遍历公式
		uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);
		if (!listEntry) continue;

		uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x78 * (i & 0x1FF));
		if (!entityController) continue;

		uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController +
			cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

		if (!entityControllerPawn) continue;

		// 修复: 使用 listEntry_2 获取正确的 entity
		uintptr_t listEntry_2 = memory::Read<uintptr_t>(entity_list + 0x8 * ((entityControllerPawn & 0x7FFF) >> 9) + 0x10);
		if (!listEntry_2) continue;

		uintptr_t entity = memory::Read<uintptr_t>(listEntry_2 + 0x78 * (entityControllerPawn & 0x1FF));
		if (!entity) continue;

		// 修复: 直接读取正确的类型
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

	// 修复: 直接读取正确的类型 uint8_t
	uint8_t team = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);

	// 修复: m_iIDEntIndex 是 CEntityIndex 类型，应该读取 int32_t
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
	// 修复: 直接读取正确的类型
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

void esp::frame(bool isDrawAim)
{
	renderer::pDevice->Clear(0, 0, D3DCLEAR_TARGET, NULL, 1.f, 0);
	renderer::pDevice->BeginScene();

	render(isDrawAim);

	renderer::pDevice->EndScene();
	renderer::pDevice->Present(0, 0, 0, 0);

}

void  esp::render(bool isDrawAim)
{
	vm = memory::Read<viewMatrix>(modBase + cs2_dumper::offsets::client_dll::dwViewMatrix);

	squareSize = WIDTH / 13; // 正方形的边长
	int centerX = WIDTH / 2;
	int centerY = HEIGHT / 2;
	squareX = centerX - (squareSize / 2);
	squareY = centerY - (squareSize / 2);

	// 绘制蓝色正方形
	if (isDrawAim) {
		renderer::draw::box(D3DXVECTOR2{ static_cast<float>(squareX), static_cast<float>(squareY) },
			D3DXVECTOR2{ static_cast<float>(squareX + squareSize), static_cast<float>(squareY + squareSize) },
			D3DCOLOR_XRGB(0, 0, 255));
	}

	for (uintptr_t entity : entities)
	{
		// absOrigin 是脚的位置 (玩家原点)
		vec3 feetPos = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
		// eyePos 是头/眼睛的位置 (原点 + 视角偏移)
		vec3 headPos = feetPos + memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
		// 修复: 直接读取正确的类型 int32_t
		int32_t current_health = memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
		int32_t max_health = 100;
		// 修复: 变量命名与实际含义一致
		vec2 feetScreen, headScreen;
		double distance = player_distance(feetPos, player_position);
		// 修复: feetPos -> feetScreen, headPos -> headScreen
		if (w2s(feetPos, feetScreen, vm.m))
		{
			if (w2s(headPos, headScreen, vm.m))
			{
				// 在屏幕上，脚的Y坐标比头的Y坐标大（屏幕Y轴向下）
				float boxHeight = feetScreen.y - headScreen.y;
				float boxWidth = boxHeight / 3.0f;

				// 计算方框的左上角和右下角
				float left = headScreen.x - boxWidth;
				float right = headScreen.x + boxWidth;
				float top = headScreen.y;
				float bottom = feetScreen.y;

				// 绘制ESP方框
				renderer::draw::box(D3DXVECTOR2{ left, top }, D3DXVECTOR2{ right, bottom },
					distance < 1500.0f ? D3DCOLOR_XRGB(255, 0, 0) : D3DCOLOR_XRGB(0, 255, 0));

				// 绘制血量矩形
				float health_percentage = static_cast<float>(current_health) / static_cast<float>(max_health);
				float health_bar_height = boxHeight * health_percentage;
				float health_bar_x = left - 5.0f; // 血量矩形的X坐标，位于方框左侧
				float health_bar_top = bottom - health_bar_height; // 血量矩形的顶部Y坐标

				// 绘制背景矩形
				renderer::draw::box(D3DXVECTOR2{ health_bar_x - 1, top - 1 }, D3DXVECTOR2{ health_bar_x + 3, bottom + 1 }, D3DCOLOR_XRGB(0, 0, 0));

				// 绘制血量矩形 (从底部向上填充)
				renderer::draw::box(D3DXVECTOR2{ health_bar_x, health_bar_top }, D3DXVECTOR2{ health_bar_x + 2, bottom }, D3DCOLOR_XRGB(0, 255, 0));
			}
		}
	}
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