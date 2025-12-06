#pragma once

namespace meta {

template <class T> T&& declval() noexcept;  // for unevaluated context

namespace utility {

template <class, class> struct same_type_guard;

template <class T> struct same_type_guard<T, T> {};

template <class T, class U>
concept same_as = requires {
    typename same_type_guard<T, U>;
    typename same_type_guard<U, T>;
};

template <class T> struct add_rvalue_reference {
    using type = T&&;
};

template <class T> struct add_rvalue_reference<T&> {
    using type = T&;
};

template <class T> struct add_rvalue_reference<T&&> {
    using type = T&&;
};

template <> struct add_rvalue_reference<void> {
    using type = void;
};

template <> struct add_rvalue_reference<const void> {
    using type = const void;
};

template <> struct add_rvalue_reference<volatile void> {
    using type = volatile void;
};

template <> struct add_rvalue_reference<const volatile void> {
    using type = const volatile void;
};

template <class T> using add_rvalue_reference_t = typename add_rvalue_reference<T>::type;

}  // namespace utility

// ---- 实现 convertible_to ----
template <class From, class To>
concept convertible_to =
    requires {
        // 要求能 static_cast 到 To
        static_cast<To>(declval<From>());
    } &&
    // 并且转换结果能绑定到 To&&
    requires(From&& f) {
        requires utility::same_as<
            utility::add_rvalue_reference_t<decltype(static_cast<To>(static_cast<From&&>(f)))>,
            utility::add_rvalue_reference_t<To>>;
    };

}  // namespace meta