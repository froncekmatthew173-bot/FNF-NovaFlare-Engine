package native;

@:cppFileCode('
#include <windows.h>

static LRESULT CALLBACK CircleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            HBRUSH brush = CreateSolidBrush(RGB(255,0,0));
            SelectObject(hdc, brush);
            Ellipse(hdc, 50,50,350,350);
            DeleteObject(brush);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}
')

class NativeGLWindow
{
    @:functionCode('
        const char* szClass = "NativeCircleClass";
        HINSTANCE hInst = GetModuleHandle(NULL);

        WNDCLASS wc = {0};
        wc.lpfnWndProc   = CircleWndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = szClass;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);

        RegisterClass(&wc);

        HWND hWnd = CreateWindowExA(0, szClass, "RedCircle", WS_OVERLAPPEDWINDOW, 200,200,400,400, NULL,NULL,hInst,NULL);
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
        res = 1;
    ')
    static public function showRedCircleWindow(res:Int = 0):Int
    {
        return res;
    }
}