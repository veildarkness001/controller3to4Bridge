#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include "bluetooth_native_abi.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/event.h>
#include <time.h>
#include <unistd.h>

#ifndef DT_BT_LISTEN_SECONDS
#define DT_BT_LISTEN_SECONDS 10u
#endif
int dt_ps4_bluetooth_native_probe(void)
{
    int fd = open("/dev/bt", O_RDWR | O_NONBLOCK);
    dt_log("BT native open=%d errno=%d\n", fd >= 0, fd < 0 ? errno : 0);
    if (fd < 0) return 1;
    int status = 1, registered = 0, queue = -1;
#ifdef DT_BT_ACTIVATE_REPORTS
    DtBtReportProbe report_probe={0};
#endif
#ifdef DT_BT_PROFILE_WINDOW
    DtBtProfileWindow profile_window={0};
#endif
#ifdef DT_BT_OPEN_PAIRING
    DtBtScanWindow scan_window={0};
#endif
#ifdef DT_BT_ALL_EVENTS
    uint64_t mask = UINT64_C(0x1ffffffff);
#else
    uint64_t mask = DT_BT_HID_EVENT_MASK;
#endif
#ifdef DT_BT_KNOWN_EVENTS
    uint64_t known_peer=0;
    if(dt_ps4_bluetooth_known_peer(&known_peer)!=0) goto done;
    unsigned known_events=0, known_types[33]={0};
#endif
    ssize_t written = write(fd, &mask, sizeof(mask));
    dt_log("BT native subscribe bytes=%lld errno=%d\n", (long long)written, written < 0 ? errno : 0);
    if (written != sizeof(mask)) goto done;
    DtBtNativeRegistration request = {.vendor=0x054c, .product=0x0268};
    int restored=1;
#ifdef DT_BT_ADMISSION_PROBE
    int ret = dt_bt_admitted_ioctl(fd, DT_BT_REGISTER_DS3, &request, &restored);
#else
    int ret = ioctl(fd, DT_BT_REGISTER_DS3, &request);
#endif
    dt_log("BT native register DS3 result=0x%08x errno=%d\n", (uint32_t)ret, ret < 0 ? errno : 0);
    if (!restored) status=3;
    if (ret != 0) goto done;
    registered = 1;
    if (!restored) goto done;
#ifdef DT_BT_OPEN_PAIRING
    if(dt_bt_scan_window_begin(fd,&scan_window)!=0) goto done;
#endif
#ifdef DT_BT_PROFILE_WINDOW
    int profile_result=dt_bt_profile_window_begin(&request,&profile_window);
    if(profile_result!=0) { if(profile_result==-2) status=3; goto done; }
#endif
    uint64_t start = dt_now_us();
    if (!start) goto done;
    queue=kqueue();
    if(queue<0) { dt_log("BT native kqueue failed errno=%d\n",errno); goto done; }
    struct kevent change;
    EV_SET(&change,fd,EVFILT_READ,EV_ADD,0,0,NULL);
    if(kevent(queue,&change,1,NULL,0,NULL)!=0) {
        dt_log("BT native read readiness registration failed errno=%d\n",errno); goto done;
    }
#ifdef DT_BT_CACHE_AROUND_LISTEN
    dt_log("BT native cache before listening result=%d\n",dt_ps4_bluetooth_cache_probe());
#endif
    unsigned events = 0, batches = 0, waits = 0;
#ifdef DT_BT_CACHE_ON_EVENT
    int cache_checked=0;
#endif
    status = 0;
    dt_log("BT native listening %us; no host-address/pairing writes\n",DT_BT_LISTEN_SECONDS);
    while (1) {
        uint64_t now = dt_now_us();
        if (!now || now < start) { status=1; break; }
        if (now-start >= (uint64_t)DT_BT_LISTEN_SECONDS*1000000u) break;
        /* Match the firmware HID library's EVFILT_READ subscription. O_NONBLOCK
         * alone is not evidence that a driver read will return immediately.
         * Each wait is capped at 10ms; check the total deadline before retrying. */
        struct kevent ready;
        struct timespec timeout={.tv_sec=0,.tv_nsec=10000000};
        int count_ready=kevent(queue,NULL,0,&ready,1,&timeout);
        ++waits;
        if(count_ready<0 && errno==EINTR) continue;
        if(count_ready==0) continue;
        if(count_ready!=1 || ready.ident!=(uintptr_t)fd || ready.filter!=EVFILT_READ ||
           (ready.flags&(EV_ERROR|EV_EOF))) {
            dt_log("BT native invalid/error readiness count=%d\n",count_ready);
            if(count_ready==1) dt_log("BT native readiness fd_matches=%d filter=%d flags=0x%x data=%lld\n",
                ready.ident==(uintptr_t)fd,ready.filter,ready.flags,(long long)ready.data);
            status=1; break;
        }
        if(events<16) dt_log("BT native readiness flags=0x%x data=%lld\n",ready.flags,(long long)ready.data);
        DtBtNativeEvent records[16];
        ssize_t size = read(fd, records, sizeof(records));
        if (size < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                dt_log("BT native event read error=%lld errno=%d\n", (long long)size, errno);
                status=1; break;
            }
        } else if (size > 0) {
            if ((size_t)size > sizeof(records) || size % sizeof(records[0])) {
                dt_log("BT native malformed event bytes=%lld\n", (long long)size);
                status=1; break;
            }
            ++batches;
#ifdef DT_BT_CACHE_ON_EVENT
            if(!cache_checked) {
                cache_checked=1;
                int cache_result=dt_ps4_bluetooth_cache_probe();
                dt_log("BT native matching cache inspection result=%d\n",cache_result);
            }
#endif
            unsigned count = (unsigned)size / sizeof(records[0]);
            for (unsigned i=0; i<count; ++i) {
                ++events;
#ifdef DT_BT_ACTIVATE_REPORTS
                dt_bt_report_probe_event(fd,known_peer,&records[i],&report_probe);
#endif
#ifdef DT_BT_KNOWN_EVENTS
                int known=(records[i].peer_token&UINT64_C(0xffffffffffff))==known_peer;
                if(known) {
                    ++known_events;
                    if(records[i].type<33) ++known_types[records[i].type];
                    if(known_events<=32) dt_log("BT known DS3 event type=0x%02x status=0x%02x argument=0x%08x\n",
                        records[i].type,records[i].status,records[i].vendor_product);
#ifdef DT_BT_LINK_SNAPSHOT
                    if(known_events<=32) dt_log("BT DS3 link inspection result=%d elapsed_ms=%llu\n",
                        dt_ps4_bluetooth_link_probe(known_peer),(unsigned long long)((dt_now_us()-start)/1000));
#endif
                }
#endif
                if (events <= 16) dt_log("BT native event type=0x%02x status=0x%02x\n",
                                          records[i].type, records[i].status);
                if (events<=16 && records[i].type<0x18)
                    dt_log("BT native event argument=0x%08x\n",records[i].vendor_product);
                if (events<=16 && records[i].type==0x18)
                    dt_log("BT native connection sony_ds3=%d token_present=%d\n",
                           records[i].vendor_product==UINT32_C(0x054c0268),records[i].peer_token!=0);
            }
        }
        usleep(10000); /* Do not spin on an empty/nonblocking stream. */
    }
#ifdef DT_BT_CACHE_AROUND_LISTEN
    dt_log("BT native cache after listening result=%d\n",dt_ps4_bluetooth_cache_probe());
#endif
#ifdef DT_BT_KNOWN_EVENTS
    dt_log("BT known DS3 summary events=%u\n",known_events);
    for(unsigned i=0;i<33;++i) if(known_types[i]) dt_log("BT known DS3 event type=0x%02x count=%u\n",i,known_types[i]);
#endif
    dt_log("BT native summary events=%u batches=%u waits=%u; no radio input validated\n", events, batches, waits);
done:
#ifdef DT_BT_ACTIVATE_REPORTS
    dt_log("BT DS3 report summary attempts=%u completions=%u valid=%u invalid=%u changes=%u errors=%u\n",
        report_probe.attempts,report_probe.completions,report_probe.valid,report_probe.invalid,report_probe.changes,report_probe.errors);
#endif
#if defined(DT_BT_IGNORE_NATIVE_DS3) && defined(DT_BT_KNOWN_EVENTS)
    if(registered && known_peer) {
        int present=dt_ps4_bluetooth_link_present(known_peer);
        if(present==1) {
            uint64_t peer=known_peer;
            int result=ioctl(fd,DT_BT_DISCONNECT,&peer);
            dt_log("BT DS3 cleanup disconnect result=0x%08x\n",(uint32_t)result);
            /* Accepted is not disconnected: verify disappearance, bounded3s. */
            for(unsigned i=0;i<150 && present==1;++i) {
                usleep(20000);present=dt_ps4_bluetooth_link_present(known_peer);
            }
        }
        dt_log("BT DS3 cleanup link absent=%d inspection_ok=%d\n",present==0,present>=0);
        if(present!=0 && status!=3) status=2;
    }
#endif
#ifdef DT_BT_OPEN_PAIRING
    if(dt_bt_scan_window_end(fd,&scan_window)!=0) status=3;
#endif
    if(queue>=0) {
        int result=close(queue); dt_log("BT native kqueue close=%d\n",result);
        if(result!=0 && status!=3) status=2;
    }
    if (registered) {
#ifdef DT_BT_ADMISSION_PROBE
        int ret = dt_bt_admitted_ioctl(fd, DT_BT_UNREGISTER_DS3, &request, &restored);
#else
        int ret = ioctl(fd, DT_BT_UNREGISTER_DS3, &request);
#endif
        dt_log("BT native unregister DS3 result=0x%08x errno=%d\n", (uint32_t)ret, ret < 0 ? errno : 0);
        if (ret != 0 && status != 3) status = 2;
        if (!restored) status = 3;
    }
    int closed = close(fd);
    dt_log("BT native close=%d errno=%d\n", closed, closed < 0 ? errno : 0);
    if (closed != 0 && status != 3) status = 2;
#ifdef DT_BT_PROFILE_WINDOW
    if(dt_bt_profile_window_end(&profile_window)!=0) status=3;
#endif
    return status;
}
