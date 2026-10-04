// Created by Austin Roche on 10/3/2026.
//

#ifndef UNTITLED3_USBPROC_H
#define UNTITLED3_USBPROC_H

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include <usb.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <usbiodef.h>

#include <dbt.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "Ws2_32.lib")

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

struct UsbInterfaceInfo
{
    BYTE interfaceNumber;
    BYTE interfaceClass;
    BYTE interfaceSubClass;
    BYTE interfaceProtocol;
};

struct UsbDeviceInfo
{
    std::wstring devicePath;

    std::wstring vid;
    std::wstring pid;
    std::wstring serialNumber;
    std::wstring manufacturer;
    std::wstring productName;
    std::wstring friendlyName;
	std::wstring hardwareId;
    std::wstring instanceId;
    std::wstring deviceClassWin;

	BYTE deviceClass = 0;
	BYTE deviceSubClass = 0;
    BYTE deviceProtocol = 0;

    std::wstring classGuid;

    DWORD deviceType = 0;

    std::vector<UsbInterfaceInfo> interfaces;
};

UsbDeviceInfo GetUsbDeviceInfo(const wchar_t* devicePath);

const wchar_t* PrintUsbDeviceInfo(UsbDeviceInfo info);

bool GetUsbDeviceDescriptor(
    const wchar_t* devicePath,
    USB_DEVICE_DESCRIPTOR& descriptor,
    std::vector<UsbInterfaceInfo>& interfaces);

bool GetUsbInterfaceInfo(
    HANDLE hubHandle,
    ULONG port,
    std::vector<UsbInterfaceInfo>& interfaces);

const wchar_t* transmitData(const wchar_t* data);

#endif //UNTITLED3_USBPROC_H
