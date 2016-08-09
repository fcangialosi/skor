#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>
#include <qrencode.h>

#include "common.h"
#include "window.h"

GST_DEBUG_CATEGORY(gst_skor_tunnel_debug);

void print_window(struct send_window *window) {
    int index;
    for (index = 0; index < WINDOW_SIZE; index++)
        printf("%6d ", index);
    printf("\n");
    for (index = 0; index < WINDOW_SIZE; index++)
        if (window->packets[index] != NULL)
            printf("%6d ", window->packets[index]->seq);
        else
            printf("       ");
    printf("\n\n");
}

void test_send_window() {
    assert(WINDOW_SIZE == 16);

    uint16_t next_seq = UINT16_MAX - 4;

    struct send_window *window = send_window_new(next_seq);
    window->head_index = WINDOW_SIZE - 3;

    int index;
    struct packet *packet;
    for (index = 0; index < 11; index++) {
        packet = g_malloc(sizeof(struct packet));
        packet->seq = next_seq++;
        add_sent_packet(window, packet);
    }
    print_window(window);

    assert(get_sent_packet(window, UINT16_MAX - 5) == NULL);
    assert(get_sent_packet(window, 6) == NULL);
    assert(get_sent_packet(window, 65535)->seq == 65535);
    assert(get_sent_packet(window, 5)->seq == 5);

    receive_ack(window, UINT16_MAX - 1);
    print_window(window);

    receive_ack(window, 2);
    print_window(window);

    receive_ack(window, 2);
    print_window(window);

    receive_ack(window, 6);
    print_window(window);

    do {
        packet = g_malloc(sizeof(struct packet));
        packet->seq = next_seq++;
    } while (add_sent_packet(window, packet) == 0);

    print_window(window);
}

int main (int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    test_send_window();

    return 0;
}