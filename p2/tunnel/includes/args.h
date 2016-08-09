#ifndef ARGS_H
#define ARGS_H

#include <argp.h>
#include <glib.h>
#include <stdint.h>

typedef enum { ORIGIN, TERMINUS } tunnel_end;

struct arguments {
    tunnel_end end;
    uint8_t send_framerate;
    uint8_t send_timeout;
    uint8_t recv_framerate;
    uint8_t qrversion;

    // SkorSrc
    uint16_t port;
    char *output_device;

    // SkorSink
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;

    // Set programmatically
    GAsyncQueue *out_queue;
};

int parse_arguments(int argc, char *argv[], struct arguments *result);

#endif
