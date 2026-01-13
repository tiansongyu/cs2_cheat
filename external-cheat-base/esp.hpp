#pragma once

#include<vector>

#include"memory/memory.h"
#include"client_dll.hpp"
#include"offsets.hpp"
#include "buttons.hpp"
#include "math/vector.h"

extern uint32_t WIDTH;
extern uint32_t HEIGHT;
extern uint32_t WINDOW_W;
extern uint32_t WINDOW_H;

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
	inline int squareX;
	inline int squareY;
	inline int squareSize;

	// 调试模式 - 打印实体信息
	void debug_loop();

	void frame(bool isDrawAim);
	void loop();
	void render(bool isDrawAim);
	void aim_bot();
	void auto_aim_bot();
	void auto_trigger();

	bool w2s(const vec3& world, vec2& screen, float m[16]);

	double player_distance(const vec3& a, const vec3& b);
}