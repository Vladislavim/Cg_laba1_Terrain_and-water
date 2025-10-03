#include "Window.h"
#include "Scene.h"
#include <windowsx.h>
#include <windows.h>
#include <cstdio>
#include <iostream>
#include <chrono>

using namespace std;
using namespace graphics;
using namespace window;

static const LPCWSTR	appName = L"adskii terrain";
static const int		WINDOW_HEIGHT = 1080 / 1.5;
static const int		WINDOW_WIDTH = 1920 / 1.5;
static const bool		FULL_SCREEN = false;
static Scene* pScene = nullptr;
static int				lastMouseX = -1;
static int				lastMouseY = -1;

static void KeyUp(UINT key) {
	switch (key) {
	case VK_ESCAPE:
		PostQuitMessage(0);
		return;
	}
}

static void KeyDown(UINT key) {
	switch (key) {
	case VK_SPACE:
	case _W:
	case _S:
	case _A:
	case _D:
	case _Q:
	case _Z:
	case _1:
	case _2:
	case _T:
	case _L:
		if (pScene) pScene->HandleKeyboardInput(key);
		break;
	}
}

static void HandleMouseMove(LPARAM lp) {
	int x = GET_X_LPARAM(lp);
	int y = GET_Y_LPARAM(lp);

	if (lastMouseX != -1) {
		int moveX = lastMouseX - x;
		moveX = moveX > 20 ? 20 : (moveX < -20 ? -20 : moveX);
		int moveY = lastMouseY - y;
		moveY = moveY > 20 ? 20 : (moveY < -20 ? -20 : moveY);
		if (pScene) pScene->HandleMouseInput(moveX, moveY);
	}
	lastMouseX = x;
	lastMouseY = y;
}

static LRESULT CALLBACK WndProc(HWND win, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_DESTROY:
	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;
	case WM_KEYUP:
		KeyUp((UINT)wp);
		break;
	case WM_KEYDOWN:
		KeyDown((UINT)wp);
		break;
	case WM_MOUSEMOVE:
		HandleMouseMove(lp);
		break;
	default:
		return DefWindowProc(win, msg, wp, lp);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, PSTR cmdLine, int cmdShow) {
	AllocConsole();
	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	SetConsoleTitleW(L"Debug Console");
	printf("=== Debug console attached ===\n");

	try {
		Window WIN(appName, WINDOW_HEIGHT, WINDOW_WIDTH, WndProc, FULL_SCREEN);
		Device DEV(WIN.GetWindow(), WIN.Height(), WIN.Width());
		Scene S(WIN.Height(), WIN.Width(), &DEV);
		pScene = &S;

		// === FPS state ===
		using clock = std::chrono::high_resolution_clock;
		auto lastTitleTime = clock::now();
		int frames = 0;

		MSG msg;
		ZeroMemory(&msg, sizeof(MSG));

		for (;;) {
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT) {
					pScene = nullptr;
					return 0;
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			if (IsIconic(WIN.GetWindow())) {
				Sleep(16);
				continue;
			}

			S.Update();
			++frames;

			// –аз в ~1 секунду обновл€ем заголовок окна
			auto now = clock::now();
			std::chrono::duration<double> span = now - lastTitleTime;
			if (span.count() >= 1.0) {
				double fps = frames / span.count();
				double ms = 1000.0 / (fps > 0.0 ? fps : 1.0);

				wchar_t title[256];
				swprintf_s(title, L"%s | FPS: %.1f | %.2f ms", appName, fps, ms);
				SetWindowTextW(WIN.GetWindow(), title);

				frames = 0;
				lastTitleTime = now;
			}

			Sleep(1);
		}
	}
	catch (GFX_Exception& e) {
		OutputDebugStringA(e.what());
		pScene = nullptr;
		return 3;
	}
	catch (Window_Exception& e) {
		OutputDebugStringA(e.what());
		pScene = nullptr;
		return 4;
	}
}

