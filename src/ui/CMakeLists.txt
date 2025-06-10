SET(SOURCES screens/ui_screenMain.c
    screens/ui_screenOTA.c
    screens/ui_screenDebug.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c)

add_library(ui ${SOURCES})
