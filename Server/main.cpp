#include <iostream>
#include <thread>
#include <vector>
#include <WinSock2.h>

#pragma comment(lib, "ws2_32.lib")

SOCKET listenSocket;
HANDLE hIOCP;

enum IO_OPERATION {
    IO_READ,
    IO_WRITE
};

std::vector<SOCKET> clients;

struct PER_IO_DATA {
    OVERLAPPED overlapped; // 반드시 첫 번째 멤버로 둠
    WSABUF wsabuf;
    char buffer[1024];     // 전송/수신 데이터 버퍼
    IO_OPERATION op;       // 작업 유형 (읽기/쓰기)
};

// 클라이언트 소켓에 새로운 비동기 수신을 요청하는 함수
void PostRecv(SOCKET client) {
    PER_IO_DATA* pPerIoData = new PER_IO_DATA;
    memset(&pPerIoData->overlapped, 0, sizeof(OVERLAPPED));
    pPerIoData->wsabuf.buf = pPerIoData->buffer;
    pPerIoData->wsabuf.len = sizeof(pPerIoData->buffer);
    pPerIoData->op = IO_READ;

    DWORD flags = 0;
    DWORD recvBytes = 0;
	memset(&pPerIoData->buffer, 0, sizeof(pPerIoData->buffer));
    if (WSARecv(client, &pPerIoData->wsabuf, 1, &recvBytes, &flags, &pPerIoData->overlapped, NULL) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSA_IO_PENDING) {
            std::cerr << "WSARecv failed: " << WSAGetLastError() << std::endl;
            delete pPerIoData;
            closesocket(client);
        }
    }
}

// 클라이언트 연결을 수락하고 IOCP에 등록하는 함수
void AcceptThread() {
    while (true) {
        SOCKET client = accept(listenSocket, NULL, NULL);
        if (client == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        clients.push_back(client);

        // IOCP에 클라이언트 소켓 등록
        if (!CreateIoCompletionPort((HANDLE)client, hIOCP, (ULONG_PTR)client, 0)) {
            std::cerr << "CreateIoCompletionPort failed: " << GetLastError() << std::endl;
            closesocket(client);
            continue;
        }

        std::cout << "Client connected: " << client << std::endl;

        // 첫 번째 비동기 수신 요청
        PostRecv(client);
    }
}

// IOCP에서 완료된 작업을 처리하는 워커 스레드
void WorkerThread() {
    while (true) {
        DWORD bytesTransferred;
        SOCKET client;
        OVERLAPPED* overlapped;

        BOOL result = GetQueuedCompletionStatus(hIOCP, &bytesTransferred, (PULONG_PTR)&client, &overlapped, INFINITE);
        PER_IO_DATA* pPerIoData = (PER_IO_DATA*)overlapped;

        if (!result || bytesTransferred == 0) {
            // 오류 발생 또는 클라이언트 연결 종료
            std::cout << "Client disconnected: " << client << std::endl;
            closesocket(client);
            delete pPerIoData;
            continue;
        }

        if (pPerIoData->op == IO_READ) {
            // 수신한 데이터 출력
            std::cout << "Received from client (" << client << "): " << std::string(pPerIoData->buffer, bytesTransferred) << std::endl;

            // 다른 클라이언트에게 브로드캐스트
            for (const auto& otherClient : clients) {
                if (otherClient != client) {
                    PER_IO_DATA* pSendData = new PER_IO_DATA;
                    memset(&pSendData->overlapped, 0, sizeof(OVERLAPPED));
                    pSendData->op = IO_WRITE;
                    pSendData->wsabuf.buf = pSendData->buffer;
                    pSendData->wsabuf.len = bytesTransferred;
                    memcpy(pSendData->buffer, pPerIoData->buffer, bytesTransferred);

                    DWORD bytesSent = 0;
                    if (WSASend(otherClient, &pSendData->wsabuf, 1, &bytesSent, 0, &pSendData->overlapped, NULL) == SOCKET_ERROR) {
                        if (WSAGetLastError() != WSA_IO_PENDING) {
                            std::cerr << "WSASend failed: " << WSAGetLastError() << std::endl;
                            delete pSendData;
                        }
                    }
                }
            }

            // 다시 비동기 수신을 요청
            PostRecv(client);
        }

        delete pPerIoData; // 완료된 IO 데이터 삭제
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        return 1;
    }

    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!hIOCP) {
        std::cerr << "CreateIoCompletionPort failed" << std::endl;
        return 1;
    }

    std::thread acceptThread(AcceptThread);

    const int workerCount = std::thread::hardware_concurrency();
    std::vector<std::thread> workerThreads;
    for (int i = 0; i < workerCount; ++i) {
        workerThreads.emplace_back(WorkerThread);
    }

    acceptThread.join();
    for (auto& t : workerThreads) {
        t.join();
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
