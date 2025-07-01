// Reflective.hpp
#pragma once

#include <string_view>
#include <tuple>
#include <stdexcept>
#include <type_traits>

namespace reflective {

// Helper for static string-keyed field list
#define REFLECT_DECLARE(...)                    \
    using Self = std::decay_t<decltype(*this)>; \
    static constexpr auto _reflect_fields() {   \
        return std::make_tuple(__VA_ARGS__);    \
    }

#define REFLECT_FIELD(field) \
    std::pair<std::string_view, decltype(&Self::field)> { #field, &Self::field }

// Main template for reflection access

namespace detail {
template <typename Tuple, std::size_t... I>
constexpr auto find_field(std::string_view name, Tuple&& fields, std::index_sequence<I...>) {
    using field_t = std::remove_cv_t<std::remove_reference_t<decltype(std::get<0>(fields))>>;
    field_t const* match = nullptr;
    ((std::get<I>(fields).first == name ? (match = &std::get<I>(fields), true) : false) || ...);
    return match;
}
}  // namespace detail

template <typename T>
struct Reflector {
    static_assert(std::is_class_v<T>, "Reflector requires a class/struct type");

    static constexpr auto fields() {
        return T::_reflect_fields();
    }

    static auto& get(T& obj, std::string_view name) {
        constexpr auto fs = fields();
        constexpr auto size = std::tuple_size_v<decltype(fs)>;
        auto* field = detail::find_field(name, fs, std::make_index_sequence<size>{});
        if (!field) throw std::out_of_range("Invalid field: " + std::string(name));
        return obj.*(field->second);
    }

    static const auto& get(const T& obj, std::string_view name) {
        constexpr auto fs = fields();
        constexpr auto size = std::tuple_size_v<decltype(fs)>;
        auto* field = detail::find_field(name, fs, std::make_index_sequence<size>{});
        if (!field) throw std::out_of_range("Invalid field: " + std::string(name));
        return obj.*(field->second);
    }
};

}  // namespace reflective

// Optionally define macro for auto [] access
#define REFLECT_INDEXABLE()                                    \
    auto& operator[](std::string_view key) {                   \
        return ::reflective::Reflector<Self>::get(*this, key); \
    }                                                          \
    const auto& operator[](std::string_view key) const {       \
        return ::reflective::Reflector<Self>::get(*this, key); \
    }
