#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <windows.h>
#include <stdlib.h>
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <stdint.h>
#define TCP_MAX_SIZE 65507

using namespace std;

vector<string> msgs;

int init()
{
	WSADATA wsaData;
	return (0 == WSAStartup(MAKEWORD(2, 2), &wsaData));
}

void readFile(char* fileName)
{
	ifstream fdesc(fileName);
	string msg;
	while (getline(fdesc, msg))
	{
		if (!msg.empty())
		{
			msgs.push_back(msg);
		}
		msg.clear();
	}
	fdesc.close();
}

void append_chunk(string& s, const uint8_t* chunk, size_t chunk_num_bytes)
{
	s.append((char*)chunk, chunk_num_bytes);
}

void transferData(SOCKET s)
{
	char ok[3] = { 0 };
	int okCount = 0;
	int msgCount = msgs.size();
	while (okCount != msgCount)
	{
		string currentMsg = msgs[okCount], send_msg, token;
		vector<string> Tokens;
		istringstream _stream(currentMsg);
		while (std::getline(_stream, token, ' '))
		{
			if (!token.empty())
			{
				Tokens.push_back(token);
			}
			token.clear();
		}
		vector<string> dateParts;
		stringstream date(Tokens[0]);
		string part;
		while (std::getline(date, part, '.'))
		{
			dateParts.push_back(part);
			part.clear();
		}
		uint32_t index_in_message = htonl(okCount);
		uint8_t day = (uint8_t)atoi(dateParts[0].c_str());
		uint8_t month = (uint8_t)atoi(dateParts[1].c_str());
		uint16_t year = htons(atoi(string(dateParts[2].begin(), dateParts[2].end()).c_str()));
		int16_t AA = htons(atoi(Tokens[1].c_str()));
		/* Phone Number Process */
		char* phone = (char*)Tokens[2].c_str();
		/* Packing message process */
		send_msg.reserve(currentMsg.size() + 4);
		append_chunk(send_msg, (const uint8_t*)&index_in_message, sizeof(uint32_t));
		append_chunk(send_msg, (const uint8_t*)&day, 1);
		append_chunk(send_msg, (const uint8_t*)&month, 1);
		append_chunk(send_msg, (const uint8_t*)&year, sizeof(uint16_t));
		append_chunk(send_msg, (const uint8_t*)&AA, sizeof(signed short));
		append_chunk(send_msg, (const uint8_t*)phone, strlen(phone));
		/* Pacling all data after phone */
		size_t data_pos = currentMsg.find("+") + 13;
		string data = currentMsg.substr(data_pos);
		append_chunk(send_msg, (const uint8_t*)data.c_str(), data.length());
		send_msg[send_msg.size()] = '\0';
		int status = send(s, send_msg.c_str(), send_msg.size() + 1, 0);
		int r;
		while ((r = recv(s, ok, 2, 0)))
		{
			if (strcmp(ok, "ok") == 0)
			{
				okCount++;
				break;
			}
		}
	}
}

int main(int argc, char** argv)
{
	bool isConnected = false;
	if (argc != 3)
	{
		cout << "Usage ./tcpclient IP:PORT TEXTFILE" << endl;
		return 0;
	}
	char* ip = strtok(argv[1], ":");
	char* port = strtok(NULL, "\0");
	struct sockaddr_in addr;
	init();
	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
	{
		cout << "socket() function error" << endl;
		return 0;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(port));
	addr.sin_addr.s_addr = inet_addr(ip);
	for (int connAttempt = 0; connAttempt < 10 && !isConnected; connAttempt++)
	{
		if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != -1)
		{
			int startMessaging = send(s, "put", 3, 0);
			if (startMessaging < 0)
			{
				cout << "Sending problems..." << endl;
				break;
			}
			isConnected = true;
			cout << "Connection complete!" << endl;
		}
		else
		{
			cout << "Attempt to connect..." << endl;
			Sleep((DWORD)100);
		}
	}
	if (!isConnected)
	{
		cout << "Connection refused" << endl;
		closesocket(s);
		return 0;
	}
	readFile(argv[2]);
	transferData(s);
	closesocket(s);
	WSACleanup();
	return 0;
}