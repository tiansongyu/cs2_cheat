#pragma once

#include "../memory/memory.h"
#include <d3d9.h>
#include <d3dx9.h>

#include "../math/vector.h"

extern uint32_t WIDTH;
extern uint32_t HEIGHT;
extern uint32_t WINDOW_W;
extern uint32_t WINDOW_H;
namespace renderer
{
	inline IDirect3D9* pD3D = nullptr;
	inline IDirect3DDevice9* pDevice = nullptr;
	inline ID3DXLine* mLine = nullptr;

	inline MSG msg;
	inline bool running = true;



	bool init(HWND hwnd);
	void destroy();

	void frame();
	void handle_events();

	namespace draw
	{
		void line(D3DXVECTOR2 p1, D3DXVECTOR2 p2, D3DCOLOR color);
		void box(D3DXVECTOR2 tl, D3DXVECTOR2 br, D3DCOLOR color);
	}

};

namespace window
{
	HWND InitWindow(HINSTANCE hInstance);
	inline LRESULT CALLBACK WinProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		LRESULT Result = 0;
		switch (message)
		{
		case WM_DESTROY:
		{
			renderer::running = false;
			break;
		}
		case WM_CLOSE:
		{
			renderer::running = false;
			break;
		}
		default:
		{
			Result = DefWindowProc(hWnd, message, wParam, lParam);
			break;
		}

		}
		return Result;
	}


}


