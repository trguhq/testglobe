/* System 7 application resources for the MPW QuickDraw build. */
#define SystemSevenOrLater 1
#include <Types.r>

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
    2048 * 1024,
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
