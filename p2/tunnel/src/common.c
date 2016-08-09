#include <arpa/inet.h>
#include <assert.h>
#include <qrencode.h>
#include <stdint.h>
#include <unistd.h>

#include "common.h"
#include "eventqueue.h"
#include "window.h"

int16_t qr_caps[] = { 929, 1003, 1091, 1171, 1273, 1367, 1465, 1528, 1628, 1732, 1840, 1952, 2068, 2188, 2303, 2431, 2563, 2699, 2809, 2953 };

void write_and_advance_write(uint8_t **write_head, const void *source, uint32_t length, gsize *total) {
    memcpy(*write_head, source, length);
    *write_head = *write_head + length;
    if (total)
        *total += length;
}

void write_and_advance_read(void *destination, const uint8_t **read_head, uint32_t length, gsize *total) {
    memcpy(destination, *read_head, length);
    *read_head = *read_head + length;
    if (total)
        *total -= length;
}

struct connection *create_connection(uint16_t id, int sockfd, uint16_t start_seq) {
    struct connection *conn = g_malloc(sizeof(struct connection));
    conn->id = id;
    conn->sockfd = sockfd;
    conn->state = NEW;
    conn->next_seq = start_seq;
    conn->sent = send_window_new(conn->next_seq);
    conn->recved = NULL;
    conn->last_ack = -1;
    conn->repeat_count = 0;

    return conn;
}

void set_closed(struct connection *conn, struct packet *packet) {
    conn->state = FIN_SENT;
    FD_CLR(conn->sockfd, &g_state->readfds);
    FD_CLR(conn->sockfd, &g_state->writefds);
    packet->flags |= FIN;
}

void destroy_connection(struct connection *conn) {
    GST_INFO("[%d/--/--] Destroying connection.", conn->id);

    close(conn->sockfd);

    send_window_destroy(conn->sent);
    recv_window_destroy(conn->recved);

    HASH_DELETE(hh, g_state->connections, conn);

    FD_CLR(conn->sockfd, &g_state->readfds);
    FD_CLR(conn->sockfd, &g_state->writefds);

    g_free(conn);

    set_timeout(CONN_TIMEWAIT, conn->id, 0);
}

int read_socket(const struct connection *conn, struct packet *packet) {
    int rc = 0;

    ssize_t n_bytes_recved = recv(conn->sockfd, packet->data, DATALEN(g_state->qrversion), 0);
    check(n_bytes_recved <  0 && (errno != EAGAIN && errno != EWOULDBLOCK), GST_LEVEL_ERROR, "[%d/--/--] Failed to read from socket %d.", conn->id, conn->sockfd);
    check(n_bytes_recved == 0, GST_LEVEL_INFO, "[%d/--/--] Socket %d closed by peer.", conn->id, conn->sockfd);

    packet->length = MAX(0, n_bytes_recved); // Allow for spurious select;

    GST_INFO("[%d/--/--] Read %ld bytes from socket %d.", conn->id, packet->length, conn->sockfd);

  error:
    return rc;
}

int write_socket(struct connection *conn) {
    int rc = 0;

    struct packet *packet = peek_recvd_packet(conn->recved);
    ssize_t n_bytes_sent = send(conn->sockfd, packet->data + packet->sent, packet->length - packet->sent, 0);
    check(n_bytes_sent < 0, GST_LEVEL_ERROR, "[%d/%d/%d] Failed to write to socket %d.", conn->id, LOGSEQ(packet), LOGACK(packet), conn->sockfd);
    packet->sent += n_bytes_sent;

    GST_INFO("[%d/%d/%d] Wrote %ld/%ld bytes to socket %d.", conn->id, LOGSEQ(packet), LOGACK(packet), n_bytes_sent, packet->length, conn->sockfd);

    if (packet->sent == packet->length) {
        pop_recvd_packet(conn->recved);

        if (peek_recvd_packet(conn->recved) == NULL)
            FD_CLR(conn->sockfd, &g_state->writefds);

        free(packet->data);
        if (conn->state == FIN_RECVED && (packet->flags & FIN) == FIN)
            destroy_connection(conn);
        free(packet);
    }

  error:
    return rc;
}

struct packet *create_packet(uint16_t flags, gsize data_length) {
    struct packet *packet = g_malloc(sizeof(struct packet));
    packet->seq = packet->ack = packet->sent = packet->length = 0;
    packet->flags = flags;
    packet->data = g_malloc(data_length);
    return packet;
}

int extract_header(struct packet *packet, uint16_t *conn_id) {
    if (packet->length < HEADERLEN)
        return -1;

    const uint8_t *read_head = packet->data;

    write_and_advance_read(conn_id,        &read_head, sizeof(*conn_id),      &packet->length);
    write_and_advance_read(&packet->flags, &read_head, sizeof(packet->flags), &packet->length);
    write_and_advance_read(&packet->seq,   &read_head, sizeof(packet->seq),   &packet->length);
    write_and_advance_read(&packet->ack,   &read_head, sizeof(packet->ack),   &packet->length);

    *conn_id      = ntohs(*conn_id);
    packet->flags = ntohs(packet->flags);
    packet->seq   = ntohs(packet->seq);
    packet->ack   = ntohs(packet->ack);

    memmove(packet->data, read_head, packet->length);

    return 0;
}

// This is called by SkorSink to add a decoded QR code to the incoming packet queue.
// It writes to the self-pipe in order to wake up the select() in the server's main loop.
void untunnel_packet(const char *b64text) {
    struct packet *packet = g_malloc(sizeof(struct packet));
    packet->seq = packet->ack = packet->flags = packet->sent = 0;
    packet->data = g_base64_decode(b64text, &packet->length);
    g_async_queue_push(g_state->in_queue, packet);

    write(g_state->pipe[1], &(char){ 0 }, sizeof(char));
}

gint sort(gconstpointer a, gconstpointer b, gpointer user_data) {
    if (a == user_data)
        return -1;
    else if (b == user_data)
        return 1;
    else
        assert(FALSE);
}

// This frames a packet, encodes it into a QR code, and then pushes it onto SkorSrc's input queue.
// It also sets a re-send timeout if the packet is a SYN, FIN, or contains data (but not if it's only an ACK).
void tunnel_packet(uint16_t conn_id, const struct packet *packet, gboolean fast_retrans) {
    // assert(packet->length <= (gsize)DATALEN(g_state->qrversion));

    GST_INFO("[%d/%d/%d/%ld/%s] Entering tunnel queue.", conn_id, LOGSEQ(packet), LOGACK(packet), packet->length,
            (packet->flags & SYN) == SYN ? "S" : (packet->flags & FIN) == FIN ? "F" : (packet->flags & RST) == RST ? "R" : "-");

    uint8_t *buffer = g_malloc(packet->length + HEADERLEN);
    uint8_t *write_head = buffer;

    write_and_advance_write(&write_head, &(uint16_t){ htons(conn_id) },      sizeof(uint16_t), NULL);
    write_and_advance_write(&write_head, &(uint16_t){ htons(packet->flags) }, sizeof(uint16_t), NULL);
    write_and_advance_write(&write_head, &(uint16_t){ htons(packet->seq) },   sizeof(uint16_t), NULL);
    write_and_advance_write(&write_head, &(uint16_t){ htons(packet->ack) },   sizeof(uint16_t), NULL);
    write_and_advance_write(&write_head, packet->data,                        packet->length,   NULL);

    gchar *base64 = g_base64_encode(buffer, packet->length + HEADERLEN);
    QRcode *qrcode = QRcode_encodeString8bit(base64, g_state->qrversion, QR_ECLEVEL_L);
    g_free(base64);

    g_free(buffer);

    assert(qrcode->width > 0);
    if (fast_retrans)
        g_async_queue_push_sorted(g_state->out_queue, qrcode, sort, qrcode);
    else
        g_async_queue_push(g_state->out_queue, qrcode);

    if (NOT_JUST_ACK(packet) && !fast_retrans)
        set_timeout(PKT_TIMEOUT, conn_id, packet->seq);
}

// Given a connection and a packet to send through the tunnel, performs the following:
// 1) sets the ACK number if the flag is set
// 2) assigns a sequence number if appropriate
// 3) adds the packet to the send window if appropriate, clearing the sockfd if the window is full
// 4) adds the packet to the tunnel's input queue
void queue_packet_for_tunnel(struct connection *conn, struct packet *packet) {
    if ((packet->flags & ACK) == ACK)
        packet->ack = conn->recved->ack;

    if (NOT_JUST_ACK(packet)) {
        packet->seq = conn->next_seq++;
        if (add_sent_packet(conn->sent, packet) < 0) {
            FD_CLR(conn->sockfd, &g_state->readfds); // the send window is full
            GST_INFO("[%d/%d/%d] Send window is full.", conn->id, LOGSEQ(packet), LOGACK(packet));
        }
    }

    tunnel_packet(conn->id, packet, FALSE);
}

void set_timeout(event_type type, uint16_t conn_id, uint16_t seq) {
    struct event *event = g_malloc(sizeof(struct event));

    event->remaining_time = g_malloc(sizeof(struct timeval));
    event->remaining_time->tv_usec = 0;
    event->remaining_time->tv_sec = (type == CONN_TIMEWAIT) ? CONN_TIMEWAIT_INTERVAL
                                  : (type == PKT_TIMEOUT)   ? g_state->timeout_interval
                                  :                           -1;
    if (event->remaining_time->tv_sec == -1) assert(FALSE);

    struct tunnel_event *tunnel_event = g_malloc(sizeof(struct tunnel_event));
    tunnel_event->type = type;
    tunnel_event->conn_id = conn_id;
    tunnel_event->seq = seq;
    event->data = tunnel_event;

    add_event(g_state->events, event);
}

void unpack_timeout(struct event *event, event_type *type, uint16_t *conn_id, uint16_t *seq) {
    struct tunnel_event *tunnel_event = (struct tunnel_event *)event->data;
    *type = tunnel_event->type;
    *conn_id = tunnel_event->conn_id;
    *seq = tunnel_event->seq;

    g_free(event->data);
    g_free(event->remaining_time);
    g_free(event);
}
