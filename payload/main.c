#include <bldr.h>
#include <chainload.h>
#include <debug.h>
#include <mmio.h>
#include <patches.h>
#include <target.h>

/*
 * Emergency download escape hatch.
 *
 * When the device comes up in the aee_lk/abnormal-boot path, the
 * preloader's USB download mode (usbdl) is never initialized: the
 * "Starting tool handshake" call site 0x0205F31C lives on the normal
 * boot_mode==0 branch only, and the aee path branches around it to the
 * "Second Bootloader Load Failed" handler at 0x0205F4A0 which ends in
 * the emergency-download/reset sequence. On that path the second port
 * never appears, so this payload's DA window is dead -- and chainload
 * would just reboot into the same broken boot loop.
 *
 * 0x020723A0 is the preloader's own emergency-download entry: it prints
 * "emergency download mode(timeout: %ds)", programs the RGU for an
 * unconditional reset and calls mtk_arch_reset. Rebooting through it
 * lands in BROM download mode, which needs no preloader cooperation at
 * all -- that is the only usable download channel from here.
 */
static void enter_emergency_download(void)
{
    ((void (*)(uint32_t))PL_EMERGENCY_DL_FUNC)(EMERGENCY_DL_TIMEOUT_S);
    while (1)
        __asm__ volatile("wfe");
}

int main(void) {
    printf("\n");
    printf("           .--._.--.          \n");
    printf("          ( O     O )         \n");
    printf("          /   . .   \\         \n");
    printf("         .`._______.'.        \n");
    printf("        /(           )\\                              _       \n");
    printf("      _/  \\  \\   /  /  \\_           ___ _ __  _ __(_) __ _ \n");
    printf("   .~   `  \\  \\ /  /  '   ~.       / __| '_ \\| '__| |/ _` |\n");
    printf("  {    -.   \\  V  /   .-    }      \\__ \\ |_) | |  | | (_| |\n");
    printf("_ _`.    \\  |  |  |  /    .'_ _    |___/ .__/|_|  |_|\\__, |\n");
    printf(">_       _} |  |  | {_       _<         |_|           |___/ \n");
    printf(" /. - ~ ,_-'  .^.  `-_, ~ - .\\      \n");
    printf("         '-'|/   \\|`-`              \n\n");

    if (patch_apply_all() != 0) {
        printf("Patch verification failed, refusing to continue.\n");
        while (1);
    }

#ifdef OPPO_USB_ENUM_LOCK
    /* OPPO may have latched usbEnum off in an earlier boot path, which
       makes usb_connect() fail inside the handshake. Clear it. Not
       needed on Xiaomi rothko. */
    writeb(0, OPPO_USB_ENUM_LOCK);
    flush_dcache_range(OPPO_USB_ENUM_LOCK, 64);
#endif

#ifdef PL_EMERGENCY_DL_FUNC
    /*
     * Abnormal-boot (aee_lk) detection: the preloader's rgu driver
     * caches the reset status at 0x020D262C and stamps the magic
     * 0xAEEDEAD there when the previous reset was an aee/wdt abort
     * (checked at 0x02078ED8 before the composite load). In that state
     * usbdl is never initialized, so hand the device to the BROM
     * download mode instead of chainloading into a dead boot loop.
     */
    if (readl(PL_RGU_AEE_STATUS) == PL_RGU_AEE_MAGIC) {
        printf("abnormal boot (aee) detected; entering emergency download\n");
        enter_emergency_download();
    }
#endif

    printf("About to handshake...\n\n");

    int r = bldr_handshake();
    if (r == BLDR_ERR_RESTORE) {
        while (1)
            __asm__ volatile("wfe");
    }
    printf("handshake returned %d\n", r);

    /*
     * The session has restored any temporary cache/permission changes.
     * The stock bl2_ext owns its normal UFS initialization.
     */
    chainload_to_bl2();

    while (1);
}
