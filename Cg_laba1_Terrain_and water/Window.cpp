#include "Window.h"

namespace window {

	Window::Window(LPCWSTR appName, int height, int width, WNDPROC WndProc, bool isFullscreen)
		: m_Instance(GetModuleHandle(nullptr))
		, m_Window(nullptr)
		, m_isFullscreen(isFullscreen)
		, m_hWindow(height)
		, m_wWindow(width)
		, m_nameApp(appName ? appName : L"DX12 Window")
	{
		// регистрируем класс окна
		WNDCLASSEX wc{};
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = m_Instance;
		wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszMenuName = nullptr;
		wc.lpszClassName = m_nameApp;
		wc.hIconSm = wc.hIcon;

		if (!RegisterClassEx(&wc)) {
			throw Window_Exception("RegisterClassEx failed.");
		}

		DWORD style = WS_OVERLAPPEDWINDOW;
		DWORD styleEx = WS_EX_APPWINDOW;

		if (m_isFullscreen) {
			// фуллскрин на весь монитор
			DEVMODE dm{};
			dm.dmSize = sizeof(dm);
			dm.dmPelsWidth = (unsigned long)GetSystemMetrics(SM_CXSCREEN);
			dm.dmPelsHeight = (unsigned long)GetSystemMetrics(SM_CYSCREEN);
			dm.dmBitsPerPel = 32;
			dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

			if (ChangeDisplaySettings(&dm, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) {
				UnregisterClass(m_nameApp, m_Instance);
				throw Window_Exception("ChangeDisplaySettings (fullscreen) failed.");
			}

			style = WS_POPUP;
			styleEx = WS_EX_APPWINDOW | WS_EX_TOPMOST;

			m_Window = CreateWindowEx(
				styleEx, m_nameApp, m_nameApp, style,
				0, 0,
				GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
				nullptr, nullptr, m_Instance, nullptr);
		}
		else {
			// хотим ровно m_wWindow x m_hWindow клиентской области
			RECT rc{ 0, 0, m_wWindow, m_hWindow };
			AdjustWindowRectEx(&rc, style, FALSE, styleEx);
			const int winW = rc.right - rc.left;
			const int winH = rc.bottom - rc.top;

			const int scrW = GetSystemMetrics(SM_CXSCREEN);
			const int scrH = GetSystemMetrics(SM_CYSCREEN);
			const int x = (scrW - winW) / 2;
			const int y = (scrH - winH) / 2;

			m_Window = CreateWindowEx(
				styleEx, m_nameApp, m_nameApp, style,
				x, y, winW, winH,
				nullptr, nullptr, m_Instance, nullptr);
		}

		if (!m_Window) {
			UnregisterClass(m_nameApp, m_Instance);
			throw Window_Exception("CreateWindowEx failed.");
		}

		ShowWindow(m_Window, SW_SHOW);
		UpdateWindow(m_Window);

		// как в оригинальном проекте — курсор прячем, чтобы не мешал
		ShowCursor(false);
	}

	Window::~Window() {
		// всегда возвращаем курсор
		ShowCursor(TRUE);

		// если включали фуллскрин — вернуть режим
		if (m_isFullscreen) {
			ChangeDisplaySettings(nullptr, 0);
		}

		if (m_Window) {
			DestroyWindow(m_Window);
			m_Window = nullptr;
		}

		if (m_Instance && m_nameApp) {
			UnregisterClass(m_nameApp, m_Instance);
		}

		m_Instance = nullptr;
	}
}
