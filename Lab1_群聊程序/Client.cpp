#include <iostream>
#include <string>
#include <map>
#include <stdio.h>
#include <time.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")

#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 2333
using namespace std;
string name;
string prompt;

struct Message {
	string type;
	string name;
	string time;
	string sendto;
	string msg;
	Message() {
		type = "message";
		name = time = sendto = msg = "";
	}
};

string get_now_time() {
	time_t timep;
	time(&timep);
	char tmp[256];
	strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", localtime(&timep));
	string r_time = "";
	r_time.append(tmp);
	return r_time;
}

string construct_message(Message message) {
	string send_msg = "";
	if (message.type == "message") {
		send_msg += "message\n";
		send_msg += "name:" + message.name + "\n";
		send_msg += "time:" + message.time + "\n";
		send_msg += "sendto:" + message.sendto + "\n";
		send_msg += "msg:" + message.msg;
	} else if (message.type == "in") {
		send_msg += "in\n";
		send_msg += "name:" + message.name;
	} else if (message.type == "out") {
		send_msg += "out\n";
		send_msg += "name:" + message.name;
	}
	return send_msg;
}

Message parse_message(char* msg_buff) {
	char message_buff[BUFFER_SIZE];
	strcpy(message_buff, msg_buff);
	Message message;
	char* field;
	const char delim[2] = "\n";
	field = strtok(message_buff, delim);
	while (field != NULL) {
		string s_field = "";
		s_field.append(field);
		if (s_field == "in" || s_field == "out" || s_field == "message" || s_field.rfind("answer", 0) == 0) {
			message.type = s_field;
		}
		else if (s_field.rfind("name:", 0) == 0) {
			message.name = s_field.substr(5, s_field.length() - 5);
		} else if (s_field.rfind("time:", 0) == 0) {
			message.time = s_field.substr(5, s_field.length() - 5);
		} else if (s_field.rfind("sendto:", 0) == 0) {
			message.sendto = s_field.substr(7, s_field.length() - 7);
		} else if (s_field.rfind("msg:", 0) == 0) {
			message.msg = s_field.substr(4, s_field.length() - 4);
		}
		field = strtok(NULL, delim);
	}
	return message;
}

DWORD WINAPI recv_thread(LPVOID lpParam) {
	//һֱ���շ�����������
	SOCKET* client_socket = (SOCKET*)lpParam;
	char recv_buff[BUFFER_SIZE];
	while (true) {
		int recv_byte = recv(*client_socket, recv_buff, sizeof(recv_buff), 0);
		if (recv_byte > 0) {
			Message message = parse_message(recv_buff);
			if (message.type == "message") {
				if (message.sendto == "all") 
					cout << endl << message.time << endl << "@" << message.name << " said:" << endl << message.msg << endl << prompt;
				else
					cout << endl << message.time << endl << "@" << message.name << " said to you:" << endl << message.msg << endl << prompt;
			} else if (message.type == "in") {
				cout << endl << message.name << "����������" << endl << prompt;
			} else if (message.type == "out") {
				cout << endl << message.name << "�뿪������" << endl << prompt;
			}
		}
		memset(recv_buff, 0, sizeof(recv_buff));
	}
	return 0;
}

void try_to_enter_room(SOCKET* socket) {
	cout << "����������: ";
	cin >> name;
	getchar();
	SOCKET* client_socket = socket;
	char send_buff[BUFFER_SIZE];
	char recv_buff[BUFFER_SIZE];
	Message greet_msg;
	greet_msg.type = "in";
	greet_msg.name = name;
	string send_msg = construct_message(greet_msg);
	strcpy(send_buff, send_msg.data());
	send(*client_socket, send_buff, sizeof(send_buff), 0);
	while (recv(*client_socket, recv_buff, sizeof(recv_buff), 0) != -1) {
		Message message = parse_message(recv_buff);
		string status = message.type.substr(7, message.type.length() - 7);
		if (status == "accept") {
			break;
		}
		cout << "�����ظ��ˣ�����һ����: ";
		cin >> name;
		getchar();
		greet_msg.name = name;
		greet_msg.time = get_now_time();
		string send_msg = construct_message(greet_msg);
		strcpy(send_buff, send_msg.data());
		send(*client_socket, send_buff, sizeof(send_buff), 0);
	}
	cout << "�ɹ�����������" << endl;
	prompt = name + "> ";
	return;
}

void send_msg_to_server(SOCKET* socket) {
	SOCKET* client_socket = socket;
	char send_buff[BUFFER_SIZE];
	memset(send_buff, 0, sizeof(send_buff));
	Message message;
	message.type = "message";
	message.name = name;
	string msg = "";
	string send_msg;
	while (true) {
		cout << prompt;
		getline(cin, msg);
		message.time = get_now_time();
		message.sendto = "";
		message.msg = "";
		int i = 2;
		if (msg.rfind("st", 0) == 0) {
			if (msg.length() <= 3) {
				goto L;
			}
			for (i = 3; msg[i] != ' ' && msg[i] != '\0'; i++) {
				message.sendto += msg[i];
			}
			if (msg.length() <= i + 1) {
				goto L;
			}
			i++;
		}
		else if (msg.rfind("s", 0) == 0) {
			if (msg.length() <= 2) {
				goto L;
			}
			message.sendto = "all";
		} else if (msg.rfind("exit", 0) == 0) {
			message.type = "out";
		} else {
			L: cout << "���벻�Ϸ����������������ʽ" << endl << "s msg" << "��ʾ��msgȺ����������" << endl << "st name msg" << \
				"��ʾ��msg���͸�����Ϊname����" << endl << "exit" << "��ʾ�˳��ͻ���" << endl;
			continue;
		}
		message.msg.append(msg.substr(i, msg.length() - i));
		send_msg = construct_message(message);
		strcpy(send_buff, send_msg.data());
		int send_byte = send(*client_socket, send_buff, sizeof(send_buff), 0);
		if (send_byte < 0) {
			cout << "����ʧ��" << endl;
		} else {
			cout << "���ͳɹ�" << endl;
		}
		memset(send_buff, 0, sizeof(send_buff));
		if (message.type == "out") {
			return;
		}
	}
}

int main() {
	//���ӷ�����
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (client_socket == -1) {
		cout << "����socketʧ��" << endl;
		system("pause");
		return -1;
	} else {
		cout << "����socket�ɹ�" << endl;
	}
	cout << "�������������IP4��ַ(Ĭ��Ϊ127.0.0.1): ";
	string server_ip;
	getline(cin, server_ip);
	cout << "������������Ķ˿ں�(Ĭ��Ϊ2333): ";
	string server_port;
	getline(cin, server_port);
	//��������ַ
	SOCKADDR_IN server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.S_un.S_addr = inet_addr(server_ip.length() > 1 ? server_ip.data() : SERVER_IP);
	server_addr.sin_port = htons(server_port.length() > 1 ? stoi(server_port) : SERVER_PORT);
	int error;
	error = connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr));
	if (error == SOCKET_ERROR) {
		cout << "���ӷ�����ʧ��" << endl;
		system("pause");
		return -1;
	} else {
		cout << "���ӷ������ɹ�" << endl;
	}
	//�������������
	try_to_enter_room(&client_socket);
	//���������߳�
	HANDLE recv_th;
	recv_th = CreateThread(NULL, 0, recv_thread, &client_socket, 0, NULL);
	CloseHandle(recv_th);
	//������Ϣ���ʹ���
	send_msg_to_server(&client_socket);
	//�ͻ��˽���
	WaitForSingleObject(recv_th, 0);
	closesocket(client_socket);
	WSACleanup();
	cout << "ByeBye" << endl;
	system("pause");
	return 0;
}