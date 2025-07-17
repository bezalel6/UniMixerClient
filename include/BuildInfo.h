#pragma once

#include <stdio.h>

// Include auto-generated build information
#include "BuildInfoGenerated.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file BuildInfo.h
 * @brief Build information and version macros
 *
 * This header provides build time, date, and version information
 * that can be accessed throughout the codebase.
 *
 * Auto-generated information (git hash, branch, timestamp) is included
 * from BuildInfoGenerated.h which is created by scripts/get_build_info.py
 */

// =============================================================================
// BUILD TIME AND DATE INFORMATION
// =============================================================================

/**
 * Build date string (e.g., "Dec 25 2024")
 */
#define BUILD_DATE __DATE__

/**
 * Build time string (e.g., "12:34:56")
 */
#define BUILD_TIME __TIME__

/**
 * Combined build timestamp string
 */
#define BUILD_TIMESTAMP BUILD_DATE " " BUILD_TIME

/**
 * Build year as string
 */
#define BUILD_YEAR_STR (__DATE__ + 7)

/**
 * Get build year as a 4-digit integer
 * Note: This is a compile-time constant calculation
 */
#define BUILD_YEAR (((__DATE__[7] - '0') * 1000) + \
                    ((__DATE__[8] - '0') * 100) +  \
                    ((__DATE__[9] - '0') * 10) +   \
                    (__DATE__[10] - '0'))

// =============================================================================
// COMPILER INFORMATION
// =============================================================================

#ifdef __GNUC__
#define COMPILER_VERSION __VERSION__
#else
#define COMPILER_VERSION "Unknown"
#endif

// =============================================================================
// FIRMWARE VERSION INFORMATION
// =============================================================================

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

// Note: FIRMWARE_BUILD_NUMBER, GIT_BRANCH, and BUILD_TIMESTAMP_NUM
// are now provided by BuildInfoGenerated.h

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================

/**
 * @brief Get a formatted build information string
 * @return const char* formatted string with build info
 */
static const char* getBuildInfo() {
    static char buildInfo[256];
    snprintf(buildInfo, sizeof(buildInfo),
             "Version: %s-%s | Built: %s | Compiler: %s",
             FIRMWARE_VERSION,
             FIRMWARE_BUILD_NUMBER,
             BUILD_TIMESTAMP,
             COMPILER_VERSION);
    return buildInfo;
}

/**
 * @brief Get build timestamp as const string
 * @return const char* build timestamp
 */
static const char* getBuildTimestamp() {
    return BUILD_TIMESTAMP;
}

/**
 * @brief Get build date as const string
 * @return const char* build date
 */
static const char* getBuildDate() {
    return BUILD_DATE;
}

/**
 * @brief Get build time as const string
 * @return const char* build time
 */
static const char* getBuildTime() {
    return BUILD_TIME;
}

/**
 * @brief Get firmware version string
 * @return const char* firmware version
 */
static const char* getFirmwareVersion() {
    return FIRMWARE_VERSION;
}

/**
 * @brief Get build year as integer (compile-time only)
 * @return int build year
 */
static int getBuildYear() {
    return BUILD_YEAR;
}

/**
 * @brief Get build time in 12-hour format with AM/PM
 * @return const char* time string like "12:00 PM"
 */
static const char* getBuildTime12Hour() {
    static char time12[16];

    // Parse __TIME__ which is in format "HH:MM:SS"
    int hour = (BUILD_TIME[0] - '0') * 10 + (BUILD_TIME[1] - '0');
    int minute = (BUILD_TIME[3] - '0') * 10 + (BUILD_TIME[4] - '0');

    const char* period = (hour >= 12) ? "PM" : "AM";

    // Convert to 12-hour format
    if (hour == 0) {
        hour = 12;  // 12 AM
    } else if (hour > 12) {
        hour -= 12;  // Convert PM hours
    }

    snprintf(time12, sizeof(time12), "%d:%02d %s", hour, minute, period);
    return time12;
}

/**
 * @brief Get build date in day/month format
 * @return const char* date string like "25/12"
 */
static const char* getBuildDateDayMonth() {
    static char dayMonth[8];

    // Parse __DATE__ which is in format "Mon DD YYYY"
    // Extract day (characters 4-5, but handle single digit days)
    int day;
    if (BUILD_DATE[4] == ' ') {
        // Single digit day like "Dec  5 2024"
        day = BUILD_DATE[5] - '0';
    } else {
        // Double digit day like "Dec 25 2024"
        day = (BUILD_DATE[4] - '0') * 10 + (BUILD_DATE[5] - '0');
    }

    // Extract month from the 3-letter abbreviation
    int month;
    if (BUILD_DATE[0] == 'J' && BUILD_DATE[1] == 'a' && BUILD_DATE[2] == 'n')
        month = 1;
    else if (BUILD_DATE[0] == 'F' && BUILD_DATE[1] == 'e' && BUILD_DATE[2] == 'b')
        month = 2;
    else if (BUILD_DATE[0] == 'M' && BUILD_DATE[1] == 'a' && BUILD_DATE[2] == 'r')
        month = 3;
    else if (BUILD_DATE[0] == 'A' && BUILD_DATE[1] == 'p' && BUILD_DATE[2] == 'r')
        month = 4;
    else if (BUILD_DATE[0] == 'M' && BUILD_DATE[1] == 'a' && BUILD_DATE[2] == 'y')
        month = 5;
    else if (BUILD_DATE[0] == 'J' && BUILD_DATE[1] == 'u' && BUILD_DATE[2] == 'n')
        month = 6;
    else if (BUILD_DATE[0] == 'J' && BUILD_DATE[1] == 'u' && BUILD_DATE[2] == 'l')
        month = 7;
    else if (BUILD_DATE[0] == 'A' && BUILD_DATE[1] == 'u' && BUILD_DATE[2] == 'g')
        month = 8;
    else if (BUILD_DATE[0] == 'S' && BUILD_DATE[1] == 'e' && BUILD_DATE[2] == 'p')
        month = 9;
    else if (BUILD_DATE[0] == 'O' && BUILD_DATE[1] == 'c' && BUILD_DATE[2] == 't')
        month = 10;
    else if (BUILD_DATE[0] == 'N' && BUILD_DATE[1] == 'o' && BUILD_DATE[2] == 'v')
        month = 11;
    else if (BUILD_DATE[0] == 'D' && BUILD_DATE[1] == 'e' && BUILD_DATE[2] == 'c')
        month = 12;
    else
        month = 1;  // Default fallback

    snprintf(dayMonth, sizeof(dayMonth), "%d/%d", day, month);
    return dayMonth;
}

/**
 * @brief Get build time and date in the format "9:41 PM    12/12"
 * @return const char* formatted string with time and date separated by 4 spaces
 */
static const char* getBuildTimeAndDate() {
    static char timeAndDate[32];
    snprintf(timeAndDate, sizeof(timeAndDate), "%s    %s",
             getBuildTime12Hour(), getBuildDateDayMonth());
    return timeAndDate;
}

#ifdef __cplusplus
}
#endif
