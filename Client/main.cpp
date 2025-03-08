#include <iostream>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <string>
#pragma comment(lib, "ws2_32.lib")
#include <thread>
SOCKET clientSocket;
void RecvThread() {
	while (1) {
		char buffer[1024];
		memset(buffer, 0, sizeof(buffer));
		recv(clientSocket, buffer, 1024, 0);
		std::cout << buffer << std::endl;
	}
}

int main(void) {
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	 clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(8888);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	std::string name;
	std::cout << "Enter your name: ";
	std::getline(std::cin, name);
	connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
	std::cout << "Connected to Server!" << std::endl;
	std::thread t(RecvThread);
	while (1) {
		std::string buffer;
		std::getline(std::cin, buffer);
		buffer = name + ": " + buffer;
		send(clientSocket, buffer.c_str(), strlen(buffer.c_str()), 0);
	}
	WSACleanup();
	return 0;
}