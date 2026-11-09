/* System 7 application resources for MPW and Retro68 QuickDraw builds. */
#define SystemSevenOrLater 1
#ifdef QD_MULTIVERSAL
#include "Multiverse.r"
#else
#include <Types.r>
#endif
#include "earthres.h"

#ifdef QD_MULTIVERSAL
/* The open interfaces use the original Process Manager spelling. */
#define ignoreAppDiedEvents ignoreChildDiedEvents
#endif

/* Keep code/data pointer values clean in either 24- or 32-bit addressing.
 * A failed optional GWorld allocation falls back to drawing in the window. */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreAppDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    8192 * 1024,
    512 * 1024
};

/* qd_error uses ParamText followed by StopAlert, available on System 7. */
resource 'ALRT' (128, purgeable) {
    {80, 80, 270, 500},
    128,
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1
    },
    centerMainScreen
};

resource 'DITL' (128, purgeable) {
    {
        {145, 330, 165, 400}, Button {enabled, "OK"};
        {15, 65, 130, 400}, StaticText {disabled, "^0"}
    }
};

/* These resources live in the heap instead of the A5-relative C globals.
 * Rez READ filenames are relative to this source file, not its -i paths. */
#ifdef QD_MULTIVERSAL
/* Retro68 Rez reads bytes with $$read, relative to the build command's
 * current directory. cross_qd.py invokes it from this source directory. */
#define QD_EARTH_RESOURCE(id, file) data 'EART' (id, purgeable) { $$read("../data/" file) }
QD_EARTH_RESOURCE(TG_EARTH_32_ID, "e32.bin");
QD_EARTH_RESOURCE(TG_EARTH_64_ID, "e64.bin");
QD_EARTH_RESOURCE(TG_EARTH_128_ID, "e128.bin");
QD_EARTH_RESOURCE(TG_EARTH_256_ID, "e256_0.bin");
QD_EARTH_RESOURCE(TG_EARTH_256_1_ID, "e256_1.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_ID, "e512_0.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_1_ID, "e512_1.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_2_ID, "e512_2.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_3_ID, "e512_3.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_4_ID, "e512_4.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_5_ID, "e512_5.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_6_ID, "e512_6.bin");
QD_EARTH_RESOURCE(TG_EARTH_512_7_ID, "e512_7.bin");
#else
read 'EART' (TG_EARTH_32_ID, purgeable) "::data:e32.bin";
read 'EART' (TG_EARTH_64_ID, purgeable) "::data:e64.bin";
read 'EART' (TG_EARTH_128_ID, purgeable) "::data:e128.bin";
read 'EART' (TG_EARTH_256_ID, purgeable) "::data:e256_0.bin";
read 'EART' (TG_EARTH_256_1_ID, purgeable) "::data:e256_1.bin";
read 'EART' (TG_EARTH_512_ID, purgeable) "::data:e512_0.bin";
read 'EART' (TG_EARTH_512_1_ID, purgeable) "::data:e512_1.bin";
read 'EART' (TG_EARTH_512_2_ID, purgeable) "::data:e512_2.bin";
read 'EART' (TG_EARTH_512_3_ID, purgeable) "::data:e512_3.bin";
read 'EART' (TG_EARTH_512_4_ID, purgeable) "::data:e512_4.bin";
read 'EART' (TG_EARTH_512_5_ID, purgeable) "::data:e512_5.bin";
read 'EART' (TG_EARTH_512_6_ID, purgeable) "::data:e512_6.bin";
read 'EART' (TG_EARTH_512_7_ID, purgeable) "::data:e512_7.bin";
#endif
