#include "HelloTriangleApp.h"

#define WIN32_APPLICATION_IMPLEMENTATION
#include "Win32Application.h"

/* åxçê (C28251) ó}êßÇÃÇΩÇﬂ SAL íçéﬂÇïtó^ */
int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance, 
    _In_opt_ HINSTANCE /*hPrevInstance*/, 
    _In_ LPWSTR /*cmdline*/, 
    _In_ int /*nCmdShow*/)
{
    HelloTriangle theApp(1280, 720);
    return Win32Application::Run(&theApp, hInstance);
}
