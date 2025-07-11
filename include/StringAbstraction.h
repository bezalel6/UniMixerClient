#pragma once

#include <Arduino.h>
#include <WString.h>

// =============================================================================
// STL HASH SPECIALIZATION FOR ARDUINO STRING (MUST BE EARLY)
// =============================================================================

// Provide hash specialization for Arduino String to work with std::unordered_map
// This must be defined before any STL container that uses String as a key
namespace std {
template <>
struct hash<String> {
    size_t operator()(const String& s) const noexcept {
        // Use simple djb2 hash algorithm
        size_t hash = 5381;
        for (size_t i = 0; i < s.length(); ++i) {
            hash = ((hash << 5) + hash) + s.charAt(i);
        }
        return hash;
    }
};
}  // namespace std

// =============================================================================
// STRING ABSTRACTION LAYER - Universal String Interface
// =============================================================================

namespace StringAbstraction {

// =============================================================================
// CONFIGURATION MACROS
// =============================================================================

// Define the underlying string implementation
#define STRING_IMPL_ARDUINO_STRING 1
#define STRING_IMPL_ETL_STRING 2
#define STRING_IMPL_STD_STRING 3

// Current implementation (can be changed later)
#ifndef STRING_CURRENT_IMPL
#define STRING_CURRENT_IMPL STRING_IMPL_ARDUINO_STRING
#endif

// =============================================================================
// FORWARD DECLARATIONS AND TYPE ALIASES
// =============================================================================

#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
using underlying_string = String;
using underlying_string_view = String;  // Arduino String doesn't have string_view
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
// Will be implemented later
#error "ETL String implementation not yet available"
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
#include <string>
using underlying_string = std::string;
using underlying_string_view = std::string_view;
#endif

// =============================================================================
// SAFE STRING CREATION MACROS
// =============================================================================

/**
 * Create a string from a literal - ensures proper capacity
 */
#define MAKE_STRING(literal) \
    StringAbstraction::make_string(literal)

/**
 * Create a string with specific capacity (for future ETL implementation)
 */
#define MAKE_STRING_WITH_CAPACITY(capacity, literal) \
    StringAbstraction::make_string_with_capacity<capacity>(literal)

/**
 * Create an empty string with specific capacity
 */
#define MAKE_EMPTY_STRING(capacity) \
    StringAbstraction::make_empty_string<capacity>()

// =============================================================================
// STRING OPERATION MACROS
// =============================================================================

/**
 * Safe string concatenation - prevents buffer overflow
 */
#define STRING_CONCAT(dest, src) \
    StringAbstraction::safe_concat(dest, src)

/**
 * Safe string assignment - prevents buffer overflow
 */
#define STRING_ASSIGN(dest, src) \
    StringAbstraction::safe_assign(dest, src)

/**
 * Safe string append - prevents buffer overflow
 */
#define STRING_APPEND(dest, src) \
    StringAbstraction::safe_append(dest, src)

/**
 * Convert to C-style string
 */
#define STRING_C_STR(str) \
    StringAbstraction::c_str(str)

/**
 * Get string length
 */
#define STRING_LENGTH(str) \
    StringAbstraction::length(str)

/**
 * Check if string is empty
 */
#define STRING_IS_EMPTY(str) \
    StringAbstraction::is_empty(str)

/**
 * Clear string contents
 */
#define STRING_CLEAR(str) \
    StringAbstraction::clear(str)

// =============================================================================
// UNIVERSAL STRING TYPE ALIAS
// =============================================================================

/**
 * Universal string type - can be swapped out by changing implementation
 */
using string = underlying_string;

/**
 * Universal string view type - for read-only string operations
 */
using string_view = underlying_string_view;

// =============================================================================
// TEMPLATE FUNCTIONS FOR STRING OPERATIONS
// =============================================================================

/**
 * Create a string from a literal
 */
template <size_t N>
inline string make_string(const char (&literal)[N]) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return String(literal);
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    // ETL implementation will go here
    return etl::string<N>(literal);
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return std::string(literal);
#endif
}

/**
 * Create a string from a C-style string
 */
inline string make_string(const char* cstr) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return String(cstr);
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return etl::string<256>(cstr);  // Default capacity
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return std::string(cstr);
#endif
}

/**
 * Create a string with specific capacity (for future ETL implementation)
 */
template <size_t Capacity>
inline string make_string_with_capacity(const char* literal) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return String(literal);  // Arduino String doesn't have fixed capacity
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return etl::string<Capacity>(literal);
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return std::string(literal);
#endif
}

/**
 * Create an empty string with specific capacity
 */
template <size_t Capacity = 256>
inline string make_empty_string() {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return String("");
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return etl::string<Capacity>();
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return std::string();
#endif
}

// =============================================================================
// SAFE STRING OPERATIONS
// =============================================================================

/**
 * Safe string concatenation
 */
template <typename StringType1, typename StringType2>
inline StringType1& safe_concat(StringType1& dest, const StringType2& src) {
    dest += src;
    return dest;
}

/**
 * Safe string assignment
 */
template <typename StringType1, typename StringType2>
inline StringType1& safe_assign(StringType1& dest, const StringType2& src) {
    dest = src;
    return dest;
}

/**
 * Safe string append
 */
template <typename StringType1, typename StringType2>
inline StringType1& safe_append(StringType1& dest, const StringType2& src) {
    dest += src;
    return dest;
}

/**
 * Get C-style string
 */
template <typename StringType>
inline const char* c_str(const StringType& str) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return str.c_str();
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return str.c_str();
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return str.c_str();
#endif
}

/**
 * Get string length
 */
template <typename StringType>
inline size_t length(const StringType& str) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return str.length();
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return str.length();
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return str.length();
#endif
}

/**
 * Check if string is empty
 */
template <typename StringType>
inline bool is_empty(const StringType& str) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return str.length() == 0;
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return str.empty();
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return str.empty();
#endif
}

/**
 * Clear string contents
 */
template <typename StringType>
inline void clear(StringType& str) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    str = "";
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    str.clear();
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    str.clear();
#endif
}

// =============================================================================
// CONVERSION UTILITIES
// =============================================================================

/**
 * Convert integer to string
 */
template <typename IntType>
inline string int_to_string(IntType value) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return String(value);
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return etl::to_string(value);
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return std::to_string(value);
#endif
}

/**
 * Convert float to string
 */
template <typename FloatType>
inline string float_to_string(FloatType value, int precision = 2) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return String(value, precision);
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return etl::to_string(value, etl::format_spec().precision(precision));
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return std::to_string(value);
#endif
}

/**
 * Convert boolean to string
 */
inline string bool_to_string(bool value) {
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
    return String(value ? "true" : "false");
#elif STRING_CURRENT_IMPL == STRING_IMPL_ETL_STRING
    return etl::string<6>(value ? "true" : "false");
#elif STRING_CURRENT_IMPL == STRING_IMPL_STD_STRING
    return std::string(value ? "true" : "false");
#endif
}

// =============================================================================
// DEBUGGING AND LOGGING UTILITIES
// =============================================================================

/**
 * Debug print string information
 */
template <typename StringType>
inline void debug_string_info(const StringType& str, const char* name = "string") {
    ESP_LOGD("StringAbstraction", "%s: length=%zu, content='%s'",
             name, length(str), c_str(str));
}

/**
 * Log string memory usage (for Arduino String)
 */
#if STRING_CURRENT_IMPL == STRING_IMPL_ARDUINO_STRING
template <typename StringType>
inline void log_string_memory(const StringType& str, const char* name = "string") {
    ESP_LOGD("StringAbstraction", "%s: length=%u, capacity=%u, heap_usage=%u",
             name, str.length(), str.capacity(),
             str.capacity() > 0 ? str.capacity() + 1 : 0);
}
#endif

}  // namespace StringAbstraction

// =============================================================================
// GLOBAL CONVENIENCE MACROS
// =============================================================================

// Make the string type available globally
using string = StringAbstraction::string;
using string_view = StringAbstraction::string_view;

// Convenience macros for common operations
#define STRING_EMPTY StringAbstraction::make_empty_string()
#define STRING_FROM_LITERAL(lit) StringAbstraction::make_string(lit)
#define STRING_FROM_CSTR(cstr) StringAbstraction::make_string(cstr)
//Create a string from an std::string, for edge cases where using them is obligatory no matter the STRING_CURRENT_IMPL
#define STRING_FROM_STD_STR(std_str) StringAbstraction::make_string(std_str.c_str())
#define STRING_FROM_INT(val) StringAbstraction::int_to_string(val)
#define STRING_FROM_FLOAT(val, prec) StringAbstraction::float_to_string(val, prec)
#define STRING_FROM_BOOL(val) StringAbstraction::bool_to_string(val)
