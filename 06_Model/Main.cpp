#define DXRBOOK_FRAMEWORK_IMPLEMENTATION
#include "ModelScene.h"

#define WIN32_APPLICATION_IMPLEMENTATION
#define USE_IMGUI
#include "Win32Application.h"

/* �x�� (C28251) �}���̂��� SAL ���߂�t�^ */
int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*cmdline*/,
    _In_ int /*nCmdShow*/)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(5453);
    ModelScene theApp(800, 600);
    return Win32Application::Run(&theApp, hInstance);
}
