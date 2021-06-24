#define DXRBOOK_FRAMEWORK_IMPLEMENTATION
#include "MaterialScene.h"

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
    MaterialScene theApp(800, 600);
    return Win32Application::Run(&theApp, hInstance);
}
