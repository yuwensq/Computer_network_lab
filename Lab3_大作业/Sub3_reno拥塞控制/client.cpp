#include <iostream>
#include <string>
#include <vector>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <time.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
using namespace std;
#define MSS 6666
#define WND 10
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4
#define LAS 0x8
#define RST 0x10
#define INITIALSSTHRESH 8
#define TCP_SYN_RETRIES 5
#define TCP_WAV_RETRIES 5
#define TIMEOUT (CLOCKS_PER_SEC / 2)

#pragma pack(push)
#pragma pack(1)
struct Header { // header�ṹ��
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

#pragma pack(push)
#pragma pack(1)
struct Packet { // packet�ṹ��
	Header hdr;
	char data[MSS];
};
#pragma pack(pop)

class timer {
private:
	mutex mtx;
	bool valid;
	u_int esti_rtt;
	clock_t start;
public:
	timer() : esti_rtt(TIMEOUT) {};
	void start_timer(u_int esti_rtt) {
		mtx.lock();
		valid = true;
		this->esti_rtt = esti_rtt;
		start = clock();
		mtx.unlock();
	};
	void start_timer() {
		mtx.lock();
		valid = true;
		start = clock();
		mtx.unlock();
	}
	void stop_timer() {
		mtx.lock();
		valid = false;
		mtx.unlock();
	}
	bool time_out() {
		return valid && (clock() - start >= esti_rtt);
	}
	float remain_time() {
		return (esti_rtt - (clock() - start)) / 1000.0 <= 0 ? 0 : (esti_rtt - (clock() - start)) / 1000.0;
	}
};

class congestionAvoidancer {
private:
	u_int cwnd;
	u_int ssthresh;
	u_int dupACKcount;
	u_int step;
	enum {
		SLOWSTART,
		CONGESTIONAVOID,
		QUICKRECOVER
	} state;
public:
	congestionAvoidancer() {
		cwnd = 1;
		ssthresh = INITIALSSTHRESH;
		dupACKcount = 0;
		state = SLOWSTART;
	};
	u_int getCWND() {
		return cwnd;
	}
	void getNewACK() {
		dupACKcount = 0;
		switch(state) {
		case SLOWSTART: {
			cwnd += 1;
			if (cwnd >= ssthresh) {
				step = cwnd;
				state = CONGESTIONAVOID;
			}
		}break;
		case CONGESTIONAVOID: {
			step -= 1;
			if (step <= 0) {
				cwnd += 1;
				step = cwnd;
			}
		}break;
		case QUICKRECOVER: {
			cwnd = ssthresh;
			state = CONGESTIONAVOID;
			step = cwnd;
		};break;
		}
	}
	void timeOut() {
		dupACKcount = 0;
		ssthresh = cwnd / 2;
		cwnd = 1;
		state = SLOWSTART;
	}
	void getDupACK() {
		dupACKcount++;
		if (dupACKcount < 3) {
			return;
		}
		switch(state) {
		case SLOWSTART: 
		case CONGESTIONAVOID: {
			ssthresh = cwnd / 2;
			cwnd = ssthresh + 3;
		}break;
		}
		state = QUICKRECOVER;
	}
};

mutex buffer_lock;
mutex print_lock;
timer my_timer;
congestionAvoidancer my_cong_avoider;
bool send_over;
vector<Packet*> gbn_buffer;
u_short base = 1, nextseqnum = 1;

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
	header.set_args(base, 0, SYN, 0, 0);
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

void receive_thread(SOCKET *server, SOCKADDR_IN *server_addr) {
	// ����������ģʽ
	u_long mode = 1;
	ioctlsocket(*server, FIONBIO, &mode);
	char *recv_buffer = new char[sizeof(Header)];
	Header *header;
	SOCKADDR_IN ser_addr;
	int ser_addr_len = sizeof(SOCKADDR_IN);
	while (true) {
		if (send_over) {
			mode = 0;
			ioctlsocket(*server, FIONBIO, &mode);
			delete []recv_buffer;
			return;
		}
		while (recvfrom(*server, recv_buffer, sizeof(Header), 0, (sockaddr*)&ser_addr, &ser_addr_len) <= -1) {
			if (send_over) {
				mode = 0;
				ioctlsocket(*server, FIONBIO, &mode);
				delete []recv_buffer;
				return;
			}
			if (my_timer.time_out()) {
				my_cong_avoider.timeOut();
				for (auto packet : gbn_buffer) {
					sendto(*server, (char*)packet, sizeof(Header) + packet->hdr.length, 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
					print_lock.lock();
					cout << "��ʱ�ش����ݰ����ײ�Ϊ: seq:" << packet->hdr.seq << ", ack:" << packet->hdr.ack << ", flag:" << packet->hdr.flag << ", checksum:" << packet->hdr.checksum << ", len:" << packet->hdr.length << endl;
					print_lock.unlock();
				}
				my_timer.start_timer();
			}
		}
		header = (Header*)recv_buffer;
		int chksum = checksum(recv_buffer, sizeof(Header));
		print_lock.lock();
		cout << "���յ����Է����������ݰ����ײ�Ϊ: seq:" << header->seq << ", ack:" << header->ack << ", flag:" << header->flag << ", checksum:" << header->checksum << ", len:" << header->length << endl;
		print_lock.unlock();
		if (chksum != 0) {
			continue;
		}
		if (header->flag == RST) {
			cout << "�����쳣���˳�����" << endl;
			exit(-1);
		}
		else if (header->flag == ACK) {
			if (header->ack >= base) {
				my_cong_avoider.getNewACK();
			}
			else {
				my_cong_avoider.getDupACK();
			}
			int recv_num = header->ack + 1 - base;
			for (int i = 0; i < recv_num; i++) {
				buffer_lock.lock();
				if (gbn_buffer.size() <= 0) {
					break;
				}
				delete gbn_buffer[0];
				gbn_buffer.erase(gbn_buffer.begin());
				buffer_lock.unlock();
			}
			base = header->ack + 1;
		}
		if (base != nextseqnum) {
			my_timer.start_timer();
		}
		else {
			my_timer.stop_timer();
		}
	}
}

void rdt_send(SOCKET *server, SOCKADDR_IN *server_addr, char *msg, int len, bool last = false) {
	assert(len <= MSS);
	// �������˾�����
	while (nextseqnum >= base + WND || nextseqnum >= base + my_cong_avoider.getCWND()) {
		continue;
	}
	Packet *packet = new Packet;
	packet->hdr.set_args(nextseqnum, 0, last ? LAS : 0, 0, len); 
	memcpy(packet->data, msg, len);
	u_short chksum = checksum((char*)packet, sizeof(Header) + len);
	packet->hdr.checksum = chksum;
	buffer_lock.lock();
	gbn_buffer.push_back(packet);
	buffer_lock.unlock();
	sendto(*server, (char*)packet, len + sizeof(Header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
	print_lock.lock();
	cout << "��������������ݰ����ײ�Ϊ: seq:" << packet->hdr.seq << ", ack:" << packet->hdr.ack << ", flag:" << packet->hdr.flag << ", checksum:" << packet->hdr.checksum << ", len:" << packet->hdr.length << ", ʣ�෢�ʹ��ڴ�С:" << WND - (nextseqnum - base) - 1 << ", ʣ��ӵ�����ڴ�С:" << my_cong_avoider.getCWND() - (nextseqnum - base) - 1 <<endl;
	print_lock.unlock();
	if (base == nextseqnum) {
		my_timer.start_timer();
	}
	nextseqnum += 1;
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
	// ���������߳�
	thread receive_t(receive_thread, server, server_addr);
	// ���ļ��ֳ����ɸ������ֱ���
	for (int offset = 0; offset < buffer_length; offset += MSS) {
		rdt_send(server, server_addr, file_buffer + offset, buffer_length - offset >= MSS ? MSS : buffer_length - offset, buffer_length - offset <= MSS ? true : false);
	}
	while (gbn_buffer.size() != 0) {
		continue;
	}
	send_over = true;
	receive_t.join();
	clock_t end = clock();
	cout << "�����ļ�" + path + "�ɹ�" << endl;
	cout << "��ʱ: " << (end - start) / CLOCKS_PER_SEC << "s" << endl;
	cout << "������: " << buffer_length / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
	delete []file_buffer;
}

void wave_hand(SOCKET *server, SOCKADDR_IN *server_addr) {
	int retry_times = TCP_WAV_RETRIES;
	static int esti_rtt = TIMEOUT;
	int seq = nextseqnum;
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
		if (chksum == 0 && header.flag == ACK && header.ack == (u_short)(seq + 1)) {
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