#ifndef DT_PS4_BLUETOOTH_PROBE_H
#define DT_PS4_BLUETOOTH_PROBE_H
#include <stdint.h>
/* Reconnaissance only: never initializes HID, opens a radio device, binds,
 * connects, registers callbacks, changes pairing, or writes kernel memory.
 * Zero means inspection completed, NOT that Bluetooth/DS3 is supported. */
int dt_ps4_bluetooth_probe(void);
/* Separate experiment: opens /dev/bt, subscribes to HID events, registers Sony
 * DS3 only, listens for 10s, unregisters and closes. No pairing/host writes. */
int dt_ps4_bluetooth_native_probe(void);
/* Separate comparison with the existing VDA temporary credential profile;
 * snapshots and restores authid/caps, including partial failure paths. */
int dt_ps4_bluetooth_privileged_probe(void);
/* Firmware-specific read-only RX code excerpts around the observed driver
 * error. Validates ELF ranges; excludes writable segments and runtime data. */
int dt_ps4_bluetooth_code_probe(void);
/* Read-only native property query; values never persisted or logged. */
int dt_ps4_bluetooth_host_probe(void);
/* Eight-byte little-endian local address; never log or persist it. */
int dt_ps4_bluetooth_local_address(uint8_t out[8]);
int dt_ps4_bluetooth_pairing_probe(void);
int dt_ps4_bluetooth_pairing_restore(void);
/* Read-only metadata for the known DS3 only, firmware13.52 guarded. */
int dt_ps4_bluetooth_cache_probe(void);
/* Minimal temporary event-subscription capability; snapshot/restore verified. */
int dt_ps4_bluetooth_events_probe(void);
/* Read-only two-bit scan field, exact firmware/predicate guarded. */
int dt_ps4_bluetooth_scan_state(uint8_t *out);
/* Console-private identity in memory only, for event correlation; never log. */
int dt_ps4_bluetooth_known_peer(uint64_t *out);
/* Read-only connection/channel flags for a correlated DS3; no identifiers. */
int dt_ps4_bluetooth_link_probe(uint64_t peer);
/* 1 present, 0 absent, -1 inspection failure. */
int dt_ps4_bluetooth_link_present(uint64_t peer);
#endif
