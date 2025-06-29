#include <cstdint>
#include <cstring>

class Hashable;
class Hash {
   public:
    static constexpr uint32_t FNV_OFFSET = 2166136261U;
    static constexpr uint32_t FNV_PRIME = 16777619U;

    static uint32_t fnv1a(const void* data, size_t len, uint32_t hash = FNV_OFFSET) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }

    template <typename T>
    static uint32_t of(const T& value) {
        return fnv1a(&value, sizeof(T));
    }

    static uint32_t combine(uint32_t h1, uint32_t h2) {
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

// Smart hash member - handles different types automatically
template <typename T>
uint32_t hashMember(const T& member) {
    if constexpr (std::is_base_of_v<Hashable, T>) {
        return member.hash();
    } else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
        return member ? Hash::fnv1a(member, strlen(member)) : 0;
    } else if constexpr (std::is_array_v<T> && std::is_same_v<std::remove_extent_t<T>, char>) {
        return Hash::fnv1a(member, strlen(member));
    } else if constexpr (std::is_pointer_v<T>) {
        return member ? Hash::of(*member) : 0;
    } else {
        return Hash::of(member);
    }
}

// Variadic hash combining
template <typename... Args>
uint32_t combineHashes(const Args&... args) {
    uint32_t result = Hash::FNV_OFFSET;
    ((result = Hash::combine(result, hashMember(args))), ...);
    return result;
}

// // Alternative macro that handles the Hashable inheritance too
// #define HASHABLE_STRUCT(StructName, ...)       \
//     struct StructName : public Hashable {      \
//         __VA_ARGS__                            \
//         uint32_t hash() const override {       \
//             return combineHashes(__VA_ARGS__); \
//         }                                      \
//     }

#define IS(state, flag) (((state) & (flag)) != 0)

class Hashable {
   private:
    mutable uint32_t cachedHash = 0;
    mutable bool hashValid = false;

   protected:
    virtual uint32_t computeHash() const = 0;

    // Call this when any field changes
    void invalidate() {
        hashValid = false;
    }

   public:
    uint32_t hash() const {
        if (!hashValid) {
            cachedHash = computeHash();
            hashValid = true;
        }
        return cachedHash;
    }

    virtual ~Hashable() = default;
};

// Advanced hash implementation macros

// ============================================================================
// ADVANCED MACRO UTILITIES
// ============================================================================
#ifndef ADVANCED_MACRO_UTILITIES
#define ADVANCED_MACRO_UTILITIES

// Basic hash implementation
#define IMPLEMENT_HASH(...)                 \
   protected:                               \
    uint32_t computeHash() const override { \
        return combineHashes(__VA_ARGS__);  \
    }                                       \
                                            \
   public:                                  \
    template <typename T>                   \
    void set(T& field, const T& newValue) { \
        if (field != newValue) {            \
            field = newValue;               \
            invalidate();                   \
        }                                   \
    }

// Hash comparison utilities
#define HASH_CHANGED(obj, lastHash)                 \
    ([&]() {                                        \
        uint32_t currentHash = (obj).hash();        \
        bool changed = (currentHash != (lastHash)); \
        if (changed) (lastHash) = currentHash;      \
        return changed;                             \
    }())

// Static hash tracker for change detection
#define DEFINE_HASH_TRACKER(name, obj)   \
    static uint32_t name##_lastHash = 0; \
    bool name##_changed = HASH_CHANGED(obj, name##_lastHash)

// Conditional hash update - execute action only if hash changed
#define UPDATE_IF_HASH_CHANGED(obj, lastHash, action) \
    do {                                              \
        if (HASH_CHANGED(obj, lastHash)) {            \
            action;                                   \
        }                                             \
    } while (0)

// Batch setter macros - set multiple fields and invalidate once
#define BATCH_SET_BEGIN() \
    do {                  \
        bool needsInvalidate = false;

#define BATCH_SET(field, value) \
    if ((field) != (value)) {   \
        (field) = (value);      \
        needsInvalidate = true; \
    }

#define BATCH_SET_END()                \
    if (needsInvalidate) invalidate(); \
    }                                  \
    while (0)

// ============================================================================
// ADVANCED HASH UTILITIES
// ============================================================================
// Hash equality check
template <typename T>
bool hashEquals(const T& a, const T& b) {
    static_assert(std::is_base_of_v<Hashable, T>, "Type must inherit from Hashable");
    return a.hash() == b.hash();
}

// Hash collection utilities
template <typename Container>
uint32_t hashContainer(const Container& container) {
    uint32_t result = Hash::FNV_OFFSET;
    for (const auto& item : container) {
        result = Hash::combine(result, hashMember(item));
    }
    return result;
}

// Hash-enabled struct with automatic implementation
#define HASHABLE_STRUCT(StructName, ...)                 \
    struct StructName : public Hashable {                \
        __VA_ARGS__                                      \
        IMPLEMENT_HASH(EXTRACT_FIELD_NAMES(__VA_ARGS__)) \
    }

// Bulk field setter with hash invalidation
#define SET_FIELDS(obj, ...)                           \
    do {                                               \
        bool anyChanged = false;                       \
        APPLY_FIELD_SETS(obj, anyChanged, __VA_ARGS__) \
        if (anyChanged) (obj).invalidate();            \
    } while (0)

// Thread-safe hash comparison
#define THREAD_SAFE_HASH_CHANGED(obj, lastHash, mutex) \
    ([&]() {                                           \
        std::lock_guard<std::mutex> lock(mutex);       \
        return HASH_CHANGED(obj, lastHash);            \
    }())

// Helper macros for advanced functionality
#define EXTRACT_FIELD_NAMES(...) __VA_ARGS__

#define APPLY_FIELD_SETS(obj, changed, field, value, ...) \
    if ((obj).field != (value)) {                         \
        (obj).field = (value);                            \
        changed = true;                                   \
    }                                                     \
    IF_NOT_EMPTY(__VA_ARGS__)(APPLY_FIELD_SETS(obj, changed, __VA_ARGS__))

#define IF_NOT_EMPTY(...) \
    CONCAT(IF_NOT_EMPTY_, IS_EMPTY(__VA_ARGS__))

#define IS_EMPTY(...) \
    CONCAT(IS_EMPTY_, HAS_COMMA(__VA_ARGS__))

#define HAS_COMMA(...) \
    GET_ARG_16(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0)

#define GET_ARG_16(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, ...) _16

#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define CONCAT_IMPL(a, b) a##b

#define IF_NOT_EMPTY_0(then) then
#define IF_NOT_EMPTY_1(then)

#define IS_EMPTY_0 1
#define IS_EMPTY_1 0

#endif
