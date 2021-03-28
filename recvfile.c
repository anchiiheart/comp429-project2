#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "reliable_file.h"

int open_connect(short port);

int recv_swp(int sockfd, short port);

int process_recv_data(void *data, size_t data_len, Packet *packet);

int write_swp_packet(Packet read, FILE *write);

void send_ack(int sockfd, void *send_buf, int ack_num, int offset,
        struct sockaddr_in send_addr, socklen_t sender_len); 

int main(int argc, char **argv) {
    // Send error if aguments not formatted properly
    if (argc != 3) {
        fprintf(stderr, "Usage: recvfile -p <recv_port>\n");
        exit(1);
    }

    short port;

    // Process command line arguments
    int opt;
    bool abort_f = false;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case '?':
                if (optopt == 'p') {
                    fprintf(stderr, "Option -p requires a port number.\n");
                }
                else {
                    fprintf(stderr, "Unknown flag %c.\nUsage: recvfile -p <recv_port>\n", opt);
                }
            default:
                abort_f = true;
        }
    }

    if (abort_f) {
        exit(1);
    }

    int sockfd = open_connect(port);
    if (sockfd < 0) {
        fprintf(stderr, "Undable to bind socket.\n");
        exit(1);
    }

    // Set five-second timeout on receiving.
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    recv_swp(sockfd, htons(port));

    return 0;
}

int open_connect(short port) {
    struct sockaddr_in recv_addr;
    int sockfd;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    memset(&recv_addr, 0, sizeof(recv_addr));

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    recv_addr.sin_port = htons(port);

    bind(sockfd, (const struct sockaddr *) &recv_addr, sizeof(recv_addr));

    return sockfd;

}

int process_swp_packet(SlidingWindow *window, Packet packet, FILE *file) {
    if (in_bounds(*window, packet.header.ack_num)) {
        //printf("process_swp_packet: Processing SWP packet of ack_num %d\n", packet.header.ack_num);
        //printf("process_swp_packet: Current minimum accepting ack number: %d\n", window->min_accept);
        
        PacketInfo *pack_info = get_packet_info(*window, packet.header.ack_num);
        pack_info->terminal = false;
        
        if (packet.header.type == Terminal) {
            //printf("process_swp_packet: Received terminal packet from sender.\n");
            //printf("process_swp_packet: T packet = %d\n", packet.header.ack_num);
            pack_info->terminal = true;
        }


        else if (ftell(file) > packet.header.offset) {
            printf("[recv data] %d (%zu) IGNORED\n", packet.header.offset, packet.header.length);
            return decrement_mod(window->min_accept, TOT_WINDOWS);
        }

        if (!pack_info->ack) {
            pack_info->ack = true;
        } else {
            printf("[recv data] %d (%zu) IGNORED\n", packet.header.offset, packet.header.length);
        }
        
        // Copy packet into PacketInfo struct.
        memcpy(&(pack_info->packet), &packet, sizeof(packet));
        

        // Advance window, if ready.
        if (packet.header.ack_num == window->min_accept) {
            printf("[recv data] %d (%zu) ACCEPTED (in-order)\n", 
                    pack_info->packet.header.offset, packet.header.length);
            PacketInfo *check_pack_info = get_packet_info(*window, window->min_accept);
            while (check_pack_info->ack == true) {
                if (check_pack_info->terminal == true) {
                    return check_pack_info->packet.header.ack_num;
                }
                printf("process_swp_packet: As it turns out, ack_num %d has been found already.\n",
                        check_pack_info->packet.header.ack_num);
                //printf("process_swp_packet: Packet length: %zu bytes.\n", check_pack_info->packet.header.length);
                write_swp_packet(check_pack_info->packet, file);
                shift_window(window);
                printf("process_swp_packet: New minimum accepting: %d\n", window->min_accept);
                check_pack_info = get_packet_info(*window, window->min_accept);
            }
        } else {
            printf("[recv data] %d (%zu) ACCEPTED (out-of-order)\n", packet.header.offset,
                    packet.header.length);
        }
        
    } else {
        printf("[recv data] %d (%zu) IGNORED\n", packet.header.offset, packet.header.length);
        //fprintf(stderr, "Window not in bounds: acknum=%d.\n", packet.header.ack_num);
    }
    // Send ack for packet sequence number "min - 1".
    return decrement_mod(window->min_accept, TOT_WINDOWS);
}

int recv_swp(int sockfd, short port) {
    struct sockaddr_in sender_addr;
    ssize_t read;
    socklen_t sender_len;
    SlidingWindow window;
    bool subdir_opened, file_opened;
    FILE *file;


    create_sliding_window(&window);
    sender_len = sizeof(sender_addr);

    subdir_opened = false;
    file_opened = false;
    bool finish = false;
    void *buf = calloc(1, PACKET_SIZE);
    void *send_buf = calloc(1, PACKET_SIZE);
    while (true) {
        read = recvfrom(
                sockfd, buf, PACKET_SIZE, 0,
                (struct sockaddr *)&sender_addr, &sender_len);
        if (read == -1) {
            if (finish) {
                break;
            }
            continue;
        }

        // Create packet from received data
        Packet packet;
        if (process_recv_data(buf, read, &packet) < 0) {
            // TODO: This should not fail in the final result.
            fprintf(stderr, "Failed to process packet.\n");
            // free(buf);
            break;
        }

        // Check packet for special cases
        // Subdirectory
        if (packet.header.type == FileSubdir) {
            printf("recv_swp: Got a subdir packet.\n");
            send_ack(sockfd, send_buf, -1, 0, sender_addr, sender_len);
            if (subdir_opened) {
                /* If the subdirectory has been gotten, ignore for now. */
                continue;
            }

            subdir_opened = true;

            printf("recv_swp: Attempting to change directory.\n");
            if (chdir(packet.data) != 0) {
                //TODO: Fix this behavior? Fail fast
                fprintf(stderr, "Failed to change directory.\n");
                free(packet.data);
                break;
            }
        }

        // Filename
        else if (packet.header.type == Filename) {
            if (file_opened) {
                send_ack(sockfd, send_buf, -1, 0, sender_addr, sender_len);
                continue;
            } if (!subdir_opened) {
                continue;
            }
            char write_filename[get_data_len(packet) + 5];
            strcpy(write_filename, (char *)packet.data);
            strcat(write_filename, ".recv");
            printf("File to write to: %s\n", write_filename);

            file = fopen(write_filename, "w");
            file_opened = true;
            send_ack(sockfd, send_buf, -1, 0, sender_addr, sender_len);
        }
      

        // Otherwise, process packet using SWP.
        else {
            if (file_opened && subdir_opened) {
                int status = process_swp_packet(&window, packet, file);

                // If status = TOT_WINDOWS, we are done.
                if (get_packet_info(window, status)->terminal == true) {
                    Packet t_packet;
                    send_terminal(sockfd, &t_packet, packet.header.ack_num, sender_addr, sender_len);
                    printf("recv_swp: Sent terminal with acknum %d.\n", t_packet.header.ack_num);
                    clear_packet(&t_packet);
                    
                    finish = true;
                } else {
                    // Send back ack.
                    printf("recv_swp: Sending ack for sequence number %d\n", status);
                    PacketInfo *new_pack_info = get_packet_info(window, status);
                    printf("recv_swp: Packet has offset %d\n", new_pack_info->packet.header.offset);
                    send_ack(sockfd, send_buf, status, 
                            new_pack_info->packet.header.offset,
                            sender_addr, sender_len);
                }
            } else {
                // TODO: send error packet, followed by closing packet.
                continue;
            }
        }

        // Clear recv and send buffers
        memset(buf, 0, PACKET_SIZE);
        memset(send_buf, 0, PACKET_SIZE);
    }
    free(buf);
    free(send_buf);
    if (file != NULL) {
        fclose(file);
    }
    return 0;
}

void send_ack(int sockfd, void *send_buf, int ack_num, int offset,
        struct sockaddr_in send_addr, socklen_t sender_len) {
    Packet ack;
    ack.header.length = sizeof(ack.header) + 1;
    ack.header.offset = offset;
    ack.header.type = Ack;
    ack.header.ack_num = ack_num;
    ack.data = calloc(1, 1);
    
    fill_send_buffer(send_buf, ack);
    sendto(sockfd, send_buf, ack.header.length, 0,
            (struct sockaddr*)&send_addr, sender_len);
}

int write_swp_packet(Packet packet, FILE* file) {
    int written;
    while (true) {
        written = fwrite(packet.data, 1, get_data_len(packet), file);
        if (written <= 0) {
            continue;
        }
        printf("process_swp_packet: Wrote %d bytes to file.\n", written);
        break;
    }
    return 0;
}

