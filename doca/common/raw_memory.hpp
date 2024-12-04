#pragma once

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>

namespace doca {
    namespace detail {
        template<typename To, typename From>
        struct transfer_cv {
            using type = To;
        };

        template<typename To, typename From>
        struct transfer_cv<To, From const> {
            using type = To const;
        };

        template<typename To, typename From>
        struct transfer_cv<To, From volatile> {
            using type = To volatile;
        };

        template<typename To, typename From>
        struct transfer_cv<To, From const volatile> {
            using type = To const volatile;
        };

        template<typename To, typename From>
        using transfer_cv_t = typename transfer_cv<To, From>::type;
    }

    template<typename T>
    concept non_cv_byteish =
        std::is_same_v<T, std::byte> ||
        std::is_same_v<T, char> ||
        std::is_same_v<T, std::uint8_t>;

    template<typename T>
    concept byteish = non_cv_byteish<std::remove_cv_t<T>>;

    template<typename R>
    concept spannable_range =
        std::ranges::contiguous_range<R> &&
        std::ranges::sized_range<R> &&
        (std::ranges::borrowed_range<R> || std::is_const_v<std::ranges::range_value_t<R>>);

    template<typename R>
    concept non_cv_byte_range = spannable_range<R> && non_cv_byteish<std::ranges::range_value_t<R>>;

    template<typename R>
    concept byte_range = spannable_range<R> && byteish<std::ranges::range_value_t<R>>;

    template<byteish OutByte>
    auto create_span(
        detail::transfer_cv_t<void, OutByte> *base,
        std::size_t size
    ) -> std::span<OutByte> {
        return { reinterpret_cast<OutByte*>(base), size };
    }

    template<byteish OutByte, byteish InByte>
    auto reinterpret_span(std::span<InByte> in) -> std::span<OutByte> {
        return create_span<OutByte>(in.data(), in.size());
    }

    template<byteish OutByte, byteish InByte>
    auto create_span(std::span<InByte> in) -> std::span<OutByte> {
        return reinterpret_span<OutByte>(in);
    }

    template<byteish OutByte, byte_range InRange>
    auto create_span(InRange &&in) -> std::span<OutByte> {
        return reinterpret_span<OutByte>(
            std::span<std::ranges::range_value_t<InRange>> { std::forward<InRange>(in) }
        );
    }
}
