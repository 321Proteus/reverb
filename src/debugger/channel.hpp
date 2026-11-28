#pragma once
// net.hpp first: winsock2.h must come before windows.h
#include "core/net.hpp"
#include <pro.h>

/**
 * @file channel.hpp
 * @brief The plugin's end of the channel to reverb-minihost (see
 * src/core/net.hpp): its log comes up into the Output window, commands go
 * down
 *
 * Locally, the plugin listens on loopback all session and the minihost
 * connects (--connect). With a remote target, the minihost listens there
 * (--listen) and the plugin connects to it. When the plugin can't listen, the
 * minihost writes its log to a file that is followed instead.
 */
namespace rv::debug {

/** @brief Starts listening on loopback; false if it can't */
bool channel_init();

/** @brief Undoes channel_init(), and ends a run */
void channel_term();

/** @brief The loopback port the minihost connects to, 0 if not listening */
int channel_port();

/**
 * @brief The log file a run follows instead of the channel; empty for none
 */
void set_log_file(const qstring& path);

/**
 * @brief Follows a run that has just started
 *
 * @param remote the remote minihost to connect to, or null to wait for it to
 * connect here
 */
void begin_run(const sockaddr_in* remote);

/** @brief Stops following the run, printing what's left */
void end_run();

/** @brief True while a minihost is connected */
bool minihost_connected();

/**
 * @brief Sends @p command to the minihost
 *
 * @param command the command to send (e.g. info, play, blocks N, quit)
 * @return false if none is connected
 */
bool send_command(const char* command);

} // namespace rv::debug
