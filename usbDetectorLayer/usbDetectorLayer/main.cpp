#include <iostream>
#include "usbproc.h"

// use port 5000 for outgoing to python comms and 5050 for incoming from python comms
// TIP To <b>Run</b> code, press <shortcut actionId="Run"/> or click the <icon src="AllIcons.Actions.Execute"/> icon in the gutter.
int main() {
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"USBProcClass";
    if (!RegisterClassExW(&wc)) {
        std::cout << "Failed to register window class" << std::endl;
        return 1;
    }

    HINSTANCE hInst = GetModuleHandle(NULL);

    HWND hWnd = CreateWindowExW(0, L"USBProcClass", L"USBWait", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
    if(!hWnd) {
        std::cout << "Failed to create window" << std::endl;
        return 1;
	}
   
    //MessageBoxW(NULL, L"Hello, World!", L"Hello, World!", MB_OK);
    DEV_BROADCAST_DEVICEINTERFACE filter = { 0 };
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    HDEVNOTIFY hNotify = RegisterDeviceNotificationW(hWnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    if (hNotify == NULL) {
        std::cout << "Failed to register device notification" << std::endl;
        return 1;
    }

    printf("Blocking till device intercept\n");
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    printf("USB Intercepted, exiting.\n");

    UnregisterDeviceNotification(hNotify);
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
    // TIP See CLion help at <a href="https://www.jetbrains.com/help/clion/">jetbrains.com/help/clion/</a>. Also, you can try interactive lessons for CLion by selecting 'Help | Learn IDE Features' from the main menu.
}