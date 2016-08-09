#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "common.h"

extern int g_verbosity;

static int parse_and_validate_int(const char *string, int min_value, int max_value, int *result) {
    errno = 0;
    char *end_pointer;
    long value = strtol(string, &end_pointer, 0);

    if ((errno == ERANGE && (value == LONG_MAX || value == LONG_MIN))
            || value < min_value || value > max_value)
        return -1;

    if (end_pointer == string)
        return -2;

    if (errno != 0 && value == 0)
        return -3;

    *result = (int)value;

    if (*end_pointer != '\0')
        return -4;

    return 0;
}

/* Argp parser function (see http://www.gnu.org/software/libc/manual/html_node/Argp-Parser-Functions.html)
 */
static error_t option_parser(int key, char *value, struct argp_state *state) {
    struct arguments *args = state->input;
    error_t ret = 0;
    int int_value;

    switch (key) {
        case 's':
            if (parse_and_validate_int(value, 1, 30, &int_value) < 0)
                argp_error(state, "Send frame-rate must be between 1 and 30 inclusive");
            args->send_framerate = int_value;
            break;
        case 't':
            if (parse_and_validate_int(value, 1, 300, &int_value) < 0)
                argp_error(state, "Send time-out must be between 1 and 300 inclusive");
            args->send_timeout = int_value;
            break;
        case 'r':
            if (parse_and_validate_int(value, 1, 30, &int_value) < 0)
                argp_error(state, "Receive frame-rate must be between 1 and 30 inclusive");
            args->recv_framerate = int_value;
            break;
        case 'p':
            if (parse_and_validate_int(value, 1, 65535, &int_value) < 0)
                argp_error(state, "Port number must be between 1 and 65535 inclusive");
            args->port = htons(int_value);
            break;
        case 'o':
            args->output_device = malloc(strlen(value) + 1);
            strcpy(args->output_device, value);
            break;
        case 'q':
            if (parse_and_validate_int(value, 21, 40, &int_value) < 0)
                argp_error(state, "QR code version must be between 21 and 40 inclusive");
            args->qrversion = int_value;
            break;
        case 'x':
            if (parse_and_validate_int(value, 0, 65535, &int_value) < 0)
                argp_error(state, "X-coordinate must be between 0 and 65535 inclusive");
            args->x = int_value;
            break;
        case 'y':
            if (parse_and_validate_int(value, 0, 65535, &int_value) < 0)
                argp_error(state, "Y-coordinate must be between 0 and 65535 inclusive");
            args->y = int_value;
            break;
        case 'w':
            if (parse_and_validate_int(value, 1, 65535, &int_value) < 0)
                argp_error(state, "Width must be between 1 and 65535 inclusive");
            args->w = int_value;
            break;
        case 'h':
            if (parse_and_validate_int(value, 1, 65535, &int_value) < 0)
                argp_error(state, "Height must be between 1 and 65535 inclusive");
            args->h = int_value;
            break;
        default:
            ret = ARGP_ERR_UNKNOWN;
            break;
    }

    return ret;
}

int parse_arguments(int argc, char *argv[], struct arguments *result) {
    int rc = 0;
    memset(result, 0, sizeof(*result));
    result->send_framerate = 1;
    result->send_timeout = 30; // TODO get rid of this
    result->recv_framerate = 1;
    result->qrversion = 10;
    result->x = 0;
    result->y = 20;
    result->w = result->h = 65;

    struct argp_option options[] = {
        { "send-framerate", 's', "number", 0, "The number of frames per second to send", 0 },
        { "send-timeout",   't', "number", 0, "The number of seconds to wait before re-sending a packet", 0 },
        { "recv-framerate", 'r', "number", 0, "The number of frames per second to recv", 0 },
        { "port",           'p', "port",   0, "The port to bind to (only applicable to the tunnel start; if not specified, indicates that this is the tunnel end)", 0 },
        { "out-dev",        'o', "device", 0, "The V4L2 device to output to", 0 },
        { "qrversion",      'q', "number", 0, "The QR code version level (between 21 and 40, inclusive)", 0 },
        { "x-coord",        'x', "number", 0, "The x-coordinate of the left edge of the capture area", 0 },
        { "y-coord",        'y', "number", 0, "The y-coordinate of the top edge of the capture area", 0 },
        { "width",          'w', "number", 0, "The width of the capture area", 0 },
        { "height",         'h', "number", 0, "The height of the capture area", 0 },
        { 0 }
    };

    struct argp argp_parser = { options, option_parser, 0, 0, 0, 0, 0 };

    check(argp_parse(&argp_parser, argc, argv, 0, NULL, result) < 0, GST_LEVEL_ERROR, "Error parsing arguments");

    if (result->output_device == NULL) {
        GST_ERROR("The V4L2 output device must be specified\n");
        argp_help(&argp_parser, stderr, ARGP_HELP_SHORT_USAGE | ARGP_HELP_LONG, "origin");
        rc = -1;
        goto error;
    }

  error:
    return rc;
}
