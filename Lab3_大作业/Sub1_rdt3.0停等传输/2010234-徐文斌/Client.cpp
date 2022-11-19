#include <iostream>
#include <string>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <cstdlib>
#include <time.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
using namespace std;
#define MSS 14600
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4
#define LAS 0x8
#define RST 0x10
#define SEQRANGE 256
#define TCP_SYN_RETRIES 5
#define TCP_WAV_RETRIES 5
#define TIMEOUT (CLOCKS_PER_SEC / 2)

int seq_num = 0;

#pragma pack(push)
#pragma pack(1)
struct Header {
	u_short seq;
	u_short ack;
	u_short flag;
	u_short checksum;
	u_short length;
	void set_args(u_short seq, u_short ack, u_short flag, u_short checksum, u_short length) {
		this->seq = seq;
		this->ack = ack;
		this->flag = flag;
		this->checksum = checksum;
		this->length = length;
	}
};
#pragma pack(pop)

u_short checksum(char *msg, int length) {
	int size = length % 2 ? length + 1 : length;
    int count = size / 2;
	char* buf = new char[size];
    memset(buf, 0, size);
    memcpy(buf, msg, length);
    u_long sum = 0;
	u_short *buf_it = (u_short*)buf;
    while (count--) {
        sum += *buf_it++;
        if (sum & 0xffff0000) {
            sum &= 0xffff;
            sum++;
        }
    }
	delete []buf;
    return ~(sum & 0xffff);
}

int shake_hand(SOCKET *server, SOCKADDR_IN *server_addr) {
	int retry_times = TCP_SYN_RETRIES;
	static int esti_rtt = TIMEOUT;
	Header header;
	char *send_buffer = new char[sizeof(header)];
	char *recv_buffer = new char[sizeof(header)];
	// 发送第一个SYN数据报
	header.set_args(0, 0, SYN, 0, 0);
	memcpy(send_buffer, (char*)&header, sizeof(header));
	int chksum = checksum(send_buffer, sizeof(header));
	((Header*)send_buffer)->checksum = chksum;
	int res = sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
	if (res == SOCKET_ERROR) {
		delete []send_buffer;
		delete []recv_buffer;
		return -1;
	}
	cout << "向服务器发送SYN报文，请求建连" << endl;
	clock_t start = clock();
	// 非阻塞模式
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	SOCKADDR_IN serv_addr;
	int serv_addr_len = sizeof(SOCKADDR_IN);
	// 等待第二次握手信息
	while (true) {
		while (recvfrom(*server, recv_buffer, sizeof(header), 0, (sockaddr*)&serv_addr, &serv_addr_len) <= 0) {
			if (clock() - start > 1.2 * esti_rtt) {
				if (retry_times <= 0) {
					cout << "超过最大握手重传次数";
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					delete []send_buffer;
					delete []recv_buffer;
					return -1;
				}
				sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
				start = clock();
				cout << "超时，重发第一次握手信息" << endl;
				esti_rtt += CLOCKS_PER_SEC;
				retry_times--;
			}
		}
		memcpy(&header, recv_buffer, sizeof(header));
		chksum = checksum(recv_buffer, sizeof(header));
		if ((header.flag & ACK) && (header.flag & SYN) && chksum == 0) {
			cout << "接收到服务器第二次握手信息SYN = 1,ACK = 1" << endl;
			// 发送第三次握手信息
			header.set_args(0, 0, ACK, 0, 0);
			memcpy(send_buffer, (char*)&header, sizeof(header));
			chksum = checksum(send_buffer, sizeof(header));
			((Header*)send_buffer)->checksum = chksum;
			sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
			cout << "向服务端发送ACK报文" << endl;
			// 结束
			mode = 0;
			ioctlsocket(*server, FIONBIO, &mode);
			delete []send_buffer;
			delete []recv_buffer;
			return 1;
		}
	}
	// 阻塞模式
	mode = 0;
	ioctlsocket(*server, FIONBIO, &mode);
	delete []send_buffer;
	delete []recv_buffer;
	return 1;
}

bool rdt_send(SOCKET *server, SOCKADDR_IN *server_addr, char *msg, int len, bool last = false) {
	assert(len <= MSS);
	bool result = true;
	// 设置一下header
	static int esti_rtt = TIMEOUT;
	int flag = 0;
	flag = last ? LAS : 0;
	Header header;
	header.set_args(seq_num, 0, flag, 0, len);
	// 发送缓冲区
	char *send_buffer = new char[sizeof(header) + len];
	memcpy(send_buffer, (char *)&header, sizeof(header));
	memcpy(send_buffer + sizeof(header), msg, len);
	// 计算校验和
	u_short chksum = checksum(send_buffer, len + sizeof(header));
	((Header*)send_buffer)->checksum = chksum;
	// 发送数据
	int n = sendto(*server, send_buffer, sizeof(header) + len, 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
	cout << "发送数据包:";
	cout << "seq: " << header.seq << " ack: " << header.ack << " flag: " << header.flag << " checksum: " << chksum << " length: " << header.length << endl;
	// 等待来ack
	char *recv_buffer = new char[sizeof(header)];
	clock_t start = clock(); // 开启时钟
	clock_t begin = start;
	bool rep = false;
	// 开启非阻塞模式
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	while (true) {
		SOCKADDR_IN serv_addr;
		int serv_addr_len = sizeof(SOCKADDR_IN);
		while (recvfrom(*server, recv_buffer, sizeof(header), 0, (sockaddr*)&serv_addr, &serv_addr_len) <= 0) {
			// 如果超时了，要重传
			if (clock() - start > 1.2 * esti_rtt) {
				int n = sendto(*server, send_buffer, sizeof(header) + len, 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
				cout << "超时重传数据包" << endl;
				start = clock();
				rep = true;
			}
		}
		// 接收到来自服务器的报文
		memcpy(&header, recv_buffer, sizeof(header));
		cout << "接收到来自服务器的报文,头部为: ";
		cout << "seq: " << header.seq << " ack: " << header.ack << " flag: " << header.flag << " checksum: " << header.checksum << " length: " << header.length << endl;
		chksum = checksum(recv_buffer, sizeof(header));
		if (chksum == 0 && (header.flag & ACK) && header.ack == seq_num) {
			if (rep == false) 
				esti_rtt = 0.8 * esti_rtt + 0.2 * (clock() - begin);
			cout << "服务器接收成功" << endl;
			break;
		}
		else if (chksum == 0 && (header.flag & RST)) {
			cout << "连接异常" << endl;
			result = false;
			break;
		}
		else {
			continue;
		}
	}
	// 改回阻塞模式
	mode = 0;
	ioctlsocket(*server, FIONBIO, &mode);
	// 序列号反转
	seq_num ^= 1;
	delete []send_buffer;
	delete []recv_buffer;
	return result;
}

void send_file(string path, SOCKET *server, SOCKADDR_IN *server_addr) {
	ifstream file(path.c_str(), ifstream::binary);
	// 获得文件大小
	int file_length = 0;
	file.seekg(0, file.end);
	file_length = file.tellg();
	file.seekg(0, file.beg);
	// 把文件拷贝到内存缓冲区中
	int buffer_length = path.length() + file_length + 1;
	char *file_buffer = new char[buffer_length];
	memset(file_buffer, 0, sizeof(char) * buffer_length);
	memcpy(file_buffer, path.c_str(), path.length());
	file_buffer[path.length()] = 0; // 把文件名和文件内容分隔开
	file.read(file_buffer + path.length() + 1, file_length);
	file.close();
	cout << "开始发送文件" + path << "文件大小: " << file_length << "字节" << endl;
	clock_t start = clock();
	// 循环发送整个文件
	for (int offset = 0; offset < buffer_length; offset += MSS) {
		bool result = rdt_send(server, server_addr, file_buffer + offset, buffer_length - offset >= MSS ? MSS : buffer_length - offset, buffer_length - offset <= MSS ? true : false);
		if (result == false) {
			delete []file_buffer;
			system("pause");
			exit(-1);
		}
	}
	clock_t end = clock();
	cout << "发送文件" + path + "成功" << endl;
	cout << "用时: " << (end - start) / CLOCKS_PER_SEC << "s" << endl;
	cout << "吞吐率: " << buffer_length / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
	delete []file_buffer;
}

void wave_hand(SOCKET *server, SOCKADDR_IN *server_addr) {
	int retry_times = TCP_WAV_RETRIES;
	static int esti_rtt = TIMEOUT;
	int seq = rand() % SEQRANGE;
	Header header;
	char *send_buffer = new char[sizeof(header)];
	char *recv_buffer = new char[sizeof(header)];
	header.set_args(seq, 0, FIN, 0, 0);
	memcpy(send_buffer, (char*)&header, sizeof(header));
	int chksum = checksum(send_buffer, sizeof(header));
	((Header*)send_buffer)->checksum = chksum;
	// 发送fin报文
	sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
	cout << "开始挥手，向服务器发送FIN报文，seq为" << seq << endl;
	// 等待并超时重发
	// 开启非阻塞模式
	SOCKADDR_IN serv_addr;
	int serv_addr_length = sizeof(SOCKADDR_IN);
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	clock_t start = clock();
	while (true) {
		while (recvfrom(*server, recv_buffer, sizeof(header), 0, (sockaddr*)&serv_addr, &serv_addr_length) <= 0) {
			// 超时了
			if (clock() - start > 1.2 * esti_rtt) {
				// 重传次数用完了，直接返回
				if (retry_times <= 0) {
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					delete []send_buffer;
					delete []recv_buffer;
					cout << "挥手超过重传次数，直接结束" << endl;
					return;
				}
				sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
				retry_times--;
				esti_rtt += CLOCKS_PER_SEC;
				start = clock();
				cout << "超时，重发FIN报文" << endl;
			}
		}
		memcpy(&header, recv_buffer, sizeof(header));
		chksum = checksum(recv_buffer, sizeof(header));
		if (chksum == 0 && header.flag == ACK && header.ack == (seq + 1) % SEQRANGE) {
			cout << "接收到来自服务器的ACK报文，挥手成功" << endl;
			break;
		} 
		else {
			continue;
		}
	}
	mode = 0;
	ioctlsocket(*server, FIONBIO, &mode);
	delete []send_buffer;
	delete []recv_buffer;
	return;
}

int main() {
	srand(time(NULL));
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
	// 创建数据报套接字
	SOCKET server = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	server_addr.sin_port = htons(6666);
	// 获得文件名
	cout << "请输入文件名: ";
	string file_path;
	cin >> file_path;
	// 握手
	if (shake_hand(&server, &server_addr) == -1) {
		cout << "与服务器握手失败" << endl;
		closesocket(server);
		WSACleanup();
		exit(-1);
	}
	else {
		cout << "与服务器握手成功" << endl;
	}
	// 发送文件
	send_file(file_path, &server, &server_addr);
	// 挥手
	wave_hand(&server, &server_addr);
	closesocket(server);
	WSACleanup();
	system("pause");
	return 0;
}