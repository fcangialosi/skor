#ifndef WINDOW_H
#define WINDOW_H

struct send_window {
	uint16_t head_index;
	uint16_t head_seq;
	struct packet *packets[WINDOW_SIZE];
};

struct recv_window {
	uint16_t head_index;
	uint16_t head_seq;
	uint16_t ack;
	struct packet *packets[WINDOW_SIZE];
};

struct send_window *send_window_new(uint16_t init_seq);
void send_window_destroy(struct send_window *window);
int add_sent_packet(struct send_window *window, struct packet *packet);
struct packet *get_sent_packet(struct send_window *window, uint16_t seq);
int receive_ack(struct send_window *window, uint16_t ack);

struct recv_window *recv_window_new(uint16_t init_seq);
void recv_window_destroy(struct recv_window *window);
int add_recvd_packet(struct recv_window *window, struct packet *packet);
struct packet *peek_recvd_packet(struct recv_window *window);
struct packet *pop_recvd_packet(struct recv_window *window);

#endif
