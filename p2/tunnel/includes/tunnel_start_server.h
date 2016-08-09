#ifndef TUNNEL_START_SERVER_H
#define TUNNEL_START_SERVER_H

#include "args.h"

void add_incoming(const char *b64text);
void tunnel_start_server_loop(struct arguments *args);

#endif
