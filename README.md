# Comp 429: Reliable File Transfer

Slip days used: 4

# Group Members

Joshua Washington (jsw12)  
Ami Ni (emn4)  
Nathan Eigbe (nee2)  
Matt Joss (maj7)  

# Packet Format

Our packets are a size of 1400 and are constructed in the format of 

typedef struct Packet {  
    Header header;  
    void *data;  
}  

where data is the data of the packet and header is another struct constructed with the fields:

typedef struct Header {  
    size_t length;  
    int32_t offset;  
    enum PacketType type;  
    int16_t ack_num;  
    unsigned short checksum;  
}  

Length is the number of bytes contained in each packet, this includes the size of the header and the packet. Offset is the index of the spot where the data for the file came from in the original file. Type is the type of packet being sent, this is represented by an integer, with the enum given below:

enum PacketType {  
    FileSubdir,  
    Filename,  
    Data,  
    Terminal,  
    Ack  
}  

ack_num is the acknowledgement number, which is used to keep track of which packets we have recieved in our sliding window protocol. Checksum is our checksum which we use to verify that no bit has been corrupted in the file transfer. 

The protocol we are using is the sliding window protocol. The implementation of this is that we use a window size of 60 in which we can send packets 1-60. Then, anytime we recieve an acknowledgement that the lowest packet of our window has been recieved, we shift the recieving window 1 packet down, which means in the case above, the window is now 2-61. We send an acknowledgement packet back to the sender, who then processes it and shifts their window from 1 packet down as well, in the case above, the window is now 2-61. We continue this until the window has reached the end. The reason this is possible is that we keep the offset of the data in the packet, which tells the reciever which chunk of the file this packet contains. This allows the reciever to build the file piece by piece. 

# Features of the design:

We wait for delays until the timeout period and then we resend the packet. Since we use a sliding window protocol, the sender can still send data while waiting for the delay to end. This is important for efficiency of time. The reason this is possible is that we keep the offset of the data in the packet, which tells the reciever which chunk of the file this packet contains. This allows the reciever to build the file piece by piece. 

For dropped packets the sender will resend the packet after the timeout period. This is to make sure that the reciever has recieved the packet properly. 

For reordered packets, we don't really care since the sliding window only sends or recieves packets within the window. This makes it so that you can send a packet in any order within the window.  

Mangled packets are checked using the checksum, which is an additional field that makes sure the data has not been corrupted, as when the reciever runs the checksum function again on the packet, the checksum generated will be 0. If the checksum is not 0, that means we know a bit has been flipped, and we don't send an acknowledgement to the sender, who then retries and sends us the packet again.   

Duplicate packets are not an issue as they are handled by our process packet function, which keeps track of which packets we have recieved already, preventing any duplicate data from being written and sending an acknowledgment for this specific packet to the sender.  

# Running the program 

First change your directory into the folder containing all the files. Then run the make file by using make.  
sendfile.c contains the sending functionality, to run this, use the command: ./sendfile -r <recv host>:<recv port> -f <subdir>/<filename>  
where  
    • <recv host> (Required) The IP address of the remote host in a.b.c.d format.   
    • <recv port> (Required) The UDP port of the remote host.  
    • <subdir> (Required) The local subdirectory where the file is located.  
    • <filename> (Required) The name of the file to be sent.  
    
recvfile.c contains the recieving functionality, this is run with recvfile -p <recv port> where <recv port> contains the port in which the sender is operating on.  

# Testing Method

We ran the sendfile code on the machine on cai.cs.rice.edu and changed the parameters to increase the percent of packets that are delayed, dropped, reordered, mangled, and duplicated all at the same time. We changed these to 40% each. Then we checked that the files were recieved properly by using the diff and the md5sum program to compare the received file against the sent file. 

    
