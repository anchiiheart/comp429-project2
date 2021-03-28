#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "reliable_file.h"

struct recv_dest {
    char *hostname;
    short port;
};

struct file_path {
    char *subdir;
    char *filename;
};

int open_send(char *hostname, short port, struct sockaddr_in *recv_addr); 

int parse_dir(char *optarg, struct file_path *path);

int parse_receiver(char *optarg, struct recv_dest *dest);

int send_swp(int sockfd, struct file_path path,
        struct sockaddr_in recv_addr, socklen_t recv_addr_len);

int send_metadata(int sockfd, enum PacketType type, char *data,
        struct sockaddr_in recv_addr, socklen_t recv_addr_len);

int main(int argc, char **argv) {
    char *usage_str = "sendfile -r <recv_host>:<recv_port> -f <subdir>/<filename>";

    // Send error if aguments not formatted properly
    if (argc != 5) {
        fprintf(stderr, "Usage: %s\n", usage_str);
        exit(1);
    }

    // Create structs for the command line args.
    struct recv_dest dest; 
    struct file_path file;

    memset(&dest, 0, sizeof(dest));
    memset(&file, 0, sizeof(file));

    // Set boolean flags for if certain coptions have been seen
    bool r_option, f_option, abort_f = false;

    // Process command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "r:f:")) != -1) {
        switch (opt) {
            case 'r': // Get -r option.

                if (r_option) {
                    // Fail if -r option shows twice
                    fprintf(stderr, "Option -r already shown.\n");
                    abort_f = true;
                } else if (parse_receiver(optarg, &dest) < 0) {
                    // Fail if the option argument is malformed.
                    abort_f = true;
                }
                r_option = true;
                break;
            case 'f': // Get -f option

                // Fail if -f option shows twice
                if (f_option) {
                    fprintf(stderr, "Option -f already shown.\n");
                    abort_f = true;
                } else if (parse_dir(optarg, &file) < 0) {
                    // Fail if the option argument is malformed.
                    abort_f = true;
                }
                f_option = true;
                break;
            case '?':
                if (optopt == 'r' || optopt == 'f') {
                    fprintf(stderr, "Option -%c requires a port number.\n", optopt);
                } else {
                    fprintf(stderr, "Unknown flag %c.\nUsage: recvfile -p <recv_port>\n", opt);
                }
            default:
                abort_f = true;
        }
    }

    if (abort_f) {
        exit(1);
    }

    int sockfd;
    struct sockaddr_in recv_addr;
    if ((sockfd = open_send(dest.hostname, dest.port, &recv_addr)) < 0) {
        fprintf(stderr, "Failed to open send.\n");
        exit(1);
    }

    // Start program
    int recv_addr_len = sizeof(recv_addr);
    send_swp(sockfd, file, recv_addr, recv_addr_len);

    // Close socket before return.
    close(sockfd);
    return 0;
}

int send_packet(int sockfd, PacketInfo *pack_info, void *send_buf,
        struct sockaddr_in recv_addr, socklen_t recv_len) {
    fill_send_buffer(send_buf, pack_info->packet);
    
    printf("[send data] %zu (%d) %d\n", pack_info->packet.header.length, 
            pack_info->packet.header.offset, pack_info->packet.header.ack_num);

    int sent;
    while((sent = sendto(sockfd, send_buf, pack_info->packet.header.length,
                    0, (struct sockaddr *)&(recv_addr), recv_len)) == -1) {
        continue;
    }
    return sent;
}

int craft_packet(void *data, enum PacketType type, int16_t ack_num, FILE *file, Packet *packet) {
    Header *head = &(packet->header);
    head->offset = ftell(file);

    // Read in data
    //printf("craft_packet: Reading in data.\n");
    int read = fread(data, 1, PACKET_SIZE - sizeof(*head), file);
    if (read == -1) {
        return -1;
    }
    //printf("craft_packet: Read %d bytes of data.\n", read);

    //printf("craft_packet: Assigning header fields.\n");
    // Assign header fields
    head->length = sizeof(*head) + read;
    head->type = type;
    head->ack_num = ack_num;

    // Add data
    //printf("craft_packet: setting data pointer\n");
    packet->data = data;

    return read;
}

int send_swp(int sockfd, struct file_path path, 
        struct sockaddr_in recv_addr, socklen_t recv_addr_len) {
    FILE *file;

    // Attempt to change working directory to subdir
    if (chdir(path.subdir) != 0) {
        fprintf(stderr, "Failed to open directory.\n");
        return -1;
    }
    // Open the file to be sent
    file = fopen(path.filename, "r");
    if (file == NULL) {
        fprintf(stderr, "File does not exist.\n");
        return -1;
    }

    // Create sliding window
    SlidingWindow window;
    create_sliding_window(&window);

    // Create initial messages sending the subdir and name of the file
    send_metadata(sockfd, FileSubdir, path.subdir, recv_addr, recv_addr_len);
    send_metadata(sockfd, Filename, path.filename, recv_addr, recv_addr_len);
    printf("send_swp: Metadata received and acknowledged.\n");

    void *buf = calloc(1, PACKET_SIZE + 1);
    void *recv_buf = calloc(1, PACKET_SIZE);

    // Create timeval struct for retransmission time.
    struct timeval timeout_elapse;
    timerclear(&timeout_elapse);
    timeout_elapse.tv_usec = TIMEOUT_US;

    int curr_acknum = -1;
    bool ready = true;
    bool final = false;
    bool done = false;
    while (true) {
        // Check for min_accept packet.
        if (!ready || final) {
            // Get time now.
            struct timeval timenow;
            gettimeofday(&timenow, NULL);
            // Set timout if not set
            if (!window.timeout_set) {
                timeradd(&timenow, &timeout_elapse, &(window.timeout));
                window.timeout_set = true;
            }
            // If timeout is set, check for if RTT has been met.
            else if (timercmp(&timenow, &(window.timeout), >=)) {
                //printf("send_swp: Timeout hit. Will resend packet %d\n", window.min_accept);
                send_packet(sockfd, get_packet_info(window, window.min_accept),
                        buf, recv_addr, recv_addr_len);
                window.timeout_set = false;
                timerclear(&(window.timeout));
            }
            ssize_t get_data = recvfrom(sockfd, recv_buf, PACKET_SIZE,
                    MSG_DONTWAIT, (struct sockaddr *)&recv_addr, &recv_addr_len);
            Packet ack;
            int processed_data;
            if (get_data != -1) {
                processed_data = process_recv_data(recv_buf, get_data, &ack);
            } else {
                continue;
            }

            if (processed_data == 0) {
                // Check if ack packet received.
                //printf("send_swp: Received packet with ack_num %d\n", ack.header.ack_num);
                int ack_num = ack.header.ack_num;
                PacketInfo *pack_info = get_packet_info(window, ack.header.ack_num);
                if (pack_info == NULL) {
                    continue;
                }
                if (in_bounds(window, ack_num)) {
                    // Verify the validity of the ack packet; could be a packet
                    // from a previous sliding window cycle that was
                    // duplicated, reordered or delayed.
                    if (ack.header.offset != pack_info->packet.header.offset) {
                        printf("Received ack for acknum %d\n", ack.header.ack_num);
                        printf("Sequence number mismatch: expected %d, but received %d.\n",
                                pack_info->packet.header.offset, ack.header.offset);
                        continue;
                    }
                    pack_info->ack = true;
                } else { // Ignore packet because it was out of range.
                    continue;
                }
            } else {
                fprintf(stderr, "Failed to process acknowledgement data\n");
            }
            // Here, we need to shift the window until we reach a packet that
            // has not been acknowledged.
            while (window.min_accept != increment_mod(ack.header.ack_num, TOT_WINDOWS)) {
                PacketInfo *check_pack_info = get_packet_info(window, window.min_accept);
                if (check_pack_info->terminal == true) {
                    done = true;
                    break;
                }
                shift_window(&window);
            }
            if (done) {
                break;
            }

            // Clear timeout and set ready to true to begin sending again
            window.timeout_set = false;
            timerclear(&(window.timeout));
            ready = true;
        } 

        if (final) {
            continue;
        }

        curr_acknum = increment_mod(curr_acknum, TOT_WINDOWS);
        //printf("send_swp: Current ack num: %d\n", curr_acknum);

        // Set ready flag to false if we hit max_accept
        if (curr_acknum == window.max_accept) {
            printf("We have hit the maximum ack number.\n");
            ready = false;
        }

        PacketInfo *curr_pack_info = get_packet_info(window, curr_acknum);

        // If end of file, send terminal packet.
        if (feof(file)) {
            printf("send_swp: End of file...\n");
            final = true;
            ready = false;
            curr_pack_info->terminal = true;
            send_terminal(sockfd, &(curr_pack_info->packet), curr_acknum, recv_addr, recv_addr_len);
            //printf("send_swp: Sent terminal message with acknum %d.\n", curr_acknum);
            continue;
        }

        size_t packet_data_len = PACKET_SIZE - sizeof(curr_pack_info->packet.header);
        void *packet_data = calloc(1, packet_data_len);

        // Construct and send the packet
        craft_packet(packet_data, Data, curr_acknum, file, &(curr_pack_info->packet));
        send_packet(sockfd, curr_pack_info, buf, recv_addr, recv_addr_len);

        //printf("send_swp: Sent packet with ack num %d\n", curr_pack_info->packet.header.ack_num);
    }

    free(buf);

    printf("Closing file...\n");
    fclose(file);
    printf("Successfully closed file.\n");
    return 0;

}

int open_send(char *hostname, short port, struct sockaddr_in *recv_addr) {
    struct addrinfo *ai;
    int sockfd;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;

    // Use getaddrinfo() to get the server's IP address.
    int code = getaddrinfo(hostname, NULL, &hints, &ai);
    if (code != 0) {
        fprintf(stderr, "%s\n", gai_strerror(code));
        return -1;
    }

    memset(recv_addr, 0, sizeof(*recv_addr));
    memcpy(recv_addr, ai->ai_addr, sizeof(*recv_addr));
    recv_addr->sin_port = htons(port);

    // connect(sockfd, (const struct sockaddr *) &recv_addr, sizeof(recv_addr));

    return sockfd;

}

int parse_receiver(char *optarg, struct recv_dest* dest) {
    char *colon_loc;
    if ((colon_loc = strchr(optarg, ':')) == NULL) {
        fprintf(stderr, "Problem parsing hostame and port.\n");
        return -1;
    }

    char* host;
    host = calloc(1, colon_loc - optarg + 1);
    strncpy(host, optarg, colon_loc - optarg);

    short port;
    if ((port = atoi(colon_loc + 1)) == 0 && strcmp((colon_loc + 1), "0") != 0) {
        fprintf(stderr, "Failed to convert port number.\n");
        return -1;
    }

    // Set destination and port, if no previous errors.
    dest->hostname = host;
    dest->port = port;

    return 0;
}

int parse_dir(char *optarg, struct file_path* file) {
    char *last_slash;
    char *slash = optarg;

    // Get location of final slash in string.
    while ((slash = strchr(slash, '/')) != NULL) {
        last_slash = slash + 1;
        slash++;
    }

    if (last_slash == NULL) {
        fprintf(stderr, "Subdirectory is a required for the file path.\n");
        return -1;
    }

    char *path = calloc(1, last_slash - optarg + 1);
    strncpy(path, optarg, last_slash - optarg);

    char* filename = last_slash;
    if (strlen(filename) == 0) {
        fprintf(stderr, "Filename is required to send messages.\n");
        return -1;
    }

    file->subdir = path;
    file->filename = filename;

    return 0;
}

int send_metadata(int sockfd, enum PacketType type, char *data,
        struct sockaddr_in recv_addr, socklen_t recv_addr_len) {
    Packet packet;
    int data_len = strlen(data) + 1;

    // Fill header length field.
    packet.header.length = sizeof(packet.header) + data_len;
    void *buf = calloc(1, packet.header.length);
    void *recv_buf = calloc(1, PACKET_SIZE);

    // Fill in header data
    packet.header.offset = 0;
    packet.header.type = type;
    packet.header.ack_num = 0;

    // Fill in packet data
    void *packet_data;
    packet_data = calloc(1, data_len);
    memcpy(packet_data, data, data_len);

    packet.data = packet_data;
    fill_send_buffer(buf, packet);

    // Set elapsed timeout value.
    struct timeval timeout_elapse;
    timerclear(&timeout_elapse);
    timeout_elapse.tv_usec = TIMEOUT_US;

    int bytes, code;
    struct timeval timenow;
    struct timeval timeout;
    bool timeout_set = false;
    // Send data
    while (true) {
        struct sockaddr r_addr;
        socklen_t r_addr_len;

        // Set current time
        gettimeofday(&timenow, 0);
        if(!timeout_set) {
            timeradd(&timenow, &timeout_elapse, &timeout);
            timeout_set = true;
        } else if (timercmp(&timenow, &timeout, <)) { // Attempt a receive.
            bytes = recvfrom(sockfd, recv_buf, PACKET_SIZE, MSG_DONTWAIT,
                    (struct sockaddr*)&r_addr, &r_addr_len);
            if (bytes == -1) {
                continue;
            }

            Packet recv_packet;
            code = process_recv_data(recv_buf, bytes, &recv_packet);
            break;
        } else {
            timeout_set = false;
        }
        // Perform send or resend.
        while(sendto(sockfd, buf, packet.header.length, 0,
                    (struct sockaddr*)&recv_addr, recv_addr_len) == -1) {
            continue;
        }
        printf("send_metadata: Sent data to receiver.\n");

    }
    free(recv_buf);
    free(buf);
    if (code == -1) {
        fprintf(stderr, "Unable to process acknowledgement of data.\n");
        return -1;
    }
    return 0;
}


