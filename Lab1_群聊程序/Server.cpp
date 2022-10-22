#include <iostream>
#include <string>
#include <map>
#include <set>
#include <time.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")

#define MAX_CLIENT 10
#define BUFFER_SIZE 1024
#define SERVER_PORT 2333
#define WAIT_TIME 1000
using namespace std;
set<int> free_ids;
set<int> used_ids;
map<int, string> id2name;
map<string, int> name2id;
map<int, SOCKET*> id2socket;

struct Wrap_Data {
	string message;
	int client_from;
	int client_to;
};

struct Message {
	string type;
	string name;
	string time;
	string sendto;
	Message() {
		type = "message";
		name = time = sendto = "";
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
		if (s_field == "in" || s_field == "out" || s_field == "message") {
			message.type = s_field;
		}
		else if (s_field.rfind("name:", 0) == 0) {
			message.name = s_field.substr(5, s_field.length() - 5);
		} else if (s_field.rfind("time:", 0) == 0) {
			message.time = s_field.substr(5, s_field.length() - 5);
		} else if (s_field.rfind("sendto:", 0) == 0) {
			message.sendto = s_field.substr(7, s_field.length() - 7);
		}
		field = strtok(NULL, delim);
	}
	return message;
}

DWORD WINAPI send_to_users(LPVOID lpParam) {
	char send_buff[BUFFER_SIZE];
	Wrap_Data wpd = *(Wrap_Data*)lpParam;
	delete (Wrap_Data*)lpParam;
	int client_from = wpd.client_from;
	int client_to = wpd.client_to;
	strcpy_s(send_buff, wpd.message.data());
	for (auto it = used_ids.begin(); client_to == -1 && it != used_ids.end(); it++) {
		if (client_from == *it || id2name[*it] == "") {
			continue;
		}
		int send_byte = send(*id2socket[*it], send_buff, sizeof(send_buff), 0);
	}
	if (client_to != -1) {
		int send_byte = send(*id2socket[client_to], send_buff, sizeof(send_buff), 0);
	}
	return 0;
}

DWORD WINAPI recv_thread(LPVOID lpParam) {
	//一直接收客户端的数据
	int client_no = (int) lpParam;
	SOCKET* client_socket = (SOCKET*)id2socket[client_no];
	char recv_buff[BUFFER_SIZE];
	HANDLE handle;
	while (true) {
		int recv_byte = recv(*client_socket, recv_buff, sizeof(recv_buff), 0);
		if (recv_byte > 0) {
			Wrap_Data* wpd = new Wrap_Data;
			Wrap_Data* wpd1 = new Wrap_Data;
			Message message = parse_message(recv_buff);
			cout << get_now_time() << ":" << "接收到来自" << message.name << "的报文:" << endl << recv_buff << endl << endl;
			//根据报文的不同类型进行处理转发
			string msg = "";
			wpd->client_from = client_no;
			if (message.type == "message") {
				msg.append(recv_buff);
				wpd->message = msg;
				if (message.sendto == "all") {
					wpd->client_to = -1;
					cout << get_now_time() << ":" << "向所有用户（除了" << message.name << "）发送报文:" << endl << wpd->message << endl << endl;
				} else {
					wpd->client_to = name2id[message.sendto];
					if (wpd->client_to == 0) {
						cout << get_now_time() << ":" << "用户" << message.sendto << "不存在" << endl << endl;
						continue;
					}
					cout << get_now_time() << ":" << "向用户" << message.sendto << "发送报文:" << endl << wpd->message << endl << endl;
				}
				handle = CreateThread(NULL, 0, send_to_users, (LPVOID)wpd, 0, NULL);
				CloseHandle(handle);
			} else if (message.type == "in") {
				if (name2id.find(message.name) != name2id.end() && id2socket[name2id[message.name]] != NULL) {
					wpd->client_to = client_no;
					wpd->message = "answer nameconflict";
					cout << get_now_time() << ":" << message.name << "名字重复拒绝进入聊天室" << endl << endl;
					cout << get_now_time() << ":" << "向用户" << message.name << "发送报文:" << endl << wpd->message << endl << endl;
					handle = CreateThread(NULL, 0, send_to_users, (LPVOID)wpd, 0, NULL);
					CloseHandle(handle);
				} else {
					name2id[message.name] = client_no;
					id2name[client_no] = message.name;
					cout << get_now_time() << ":" << message.name << "加入聊天室" << endl << endl;
					wpd->client_to = client_no;
					wpd->message = "answer accept";
					cout << get_now_time() << ":" << "向用户" << message.name << "发送报文:" << endl << wpd->message << endl << endl;
					handle = CreateThread(NULL, 0, send_to_users, (LPVOID)wpd, 0, NULL);
					CloseHandle(handle);
					wpd1->message = "";
					wpd1->message.append(recv_buff);
					wpd1->client_from = client_no;
					wpd1->client_to = -1;
					cout << get_now_time() << ":" << "向所有用户（除了" << message.name << "）发送报文:" << endl << wpd1->message << endl << endl;
					handle = CreateThread(NULL, 0, send_to_users, (LPVOID)wpd1, 0, NULL);
					CloseHandle(handle);
				}
			} else if (message.type == "out") {
				wpd->client_to = -1;
				wpd->message = "";
				wpd->message.append(recv_buff);
				cout << get_now_time() << ":" << "用户" << message.name << "离开聊天室" << endl << endl;
				cout << get_now_time() << ":" << "向所有用户发送报文:" << endl << wpd->message << endl << endl;
				handle = CreateThread(NULL, 0, send_to_users, (LPVOID)wpd, 0, NULL);
				CloseHandle(handle);
				if (id2name.find(client_no) != id2name.end()) {
					name2id[id2name[client_no]] = -1;
					id2name[client_no] = "";
					id2socket[client_no] = NULL;
					free_ids.insert(client_no);
					used_ids.erase(client_no);
				}
				closesocket(*client_socket);
				delete client_socket;
				return 0;
			} else {
				cout << get_now_time() << ":" << "来自" << message.name << "的报文解析错误" << endl << endl;
			} 
		}
		else {
			if (errno != EINTR) {
				cout << get_now_time() << ":" << "用户" << id2name[client_no] << "断开连接" << endl << endl;
				Wrap_Data* wpd = new Wrap_Data;
				wpd->client_from = client_no;
				wpd->client_to = -1;
				wpd->message = "out\nname:" + id2name[client_no];
				if (id2name.find(client_no) != id2name.end()) {
					name2id[id2name[client_no]] = -1;
					id2name[client_no] = "";
					id2socket[client_no] = NULL;
					free_ids.insert(client_no);
					used_ids.erase(client_no);
				}
				cout << get_now_time() << ":" << "向所有用户发送报文:" << endl << wpd->message << endl << endl;
				handle = CreateThread(NULL, 0, send_to_users, (LPVOID)wpd, 0, NULL);
				CloseHandle(handle);
				closesocket(*client_socket);
				delete client_socket;
				return 0;
			}
		}
		memset(recv_buff, 0, sizeof(recv_buff));
	}
	return 0;
}

int main() {
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == -1) {
		cout << get_now_time() << ":" << "创建socket失败" << endl;
		system("pause");
		return -1;
	} else {
		cout << get_now_time() << ":" << "创建socket成功" << endl;
	}
	cout << "请输入服务器绑定的端口号(默认为2333): ";
	string server_port;
	getline(cin, server_port);
	//服务器监听地址
	SOCKADDR_IN listen_addr;
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	listen_addr.sin_port = htons(server_port.length() > 1 ? stoi(server_port) : SERVER_PORT);
	int error;
	error = bind(listen_socket, (sockaddr*)&listen_addr, sizeof(listen_addr));
	if (error == SOCKET_ERROR) {
		cout << get_now_time() << ":" << "服务器端口绑定失败" << endl;
		system("pause");
		return -1;
	} else {
		cout << get_now_time() << ":" << "服务器端口绑定成功" << endl;
	}
	error = listen(listen_socket, MAX_CLIENT);
	if (error != 0) {
		cout << get_now_time() << ":" << "监听失败，原因为: " << GetLastError() << endl;
	} else {
		cout << get_now_time() << ":" << "成功进入监听状态" << endl;
	}
	//初始free_ids队列，用于获取可得的sockeid
	free_ids.clear();
	used_ids.clear();
	for (int i = 0; i < MAX_CLIENT; i++) {
		free_ids.insert(i + 1);
	}
	while (true) {
		if (used_ids.size() < MAX_CLIENT) {
			SOCKET* server_socket = new SOCKET;
			*server_socket = accept(listen_socket, 0, 0);
			int client_no = *free_ids.begin();
			id2socket[client_no] = server_socket;
			id2name[client_no] = "";
			used_ids.insert(client_no);
			free_ids.erase(client_no);
			HANDLE recv_th;
			recv_th = CreateThread(NULL, 0, recv_thread, (LPVOID)client_no, 0, NULL);
			CloseHandle(recv_th);
		} else {
			Sleep(WAIT_TIME);
		}
	}
	closesocket(listen_socket);
	WSACleanup();
	cout << "ByeBye" << endl;
	system("pause");
	return 0;
}