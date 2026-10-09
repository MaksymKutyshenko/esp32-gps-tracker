#include "App.hpp"

extern "C" void app_main()
{
    // The FreeRTOS tasks started by run() use the app for their whole
    // lifetime, and app_main's own stack frame dies when it returns — so the
    // app lives in static storage.
    static App app;
    app.run();
}
