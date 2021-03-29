# comp429-project2

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
