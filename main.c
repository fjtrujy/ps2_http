// Copyright (c) 2023 Ty (Fobes) Lamontagne
// Example of a simple HTTP server using Mongoose and the ps2_drivers library.
// Licensed under the GPL v3 license.
//
// Requires a PS2 with a network adapter. Files are served from the directory
// the ELF was booted from (HDD partition / USB / host: under PCSX2 / etc.).
// This example uses DHCP on the EE-side path and a static IP on the IOP-side
// path (the IOP stack does not support DHCP).
//
// Network layer:
//   By default lwIP runs on the EE (ps2_eeip_driver, statically linked).
//   Define USE_IOP_NETWORK below to run lwIP on the IOP via ps2ip-nm.irx +
//   ps2ips.irx, with the EE talking to it through SIF RPC.
// #define USE_IOP_NETWORK

#define LIBCGLUE_SYS_SOCKET_ALIASES 1

#include "mongoose.h"

#include <debug.h>
#include <delaythread.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ps2_filesystem_driver.h>
#include <ps2_network_driver.h>

// Mirror status messages to the GS debug screen AND stdout. On real hardware
// stdout goes through SIF RPC to the IOP tty; under PCSX2 it lands in the
// process's stdout, which makes log capture trivial. The leading spaces dodge
// TV overscan. Same pattern as the ps2_drivers network samples.
#define DPRINTF_PAD "       "
#define dprintf(fmt, ...) do { \
	scr_printf(DPRINTF_PAD fmt, ##__VA_ARGS__); \
	printf(fmt, ##__VA_ARGS__); \
} while (0)

// Absolute path to the directory the ELF was booted from. Captured once via
// getcwd() after init_only_boot_ps2_filesystem_driver() — on real hardware
// the cwd is device-prefixed (mass0:/foo, pfs0:/foo, mc0:/foo, ...) and
// mongoose's directory listing won't enumerate "." reliably against those,
// so we hand it the resolved absolute path instead.
static char g_root[FILENAME_MAX];

static u64 req_count = 0;

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data)
{
	if (ev != MG_EV_HTTP_MSG)
		return;

	struct mg_http_message *hm = (struct mg_http_message *)ev_data;
	dprintf("%.*s %.*s (#%llu)\n", (int)hm->method.len, hm->method.ptr,
			(int)hm->uri.len, hm->uri.ptr, ++req_count);

	if (mg_http_match_uri(hm, "/ping"))
	{
		mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "pong\n");
		return;
	}

	struct mg_http_serve_opts opts = {.root_dir = g_root};
	mg_http_serve_dir(c, hm, &opts);
}

#ifdef USE_IOP_NETWORK
// IOP-side network: lwIP runs on the IOP (ps2ip-nm.irx). DHCP is not
// supported on this path; configure a static IP, netmask, and gateway.

static const char *iopip_event_name(enum IOPIP_PROGRESS_EVENT ev)
{
	switch (ev) {
		case IOPIP_PROGRESS_SETTING_LINK_MODE:  return "setting link mode";
		case IOPIP_PROGRESS_APPLYING_IP_CONFIG: return "applying IP config";
		case IOPIP_PROGRESS_WAITING_LINK_UP:    return "waiting for link up";
		case IOPIP_PROGRESS_LINK_UP:            return "link up";
		case IOPIP_PROGRESS_READY:              return "ready";
	}
	return "?";
}

static void on_iopip_progress(enum IOPIP_PROGRESS_EVENT ev, void *user)
{
	(void)user;
	dprintf("[net] %s\n", iopip_event_name(ev));
}

static int setup_network(void)
{
	if (init_network_IOP_driver(true) != IOPIP_INIT_STATUS_OK) {
		dprintf("init_network_IOP_driver failed\n");
		return -1;
	}

	iopip_network_config_t cfg;
	iopip_network_config_default(&cfg);
	cfg.on_progress = on_iopip_progress;
	cfg.timeout_seconds = 30;
	// Adjust these for your network.
	cfg.ip      = IOPIP_ADDR(192, 168, 1, 10);
	cfg.netmask = IOPIP_ADDR(255, 255, 255, 0);
	cfg.gateway = IOPIP_ADDR(192, 168, 1, 1);

	if (configure_iopip_network(&cfg) != IOPIP_NET_STATUS_OK) {
		dprintf("configure_iopip_network failed\n");
		return -1;
	}

	struct ip4_addr ip;
	if (iopip_get_current_config(&ip, NULL, NULL) == 0) {
		dprintf("IP: %d.%d.%d.%d\n",
				ip4_addr1(&ip), ip4_addr2(&ip), ip4_addr3(&ip), ip4_addr4(&ip));
	}
	return 0;
}

#else
// EE-side network: lwIP linked into the EE ELF, DHCP-enabled by default.

static const char *eeip_event_name(enum EEIP_PROGRESS_EVENT ev)
{
	switch (ev) {
		case EEIP_PROGRESS_SETTING_LINK_MODE:  return "setting link mode";
		case EEIP_PROGRESS_TCPIP_INIT:         return "lwIP init";
		case EEIP_PROGRESS_APPLYING_IP_CONFIG: return "applying IP config";
		case EEIP_PROGRESS_WAITING_LINK_UP:    return "waiting for link up";
		case EEIP_PROGRESS_LINK_UP:            return "link up";
		case EEIP_PROGRESS_WAITING_DHCP:       return "waiting for DHCP lease";
		case EEIP_PROGRESS_DHCP_BOUND:         return "DHCP bound";
		case EEIP_PROGRESS_READY:              return "ready";
	}
	return "?";
}

static void on_eeip_progress(enum EEIP_PROGRESS_EVENT ev, void *user)
{
	(void)user;
	dprintf("[net] %s\n", eeip_event_name(ev));
}

static int setup_network(void)
{
	if (init_network_EE_driver(true) != EEIP_INIT_STATUS_OK) {
		dprintf("init_network_EE_driver failed\n");
		return -1;
	}

	eeip_network_config_t cfg;
	eeip_network_config_default_dhcp(&cfg);
	cfg.on_progress = on_eeip_progress;
	// 10 s per phase isn't always enough on real hardware (slow link
	// negotiation, real DHCP server round-trips); give it more headroom.
	cfg.timeout_seconds = 30;

	if (configure_eeip_network(&cfg) != EEIP_NET_STATUS_OK) {
		dprintf("configure_eeip_network failed\n");
		return -1;
	}

	struct ip4_addr ip;
	if (eeip_get_current_config(&ip, NULL, NULL) == 0) {
		dprintf("IP: %d.%d.%d.%d\n",
				ip4_addr1(&ip), ip4_addr2(&ip), ip4_addr3(&ip), ip4_addr4(&ip));
	}
	return 0;
}
#endif

static void boot_setup(void)
{
	SifInitRpc(0);
	while (!SifIopReset("", 0))
		;
	while (!SifIopSync())
		;
	SifInitRpc(0);

	// Allow ps2_drivers to load embedded IRX modules from EE memory.
	sbv_patch_enable_lmb();

	// Initialize only the filesystem driver for the booted device
	// (HDD partition, mass:, host:, etc.). For HDD boots this also
	// auto-mounts the active partition under pfs and chdirs into it.
	init_only_boot_ps2_filesystem_driver();

	// Capture the resolved absolute boot directory now (after the
	// HDD partition has been mounted / chdir'd into).
	if (getcwd(g_root, sizeof(g_root)) == NULL) {
		// Fallback: serve from "." if getcwd somehow fails.
		g_root[0] = '.';
		g_root[1] = '\0';
	}
	dprintf("ROOT: %s\n", g_root);

	if (setup_network() != 0) {
		dprintf("Network bring-up failed; halting.\n");
		SleepThread();
	}
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	init_scr();
	dprintf("\n\n\nInitializing PS2 HTTP server...\n");

	boot_setup();

	dprintf("Starting mongoose version %s\n", MG_VERSION);

	// Mute mongoose's internal logger: its default sink is putchar -> stdout.
	// On a USB boot with no consumer the IOP tty buffer eventually fills and
	// any MG_DEBUG / MG_ERROR call would freeze the poll loop.
	mg_log_set(MG_LL_NONE);

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	struct mg_connection *lc = mg_http_listen(&mgr, "http://0.0.0.0:80", fn, &mgr);
	if (lc == NULL) {
		dprintf("mg_http_listen failed\n");
		SleepThread();
	}
	dprintf("listening on :80 (fd=%ld)\n", (long)(intptr_t)lc->fd);

	// Poll with timeout 0: lwip_select scans non-blocking and returns
	// immediately. Yield 5 ms between iterations so we don't burn CPU.
	for (;;) {
		mg_mgr_poll(&mgr, 0);
		DelayThread(5 * 1000);
	}

	mg_mgr_free(&mgr);
	return 0;
}
