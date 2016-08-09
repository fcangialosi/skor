#ifndef TUNNEL_END_SERVER_H
#define TUNNEL_END_SERVER_H

#include "args.h"

void add_incoming(const char *b64text);
void tunnel_end_server_loop(struct arguments *args);

#endif
