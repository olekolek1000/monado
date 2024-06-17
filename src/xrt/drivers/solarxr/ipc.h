// Copyright 2024, rcelyte
// SPDX-License-Identifier: BSL-1.0

#pragma once
#include "util/u_logging.h"

struct IpcSocket
{
	_Atomic(int) sockfd;
	_Atomic(uint32_t) sockfd_pin;
	enum u_logging_level log_level;
	int64_t timestamp;
	uint32_t head, buffer_len;
	uint8_t buffer[0x1000];
};

bool
IpcSocket_init(struct IpcSocket *state, enum u_logging_level log_level);
void
IpcSocket_destroy(struct IpcSocket *state); // threadsafe
bool
IpcSocket_connect(const struct IpcSocket *state, const char runtime_path[], const char fallback_path[]);
bool
IpcSocket_wait(struct IpcSocket *state);
bool
IpcSocket_send(struct IpcSocket *state, uint8_t packet[], uint32_t packet_len); // threadsafe
uint32_t
IpcSocket_receive(struct IpcSocket *state);
