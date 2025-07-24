#include "../../../../logo/LogoManager.h"
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

// Configuration
#define MAX_FILENAME_LENGTH 64
#define CACHE_SIZE 128  // Maximum cached entries

// Optimized cache structure
struct LogoCache {
    std::vector<String> allLogos;
    std::vector<String> filteredLogos;
    String lastFilter;
    bool cacheValid = false;
    unsigned long lastUpdateTime = 0;
    static constexpr unsigned long CACHE_LIFETIME_MS = 5000;  // 5 seconds
    
    void invalidate() {
        cacheValid = false;
        allLogos.clear();
        filteredLogos.clear();
        lastFilter.clear();
    }
    
    bool isExpired() const {
        return (millis() - lastUpdateTime) > CACHE_LIFETIME_MS;
    }
    
    void updateFilter(const String& filter) {
        if (filter != lastFilter || isExpired()) {
            lastFilter = filter;
            filteredLogos.clear();
            
            if (filter.isEmpty()) {
                filteredLogos = allLogos;
            } else {
                // Case-insensitive filtering
                String lowerFilter = filter;
                lowerFilter.toLowerCase();
                
                filteredLogos.reserve(allLogos.size());
                for (const auto& logo : allLogos) {
                    String lowerLogo = logo;
                    lowerLogo.toLowerCase();
                    if (lowerLogo.indexOf(lowerFilter) >= 0) {
                        filteredLogos.push_back(logo);
                    }
                }
                filteredLogos.shrink_to_fit();
            }
        }
    }
};

// Global cache instance
static LogoCache g_logoCache;

// Thread-safe string buffer pool for better memory management
class StringBufferPool {
private:
    static constexpr size_t POOL_SIZE = 12;  // LOGOS_PER_PAGE * 2
    struct Buffer {
        char data[MAX_FILENAME_LENGTH];
        bool inUse = false;
    };
    Buffer buffers[POOL_SIZE];
    
public:
    char* acquire() {
        for (auto& buffer : buffers) {
            if (!buffer.inUse) {
                buffer.inUse = true;
                buffer.data[0] = '\0';
                return buffer.data;
            }
        }
        return nullptr;  // Pool exhausted
    }
    
    void release(char* ptr) {
        if (!ptr) return;
        for (auto& buffer : buffers) {
            if (buffer.data == ptr) {
                buffer.inUse = false;
                return;
            }
        }
    }
    
    void releaseAll() {
        for (auto& buffer : buffers) {
            buffer.inUse = false;
        }
    }
};

static StringBufferPool g_stringPool;

extern "C" {

/**
 * @brief Scan logos and update cache
 */
void logo_browser_scan_logos(void) {
    // Force SimpleLogoManager to scan
    AssetManagement::LogoManager::getInstance().scanLogosOnce();
    
    // Get all logos
    int totalCount = AssetManagement::LogoManager::getInstance().getTotalLogoCount();
    
    // Update cache
    g_logoCache.invalidate();
    g_logoCache.allLogos.reserve(totalCount);
    
    // Batch fetch all logos efficiently
    int pageSize = 50;  // Fetch in chunks
    for (int page = 0; page * pageSize < totalCount; page++) {
        auto logos = AssetManagement::LogoManager::getInstance().getPagedLogos(page, pageSize);
        for (const auto& logo : logos) {
            g_logoCache.allLogos.push_back(logo);
            if (g_logoCache.allLogos.size() >= CACHE_SIZE) {
                break;  // Limit cache size
            }
        }
    }
    
    g_logoCache.filteredLogos = g_logoCache.allLogos;
    g_logoCache.cacheValid = true;
    g_logoCache.lastUpdateTime = millis();
}

/**
 * @brief Get total number of logos (cached)
 */
int logo_browser_get_total_logos(void) {
    if (!g_logoCache.cacheValid || g_logoCache.isExpired()) {
        logo_browser_scan_logos();
    }
    return g_logoCache.allLogos.size();
}

/**
 * @brief Get paged logos with optimized memory usage
 */
void logo_browser_get_paged_logos(int pageIndex, int itemsPerPage, char** paths, int* count) {
    if (!paths || !count) return;
    
    *count = 0;
    
    if (!g_logoCache.cacheValid || g_logoCache.isExpired()) {
        logo_browser_scan_logos();
    }
    
    const auto& logos = g_logoCache.allLogos;
    int startIdx = pageIndex * itemsPerPage;
    int endIdx = std::min(startIdx + itemsPerPage, static_cast<int>(logos.size()));
    
    for (int i = startIdx; i < endIdx && i - startIdx < itemsPerPage; i++) {
        if (paths[i - startIdx]) {
            strncpy(paths[i - startIdx], logos[i].c_str(), MAX_FILENAME_LENGTH - 1);
            paths[i - startIdx][MAX_FILENAME_LENGTH - 1] = '\0';
            (*count)++;
        }
    }
}

/**
 * @brief Convert path to LVGL format
 */
void logo_browser_get_lvgl_path(const char* path, char* lvglPath, size_t maxLen) {
    if (!path || !lvglPath) return;
    
    // Use SimpleLogoManager for consistency
    String result = AssetManagement::LogoManager::getInstance().getLogoLVGLPath(String(path));
    strncpy(lvglPath, result.c_str(), maxLen - 1);
    lvglPath[maxLen - 1] = '\0';
}

/**
 * @brief Get filtered logo count
 */
int logo_browser_get_filtered_total_logos(const char* filter) {
    if (!filter || strlen(filter) == 0) {
        return logo_browser_get_total_logos();
    }
    
    if (!g_logoCache.cacheValid || g_logoCache.isExpired()) {
        logo_browser_scan_logos();
    }
    
    g_logoCache.updateFilter(String(filter));
    return g_logoCache.filteredLogos.size();
}

/**
 * @brief Get filtered paged logos
 */
void logo_browser_get_filtered_paged_logos(const char* filter, int pageIndex, int itemsPerPage, 
                                          char** paths, int* count) {
    if (!paths || !count) return;
    
    *count = 0;
    
    if (!filter || strlen(filter) == 0) {
        logo_browser_get_paged_logos(pageIndex, itemsPerPage, paths, count);
        return;
    }
    
    if (!g_logoCache.cacheValid || g_logoCache.isExpired()) {
        logo_browser_scan_logos();
    }
    
    g_logoCache.updateFilter(String(filter));
    const auto& logos = g_logoCache.filteredLogos;
    
    int startIdx = pageIndex * itemsPerPage;
    int endIdx = std::min(startIdx + itemsPerPage, static_cast<int>(logos.size()));
    
    for (int i = startIdx; i < endIdx && i - startIdx < itemsPerPage; i++) {
        if (paths[i - startIdx]) {
            strncpy(paths[i - startIdx], logos[i].c_str(), MAX_FILENAME_LENGTH - 1);
            paths[i - startIdx][MAX_FILENAME_LENGTH - 1] = '\0';
            (*count)++;
        }
    }
}

/**
 * @brief Force cache refresh (optional utility)
 */
void logo_browser_refresh_cache(void) {
    g_logoCache.invalidate();
    logo_browser_scan_logos();
}

/**
 * @brief Clear cache to free memory (optional utility)
 */
void logo_browser_clear_cache(void) {
    g_logoCache.invalidate();
    g_stringPool.releaseAll();
}

} // extern "C"