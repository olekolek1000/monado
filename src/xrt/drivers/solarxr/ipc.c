// Copyright 2024, rcelyte
// SPDX-License-Identifier: BSL-1.0

#include "ipc.h"
#include "os/os_time.h"
#include "util/u_file.h"
#include <endian.h>
#include <errno.h>
#include <linux/un.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// TODO: don't stall indefinitely in `IpcSocket_wait()` on connection loss

bool
IpcSocket_init(struct IpcSocket *const state, const enum u_logging_level log_level)
{
	state->sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	state->sockfd_pin = 0;
	state->log_level = log_level;
	state->timestamp = (int64_t)os_monotonic_get_ns();
	state->buffer_len = state->head = 0;
	if (state->sockfd == -1) {
		U_LOG_IFL_E(log_level, "socket() failed");
		return false;
	}
	return true;
}

void
IpcSocket_destroy(struct IpcSocket *const state)
{
	const int sockfd = atomic_exchange(&state->sockfd, -1);
	if (sockfd == -1) {
		return;
	}
	shutdown(sockfd, SHUT_RDWR); // unblock `IpcSocket_wait()`
	while (atomic_load(&state->sockfd_pin) != 0)
		sched_yield();
	close(sockfd);
}

static bool path_is_socket(const char path[const]) {
	struct stat result = {0};
	return stat(path, &result) == 0 && S_ISSOCK(result.st_mode);
}

bool
IpcSocket_connect(const struct IpcSocket *const state, const char runtime_path[const], const char fallback_path[const])
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	ssize_t path_len = u_file_get_path_in_runtime_dir(runtime_path, addr.sun_path, sizeof(addr.sun_path));
	if(path_len <= 0 || (size_t)path_len >= sizeof(addr.sun_path)) {
		U_LOG_IFL_E(state->log_level, "u_file_get_path_in_runtime_dir() failed");
		return false;
	}
	if(!path_is_socket(addr.sun_path)) {
		const char *env;
		path_len =
			((env = getenv("XDG_DATA_HOME")) != NULL) ? snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", env, fallback_path) :
			((env = getenv("HOME")) != NULL) ? snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/.local/share/%s", env, fallback_path) :
			0;
		if(path_len <= 0 || (size_t)path_len >= sizeof(addr.sun_path)) {
			U_LOG_IFL_E(state->log_level, "failed to resolve SlimeVR socket path");
			return false;
		}
		if(!path_is_socket(addr.sun_path)) {
			U_LOG_IFL_E(state->log_level, "path not found");
			return false;
		}
	}
	if(connect(state->sockfd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
		U_LOG_IFL_E(state->log_level, "connect() failed: %s", strerror(errno));
		return false;
	}
	return true;
}

bool
IpcSocket_wait(struct IpcSocket *const state)
{
	atomic_fetch_add(&state->sockfd_pin, 1);
	struct pollfd sockfd = {atomic_load(&state->sockfd), POLLIN, 0};
	bool result = false;
	if (sockfd.fd != -1) {
		result = poll(&sockfd, 1, -1) != -1 || errno == EINTR;
	}
	atomic_fetch_sub(&state->sockfd_pin, 1);
	return result;
}

bool
IpcSocket_send(struct IpcSocket *const state, uint8_t packet[const], const uint32_t packet_len)
{
	atomic_fetch_add(&state->sockfd_pin, 1);
	const int sockfd = atomic_load(&state->sockfd);
	if (sockfd == -1) {
		atomic_fetch_sub(&state->sockfd_pin, 1);
		return false;
	}
	const ssize_t result = sendmsg(sockfd, &(const struct msghdr){
	   .msg_iov = (struct iovec[2]){
			{&(uint32_t){htole32(packet_len + sizeof(uint32_t))}, sizeof(uint32_t)},
			{packet, packet_len},
		},
		.msg_iovlen = 2,
	}, MSG_NOSIGNAL);
	atomic_fetch_sub(&state->sockfd_pin, 1);
	return ((size_t)result == sizeof(uint32_t) + packet_len);
}

uint32_t
IpcSocket_receive(struct IpcSocket *const state)
{
	atomic_fetch_add(&state->sockfd_pin, 1);
	const int sockfd = atomic_load(&state->sockfd);
	if (sockfd == -1) {
		atomic_fetch_sub(&state->sockfd_pin, 1);
		return 0;
	}
	if (state->head == state->buffer_len) {
		uint32_t header = 0;
		ssize_t length = recv(sockfd, &header, sizeof(header), MSG_PEEK | MSG_DONTWAIT);
		if (length < 0 && errno != EAGAIN) {
			U_LOG_IFL_E(state->log_level, "recv() failed: %s", strerror(errno));
			goto fail;
		}
		if (length < (ssize_t)sizeof(header)) {
			atomic_fetch_sub(&state->sockfd_pin, 1);
			return 0;
		}
		length = recv(sockfd, &header, sizeof(header), MSG_DONTWAIT);
		if (length != sizeof(header)) {
			U_LOG_IFL_E(state->log_level, "recv() failed: %s",
			            (length < 0) ? strerror(errno) : "bad length");
			goto fail;
		}
		header = le32toh(header) - 4;
		if (header > sizeof(state->buffer)) {
			U_LOG_IFL_E(state->log_level, "Packet too large");
			goto fail;
		}
		state->buffer_len = header;
		state->head = 0;
		state->timestamp = (int64_t)os_monotonic_get_ns();
	}
	for (ssize_t length; state->head < state->buffer_len; state->head += (size_t)length) {
		length = recv(sockfd, &state->buffer[state->head], state->buffer_len - state->head, MSG_DONTWAIT);
		if (length < 0 && errno != EAGAIN) {
			U_LOG_IFL_E(state->log_level, "recv() failed: %s", strerror(errno));
			goto fail;
		}
		if (length <= 0) {
			atomic_fetch_sub(&state->sockfd_pin, 1);
			return 0;
		}
		if (length > state->buffer_len - state->head) {
			U_LOG_IFL_E(state->log_level, "recv() returned invalid length");
			goto fail;
		}
	}
	atomic_fetch_sub(&state->sockfd_pin, 1);
	return state->buffer_len;
fail:
	atomic_fetch_sub(&state->sockfd_pin, 1);
	IpcSocket_destroy(state);
	return 0;
}
