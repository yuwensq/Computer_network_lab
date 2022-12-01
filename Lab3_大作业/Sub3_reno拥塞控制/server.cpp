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
		// �յ����Կͻ��˵Ľ�������
		memcpy(&header, recv_buffer, sizeof(header));
		int chksum = checksum(recv_buffer, sizeof(header));
		if (chksum == 0 && (header.flag & SYN)) {
			cout << "���յ����Կͻ��˵�SYN����������" << endl;
			header.set_args(0, 0, ACK + SYN, 0, 0);
			memcpy(send_buffer, (char*)&header, sizeof(header));
			chksum = checksum(send_buffer, sizeof(header));
			((Header*)send_buffer)->checksum = chksum;
			// ���͵ڶ���������Ϣ���ͻ���
			int n = sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			if (n == SOCKET_ERROR) {
				delete []send_buffer;
				delete []recv_buffer;
				return -1;
			}
			cout << "���ظ��ͻ���SYN��ACK����" << endl;
			// ������ģʽ
			u_long mode = 1;
			ioctlsocket(*server, FIONBIO, &mode);
			clock_t start = clock();
			// �ȴ����յ�����������Ϣ
			while (true) {
				int result;
				while ((result = recvfrom(*server, recv_buffer, sizeof(header) + MSS, 0, (sockaddr*)&client_addr, &client_addr_length)) <= 0) {
					if (clock() - start > 1.2 * esti_rtt) {
						if (retry_times <= 0) {
							cout << "����SYN��ACK�����ش����ޣ�����ʧ��" << endl;
							// �������ش�����
							delete []send_buffer;
							delete []recv_buffer;
							mode = 0;
							ioctlsocket(*server, FIONBIO, &mode);
							return -1;
						}
						int n = sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
						cout << "�ȴ�ACK��ʱ���ط�SYN��ACK����" << endl;
						retry_times--;
						esti_rtt += CLOCKS_PER_SEC;
						start = clock();
					}
				}
				memcpy(&header, recv_buffer, sizeof(header));
				chksum = checksum(recv_buffer, result);
				if (chksum == 0 && (header.flag & ACK)) {
					// �ɹ��յ�������������Ϣ
					delete []send_buffer;
					delete []recv_buffer;
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					cout << "���յ����Կͻ��˵�ACK�������ֳɹ�" << endl;
					return 1;
				}
				else if (chksum == 0 && (header.flag & SYN)) {
					start = clock();
					sendto(*server, send_buffer, sizeof(header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
				}
				// ������յ������ݱ��ģ����ظ��ͻ���һ��rst����
				else if (chksum == 0 && header.flag == 0){
					Header reply_header;
					reply_header.set_args(0, 0, RST, 0, 0);
					memcpy(send_buffer, (char*)&reply_header, sizeof(reply_header));
					chksum = checksum(send_buffer, sizeof(reply_header));
					((Header*)send_buffer)->checksum = chksum;
					int n = sendto(*server, send_buffer, sizeof(reply_header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
					cout << "���յ����Կͻ��˵����ݣ�����RST���ģ�����ʧ��" << endl;
					delete []send_buffer;
					delete []recv_buffer;
					mode = 0;
					ioctlsocket(*server, FIONBIO, &mode);
					return -1;
				} 
				// ��ĸ���
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
	// ���ջ���ͷ��ͻ���
	char *recv_buffer = new char[MSS + sizeof(header)];
	char *send_buffer = new char[sizeof(header)];
	memset(recv_buffer, 0, MSS + sizeof(header));
	memset(send_buffer, 0, sizeof(header));
	// �ȳ�ʼ��һ��send_buffer
	((Header*)send_buffer)->set_args(0, -1, ACK, 0, 0);
	((Header*)send_buffer)->checksum = checksum(send_buffer, sizeof(Header));
	// һֱ��������
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	clock_t start;
	while (true) {
		int result;
		while ((result = recvfrom(*server, recv_buffer, MSS + sizeof(header), 0, (sockaddr*)&client_addr, &client_addr_length)) <= 0) {
			// ��ʱ��û�н��յ���Ϣ�����ļ������ˣ���Ϊ�Է�����
			if (file_finish == true && clock() - start > WAITTIME) {
				cout << "��ʱ��û�н��յ����ģ��Ͽ�����";
				delete []send_buffer;
				delete []recv_buffer;
				mode = 0;
				ioctlsocket(*server, FIONBIO, &mode);
				return;
			}
		}
		// �õ�����ͷ
		memcpy(&header, recv_buffer, sizeof(header));
		cout << "���յ�����Ϊ" << result << "�ֽڵ����ݱ�,ͷ��Ϊ: ";
		cout << "seq: " << header.seq << " ack: " << header.ack << " flag: " << header.flag << " checksum: " << header.checksum << " length: " << header.length << endl;
		// �ȼ���һ��У���
		u_short chksum = checksum(recv_buffer, result);
		// У��Ͳ���
		if (chksum != 0) {
			int n = sendto(*server, send_buffer, sizeof(Header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			cout << "���ݱ�У��ͳ���" << endl;
			continue;
		}
		if (header.flag == FIN) {
			Header reply_header;
			reply_header.set_args(0, (u_short)(header.seq + 1), ACK, 0, 0);
			memcpy(send_buffer, (char*)&reply_header, sizeof(reply_header));
			chksum = checksum(send_buffer, sizeof(reply_header));
			((Header*)send_buffer)->checksum = chksum;
			int n = sendto(*server, send_buffer, sizeof(reply_header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			cout << "���յ�FIN���ģ�����ACK���ģ��������" << endl;
			break;
		}
		// ���յ�һ�����ݱ���
		else if (header.seq == expectedseqnum) {
			// ����һ��ack
			Header reply_header;
			reply_header.set_args(0, expectedseqnum, ACK, 0, 0);
			memcpy(send_buffer, (char*)&reply_header, sizeof(reply_header));
			chksum = checksum(send_buffer, sizeof(reply_header));
			((Header*)send_buffer)->checksum = chksum;
			int n = sendto(*server, send_buffer, sizeof(reply_header), 0, (sockaddr*)&client_addr, sizeof(SOCKADDR_IN));
			cout << "����ACK����:" << "seq: " << reply_header.seq << " ack: " << reply_header.ack << " flag: " << reply_header.flag << " checksum: " << chksum << " length: " << reply_header.length << endl;
			expectedseqnum++;
			memcpy(data_buffer + *len, recv_buffer + sizeof(header), header.length);
			*len += header.length;
			cout << "���ݱ��ɹ���ȡ" << endl;
			// ���һ�����ĵ���
			if (header.flag & LAS) {
				file_finish = true;
				start = clock();
				cout << "�ļ�������ϣ��ȴ��ͻ��˻���" << endl;
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
	// �������ݱ��׽���
	SOCKET server = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	server_addr.sin_port = htons(8888);
	if (bind(server, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
		cout << "�󶨶˿�ʧ��" << endl;
		closesocket(server);
		WSACleanup();
		exit(-1);
	}
	else {
		cout << "�󶨶˿ڳɹ�" << endl;
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
		cout << "���յ��ļ�" + file_name << "����Ϊ: " << data_len - i - 1 << endl;
		ofstream file(file_name.c_str(), ofstream::binary);
		file.write(data + i + 1, data_len - i - 1);
		file.close();
		delete []data;
	}
	else {
		cout << "����ʧ�ܣ��������ͻ��˺ͷ����" << endl;
	}
	closesocket(server);
	WSACleanup();
	system("pause");
	return 0;
}