// AT* Wi-Fi and clock commands (project brief 6.6, 6.8): AT*WIFI, AT*WIFISCAN,
// AT*WIFIJOIN, AT*WIFIFORGET, AT*WIFIIP, AT*HOSTNAME, AT*TIME, AT*TZ, AT*NTP.
//
// The Wi-Fi password is typed at a masked prompt (AT*WIFIJOIN) and is never printed:
// AT*WIFI? shows only "set" / "not set" (Q46). A join or a scan sends its result later,
// so the other port keeps working meanwhile; any key cancels it.
//
// No Arduino dependencies: unit-tested on the PC with a fake radio.

#pragma once

#include <cstdint>

#include "atcmd.h"
#include "netcfg.h"

namespace netcfg {

// Connects the commands to the controller and a millisecond clock. Call once, before
// any command runs. callUpFn says whether any port has a call connected: joining, scanning
// or changing the address would drop it, so those commands are refused until it ends.
void attachCommands(Net &net, uint32_t (*millisFn)(), bool (*callUpFn)() = nullptr);

// Call every loop, after Net::tick(): sends the result of a join or scan that finished.
void pollCommands();
void detachCommands(atcmd::Port &port);

atcmd::Reply cmdWifi(atcmd::Port &port, const char *args);
atcmd::Reply cmdWifiScan(atcmd::Port &port, const char *args);
atcmd::Reply cmdWifiJoin(atcmd::Port &port, const char *args);
atcmd::Reply cmdWifiForget(atcmd::Port &port, const char *args);
atcmd::Reply cmdWifiIp(atcmd::Port &port, const char *args);
atcmd::Reply cmdHostname(atcmd::Port &port, const char *args);
atcmd::Reply cmdTime(atcmd::Port &port, const char *args);
atcmd::Reply cmdTz(atcmd::Port &port, const char *args);
atcmd::Reply cmdNtp(atcmd::Port &port, const char *args);

}  // namespace netcfg

// The commands' rows for an AT* table (atcmd::StarCommand), in AT*HELP order.
// clang-format off
#define NETCFG_STAR_COMMANDS \
    {"WIFI", "AT*WIFI[?] | AT*WIFI=<ON|OFF>", "Wi-Fi status; switch Wi-Fi on or off", \
     netcfg::cmdWifi, false}, \
    {"WIFISCAN", "AT*WIFISCAN", "list the Wi-Fi networks in range", netcfg::cmdWifiScan, \
     false}, \
    {"WIFIJOIN", "AT*WIFIJOIN=<n|\"name\">", "join a network (asks for the password)", \
     netcfg::cmdWifiJoin, false}, \
    {"WIFIFORGET", "AT*WIFIFORGET", "forget the network and its password", \
     netcfg::cmdWifiForget, false}, \
    {"WIFIIP", "AT*WIFIIP=<DHCP|ip,mask,gateway[,dns]>", "address from DHCP, or static", \
     netcfg::cmdWifiIp, false}, \
    {"HOSTNAME", "AT*HOSTNAME=<name>", "network name (<name>.local)", netcfg::cmdHostname, \
     false}, \
    {"TIME", "AT*TIME", "date and time (from NTP)", netcfg::cmdTime, false}, \
    {"TZ", "AT*TZ=<zone|POSIX rule>", "time zone (AT*TZ=? lists the zones)", netcfg::cmdTz, \
     false}, \
    {"NTP", "AT*NTP=<server>", "time server", netcfg::cmdNtp, false}
// clang-format on
