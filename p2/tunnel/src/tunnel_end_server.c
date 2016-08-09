#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include <qrencode.h>

#include "common.h"
#include "eventqueue.h"
#include "tunnel_end_server.h"
#include "uthash.h"
#include "window.h"

static int connect_to_server(uint16_t conn_id, struct packet *packet) {
    int rc = 0, sockfd = -1;

    uint16_t port;
    uint32_t ip;
    check(packet->length != sizeof(port) + sizeof(ip), GST_LEVEL_ERROR, "Bad hello of length %ld.", packet->length);
    const uint8_t *read_head = packet->data;
    write_and_advance_read(&port, &read_head, sizeof(port), &packet->length);
    write_and_advance_read(&ip,   &read_head, sizeof(ip),   &packet->length);

    char ipstring[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip, ipstring, INET_ADDRSTRLEN);
    GST_INFO("[%d/%d/%d] Requesting forwarding to %s:%d", conn_id, LOGSEQ(packet), LOGACK(packet), ipstring, ntohs(port));

    check((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0, GST_LEVEL_ERROR, "Failed to create socket.");
    check(fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) < 0, GST_LEVEL_ERROR, "Failed to set socket to non-blocking.");
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr = { .s_addr = ip }, .sin_port = port };
    check(connect(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0 && errno != EINPROGRESS,
            GST_LEVEL_ERROR, "[%d/%d/%d] Could not establish connection to %s:%d (error number %d).", conn_id, LOGSEQ(packet), LOGACK(packet), ipstring, ntohs(port), errno);

    return sockfd;

  error:
    if (sockfd > -1)
        close(sockfd);

    return rc;
}

static void accept_packet_from_tunnel(struct packet *packet, fd_set *readfds) {
    gboolean packet_queued = FALSE;

    uint16_t conn_id;
    if (extract_header(packet, &conn_id) < 0)
        return;

    GST_INFO("[%d/%d/%d/%ld/%s] Recved tunneled packet.", conn_id, LOGSEQ(packet), LOGACK(packet), packet->length, 
            (packet->flags & SYN) == SYN ? "S" : (packet->flags & FIN) == FIN ? "F" : "-");

    struct connection *conn = NULL;
    HASH_FIND(hh, g_state->connections, &conn_id, sizeof(conn_id), conn);
    if (conn == NULL) {
        if ((packet->flags & SYN) == 0) {
            GST_ERROR("[%d/%d/%d] Recved out-of-order packet before connection is established.", conn_id, LOGSEQ(packet), LOGACK(packet));
            goto finally;
        } else {
            int sockfd;
            if ((sockfd = connect_to_server(conn_id, packet)) < 0) {
                struct packet *reset = create_packet(RST, 1);
                tunnel_packet(conn_id, reset, FALSE);
                g_free(reset->data);
                g_free(reset);
                goto finally;
            }

            conn = create_connection(conn_id, sockfd, 91);
            FD_SET(conn->sockfd, &g_state->readfds);
            g_state->maxfd = MAX(conn->sockfd, g_state->maxfd);
            HASH_ADD(hh, g_state->connections, id, sizeof(conn->id), conn);

            GST_INFO("[%d/%d/%d] New tunneled connection on sockfd %d.", conn->id, LOGSEQ(packet), LOGACK(packet), conn->sockfd);
        }
    } 

    if ((packet->flags & RST) == RST) {
        GST_INFO("[%d/%d/%d] Connection reset.", conn->id, LOGSEQ(packet), LOGACK(packet));
        destroy_connection(conn);
        goto finally;
    }

    // Handle ACK
    if ((packet->flags & ACK) == ACK) {
        if (receive_ack(conn->sent, packet->ack) == 0) {
            switch (conn->state) {
                case NEW:
                    assert(FALSE);
                    break;
                case HANDSHAKING:
                    conn->state = ESTABLISHED;
                    GST_INFO("[%d/%d/%d] Connection established.", conn->id, LOGSEQ(packet), LOGACK(packet));
                case ESTABLISHED:
                    if (!FD_ISSET(conn->sockfd, &g_state->readfds)) {
                        FD_SET(conn->sockfd, &g_state->readfds); // ACK moved the left edge of the window, so allow more packets
                        GST_INFO("[%d/%d/%d] Send window is open.", conn->id, LOGSEQ(packet), LOGACK(packet));
                    }
                    break;
                case FIN_SENT:
                    if (conn->sent->head_seq == conn->next_seq) {
                        GST_INFO("[%d/%d/%d] FIN has been ACKED.", conn->id, LOGSEQ(packet), LOGACK(packet));
                        destroy_connection(conn);
                        goto finally;
                    }
                    break;
                case FIN_RECVED:
                    break;
            }

            conn->repeat_count = 0;

        } else if (++conn->repeat_count > 2) {
            struct packet *retrans;
            if ((retrans = get_sent_packet(conn->sent, packet->ack)) != NULL) {
                GST_INFO("[%d/%d/%d] Fast retransmit.", conn->id, LOGSEQ(retrans), LOGACK(retrans));
                tunnel_packet(conn->id, retrans, TRUE);
            }
            conn->repeat_count = 0;
        }
    }

    // Handle SYN/FIN/data
    if (NOT_JUST_ACK(packet)) {
        struct packet *ack = create_packet(ACK, DATALEN(g_state->qrversion));

        switch (conn->state) {
            case NEW:
                assert((packet->flags & SYN) == SYN);
                conn->recved = recv_window_new(packet->seq);
                conn->state = HANDSHAKING;
                ack->flags |= SYN;
            case HANDSHAKING: // For tunnel_end this means we've recved a SYN, but haven't gotten an ACK for the one we sent
            case ESTABLISHED:
                if ((packet->flags & FIN) == FIN) {
                    conn->state = FIN_RECVED;
                    FD_CLR(conn->sockfd, &g_state->readfds);
                    FD_CLR(conn->sockfd, readfds);
                }
            case FIN_RECVED:
                // Add data packet to recv window, from whence it will be sent to the socket 
                if ((packet_queued = TRUE) && add_recvd_packet(conn->recved, packet) == 0)
                    FD_SET(conn->sockfd, &g_state->writefds);

                // The ACK is going out, so if there's data in the socket read it now vice later
                if (FD_ISSET(conn->sockfd, readfds)) {
                    if (read_socket(conn, ack) < 0)
                        set_closed(conn, ack);
                    FD_CLR(conn->sockfd, readfds);
                }
            case FIN_SENT:
                queue_packet_for_tunnel(conn, ack);
                break;
        }
    }

  finally:
    if (!packet_queued) {
        g_free(packet->data);
        g_free(packet);
    }
}

void tunnel_end_server_loop(struct arguments *args) {
    int rc = 0;
    srand(time(NULL));

    // Initialize datastructures
    g_state = g_malloc0(sizeof(struct app_state));
    g_state->qrversion = args->qrversion;
    g_state->timeout_interval = args->send_timeout;
    g_state->out_queue = args->out_queue;
    g_state->in_queue = g_async_queue_new();
    g_state->events = initialize_eventqueue();
    check(pipe(g_state->pipe) < 0, GST_LEVEL_ERROR, "Failed to create self-pipe.");

    // Initialize file descriptor sets
    FD_ZERO(&g_state->readfds);
    FD_ZERO(&g_state->writefds);
    FD_SET(g_state->pipe[0], &g_state->readfds);
    g_state->maxfd = g_state->pipe[0];

    while (TRUE) {
        fd_set readfds = g_state->readfds;
        fd_set writefds = g_state->writefds;

        struct event *timedout_event;
        if (select_until_event(g_state->maxfd + 1, &readfds, &writefds, g_state->events, &timedout_event) < 0) {
            if (errno == EINTR)
                break;
            GST_ERROR("Error on select.");
            continue;
        } 

        if (timedout_event != NULL) {
            event_type type;
            uint16_t conn_id, seq;
            unpack_timeout(timedout_event, &type, &conn_id, &seq);

            if (type == PKT_TIMEOUT) {
                struct connection *conn;
                HASH_FIND(hh, g_state->connections, &conn_id, sizeof(conn_id), conn);
                if (conn != NULL) {
                    struct packet *packet = get_sent_packet(conn->sent, seq);
                    if (packet != NULL) {
                        GST_INFO("[%d/%d/%d] Time out; resending.", conn->id, LOGSEQ(packet), LOGACK(packet));
                        packet->flags &= ~ACK;
                        tunnel_packet(conn->id, packet, FALSE);
                    }
                }
            }

            continue;
        }

        if (FD_ISSET(g_state->pipe[0], &readfds)) {
            // A new packet has arrived through the tunnel
            read(g_state->pipe[0], &(char){ 0 }, sizeof(char));
            struct packet *packet = g_async_queue_try_pop(g_state->in_queue);
            assert(packet != NULL);

            accept_packet_from_tunnel(packet, &readfds);
        }

        struct connection *conn, *next;
        for (conn = g_state->connections; conn != NULL; conn = next) {
            next = conn->hh.next;
            int rc = 0;

            if (FD_ISSET(conn->sockfd, &readfds)) {
                struct packet *packet = create_packet(NONE, DATALEN(g_state->qrversion));

                if ((rc = read_socket(conn, packet)) == 0) {
                    switch (conn->state) {
                        case NEW:
                            assert(FALSE);
                            break;
                        case HANDSHAKING:
                        case ESTABLISHED:
                            queue_packet_for_tunnel(conn, packet);
                            break;
                        case FIN_SENT:
                        case FIN_RECVED:
                            assert(FALSE); // Whenever we set state to either of these we (should) clear the sockfd from g_state->readfds
                            break;
                    }
                } else {
                    set_closed(conn, packet);
                    queue_packet_for_tunnel(conn, packet);
                }
            }

            if (rc == 0 && FD_ISSET(conn->sockfd, &writefds) && write_socket(conn) < 0 && conn->state != FIN_RECVED) {
                struct packet *packet = create_packet(FIN, 1);
                set_closed(conn, packet);
                queue_packet_for_tunnel(conn, packet);
            }
        }
    }

    (void)rc;

  error:
    ;
}