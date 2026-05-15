#define NOMINMAX

#include "misc.hpp"
#include "esp.hpp"
#include "menu.hpp"
#include <Windows.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

constexpr WORD SC_W     = 0x11;
constexpr WORD SC_A     = 0x1E;
constexpr WORD SC_S     = 0x1F;
constexpr WORD SC_D     = 0x20;
constexpr WORD SC_SPACE = 0x39;

constexpr uint32_t FL_ONGROUND = 1u << 0;

enum CounterKey { CK_W = 0, CK_A = 1, CK_S = 2, CK_D = 3, CK_COUNT = 4 };
constexpr WORD COUNTER_SC[CK_COUNT] = { SC_W, SC_A, SC_S, SC_D };

bool counterKeyDown[CK_COUNT] = { false, false, false, false };
bool synthSpaceHeld = false;

void sendScan(WORD scanCode, bool down)
{
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scanCode;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(INPUT));
}

void pressCounter(int idx)
{
    if (!counterKeyDown[idx]) {
        sendScan(COUNTER_SC[idx], true);
        counterKeyDown[idx] = true;
    }
}

void releaseCounter(int idx)
{
    if (counterKeyDown[idx]) {
        sendScan(COUNTER_SC[idx], false);
        counterKeyDown[idx] = false;
    }
}

void releaseAllCounters()
{
    for (int i = 0; i < CK_COUNT; ++i) releaseCounter(i);
}

void runQuickStop()
{
    if (!menu::quickStopEnabled ||
        !(GetAsyncKeyState(menu::quickStopKey) & 0x8000) ||
        !esp::localPlayer.isValid)
    {
        releaseAllCounters();
        return;
    }

    const vec3& v = esp::localPlayer.velocity;
    float yaw = esp::localPlayer.viewAngle.y * static_cast<float>(M_PI) / 180.0f;
    float c = std::cos(yaw);
    float s = std::sin(yaw);

    // Project world-space velocity onto the player's local axes (Source convention)
    float forwardSpeed = v.x * c + v.y * s;
    float rightSpeed   = v.x * s - v.y * c;

    float threshold = menu::quickStopThreshold;

    if (forwardSpeed >  threshold) { pressCounter(CK_S); releaseCounter(CK_W); }
    else if (forwardSpeed < -threshold) { pressCounter(CK_W); releaseCounter(CK_S); }
    else { releaseCounter(CK_W); releaseCounter(CK_S); }

    if (rightSpeed >  threshold) { pressCounter(CK_A); releaseCounter(CK_D); }
    else if (rightSpeed < -threshold) { pressCounter(CK_D); releaseCounter(CK_A); }
    else { releaseCounter(CK_A); releaseCounter(CK_D); }
}

void runBhop()
{
    bool keyDown = menu::bhopEnabled &&
                   (GetAsyncKeyState(menu::bhopKey) & 0x8000) != 0;

    if (!keyDown || !esp::localPlayer.isValid) {
        if (synthSpaceHeld) { sendScan(SC_SPACE, false); synthSpaceHeld = false; }
        return;
    }

    bool onGround = (esp::localPlayer.flags & FL_ONGROUND) != 0;

    // Hold Space while on ground, release while airborne. The game treats the
    // edge from released-to-held as a fresh jump command, so leaving the
    // ground reliably re-arms the next jump on landing.
    if (onGround) {
        if (!synthSpaceHeld) { sendScan(SC_SPACE, true); synthSpaceHeld = true; }
    } else {
        if (synthSpaceHeld)  { sendScan(SC_SPACE, false); synthSpaceHeld = false; }
    }
}

} // anonymous namespace

void misc::update()
{
    runQuickStop();
    runBhop();
}

void misc::releaseAll()
{
    releaseAllCounters();
    if (synthSpaceHeld) { sendScan(SC_SPACE, false); synthSpaceHeld = false; }
}
