#include "packet.h"
#include <cstring> 
#include <iostream> 
#include <stdio.h> 
#include <string> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <netdb.h> 
#include <sys/stat.h> 
#include <fcntl.h> 
#include <unistd.h> 
#include <unordered_map> 
#include <sys/time.h>
#include <cmath> 
#include <signal.h> 
#include <fstream> 
#include <errno.h>

using namespace std;

uint16_t updateWindow(const Packet &p, unordered_map<uint16_t, Packet_info> &window, uint16_t &base_num, PacketTime &pt);
struct timeval timeLeft(unordered_map<uint16_t, Packet_info> &window, uint16_t base_num);
void Receive(int nBytes, const string &function, int sockfd, Packet_info &last_ack, struct sockaddr_storage recv_addr, socklen_t addr_len, const PacketTime &pt);
void processError(int status, const string &function);
bool valid_ack(const Packet &p, uint16_t base_num);
int open_file(char* file);
int set_up_socket(char* port);

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " PORT-NUMBER FILE-NAME" << endl;
        exit(1);
    }

    ifstream file(argv[2]);
    int sockfd = set_up_socket(argv[1]);
    struct sockaddr_storage recv_addr;
    socklen_t addr_len = sizeof(recv_addr);
    int status, nBytes;
    uint16_t ack_num, base_num;
    Packet p;

    unordered_map<uint16_t, Packet_info> window;
    Packet_info pkt_info;

    // select random seq_num
    srand(time(NULL));
    uint16_t seq_num = rand() % MSN;

    PacketTime pt;

    // recv SYN from client
    do
    {
        nBytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        processError(nBytes, "recv SYN");
    } while (!p.syn_set());
    cout << "Receiving packet " << p.ack_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    // sending SYN ACK
    p = Packet(1, 1, 0, seq_num, ack_num, 0, "", 0);
    pkt_info = Packet_info(p, nBytes - HEADER_LEN, pt.get_timeout());
    status = sendto(sockfd, (void *) &p, HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
    processError(status, "sending SYN ACK");
    cout << "Sending packet " << seq_num << " SYN" << endl;
    seq_num = (seq_num + 1) % MSN;
    base_num = seq_num;

    // recv ACK
    do
    {
        struct timeval max_time = pkt_info.get_max_time();
        struct timeval curr_time;
        gettimeofday(&curr_time, NULL);
        struct timeval timeLeft;
        timersub(&max_time, &curr_time, &timeLeft);
        status = setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (char *)&timeLeft, sizeof(timeLeft));
        processError(status, "setsockopt");
        nBytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        Receive(nBytes, "recv ACK after SYN ACK", sockfd, pkt_info, recv_addr, addr_len, pt);
    } while (p.seq_num() != ack_num); // discard invalid ack
    uint16_t prev_ack = p.ack_num();
    cout << "Receiving packet " << p.ack_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    double cwnd = min((double) MSS, MSN / 2.0);
    uint16_t cwnd_used = 0;
    uint16_t ssthresh = INITIAL_SSTHRESH;
    uint16_t cwd_pkts = 0;
    uint16_t pkts_sent = 0;
    uint16_t dup_ack = 0;
    uint16_t recv_window = UINT16_MAX;
    bool slow_start = true;
    bool congestion_avoidance = false;
    bool fast_recovery = false;
    bool retransmission = false;
    bool last_retransmit = false;
    // send file
    do
    {
        if (retransmission) // retransmit missing segment
        {
            auto found = window.find(base_num);
            p = found->second.pkt();
            status = sendto(sockfd, (void *) &p, found->second.data_len() + HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
            processError(status, "sending retransmission");
            cout << "Sending packet " << p.seq_num() << " Retransmission" << endl;
            retransmission = false;


            found->second.update_time(pt.get_timeout());
            last_retransmit = true;
        }
        else // transmit new segment(s)
        {
            last_retransmit = false;
            while (floor(cwnd) - cwnd_used >= MSS && !file.eof())
            {
                string data;
                size_t buf_pos = 0;
                data.resize(MSS);

                cout << "Sending packet " << seq_num << endl;
                // make sure recv all that we can
                do
                {
                    size_t n_to_send = (size_t) min(cwnd - cwnd_used, (double) min(MSS, recv_window));
                    file.read(&data[buf_pos], n_to_send);
                    nBytes = file.gcount();
                    buf_pos += nBytes;
                    cwnd_used += nBytes;
                } while (cwnd_used < floor(cwnd) && !file.eof() && buf_pos != MSS);

                // send packet
                p = Packet(0, 0, 0, seq_num, ack_num, 0, data.c_str(), buf_pos);
                pkt_info = Packet_info(p, buf_pos, pt.get_timeout());
                status = sendto(sockfd, (void *) &p, buf_pos + HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
                processError(status, "sending packet");
                window.emplace(seq_num, pkt_info);
                seq_num = (seq_num + pkt_info.data_len()) % MSN;
            }
        }

        // recv ACK
        do
        {
            struct timeval timeLeft_tv= timeLeft(window, base_num);
            status = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeLeft_tv, sizeof(timeLeft_tv));
            processError(status, "setsockopt");
            int ack_nBytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
            if (ack_nBytes == -1) //error
            {
                // check if timed out
                if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
                {
                    // adjust cwnd and ssthresh
                    ssthresh = cwnd / 2;
                    cwnd = MSS;
                    dup_ack = 0;
                    slow_start = true;
                    congestion_avoidance = false;
                    fast_recovery = false;
                    retransmission = true;
                    break;
                }
                else // else another error and process it
                {
                    processError(ack_nBytes, "recv ACK after sending data");
                }
            }
        } while (!valid_ack(p, base_num));
        if (retransmission)
            continue;

        cout << "Receiving packet " << p.ack_num() << endl;
        if (prev_ack == p.ack_num()) // if duplicate
        {
            if (fast_recovery)
            {
                cwnd += MSS;
            }
            else
            {
                dup_ack++;
            }

            // if retransmit
            if (dup_ack == 3)
            {
                ssthresh = cwnd / 2;
                cwnd = ssthresh + 3 * MSS;
                fast_recovery = true;
                slow_start = false;
                congestion_avoidance = false;
                retransmission = true;
                dup_ack = 0;
            }
        }
        else // new ack
        {
            if (slow_start)
            {
                cwnd += MSS;
                dup_ack = 0;

                if (cwnd >= ssthresh)
                {
                    slow_start = false;
                    congestion_avoidance = true;
                    cwd_pkts = cwnd / MSS;
                    pkts_sent = 0;
                }
            }
            else if (congestion_avoidance)
            {
                if (pkts_sent == cwd_pkts)
                {
                    cwd_pkts = cwnd / MSS;
                    pkts_sent = 0;
                }
                cwnd += MSS / (double) cwd_pkts;
                pkts_sent++;
            }
            else // fast recovery
            {
                cwnd = ssthresh;
                dup_ack = 0;
                fast_recovery = false;
                congestion_avoidance = true;
            }

            prev_ack = p.ack_num();
            cwnd_used -= updateWindow(p, window, base_num, pt);
            ack_num = (p.seq_num() + 1) % MSN;
        }

        cwnd = min(cwnd, MSN / 2.0); // make sure cwnd is not greater than MSN/2
        cwnd = max(cwnd, (double) MSS); // make sure cwnd is not less than MSS
        ssthresh = max(ssthresh, MSS); // make sure ssthresh is at least MSS

        recv_window = p.recv_window();
    } while (!file.eof() || (window.size() != 0));

    // send FIN
    p = Packet(0, 0, 1, seq_num, ack_num, 0, "", 0);
    pkt_info = Packet_info(p, 0, pt.get_timeout());
    status = sendto(sockfd, (void *) &p, HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
    processError(status, "sending FIN");
    cout << "Sending packet " << seq_num << " FIN" << endl;
    seq_num = (seq_num + 1) % MSN;

    // recv FIN ACK
    do
    {
        struct timeval max_time = pkt_info.get_max_time();
        struct timeval curr_time;
        gettimeofday(&curr_time, NULL);
        struct timeval timeLeft;
        timersub(&max_time, &curr_time, &timeLeft);
        status = setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (char *)&timeLeft, sizeof(timeLeft));
        processError(status, "setsockopt");
        nBytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        Receive(nBytes, "recv FIN ACK", sockfd, pkt_info, recv_addr, addr_len, pt);
    } while (!p.fin_set() || !p.ack_set());
    prev_ack = p.ack_num();
    cout << "Receiving packet " << p.ack_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    // send ACK after FIN ACK
    p = Packet(0, 1, 0, seq_num, ack_num, 0, "", 0);
    pkt_info = Packet_info(p, 0, pt.get_timeout());
    status = sendto(sockfd, (void *) &p, HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
    processError(status, "sending ACK after FIN ACK");
    cout << "Sending packet " << seq_num << endl;

    // make sure client receives ACK after FIN ACK
    do
    {
        struct timeval max_time = pkt_info.get_max_time();
        struct timeval curr_time;
        gettimeofday(&curr_time, NULL);
        struct timeval timeLeft;
        timersub(&max_time, &curr_time, &timeLeft);
        //timeLeft.tv_sec *= 2; // 2*PacketTime
        //timeLeft.tv_usec *= 2; // 2*PacketTime
        status = setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (char *)&timeLeft, sizeof(timeLeft));
        processError(status, "setsockopt");
        nBytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        if (nBytes == -1) //error
        {
            // check if timed out, if so everything is fine close
            if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
            {
                break;
            }
            else // else another error and process it
            {
                processError(nBytes, "checking to see if recv FIN ACK again");
            }
        }
        else if (p.fin_set() && p.ack_set()) // if client send FIN ACK again, send ACK
        {
            pkt_info.update_time(pt.get_timeout());
            p = pkt_info.pkt();
            status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
            processError(status, "sending packet");
        }
    } while (!p.fin_set() || !p.ack_set());
}

void Receive(int nBytes, const string &function, int sockfd, Packet_info &last_ack, struct sockaddr_storage recv_addr, socklen_t addr_len, const PacketTime &pt)
{
    if (nBytes == -1) //error
    {
        // check if timed out
        if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
        {
            last_ack.update_time(pt.get_timeout());
            Packet p = last_ack.pkt();
            int status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
            processError(status, "sending packet");
        }
        else // else another error and process it
        {
            processError(nBytes, "checking to see if recv FIN ACK again");
        }
    }
}

struct timeval timeLeft(unordered_map<uint16_t, Packet_info> &window, uint16_t base_num)
{
    auto first_pkt = window.find(base_num);
    if (first_pkt == window.end())
    {
        cerr << "could not find base_num packet " << base_num << " in window, in timeLeft function" << endl;
        exit(1);
    }
    struct timeval max_time = first_pkt->second.get_max_time();

    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);

    struct timeval timeLeft;
    timersub(&max_time, &curr_time, &timeLeft);

    return timeLeft;
}

bool valid_ack(const Packet &p, uint16_t base_num)
{
    uint16_t ack = p.ack_num();
    uint16_t max = (base_num + MSN/2) % MSN;

    if (base_num < max) // no overflow of window
    {
        if (ack >= base_num && ack <= max)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else // window overflowed
    {
        if (ack > max && ack < base_num)
        {
            return false;
        }
        else
        {
            return true;
        }
    }
}

uint16_t updateWindow(const Packet &p, unordered_map<uint16_t, Packet_info> &window, uint16_t &base_num, PacketTime &pt)
{
    uint16_t n_removed = 0;

    while (base_num > p.ack_num())
    {
        auto found = window.find(base_num);
        if (found == window.end())
        {
            cerr << "could not find base_num packet with base_num " << base_num << " and ack num " << p.ack_num() << " in window, updateWindow" << endl;
            exit(1);
        } 
        uint16_t len = found->second.data_len();
        window.erase(base_num);
        n_removed += len;
        base_num = (base_num + len) % MSN;
    }
    while (base_num < p.ack_num())
    {
        auto found = window.find(base_num);
        if (found == window.end())
        {
            cerr << "could not find base_num packet with base_num " << base_num << " and ack num " << p.ack_num() << " in window, updateWindow" << endl;
            exit(1);
        }
        uint16_t len = found->second.data_len();
        window.erase(base_num);
        n_removed += len;
        base_num = (base_num + len) % MSN;
    }

    return n_removed;
}

int set_up_socket(char* port)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int status;

    // set up hints addrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    //set up socket calls
    status = getaddrinfo(NULL, port, &hints, &res);
    if (status != 0)
    {
        cerr << "getaddrinfo error: " << gai_strerror(status) << endl;
        exit(1);
    }

    // find socket to bind to
    int sockfd;
    int yes = 1;
    auto i = res;
    for (; i != NULL; i = i ->ai_next)
    {
        sockfd = socket(res->ai_family, res->ai_socktype, 0);
        if (sockfd == -1) 
        {
            continue;
        }

        status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (status == -1)
        {
            continue;
        }

        status = bind(sockfd, res->ai_addr, res->ai_addrlen);
        if (status == -1)
        {
            continue;
        }

        break;
    }
    freeaddrinfo(res);

    // check if reached end of linked list
    // means could not find a socket to bind to
    if (i == NULL)
    {
        perror("bind to a socket");
        exit(1);
    }

    return sockfd;
}

void processError(int status, const string &function)
{
    if (status == -1)
    {
        perror(&function[0]);
        exit(1);
    }
}
