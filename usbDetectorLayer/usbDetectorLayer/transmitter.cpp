#include "usbproc.h"

const wchar_t* transmitData(const wchar_t* string)
{
    static std::wstring response;
    response.clear();

    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return L"WSAStartup failed";


    // ============================================================
    // 1. CREATE LISTENING SOCKET ON PORT 5000 FIRST
    // ============================================================

    SOCKET listenSock = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (listenSock == INVALID_SOCKET)
    {
        WSACleanup();
        return L"listen socket failed";
    }


    sockaddr_in localAddr{};

    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(5000);
    localAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);


    // Allow immediate reuse of the port
    BOOL reuse = TRUE;

    setsockopt(
        listenSock,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse)
    );


    // ============================================================
    // 2. BIND TO PORT 5000
    // ============================================================

    if (bind(
        listenSock,
        reinterpret_cast<sockaddr*>(&localAddr),
        sizeof(localAddr)
    ) == SOCKET_ERROR)
    {
        closesocket(listenSock);
        WSACleanup();
        return L"bind 5000 failed";
    }


    // ============================================================
    // 3. START LISTENING ON PORT 5000
    // ============================================================

    if (listen(listenSock, 1) == SOCKET_ERROR)
    {
        closesocket(listenSock);
        WSACleanup();
        return L"listen failed";
    }


    // ============================================================
    // 4. NOW CONNECT TO PYTHON ON PORT 5050
    // ============================================================

    SOCKET txSock = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (txSock == INVALID_SOCKET)
    {
        closesocket(listenSock);
        WSACleanup();
        return L"transmit socket failed";
    }


    sockaddr_in server{};

    server.sin_family = AF_INET;
    server.sin_port = htons(5050);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &server.sin_addr
    );


    if (connect(
        txSock,
        reinterpret_cast<sockaddr*>(&server),
        sizeof(server)
    ) == SOCKET_ERROR)
    {
        closesocket(txSock);
        closesocket(listenSock);
        WSACleanup();
        return L"connect 5050 failed";
    }


    // ============================================================
    // 5. CONVERT wchar_t -> UTF-8
    // ============================================================

    int utf8Size = WideCharToMultiByte(
        CP_UTF8,
        0,
        string,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (utf8Size <= 0)
    {
        closesocket(txSock);
        closesocket(listenSock);
        WSACleanup();
        return L"UTF-8 conversion failed";
    }


    std::string data(
        utf8Size - 1,
        '\0'
    );


    WideCharToMultiByte(
        CP_UTF8,
        0,
        string,
        -1,
        &data[0],
        utf8Size,
        nullptr,
        nullptr
    );


    // ============================================================
    // 6. SEND DATA TO PYTHON :5050
    // ============================================================

    size_t totalSent = 0;

    while (totalSent < data.size())
    {
        int sent = send(
            txSock,
            data.data() + totalSent,
            static_cast<int>(data.size() - totalSent),
            0
        );

        if (sent == SOCKET_ERROR)
        {
            closesocket(txSock);
            closesocket(listenSock);
            WSACleanup();
            return L"send failed";
        }

        totalSent += sent;
    }


    // We are done sending the request.
    closesocket(txSock);


    // ============================================================
    // 7. WAIT FOR PYTHON TO CONNECT TO :5000
    // ============================================================

    SOCKET rxSock = accept(
        listenSock,
        nullptr,
        nullptr
    );

    if (rxSock == INVALID_SOCKET)
    {
        closesocket(listenSock);
        WSACleanup();
        return L"accept failed";
    }


    // We only expect one response connection.
    closesocket(listenSock);


    // ============================================================
    // 8. RECEIVE RESPONSE FROM PYTHON
    // ============================================================

    std::string received;

    char buffer[4096];

    while (true)
    {
        int bytesReceived = recv(
            rxSock,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytesReceived == 0)
        {
            // Python closed the connection.
            break;
        }

        if (bytesReceived == SOCKET_ERROR)
        {
            closesocket(rxSock);
            WSACleanup();
            return L"receive failed";
        }

        received.append(
            buffer,
            bytesReceived
        );
    }


    closesocket(rxSock);


    // ============================================================
    // 9. CONVERT UTF-8 RESPONSE -> wchar_t
    // ============================================================

    if (!received.empty())
    {
        int wideSize = MultiByteToWideChar(
            CP_UTF8,
            0,
            received.data(),
            static_cast<int>(received.size()),
            nullptr,
            0
        );

        if (wideSize <= 0)
        {
            WSACleanup();
            return L"UTF-8 receive conversion failed";
        }


        response.resize(wideSize);

        MultiByteToWideChar(
            CP_UTF8,
            0,
            received.data(),
            static_cast<int>(received.size()),
            &response[0],
            wideSize
        );
    }


    // ============================================================
    // 10. CLEAN UP
    // ============================================================

    WSACleanup();

    return response.c_str();
}