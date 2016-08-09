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
//#include "eventqueue.h"
#include "tunnel_start_server.h"
//#include "uthash.h"
//#include "window.h"

//#define MAX_PENDING_CONNECTIONS 5

// static uint16_t get_id() {
//     uint16_t id;
//     do {
//         id = rand();
//     } while (FD_ISSET(id, &g_state->ids));
//     FD_SET(id, &g_state->ids);
//     return id;
// }

// static void free_id(uint16_t id) {
//     FD_CLR(id, &g_state->ids);
// }

// Given a connection and a SOCKS4 request from the client, sends the client an appropriate response.
// If the request was valid, also sends a tunneling request through the tunnel.
// Returns 0 if the request packet was valid, else -1.
// static int socks4(struct connection *conn, const struct packet *packet) {
//     int rc = 0;

//     uint8_t version, command;
//     uint16_t port;
//     uint32_t ip;
//     const uint8_t *read_head = packet->data;

//     check(packet->length < sizeof(version) + sizeof(command) + sizeof(port) + sizeof(ip), GST_LEVEL_ERROR, "Short SOCKS request.");

//     write_and_advance_read(&version, &read_head, sizeof(version), NULL);
//     write_and_advance_read(&command, &read_head, sizeof(command), NULL);
//     write_and_advance_read(&port,    &read_head, sizeof(port),    NULL);
//     write_and_advance_read(&ip,      &read_head, sizeof(ip),      NULL);

//     check(version != 0x04, GST_LEVEL_ERROR, "Bad SOCKS version.");
//     check(command != 0x01, GST_LEVEL_ERROR, "Bad SOCKS command.");

//     // Packet to send through tunnel
//     struct packet *syn = create_packet(SYN, sizeof(port) + sizeof(ip));
//     uint8_t *write_head = syn->data;
//     write_and_advance_write(&write_head, &port, sizeof(port), &syn->length);
//     write_and_advance_write(&write_head, &ip,   sizeof(ip),   &syn->length);
//     queue_packet_for_tunnel(conn, syn);

//   error: ;

//     // Packet to send to client
//     struct packet *response = create_packet(OOB | (rc < 0 ? FIN : NONE), 8);
//     response->length = 8;
//     response->data[0] = 0x00;
//     response->data[1] = rc == 0 ? 0x5a : 0x5b;

//     assert(add_recvd_packet(conn->recved, response) == 0);
//     FD_SET(conn->sockfd, &g_state->writefds);

//     return rc;
// }

// static void accept_packet_from_tunnel(struct packet *packet, fd_set *readfds) {
//     gboolean packet_queued = FALSE;

//     uint16_t conn_id;
//     if (extract_header(packet, &conn_id) < 0)
//         return;

//     GST_INFO("[%d/%d/%d/%ld/%s] Recved tunneled packet.", conn_id, LOGSEQ(packet), LOGACK(packet), packet->length,
//             (packet->flags & SYN) == SYN ? "S" : (packet->flags & FIN) == FIN ? "F" : (packet->flags & RST) == RST ? "R" : "-");

//     struct connection *conn;
//     HASH_FIND(hh, g_state->connections, &conn_id, sizeof(conn_id), conn);
//     if (conn == NULL)
//         GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT, FD_ISSET(conn_id, &g_state->ids) ? GST_LEVEL_INFO : GST_LEVEL_ERROR, NULL,
//             "[%d/%d/%d] Recved packet for %s connection.", conn_id, LOGSEQ(packet), LOGACK(packet), FD_ISSET(conn_id, &g_state->ids) ? "closed" : "unknown");

//     if (conn != NULL && (packet->flags & RST) == RST) {
//         GST_INFO("[%d/%d/%d] Connection reset.", conn->id, LOGSEQ(packet), LOGACK(packet));
//         destroy_connection(conn);
//         goto finally;
//     }

//     // Handle ACK
//     if (conn != NULL && (packet->flags & ACK) == ACK) {
//         if (receive_ack(conn->sent, packet->ack) == 0) {
//             switch (conn->state) {
//                 case NEW:
//                     assert(FALSE);
//                     break;
//                 case HANDSHAKING:
//                 case ESTABLISHED:
//                     if (!FD_ISSET(conn->sockfd, &g_state->readfds)) {
//                         FD_SET(conn->sockfd, &g_state->readfds); // ACK moved the left edge of the window, so allow more packets
//                         GST_INFO("[%d/%d/%d] Send window is open.", conn->id, LOGSEQ(packet), LOGACK(packet));
//                     }
//                     break;
//                 case FIN_SENT:
//                     if (conn->sent->head_seq == conn->next_seq) {
//                         GST_INFO("[%d/%d/%d] FIN has been ACKED.", conn->id, LOGSEQ(packet), LOGACK(packet));
//                         destroy_connection(conn);
//                         goto finally;
//                     }
//                     break;
//                 case FIN_RECVED:
//                     break;
//             }

//             conn->repeat_count = 0;

//         } else if (++conn->repeat_count > 2) {
//             struct packet *retrans;
//             if ((retrans = get_sent_packet(conn->sent, packet->ack)) != NULL) {
//                 GST_INFO("[%d/%d/%d] Fast retransmit.", conn->id, LOGSEQ(retrans), LOGACK(retrans));
//                 tunnel_packet(conn->id, retrans, TRUE);
//             }
//             conn->repeat_count = 0;
//         }
//     }

//     // Handle SYN/FIN/data
//     if (NOT_JUST_ACK(packet)) {
//         struct packet *ack = create_packet(ACK, DATALEN(g_state->qrversion));

//         if (conn == NULL) {
//             ack->flags = RST;
//             tunnel_packet(conn_id, ack, FALSE);
//             g_free(ack->data);
//             g_free(ack);
//             ack = NULL;
//         } else {
//             switch (conn->state) {
//                 case NEW:
//                     assert(FALSE);
//                 case HANDSHAKING: // For tunnel_start this means we've sent a SYN, but haven't gotten one back
//                     if ((packet->flags & SYN) == 0) {
//                         GST_ERROR("[%d/%d/%d] Recved out-of-order packet before connection is established.", conn->id, LOGSEQ(packet), LOGACK(packet));
//                         g_free(ack->data);
//                         g_free(ack);
//                         ack = NULL;
//                         break;
//                     } else {
//                         conn->recved->head_seq = conn->recved->ack = packet->seq;
//                         conn->state = ESTABLISHED;
//                         GST_INFO("[%d/%d/%d] Connection established.", conn->id, LOGSEQ(packet), LOGACK(packet));
//                     }
//                 case ESTABLISHED:
//                     if ((packet->flags & FIN) == FIN) {
//                         conn->state = FIN_RECVED;
//                         FD_CLR(conn->sockfd, &g_state->readfds);
//                         FD_CLR(conn->sockfd, readfds);
//                     }
//                 case FIN_RECVED:
//                     // Add data packet to recv window, from whence it will be sent to the socket
//                     if ((packet_queued = TRUE) && add_recvd_packet(conn->recved, packet) == 0)
//                         FD_SET(conn->sockfd, &g_state->writefds);

//                     // The ACK is going out, so if there's data in the socket read it now vice later
//                     if (FD_ISSET(conn->sockfd, readfds)) {
//                         if (read_socket(conn, ack) < 0)
//                             set_closed(conn, ack);
//                         FD_CLR(conn->sockfd, readfds);
//                     }
//                 case FIN_SENT:
//                     queue_packet_for_tunnel(conn, ack);
//                     break;
//             }
//         }
//     }

//   finally:
//     if (!packet_queued) {
//         g_free(packet->data);
//         g_free(packet);
//     }
// }

void tunnel_start_server_loop(struct arguments *args) {
    int rc = 0;
    srand(time(NULL));

    int16_t qr_caps[] = { 929, 1003, 1091, 1171, 1273, 1367, 1465, 1528, 1628, 1732, 1840, 1952, 2068, 2188, 2303, 2431, 2563, 2699, 2809, 2953 };

    // Initialize datastructures
    g_state = g_malloc0(sizeof(struct app_state));
    g_state->qrversion = args->qrversion;
    //g_state->timeout_interval = args->send_timeout;
    g_state->out_queue = args->out_queue;
    //g_state->in_queue = g_async_queue_new();
    //g_state->events = initialize_eventqueue();
    // check(pipe(g_state->pipe) < 0, GST_LEVEL_ERROR, "Failed to create self-pipe.");

    // [NEW]
    // struct packet *packet = create_packet(NONE, DATALEN(g_state->qrversion));
    // tunnel_packet(conn->id, packet, FALSE);

    gsize data_length = DATALEN(g_state->qrversion);
    guchar *data = g_malloc(data_length);
    // TODO add data into data

    GST_INFO("New packet entering tunnel queue!");

    // Encode buffer as QRCode(base64(buffer))
    uint8_t *buffer = g_malloc(data_length);
    memcpy(buffer, data, data_length);
    gchar *base64 = g_base64_encode(buffer, data_length);
    QRcode *qrcode = QRcode_encodeString8bit(base64, g_state->qrversion, QR_ECLEVEL_L);

    g_free(base64);
    g_free(buffer);

    assert(qrcode->width > 0);

    // Add QR code to the queue
    g_async_queue_push(g_state->out_queue, qrcode);
    // [/NEW]



    // Bind and listen to socket
    // g_state->listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // check(g_state->listenfd < 0, GST_LEVEL_ERROR, "Failed to open socket.");
    // struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY), .sin_port = args->port };
    // check(bind(g_state->listenfd, (struct sockaddr *)&address, sizeof(address)) < 0, GST_LEVEL_ERROR, "Failed to bind to port.");
    // check(listen(g_state->listenfd, MAX_PENDING_CONNECTIONS) < 0, GST_LEVEL_ERROR, "Failed to listen() to bound socket.");
    // check(fcntl(g_state->listenfd, F_SETFL, fcntl(g_state->listenfd, F_GETFL, 0) | O_NONBLOCK) < 0, GST_LEVEL_ERROR, "Failed to set listen socket to non-blocking.");

    // Initialize file descriptor sets
    //FD_ZERO(&g_state->ids);
    //FD_ZERO(&g_state->readfds);
    //FD_ZERO(&g_state->writefds);
    //FD_SET(g_state->listenfd, &g_state->readfds);
    //FD_SET(g_state->pipe[0], &g_state->readfds);
    //g_state->maxfd = MAX(g_state->listenfd, g_state->pipe[0]);

    // while (TRUE) {
    //     fd_set readfds = g_state->readfds;
    //     fd_set writefds = g_state->writefds;

    //     struct event *timedout_event;
    //     if (select_until_event(g_state->maxfd + 1, &readfds, &writefds, g_state->events, &timedout_event) < 0) {
    //         if (errno == EINTR)
    //             break;
    //         GST_ERROR("Error on select.");
    //         continue;
    //     }

    //     if (timedout_event != NULL) {
    //         event_type type;
    //         uint16_t conn_id, seq;
    //         unpack_timeout(timedout_event, &type, &conn_id, &seq);
    //         switch (type) {
    //             case CONN_TIMEWAIT:
    //                 free_id(conn_id);
    //                 break;
    //             case PKT_TIMEOUT: ;
    //                 struct connection *conn;
    //                 HASH_FIND(hh, g_state->connections, &conn_id, sizeof(conn_id), conn);
    //                 if (conn != NULL) {
    //                     struct packet *packet = get_sent_packet(conn->sent, seq);
    //                     if (packet != NULL) {
    //                         GST_INFO("[%d/%d/%d] Time out; resending.", conn->id, LOGSEQ(packet), LOGACK(packet));
    //                         packet->flags &= ~ACK;
    //                         tunnel_packet(conn->id, packet, FALSE);
    //                     }
    //                 }
    //                 break;
    //             default:
    //                 assert(FALSE);
    //         }
    //         continue;
    //     }

    //     if (FD_ISSET(g_state->pipe[0], &readfds)) {
    //         // A new packet has arrived through the tunnel
    //         read(g_state->pipe[0], &(char){ 0 }, sizeof(char));
    //         struct packet *packet = g_async_queue_try_pop(g_state->in_queue);
    //         assert(packet != NULL);

    //         accept_packet_from_tunnel(packet, &readfds);
    //     }

    //     if (FD_ISSET(g_state->listenfd, &readfds)) {
    //         // New client connection
    //         struct sockaddr_storage client_address;
    //         socklen_t client_address_length = sizeof(client_address);
    //         int clientfd = accept(g_state->listenfd, (struct sockaddr *)&client_address, &client_address_length);

    //         if (clientfd < 0) {
    //             if (errno != EAGAIN && errno != EWOULDBLOCK)
    //                 GST_ERROR("Failed to accept new connection.");
    //         } else {
    //             // Making the client sockfd non-blocking so we can call recv multiple times on successful select
    //             if (fcntl(clientfd, F_SETFL, fcntl(clientfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
    //                 GST_ERROR("Failed to set client socket to non-blocking.");
    //                 close(clientfd);
    //             } else {
    //                 struct connection *conn = create_connection(get_id(), clientfd, 1);
    //                 conn->recved = recv_window_new(0);

    //                 FD_SET(conn->sockfd, &g_state->readfds);
    //                 g_state->maxfd = MAX(conn->sockfd, g_state->maxfd);
    //                 HASH_ADD(hh, g_state->connections, id, sizeof(conn->id), conn);

    //                 GST_INFO("[%d/--/--] New client on sockfd %d.", conn->id, conn->sockfd);
    //             }
    //         }
    //     }

    //     struct connection *conn, *next;
    //     for (conn = g_state->connections; conn != NULL; conn = next) {
    //         next = conn->hh.next;
    //         int rc = 0;

    //         if (FD_ISSET(conn->sockfd, &readfds)) {
                // struct packet *packet = create_packet(NONE, DATALEN(g_state->qrversion));


                // if ((rc = read_socket(conn, packet)) == 0) {
                //     switch (conn->state) {
                //         case NEW:
                //             if (socks4(conn, packet) == 0) {
                //                 conn->state = HANDSHAKING;
                //             } else {
                //                 conn->state = FIN_RECVED;
                //                 FD_CLR(conn->sockfd, &g_state->readfds);
                //             }
                //             g_free(packet->data);
                //             g_free(packet);
                //             packet = NULL;
                //             break;
                //         case HANDSHAKING:
                //         case ESTABLISHED:
                //             queue_packet_for_tunnel(conn, packet);
                //             break;
                //         case FIN_SENT:
                //         case FIN_RECVED:
                //             assert(FALSE); // Whenever we set state to either of these we (should) clear the sockfd from g_state->readfds
                //             break;
                //     }
                // } else {
                //     set_closed(conn, packet);
                    // queue_packet_for_tunnel(conn, packet);
                    // tunnel_packet(conn->id, packet, FALSE);

        //         }
        //     }

        //     if (rc == 0 && FD_ISSET(conn->sockfd, &writefds) && write_socket(conn) < 0 && conn->state != FIN_RECVED) {
        //         struct packet *packet = create_packet(FIN, 1);
        //         set_closed(conn, packet);
        //         queue_packet_for_tunnel(conn, packet);
        //     }
        // }
    // }

    (void)rc;

  // error:
  //   return -1;
}