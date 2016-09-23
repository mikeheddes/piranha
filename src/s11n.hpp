/* Copyright 2009-2016 Francesco Biscani (bluescarni@gmail.com)

This file is part of the Piranha library.

The Piranha library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The Piranha library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the Piranha library.  If not,
see https://www.gnu.org/licenses/. */

#ifndef PIRANHA_S11N_HPP
#define PIRANHA_S11N_HPP

// Common headers for serialization.
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/mpl/bool.hpp>
#include <boost/serialization/string.hpp>
#include <boost/version.hpp>
#include <cstddef>
#include <fstream>
#include <ios>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "detail/demangle.hpp"
#include "exceptions.hpp"
#include "is_key.hpp"
#include "symbol_set.hpp"
#include "type_traits.hpp"

namespace piranha
{

inline namespace impl
{

// Scalar types directly supported by the all serialization libraries.
template <typename T>
struct is_serialization_scalar
    : std::integral_constant<bool, std::is_same<char, T>::value || std::is_same<signed char, T>::value
                                       || std::is_same<unsigned char, T>::value || std::is_same<short, T>::value
                                       || std::is_same<unsigned short, T>::value || std::is_same<int, T>::value
                                       || std::is_same<unsigned, T>::value || std::is_same<long, T>::value
                                       || std::is_same<unsigned long, T>::value || std::is_same<long long, T>::value
                                       || std::is_same<unsigned long long, T>::value || std::is_same<float, T>::value
                                       || std::is_same<double, T>::value || std::is_same<bool, T>::value> {
};

// Implementation of the detection of boost saving archives.
namespace ibsa_impl
{

template <typename A>
using is_saving_t = typename A::is_saving;

template <typename A>
using is_loading_t = typename A::is_loading;

template <typename A, typename T>
using lshift_t = decltype(std::declval<A &>() << std::declval<const T &>());

template <typename A, typename T>
using and_t = decltype(std::declval<A &>() & std::declval<const T &>());

// NOTE: here it does not make much sense that the pointer is non-const, but we are going by the literal
// description in the boost archive concept.
template <typename A, typename T>
using save_binary_t = decltype(std::declval<A &>().save_binary(std::declval<T *>(), std::declval<std::size_t>()));

template <typename A, typename T>
using register_type_t = decltype(std::declval<A &>().template register_type<T>());

template <typename A>
using get_library_version_t = decltype(std::declval<const A &>().get_library_version());

struct helper;

template <typename A>
using get_helper_t_1 = decltype(std::declval<A &>().template get_helper<helper>());

template <typename A>
using get_helper_t_2 = decltype(std::declval<A &>().template get_helper<helper>(static_cast<void *const>(nullptr)));

template <typename Archive, typename T>
using impl = std::
    integral_constant<bool, std::is_same<is_detected_t<is_saving_t, uncvref_t<Archive>>, boost::mpl::bool_<true>>::value
                                && std::is_same<is_detected_t<is_loading_t, uncvref_t<Archive>>,
                                                boost::mpl::bool_<false>>::value
                                // NOTE: add lvalue ref instead of using Archive &, so we avoid a hard
                                // error if Archive is void.
                                && std::is_same<is_detected_t<lshift_t, Archive, T>, addlref_t<Archive>>::value
                                && std::is_same<is_detected_t<and_t, Archive, T>, addlref_t<Archive>>::value
                                && is_detected<save_binary_t, Archive, unref_t<T>>::value
                                && is_detected<register_type_t, Archive, uncvref_t<T>>::value
                                // NOTE: the docs here mention that get_library_version() is supposed to
                                // return an unsigned integral type, but the boost archives apparently
                                // return a type which is implicitly convertible to some unsigned int.
                                // This seems to work and it should cover also the cases in which the
                                // return type is a real unsigned int.
                                && std::is_convertible<is_detected_t<get_library_version_t, Archive>,
                                                       unsigned long long>::value
#if BOOST_VERSION >= 105700
                                //  Helper support is available since 1.57.
                                && is_detected<get_helper_t_1, Archive>::value
                                && is_detected<get_helper_t_2, Archive>::value
#endif
                      >;
}
}

/// Detect Boost saving archives.
/**
 * This type trait will be \p true if \p Archive is a valid Boost saving archive for type \p T,
 * \p false otherwise. The Boost saving archive concept is described at
 * http://www.boost.org/doc/libs/1_61_0/libs/serialization/doc/archives.html.
 */
template <typename Archive, typename T>
class is_boost_saving_archive
{
    static const bool implementation_defined = ibsa_impl::impl<Archive, T>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Archive, typename T>
const bool is_boost_saving_archive<Archive, T>::value;

inline namespace impl
{

// Implementation of boost loading archive concept. We reuse some types from the ibsa_impl namespace.
namespace ibla_impl
{

template <typename A, typename T>
using rshift_t = decltype(std::declval<A &>() >> std::declval<T &>());

template <typename A, typename T>
using and_t = decltype(std::declval<A &>() & std::declval<T &>());

template <typename A, typename T>
using load_binary_t = decltype(std::declval<A &>().load_binary(std::declval<T *>(), std::declval<std::size_t>()));

template <typename A, typename T>
using reset_object_address_t
    = decltype(std::declval<A &>().reset_object_address(std::declval<T *>(), std::declval<T *>()));

template <typename A>
using delete_created_pointers_t = decltype(std::declval<A &>().delete_created_pointers());

template <typename Archive, typename T>
using impl
    = std::integral_constant<bool, std::is_same<is_detected_t<ibsa_impl::is_saving_t, uncvref_t<Archive>>,
                                                boost::mpl::bool_<false>>::value
                                       && std::is_same<is_detected_t<ibsa_impl::is_loading_t, uncvref_t<Archive>>,
                                                       boost::mpl::bool_<true>>::value
                                       && std::is_same<is_detected_t<rshift_t, Archive, T>, addlref_t<Archive>>::value
                                       && std::is_same<is_detected_t<and_t, Archive, T>, addlref_t<Archive>>::value
                                       && is_detected<load_binary_t, Archive, unref_t<T>>::value
                                       && is_detected<ibsa_impl::register_type_t, Archive, uncvref_t<T>>::value
                                       && std::is_convertible<is_detected_t<ibsa_impl::get_library_version_t, Archive>,
                                                              unsigned long long>::value
                                       && is_detected<reset_object_address_t, Archive, unref_t<T>>::value
                                       && is_detected<delete_created_pointers_t, Archive>::value
#if BOOST_VERSION >= 105700
                                       && is_detected<ibsa_impl::get_helper_t_1, Archive>::value
                                       && is_detected<ibsa_impl::get_helper_t_2, Archive>::value
#endif
                             >;
}
}

/// Detect Boost loading archives.
/**
 * This type trait will be \p true if \p Archive is a valid Boost loading archive for type \p T,
 * \p false otherwise. The Boost loading archive concept is described at
 * http://www.boost.org/doc/libs/1_61_0/libs/serialization/doc/archives.html.
 */
template <typename Archive, typename T>
class is_boost_loading_archive
{
    static const bool implementation_defined = ibla_impl::impl<Archive, T>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Archive, typename T>
const bool is_boost_loading_archive<Archive, T>::value;

/// Default implementation of piranha::boost_save().
/**
 * The default implementation does not define any call operator, and will thus result
 * in a compile-time error if used.
 */
template <typename Archive, typename T, typename = void>
class boost_save_impl
{
};

inline namespace impl
{

// Enabler for the arithmetic specialisation of boost_save().
template <typename Archive, typename T>
using boost_save_arithmetic_enabler =
    // NOTE: the check for non-constness of Archive is implicit in the saving archive concept.
    // NOTE: here we are relying on the fact that the Archive class correctly advertises the capability
    // to serialize arithmetic types. This is for instance *not* the case for Boost archives, which
    // accept every type (at the signature level), but since here we are concerned only with arithmetic
    // types (for which serialization is always available in Boost archives), it should not matter.
    typename std::enable_if<is_boost_saving_archive<Archive, T>::value
                            && (is_serialization_scalar<T>::value || std::is_same<T, long double>::value)>::type;
}

/// Implementation of piranha::boost_save() for arithmetic types.
/**
 * \note
 * This specialisation is enabled if \p Archive and \p T satisfy piranha::is_boost_saving_archive and
 * \p T is either a floating-point type, or one of
 * - \p char, <tt>signed char</tt>, or <tt>unsigned char</tt>,
 * - \p int or <tt>unsigned</tt>,
 * - \p long or <tt>unsigned long</tt>,
 * - <tt>long long</tt> or <tt>unsigned long long</tt>,
 * - \p bool.
 */
template <typename Archive, typename T>
class boost_save_impl<Archive, T, boost_save_arithmetic_enabler<Archive, T>>
{
public:
    /// Call operator.
    /**
     * The body of this functions is equivalent to:
     * @code
     * ar << x;
     * @endcode
     *
     * @param[in] ar target Boost saving archive.
     * @param[in] x object to be saved.
     *
     * @throws unspecified any exception thrown by the stream operator of the archive.
     */
    void operator()(Archive &ar, const T &x) const
    {
        ar << x;
    }
};

inline namespace impl
{

// Enabler for boost_save() for strings.
template <typename Archive, typename T>
using boost_save_string_enabler =
    typename std::enable_if<is_boost_saving_archive<Archive, T>::value && std::is_same<T, std::string>::value>::type;
}

/// Implementation of piranha::boost_save() for \p std::string.
/**
 * \note
 * This specialisation is enabled if \p Archive and \p T satisfy piranha::is_boost_saving_archive and \p T is
 * \p std::string.
 */
template <typename Archive, typename T>
class boost_save_impl<Archive, T, boost_save_string_enabler<Archive, T>>
{
public:
    /// Call operator.
    /**
     * @param[in] a target archive.
     * @param[in] s string argument.
     *
     * @throws unspecified any exception thrown by the streaming operator of \p Archive.
     */
    void operator()(Archive &a, const std::string &s) const
    {
        a << s;
    }
};

inline namespace impl
{

template <typename Archive, typename T>
using boost_save_impl_t = decltype(boost_save_impl<Archive, T>{}(std::declval<Archive &>(), std::declval<const T &>()));

// Enabler for boost_save().
template <typename Archive, typename T>
using boost_save_enabler = typename std::enable_if<is_boost_saving_archive<Archive, T>::value
                                                       && is_detected<boost_save_impl_t, Archive, T>::value,
                                                   int>::type;
}

/// Save to Boost archive.
/**
 * \note
 * This function is enabled only if \p Archive and \p T satisfy piranha::is_boost_saving_archive, and the expression
 * <tt>boost_save_impl<Archive, T>{}(ar, x)</tt> is well-formed.
 *
 * This function will save to the Boost archive \p ar the object \p x. The implementation of this function is in
 * the call operator of the piranha::boost_save_impl functor. The body of this function is equivalent to:
 * @code
 * boost_save_impl<Archive, T>{}(ar, x);
 * @endcode
 *
 * @param[in] ar target Boost saving archive.
 * @param[in] x object to be saved.
 *
 * @throws unspecified any exception thrown by the call operator of piranha::boost_save_impl.
 */
template <typename Archive, typename T, boost_save_enabler<Archive, T> = 0>
inline void boost_save(Archive &ar, const T &x)
{
    boost_save_impl<Archive, T>{}(ar, x);
}

inline namespace impl
{

template <typename A, typename T>
using boost_save_t = decltype(piranha::boost_save(std::declval<A &>(), std::declval<const T &>()));
}

/// Detect the presence of piranha::boost_save().
/**
 * This type trait will be \p true if piranha::boost_save() can be called with template arguments \p Archive and
 * \p T, \p false otherwise.
 */
template <typename Archive, typename T>
class has_boost_save
{
    static const bool implementation_defined = is_detected<boost_save_t, Archive, T>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Archive, typename T>
const bool has_boost_save<Archive, T>::value;

/// Default implementation of piranha::boost_load().
/**
 * The default implementation does not define any call operator, and will thus result
 * in a compile-time error if used.
 */
template <typename Archive, typename T, typename = void>
class boost_load_impl
{
};

inline namespace impl
{

// Enabler for the arithmetic specialisation of boost_load().
template <typename Archive, typename T>
using boost_load_arithmetic_enabler =
    typename std::enable_if<is_boost_loading_archive<Archive, T>::value
                            && (is_serialization_scalar<T>::value || std::is_same<T, long double>::value)>::type;
}

/// Implementation of piranha::boost_load() for arithmetic types.
/**
 * \note
 * This specialisation is enabled if \p Archive and \p T satisfy piranha::is_boost_loading_archive and
 * \p T is either a floating-point type, or one of
 * - \p char, <tt>signed char</tt>, or <tt>unsigned char</tt>,
 * - \p int or <tt>unsigned</tt>,
 * - \p long or <tt>unsigned long</tt>,
 * - <tt>long long</tt> or <tt>unsigned long long</tt>,
 * - \p bool.
 */
template <typename Archive, typename T>
class boost_load_impl<Archive, T, boost_load_arithmetic_enabler<Archive, T>>
{
public:
    /// Call operator.
    /**
     * The body of this functions is equivalent to:
     * @code
     * ar >> x;
     * @endcode
     *
     * @param[in] ar the source Boost loading archive.
     * @param[in] x object to be loaded from \p ar.
     *
     * @throws unspecified any exception thrown by the stream operator of the archive.
     */
    void operator()(Archive &ar, T &x) const
    {
        ar >> x;
    }
};

inline namespace impl
{

// Enabler for boost_load for strings.
template <typename Archive, typename T>
using boost_load_string_enabler =
    typename std::enable_if<is_boost_loading_archive<Archive, T>::value && std::is_same<T, std::string>::value>::type;
}

/// Implementation of piranha::boost_load() for \p std::string.
/**
 * \note
 * This specialisation is enabled if \p Archive and \p T satisfy piranha::is_boost_loading_archive and \p T is
 * \p std::string.
 */
template <typename Archive, typename T>
class boost_load_impl<Archive, T, boost_load_string_enabler<Archive, T>>
{
public:
    /// Call operator.
    /**
     * @param[in] a the source archive.
     * @param[in] s target string.
     *
     * @throws unspecified any exception thrown by the streaming operator of \p Archive.
     */
    void operator()(Archive &a, std::string &s) const
    {
        a >> s;
    }
};

inline namespace impl
{

template <typename Archive, typename T>
using boost_load_impl_t = decltype(boost_load_impl<Archive, T>{}(std::declval<Archive &>(), std::declval<T &>()));

// Enabler for boost_load().
template <typename Archive, typename T>
using boost_load_enabler = typename std::enable_if<is_boost_loading_archive<Archive, T>::value
                                                       && is_detected<boost_load_impl_t, Archive, T>::value,
                                                   int>::type;
}

/// Load from Boost archive.
/**
 * \note
 * This function is enabled only if \p Archive and \p T satisfy piranha::is_boost_loading_archive, and the expression
 * <tt>boost_load_impl<Archive, T>{}(ar, x)</tt> is well-formed.
 *
 * This function will load the object \p x from the Boost archive \p ar. The implementation of this function is in
 * the call operator of the piranha::boost_load_impl functor. The body of this function is equivalent to:
 * @code
 * boost_load_impl<Archive, T>{}(ar, x);
 * @endcode
 *
 * @param[in] ar the source Boost loading archive.
 * @param[in] x the object that will be loaded from \p ar.
 *
 * @throws unspecified any exception thrown by the call operator of piranha::boost_load_impl.
 */
template <typename Archive, typename T, boost_load_enabler<Archive, T> = 0>
inline void boost_load(Archive &ar, T &x)
{
    boost_load_impl<Archive, T>{}(ar, x);
}

inline namespace impl
{

template <typename A, typename T>
using boost_load_t = decltype(piranha::boost_load(std::declval<A &>(), std::declval<T &>()));
}

/// Detect the presence of piranha::boost_load().
/**
 * This type trait will be \p true if piranha::boost_load() can be called with template arguments \p Archive and
 * \p T, \p false otherwise.
 */
template <typename Archive, typename T>
class has_boost_load
{
    static const bool implementation_defined = is_detected<boost_load_t, Archive, T>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Archive, typename T>
const bool has_boost_load<Archive, T>::value;

inline namespace impl
{

template <typename A, typename K>
using key_boost_save_t
    = decltype(std::declval<const K &>().boost_save(std::declval<A &>(), std::declval<const symbol_set &>()));
}

/// Detect the presence of the <tt>boost_save()</tt> method in keys.
/**
 * This type trait will be \p true if \p Key
 * provides a method compatible with the following signature:
 * @code
 * Key::boost_save(Archive &, const symbol_set &) const;
 * @endcode
 * The return type of the method is ignored by this type trait.
 *
 * If \p Key, after the removal of cv-ref qualifiers, does not satisfy piranha::is_key,
 * a compile-time error will be produced.
 */
template <typename Archive, typename Key>
class key_has_boost_save
{
    PIRANHA_TT_CHECK(is_key, uncvref_t<Key>);
    static const bool implementation_defined = is_detected<key_boost_save_t, Archive, Key>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Archive, typename Key>
const bool key_has_boost_save<Archive, Key>::value;

inline namespace impl
{

template <typename A, typename K>
using key_boost_load_t
    = decltype(std::declval<K &>().boost_load(std::declval<A &>(), std::declval<const symbol_set &>()));
}

/// Detect the presence of the <tt>boost_load()</tt> method in keys.
/**
 * This type trait will be \p true if \p Key
 * provides a method compatible with the following signature:
 * @code
 * Key::boost_load(Archive &, const symbol_set &);
 * @endcode
 * The return type of the method is ignored by this type trait.
 *
 * If \p Key, after the removal of cv-ref qualifiers, does not satisfy piranha::is_key,
 * a compile-time error will be produced.
 */
template <typename Archive, typename Key>
class key_has_boost_load
{
    PIRANHA_TT_CHECK(is_key, uncvref_t<Key>);
    static const bool implementation_defined = is_detected<key_boost_load_t, Archive, Key>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Archive, typename Key>
const bool key_has_boost_load<Archive, Key>::value;
}

#include "config.hpp"

#if defined(PIRANHA_WITH_MSGPACK)

#include <msgpack.hpp>

#if MSGPACK_VERSION_MAJOR < 2

#error Minimum msgpack-c version supported is 2.

#endif

#include <algorithm>
#include <array>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <cmath>
#include <ios>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>

namespace piranha
{

inline namespace impl
{

// Wrapper for std stream classes for use in msgpack. The reason for this wrapper is that msgpack expects
// streams with a write(const char *, std::size_t) method, but in std streams the second param is std::streamsize
// (which is a signed int). Hence we wrap the write method to do the safe conversion from size_t to streamsize.
template <typename Stream>
class msgpack_stream_wrapper : public Stream
{
public:
    // Inherit ctors.
    using Stream::Stream;
    auto write(const typename Stream::char_type *p, std::size_t count)
        -> decltype(std::declval<Stream &>().write(p, std::streamsize(0)))
    {
        // NOTE: we need numeric_cast because of circular dep problem if including safe_cast.
        // NOTE: this can probably be again a safe_cast, once we sanitize safe_cast.
        return static_cast<Stream *>(this)->write(p, boost::numeric_cast<std::streamsize>(count));
    }
};

template <typename T>
using msgpack_stream_write_t
    = decltype(std::declval<T &>().write(std::declval<const char *>(), std::declval<std::size_t>()));
}

/// Detect msgpack stream.
/**
 * This type trait will be \p true if \p T is a type which can be used as template argument to <tt>msgpack::packer</tt>,
 * \p false otherwise. Specifically, in order for this trait to be true \p T must be a non-const, non-reference type
 * with a <tt>write()</tt> method with a signature compatible with
 * @code
 * T::write(const char *, std::size_t);
 * @endcode
 * The return type of the <tt>write()</tt> method is ignored by the type trait.
 */
template <typename T>
class is_msgpack_stream
{
    static const bool implementation_defined
        = is_detected<msgpack_stream_write_t, T>::value && !std::is_reference<T>::value && !std::is_const<T>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename T>
const bool is_msgpack_stream<T>::value;

/// Serialization format for msgpack.
/**
 * The serialization of non-primitive objects can often be performed in different ways, with different tradeoffs
 * between performance, storage requirements and portability. The piranha::mp_integer class, for instance, can be
 * serialized either via a string representation (slow and with high storage requirements, but portable across
 * architectures, compilers and operating systems) or as an array of platform-dependent unsigned integrals (fast
 * and compact, but not portable).
 *
 * This enum establishes two strategies for msgpack serialization: a portable format, intended
 * to be usable across different platforms and suitable for the long-term storage of serialized objects, and a binary
 * format, intended for use in high-performance scenarios (e.g., as temporary on-disk storage during long
 * or memory-intensive computations). These formats are used by the Piranha msgpack serialization functions
 * (piranha::msgpack_pack(), piranha::msgpack_convert(), etc.).
 */
enum class msgpack_format {
    /// Portable.
    portable,
    /// Binary.
    binary
};

/// Default functor for the implementation of piranha::msgpack_pack().
/**
 * This functor can be specialised via the \p std::enable_if mechanism. Default implementation will not define
 * the call operator, and will hence result in a compilation error when used.
 */
template <typename Stream, typename T, typename = void>
struct msgpack_pack_impl {
};

inline namespace impl
{

template <typename Stream, typename T>
using msgpack_scalar_enabler =
    typename std::enable_if<is_msgpack_stream<Stream>::value && is_serialization_scalar<T>::value>::type;
}

/// Implementation of piranha::msgpack_pack() for fundamental C++ types supported by msgpack.
/**
 * \note
 * This specialisation is enabled if \p Stream satisfies piranha::is_msgpack_stream and \p T is one of the
 * following types:
 * - \p char, <tt>signed char</tt>, or <tt>unsigned char</tt>,
 * - \p int or <tt>unsigned</tt>,
 * - \p long or <tt>unsigned long</tt>,
 * - <tt>long long</tt> or <tt>unsigned long long</tt>,
 * - \p float or \p double,
 * - \p bool.
 *
 * The call operator will use directly the <tt>pack()</tt> method of the input msgpack packer.
 */
template <typename Stream, typename T>
struct msgpack_pack_impl<Stream, T, msgpack_scalar_enabler<Stream, T>> {
    /// Call operator.
    /**
     * @param[in] packer the target packer.
     * @param[in] x the object to be packed.
     *
     * @throws unspecified any exception thrown by <tt>msgpack::packer::pack()</tt>.
     */
    void operator()(msgpack::packer<Stream> &packer, const T &x, msgpack_format) const
    {
        packer.pack(x);
    }
};

inline namespace impl
{

template <typename Stream, typename T>
using msgpack_ld_enabler =
    typename std::enable_if<is_msgpack_stream<Stream>::value && std::is_same<T, long double>::value>::type;
}

/// Implementation of piranha::msgpack_pack() for <tt>long double</tt>.
/**
 * \note
 * This specialisation is enabled if \p Stream satisfies piranha::is_msgpack_stream and \p T is <tt>long double</tt>.
 */
template <typename Stream, typename T>
struct msgpack_pack_impl<Stream, T, msgpack_ld_enabler<Stream, T>> {
    /// Call operator.
    /**
     * If \p f is msgpack_format::binary then the byte representation of \p x is packed into \p packer. Otherwise,
     * \p x is converted into string format and the resulting string will be packed into \p packer.
     *
     * @param[in] packer the target packer.
     * @param[in] x the object to be packed.
     * @param[in] f the serialization format.
     *
     * @throws unspecified any exception thrown by:
     * - <tt>msgpack::packer::pack()</tt>, <tt>msgpack::packer::pack_bin()</tt>,
     *   <tt>msgpack::packer::pack_bin_body()</tt>,
     * - the public interface of \p std::ostringstream.
     */
    void operator()(msgpack::packer<Stream> &packer, const long double &x, msgpack_format f) const
    {
        if (f == msgpack_format::binary) {
            packer.pack_bin(sizeof(long double));
            packer.pack_bin_body(reinterpret_cast<const char *>(&x), sizeof(long double));
        } else {
            if (std::isnan(x)) {
                if (std::signbit(x)) {
                    packer.pack("-nan");
                } else {
                    packer.pack("+nan");
                }
            } else if (std::isinf(x)) {
                if (std::signbit(x)) {
                    packer.pack("-inf");
                } else {
                    packer.pack("+inf");
                }
            } else {
                std::ostringstream oss;
                // Make sure we are using the C locale.
                oss.imbue(std::locale::classic());
                // Use scientific format.
                oss << std::scientific;
                // http://stackoverflow.com/questions/554063/how-do-i-print-a-double-value-with-full-precision-using-cout
                // NOTE: this does not mean that the *exact* value of the long double is printed, just that the
                // value is recovered exactly if reloaded on the same machine. This is a compromise, as the exact
                // printing of the value in string form would take up hundreds of digits. On the other hand, there is a
                // similar situation also for float and double, as there is not guarantee that they conform to IEEE. In
                // the end it seems the only practical approach is to consider all floating-point types as approximate
                // values, subject to various platform/architecture vagaries.
                oss.precision(std::numeric_limits<long double>::max_digits10);
                oss << x;
                packer.pack(oss.str());
            }
        }
    }
};

inline namespace impl
{

template <typename Stream, typename T>
using msgpack_string_enabler =
    typename std::enable_if<is_msgpack_stream<Stream>::value && std::is_same<T, std::string>::value>::type;
}

/// Implementation of piranha::msgpack_pack() for \p std::string.
/**
 * \note
 * This specialisation is enabled if \p Stream satisfies piranha::is_msgpack_stream and \p T is \p std::string.
 *
 * The call operator will use directly the <tt>pack()</tt> method of the input msgpack packer.
 */
template <typename Stream, typename T>
struct msgpack_pack_impl<Stream, T, msgpack_string_enabler<Stream, T>> {
    /// Call operator.
    /**
     * @param[in] packer the target packer.
     * @param[in] s the string to be packed.
     *
     * @throws unspecified any exception thrown by <tt>msgpack::packer::pack()</tt>.
     */
    void operator()(msgpack::packer<Stream> &packer, const std::string &s, msgpack_format) const
    {
        packer.pack(s);
    }
};

inline namespace impl
{

template <typename Stream, typename T>
using msgpack_pack_impl_t = decltype(msgpack_pack_impl<Stream, T>{}(
    std::declval<msgpack::packer<Stream> &>(), std::declval<const T &>(), std::declval<msgpack_format>()));

// Enabler for msgpack_pack.
template <typename Stream, typename T>
using msgpack_pack_enabler =
    typename std::enable_if<is_msgpack_stream<Stream>::value && is_detected<msgpack_pack_impl_t, Stream, T>::value,
                            int>::type;
}

/// Pack generic object in a msgpack stream.
/**
 * \note
 * This function is enabled only if \p Stream satisfies piranha::is_msgpack_stream and if
 * <tt>msgpack_pack_impl<Stream, T>{}(packer, x, f)</tt> is a valid expression.
 *
 * This function is intended to pack the input value \p x into a msgpack \p packer using the format
 * \p f. The actual implementation of this function is in the piranha::msgpack_pack_impl functor.
 * The body of this function is equivalent to:
 * @code
 * msgpack_pack_impl<Stream, T>{}(packer, x, f);
 * @endcode
 *
 * @param[in] packer the msgpack packer object.
 * @param[in] x the object to be packed into \p packer.
 * @param[in] f the serialization format.
 *
 * @throws unspecified any exception thrown by the call operator piranha::msgpack_pack_impl.
 */
template <typename Stream, typename T, msgpack_pack_enabler<Stream, T> = 0>
inline void msgpack_pack(msgpack::packer<Stream> &packer, const T &x, msgpack_format f)
{
    msgpack_pack_impl<Stream, T>{}(packer, x, f);
}

/// Default functor for the implementation of piranha::msgpack_convert().
/**
 * This functor can be specialised via the \p std::enable_if mechanism. Default implementation will not define
 * the call operator, and will hence result in a compilation error when used.
 */
template <typename T, typename = void>
struct msgpack_convert_impl {
};

/// Implementation of piranha::msgpack_convert() for fundamental C++ types supported by msgpack.
/**
 * \note
 * This specialisation is enabled if \p T is one of the following types:
 * - \p char, <tt>signed char</tt>, or <tt>unsigned char</tt>,
 * - \p int or <tt>unsigned</tt>,
 * - \p long or <tt>unsigned long</tt>,
 * - <tt>long long</tt> or <tt>unsigned long long</tt>,
 * - \p float or \p double,
 * - \p bool.
 *
 * The call operator will use directly the <tt>convert()</tt> method of the input msgpack object.
 */
template <typename T>
struct msgpack_convert_impl<T, typename std::enable_if<is_serialization_scalar<T>::value>::type> {
    /// Call operator.
    /**
     * @param[out] x the output value.
     * @param[in] o the object to be converted.
     *
     * @throws unspecified any exception thrown by <tt>msgpack::object::convert()</tt>.
     */
    void operator()(T &x, const msgpack::object &o, msgpack_format) const
    {
        o.convert(x);
    }
};

/// Implementation of piranha::msgpack_convert() for <tt>long double</tt>.
template <>
struct msgpack_convert_impl<long double> {
    /// Call operator.
    /**
     * @param[out] x the output value.
     * @param[in] o the object to be converted.
     * @param[in] f the serialization format.
     *
     * @throws unspecified any exception thrown by <tt>msgpack::object::convert()</tt> or by the
     * public interface of <tt>std::istringstream</tt>.
     * @throws std::invalid_argument if the serialized value is a non-finite value not supported by the implementation,
     * or, when using the msgpack_format::portable format, the deserialized string does not represent a floating-point
     * value.
     */
    void operator()(long double &x, const msgpack::object &o, msgpack_format f) const
    {
        using lim = std::numeric_limits<long double>;
        if (f == msgpack_format::binary) {
            std::array<char, sizeof(long double)> tmp;
            o.convert(tmp);
            std::copy(tmp.begin(), tmp.end(), reinterpret_cast<char *>(&x));
        } else {
#if defined(PIRANHA_HAVE_THREAD_LOCAL)
            static thread_local
#endif
                std::string tmp;
            o.convert(tmp);
            if (tmp == "+nan") {
                if (lim::has_quiet_NaN) {
                    x = std::copysign(lim::quiet_NaN(), 1.l);
                } else {
                    piranha_throw(std::invalid_argument, "cannot deserialize a NaN if the platform does not support "
                                                         "quiet NaNs");
                }
            } else if (tmp == "-nan") {
                if (lim::has_quiet_NaN) {
                    x = std::copysign(lim::quiet_NaN(), -1.l);
                } else {
                    piranha_throw(std::invalid_argument, "cannot deserialize a NaN if the platform does not support "
                                                         "quiet NaNs");
                }
            } else if (tmp == "+inf") {
                if (lim::has_infinity) {
                    x = lim::infinity();
                } else {
                    piranha_throw(std::invalid_argument, "infinities are not supported by the platform");
                }
            } else if (tmp == "-inf") {
                if (lim::has_infinity) {
                    x = std::copysign(lim::infinity(), -1.l);
                } else {
                    piranha_throw(std::invalid_argument, "infinities are not supported by the platform");
                }
            } else {
                std::istringstream iss;
                iss.imbue(std::locale::classic());
                // NOTE: is seems like the std::scientific format flag has an effect on input
                // streams as well. See the example here:
                // http://en.cppreference.com/w/cpp/io/manip/fixed
                iss >> std::scientific;
                iss.str(tmp);
                iss >> x;
                if (unlikely(iss.fail())) {
                    piranha_throw(std::invalid_argument, "failed to parse the string '" + tmp + "' as a long double");
                }
            }
        }
    }
};

/// Implementation of piranha::msgpack_convert() for \p std::string.
/**
 * \note
 * This specialisation is enabled if \p T is \p std::string.
 *
 * The call operator will use directly the <tt>convert()</tt> method of the input msgpack object.
 */
template <typename T>
struct msgpack_convert_impl<T, typename std::enable_if<std::is_same<T, std::string>::value>::type> {
    /// Call operator.
    /**
     * @param[out] s the output string.
     * @param[in] o the object to be converted.
     *
     * @throws unspecified any exception thrown by <tt>msgpack::object::convert()</tt>.
     */
    void operator()(std::string &s, const msgpack::object &o, msgpack_format) const
    {
        o.convert(s);
    }
};

inline namespace impl
{

template <typename T>
using msgpack_convert_impl_t = decltype(msgpack_convert_impl<T>{}(
    std::declval<T &>(), std::declval<const msgpack::object &>(), std::declval<msgpack_format>()));

// Enabler for msgpack_convert.
template <typename T>
using msgpack_convert_enabler =
    typename std::enable_if<!std::is_const<T>::value && is_detected<msgpack_convert_impl_t, T>::value, int>::type;
}

/// Convert msgpack object.
/**
 * \note
 * This function is enabled only if \p T is not const and if
 * <tt>msgpack_convert_impl<T>{}(x, o, f)</tt> is a valid expression.
 *
 * This function is intended to convert the msgpack object \p o into an instance of type \p T, and to write
 * the converted value into \p x. The actual implementation of this function is in the piranha::msgpack_convert_impl
 * functor. The body of this function is equivalent to:
 * @code
 * msgpack_convert_impl<T>{}(x, o, f);
 * @endcode
 *
 * @param[out] x the output value.
 * @param[in] o the msgpack object that will be converted into \p x.
 * @param[in] f the serialization format.
 *
 * @throws unspecified any exception thrown by the call operator piranha::msgpack_convert_impl.
 */
template <typename T, msgpack_convert_enabler<T> = 0>
inline void msgpack_convert(T &x, const msgpack::object &o, msgpack_format f)
{
    msgpack_convert_impl<T>{}(x, o, f);
}

inline namespace impl
{

template <typename Stream, typename T>
using msgpack_pack_t = decltype(piranha::msgpack_pack(std::declval<msgpack::packer<Stream> &>(),
                                                      std::declval<const T &>(), std::declval<msgpack_format>()));
}

/// Detect the presence of piranha::msgpack_pack().
/**
 * This type trait will be \p true if piranha::msgpack_pack() can be called with template arguments
 * \p Stream and \p T, \p false otherwise.
 */
template <typename Stream, typename T>
class has_msgpack_pack
{
    static const bool implementation_defined = is_detected<msgpack_pack_t, Stream, T>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Stream, typename T>
const bool has_msgpack_pack<Stream, T>::value;

inline namespace impl
{

template <typename T>
using msgpack_convert_t = decltype(piranha::msgpack_convert(
    std::declval<T &>(), std::declval<const msgpack::object &>(), std::declval<msgpack_format>()));
}

/// Detect the presence of piranha::msgpack_convert().
/**
 * This type trait will be \p true if piranha::msgpack_convert() can be called with template argument \p T.
 */
template <typename T>
class has_msgpack_convert
{
    static const bool implementation_defined = is_detected<msgpack_convert_t, T>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename T>
const bool has_msgpack_convert<T>::value;

inline namespace impl
{

template <typename Stream, typename Key>
using key_msgpack_pack_t = decltype(std::declval<const Key &>().msgpack_pack(
    std::declval<msgpack::packer<Stream> &>(), std::declval<msgpack_format>(), std::declval<const symbol_set &>()));
}

/// Detect the presence of the <tt>%msgpack_pack()</tt> method in keys.
/**
 * This type trait will be \p true if \p Stream satisfies piranha::is_msgpack_stream and the \p Key type has
 * a method whose signature is compatible with:
 * @code
 * Key::msgpack_pack(msgpack::packer<Stream> &, msgpack_format, const symbol_set &) const;
 * @endcode
 * The return type of the method is ignored by this type trait.
 *
 * If \p Key, after the removal of cv-ref qualifiers, does not satisfy piranha::is_key,
 * a compile-time error will be produced.
 */
template <typename Stream, typename Key>
class key_has_msgpack_pack
{
    PIRANHA_TT_CHECK(is_key, uncvref_t<Key>);
    static const bool implementation_defined
        = is_detected<key_msgpack_pack_t, Stream, Key>::value && is_msgpack_stream<Stream>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Stream, typename Key>
const bool key_has_msgpack_pack<Stream, Key>::value;

inline namespace impl
{

template <typename Key>
using key_msgpack_convert_t = decltype(std::declval<Key &>().msgpack_convert(
    std::declval<const msgpack::object &>(), std::declval<msgpack_format>(), std::declval<const symbol_set &>()));
}

/// Detect the presence of the <tt>%msgpack_convert()</tt> method in keys.
/**
 * This type trait will be \p true if the \p Key type has a method whose signature is compatible with:
 * @code
 * Key::msgpack_convert(const msgpack::object &, msgpack_format, const symbol_set &);
 * @endcode
 * The return type of the method is ignored by this type trait.
 *
 * If \p Key, after the removal of cv-ref qualifiers, does not satisfy piranha::is_key,
 * a compile-time error will be produced.
 */
template <typename Key>
class key_has_msgpack_convert
{
    PIRANHA_TT_CHECK(is_key, uncvref_t<Key>);
    static const bool implementation_defined = is_detected<key_msgpack_convert_t, Key>::value;

public:
    /// Value of the type trait.
    static const bool value = implementation_defined;
};

template <typename Key>
const bool key_has_msgpack_convert<Key>::value;
}

#endif

#if defined(PIRANHA_WITH_ZLIB)

#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/zlib.hpp>

#define PIRANHA_ZLIB_CONDITIONAL(expr) expr

#else

#define PIRANHA_ZLIB_CONDITIONAL(expr) piranha_throw(not_implemented_error, "zlib support is not enabled")

#endif

namespace piranha
{

/// Data format.
/**
 * Data format used by high-level serialization functions such as piranha::save_file() and piranha::load_file().
 * The Boost formats are based on the Boost serialization library, while the msgpack formats are based on the msgpack
 * serialization format.
 *
 * The portable variants are intended to be usable across different architectures and
 * Piranha versions, whereas the binary variants are non-portable high-performance serialization formats intended
 * for temporary storage. That is, saving a binary archive created with Piranha version \p N on architecture \p A
 * and then loading it on a different architecture \p B or using a different Piranha version \p M will result in
 * undefined behaviour.
 */
enum class data_format {
    /// Boost binary.
    /**
     * This format is based on the Boost binary archives.
     */
    boost_binary,
    /// Boost portable.
    /**
     * This format is based on the Boost text archives.
     */
    boost_portable,
    /// msgpack binary.
    /**
     * This format will employ internally the msgpack_format::binary format.
     */
    msgpack_binary,
    /// msgpack portable.
    /**
     * This format will employ internally the msgpack_format::portable format.
     */
    msgpack_portable
};

/// Compression format.
/**
 * Compression formats used by high-level serialization functions such as piranha::save_file() and piranha::load_file().
 */
enum class compression {
    /// No compression.
    none,
    /// bzip2 compression.
    bzip2,
    /// gzip compression.
    gzip,
    /// zlib compression.
    zlib
};

namespace detail
{

// NOTE: no need for ifdefs guards here, as the compression-specific stuff is hidden in the CompressionFilter type.
template <typename CompressionFilter, typename T>
inline void save_file_boost_compress_impl(const T &x, std::ofstream &ofile, data_format f)
{
    piranha_assert(f == data_format::boost_binary || f == data_format::boost_portable);
    // NOTE: there seem to be 2 choices here: stream or streambuf. The first does some formatting, while the second
    // one if "raw" but does not provide the stream interface (which is used, e.g., by msgpack). Since we always
    // open files in binary mode (as suggested by the Boost serialization library), this should not matter in the end.
    // Some resources:
    // http://stackoverflow.com/questions/1753469/how-to-hook-up-boost-serialization-iostreams-to-serialize-gzip-an-object-to
    // https://code.google.com/p/cloudobserver/wiki/TutorialsBoostIOstreams
    // http://stackoverflow.com/questions/8116541/what-exactly-is-streambuf-how-do-i-use-it
    // http://www.boost.org/doc/libs/1_46_1/libs/serialization/doc/special.html
    boost::iostreams::filtering_ostream out;
    out.push(CompressionFilter{});
    out.push(ofile);
    if (f == data_format::boost_binary) {
        boost::archive::binary_oarchive oa(out);
        boost_save(oa, x);
    } else {
        boost::archive::text_oarchive oa(out);
        boost_save(oa, x);
    }
}

// NOTE: the implementation is the specular of the above.
template <typename DecompressionFilter, typename T>
inline void load_file_boost_compress_impl(T &x, std::ifstream &ifile, data_format f)
{
    piranha_assert(f == data_format::boost_binary || f == data_format::boost_portable);
    boost::iostreams::filtering_istream in;
    in.push(DecompressionFilter{});
    in.push(ifile);
    if (f == data_format::boost_binary) {
        boost::archive::binary_iarchive ia(in);
        boost_load(ia, x);
    } else {
        boost::archive::text_iarchive ia(in);
        boost_load(ia, x);
    }
}

// Main save/load functions for Boost format.
template <typename T, typename std::enable_if<has_boost_save<boost::archive::binary_oarchive, T>::value
                                                  && has_boost_save<boost::archive::text_oarchive, T>::value,
                                              int>::type
                      = 0>
inline void save_file_boost_impl(const T &x, const std::string &filename, data_format f, compression c)
{
    namespace bi = boost::iostreams;
    // NOTE: always open in binary mode in order to avoid problems with special formatting in streams.
    std::ofstream ofile(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (unlikely(!ofile.good())) {
        piranha_throw(std::runtime_error, "file '" + filename + "' could not be opened for saving");
    }
    switch (c) {
        case compression::bzip2:
            save_file_boost_compress_impl<bi::bzip2_compressor>(x, ofile, f);
            break;
        case compression::gzip:
            PIRANHA_ZLIB_CONDITIONAL(save_file_boost_compress_impl<bi::gzip_compressor>(x, ofile, f));
            break;
        case compression::zlib:
            PIRANHA_ZLIB_CONDITIONAL(save_file_boost_compress_impl<bi::zlib_compressor>(x, ofile, f));
            break;
        case compression::none:
            if (f == data_format::boost_binary) {
                boost::archive::binary_oarchive oa(ofile);
                boost_save(oa, x);
            } else {
                boost::archive::text_oarchive oa(ofile);
                boost_save(oa, x);
            }
    }
}

template <typename T, typename std::enable_if<!has_boost_save<boost::archive::binary_oarchive, T>::value
                                                  || !has_boost_save<boost::archive::text_oarchive, T>::value,
                                              int>::type
                      = 0>
inline void save_file_boost_impl(const T &, const std::string &, data_format, compression)
{
    piranha_throw(not_implemented_error, "type '" + demangle<T>() + "' does not support serialization via Boost");
}

template <typename T, typename std::enable_if<has_boost_load<boost::archive::binary_iarchive, T>::value
                                                  && has_boost_load<boost::archive::text_iarchive, T>::value,
                                              int>::type
                      = 0>
inline void load_file_boost_impl(T &x, const std::string &filename, data_format f, compression c)
{
    namespace bi = boost::iostreams;
    std::ifstream ifile(filename, std::ios::in | std::ios::binary);
    if (unlikely(!ifile.good())) {
        piranha_throw(std::runtime_error, "file '" + filename + "' could not be opened for loading");
    }
    switch (c) {
        case compression::bzip2:
            load_file_boost_compress_impl<bi::bzip2_decompressor>(x, ifile, f);
            break;
        case compression::gzip:
            PIRANHA_ZLIB_CONDITIONAL(load_file_boost_compress_impl<bi::gzip_decompressor>(x, ifile, f));
            break;
        case compression::zlib:
            PIRANHA_ZLIB_CONDITIONAL(load_file_boost_compress_impl<bi::zlib_decompressor>(x, ifile, f));
            break;
        case compression::none:
            if (f == data_format::boost_binary) {
                boost::archive::binary_iarchive ia(ifile);
                boost_load(ia, x);
            } else {
                boost::archive::text_iarchive ia(ifile);
                boost_load(ia, x);
            }
    }
}

template <typename T, typename std::enable_if<!has_boost_load<boost::archive::binary_iarchive, T>::value
                                                  || !has_boost_load<boost::archive::text_iarchive, T>::value,
                                              int>::type
                      = 0>
inline void load_file_boost_impl(T &, const std::string &, data_format, compression)
{
    piranha_throw(not_implemented_error, "type '" + demangle<T>() + "' does not support deserialization via Boost");
}

#if defined(PIRANHA_WITH_MSGPACK)

// Compressed load/save for msgpack.
template <typename CompressionFilter, typename T>
inline void save_file_msgpack_compress_impl(const T &x, msgpack_stream_wrapper<std::ofstream> &ofile, msgpack_format mf)
{
    msgpack_stream_wrapper<boost::iostreams::filtering_ostream> out;
    out.push(CompressionFilter{});
    out.push(ofile);
    msgpack::packer<decltype(out)> packer(out);
    msgpack_pack(packer, x, mf);
}

template <typename DecompressionFilter, typename T>
inline void load_file_msgpack_compress_impl(T &x, const std::string &filename, msgpack_format mf)
{
    std::ifstream ifile(filename, std::ios::in | std::ios::binary);
    if (unlikely(!ifile.good())) {
        piranha_throw(std::runtime_error, "file '" + filename + "' could not be opened for loading");
    }
    std::vector<char> vchar;
    boost::iostreams::filtering_istream in;
    in.push(DecompressionFilter{});
    in.push(ifile);
    std::copy(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), std::back_inserter(vchar));
    auto oh = msgpack::unpack(vchar.data(), boost::numeric_cast<std::size_t>(vchar.size()));
    msgpack_convert(x, oh.get(), mf);
}

// Main msgpack load/save functions.
template <typename T,
          typename std::enable_if<has_msgpack_pack<msgpack_stream_wrapper<std::ofstream>, T>::value
                                      && has_msgpack_pack<msgpack_stream_wrapper<boost::iostreams::filtering_ostream>,
                                                          T>::value,
                                  int>::type
          = 0>
inline void save_file_msgpack_impl(const T &x, const std::string &filename, data_format f, compression c)
{
    namespace bi = boost::iostreams;
    const auto mf = (f == data_format::msgpack_binary) ? msgpack_format::binary : msgpack_format::portable;
    msgpack_stream_wrapper<std::ofstream> ofile(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (unlikely(!ofile.good())) {
        piranha_throw(std::runtime_error, "file '" + filename + "' could not be opened for saving");
    }
    switch (c) {
        case compression::bzip2:
            save_file_msgpack_compress_impl<bi::bzip2_compressor>(x, ofile, mf);
            break;
        case compression::gzip:
            PIRANHA_ZLIB_CONDITIONAL(save_file_msgpack_compress_impl<bi::gzip_compressor>(x, ofile, mf));
            break;
        case compression::zlib:
            PIRANHA_ZLIB_CONDITIONAL(save_file_msgpack_compress_impl<bi::zlib_compressor>(x, ofile, mf));
            break;
        case compression::none: {
            msgpack::packer<decltype(ofile)> packer(ofile);
            msgpack_pack(packer, x, mf);
        }
    }
}

template <typename T,
          typename std::enable_if<!has_msgpack_pack<msgpack_stream_wrapper<std::ofstream>, T>::value
                                      || !has_msgpack_pack<msgpack_stream_wrapper<boost::iostreams::filtering_ostream>,
                                                           T>::value,
                                  int>::type
          = 0>
inline void save_file_msgpack_impl(const T &, const std::string &, data_format, compression)
{
    piranha_throw(not_implemented_error, "type '" + demangle<T>() + "' does not support serialization via msgpack");
}

template <typename T, typename std::enable_if<has_msgpack_convert<T>::value, int>::type = 0>
inline void load_file_msgpack_impl(T &x, const std::string &filename, data_format f, compression c)
{
    namespace bi = boost::iostreams;
    const auto mf = (f == data_format::msgpack_binary) ? msgpack_format::binary : msgpack_format::portable;
    switch (c) {
        case compression::bzip2:
            load_file_msgpack_compress_impl<bi::bzip2_decompressor>(x, filename, mf);
            break;
        case compression::gzip:
            PIRANHA_ZLIB_CONDITIONAL(load_file_msgpack_compress_impl<bi::gzip_decompressor>(x, filename, mf));
            break;
        case compression::zlib:
            PIRANHA_ZLIB_CONDITIONAL(load_file_msgpack_compress_impl<bi::zlib_decompressor>(x, filename, mf));
            break;
        case compression::none: {
            // NOTE: two-stage construction for exception handling.
            std::unique_ptr<bi::mapped_file> mmap;
            try {
                mmap.reset(new bi::mapped_file(filename, bi::mapped_file::readonly));
            } catch (...) {
                // NOTE: this is just to beautify a bit the error message, and we assume any error in the line
                // above results from being unable to open the file.
                piranha_throw(std::runtime_error, "file '" + filename + "' could not be opened for loading");
            }
            // NOTE: this might be superfluous, but better safe than sorry.
            if (unlikely(!mmap->is_open())) {
                piranha_throw(std::runtime_error, "file '" + filename + "' could not be opened for loading");
            }
            auto oh = msgpack::unpack(mmap->const_data(), boost::numeric_cast<std::size_t>(mmap->size()));
            msgpack_convert(x, oh.get(), mf);
        }
    }
}

template <typename T, typename std::enable_if<!has_msgpack_convert<T>::value, int>::type = 0>
inline void load_file_msgpack_impl(T &, const std::string &, data_format, compression)
{
    piranha_throw(not_implemented_error, "type '" + demangle<T>() + "' does not support deserialization via msgpack");
}

#else

// If msgpack is not available, just error out.
template <typename T>
inline void save_file_msgpack_impl(const T &, const std::string &, data_format, compression)
{
    piranha_throw(not_implemented_error, "msgpack support is not enabled");
}

template <typename T>
inline void load_file_msgpack_impl(T &, const std::string &, data_format, compression)
{
    piranha_throw(not_implemented_error, "msgpack support is not enabled");
}

#endif

// General enabler for load_file().
template <typename T>
using load_file_enabler = typename std::enable_if<!std::is_const<T>::value, int>::type;
}

/// Save to file.
/**
 * This function will save the generic object \p x to the file named \p filename, using the data format
 * \p f and the compression method \p c.
 *
 * This function is built on lower-level routines such as piranha::boost_save() and piranha::msgpack_pack(). The data
 * format \p f establishes both the lower level serialization method to be used and its variant (e.g., portable
 * vs binary). If requested (i.e., if \p c is not piranha::compression::none), the output file will be compressed.
 *
 * @param[in] x object to be saved to file.
 * @param[in] filename name of the output file.
 * @param[in] f data format.
 * @param[in] c compression format.
 *
 * @throws piranha::not_implemented_error in the following cases:
 * - the type \p T does not implement the required serialization method (e.g., \p f is
 *   piranha::data_format::boost_binary but \p T does not provide an implementation of piranha::boost_save()),
 * - a necessary optional third-party library (e.g., msgpack or one of the compression libraries)
 *   is not available on the host platform.
 * @throws std::runtime_error in case the file cannot be opened for writing.
 * @throws unspecified any exception thrown by:
 * - <tt>boost::numeric_cast()</tt>,
 * - the invoked low-level serialization function,
 * - the public interface of the Boost iostreams library.
 */
template <typename T>
inline void save_file(const T &x, const std::string &filename, data_format f, compression c)
{
    if (f == data_format::boost_binary || f == data_format::boost_portable) {
        detail::save_file_boost_impl(x, filename, f, c);
    } else if (f == data_format::msgpack_binary || f == data_format::msgpack_portable) {
        detail::save_file_msgpack_impl(x, filename, f, c);
    }
}

/// Load from file.
/**
 * \note
 * This function is enabled only if \p T is not const.
 *
 * This function will load the content of the file named \p filename into the object \p x, assuming that the data is
 * stored in the format \p f using the compression method \p c. If \p c is not piranha::compression::none, it will
 * be assumed that the file is compressed.
 *
 * This function is built on lower-level routines such as piranha::boost_load() and piranha::msgpack_convert(). The data
 * format \p f establishes both the lower level serialization method to be used and its variant (e.g., portable
 * vs binary).
 *
 * @param[out] x the object into which the content of the file name \p filename will be deserialized.
 * @param[in] filename name of the input file.
 * @param[in] f data format.
 * @param[in] c compression format.
 *
 * @throws piranha::not_implemented_error in the following cases:
 * - the type \p T does not implement the required serialization method (e.g., \p f is
 *   piranha::data_format::boost_binary but \p T does not provide an implementation of piranha::boost_load()),
 * - a necessary optional third-party library (e.g., msgpack or one of the compression libraries)
 *   is not available on the host platform.
 * @throws std::runtime_error in case the file cannot be opened for reading.
 * @throws unspecified any exception thrown by:
 * - <tt>boost::numeric_cast()</tt>,
 * - the invoked low-level serialization function,
 * - the \p new operator,
 * - the public interface of the Boost iostreams library.
 */
template <typename T, detail::load_file_enabler<T> = 0>
inline void load_file(T &x, const std::string &filename, data_format f, compression c)
{
    if (f == data_format::boost_binary || f == data_format::boost_portable) {
        detail::load_file_boost_impl(x, filename, f, c);
    } else if (f == data_format::msgpack_binary || f == data_format::msgpack_portable) {
        detail::load_file_msgpack_impl(x, filename, f, c);
    }
}
}

#undef PIRANHA_ZLIB_CONDITIONAL

#endif