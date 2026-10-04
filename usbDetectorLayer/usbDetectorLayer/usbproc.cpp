//
// Created by Austin Roche on 10/3/2026.
//

#define NUM_VARS 4

#define INITGUID

#include "usbproc.h"

#include <windows.h>
#include <dbt.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <usbioctl.h>

#include <iostream>
#include <vector>
#include <string>
#include <cwchar>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")


// ============================================================
// Window procedure
// ============================================================

LRESULT CALLBACK WndProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_DEVICECHANGE:
    {
        if (wParam == DBT_DEVICEARRIVAL)
        {
            PDEV_BROADCAST_HDR pHdr =
                reinterpret_cast<PDEV_BROADCAST_HDR>(lParam);

            if (!pHdr)
                break;

            if (pHdr->dbch_devicetype !=
                DBT_DEVTYP_DEVICEINTERFACE)
            {
                break;
            }

            PDEV_BROADCAST_DEVICEINTERFACE_W pDevInf =
                reinterpret_cast<
                PDEV_BROADCAST_DEVICEINTERFACE_W>(
                    pHdr);

            if (!pDevInf)
                break;

            //
            // IMPORTANT:
            //
            // Windows can generate several device-interface
            // arrivals for one USB flash drive:
            //
            // USB#
            // USBSTOR#
            // STORAGE#
            // SWD#WPDBUSENUM#
            //
            // We only want the actual USB device interface.
            //
            if (wcsncmp(
                pDevInf->dbcc_name,
                L"\\\\?\\USB#",
                8) != 0)
            {
                break;
            }

            std::wcout
                << L"USB device connected"
                << std::endl;

            std::wcout
                << L"Device name: "
                << pDevInf->dbcc_name
                << std::endl;

            //
            // Get the device information ONCE.
            //
            UsbDeviceInfo deviceInfo =
                GetUsbDeviceInfo(
                    pDevInf->dbcc_name);

            //
            // Format it ONCE.
            //
            const wchar_t* deviceInfoText =
                PrintUsbDeviceInfo(deviceInfo);

            std::wcout
                << L"Device info: "
                << deviceInfoText
                << std::endl;

            //
            // Send the same information to Python.
            //
            const wchar_t* resp =
                transmitData(deviceInfoText);

            if (resp == nullptr)
            {
                MessageBoxW(
                    hWnd,
                    L"Python script returned no response.",
                    L"USB Device Not Checked",
                    MB_OK | MB_ICONWARNING);

                break;
            }

            //
            // IMPORTANT:
            //
            // wcscmp() returns 0 when strings are equal.
            //
            if (wcscmp(resp, L"APPROVE") == 0)
            {
                MessageBoxW(
                    hWnd,
                    L"USB device approved by Python script.",
                    L"USB Device Approved",
                    MB_OK | MB_ICONINFORMATION);
            }
            else if (wcscmp(resp, L"DENY") == 0)
            {
                MessageBoxW(
                    hWnd,
                    L"USB device blocked by Python script. "
                    L"Disconnect recommended.",
                    L"USB Device Blocked",
                    MB_OK | MB_ICONWARNING);
            }
            else
            {
                MessageBoxW(
                    hWnd,
                    L"USB device was not checked by the "
                    L"Python script. Disconnect recommended.",
                    L"USB Device Not Checked",
                    MB_OK | MB_ICONWARNING);
            }

            return 0;
        }

        break;
    }
    }

    return DefWindowProc(
        hWnd,
        message,
        wParam,
        lParam);
}


// ============================================================
// GetUsbDeviceInfo
// ============================================================

UsbDeviceInfo GetUsbDeviceInfo(
    const wchar_t* devicePath)
{
    UsbDeviceInfo ret{};

    if (!devicePath)
        return ret;

    ret.devicePath = devicePath;

    //
    // Create an empty device information set.
    //
    HDEVINFO hDevInfo =
        SetupDiCreateDeviceInfoList(
            nullptr,
            nullptr);

    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();

        std::wcerr
            << L"SetupDiCreateDeviceInfoList failed: "
            << error
            << std::endl;

        return ret;
    }

    //
    // Open the device interface.
    //
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize =
        sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiOpenDeviceInterfaceW(
        hDevInfo,
        devicePath,
        0,
        &interfaceData))
    {
        DWORD error = GetLastError();

        std::wcerr
            << L"SetupDiOpenDeviceInterfaceW failed: "
            << error
            << std::endl;

        SetupDiDestroyDeviceInfoList(hDevInfo);

        return ret;
    }

    //
    // --------------------------------------------------------
    // First call:
    // determine required buffer size.
    //
    // IMPORTANT:
    // Do NOT pass devInfoData here.
    // Get the SP_DEVINFO_DATA on the successful second call.
    // --------------------------------------------------------
    //

    DWORD requiredSize = 0;

    SetupDiGetDeviceInterfaceDetailW(
        hDevInfo,
        &interfaceData,
        nullptr,
        0,
        &requiredSize,
        nullptr);

    DWORD error = GetLastError();

    if (error != ERROR_INSUFFICIENT_BUFFER ||
        requiredSize == 0)
    {
        std::wcerr
            << L"SetupDiGetDeviceInterfaceDetailW "
            L"size query failed. Error="
            << error
            << std::endl;

        SetupDiDestroyDeviceInfoList(hDevInfo);

        return ret;
    }

    //
    // Allocate detail buffer.
    //
    std::vector<BYTE> detailBuffer(
        requiredSize);

    auto* detailData =
        reinterpret_cast<
        SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            detailBuffer.data());

    detailData->cbSize =
        sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    //
    // Device information for the actual PnP device.
    //
    SP_DEVINFO_DATA devInfoData{};
    devInfoData.cbSize =
        sizeof(SP_DEVINFO_DATA);

    //
    // --------------------------------------------------------
    // Second call:
    // obtain the actual SP_DEVINFO_DATA.
    // --------------------------------------------------------
    //

    if (!SetupDiGetDeviceInterfaceDetailW(
        hDevInfo,
        &interfaceData,
        detailData,
        requiredSize,
        nullptr,
        &devInfoData))
    {
        error = GetLastError();

        std::wcerr
            << L"SetupDiGetDeviceInterfaceDetailW failed: "
            << error
            << std::endl;

        SetupDiDestroyDeviceInfoList(hDevInfo);

        return ret;
    }

    std::wcout
        << L"Device instance handle: "
        << devInfoData.DevInst
        << std::endl;


    // ========================================================
    // HARDWARE ID
    // ========================================================

    WCHAR hardwareIdBuffer[4096]{};

    DWORD propertyType = 0;
    DWORD propertySize = 0;

    if (SetupDiGetDeviceRegistryPropertyW(
        hDevInfo,
        &devInfoData,
        SPDRP_HARDWAREID,
        &propertyType,
        reinterpret_cast<PBYTE>(
            hardwareIdBuffer),
        sizeof(hardwareIdBuffer),
        &propertySize))
    {
        //
        // SPDRP_HARDWAREID is normally REG_MULTI_SZ.
        //
        ret.hardwareId =
            hardwareIdBuffer;

        //
        // Find VID/PID.
        //
        std::wstring hardwareId =
            hardwareIdBuffer;

        size_t vidPos =
            hardwareId.find(L"VID_");

        if (vidPos != std::wstring::npos &&
            vidPos + 8 <= hardwareId.length())
        {
            ret.vid =
                hardwareId.substr(
                    vidPos + 4,
                    4);
        }

        size_t pidPos =
            hardwareId.find(L"PID_");

        if (pidPos != std::wstring::npos &&
            pidPos + 8 <= hardwareId.length())
        {
            ret.pid =
                hardwareId.substr(
                    pidPos + 4,
                    4);
        }

        std::wcout
            << L"Hardware ID: "
            << hardwareId
            << std::endl;
    }
    else
    {
        error = GetLastError();

        std::wcerr
            << L"SPDRP_HARDWAREID failed: "
            << error
            << L" (0x"
            << std::hex
            << error
            << std::dec
            << L")"
            << std::endl;

        std::wcerr
            << L"DevInst: "
            << devInfoData.DevInst
            << std::endl;

        std::wcerr
            << L"Property type: "
            << propertyType
            << std::endl;

        std::wcerr
            << L"Property size: "
            << propertySize
            << std::endl;
    }


    // ========================================================
    // MANUFACTURER
    // ========================================================

    WCHAR manufacturerBuffer[1024]{};

    if (SetupDiGetDeviceRegistryPropertyW(
        hDevInfo,
        &devInfoData,
        SPDRP_MFG,
        nullptr,
        reinterpret_cast<PBYTE>(
            manufacturerBuffer),
        sizeof(manufacturerBuffer),
        nullptr))
    {
        ret.manufacturer =
            manufacturerBuffer;
    }


    // ========================================================
    // DEVICE DESCRIPTION
    // ========================================================

    WCHAR productNameBuffer[1024]{};

    if (SetupDiGetDeviceRegistryPropertyW(
        hDevInfo,
        &devInfoData,
        SPDRP_DEVICEDESC,
        nullptr,
        reinterpret_cast<PBYTE>(
            productNameBuffer),
        sizeof(productNameBuffer),
        nullptr))
    {
        ret.productName =
            productNameBuffer;
    }


    // ========================================================
    // FRIENDLY NAME
    // ========================================================

    WCHAR friendlyNameBuffer[1024]{};

    if (SetupDiGetDeviceRegistryPropertyW(
        hDevInfo,
        &devInfoData,
        SPDRP_FRIENDLYNAME,
        nullptr,
        reinterpret_cast<PBYTE>(
            friendlyNameBuffer),
        sizeof(friendlyNameBuffer),
        nullptr))
    {
        ret.friendlyName =
            friendlyNameBuffer;
    }


    // ========================================================
    // INSTANCE ID
    // ========================================================

    WCHAR instanceId[MAX_DEVICE_ID_LEN]{};

    CONFIGRET cr =
        CM_Get_Device_IDW(
            devInfoData.DevInst,
            instanceId,
            MAX_DEVICE_ID_LEN,
            0);

    if (cr == CR_SUCCESS)
    {
        ret.instanceId =
            instanceId;
    }
    else
    {
        std::wcerr
            << L"CM_Get_Device_IDW failed: "
            << cr
            << std::endl;
    }


    // ========================================================
    // WINDOWS DEVICE CLASS
    // ========================================================

    WCHAR deviceClassBuffer[1024]{};

    if (SetupDiGetDeviceRegistryPropertyW(
        hDevInfo,
        &devInfoData,
        SPDRP_CLASS,
        nullptr,
        reinterpret_cast<PBYTE>(
            deviceClassBuffer),
        sizeof(deviceClassBuffer),
        nullptr))
    {
        ret.deviceClassWin =
            deviceClassBuffer;
    }


    // ========================================================
    // CLASS GUID
    // ========================================================

    WCHAR classGuidBuffer[1024]{};

    if (SetupDiGetDeviceRegistryPropertyW(
        hDevInfo,
        &devInfoData,
        SPDRP_CLASSGUID,
        nullptr,
        reinterpret_cast<PBYTE>(
            classGuidBuffer),
        sizeof(classGuidBuffer),
        nullptr))
    {
        ret.classGuid =
            classGuidBuffer;
    }


    // ========================================================
    // DEVICE TYPE
    // ========================================================

    DWORD deviceType = 0;

    if (SetupDiGetDeviceRegistryPropertyW(
        hDevInfo,
        &devInfoData,
        SPDRP_DEVTYPE,
        nullptr,
        reinterpret_cast<PBYTE>(
            &deviceType),
        sizeof(deviceType),
        nullptr))
    {
        ret.deviceType =
            deviceType;
    }


    // ========================================================
    // USB DESCRIPTOR
    // ========================================================

    USB_DEVICE_DESCRIPTOR descriptor{};

    if (GetUsbDeviceDescriptor(
        devicePath,
        descriptor,
        ret.interfaces))
    {
        ret.deviceClass =
            descriptor.bDeviceClass;

        ret.deviceSubClass =
            descriptor.bDeviceSubClass;

        ret.deviceProtocol =
            descriptor.bDeviceProtocol;
    }
    else
    {
        std::wcerr
            << L"GetUsbDeviceDescriptor failed."
            << std::endl;
    }


    SetupDiDestroyDeviceInfoList(hDevInfo);

    return ret;
}


// ============================================================
// PrintUsbDeviceInfo
// ============================================================

const wchar_t* PrintUsbDeviceInfo(
    UsbDeviceInfo info)
{
    static std::wstring output;

    output.clear();

    output +=
        L"==============================\n";

    output +=
        L"USB DEVICE INFORMATION\n";

    output +=
        L"==============================\n";

    output +=
        L"Device Path: " +
        info.devicePath +
        L"\n";

    output +=
        L"VID:         " +
        info.vid +
        L"\n";

    output +=
        L"PID:         " +
        info.pid +
        L"\n";

    output +=
        L"Manufacturer: " +
        info.manufacturer +
        L"\n";

    output +=
        L"Product:      " +
        info.productName +
        L"\n";

    output +=
        L"Friendly Name: " +
        info.friendlyName +
        L"\n";

    output +=
        L"Hardware ID:  " +
        info.hardwareId +
        L"\n";

    output +=
        L"Instance ID:  " +
        info.instanceId +
        L"\n";


    // ========================================================
    // Windows SetupAPI class
    // ========================================================

    output +=
        L"\nWindows Device Class: " +
        info.deviceClassWin +
        L"\n";

    output +=
        L"Class GUID: " +
        info.classGuid +
        L"\n";


    // ========================================================
    // Actual USB DEVICE descriptor
    // ========================================================

    output +=
        L"\nUSB Device Descriptor\n";

    output +=
        L"--------------------\n";

    output +=
        L"bDeviceClass:    " +
        std::to_wstring(
            info.deviceClass) +
        L"\n";

    output +=
        L"bDeviceSubClass: " +
        std::to_wstring(
            info.deviceSubClass) +
        L"\n";

    output +=
        L"bDeviceProtocol: " +
        std::to_wstring(
            info.deviceProtocol) +
        L"\n";


    // ========================================================
    // Actual USB INTERFACE descriptors
    // ========================================================

    output +=
        L"\nUSB Interfaces\n";

    output +=
        L"--------------\n";

    if (info.interfaces.empty())
    {
        output +=
            L"No USB interfaces found.\n";
    }
    else
    {
        for (const auto& interfaceInfo :
            info.interfaces)
        {
            output +=
                L"Interface " +
                std::to_wstring(
                    interfaceInfo.interfaceNumber) +
                L":\n";

            output +=
                L"    bInterfaceClass:    " +
                std::to_wstring(
                    interfaceInfo.interfaceClass) +
                L"\n";

            output +=
                L"    bInterfaceSubClass: " +
                std::to_wstring(
                    interfaceInfo.interfaceSubClass) +
                L"\n";

            output +=
                L"    bInterfaceProtocol: " +
                std::to_wstring(
                    interfaceInfo.interfaceProtocol) +
                L"\n";
        }
    }

    output +=
        L"==============================\n";

    return output.c_str();
}


// ============================================================
// GetUsbDeviceDescriptor
// ============================================================

bool GetUsbDeviceDescriptor(
    const wchar_t* devicePath,
    USB_DEVICE_DESCRIPTOR& descriptor,
    std::vector<UsbInterfaceInfo>& interfaces)
{
    descriptor = {};
    interfaces.clear();

    if (!devicePath)
        return false;

    std::wstring path(devicePath);

    //
    // IMPORTANT:
    //
    // The actual device path contains:
    //
    // VID_0781
    // PID_5567
    //
    // Your previous code searched for lowercase:
    //
    // vid_
    // pid_
    //
    // std::wstring::find() is case-sensitive.
    //
    size_t vidPos =
        path.find(L"VID_");

    size_t pidPos =
        path.find(L"PID_");

    if (vidPos == std::wstring::npos ||
        pidPos == std::wstring::npos)
    {
        std::wcerr
            << L"Could not find VID/PID in device path:"
            << std::endl
            << path
            << std::endl;

        return false;
    }

    if (vidPos + 8 > path.length() ||
        pidPos + 8 > path.length())
    {
        return false;
    }

    std::wstring vid =
        path.substr(
            vidPos + 4,
            4);

    std::wstring pid =
        path.substr(
            pidPos + 4,
            4);

    std::wcout
        << L"Searching USB topology for "
        << L"VID=" << vid
        << L" PID=" << pid
        << std::endl;


    // ========================================================
    // Get all USB hub interfaces.
    // ========================================================

    HDEVINFO hDevInfo =
        SetupDiGetClassDevsW(
            &GUID_DEVINTERFACE_USB_HUB,
            nullptr,
            nullptr,
            DIGCF_PRESENT |
            DIGCF_DEVICEINTERFACE);

    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();

        std::wcerr
            << L"SetupDiGetClassDevsW for USB hubs failed: "
            << error
            << std::endl;

        return false;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize =
        sizeof(SP_DEVICE_INTERFACE_DATA);

    bool found = false;


    // ========================================================
    // Enumerate USB hubs.
    // ========================================================

    for (DWORD index = 0;
        SetupDiEnumDeviceInterfaces(
            hDevInfo,
            nullptr,
            &GUID_DEVINTERFACE_USB_HUB,
            index,
            &interfaceData);
        ++index)
    {
        DWORD requiredSize = 0;

        SetupDiGetDeviceInterfaceDetailW(
            hDevInfo,
            &interfaceData,
            nullptr,
            0,
            &requiredSize,
            nullptr);

        if (requiredSize == 0)
            continue;

        std::vector<BYTE> buffer(
            requiredSize);

        auto* detail =
            reinterpret_cast<
            SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
                buffer.data());

        detail->cbSize =
            sizeof(
                SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(
            hDevInfo,
            &interfaceData,
            detail,
            requiredSize,
            nullptr,
            nullptr))
        {
            continue;
        }

        //
        // Open hub.
        //
        HANDLE hubHandle =
            CreateFileW(
                detail->DevicePath,
                GENERIC_WRITE,
                FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);

        if (hubHandle == INVALID_HANDLE_VALUE)
        {
            continue;
        }


        // ====================================================
        // Get hub information.
        // ====================================================

        USB_NODE_INFORMATION hubInfo{};

        hubInfo.NodeType =
            UsbHub;

        DWORD bytesReturned = 0;

        BOOL success =
            DeviceIoControl(
                hubHandle,
                IOCTL_USB_GET_NODE_INFORMATION,
                &hubInfo,
                sizeof(hubInfo),
                &hubInfo,
                sizeof(hubInfo),
                &bytesReturned,
                nullptr);

        if (!success)
        {
            DWORD error = GetLastError();

            std::wcerr
                << L"IOCTL_USB_GET_NODE_INFORMATION failed. "
                << L"Error="
                << error
                << std::endl;

            CloseHandle(hubHandle);

            continue;
        }

        ULONG portCount =
            hubInfo
            .u
            .HubInformation
            .HubDescriptor
            .bNumberOfPorts;


        // ====================================================
        // Enumerate ports.
        // ====================================================

        for (ULONG port = 1;
            port <= portCount;
            ++port)
        {
            DWORD connectionInfoSize =
                sizeof(
                    USB_NODE_CONNECTION_INFORMATION_EX);

            std::vector<BYTE> connectionBuffer(
                connectionInfoSize);

            auto* connectionInfo =
                reinterpret_cast<
                PUSB_NODE_CONNECTION_INFORMATION_EX>(
                    connectionBuffer.data());

            ZeroMemory(
                connectionInfo,
                connectionInfoSize);

            connectionInfo->ConnectionIndex =
                port;

            bytesReturned = 0;

            success =
                DeviceIoControl(
                    hubHandle,
                    IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                    connectionInfo,
                    connectionInfoSize,
                    connectionInfo,
                    connectionInfoSize,
                    &bytesReturned,
                    nullptr);

            if (!success)
            {
                continue;
            }

            if (connectionInfo->ConnectionStatus !=
                DeviceConnected)
            {
                continue;
            }

            USB_DEVICE_DESCRIPTOR& usbDescriptor =
                connectionInfo->DeviceDescriptor;

            wchar_t currentVid[5]{};
            wchar_t currentPid[5]{};

            swprintf_s(
                currentVid,
                5,
                L"%04X",
                usbDescriptor.idVendor);

            swprintf_s(
                currentPid,
                5,
                L"%04X",
                usbDescriptor.idProduct);

            //
            // Compare VID/PID.
            //
            if (_wcsicmp(
                currentVid,
                vid.c_str()) == 0 &&
                _wcsicmp(
                    currentPid,
                    pid.c_str()) == 0)
            {
                std::wcout
                    << L"Found matching USB device "
                    << L"on hub port "
                    << port
                    << std::endl;

                descriptor =
                    usbDescriptor;

                //
                // Get configuration/interface descriptors.
                //
                if (!GetUsbInterfaceInfo(
                    hubHandle,
                    port,
                    interfaces))
                {
                    DWORD error =
                        GetLastError();

                    std::wcerr
                        << L"GetUsbInterfaceInfo failed. "
                        << L"Port="
                        << port
                        << L" Error="
                        << error
                        << std::endl;
                }
                else
                {
                    std::wcout
                        << L"Found "
                        << interfaces.size()
                        << L" USB interface(s) "
                        << L"on port "
                        << port
                        << std::endl;
                }

                found = true;

                break;
            }
        }

        CloseHandle(hubHandle);

        if (found)
            break;
    }

    SetupDiDestroyDeviceInfoList(
        hDevInfo);

    return found;
}


// ============================================================
// GetUsbInterfaceInfo
// ============================================================

bool GetUsbInterfaceInfo(
    HANDLE hubHandle,
    ULONG port,
    std::vector<UsbInterfaceInfo>& interfaces)
{
    interfaces.clear();

    if (hubHandle == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytesReturned = 0;


    // ========================================================
    // First request:
    // retrieve configuration descriptor header.
    // ========================================================

    DWORD headerSize =
        sizeof(USB_DESCRIPTOR_REQUEST) +
        sizeof(USB_CONFIGURATION_DESCRIPTOR);

    std::vector<BYTE> headerBuffer(
        headerSize);

    auto* headerRequest =
        reinterpret_cast<
        PUSB_DESCRIPTOR_REQUEST>(
            headerBuffer.data());

    ZeroMemory(
        headerRequest,
        headerSize);

    headerRequest->ConnectionIndex =
        port;

    //
    // High byte = descriptor type
    // Low byte  = descriptor index
    //
    headerRequest->SetupPacket.wValue =
        static_cast<USHORT>(
            (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) |
            0);

    headerRequest->SetupPacket.wIndex =
        0;

    headerRequest->SetupPacket.wLength =
        sizeof(USB_CONFIGURATION_DESCRIPTOR);

    BOOL success =
        DeviceIoControl(
            hubHandle,
            IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
            headerRequest,
            headerSize,
            headerRequest,
            headerSize,
            &bytesReturned,
            nullptr);

    if (!success)
    {
        DWORD error = GetLastError();

        std::wcerr
            << L"CONFIG HEADER FAILED. "
            << L"Port="
            << port
            << L" Error="
            << error
            << L" (0x"
            << std::hex
            << error
            << std::dec
            << L")"
            << std::endl;

        return false;
    }

    //
    // Descriptor immediately follows the request.
    //
    auto* configDescriptor =
        reinterpret_cast<
        PUSB_CONFIGURATION_DESCRIPTOR>(
            headerBuffer.data() +
            sizeof(USB_DESCRIPTOR_REQUEST));

    if (configDescriptor->bDescriptorType !=
        USB_CONFIGURATION_DESCRIPTOR_TYPE)
    {
        std::wcerr
            << L"INVALID CONFIG DESCRIPTOR TYPE. "
            << L"Got="
            << static_cast<int>(
                configDescriptor->bDescriptorType)
            << std::endl;

        return false;
    }

    //
    // Total configuration descriptor size.
    //
    USHORT totalLength =
        configDescriptor->wTotalLength;

    if (totalLength <
        sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        std::wcerr
            << L"INVALID CONFIG LENGTH. "
            << L"Length="
            << totalLength
            << std::endl;

        return false;
    }

    std::wcout
        << L"USB configuration descriptor length: "
        << totalLength
        << std::endl;


    // ========================================================
    // Second request:
    // retrieve the complete configuration descriptor.
    // ========================================================

    DWORD bufferSize =
        sizeof(USB_DESCRIPTOR_REQUEST) +
        totalLength;

    std::vector<BYTE> buffer(
        bufferSize);

    auto* request =
        reinterpret_cast<
        PUSB_DESCRIPTOR_REQUEST>(
            buffer.data());

    ZeroMemory(
        request,
        bufferSize);

    request->ConnectionIndex =
        port;

    request->SetupPacket.wValue =
        static_cast<USHORT>(
            (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) |
            0);

    request->SetupPacket.wIndex =
        0;

    request->SetupPacket.wLength =
        totalLength;

    bytesReturned = 0;

    success =
        DeviceIoControl(
            hubHandle,
            IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
            request,
            bufferSize,
            request,
            bufferSize,
            &bytesReturned,
            nullptr);

    if (!success)
    {
        DWORD error = GetLastError();

        std::wcerr
            << L"FULL CONFIG FAILED. "
            << L"Port="
            << port
            << L" Error="
            << error
            << L" (0x"
            << std::hex
            << error
            << std::dec
            << L")"
            << std::endl;

        return false;
    }


    // ========================================================
    // Walk through configuration descriptors.
    // ========================================================

    PUCHAR descriptorStart =
        buffer.data() +
        sizeof(USB_DESCRIPTOR_REQUEST);

    PUCHAR descriptorEnd =
        descriptorStart +
        totalLength;

    PUCHAR current =
        descriptorStart;

    while (
        current +
        sizeof(USB_COMMON_DESCRIPTOR)
        <= descriptorEnd)
    {
        auto* common =
            reinterpret_cast<
            PUSB_COMMON_DESCRIPTOR>(
                current);

        //
        // Malformed descriptor.
        //
        if (common->bLength == 0)
        {
            std::wcerr
                << L"USB descriptor has zero length."
                << std::endl;

            break;
        }

        //
        // Make sure descriptor stays inside
        // returned buffer.
        //
        if (current +
            common->bLength >
            descriptorEnd)
        {
            std::wcerr
                << L"USB descriptor extends beyond "
                L"configuration buffer."
                << std::endl;

            break;
        }

        //
        // Interface descriptor.
        //
        if (common->bDescriptorType ==
            USB_INTERFACE_DESCRIPTOR_TYPE)
        {
            if (common->bLength >=
                sizeof(USB_INTERFACE_DESCRIPTOR))
            {
                auto* interfaceDescriptor =
                    reinterpret_cast<
                    PUSB_INTERFACE_DESCRIPTOR>(
                        current);

                UsbInterfaceInfo info{};

                info.interfaceNumber =
                    interfaceDescriptor->
                    bInterfaceNumber;

                info.interfaceClass =
                    interfaceDescriptor->
                    bInterfaceClass;

                info.interfaceSubClass =
                    interfaceDescriptor->
                    bInterfaceSubClass;

                info.interfaceProtocol =
                    interfaceDescriptor->
                    bInterfaceProtocol;

                interfaces.push_back(
                    info);

                std::wcout
                    << L"USB INTERFACE FOUND"
                    << L" Number="
                    << static_cast<int>(
                        info.interfaceNumber)
                    << L" Class=0x"
                    << std::hex
                    << static_cast<int>(
                        info.interfaceClass)
                    << L" SubClass=0x"
                    << static_cast<int>(
                        info.interfaceSubClass)
                    << L" Protocol=0x"
                    << static_cast<int>(
                        info.interfaceProtocol)
                    << std::dec
                    << std::endl;
            }
        }

        current +=
            common->bLength;
    }


    // ========================================================
    // Result
    // ========================================================

    if (interfaces.empty())
    {
        std::wcerr
            << L"Configuration received, but no "
            L"interface descriptors were found."
            << std::endl;

        return false;
    }

    return true;
}