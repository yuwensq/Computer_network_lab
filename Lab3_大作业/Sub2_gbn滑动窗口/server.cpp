#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <time.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
using namespace std;
#define MSS 6666
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4
#define LAS 0x8
#define RST 0x10
#define TIMEOUT (CLOCKS_PER_SEC / 2)
#define TCP_SYNACK_RETRIES 5
#define WAITTIME (CLOCKS_PER_SEC * 5)

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

u_short expectedseqnum = 0;

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

int shake_hand(SOCKET *server, SOCKADDR_IN *server_addr, char *data_buffer, int *len) {
	int retry_times = TCP_SYNACK_RETRIES;
	int esti_rtt = TIMEOUT;
	Header header;
	char *send_buffer = new char[sizeof(header)];
	char *recv_buffer = new char[sizeof(header) + MSS];
	SOCKADDR_IN client_addr;
	int client_addr_length = sizeof(SOCKADDR_IN);
	while (true) {
		int res = recvfrom(*server, recv_buffer, sizeof(header), 0, (sockaddr*)&client_addr, &client_addr_length);
		if (res == SOCKET_ERROR) {
			cout << strerror(errno);
			Sleep(2000);
			continue;
		}
		// 收到来自客户端的建连请求
		memcpy(&header, recv_buffer, sizeof(header));
		int chksum = checksum(recv_buffer, sizeof(header));
		if (chksum == 0 && (header.flag & SYN)) {
			cout << "接收到来自客户端的SYN建连请求报文" << endl;
			header.set_args(0, 0, ACK + SYN, 0, 0);
			memcpy(send_buffer, (char*)&header, sizeof(header));
			chksum = checksum(send_buffer, sizeof(header));
			((Header*)send_buffer)->checksum = chksum;
			// 发送第二次握手信息给客户端
			int n = sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			if (n == SOCKET_ERROR) {
				delete []send_buffer;
				delete []recv_buffer;
				return -1;
			}
			cout << "发回给客户端SYN、ACK报文" << endl;
			// 非阻塞模式
			u_long mode = 1;
			ioctlsocket(*server, FIONBIO, &mode);
			clock_t start = clock();
			// 等待接收第三次握手信息
			while (true) {
				int result;
				while ((result = recvfrom(*server, recv_buffer, sizeof(header) + MSS, 0, (sockaddr*)&client_addr, &client_addr_length)) <= 0) {
					if (clock() - start > 1.2 * esti_rtt) {
						if (retry_times <= 0) {
							cout << "超过SYN、ACK报文重传上限，握手失败" << endl;
							// 超过大重传次数
							delete []send_buffer;
							delete []recv_buffer;
							mode = 0;
							ioctlsocket(*server, FIONBIO, &mode);
							return -1;
						}
						int n = sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
						cout << "等待ACK超时，重发SYN、ACK报文" << endl;
						retry_times--;
						esti_rtt += CLOCKS_PER_SEC;
						start = clock();
					}
				}
				memcpy(&header, recv_buffer, sizeof(header));
				chksum = checksum(recv_buffer, result);
				if (chksum == 0 && (header.flag & ACK)) {
					// 成功收到第三次握手信息
					delete []send_buffer;
					delete []recv_buffer;
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					cout << "接收到来自客户端的ACK包，握手成功" << endl;
					return 1;
				}
				else if (chksum == 0 && (header.flag & SYN)) {
					start = clock();
					sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
				}
				// 如果接收到了数据报文，发回给客户端一个rst报文
				else if (chksum == 0 && header.flag == 0){
					Header reply_header;
					reply_header.set_args(0, 0, RST, 0, 0);
					memcpy(send_buffer, (char*)&reply_header, sizeof(reply_header));
					chksum = checksum(send_buffer, sizeof(reply_header));
					((Header*)send_buffer)->checksum = chksum;
					int n = sendto(*server, send_buffer, sizeof(reply_header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
					cout << "接收到来自客户端的数据，发送RST报文，握手失败" << endl;
					delete []send_buffer;
					delete []recv_buffer;
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					return -1;
				} 
				// 真的嘎了
				else {
					delete []send_buffer;
					delete []recv_buffer;
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					return -1;
				}
			}
		}
	}
	return 1;
}

void recv_file(SOCKET *server, SOCKADDR_IN *server_addr, char *data_buffer, int *len) {
	bool file_finish = false;
	SOCKADDR_IN client_addr;
	int client_addr_length = sizeof(SOCKADDR_IN);
	Header header;
	// 接收缓冲和发送缓冲
	char *recv_buffer = new char[MSS + sizeof(header)];
	char *send_buffer = new char[sizeof(header)];
	memset(recv_buffer, 0, MSS + sizeof(header));
	memset(send_buffer, 0, sizeof(header));
	// 先初始化一下send_buffer
	((Header*)send_buffer)->set_args(0, -1, ACK, 0, 0);
	((Header*)send_buffer)->checksum = checksum(send_buffer, sizeof(Header));
	// 一直接收数据
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	clock_t start;
	while (true) {
		int result;
		while ((result = recvfrom(*server, recv_buffer, MSS + sizeof(header), 0, (sockaddr*)&client_addr, &client_addr_length)) <= 0) {
			// 长时间没有接收到信息，且文件传完了，认为对方嘎了
			if (file_finish == true && clock() - start > WAITTIME) {
				cout << "长时间没有接收到报文，断开连接";
				delete []send_buffer;
				delete []recv_buffer;
				mode = 0;
				ioctlsocket(*server, FIONBIO, &mode);
				return;
			}
		}
		// 得到数据头
		memcpy(&header, recv_buffer, sizeof(header));
		cout << "接收到长度为" << result << "字节的数据报,头部为: ";
		cout << "seq: " << header.seq << " ack: " << header.ack << " flag: " << header.flag << " checksum: " << header.checksum << " length: " << header.length << endl;
		// 先计算一下校验和
		u_short chksum = checksum(recv_buffer, result);
		// 校验和不对
		if (chksum != 0) {
			int n = sendto(*server, send_buffer, sizeof(Header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			cout << "数据报校验和出错" << endl;
			continue;
		}
		if (header.flag == FIN) {
			Header reply_header;
			reply_header.set_args(0, (u_short)(header.seq + 1), ACK, 0, 0);
			memcpy(send_buffer, (char*)&reply_header, sizeof(reply_header));
			chksum = checksum(send_buffer, sizeof(reply_header));
			((Header*)send_buffer)->checksum = chksum;
			int n = sendto(*server, send_buffer, sizeof(reply_header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			cout << "接收到FIN报文，发回ACK报文，传输结束" << endl;
			break;
		}
		// 接收到一个数据报文
		else if (header.seq == expectedseqnum) {
			// 发回一个ack
			Header reply_header;
			reply_header.set_args(0, expectedseqnum, ACK, 0, 0);
			memcpy(send_buffer, (char*)&reply_header, sizeof(reply_header));
			chksum = checksum(send_buffer, sizeof(reply_header));
			((Header*)send_buffer)->checksum = chksum;
			int n = sendto(*server, send_buffer, sizeof(reply_header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			cout << "发送ACK报文:" << "seq: " << reply_header.seq << " ack: " << reply_header.ack << " flag: " << reply_header.flag << " checksum: " << chksum << " length: " << reply_header.length << endl;
			expectedseqnum++;
			memcpy(data_buffer + *len, recv_buffer + sizeof(header), header.length);
			*len += header.length;
			cout << "数据报成功获取" << endl;
			// 最后一个报文到了
			if (header.flag & LAS) {
				file_finish = true;
				start = clock();
				cout << "文件接收完毕，等待客户端挥手" << endl;
			}
		}
		else {
			int n = sendto(*server, send_buffer, sizeof(Header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
		}
	}
	delete []send_buffer;
	delete []recv_buffer;
	mode = 0;
	ioctlsocket(*server, FIONBIO, &mode);
}

int main() {
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
	// 创建数据报套接字
	SOCKET server = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	server_addr.sin_port = htons(8888);
	if (bind(server, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
		cout << "绑定端口失败" << endl;
		closesocket(server);
		WSACleanup();
		exit(-1);
	}
	else {
		cout << "绑定端口成功" << endl;
	}
	char *data = new char[20000000];
	int data_len = 0;
	if (shake_hand(&server, &server_addr, data, &data_len) != -1) {
		recv_file(&server, &server_addr, data, &data_len);
		string file_name = "";
		int i = 0;
		for (i = 0; i < data_len; i++) {
			if (data[i] == 0) {
				break;
			}
			file_name += data[i];
		}
		cout << "接收到文件" + file_name << "长度为: " << data_len - i - 1 << endl;
		ofstream file(file_name.c_str(), ofstream::binary);
		file.write(data + i + 1, data_len - i - 1);
		file.close();
		delete []data;
	}
	else {
		cout << "握手失败，请重启客户端和服务端" << endl;
	}
	closesocket(server);
	WSACleanup();
	system("pause");
	return 0;
}