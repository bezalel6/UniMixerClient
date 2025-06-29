#include <cstdint>
#include <cstring>

// Basic hash utilities (same as before)
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

// Helper to hash individual members
template <typename T>
uint32_t hashMember(const T& member) {
    if constexpr (std::is_base_of_v<Hashable, T>) {
        return member.hash();
    } else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
        return Hash::fnv1a(member, strlen(member));
    } else if constexpr (std::is_array_v<T> && std::is_same_v<std::remove_extent_t<T>, char>) {
        return Hash::fnv1a(member, strlen(member));
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

// Alternative macro that handles the Hashable inheritance too
#define HASHABLE_STRUCT(StructName, ...)       \
    struct StructName : public Hashable {      \
        __VA_ARGS__                            \
        uint32_t hash() const override {       \
            return combineHashes(__VA_ARGS__); \
        }                                      \
    }
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

    bool hasChanged() const {
        uint32_t newHash = computeHash();
        bool changed = (newHash != cachedHash || !hashValid);
        if (changed) {
            cachedHash = newHash;
            hashValid = true;
        }
        return changed;
    }

    virtual ~Hashable() = default;
};

// Updated macro
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
