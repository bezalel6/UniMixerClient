SET(SOURCES components/ui_comp_btn.c
    components/ui_comp_volumeslider.c
    components/ui_comp.c
    screens/ui_screenMain.c
    screens/ui_screenOTA.c
    screens/ui_screenDebug.c
    screens/ui_screenFileExplorer.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_2039083_png.c)

add_library(ui ${SOURCES})
