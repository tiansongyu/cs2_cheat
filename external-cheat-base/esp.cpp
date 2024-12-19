#include "esp.hpp"
#include <iostream>

void esp::loop() {

	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	while (true)
	{
		uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
		BYTE team = memory::Read<BYTE>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		player_position = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
			memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);

		std::vector<uintptr_t> buffer = {};

		for (uint32_t i = 0; i < 64; i++)
		{
			uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x10 + ((8 * (i & 0x7fff) >> 9)));

			if (!listEntry) continue;

			uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x78 * (i & 0x1FF));

			if (!entityController) continue;

			uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

			if (!entityControllerPawn) continue;

			uintptr_t listEntry_2 = memory::Read<uintptr_t>(entity_list + 0x10 + ((8 * (entityControllerPawn & 0x7fff) >> 9)));

			uintptr_t entity = memory::Read<uintptr_t>(listEntry + 0x78 * (entityControllerPawn & 0x1ff));
			uintptr_t entity_2 = memory::Read<uintptr_t>(listEntry_2 + 0x78 * (entityControllerPawn & 0x1ff));

			if (!entity && !entity_2) continue;

			auto processEntity = [&](uintptr_t entity) {
				if (!entity) return;

				uint8_t current_flag = uint8_t(memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum));
				uint32_t health = uint8_t(memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth));
				bool is_diff_team = team != current_flag;

				if (is_diff_team && health > 0) {
					buffer.emplace_back(entity);
				}
				};

			processEntity(entity);
			processEntity(entity_2);
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
			return;
		}
		BYTE team = memory::Read<BYTE>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
		vec3 entity_position = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
			memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
		float closet_distance = -1;
		vec3 enemyPos;
		for (uint32_t i = 0; i < 64; i++)
		{
			uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + ((8 * (i & 0x7ff) >> 9) + 16));
			if (!listEntry) continue;

			uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 120 * (i & 0x1ff));
			if (!entityController)continue;

			uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController +
				cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

			if (!entityControllerPawn)continue;

			uintptr_t entity = memory::Read<uintptr_t>(listEntry + 120 * (entityControllerPawn & 0x1ff));
			uint8_t current_flag = uint8_t(memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum));
			uint32_t health = uint8_t(memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth));

			bool is_diff_team = team == current_flag ? false : true;
			if (!is_diff_team)
				continue;
			if (health <= 0)
				continue;
			vec3 entityEyePos = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin)
				+ memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
			vec3 relativeAngle = (entityEyePos - entity_position).RelativeAngle();
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
	BYTE team = memory::Read<BYTE>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
	vec3 entity_position = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
		memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
	float closet_distance = -1;
	vec3 enemyPos;
	enemyPos.x = -1;
	enemyPos.y = -1;
	enemyPos.z = -1;
	vec2 head;
	for (uint32_t i = 1; i < 32; i++)
	{
		uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + ((8 * (i & 0x7ff) >> 9) + 16));
		if (!listEntry) continue;

		uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 120 * (i & 0x1ff));
		if (!entityController)continue;

		uintptr_t entityControllerPawn = memory::Read<uintptr_t>(entityController +
			cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);

		if (!entityControllerPawn)continue;

		uintptr_t entity = memory::Read<uintptr_t>(listEntry + 120 * (entityControllerPawn & 0x1ff));
		uint8_t current_flag = uint8_t(memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum));
		uint32_t health = uint8_t(memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth));

		bool is_diff_team = team == current_flag ? false : true;
		if (entity && is_diff_team && health > 0)
		{
			vec3 entityEyePos = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin)
				+ memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
			if (w2s(entityEyePos, head, vm.m)) {
				if (head.x > squareX && head.y > squareY &&
					head.x < squareX + squareSize &&
					head.y < squareY + squareSize) {
					enemyPos = entityEyePos;
					break;
				}
			}
		}
	}
	if (enemyPos.x == -1 || enemyPos.y == -1 || enemyPos.z == -1) {
		return;
	}
	vec3 relativeAngle = (enemyPos - entity_position).RelativeAngle();
	memory::Write<vec3>(modBase + cs2_dumper::offsets::client_dll::dwViewAngles, relativeAngle);
}

void esp::auto_trigger()
{
	uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
	uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
	BYTE team = memory::Read<BYTE>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);

	int corsshair_entity_index = memory::Read<uintptr_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_iIDEntIndex);
	if (corsshair_entity_index < 0)
	{
		return;
	}
	uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x8 * (corsshair_entity_index >> 9) + 0x10);
	uintptr_t entity = memory::Read<uintptr_t>(listEntry + 120 * (corsshair_entity_index & 0x1ff));
	if (!entity) {
		return;
	}
	if (team == memory::Read<BYTE>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum))
		return;
	if (memory::Read<int>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth) <= 0)
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
		vec3 absOrigin = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
		vec3 eyePos = absOrigin + memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
		uint32_t current_health = uint32_t(memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth));
		uint32_t max_health = 100;
		vec2 head, feet;
		double distance = player_distance(absOrigin, player_position);
		if (w2s(absOrigin, head, vm.m))
		{
			if (w2s(eyePos, feet, vm.m))
			{
				float width = (head.y - feet.y);
				feet.x += width / 3.0f;
				head.x -= width / 3.0f;
				renderer::draw::box(D3DXVECTOR2{ head.x ,head.y }, D3DXVECTOR2{ feet.x ,feet.y }, distance < 1500.0f ? D3DCOLOR_XRGB(255, 0, 0) : D3DCOLOR_XRGB(0, 255, 0));
            // 绘制血量矩形
            float health_percentage = static_cast<float>(current_health) / static_cast<float>(max_health);
            float health_bar_height = width * health_percentage;
            float health_bar_x = head.x - 5.0f; // 血量矩形的X坐标，位于正方形左侧
            float health_bar_y = feet.y + (width - health_bar_height); // 血量矩形的Y坐标，随血量变化

            // 绘制背景矩形
            renderer::draw::box(D3DXVECTOR2{ health_bar_x - 1, head.y - 1 }, D3DXVECTOR2{ health_bar_x + 3, feet.y + 1 }, D3DCOLOR_XRGB(0, 0, 0));

            // 绘制血量矩形
            renderer::draw::box(D3DXVECTOR2{ health_bar_x, health_bar_y }, D3DXVECTOR2{ health_bar_x + 2, head.y }, D3DCOLOR_XRGB(0, 255, 0));
      
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