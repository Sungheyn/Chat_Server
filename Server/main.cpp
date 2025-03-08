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
    OVERLAPPED overlapped; // �ݵ�� ù ��° ����� ��
    WSABUF wsabuf;
    char buffer[1024];     // ����/���� ������ ����
    IO_OPERATION op;       // �۾� ���� (�б�/����)
};

// Ŭ���̾�Ʈ ���Ͽ� ���ο� �񵿱� ������ ��û�ϴ� �Լ�
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

// Ŭ���̾�Ʈ ������ �����ϰ� IOCP�� ����ϴ� �Լ�
void AcceptThread() {
    while (true) {
        SOCKET client = accept(listenSocket, NULL, NULL);
        if (client == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        clients.push_back(client);

        // IOCP�� Ŭ���̾�Ʈ ���� ���
        if (!CreateIoCompletionPort((HANDLE)client, hIOCP, (ULONG_PTR)client, 0)) {
            std::cerr << "CreateIoCompletionPort failed: " << GetLastError() << std::endl;
            closesocket(client);
            continue;
        }

        std::cout << "Client connected: " << client << std::endl;

        // ù ��° �񵿱� ���� ��û
        PostRecv(client);
    }
}

// IOCP���� �Ϸ�� �۾��� ó���ϴ� ��Ŀ ������
void WorkerThread() {
    while (true) {
        DWORD bytesTransferred;
        SOCKET client;
        OVERLAPPED* overlapped;

        BOOL result = GetQueuedCompletionStatus(hIOCP, &bytesTransferred, (PULONG_PTR)&client, &overlapped, INFINITE);
        PER_IO_DATA* pPerIoData = (PER_IO_DATA*)overlapped;

        if (!result || bytesTransferred == 0) {
            // ���� �߻� �Ǵ� Ŭ���̾�Ʈ ���� ����
            std::cout << "Client disconnected: " << client << std::endl;
            closesocket(client);
            delete pPerIoData;
            continue;
        }

        if (pPerIoData->op == IO_READ) {
            // ������ ������ ���
            std::cout << "Received from client (" << client << "): " << std::string(pPerIoData->buffer, bytesTransferred) << std::endl;

            // �ٸ� Ŭ���̾�Ʈ���� ��ε�ĳ��Ʈ
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

            // �ٽ� �񵿱� ������ ��û
            PostRecv(client);
        }

        delete pPerIoData; // �Ϸ�� IO ������ ����
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
