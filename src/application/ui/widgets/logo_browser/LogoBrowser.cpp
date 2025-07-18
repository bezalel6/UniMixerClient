#include "../../../../logo/SimpleLogoManager.h"
#include <vector>
#include <string>
#include <cstring>

// Define the constant here since it's needed
#define MAX_FILENAME_LENGTH 64

// Static storage for paged logos
static std::vector<String> cachedPagedLogos;

extern "C" {

int logo_browser_get_total_logos(void) {
    return SimpleLogoManager::getInstance().getTotalLogoCount();
}

void logo_browser_get_paged_logos(int pageIndex, int itemsPerPage, char** paths, int* count) {
    if (!paths || !count) return;
    
    cachedPagedLogos = SimpleLogoManager::getInstance().getPagedLogos(pageIndex, itemsPerPage);
    *count = cachedPagedLogos.size();
    
    for (int i = 0; i < *count && i < itemsPerPage; i++) {
        if (paths[i]) {
            strncpy(paths[i], cachedPagedLogos[i].c_str(), MAX_FILENAME_LENGTH - 1);
            paths[i][MAX_FILENAME_LENGTH - 1] = '\0';
        }
    }
}

void logo_browser_get_lvgl_path(const char* path, char* lvglPath, size_t maxLen) {
    if (!path || !lvglPath) return;
    
    String result = SimpleLogoManager::getInstance().getLogoLVGLPath(String(path));
    strncpy(lvglPath, result.c_str(), maxLen - 1);
    lvglPath[maxLen - 1] = '\0';
}

void logo_browser_scan_logos(void) {
    SimpleLogoManager::getInstance().scanLogosOnce();
}

} // extern "C"