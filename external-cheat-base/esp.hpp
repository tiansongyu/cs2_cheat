#pragma once

#include<vector>

#include"renderer/renderer.h"
#include"client_dll.hpp"
#include"offset.hpp"
#include "button.hpp"

struct viewMatrix
{
	float m[16];
};

namespace esp
{
	inline std::vector<uintptr_t> entities;
	inline viewMatrix vm = {};

	inline vec3 player_position{};
	inline uintptr_t pID;
	inline uintptr_t modBase;
	inline uintptr_t modEngine2;
	inline bool auto_aimBotEnabled = false;
	void frame();
	void loop();
	void render();
	void aim_bot();
	void auto_aim_bot();
	void auto_trigger();

	bool w2s(const vec3& world, vec2& screen, float m[16]);

	double player_distance(const vec3& a, const vec3& b);
}