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
	// ���͵�һ��SYN���ݱ�
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
	cout << "�����������SYN���ģ�������" << endl;
	clock_t start = clock();
	// ������ģʽ
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	SOCKADDR_IN serv_addr;
	int serv_addr_len = sizeof(SOCKADDR_IN);
	// �ȴ��ڶ���������Ϣ
	while (true) {
		while (recvfrom(*server, recv_buffer, sizeof(header), 0, (sockaddr*)&serv_addr, &serv_addr_len) <= 0) {
			if (clock() - start > 1.2 * esti_rtt) {
				if (retry_times <= 0) {
					cout << "������������ش�����";
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					delete []send_buffer;
					delete []recv_buffer;
					return -1;
				}
				sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
				start = clock();
				cout << "��ʱ���ط���һ��������Ϣ" << endl;
				esti_rtt += CLOCKS_PER_SEC;
				retry_times--;
			}
		}
		memcpy(&header, recv_buffer, sizeof(header));
		chksum = checksum(recv_buffer, sizeof(header));
		if ((header.flag & ACK) && (header.flag & SYN) && chksum == 0) {
			cout << "���յ��������ڶ���������ϢSYN = 1,ACK = 1" << endl;
			// ���͵�����������Ϣ
			header.set_args(0, 0, ACK, 0, 0);
			memcpy(send_buffer, (char*)&header, sizeof(header));
			chksum = checksum(send_buffer, sizeof(header));
			((Header*)send_buffer)->checksum = chksum;
			sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
			cout << "�����˷���ACK����" << endl;
			// ����
			mode = 0;
			ioctlsocket(*server, FIONBIO, &mode);
			delete []send_buffer;
			delete []recv_buffer;
			return 1;
		}
	}
	// ����ģʽ
	mode = 0;
	ioctlsocket(*server, FIONBIO, &mode);
	delete []send_buffer;
	delete []recv_buffer;
	return 1;
}

bool rdt_send(SOCKET *server, SOCKADDR_IN *server_addr, char *msg, int len, bool last = false) {
	assert(len <= MSS);
	bool result = true;
	// ����һ��header
	static int esti_rtt = TIMEOUT;
	int flag = 0;
	flag = last ? LAS : 0;
	Header header;
	header.set_args(seq_num, 0, flag, 0, len);
	// ���ͻ�����
	char *send_buffer = new char[sizeof(header) + len];
	memcpy(send_buffer, (char *)&header, sizeof(header));
	memcpy(send_buffer + sizeof(header), msg, len);
	// ����У���
	u_short chksum = checksum(send_buffer, len + sizeof(header));
	((Header*)send_buffer)->checksum = chksum;
	// ��������
	int n = sendto(*server, send_buffer, sizeof(header) + len, 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
	cout << "�������ݰ�:";
	cout << "seq: " << header.seq << " ack: " << header.ack << " flag: " << header.flag << " checksum: " << chksum << " length: " << header.length << endl;
	// �ȴ���ack
	char *recv_buffer = new char[sizeof(header)];
	clock_t start = clock(); // ����ʱ��
	clock_t begin = start;
	bool rep = false;
	// ����������ģʽ
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	while (true) {
		SOCKADDR_IN serv_addr;
		int serv_addr_len = sizeof(SOCKADDR_IN);
		while (recvfrom(*server, recv_buffer, sizeof(header), 0, (sockaddr*)&serv_addr, &serv_addr_len) <= 0) {
			// �����ʱ�ˣ�Ҫ�ش�
			if (clock() - start > 1.2 * esti_rtt) {
				int n = sendto(*server, send_buffer, sizeof(header) + len, 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
				cout << "��ʱ�ش����ݰ�" << endl;
				start = clock();
				rep = true;
			}
		}
		// ���յ����Է������ı���
		memcpy(&header, recv_buffer, sizeof(header));
		cout << "���յ����Է������ı���,ͷ��Ϊ: ";
		cout << "seq: " << header.seq << " ack: " << header.ack << " flag: " << header.flag << " checksum: " << header.checksum << " length: " << header.length << endl;
		chksum = checksum(recv_buffer, sizeof(header));
		if (chksum == 0 && (header.flag & ACK) && header.ack == seq_num) {
			if (rep == false) 
				esti_rtt = 0.8 * esti_rtt + 0.2 * (clock() - begin);
			cout << "���������ճɹ�" << endl;
			break;
		}
		else if (chksum == 0 && (header.flag & RST)) {
			cout << "�����쳣" << endl;
			result = false;
			break;
		}
		else {
			continue;
		}
	}
	// �Ļ�����ģʽ
	mode = 0;
	ioctlsocket(*server, FIONBIO, &mode);
	// ���кŷ�ת
	seq_num ^= 1;
	delete []send_buffer;
	delete []recv_buffer;
	return result;
}

void send_file(string path, SOCKET *server, SOCKADDR_IN *server_addr) {
	ifstream file(path.c_str(), ifstream::binary);
	// ����ļ���С
	int file_length = 0;
	file.seekg(0, file.end);
	file_length = file.tellg();
	file.seekg(0, file.beg);
	// ���ļ��������ڴ滺������
	int buffer_length = path.length() + file_length + 1;
	char *file_buffer = new char[buffer_length];
	memset(file_buffer, 0, sizeof(char) * buffer_length);
	memcpy(file_buffer, path.c_str(), path.length());
	file_buffer[path.length()] = 0; // ���ļ������ļ����ݷָ���
	file.read(file_buffer + path.length() + 1, file_length);
	file.close();
	cout << "��ʼ�����ļ�" + path << "�ļ���С: " << file_length << "�ֽ�" << endl;
	clock_t start = clock();
	// ѭ�����������ļ�
	for (int offset = 0; offset < buffer_length; offset += MSS) {
		bool result = rdt_send(server, server_addr, file_buffer + offset, buffer_length - offset >= MSS ? MSS : buffer_length - offset, buffer_length - offset <= MSS ? true : false);
		if (result == false) {
			delete []file_buffer;
			system("pause");
			exit(-1);
		}
	}
	clock_t end = clock();
	cout << "�����ļ�" + path + "�ɹ�" << endl;
	cout << "��ʱ: " << (end - start) / CLOCKS_PER_SEC << "s" << endl;
	cout << "������: " << buffer_length / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
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
	// ����fin����
	sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
	cout << "��ʼ���֣������������FIN���ģ�seqΪ" << seq << endl;
	// �ȴ�����ʱ�ط�
	// ����������ģʽ
	SOCKADDR_IN serv_addr;
	int serv_addr_length = sizeof(SOCKADDR_IN);
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	clock_t start = clock();
	while (true) {
		while (recvfrom(*server, recv_buffer, sizeof(header), 0, (sockaddr*)&serv_addr, &serv_addr_length) <= 0) {
			// ��ʱ��
			if (clock() - start > 1.2 * esti_rtt) {
				// �ش����������ˣ�ֱ�ӷ���
				if (retry_times <= 0) {
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					delete []send_buffer;
					delete []recv_buffer;
					cout << "���ֳ����ش�������ֱ�ӽ���" << endl;
					return;
				}
				sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
				retry_times--;
				esti_rtt += CLOCKS_PER_SEC;
				start = clock();
				cout << "��ʱ���ط�FIN����" << endl;
			}
		}
		memcpy(&header, recv_buffer, sizeof(header));
		chksum = checksum(recv_buffer, sizeof(header));
		if (chksum == 0 && header.flag == ACK && header.ack == (seq + 1) % SEQRANGE) {
			cout << "���յ����Է�������ACK���ģ����ֳɹ�" << endl;
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
	// �������ݱ��׽���
	SOCKET server = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	server_addr.sin_port = htons(6666);
	// ����ļ���
	cout << "�������ļ���: ";
	string file_path;
	cin >> file_path;
	// ����
	if (shake_hand(&server, &server_addr) == -1) {
		cout << "�����������ʧ��" << endl;
		closesocket(server);
		WSACleanup();
		exit(-1);
	}
	else {
		cout << "����������ֳɹ�" << endl;
	}
	// �����ļ�
	send_file(file_path, &server, &server_addr);
	// ����
	wave_hand(&server, &server_addr);
	closesocket(server);
	WSACleanup();
	system("pause");
	return 0;
}