#ifndef RELIABLE_H
#define RELIABLE_H

#define WINDOW_SIZE 60
#define TOT_WINDOWS 2*WINDOW_SIZE
#define PACKET_SIZE 1400
#define TIMEOUT_US 5000

enum PacketType {
    FileSubdir,
    Filename,
    Data,
    Terminal,
    Ack
} __attribute__ ((__packed__));

typedef struct Header {
    size_t length;
    int32_t offset;
    enum PacketType type;
    int16_t ack_num;
} Header;

typedef struct Packet {
    Header header;
    void *data;
} Packet;

typedef struct PacketInfo {
    Packet packet;
    bool ack;
    bool ready;
    bool terminal;
} PacketInfo;

typedef struct SlidingWindow {
    PacketInfo *packets;
    struct timeval timeout;
    bool timeout_set;
    int min_accept;
    int max_accept;
} SlidingWindow;

int modulo(int n, int mod) {
    while (n > mod) {
        n -= mod;
    }
    while (n < 0) {
        n += mod;
    }
    if (n == mod) {
        return 0;
    }
    return n;
}

int increment_mod(int n, int mod) {
    n ++;
    return modulo(n, mod);
}

int decrement_mod(int n, int mod) {
    n --;
    return modulo(n, mod);
}




void clear_packet(Packet *packet) {
    free(packet->data);
    memset(packet, 0, sizeof(*packet));
}

size_t get_data_len(Packet packet) {
    return packet.header.length - sizeof(packet.header);
}

PacketInfo* get_packet_info(
        SlidingWindow window, int index) {
    // Bounds checking
    if (index < 0 || index >= TOT_WINDOWS) {
        return NULL;
    }

    return (PacketInfo *)(window.packets + index);
}

int fill_packet_info(PacketInfo *packet_info, bool terminal) {
    packet_info->terminal = terminal;
    packet_info->ready = true;
    packet_info->ack = false;
    return 0;
}

int clear_packet_info(PacketInfo *packet_info) {
    clear_packet(&(packet_info->packet));
    packet_info->ready = false;
    packet_info->ack = false;
    return 0;
}

void create_sliding_window(SlidingWindow *window) {
    PacketInfo *packet_info_ptr;
    packet_info_ptr = calloc(2*WINDOW_SIZE, sizeof(PacketInfo));
    
    window->packets = packet_info_ptr;
    int idx;
    for (idx = 0; idx < TOT_WINDOWS; idx++) {
        PacketInfo *curr_pack;
        if ((curr_pack = get_packet_info(*window, idx)) != NULL) {
            curr_pack->ready = false;
            curr_pack->ack = false;
        }
    }
    
    window->min_accept = 0;
    window->max_accept = WINDOW_SIZE - 1;
    window->timeout_set = false;
}

void shift_window(SlidingWindow *window) {
    //clear_packet_info(get_packet_info(*window, window->min_accept));
    window->min_accept = increment_mod(window->min_accept, TOT_WINDOWS); 
    window->max_accept = increment_mod(window->max_accept, TOT_WINDOWS);
    fill_packet_info(get_packet_info(*window, window->max_accept), false);
}

int process_recv_data(void *data, size_t data_len, Packet *packet) {

    size_t head_len = sizeof(packet->header);
    if (data_len <= head_len) {
        fprintf(stderr, "process_recv_data: Failed to process data.");
        return -1;
    }

    // Perform checksum(s)
    
    // Fill in Packet header and data
    memcpy(&(packet->header), data, head_len);
    data += head_len;

    // Fill data
    void *packet_data = calloc(1, data_len - head_len);
    memcpy(packet_data, data, data_len - head_len);
    packet->data = packet_data;

    return 0;
}

int fill_send_buffer(void *buf, Packet packet) {
    unsigned char *ptr = buf;
    
    // Copy header
    memcpy(ptr, &(packet.header), sizeof(packet.header));
    ptr += sizeof(packet.header);
    
    // Copy data
    memcpy(ptr, packet.data, get_data_len(packet));
    return 0;
}

void send_terminal(int sockfd, Packet *packet, int ack_num, struct sockaddr_in addr, socklen_t addr_len) {
    int data_len = 1;

    packet->header.length = sizeof(packet->header) + data_len;
    void *buf = calloc(1, packet->header.length);

    packet->header.offset = 0;
    packet->header.type = Terminal;
    packet->header.ack_num = ack_num;

    void *packet_data = calloc(1, data_len);
    packet->data = packet_data;

    fill_send_buffer(buf, *packet);
    
    // Send terminal packet.
    int sent;
    while ((sent = sendto(sockfd, buf, packet->header.length, 0,
            (struct sockaddr*)&addr, addr_len)) == -1) {
        continue;
    }
    
    free(buf);
}

bool in_bounds(SlidingWindow window, int16_t ack_num) {
    if (ack_num >= TOT_WINDOWS) {
        return false;
    }
    if (window.min_accept < window.max_accept) {
        if (ack_num >= window.min_accept && ack_num <= window.max_accept) {
            return true;
        }
        return false;
    } else {
        if (!(ack_num < window.min_accept && ack_num > window.max_accept)) {
            return true;
        }
        return false;
    }
}


#endif
