#include <assert.h>

#include "common.h"
#include "window.h"

// Allocates a new send window.
struct send_window *send_window_new(uint16_t init_seq) {
	struct send_window *window = g_malloc0(sizeof(struct send_window));
	window->head_seq = init_seq;
	return window;
};

// Frees the memory associated with a send window and its contents.
void send_window_destroy(struct send_window *window) {
	int index;
	for (index = 0; index < WINDOW_SIZE; index++)
		if (window->packets[index] != NULL) {
			g_free(window->packets[index]->data);
			g_free(window->packets[index]);
		}
	g_free(window);
};

// Adds the packet to the send window. Must only be called when it is know that 
// the packet's sequence number is inside the window. Returns 0 if there is room 
// for more packets (assuming they are added in order of seq number), otherwise -1.
int add_sent_packet(struct send_window *window, struct packet *packet) {
	uint16_t rel_index = packet->seq - window->head_seq; // intentional overflow
	assert(rel_index < WINDOW_SIZE);
	uint16_t abs_index = (window->head_index + rel_index) % WINDOW_SIZE;
	window->packets[abs_index] = packet;
	return rel_index < (WINDOW_SIZE - 1) ? 0 : -1;
}

// Retrieves a packet by sequence number. Returns NULL if the sequence number
// given is outside of the window.
struct packet *get_sent_packet(struct send_window *window, uint16_t seq) {
	uint16_t rel_index = seq - window->head_seq; // intentional overflow
	if (rel_index >= WINDOW_SIZE)
		return NULL;
	uint16_t abs_index = (window->head_index + rel_index) % WINDOW_SIZE;
	return window->packets[abs_index];
}

// Advances the left edge of the send window, freeing ACKed packets. Returns
// 0 if the left edge of the window was moved, otherwise -1.
int receive_ack(struct send_window *window, uint16_t ack) {
	uint16_t rel_index = ack - window->head_seq; // intentional overflow
	if (rel_index >= WINDOW_SIZE)
		return -1;
	int rc = rel_index > 0 ? 0 : -1;
	while (rel_index-- > 0) {
		if (window->packets[window->head_index] != NULL) {
			g_free(window->packets[window->head_index]->data);
			g_free(window->packets[window->head_index]);
			window->packets[window->head_index] = NULL;
		}
		window->head_index = (window->head_index + 1) % WINDOW_SIZE;
		window->head_seq++; // intentional overflow
	}
	return rc;
}

// Allocates a new recv window.
struct recv_window *recv_window_new(uint16_t init_seq) {
	struct recv_window *window = g_malloc0(sizeof(struct recv_window));
	window->head_seq = window->ack = init_seq;
	return window;
}

// Frees the memory associated with a recv window and its contents.
void recv_window_destroy(struct recv_window *window) {
	int index;
	for (index = 0; index < WINDOW_SIZE; index++)
		if (window->packets[index] != NULL) {
			g_free(window->packets[index]->data);
			g_free(window->packets[index]);
		}
	g_free(window);
};

// Adds a packet to the recv window. Returns 0 if there is a packet is ready to consume;
// otherwise -1. Afterwards window->ack will be set to the next sequence number desired.
int add_recvd_packet(struct recv_window *window, struct packet *packet) {
	if ((packet->flags & OOB) == OOB) { // Out-of-band packet (i.e. no sequence number)
		assert(window->packets[window->head_index] == NULL);
		window->packets[window->head_index] = packet;
		return 0;
	}

	uint16_t rel_index = packet->seq - window->head_seq; // intentional overflow
	if (rel_index >= WINDOW_SIZE) {
	    GST_INFO("Rejected out-of-window packet with sequence number %d.", packet->seq);
		goto end;
	}
	uint16_t abs_index = (window->head_index + rel_index) % WINDOW_SIZE;
	window->packets[abs_index] = packet;

	// Advance the ACK if possible
	while (packet != NULL && window->ack == packet->seq) {
		window->ack++; // intentional overflow
		abs_index = (abs_index + 1) % WINDOW_SIZE;
		packet = window->packets[abs_index];
	}

  end:
  	return window->packets[window->head_index] != NULL ? 0 : -1;
}

// Returns but does not remove a packet from the recv window.
struct packet *peek_recvd_packet(struct recv_window *window) {
	return window->packets[window->head_index];
}

// Removes and returns a packet from the recv window. Must only be called when it is known 
// that a packet is available. 
struct packet *pop_recvd_packet(struct recv_window *window) {
	struct packet *packet = window->packets[window->head_index];
	assert(packet != NULL && ((packet->flags & OOB) == OOB || packet->seq < window->ack));
	window->packets[window->head_index] = NULL;
	window->head_index = (window->head_index + 1) % WINDOW_SIZE;
	if ((packet->flags & OOB) == 0)
		window->head_seq++; // intentional overflow
	return packet;
}
