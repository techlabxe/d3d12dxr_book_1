#pragma once

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
# define NOMINMAX
#endif

#include <windows.h>

class DxrBookFramework;

class Win32Application {
public:
    static int Run(DxrBookFramework* app, HINSTANCE hInstance);

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    static HWND GetHWND() { return m_hWnd; }

private:
    static HWND m_hWnd;
};

#if defined(WIN32_APPLICATION_IMPLEMENTATION)
#include <exception>
#include <stdexcept>
#include <sstream>

#if defined(USE_IMGUI)
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#endif

HWND Win32Application::m_hWnd;

int Win32Application::Run(DxrBookFramework* app, HINSTANCE hInstance)
{
    if (!app) {
        return EXIT_FAILURE;
    }

#if defined(USE_IMGUI)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
#endif

    try
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpfnWndProc = Win32Application::WindowProc;
        wc.lpszClassName = L"DxrBookSampleClass";
        RegisterClassExW(&wc);

        RECT rc{};
        rc.right = LONG(app->GetWidth());
        rc.bottom = LONG(app->GetHeight());

        DWORD dwStyle = WS_OVERLAPPEDWINDOW;
        dwStyle &= ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SIZEBOX);
        AdjustWindowRect(&rc, dwStyle, FALSE);

        m_hWnd = CreateWindowW(
            wc.lpszClassName,
            app->GetTitle(),
            dwStyle,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr,
            nullptr,
            hInstance,
            app
        );
#if defined(USE_IMGUI)
        ImGui_ImplWin32_Init(m_hWnd);
#endif
        app->OnInit();

        ShowWindow(m_hWnd, SW_SHOWNORMAL);

        MSG msg{};
        while (msg.message != WM_QUIT) {
            if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        app->OnDestroy();

#if defined(USE_IMGUI)
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
#endif
        return EXIT_SUCCESS;
    }
    catch (std::exception& e)
    {
        std::ostringstream ss;
        ss << "Exception Occurred.\n";
        ss << e.what() << std::endl;
        OutputDebugStringA(ss.str().c_str());
        app->OnDestroy();
        return EXIT_FAILURE;
    }
}

#if defined(USE_IMGUI)
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps{};
    POINT mousePoint{};
    static POINT lastMousePoint{};
    static bool mousePressed = false;
    auto* app = (DxrBookFramework*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

#if defined(USE_IMGUI)
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp)) {
        return TRUE;
    }
    auto io = ImGui::GetIO();
#endif

    switch (msg)
    {
    case WM_CREATE:
    { // Application インスタンスを GWLP_USERDATA で設定して、他メッセージ処理で使えるようにする.
        auto pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lp);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    }
    return 0;

    case WM_PAINT:
        if (app) {
            app->OnUpdate();
            app->OnRender();
        }
        return 0;

    case WM_LBUTTONDOWN:
        /* break thru */
    case WM_RBUTTONDOWN:
        /* break thru */
    case WM_MBUTTONDOWN:
#if defined(USE_IMGUI)
        if (io.WantCaptureMouse) {
            break;
        }
#endif
        if (app) {
            auto btn = DxrBookFramework::MouseButton::LBUTTON;
            if (msg == WM_RBUTTONDOWN) {
                btn = DxrBookFramework::MouseButton::RBUTTON;
            }
            if (msg == WM_MBUTTONDOWN) {
                btn = DxrBookFramework::MouseButton::MBUTTON;
            }
            SetCapture(hWnd);
            GetCursorPos(&mousePoint);
            ScreenToClient(hWnd, &mousePoint);

            app->OnMouseDown(btn, int(mousePoint.x), int(mousePoint.y));
            lastMousePoint = mousePoint;
            mousePressed = true;
        }
        break;

    case WM_MOUSEMOVE:
#if defined(USE_IMGUI)
        if (io.WantCaptureMouse) {
            break;
        }
#endif
        if (app && mousePressed) {
            GetCursorPos(&mousePoint);
            ScreenToClient(hWnd, &mousePoint);
            int x = mousePoint.x - lastMousePoint.x;
            int y = mousePoint.y - lastMousePoint.y;
            app->OnMouseMove(x, y);
            lastMousePoint = mousePoint;
        }
        break;

    case WM_LBUTTONUP:
        /* break thru */
    case WM_RBUTTONUP:
        /* break thru */
    case WM_MBUTTONUP:
#if defined(USE_IMGUI)
        if (io.WantCaptureMouse) {
            break;
        }
#endif
        if (app) {
            ReleaseCapture();

            auto btn = DxrBookFramework::MouseButton::LBUTTON;
            if (msg == WM_RBUTTONUP) {
                btn = DxrBookFramework::MouseButton::RBUTTON;
            }
            if (msg == WM_MBUTTONUP) {
                btn = DxrBookFramework::MouseButton::MBUTTON;
            }
            GetCursorPos(&mousePoint);
            ScreenToClient(hWnd, &mousePoint);

            app->OnMouseUp(btn, int(mousePoint.x), int(mousePoint.y));
            lastMousePoint = mousePoint;
            mousePressed = false;
        }
        break;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

#endif
