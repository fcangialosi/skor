#ifndef COMMON_H
#define COMMON_H

#include <gst/gst.h>
#include <stdint.h>
#include <sys/queue.h>

#include "args.h"
//#include "eventqueue.h"
//#include "uthash.h"

//#define WINDOW_SIZE 16                         // Max number of in-flight packets allowed

int16_t qr_caps[] = { 929, 1003, 1091, 1171, 1273, 1367, 1465, 1528, 1628, 1732, 1840, 1952, 2068, 2188, 2303, 2431, 2563, 2699, 2809, 2953 };

#define BINLEN(V) (qr_caps[(V) - 21] * 3 / 4)  // Base64 overhead
//#define HEADERLEN 8                            // 2b for ID (sockfd), 2b for flags, 2b for seq, 2b for ACK
#define DATALEN(V) ((BINLEN(V)))// - (HEADERLEN))

//#define NOT_JUST_ACK(P) ((P->flags & (SYN | FIN | RST)) != 0 || P->length > 0)
//#define LOGSEQ(P) (NOT_JUST_ACK(P) ? P->seq : -1)
//#define LOGACK(P) ((P->flags & ACK) == ACK ? P->ack : -1)

//#define CONN_TIMEWAIT_INTERVAL 60              // Number of seconds to wait before we can recycle the connection's ID

#define GST_CAT_DEFAULT gst_skor_tunnel_debug
#define check(A, L, ...) if (A) { GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT, L, NULL, ##__VA_ARGS__); rc = -1; goto error; }

GST_DEBUG_CATEGORY_EXTERN(gst_skor_tunnel_debug);
extern struct app_state *g_state;

extern int16_t qr_caps[20];

//typedef enum { NEW, HANDSHAKING, ESTABLISHED, FIN_SENT, FIN_RECVED } conn_state;
//typedef enum { CONN_TIMEWAIT, PKT_TIMEOUT } event_type;
//typedef enum { NONE = 0x00, SYN = 0x01, ACK = 0x02, FIN = 0x04, RST = 0x10, OOB = 0x20 } flag;

// struct tunnel_event {
//     event_type type;
//     uint16_t conn_id;
//     uint16_t seq;                        // Only applicable to PKT_TIMEOUT
// };

// struct packet {
//     uint16_t seq;                        // The sequence number
//     uint16_t ack;                        // The cumulative ACK
//     uint16_t flags;

//     gsize sent;
//     gsize length;
//     guchar *data;
// };

// struct connection {
//     uint16_t id;                         // A unique identifier, chosen by the origin
//     int sockfd;
//     conn_state state;

//     uint16_t next_seq;                   // The sequence number to assign to the next packet sent
//     struct send_window *sent;            // The window of packets that are in-flight through the tunnel
//     struct recv_window *recved;          // The window of packets recved through the tunnel

//     int last_ack;
//     int repeat_count;

//     UT_hash_handle hh;
// };

struct app_state {
    //struct connection *connections;      // Hashtable by id

    GAsyncQueue *out_queue;              // Queue of QR codes to be sent through Skype tunnel
    //GAsyncQueue *in_queue;               // Queue of packets received through Skype tunnel
    //int pipe[2];                         // Used to wake up thread from select() when a packet arrives

    //fd_set ids;                          // Used to assign unique ids to connections
    uint8_t qrversion;                   //
    //uint8_t timeout_interval;            //

    //struct eventqueue *events;           // Used to TIMEWAIT on ids and re-send packets after timeout

    //int listenfd;
    //fd_set readfds;
    //fd_set writefds;
    //int maxfd;
};

// void write_and_advance_write(uint8_t **write_head, const void *source, uint32_t length, gsize *total);
// void write_and_advance_read(void *destination, const uint8_t **read_head, uint32_t length, gsize *total);

// struct connection *create_connection(uint16_t id, int sockfd, uint16_t start_seq);
// void set_closed(struct connection *conn, struct packet *packet);
// void destroy_connection(struct connection *conn);

// int read_socket(const struct connection *conn, struct packet *packet);
// int write_socket(struct connection *conn);

// struct packet *create_packet(uint16_t flags, gsize data_length);
// int extract_header(struct packet *packet, uint16_t *conn_id);

// void untunnel_packet(const char *b64text);
// void tunnel_packet(uint16_t conn_id, const struct packet *packet, gboolean fast_retrans);
// void queue_packet_for_tunnel(struct connection *conn, struct packet *packet);

//void set_timeout(event_type type, uint16_t conn_id, uint16_t seq);
//void unpack_timeout(struct event *event, event_type *type, uint16_t *conn_id, uint16_t *seq);

#endif

