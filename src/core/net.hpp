#pragma once

/**
 * @file net.hpp
 * @brief The loopback TCP channel between the plugin and the minihost
 * log lines up, commands down
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string>

namespace rv::net {

#ifdef _WIN32
typedef SOCKET sock_t;
constexpr sock_t BADSOCK = INVALID_SOCKET;
#else
typedef int sock_t;
constexpr sock_t BADSOCK = -1;
#endif

/**
 * @brief On Windows, initialize the networking library (call before anything
 * else)
 */
inline bool init() {
#ifdef _WIN32
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
	return true;
#endif
}

/**
 * @brief Undoes init()
 */
inline void term() {
#ifdef _WIN32
	WSACleanup();
#endif
}

inline void close_sock(sock_t s) {
	if (s == BADSOCK) return;
#ifdef _WIN32
	closesocket(s);
#else
	::close(s);
#endif
}

inline bool set_nonblocking(sock_t s) {
#ifdef _WIN32
	u_long on = 1;
	return ioctlsocket(s, FIONBIO, &on) == 0;
#else
	const int fl = fcntl(s, F_GETFL, 0);
	return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

/**
 * @brief On MacOS, Ensures that writes to a closed peer fail instead of SIGPIPE
 * (that would kill IDA)
 */
inline void no_sigpipe(sock_t s) {
#ifdef SO_NOSIGPIPE // macOS
	int on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&on, sizeof(on));
#else
	(void)s;
#endif
}

/**
 * @brief set up TCP_NODELAY for sending lines instantly
 */
inline void no_delay(sock_t s) {
	int on = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
}

/**
 * @brief Sets up a non-blocking loopback listener at localhost on port selected
 * by the OS
 */
inline sock_t listen_loopback(int* port) {
	sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == BADSOCK) return BADSOCK;

	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = 0;

	socklen_t len = sizeof(a);
	if (bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 1) != 0 ||
		getsockname(s, (sockaddr*)&a, &len) != 0 || !set_nonblocking(s)) {
		close_sock(s);
		return BADSOCK;
	}

	*port = ntohs(a.sin_port);
	return s;
}

/**
 * @brief A connection waiting on @p listener, made non-blocking, BADSOCK if
 * none
 */
inline sock_t accept_pending(sock_t listener) {
	sock_t c = accept(listener, nullptr, nullptr);
	if (c == BADSOCK) return BADSOCK;

	set_nonblocking(c);
	no_sigpipe(c);
	no_delay(c);

	return c;
}

/**
 * @brief A blocking connection to the specific host and port
 *
 * @param host the target host (dotted IPv4 e.g. "127.0.0.1")
 * @param port the target port
 */
inline sock_t connect_to(const char* host, int port) {
	sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == BADSOCK) return BADSOCK;

	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(uint16_t(port));

	if (inet_pton(AF_INET, host, &a.sin_addr) != 1 ||
		connect(s, (sockaddr*)&a, sizeof(a)) != 0) {
		close_sock(s);
		return BADSOCK;
	}

	no_sigpipe(s);
	no_delay(s);
	return s;
}

enum : int {
	CLOSED = 0,		  ///< The peer closed the connection
	WOULD_BLOCK = -1, ///< Nothing to read yet (non-blocking)
	ERROR = -2,		  ///< Error
};

/**
 * Read what's at the socket
 * @return The byte count, or a problem code
 */
inline int recv_some(sock_t s, char* buf, size_t size) {
	const auto n = recv(s, buf, int(size), 0);
	if (n > 0) return int(n);

	if (n == 0) return CLOSED;

#ifdef _WIN32
	return WSAGetLastError() == WSAEWOULDBLOCK ? WOULD_BLOCK : ERROR;
#else
	return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			   ? WOULD_BLOCK
			   : ERROR;
#endif
}

/** @brief Sends all of @p data, waiting while the socket's buffer is full */
inline bool send_all(sock_t s, const char* data, size_t size) {

#if defined(MSG_NOSIGNAL) // Linux: no SIGPIPE
	const int flags = MSG_NOSIGNAL;
#else
	const int flags = 0;
#endif
	while (size > 0) {
		const auto n = send(s, data, int(size), flags);
		if (n > 0) {
			data += n;
			size -= size_t(n);
			continue;
		}

#ifdef _WIN32
		if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
		Sleep(1);
#else
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return false;

		usleep(1000);
#endif
	}

	return true;
}

/**
 * @brief Moves the complete lines of @p buf to @p fn, keeping a partial one
 */
template <class F> inline void take_lines(std::string& buf, F&& fn) {
	size_t start = 0;

	for (size_t nl; (nl = buf.find('\n', start)) != std::string::npos;) {
		size_t end = nl;
		if (end > start && buf[end - 1] == '\r') end--;

		fn(buf.substr(start, end - start));
		start = nl + 1;
	}

	buf.erase(0, start);
}

} // namespace rv::net
