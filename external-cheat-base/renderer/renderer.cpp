#include "renderer.h"

HWND window::InitWindow(HINSTANCE hInstance)
{
	HWND hwnd = NULL;

	// fill out the window class structure

	WNDCLASS wc = { };

	wc.lpfnWndProc = WinProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"overlay";
	
	if (!RegisterClass(&wc))
		return 0;

	hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED, L"Overlay", L"Overlay",
		WS_VISIBLE | WS_POPUP,
		0, 0, WIDTH, HEIGHT,
		NULL, NULL, hInstance, NULL);

	SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
	SetLayeredWindowAttributes(hwnd, 0, RGB(0, 0, 0), LWA_COLORKEY);

	// 设置窗口样式，使其不响应鼠标输入
	LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
	SetWindowLongPtr(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED | WS_EX_TRANSPARENT);


	return hwnd;
}
LRESULT CALLBACK WinProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
		// 忽略鼠标点击事件
		return 0;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}
bool renderer::init(HWND hwnd)
{
	pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!pD3D)
		return false;
	// set the presentation parameters
	D3DPRESENT_PARAMETERS d3dpp = {};
	ZeroMemory(&d3dpp, sizeof(d3dpp));
	d3dpp.BackBufferWidth = WIDTH;
	d3dpp.BackBufferHeight = HEIGHT;
	d3dpp.BackBufferCount = 1;
	d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.Windowed = true;
	d3dpp.EnableAutoDepthStencil = true;
	d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
	d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
	

	if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL, hwnd,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING,
		&d3dpp, &pDevice)))
		return false;

	if (FAILED(D3DXCreateLine(pDevice, &mLine)))
		return false;

	return true;

}

void renderer::destroy()
{
	if (pDevice)
	{
		pDevice->Release();
		pDevice = NULL;
	}

	if (pD3D)
	{
		pD3D->Release();
		pD3D = NULL;
	}
}

void renderer::draw::line(D3DXVECTOR2 p1, D3DXVECTOR2 p2, D3DCOLOR color)
{
	const D3DXVECTOR2 list[] = {
		p1,
		p2
	};

	mLine->Begin();
	mLine->Draw(list, 2, color);
	mLine->End();

}

void renderer::draw::box(D3DXVECTOR2 tl, D3DXVECTOR2 br, D3DCOLOR color)
{
	const D3DXVECTOR2 list[] = {
		tl,
		D3DXVECTOR2(tl.x,br.y),
		br,
		D3DXVECTOR2(br.x,tl.y),
		tl
	};
	mLine->SetWidth(3.0f);
	mLine->Begin();
	mLine->Draw(list, 5, color);
	mLine->End();
}

