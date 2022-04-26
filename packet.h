#ifndef PACKET_H
#define PACKET_H

#include <string>
#include <bitset>
#include <cstring>
#include <sys/time.h>

const uint16_t MSS = 1024; // MAX IS 1032
const uint16_t HEADER_LEN = 8; // bytes
const uint16_t INITIAL_SSTHRESH = 3000; // bytes
const uint16_t INITIAL_TIMEOUT = 500; // ms, 1 sec since PacketTime adaption
const uint16_t MSN = 30720; // bytes

class Packet
{
public:
    Packet() = default;
    Packet(bool syn, bool ack, bool fin, uint16_t num, uint16_t ack_num, uint16_t recv_window, const char *data, size_t len)
    {
        S = syn;
        A = ack;
        F = fin;
        seq_num = num;
        A_num = ack_num;
        m_recv_window = recv_window;
        strncpy(m_data, data, len);
    }

    bool syn_set() const
    {
        return S;
    }

    bool ack_set() const
    {
        return A;
    }

    bool fin_set() const
    {
        return F;
    }

    uint16_t seq_num() const
    {
        return seq_num;
    }

    uint16_t ack_num() const
    {
        return A_num;
    }

    uint16_t recv_window() const
    {
        return m_recv_window;
    }

    void data(char* buffer, size_t len) const
    {
        strncpy(buffer, m_data, len);
    }

private:
    bool     S:1;
    bool     A:1;
    bool     F:1;
    uint16_t seq_num;  // 2 bytes
    uint16_t A_num;  // 2 bytes
    uint16_t m_recv_window; // 2 bytes
    char     m_data[MSS]; // MSS bytes
};

class Packet_info
{
public:
    Packet_info() = default;
    Packet_info(Packet p, uint16_t data_len, const struct timeval &timeout)
    {
        m_p = p;
        m_data_len = data_len;
        update_time(timeout);
    }

    Packet pkt() const
    {
        return m_p;
    }

    uint16_t data_len() const
    {
        return m_data_len;
    }

    struct timeval get_max_time() const
    {
        return m_max_time;
    }

    struct timeval get_time_sent() const
    {
        return m_time_sent;
    }

    void update_time(const struct timeval &timeout)
    {
        gettimeofday(&m_time_sent, NULL);

        // find max_time for first packet
        timeradd(&m_time_sent, &timeout, &m_max_time);
    }

private:
    Packet m_p;
    struct timeval m_time_sent;
    struct timeval m_max_time;
    uint16_t       m_data_len;
};

class PacketTime
{
public:
    PacketTime()
    {
        m_timeout.tv_usec = INITIAL_TIMEOUT * 1000; // microseconds
        timerclear(&m_EstimatedRTT);
        timerclear(&m_DevRTT);
    }

    struct timeval get_timeout() const
    {
        return m_timeout;
    }

    
private:
    struct timeval m_EstimatedRTT;
    struct timeval m_DevRTT;
    struct timeval m_timeout;
};
#endif
