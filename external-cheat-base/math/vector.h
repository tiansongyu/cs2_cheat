#pragma once

#include <cmath>
#include <numbers>

struct vec4
{
	float w, x, y, z;
};

struct vec3
{
	float x, y, z;
	vec3 operator+(vec3 other)
	{
		return { this->x + other.x, this->y + other.y, this->z + other.z };
	}
	vec3 operator-(vec3 other)
	{
		return { this->x - other.x, this->y - other.y, this->z - other.z };
	}

	vec3 RelativeAngle()
	{
		return {
			std::atan2(-z, std::hypot(x, y)) * (180.0f / std::numbers::pi_v<float>),
			std::atan2(y, x) * (180.0f / std::numbers::pi_v<float>),
			0.0f
		};
	}

};

struct vec2
{
	float x, y;
};
