// http://turtle.sourceforge.net
//
// Copyright Mathieu Champlon 2012
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef MOCK_SIGNATURE_HPP_INCLUDED
#define MOCK_SIGNATURE_HPP_INCLUDED

#include "../config.hpp"
#include <type_traits>

namespace mock { namespace detail {
#define MOCK_NOARG
#define MOCK_STRIP_FUNCTION_QUALIFIERS(cv, ref)              \
    template<typename R, typename... Args>                   \
    struct strip_function_qualifiers<R(Args...) cv ref>      \
    {                                                        \
        using type = R(Args...);                             \
    };                                                       \
    template<typename R, typename... Args>                   \
    struct strip_function_qualifiers<R(Args..., ...) cv ref> \
    {                                                        \
        using type = R(Args..., ...);                        \
    };

#define MOCK_STRIP_FUNCTION_QUALIFIERS_REF(cv) \
    MOCK_STRIP_FUNCTION_QUALIFIERS(cv, )       \
    MOCK_STRIP_FUNCTION_QUALIFIERS(cv, &)      \
    MOCK_STRIP_FUNCTION_QUALIFIERS(cv, &&)

    template<typename>
    struct strip_function_qualifiers;
    MOCK_STRIP_FUNCTION_QUALIFIERS_REF(MOCK_NOARG)
    MOCK_STRIP_FUNCTION_QUALIFIERS_REF(const)
    MOCK_STRIP_FUNCTION_QUALIFIERS_REF(volatile)
    MOCK_STRIP_FUNCTION_QUALIFIERS_REF(const volatile)
#undef MOCK_NOARG
#undef MOCK_STRIP_FUNCTION_QUALIFIERS
#undef MOCK_STRIP_FUNCTION_QUALIFIERS_REF

    template<typename M>
    struct signature;

    template<typename R, typename... Args>
    struct signature<R(Args...)>
    {
        using type = R(Args...);
    };

    template<typename Sig, typename C>
    struct signature<Sig(C::*)> : signature<typename strip_function_qualifiers<Sig>::type>
    {};

    /// Return the (non-member) function signature out of (any) signature
    template<typename M>
    using signature_t = typename signature<M>::type;

    /// CRTP class to define the base_type typedef
    template<typename T>
    struct base
    {
        using base_type = T;
    };

    // If an error is generated by the line below it means the method is ambiguous.
    // Specify its signature to disambiguate
    template<typename T>
    T ambiguous_method_requires_to_specify_signature(const T&);
}} // namespace mock::detail

#define MOCK_SIGNATURE(M) \
    mock::detail::signature_t<decltype(mock::detail::ambiguous_method_requires_to_specify_signature(&base_type::M))>

#endif // MOCK_SIGNATURE_HPP_INCLUDED
