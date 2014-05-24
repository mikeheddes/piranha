/***************************************************************************
 *   Copyright (C) 2009-2011 by Francesco Biscani                          *
 *   bluescarni@gmail.com                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef PIRANHA_MP_INTEGER_HPP
#define PIRANHA_MP_INTEGER_HPP

#include <algorithm>
#include <array>
#include <boost/integer_traits.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gmp.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "config.hpp"
#include "debug_access.hpp"
#include "detail/is_digit.hpp"
#include "exceptions.hpp"
#include "math.hpp"

namespace piranha { namespace detail {

// mpz_t is an array of some struct.
using mpz_struct_t = std::remove_extent< ::mpz_t>::type;
// Integral types used for allocation size and number of limbs.
using mpz_alloc_t = decltype(std::declval<mpz_struct_t>()._mp_alloc);
using mpz_size_t = decltype(std::declval<mpz_struct_t>()._mp_size);

// Some misc tests to check that the mpz struct conforms to our expectations.
// This is crucial for the implementation of the union integer type.
struct expected_mpz_struct_t
{
	mpz_alloc_t	_mp_alloc;
	mpz_size_t	_mp_size;
	::mp_limb_t	*_mp_d;
};

static_assert(sizeof(expected_mpz_struct_t) == sizeof(mpz_struct_t) &&
	std::is_standard_layout<mpz_struct_t>::value && std::is_standard_layout<expected_mpz_struct_t>::value &&
	offsetof(mpz_struct_t,_mp_alloc) == 0u &&
	offsetof(mpz_struct_t,_mp_size) == offsetof(expected_mpz_struct_t,_mp_size) &&
	offsetof(mpz_struct_t,_mp_d) == offsetof(expected_mpz_struct_t,_mp_d) &&
	std::is_same<mpz_alloc_t,decltype(std::declval<mpz_struct_t>()._mp_alloc)>::value &&
	std::is_same<mpz_size_t,decltype(std::declval<mpz_struct_t>()._mp_size)>::value &&
	std::is_same< ::mp_limb_t *,decltype(std::declval<mpz_struct_t>()._mp_d)>::value,
	"Invalid mpz_t struct layout.");

// Metaprogramming to select the limb/dlimb types.
template <int NBits>
struct si_limb_types
{
	static_assert(NBits == 0,"Invalid limb size.");
};

#if defined(PIRANHA_UINT128_T)
template <>
struct si_limb_types<64>
{
	using limb_t = std::uint_least64_t;
	using dlimb_t = PIRANHA_UINT128_T;
	static_assert(static_cast<dlimb_t>(boost::integer_traits<limb_t>::const_max) <=
		-dlimb_t(1) / boost::integer_traits<limb_t>::const_max,"128-bit integer is too narrow.");
	static const limb_t limb_bits = 64;
};
#endif

template <>
struct si_limb_types<32>
{
	using limb_t = std::uint_least32_t;
	using dlimb_t = std::uint_least64_t;
	static const limb_t limb_bits = 32;
};

template <>
struct si_limb_types<16>
{
	using limb_t = std::uint_least16_t;
	using dlimb_t = std::uint_least32_t;
	static const limb_t limb_bits = 16;
};

template <>
struct si_limb_types<8>
{
	using limb_t = std::uint_least8_t;
	using dlimb_t = std::uint_least16_t;
	static const limb_t limb_bits = 8;
};

template <>
struct si_limb_types<0> : public si_limb_types<
#if defined(PIRANHA_UINT128_T)
	64
#else
	32
#endif
	>
{};

// Simple RAII holder for GMP integers.
struct mpz_raii
{
	mpz_raii()
	{
		::mpz_init(&m_mpz);
		piranha_assert(m_mpz._mp_alloc > 0);
	}
	mpz_raii(const mpz_raii &) = delete;
	mpz_raii(mpz_raii &&) = delete;
	mpz_raii &operator=(const mpz_raii &) = delete;
	mpz_raii &operator=(mpz_raii &&) = delete;
	~mpz_raii() noexcept
	{
		if (m_mpz._mp_d != nullptr) {
			::mpz_clear(&m_mpz);
		}
	}
	mpz_struct_t m_mpz;
};

inline std::ostream &stream_mpz(std::ostream &os, const mpz_struct_t &mpz)
{
	const std::size_t size_base10 = ::mpz_sizeinbase(&mpz,10);
	if (unlikely(size_base10 > boost::integer_traits<std::size_t>::const_max - static_cast<std::size_t>(2))) {
		piranha_throw(std::invalid_argument,"number of digits is too large");
	}
	const auto total_size = size_base10 + 2u;
	std::vector<char> tmp(static_cast<std::vector<char>::size_type>(total_size));
	if (unlikely(tmp.size() != total_size)) {
		piranha_throw(std::invalid_argument,"number of digits is too large");
	}
	os << ::mpz_get_str(&tmp[0u],10,&mpz);
	return os;
}

template <int NBits>
struct static_integer
{
	using dlimb_t = typename si_limb_types<NBits>::dlimb_t;
	using limb_t = typename si_limb_types<NBits>::limb_t;
	// Limb bits used for the representation of the number.
	static const limb_t limb_bits = si_limb_types<NBits>::limb_bits;
	// Total number of bits in the limb type, >= limb_bits.
	static const unsigned total_bits = static_cast<unsigned>(std::numeric_limits<limb_t>::digits);
	static_assert(total_bits >= limb_bits,"Invalid limb_t type.");
	using limbs_type = std::array<limb_t,std::size_t(2)>;
	// Check: we need to be able to address all bits in the 2 limbs using limb_t.
	static_assert(limb_bits < boost::integer_traits<limb_t>::const_max / 2u,"Overflow error.");
	// NOTE: init everything otherwise zero is gonna be represented by undefined values in lo/hi.
	static_integer():_mp_alloc(0),_mp_size(0),m_limbs() {}
	template <typename Integer, typename = typename std::enable_if<std::is_integral<Integer>::value>::type>
	explicit static_integer(Integer n):_mp_alloc(0),_mp_size(0),m_limbs()
	{
		// NOTE: in order to improve performance, we could attempt a boost::numeric_cast to the limb type
		// and use the result directly into the first limb. How to deal with negative values?
		// NOTE: this should be a separate function to be called from the constructor from int of mp_integer. If
		// it throws, go through the construction via mpz but in case it works we could save quite a bit of time.
		const auto orig_n = n;
		limb_t bit_idx = 0;
		while (n != Integer(0)) {
			if (bit_idx == limb_bits * 2u) {
				piranha_throw(std::overflow_error,"insufficient bit width");
			}
			// NOTE: in C++11 division will round to zero always (for negative numbers as well).
			// The bit shift operator >> has implementation defined behaviour if n is signed and negative,
			// so we do not use it.
			const Integer quot = static_cast<Integer>(n / Integer(2)), rem = static_cast<Integer>(n % Integer(2));
			if (rem) {
				set_bit(bit_idx);
			}
			n = quot;
			++bit_idx;
		}
		fix_sign_ctor(orig_n);
	}
	template <typename T>
	void fix_sign_ctor(T, typename std::enable_if<std::is_unsigned<T>::value>::type * = nullptr) {}
	template <typename T>
	void fix_sign_ctor(T n, typename std::enable_if<std::is_signed<T>::value>::type * = nullptr)
	{
		if (n < T(0)) {
			negate();
		}
	}
	static_integer(const static_integer &) = default;
	static_integer(static_integer &&) = default;
	~static_integer() noexcept
	{
		piranha_assert(consistency_checks());
	}
	static_integer &operator=(const static_integer &) = default;
	static_integer &operator=(static_integer &&) = default;
	void negate()
	{
		// NOTE: this is 2 at most, no danger in taking the negative.
		_mp_size = -_mp_size;
	}
	void set_bit(const limb_t &idx)
	{
		using size_type = typename limbs_type::size_type;
		piranha_assert(idx < limb_bits * 2u);
		// Crossing fingers for compiler optimising this out.
		const auto quot = static_cast<limb_t>(idx / limb_bits), rem = static_cast<limb_t>(idx % limb_bits);
		m_limbs[static_cast<size_type>(quot)] = static_cast<limb_t>(m_limbs[static_cast<size_type>(quot)] | static_cast<limb_t>(limb_t(1) << rem));
		// Update the size if needed. The new size must be at least quot + 1, as we set a bit
		// in the limb with index quot.
		const auto new_size = static_cast<mpz_size_t>(quot + 1u);
		if (_mp_size < 0) {
			if (-new_size < _mp_size) {
				_mp_size = -new_size;
			}
		} else {
			if (new_size > _mp_size) {
				_mp_size = new_size;
			}
		}
	}
	mpz_size_t calculate_n_limbs() const
	{
		if (m_limbs[1u] != 0u) {
			return 2;
		}
		if (m_limbs[0u] != 0u) {
			return 1;
		}
		return 0;
	}
	bool consistency_checks() const
	{
		return _mp_alloc == 0 && _mp_size <= 2 && _mp_size >= -2 &&
			// Excess bits must be zero for consistency.
			!(static_cast<dlimb_t>(m_limbs[0u]) >> limb_bits) &&
			!(static_cast<dlimb_t>(m_limbs[1u]) >> limb_bits) &&
			(calculate_n_limbs() == _mp_size || -calculate_n_limbs() == _mp_size);
	}
	mpz_size_t abs_size() const
	{
		return (_mp_size >= 0) ? _mp_size : -_mp_size;
	}
	// Convert static integer to a GMP mpz. The out struct must be initialized to zero.
	void to_mpz(mpz_struct_t &out) const
	{
		// mp_bitcnt_t must be able to count all the bits in the static integer.
		static_assert(limb_bits * 2u < boost::integer_traits< ::mp_bitcnt_t>::const_max,"Overflow error.");
		piranha_assert(out._mp_d != nullptr && mpz_cmp_si(&out,0) == 0);
		auto l = m_limbs[0u];
		for (limb_t i = 0u; i < limb_bits; ++i) {
			if (l % 2u) {
				::mpz_setbit(&out,static_cast< ::mp_bitcnt_t>(i));
			}
			l = static_cast<limb_t>(l >> 1u);
		}
		l = m_limbs[1u];
		for (limb_t i = 0u; i < limb_bits; ++i) {
			if (l % 2u) {
				::mpz_setbit(&out,static_cast< ::mp_bitcnt_t>(i + limb_bits));
			}
			l = static_cast<limb_t>(l >> 1u);
		}
		if (_mp_size < 0) {
			// Switch the sign as needed.
			::mpz_neg(&out,&out);
		}
	}
	friend std::ostream &operator<<(std::ostream &os, const static_integer &si)
	{
		mpz_raii m;
		si.to_mpz(m.m_mpz);
		return stream_mpz(os,m.m_mpz);
	}
	bool operator==(const static_integer &other) const
	{
		return _mp_size == other._mp_size && m_limbs == other.m_limbs;
	}
	bool operator!=(const static_integer &other) const
	{
		return !operator==(other);
	}
	bool is_zero() const
	{
		return _mp_size == 0;
	}
	// Compare absolute values of two integers whose sizes are the same in absolute value.
	static int compare(const static_integer &a, const static_integer &b, const mpz_size_t &size)
	{
		using size_type = typename limbs_type::size_type;
		piranha_assert(size >= 0 && size <= 2);
		piranha_assert(a._mp_size == size || -a._mp_size == size);
		piranha_assert(a._mp_size == b._mp_size || a._mp_size == -b._mp_size);
		auto limb_idx = static_cast<size_type>(size);
		while (limb_idx != 0u) {
			--limb_idx;
			if (a.m_limbs[limb_idx] > b.m_limbs[limb_idx]) {
				return 1;
			} else if (a.m_limbs[limb_idx] < b.m_limbs[limb_idx]) {
				return -1;
			}
		}
		return 0;
	}
	bool operator<(const static_integer &other) const
	{
		const auto size0 = _mp_size, size1 = other._mp_size;
		if (size0 < size1) {
			return true;
		} else if (size1 < size0) {
			return false;
		} else {
			const mpz_size_t abs_size = static_cast<mpz_size_t>(size0 >= 0 ? size0 : -size0);
			const int cmp = compare(*this,other,abs_size);
			return (size0 >= 0) ? cmp < 0 : cmp > 0;
		}
	}
	bool operator>(const static_integer &other) const
	{
		const auto size0 = _mp_size, size1 = other._mp_size;
		if (size0 < size1) {
			return false;
		} else if (size1 < size0) {
			return true;
		} else {
			const mpz_size_t abs_size = static_cast<mpz_size_t>(size0 >= 0 ? size0 : -size0);
			const int cmp = compare(*this,other,abs_size);
			return (size0 >= 0) ? cmp > 0 : cmp < 0;
		}
	}
	bool operator>=(const static_integer &other) const
	{
		return !operator<(other);
	}
	bool operator<=(const static_integer &other) const
	{
		return !operator>(other);
	}
	// NOTE: the idea here is that it could happen that limb_bits is smaller than the actual total bits
	// used for the representation of limb_t. In such a case, in arithmetic operations whenever we cast from
	// dlimb_t to limb_t or exploit wrap-around arithmetics, we might have extra bits past limb_bits set
	// that need to be set to zero for consistency.
	template <typename T = static_integer>
	void clear_extra_bits(typename std::enable_if<T::limb_bits != T::total_bits>::type * = nullptr)
	{
		const auto delta_bits = total_bits - limb_bits;
		m_limbs[0u] = static_cast<limb_t>((m_limbs[0u] << delta_bits) >> delta_bits);
		m_limbs[1u] = static_cast<limb_t>((m_limbs[1u] << delta_bits) >> delta_bits);
	}
	template <typename T = static_integer>
	void clear_extra_bits(typename std::enable_if<T::limb_bits == T::total_bits>::type * = nullptr) {}
	static void raw_add(static_integer &res, const static_integer &x, const static_integer &y)
	{
		piranha_assert(x.abs_size() <= 2 && y.abs_size() <= 2);
		const dlimb_t lo = static_cast<dlimb_t>(static_cast<dlimb_t>(x.m_limbs[0u]) + y.m_limbs[0u]);
		const dlimb_t hi = static_cast<dlimb_t>((static_cast<dlimb_t>(x.m_limbs[1u]) + y.m_limbs[1u]) + (lo >> limb_bits));
		// NOTE: throw before modifying anything here, for exception safety.
		if (unlikely(static_cast<limb_t>(hi >> limb_bits) != 0u)) {
			piranha_throw(std::overflow_error,"overflow in raw addition");
		}
		res.m_limbs[0u] = static_cast<limb_t>(lo);
		res.m_limbs[1u] = static_cast<limb_t>(hi);
		res._mp_size = res.calculate_n_limbs();
		res.clear_extra_bits();
	}
	static void raw_sub(static_integer &res, const static_integer &x, const static_integer &y)
	{
		piranha_assert(x.abs_size() <= 2 && y.abs_size() <= 2);
		piranha_assert(x.abs_size() >= y.abs_size());
		piranha_assert(x.m_limbs[1u] >= y.m_limbs[1u]);
		const bool has_borrow = x.m_limbs[0u] < y.m_limbs[0u];
		piranha_assert(x.m_limbs[1u] > y.m_limbs[1u] || !has_borrow);
		res.m_limbs[0u] = static_cast<limb_t>(x.m_limbs[0u] - y.m_limbs[0u]);
		res.m_limbs[1u] = static_cast<limb_t>((x.m_limbs[1u] - y.m_limbs[1u]) - limb_t(has_borrow));
		res._mp_size = res.calculate_n_limbs();
		res.clear_extra_bits();
	}
	template <bool AddOrSub>
	static void add_or_sub(static_integer &res, const static_integer &x, const static_integer &y)
	{
		mpz_size_t asizex = x._mp_size, asizey = static_cast<mpz_size_t>(AddOrSub ? y._mp_size : -y._mp_size);
		bool signx = true, signy = true;
		if (asizex < 0) {
			asizex = -asizex;
			signx = false;
		}
		if (asizey < 0) {
			asizey = -asizey;
			signy = false;
		}
		piranha_assert(asizex <= 2 && asizey <= 2);
		if (signx == signy) {
			raw_add(res,x,y);
			if (!signx) {
				res.negate();
			}
		} else {
			if (asizex > asizey || (asizex == asizey && compare(x,y,asizex) >= 0)) {
				raw_sub(res,x,y);
				if (!signx) {
					res.negate();
				}
			} else {
				raw_sub(res,y,x);
				if (!signy) {
					res.negate();
				}
			}
		}
	}
	static void add(static_integer &res, const static_integer &x, const static_integer &y)
	{
		add_or_sub<true>(res,x,y);
	}
	static void sub(static_integer &res, const static_integer &x, const static_integer &y)
	{
		add_or_sub<false>(res,x,y);
	}
	static void raw_mul(static_integer &res, const static_integer &x, const static_integer &y, const mpz_size_t &asizex,
		const mpz_size_t &asizey)
	{
		piranha_assert(asizex > 0 && asizey > 0);
		const dlimb_t lo = static_cast<dlimb_t>(static_cast<dlimb_t>(x.m_limbs[0u]) * y.m_limbs[0u]);
		res.m_limbs[0u] = static_cast<limb_t>(lo);
		const limb_t cy_limb = static_cast<limb_t>(lo >> limb_bits);
		res.m_limbs[1u] = cy_limb;
		res._mp_size = static_cast<mpz_size_t>((asizex + asizey) - mpz_size_t(cy_limb == 0u));
		res.clear_extra_bits();
		piranha_assert(res._mp_size > 0);
	}
	static void mul(static_integer &res, const static_integer &x, const static_integer &y)
	{
		mpz_size_t asizex = x._mp_size, asizey = y._mp_size;
		if (unlikely(asizex == 0 || asizey == 0)) {
			res._mp_size = 0;
			res.m_limbs[0u] = 0u;
			res.m_limbs[1u] = 0u;
			return;
		}
		bool signx = true, signy = true;
		if (asizex < 0) {
			asizex = -asizex;
			signx = false;
		}
		if (asizey < 0) {
			asizey = -asizey;
			signy = false;
		}
		if (unlikely(asizex > 1 || asizey > 1)) {
			piranha_throw(std::overflow_error,"overflow in multiplication");
		}
		raw_mul(res,x,y,asizex,asizey);
		if (signx != signy) {
			res.negate();
		}
	}
	static_integer &operator+=(const static_integer &other)
	{
		add(*this,*this,other);
		return *this;
	}
	static_integer &operator-=(const static_integer &other)
	{
		sub(*this,*this,other);
		return *this;
	}
	static_integer &operator*=(const static_integer &other)
	{
		mul(*this,*this,other);
		return *this;
	}
	static_integer operator+() const
	{
		return *this;
	}
	friend static_integer operator+(const static_integer &x, const static_integer &y)
	{
		static_integer retval(x);
		retval += y;
		return retval;
	}
	static_integer operator-() const
	{
		static_integer retval(*this);
		retval.negate();
		return retval;
	}
	friend static_integer operator-(const static_integer &x, const static_integer &y)
	{
		static_integer retval(x);
		retval -= y;
		return retval;
	}
	friend static_integer operator*(const static_integer &x, const static_integer &y)
	{
		static_integer retval(x);
		retval *= y;
		return retval;
	}
	void multiply_accumulate(const static_integer &b, const static_integer &c)
	{
		mpz_size_t asizea = _mp_size, asizeb = b._mp_size, asizec = c._mp_size;
		bool signa = true, signb = true, signc = true;
		if (asizea < 0) {
			asizea = -asizea;
			signa = false;
		}
		if (asizeb < 0) {
			asizeb = -asizeb;
			signb = false;
		}
		if (asizec < 0) {
			asizec = -asizec;
			signc = false;
		}
		piranha_assert(asizea <= 2);
		if (unlikely(asizeb > 1 || asizec > 1)) {
			piranha_throw(std::overflow_error,"overflow in multadd");
		}
		if (unlikely(asizeb == 0 || asizec == 0)) {
			return;
		}
		static_integer tmp;
		raw_mul(tmp,b,c,asizeb,asizec);
		const mpz_size_t asizetmp = tmp._mp_size;
		const bool signtmp = (signb == signc);
		piranha_assert(asizetmp <= 2 && asizetmp > 0);
		if (signa == signtmp) {
			raw_add(*this,*this,tmp);
			if (!signa) {
				negate();
			}
		} else {
			if (asizea > asizetmp || (asizea == asizetmp && compare(*this,tmp,asizea) >= 0)) {
				raw_sub(*this,*this,tmp);
				if (!signa) {
					negate();
				}
			} else {
				raw_sub(*this,tmp,*this);
				if (!signtmp) {
					negate();
				}
			}
		}
	}
	// Left-shift by one.
	void lshift1()
	{
		piranha_assert(m_limbs[1u] < (limb_t(1) << (limb_bits - 1u)));
		using size_type = typename limbs_type::size_type;
		// Shift both limbs.
		dlimb_t lo = static_cast<dlimb_t>(static_cast<dlimb_t>(m_limbs[0u]) << dlimb_t(1));
		dlimb_t hi = static_cast<dlimb_t>(static_cast<dlimb_t>(m_limbs[1u]) << dlimb_t(1));
		m_limbs[0u] = static_cast<limb_t>(lo);
		m_limbs[1u] = static_cast<limb_t>(hi + (lo >> limb_bits));
		piranha_assert((hi >> limb_bits) != 1u);
		mpz_size_t asize = _mp_size;
		bool sign = true;
		if (asize < 0) {
			asize = -asize;
			sign = false;
		}
		if (asize < 2) {
			asize = static_cast<mpz_size_t>(asize + (m_limbs[static_cast<size_type>(asize)] != 0u));
			_mp_size = static_cast<mpz_size_t>(sign ? asize : -asize);
		}
		clear_extra_bits();
	}
	// Division.
	static void div(static_integer &q, static_integer &r, const static_integer &a, const static_integer &b)
	{
		piranha_assert(!b.is_zero());
		// NOTE: here in principle q/r could overlap with a or b (e.g., in in-place division).
		// We need to first read everything we need from a and b, and only then write into q/r.
		// Store the signs.
		const bool signa = a._mp_size >= 0, signb = b._mp_size >= 0;
		// Compute the result in dlimb_t.
		const dlimb_t ad = static_cast<dlimb_t>(a.m_limbs[0u] + (static_cast<dlimb_t>(a.m_limbs[1u]) << limb_bits)),
			bd = static_cast<dlimb_t>(b.m_limbs[0u] + (static_cast<dlimb_t>(b.m_limbs[1u]) << limb_bits));
		const dlimb_t qd = static_cast<dlimb_t>(ad / bd), rd = static_cast<dlimb_t>(ad % bd);
		// Convert back to array of limb_t.
		q.m_limbs[0u] = static_cast<limb_t>(qd);
		q.m_limbs[1u] = static_cast<limb_t>(qd >> limb_bits);
		q.clear_extra_bits();
		q._mp_size = q.calculate_n_limbs();
		r.m_limbs[0u] = static_cast<limb_t>(rd);
		r.m_limbs[1u] = static_cast<limb_t>(rd >> limb_bits);
		r.clear_extra_bits();
		r._mp_size = r.calculate_n_limbs();
		// The sign of the remainder is the same as the numerator.
		if (!signa) {
			r.negate();
		}
		// The sign of the quotient is the sign of a/b.
		if (signa != signb) {
			q.negate();
		}
	}
	// Compute the number of bits used in the representation of the integer.
	limb_t bits_size() const
	{
		using size_type = typename limbs_type::size_type;
		const auto asize = abs_size();
		if (asize == 0) {
			return 0u;
		}
		const auto idx = static_cast<size_type>(asize - 1);
		limb_t size = static_cast<limb_t>(limb_bits * idx), limb = m_limbs[idx];
		while (limb != 0u) {
			++size;
			limb = static_cast<limb_t>(limb >> 1u);
		}
		return size;
	}
	limb_t test_bit(const limb_t &idx) const
	{
		using size_type = typename limbs_type::size_type;
		piranha_assert(idx < limb_bits * 2u);
		const auto quot = static_cast<limb_t>(idx / limb_bits), rem = static_cast<limb_t>(idx % limb_bits);
		return (static_cast<limb_t>(m_limbs[static_cast<size_type>(quot)] & static_cast<limb_t>(limb_t(1u) << rem)) != 0u);
	}
	mpz_alloc_t	_mp_alloc;
	mpz_size_t	_mp_size;
	limbs_type	m_limbs;
};

// Static init.
template <int NBits>
const typename static_integer<NBits>::limb_t static_integer<NBits>::limb_bits;

// Integer union.
template <int NBits>
union integer_union
{
	using s_storage = static_integer<NBits>;
	using d_storage = mpz_struct_t;
	static void move_ctor_mpz(mpz_struct_t &to, mpz_struct_t &from) noexcept
	{
		to._mp_alloc = from._mp_alloc;
		to._mp_size = from._mp_size;
		to._mp_d = from._mp_d;
	}
	integer_union():st() {}
	integer_union(const integer_union &other)
	{
		if (other.is_static()) {
			::new (static_cast<void *>(&st)) s_storage(other.st);
		} else {
			::new (static_cast<void *>(&dy)) d_storage;
			::mpz_init_set(&dy,&other.dy);
			piranha_assert(dy._mp_alloc > 0);
		}
	}
	integer_union(integer_union &&other) noexcept
	{
		if (other.is_static()) {
			::new (static_cast<void *>(&st)) s_storage(std::move(other.st));
		} else {
			::new (static_cast<void *>(&dy)) d_storage;
			move_ctor_mpz(dy,other.dy);
			// Downgrade the other to an empty static.
			other.dy.~d_storage();
			::new (static_cast<void *>(&other.st)) s_storage();
		}
	}
	~integer_union() noexcept
	{
		if (is_static()) {
			st.~s_storage();
		} else {
			destroy_dynamic();
		}
	}
	integer_union &operator=(const integer_union &other)
	{
		if (unlikely(this == &other)) {
			return *this;
		}
		const bool s1 = is_static(), s2 = other.is_static();
		if (s1 && s2) {
			st = other.st;
		} else if (s1 && !s2) {
			// Destroy static.
			st.~s_storage();
			// Construct the dynamic struct.
			::new (static_cast<void *>(&dy)) d_storage;
			// Init + assign the mpz.
			::mpz_init_set(&dy,&other.dy);
			piranha_assert(dy._mp_alloc > 0);
		} else if (!s1 && s2) {
			// Create a copy of other and promote it.
			auto other_copy(other);
			other_copy.promote();
			::mpz_set(&dy,&other_copy.dy);
		} else {
			::mpz_set(&dy,&other.dy);
		}
		return *this;
	}
	integer_union &operator=(integer_union &&other) noexcept
	{
		if (unlikely(this == &other)) {
			return *this;
		}
		const bool s1 = is_static(), s2 = other.is_static();
		if (s1 && s2) {
			st = std::move(other.st);
		} else if (s1 && !s2) {
			// Destroy static.
			st.~s_storage();
			// Construct the dynamic struct.
			::new (static_cast<void *>(&dy)) d_storage;
			move_ctor_mpz(dy,other.dy);
			// Downgrade the other to an empty static.
			other.dy.~d_storage();
			::new (static_cast<void *>(&other.st)) s_storage();
		} else if (!s1 && s2) {
			// Promote directly other, no need for copy.
			other.promote();
			// Swap with the promoted other.
			::mpz_swap(&dy,&other.dy);
		} else {
			// Swap with other.
			::mpz_swap(&dy,&other.dy);
		}
		return *this;
	}
	bool is_static() const noexcept
	{
		return st._mp_alloc == 0;
	}
	static bool fits_in_static(const mpz_struct_t &mpz) noexcept
	{
		// NOTE: sizeinbase returns the index of the highest bit *counting from 1* (like a logarithm).
		return (::mpz_sizeinbase(&mpz,2) <= s_storage::limb_bits * 2u);
	}
	void destroy_dynamic()
	{
		piranha_assert(!is_static());
		piranha_assert(dy._mp_alloc > 0);
		piranha_assert(dy._mp_d != nullptr);
		::mpz_clear(&dy);
		dy.~d_storage();
	}
	void promote()
	{
		piranha_assert(is_static());
		mpz_raii tmp;
		st.to_mpz(tmp.m_mpz);
		// Destroy static.
		st.~s_storage();
		// Construct the dynamic struct.
		::new (static_cast<void *>(&dy)) d_storage;
		move_ctor_mpz(dy,tmp.m_mpz);
		tmp.m_mpz._mp_d = nullptr;
	}
	s_storage	st;
	d_storage	dy;
};

}

/// Multiple precision integer class.
/**
 * This class is a wrapper around the GMP arbitrary precision \p mpz_t type, and it can represent integer numbers of arbitrary size
 * (i.e., the range is limited only by the available memory).
 *
 * As an optimisation, this class will store in static internal storage a fixed number of digits before resorting to dynamic
 * memory allocation. The internal storage consists of two limbs of size \p NBits bits, for a total of <tt>2*NBits</tt> bits
 * of static storage. The possible values for \p NBits, supported on all platforms, are 8, 16, and 32.
 * A value of 64 is supported on some platforms. The special
 * default value of 0 is used to automatically select the optimal \p NBits value on the current platform.
 * 
 * \section interop Interoperability with fundamental types
 * 
 * Full interoperability with all integral and floating-point C++ types is provided.
 * 
 * Every function interacting with floating-point types will check that the floating-point values are not
 * non-finite: in case of infinities or NaNs, an <tt>std::invalid_argument</tt> exception will be thrown.
 * It should be noted that interoperability with floating-point types is provided for convenience, and it should
 * not be relied upon in case exact results are required (e.g., the conversion of a large integer to a floating-point value
 * might not be exact).
 *
 * \section exception_safety Exception safety guarantee
 *
 * This class provides the strong exception safety guarantee for all operations. In case of memory allocation errors by GMP routines,
 * the program will terminate.
 *
 * \section move_semantics Move semantics
 *
 * Move construction and move assignment will leave the moved-from object in an unspecified but valid state.
 *
 * \section implementation_details Implementation details
 *
 * This class uses, for certain routines, the internal interface of GMP integers, which is not guaranteed to be stable
 * across different versions. GMP versions 4.x, 5.x and 6.x are explicitly supported by this class.
 *
 * @see http://gmplib.org/manual/Integer-Internals.html
 * @see http://gmplib.org/
 *
 * @author Francesco Biscani (bluescarni@gmail.com)
 * 
 * \todo performance improvements:
 *   - reduce usage of gmp integers in internal implementation, change the semantics of the raii
 *     holder so that we avoid double allocations;
 *   - avoid going through mpz for print to stream,
 *   - try not to use dlimbs for addition, seems unnecessary (just low = low1 + low2, carry if low < low1/low2,
 *     and another branch for overflow on high -> test if it makes any diff in performance),
 *   - when cting from C++ ints, attempt a numeric_cast to limb_type for very fast conversion in static integer,
 *   - in raw_add/sub/div we always operate assuming the static int has 2 limbs, maybe there's performance to be gained
 *     by switch()ing the different cases for the operands sizes;
 * - use getters for dy/st for added safety;
 * - probably the assignment operator should demote to static if possible.
 */
template <int NBits = 0>
class mp_integer
{
		// Make friend with debugging class.
		template <typename>
		friend class debug_access;
		template <typename T>
		struct is_interoperable_type
		{
			static const bool value = std::is_floating_point<T>::value ||
				std::is_integral<T>::value;
		};
		// Types allowed in binary operations involving mp_integer.
		template <typename T, typename U>
		struct are_binary_op_types: std::integral_constant<bool,
			(std::is_same<T,mp_integer>::value && is_interoperable_type<U>::value) ||
			(std::is_same<U,mp_integer>::value && is_interoperable_type<T>::value) ||
			(std::is_same<T,mp_integer>::value && std::is_same<U,mp_integer>::value)>
		{};
		// Metaprogramming to establish the return type of binary arithmetic operations involving mp_integers.
		// Default result type will be mp_integer itself; for consistency with C/C++ when one of the arguments
		// is a floating point type, we will return a value of the same floating point type.
		template <typename T, typename U, typename Enable = void>
		struct deduce_binary_op_result_type
		{
			using type = mp_integer;
		};
		template <typename T, typename U>
		struct deduce_binary_op_result_type<T,U,typename std::enable_if<std::is_floating_point<T>::value>::type>
		{
			using type = T;
		};
		template <typename T, typename U>
		struct deduce_binary_op_result_type<T,U,typename std::enable_if<std::is_floating_point<U>::value>::type>
		{
			using type = U;
		};
		template <typename Float>
		void construct_from_floating_point(Float x)
		{
			if (unlikely(!std::isfinite(x))) {
				piranha_throw(std::invalid_argument,"cannot construct integer from non-finite floating-point number");
			}
			if (x == Float(0)) {
				return;
			}
			Float abs_x = std::abs(x);
			const unsigned radix = static_cast<unsigned>(std::numeric_limits<Float>::radix);
			detail::mpz_raii m, tmp;
			int exp = std::ilogb(abs_x);
			while (exp >= 0) {
				::mpz_ui_pow_ui(&tmp.m_mpz,radix,static_cast<unsigned>(exp));
				::mpz_add(&m.m_mpz,&m.m_mpz,&tmp.m_mpz);
				const Float tmp = std::scalbn(Float(1),exp);
				if (unlikely(tmp == HUGE_VAL)) {
					piranha_throw(std::invalid_argument,"output of std::scalbn is HUGE_VAL");
				}
				abs_x -= tmp;
				// NOTE: if the float is an integer exactly divisible by the radix, we eventually
				// get to abs_x == 0, in which case we have to prevent the call to ilogb below.
				if (unlikely(abs_x == Float(0))) {
					break;
				}
				// NOTE: in principle, here ilogb could return funky values if something goes wrong
				// or the initial call to ilogb gave an undefined result for some reason:
				// http://en.cppreference.com/w/cpp/numeric/math/ilogb
				exp = std::ilogb(abs_x);
				if (unlikely(exp == INT_MAX || exp == FP_ILOGBNAN)) {
					piranha_throw(std::invalid_argument,"error calling std::ilogb");
				}
			}
			if (m_int.fits_in_static(m.m_mpz)) {
				using limb_t = typename detail::integer_union<NBits>::s_storage::limb_t;
				const auto size2 = ::mpz_sizeinbase(&m.m_mpz,2);
				for (::mp_bitcnt_t i = 0u; i < size2; ++i) {
					if (::mpz_tstbit(&m.m_mpz,i)) {
						m_int.st.set_bit(static_cast<limb_t>(i));
					}
				}
				if (std::signbit(x)) {
					m_int.st.negate();
				}
			} else {
				m_int.promote();
				::mpz_swap(&m.m_mpz,&m_int.dy);
				if (std::signbit(x)) {
					::mpz_neg(&m_int.dy,&m_int.dy);
				}
			}
		}
		template <typename Integer>
		void construct_from_integral(Integer n_orig)
		{
			if (n_orig == Integer(0)) {
				return;
			}
			Integer n = n_orig;
			detail::mpz_raii m;
			::mp_bitcnt_t bit_idx = 0;
			while (n != Integer(0)) {
				Integer div = static_cast<Integer>(n / Integer(2)), rem = static_cast<Integer>(n % Integer(2));
				if (rem != Integer(0)) {
					::mpz_setbit(&m.m_mpz,bit_idx);
				}
				if (unlikely(bit_idx == boost::integer_traits< ::mp_bitcnt_t>::const_max)) {
					piranha_throw(std::invalid_argument,"overflow in the construction from integral type");
				}
				++bit_idx;
				n = div;
			}
			if (m_int.fits_in_static(m.m_mpz)) {
				using limb_t = typename detail::integer_union<NBits>::s_storage::limb_t;
				const auto size2 = ::mpz_sizeinbase(&m.m_mpz,2);
				for (::mp_bitcnt_t i = 0u; i < size2; ++i) {
					if (::mpz_tstbit(&m.m_mpz,i)) {
						m_int.st.set_bit(static_cast<limb_t>(i));
					}
				}
				// NOTE: keep the == here, so we prevent warnings from the compiler when cting from unsigned types.
				// It is inconsequential as n == 0 is already handled on top.
				if (n_orig <= Integer(0)) {
					m_int.st.negate();
				}
			} else {
				m_int.promote();
				::mpz_swap(&m.m_mpz,&m_int.dy);
				if (n_orig <= Integer(0)) {
					::mpz_neg(&m_int.dy,&m_int.dy);
				}
			}
		}
		template <typename Float>
		void construct_from_interoperable(const Float &x, typename std::enable_if<std::is_floating_point<Float>::value>::type * = nullptr)
		{
			construct_from_floating_point(x);
		}
		template <typename Integer>
		void construct_from_interoperable(const Integer &n, typename std::enable_if<std::is_integral<Integer>::value>::type * = nullptr)
		{
			construct_from_integral(n);
		}
		// Special casing for bool.
		void construct_from_interoperable(bool v)
		{
			if (v) {
				m_int.st.set_bit(0);
			}
		}
		static void validate_string(const char *str, const std::size_t &size)
		{
			if (!size) {
				piranha_throw(std::invalid_argument,"invalid string input for integer type");
			}
			const std::size_t has_minus = (str[0] == '-'), signed_size = static_cast<std::size_t>(size - has_minus);
			if (!signed_size) {
				piranha_throw(std::invalid_argument,"invalid string input for integer type");
			}
			// A number starting with zero cannot be multi-digit and cannot have a leading minus sign (no '-0' allowed).
			if (str[has_minus] == '0' && (signed_size > 1u || has_minus)) {
				piranha_throw(std::invalid_argument,"invalid string input for integer type");
			}
			// Check that each character is a digit.
			std::for_each(str + has_minus, str + size,[](char c){
				if (!detail::is_digit(c)) {
					piranha_throw(std::invalid_argument,"invalid string input for integer type");
				}
			});
		}
		void construct_from_string(const char *str)
		{
			// NOTE: it seems to be ok to call strlen on a char pointer obtained from std::string::c_str()
			// (as we do in the constructor from std::string). The output of c_str() is guaranteed
			// to be NULL terminated, and if the string is empty, this will still work (21.4.7 and around).
			validate_string(str,std::strlen(str));
			// String is OK.
			detail::mpz_raii m;
			// Use set() as m is already inited.
			const int retval = ::mpz_set_str(&m.m_mpz,str,10);
			if (retval == -1) {
				piranha_throw(std::invalid_argument,"invalid string input for integer type");
			}
			piranha_assert(retval == 0);
			const bool negate = (mpz_sgn(&m.m_mpz) == -1);
			if (negate) {
				::mpz_neg(&m.m_mpz,&m.m_mpz);
			}
			if (m_int.fits_in_static(m.m_mpz)) {
				using limb_t = typename detail::integer_union<NBits>::s_storage::limb_t;
				const auto size2 = ::mpz_sizeinbase(&m.m_mpz,2);
				for (::mp_bitcnt_t i = 0u; i < size2; ++i) {
					if (::mpz_tstbit(&m.m_mpz,i)) {
						m_int.st.set_bit(static_cast<limb_t>(i));
					}
				}
				if (negate) {
					m_int.st.negate();
				}
			} else {
				m_int.promote();
				::mpz_swap(&m.m_mpz,&m_int.dy);
				if (negate) {
					::mpz_neg(&m_int.dy,&m_int.dy);
				}
			}
		}
		// Conversion to integral types.
		template <typename T>
		static void check_mult2(const T &n, typename std::enable_if<std::is_signed<T>::value>::type * = nullptr)
		{
			if (n < std::numeric_limits<T>::min() / T(2) || n > std::numeric_limits<T>::max() / T(2)) {
				piranha_throw(std::overflow_error,"overflow in conversion to integral type");
			}
		}
		template <typename T>
		static void check_mult2(const T &n, typename std::enable_if<!std::is_signed<T>::value>::type * = nullptr)
		{
			if (n > std::numeric_limits<T>::max() / T(2)) {
				piranha_throw(std::overflow_error,"overflow in conversion to integral type");
			}
		}
		template <typename T>
		T convert_to_impl(typename std::enable_if<std::is_integral<T>::value && !std::is_same<T,bool>::value>::type * = nullptr) const
		{
			if (m_int.st._mp_size == 0) {
				return T(0);
			}
			const bool negative = m_int.st._mp_size < 0;
			// We cannot convert to unsigned type if this is negative.
			if (std::is_unsigned<T>::value && negative) {
				piranha_throw(std::overflow_error,"overflow in conversion to integral type");
			}
			T retval(0), tmp(static_cast<T>(negative ? -1 : 1));
			if (m_int.is_static()) {
				using limb_t = typename detail::integer_union<NBits>::s_storage::limb_t;
				const limb_t bits_size = m_int.st.bits_size();
				piranha_assert(bits_size != 0u);
				for (limb_t i = 0u; i < bits_size; ++i) {
					if (i != 0u) {
						check_mult2(tmp);
						tmp = static_cast<T>(tmp * T(2));
					}
					if (m_int.st.test_bit(i)) {
						if (negative && retval < std::numeric_limits<T>::min() - tmp) {
							piranha_throw(std::overflow_error,"overflow in conversion to integral type");
						} else if (!negative && retval > std::numeric_limits<T>::max() - tmp) {
							piranha_throw(std::overflow_error,"overflow in conversion to integral type");
						}
						retval = static_cast<T>(retval + tmp);
					}
				}
			} else {
				// NOTE: copy here, it's not the fastest way but it should be safer.
				detail::mpz_raii tmp_mpz;
				::mpz_set(&tmp_mpz.m_mpz,&m_int.dy);
				// Adjust the sign as needed, in order to use the test bit function below.
				if (mpz_sgn(&tmp_mpz.m_mpz) == -1) {
					::mpz_neg(&tmp_mpz.m_mpz,&tmp_mpz.m_mpz);
				}
				const std::size_t bits_size = ::mpz_sizeinbase(&tmp_mpz.m_mpz,2);
				piranha_assert(bits_size != 0u);
				for (std::size_t i = 0u; i < bits_size; ++i) {
					if (i != 0u) {
						check_mult2(tmp);
						tmp = static_cast<T>(tmp * T(2));
					}
					::mp_bitcnt_t bit_idx;
					try {
						bit_idx = boost::numeric_cast< ::mp_bitcnt_t>(i);
					} catch (...) {
						piranha_throw(std::overflow_error,"overflow in conversion to integral type");
					}
					if (::mpz_tstbit(&tmp_mpz.m_mpz,bit_idx)) {
						if (negative && retval < std::numeric_limits<T>::min() - tmp) {
							piranha_throw(std::overflow_error,"overflow in conversion to integral type");
						} else if (!negative && retval > std::numeric_limits<T>::max() - tmp) {
							piranha_throw(std::overflow_error,"overflow in conversion to integral type");
						}
						retval = static_cast<T>(retval + tmp);
					}
				}
			}
			return retval;
		}
		// Special casing for bool.
		template <typename T>
		T convert_to_impl(typename std::enable_if<std::is_same<T,bool>::value>::type * = nullptr) const
		{
			return m_int.st._mp_size != 0;
		}
		// Convert to floating-point.
		template <typename T>
		T convert_to_impl(typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr) const
		{
			// Special case for zero.
			if (m_int.st._mp_size == 0) {
				return T(0);
			}
			// Extract a GMP mpz to work with.
			detail::mpz_raii tmp;
			if (m_int.is_static()) {
				m_int.st.to_mpz(tmp.m_mpz);
			} else {
				::mpz_set(&tmp.m_mpz,&m_int.dy);
			}
			// Work on absolute value.
			if (m_int.st._mp_size < 0) {
				::mpz_neg(&tmp.m_mpz,&tmp.m_mpz);
			}
			const unsigned radix = static_cast<unsigned>(std::numeric_limits<T>::radix);
			// NOTE: radix must be between 2 and 62 for GMP functions to work.
			if (unlikely(radix < 2u || radix > 62u)) {
				piranha_throw(std::overflow_error,"overflow in conversion to floating-point type");
			}
			unsigned long r_size;
			try {
				r_size = boost::numeric_cast<unsigned long>(::mpz_sizeinbase(&tmp.m_mpz,static_cast<int>(radix)));
			} catch (...) {
				piranha_throw(std::overflow_error,"overflow in conversion to floating-point type");
			}
			// NOTE: sizeinbase might return the correct value, or increased by one. Check
			// which one is which.
			// https://gmplib.org/manual/Miscellaneous-Integer-Functions.html#Miscellaneous-Integer-Functions
			piranha_assert(r_size >= 1u);
			detail::mpz_raii tmp2, tmp3;
			::mpz_ui_pow_ui(&tmp2.m_mpz,static_cast<unsigned long>(radix),r_size - 1ul);
			::mpz_sub_ui(&tmp2.m_mpz,&tmp2.m_mpz,1ul);
			if (::mpz_cmp(&tmp2.m_mpz,&tmp.m_mpz) > 0) {
				--r_size;
			}
			// Init return value.
			T retval(0);
			int exp = 0;
			for (unsigned long i = 0u; i < r_size; ++i) {
				const auto rem = ::mpz_fdiv_q_ui(&tmp.m_mpz,&tmp.m_mpz,static_cast<unsigned long>(radix));
				const auto exp_val = std::scalbn(static_cast<T>(rem),exp);
				if (unlikely(exp_val == HUGE_VAL)) {
					// Return infinity if possible.
					if (std::numeric_limits<T>::has_infinity) {
						retval = std::numeric_limits<T>::infinity();
						break;
					} else {
						piranha_throw(std::overflow_error,"overflow in conversion to floating-point type");
					}
				}
				retval += exp_val;
				if (unlikely(exp == std::numeric_limits<int>::max())) {
					piranha_throw(std::overflow_error,"overflow in conversion to floating-point type");
				}
				++exp;
			}
			// Adjust sign.
			if (m_int.st._mp_size < 0) {
				retval = std::copysign(retval,std::numeric_limits<T>::lowest());
			}
			return retval;
		}
		// In-place add.
		template <bool AddOrSub>
		mp_integer &in_place_add_or_sub(const mp_integer &other)
		{
			const bool s1 = is_static(), s2 = other.is_static();
			if (s1 && s2) {
				// Attempt the static add/sub.
				try {
					// NOTE: the static add/sub will try to do the operation, if it fails an overflow error
					// will be generated. The operation is exception-safe, and m_int.st will be untouched
					// in case of problems.
					if (AddOrSub) {
						m_int.st.add(m_int.st,m_int.st,other.m_int.st);
					} else {
						m_int.st.sub(m_int.st,m_int.st,other.m_int.st);
					}
					return *this;
				} catch (const std::overflow_error &) {}
			}
			// Promote as needed, we need GMP types on both sides.
			if (s1) {
				m_int.promote();
			}
			if (s2) {
				detail::mpz_raii m;
				other.m_int.st.to_mpz(m.m_mpz);
				if (AddOrSub) {
					::mpz_add(&m_int.dy,&m_int.dy,&m.m_mpz);
				} else {
					::mpz_sub(&m_int.dy,&m_int.dy,&m.m_mpz);
				}
			} else {
				if (AddOrSub) {
					::mpz_add(&m_int.dy,&m_int.dy,&other.m_int.dy);
				} else {
					::mpz_sub(&m_int.dy,&m_int.dy,&other.m_int.dy);
				}
			}
			return *this;
		}
		mp_integer &in_place_add(const mp_integer &other)
		{
			return in_place_add_or_sub<true>(other);
		}
		template <typename T>
		mp_integer &in_place_add(const T &other, typename std::enable_if<std::is_integral<T>::value>::type * = nullptr)
		{
			return in_place_add(mp_integer(other));
		}
		template <typename T>
		mp_integer &in_place_add(const T &other, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (*this = static_cast<T>(*this) + other);
		}
		// Binary add.
		template <typename T, typename U>
		static mp_integer binary_plus(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_same<U,mp_integer>::value>::type * = nullptr)
		{
			mp_integer retval(n1);
			retval += n2;
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_plus(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_integral<U>::value>::type * = nullptr)
		{
			// NOTE: for binary ops, let's do first the conversion to mp_integer and then
			// use the mp_integer vs mp_integer operator.
			mp_integer retval(n2);
			retval += n1;
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_plus(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<U,mp_integer>::value && std::is_integral<T>::value>::type * = nullptr)
		{
			mp_integer retval(n1);
			retval += n2;
			return retval;
		}
		template <typename T>
		static T binary_plus(const mp_integer &n, const T &x, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (static_cast<T>(n) + x);
		}
		template <typename T>
		static T binary_plus(const T &x, const mp_integer &n, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return binary_plus(n,x);
		}
		// Subtraction.
		mp_integer &in_place_sub(const mp_integer &other)
		{
			return in_place_add_or_sub<false>(other);
		}
		template <typename T>
		mp_integer &in_place_sub(const T &other, typename std::enable_if<std::is_integral<T>::value>::type * = nullptr)
		{
			return in_place_sub(mp_integer(other));
		}
		template <typename T>
		mp_integer &in_place_sub(const T &other, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (*this = static_cast<T>(*this) - other);
		}
		// Binary subtraction.
		template <typename T, typename U>
		static mp_integer binary_subtract(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_same<U,mp_integer>::value>::type * = nullptr)
		{
			mp_integer retval(n2);
			retval -= n1;
			retval.negate();
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_subtract(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_integral<U>::value>::type * = nullptr)
		{
			mp_integer retval(n2);
			retval -= n1;
			retval.negate();
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_subtract(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<U,mp_integer>::value && std::is_integral<T>::value>::type * = nullptr)
		{
			mp_integer retval(n1);
			retval -= n2;
			return retval;
		}
		template <typename T>
		static T binary_subtract(const mp_integer &n, const T &x, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (static_cast<T>(n) - x);
		}
		template <typename T>
		static T binary_subtract(const T &x, const mp_integer &n, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return -binary_subtract(n,x);
		}
		// Multiplication.
		mp_integer &in_place_mul(const mp_integer &other)
		{
			const bool s1 = is_static(), s2 = other.is_static();
			if (s1 && s2) {
				// Attempt the static mul.
				try {
					m_int.st.mul(m_int.st,m_int.st,other.m_int.st);
					return *this;
				} catch (const std::overflow_error &) {}
			}
			// Promote as needed, we need GMP types on both sides.
			if (s1) {
				m_int.promote();
			}
			if (s2) {
				detail::mpz_raii m;
				other.m_int.st.to_mpz(m.m_mpz);
				::mpz_mul(&m_int.dy,&m_int.dy,&m.m_mpz);
			} else {
				::mpz_mul(&m_int.dy,&m_int.dy,&other.m_int.dy);
			}
			return *this;
		}
		template <typename T>
		mp_integer &in_place_mul(const T &other, typename std::enable_if<std::is_integral<T>::value>::type * = nullptr)
		{
			return in_place_mul(mp_integer(other));
		}
		template <typename T>
		mp_integer &in_place_mul(const T &other, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (*this = static_cast<T>(*this) * other);
		}
		template <typename T, typename U>
		static mp_integer binary_mul(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_same<U,mp_integer>::value>::type * = nullptr)
		{
			mp_integer retval(n2);
			retval *= n1;
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_mul(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_integral<U>::value>::type * = nullptr)
		{
			mp_integer retval(n2);
			retval *= n1;
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_mul(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<U,mp_integer>::value && std::is_integral<T>::value>::type * = nullptr)
		{
			mp_integer retval(n1);
			retval *= n2;
			return retval;
		}
		template <typename T>
		static T binary_mul(const mp_integer &n, const T &x, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (static_cast<T>(n) * x);
		}
		template <typename T>
		static T binary_mul(const T &x, const mp_integer &n, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return binary_mul(n,x);
		}
		// Division.
		mp_integer &in_place_div(const mp_integer &other)
		{
			if (unlikely(other.sign() == 0)) {
				piranha_throw(zero_division_error,"division by zero");
			}
			const bool s1 = is_static(), s2 = other.is_static();
			if (s1 && s2) {
				mp_integer r;
				m_int.st.div(m_int.st,r.m_int.st,m_int.st,other.m_int.st);
			} else if (s1 && !s2) {
				// Promote this.
				m_int.promote();
				::mpz_tdiv_q(&m_int.dy,&m_int.dy,&other.m_int.dy);
			} else if (!s1 && s2) {
				// Create a promoted copy of other.
				detail::mpz_raii m;
				other.m_int.st.to_mpz(m.m_mpz);
				::mpz_tdiv_q(&m_int.dy,&m_int.dy,&m.m_mpz);
			} else {
				::mpz_tdiv_q(&m_int.dy,&m_int.dy,&other.m_int.dy);
			}
			return *this;
		}
		template <typename T>
		mp_integer &in_place_div(const T &other, typename std::enable_if<std::is_integral<T>::value>::type * = nullptr)
		{
			return in_place_div(mp_integer(other));
		}
		template <typename T>
		mp_integer &in_place_div(const T &other, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (*this = static_cast<T>(*this) / other);
		}
		template <typename T, typename U>
		static mp_integer binary_div(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_same<U,mp_integer>::value>::type * = nullptr)
		{
			mp_integer retval(n1);
			retval /= n2;
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_div(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<T,mp_integer>::value && std::is_integral<U>::value>::type * = nullptr)
		{
			mp_integer retval(n1);
			retval /= n2;
			return retval;
		}
		template <typename T, typename U>
		static mp_integer binary_div(const T &n1, const U &n2, typename std::enable_if<
			std::is_same<U,mp_integer>::value && std::is_integral<T>::value>::type * = nullptr)
		{
			mp_integer retval(n1);
			retval /= n2;
			return retval;
		}
		template <typename T>
		static T binary_div(const mp_integer &n, const T &x, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (static_cast<T>(n) / x);
		}
		template <typename T>
		static T binary_div(const T &x, const mp_integer &n, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr)
		{
			return (x / static_cast<T>(n));
		}
	public:
		/// Defaulted default constructor.
		/**
		 * The value of the integer will be initialised to 0.
		 */
		mp_integer() = default;
		/// Defaulted copy constructor.
		mp_integer(const mp_integer &) = default;
		/// Defaulted move constructor.
		mp_integer(mp_integer &&) = default;
		/// Generic constructor.
		/**
		 * \note
		 * This constructor is enabled only if \p T is an \ref interop "interoperable type".
		 * 
		 * Construction from a floating-point type will result in the truncated
		 * counterpart of the original value.
		 * 
		 * @param[in] x object used to construct \p this.
		 * 
		 * @throws std::invalid_argument if the construction fails (e.g., construction from a non-finite
		 * floating-point value).
		 */
		template <typename T, typename = typename std::enable_if<is_interoperable_type<T>::value>::type>
		explicit mp_integer(const T &x)
		{
			construct_from_interoperable(x);
		}
		/// Constructor from string.
		/**
		 * The string must be a sequence of decimal digits, preceded by a minus sign for
		 * strictly negative numbers. The first digit of a non-zero number must not be zero. A malformed string will throw an \p std::invalid_argument
		 * exception.
		 * 
		 * @param[in] str decimal string representation of the number used to initialise the integer object.
		 * 
		 * @throws std::invalid_argument if the string is malformed.
		 */
		explicit mp_integer(const std::string &str)
		{
			construct_from_string(str.c_str());
		}
		/// Constructor from C string.
		/**
		 * Equivalent to the constructor from C++ string.
		 * 
		 * @param[in] str decimal string representation of the number used to initialise the integer object.
		 * 
		 * @see mp_integer(const std::string &)
		 */
		explicit mp_integer(const char *str)
		{
			construct_from_string(str);
		}
		/// Defaulted destructor.
		~mp_integer() = default;
		/// Defaulted copy-assignment operator.
		mp_integer &operator=(const mp_integer &) = default;
		/// Defaulted move-assignment operator.
		mp_integer &operator=(mp_integer &&) = default;
		/// Generic assignment operator.
		/**
		 * \note
		 * This operator is enabled only if \p T is an \ref interop "interoperable type".
		 * 
		 * This assignment operator is equivalent to constructing a temporary instance of mp_integer from \p x
		 * and then move-assigning it to \p this.
		 * 
		 * @param[in] x object that will be assigned to \p this.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception thrown by the generic constructor of mp_integer.
		 */
		template <typename T, typename = typename std::enable_if<is_interoperable_type<T>::value>::type>
		mp_integer &operator=(const T &x)
		{
			operator=(mp_integer(x));
			return *this;
		}
		/// Assignment from C++ string.
		/**
		 * Equivalent to the construction and susbequent move to \p this of a temporary mp_integer from \p str.
		 * 
		 * @param[in] str C++ string that will be assigned to \p this.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception thrown by the constructor of mp_integer from string.
		 */
		mp_integer &operator=(const std::string &str)
		{
			operator=(mp_integer(str));
			return *this;
		}
		/// Assignment from C string.
		/**
		 * Equivalent to the construction and susbequent move to \p this of a temporary mp_integer from \p str.
		 * 
		 * @param[in] str C string that will be assigned to \p this.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception thrown by the constructor of mp_integer from string.
		 */
		mp_integer &operator=(const char *str)
		{
			operator=(mp_integer(str));
			return *this;
		}
		/// Conversion operator.
		/**
		 * \note
		 * This operator is enabled only if \p T is an \ref interop "interoperable type".
		 * 
		 * Conversion to integral types, if possible, will always be exact. Conversion to \p bool produces
		 * \p true for nonzero values, \p false for zero. Conversion to floating-point types is performed
		 * via arithmetic operations and might generate infinities in case the value is too large.
		 * 
		 * @return the value of \p this converted to type \p T.
		 * 
		 * @throws std::overflow_error if the conversion fails (e.g., the range of the target integral type
		 * is insufficient to represent the value of <tt>this</tt>).
		 */
		template <typename T, typename = typename std::enable_if<is_interoperable_type<T>::value>::type>
		explicit operator T() const
		{
			return convert_to_impl<T>();
		}
		/// Overload output stream operator for piranha::mp_integer.
		/**
		 * The input \p n will be directed to the output stream \p os as a string of digits in base 10.
		 *
		 * @param[in] os output stream.
		 * @param[in] n piranha::mp_integer to be directed to stream.
		 *
		 * @return reference to \p os.
		 *
		 * @throws std::invalid_argument if the number of digits is larger than an implementation-defined maximum.
		 * @throws unspecified any exception thrown by <tt>std::vector::resize()</tt>.
		 */
		friend std::ostream &operator<<(std::ostream &os, const mp_integer &n)
		{
			if (n.m_int.is_static()) {
				return (os << n.m_int.st);
			} else {
				return detail::stream_mpz(os,n.m_int.dy);
			}
		}
		/// Promote to dynamic storage.
		/**
		 * This method will promote \p this to dynamic storage, if \p this is currently stored in static
		 * storage. Otherwise, an error will be raised.
		 *
		 * @throws std::invalid_argument if \p this is not currently stored in static storage.
		 */
		void promote()
		{
			if (unlikely(!m_int.is_static())) {
				piranha_throw(std::invalid_argument,"cannot promote non-static integer");
			}
			m_int.promote();
		}
		/// Test storage status.
		/**
		 * @return \p true if \p this is currently stored in static storage, \p false otherwise.
		 */
		bool is_static() const noexcept
		{
			return m_int.is_static();
		}
		/// Negate in-place.
		void negate() noexcept
		{
			if (is_static()) {
				m_int.st.negate();
			} else {
				::mpz_neg(&m_int.dy,&m_int.dy);
			}
		}
		/// Sign.
		/**
		 * @return 1 if <tt>this > 0</tt>, 0 if <tt>this == 0</tt> and -1 if <tt>this < 0</tt>.
		 */
		int sign() const noexcept
		{
			if (is_static()) {
				if (m_int.st._mp_size > 0) {
					return 1;
				}
				if (m_int.st._mp_size < 0) {
					return -1;
				}
				return 0;
			} else {
				return mpz_sgn(&m_int.dy);
			}
		}
		/// In-place addition.
		/**
		 * \note
		 * This operator is enabled only if \p T is an \ref interop "interoperable type" or piranha::mp_integer.
		 * 
		 * Add \p x in-place. If \p T is piranha::mp_integer or an integral type, the result will be exact. If \p T is a floating-point type, the following
		 * sequence of operations takes place:
		 * 
		 * - \p this is converted to an instance \p f of type \p T via the conversion operator,
		 * - \p f is added to \p x,
		 * - the result is assigned back to \p this.
		 * 
		 * @param[in] x argument for the addition.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 */
		template <typename T>
		typename std::enable_if<is_interoperable_type<T>::value || std::is_same<mp_integer,T>::value,
			mp_integer &>::type operator+=(const T &x)
		{
			return in_place_add(x);
		}
		/// Generic in-place addition with piranha::mp_integer.
		/**
		 * \note
		 * This operator is enabled only if \p T is a non-const \ref interop "interoperable type".
		 * 
		 * Add a piranha::mp_integer in-place. This method will first compute <tt>n + x</tt>, cast it back to \p T via \p static_cast and finally assign the result to \p x.
		 * 
		 * @param[in,out] x first argument.
		 * @param[in] n second argument.
		 * 
		 * @return reference to \p x.
		 * 
		 * @throws unspecified any exception thrown by the binary operator or by casting piranha::mp_integer to \p T.
		 */
		template <typename T>
		friend typename std::enable_if<is_interoperable_type<T>::value && !std::is_const<T>::value,T &>::type
			operator+=(T &x, const mp_integer &n)
		{
			x = static_cast<T>(n + x);
			return x;
		}
		/// Generic binary addition involving piranha::mp_integer.
		/**
		 * \note
		 * This template operator is enabled only if either:
		 * - \p T is piranha::mp_integer and \p U is an \ref interop "interoperable type",
		 * - \p U is piranha::mp_integer and \p T is an \ref interop "interoperable type",
		 * - both \p T and \p U are piranha::mp_integer.
		 * 
		 * If no floating-point types are involved, the exact result of the operation will be returned as a piranha::mp_integer.
		 * 
		 * If one of the arguments is a floating-point value \p f of type \p F, the other argument will be converted to an instance of type \p F
		 * and added to \p f to generate the return value, which will then be of type \p F.
		 * 
		 * @param[in] x first argument
		 * @param[in] y second argument.
		 * 
		 * @return <tt>x + y</tt>.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 */
		template <typename T, typename U>
		friend typename std::enable_if<are_binary_op_types<T,U>::value,typename deduce_binary_op_result_type<T,U>::type>::type
			operator+(const T &x, const U &y)
		{
			return binary_plus(x,y);
		}
		/// Identity operation.
		/**
		 * @return copy of \p this.
		 */
		mp_integer operator+() const noexcept
		{
			return *this;
		}
		/// Prefix increment.
		/**
		 * Increment \p this by one.
		 * 
		 * @return reference to \p this after the increment.
		 */
		mp_integer &operator++() noexcept
		{
			return operator+=(1);
		}
		/// Suffix increment.
		/**
		 * Increment \p this by one and return a copy of \p this as it was before the increment.
		 * 
		 * @return copy of \p this before the increment.
		 */
		mp_integer operator++(int) noexcept
		{
			const mp_integer retval(*this);
			++(*this);
			return retval;
		}
		/// In-place subtraction.
		/**
		 * \note
		 * This operator is enabled only if \p T is an \ref interop "interoperable type" or piranha::mp_integer.
		 * 
		 * Subtract \p x in-place. If \p T is piranha::mp_integer or an integral type, the result will be exact. If \p T is a floating-point type, the following
		 * sequence of operations takes place:
		 * 
		 * - \p this is converted to an instance \p f of type \p T via the conversion operator,
		 * - \p x is subtracted from \p f,
		 * - the result is assigned back to \p this.
		 * 
		 * @param[in] x argument for the subtraction.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 */
		template <typename T>
		typename std::enable_if<is_interoperable_type<T>::value || std::is_same<mp_integer,T>::value,
			mp_integer &>::type operator-=(const T &x)
		{
			return in_place_sub(x);
		}
		/// Generic in-place subtraction with piranha::mp_integer.
		/**
		 * \note
		 * This operator is enabled only if \p T is a non-const \ref interop "interoperable type".
		 * 
		 * Subtract a piranha::mp_integer in-place. This method will first compute <tt>x - n</tt>, cast it back to \p T via \p static_cast and finally assign the result to \p x.
		 * 
		 * @param[in,out] x first argument.
		 * @param[in] n second argument.
		 * 
		 * @return reference to \p x.
		 * 
		 * @throws unspecified any exception thrown by the binary operator or by casting piranha::mp_integer to \p T.
		 */
		template <typename T>
		friend typename std::enable_if<is_interoperable_type<T>::value && !std::is_const<T>::value,T &>::type
			operator-=(T &x, const mp_integer &n)
		{
			x = static_cast<T>(x - n);
			return x;
		}
		/// Generic binary subtraction involving piranha::mp_integer.
		/**
		 * \note
		 * This template operator is enabled only if either:
		 * - \p T is piranha::mp_integer and \p U is an \ref interop "interoperable type",
		 * - \p U is piranha::mp_integer and \p T is an \ref interop "interoperable type",
		 * - both \p T and \p U are piranha::mp_integer.
		 * 
		 * If one of the arguments is a floating-point value \p f of type \p F, the other argument will be converted to an instance of type \p F
		 * and subtracted from (or to) \p f to generate the return value, which will then be of type \p F.
		 * 
		 * @param[in] x first argument
		 * @param[in] y second argument.
		 * 
		 * @return <tt>x - y</tt>.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 */
		template <typename T, typename U>
		friend typename std::enable_if<are_binary_op_types<T,U>::value,typename deduce_binary_op_result_type<T,U>::type>::type
			operator-(const T &x, const U &y)
		{
			return binary_subtract(x,y);
		}
		/// Negated copy.
		/**
		 * @return copy of \p -this.
		 */
		mp_integer operator-() const noexcept
		{
			mp_integer retval(*this);
			retval.negate();
			return retval;
		}
		/// Prefix decrement.
		/**
		 * Decrement \p this by one and return.
		 * 
		 * @return reference to \p this.
		 */
		mp_integer &operator--() noexcept
		{
			return operator-=(1);
		}
		/// Suffix decrement.
		/**
		 * Decrement \p this by one and return a copy of \p this as it was before the decrement.
		 * 
		 * @return copy of \p this before the decrement.
		 */
		mp_integer operator--(int)
		{
			const mp_integer retval(*this);
			--(*this);
			return retval;
		}
		/// In-place multiplication.
		/**
		 * \note
		 * This operator is enabled only if \p T is an \ref interop "interoperable type" or piranha::mp_integer.
		 * 
		 * Multiply by \p x in-place. If \p T is piranha::mp_integer or an integral type, the result will be exact. If \p T is a floating-point type, the following
		 * sequence of operations takes place:
		 * 
		 * - \p this is converted to an instance \p f of type \p T via the conversion operator,
		 * - \p x is multiplied by \p f,
		 * - the result is assigned back to \p this.
		 * 
		 * @param[in] x argument for the multiplication.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 */
		template <typename T>
		typename std::enable_if<is_interoperable_type<T>::value || std::is_same<mp_integer,T>::value,
			mp_integer &>::type operator*=(const T &x)
		{
			return in_place_mul(x);
		}
		/// Generic in-place multiplication with piranha::mp_integer.
		/**
		 * \note
		 * This operator is enabled only if \p T is a non-const \ref interop "interoperable type".
		 * 
		 * Multiply by a piranha::mp_integer in-place. This method will first compute <tt>x * n</tt>, cast it back to \p T via \p static_cast and finally assign the result to \p x.
		 * 
		 * @param[in,out] x first argument.
		 * @param[in] n second argument.
		 * 
		 * @return reference to \p x.
		 * 
		 * @throws unspecified any exception thrown by the binary operator or by casting piranha::mp_integer to \p T.
		 */
		template <typename T>
		friend typename std::enable_if<is_interoperable_type<T>::value && !std::is_const<T>::value,T &>::type
			operator*=(T &x, const mp_integer &n)
		{
			x = static_cast<T>(x * n);
			return x;
		}
		/// Generic binary multiplication involving piranha::mp_integer.
		/**
		 * \note
		 * This template operator is enabled only if either:
		 * - \p T is piranha::mp_integer and \p U is an \ref interop "interoperable type",
		 * - \p U is piranha::mp_integer and \p T is an \ref interop "interoperable type",
		 * - both \p T and \p U are piranha::mp_integer.
		 * 
		 * If one of the arguments is a floating-point value \p f of type \p F, the other argument will be converted to an instance of type \p F
		 * and multiplied by \p f to generate the return value, which will then be of type \p F.
		 * 
		 * @param[in] x first argument
		 * @param[in] y second argument.
		 * 
		 * @return <tt>x * y</tt>.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 */
		template <typename T, typename U>
		friend typename std::enable_if<are_binary_op_types<T,U>::value,typename deduce_binary_op_result_type<T,U>::type>::type
			operator*(const T &x, const U &y)
		{
			return binary_mul(x,y);
		}
		/// Combined multiply-add.
		/**
		 * Sets \p this to <tt>this + (n1 * n2)</tt>.
		 * 
		 * @param[in] n1 first argument.
		 * @param[in] n2 second argument.
		 * 
		 * @return reference to \p this.
		 */
		mp_integer &multiply_accumulate(const mp_integer &n1, const mp_integer &n2) noexcept
		{
			const bool s0 = is_static(), s1 = n1.is_static(), s2 = n2.is_static();
			if (s0 && s1 && s2) {
				try {
					m_int.st.multiply_accumulate(n1.m_int.st,n2.m_int.st);
					return *this;
				} catch (const std::overflow_error &) {}
			}
			// 2**3 possibilities.
			// NOTE: here the 0 flag means that the operand needs to be promoted,
			// 1 means that it is dynamic already.
			const unsigned mask = static_cast<unsigned>(!s0) + (static_cast<unsigned>(!s1) << 1u)
				+ (static_cast<unsigned>(!s2) << 2u);
			switch (mask) {
				case 0u:
				{
					// This is the case in which the static failed due to overflow.
					m_int.promote();
					detail::mpz_raii m1, m2;
					n1.m_int.st.to_mpz(m1.m_mpz);
					n2.m_int.st.to_mpz(m2.m_mpz);
					::mpz_addmul(&m_int.dy,&m1.m_mpz,&m2.m_mpz);
					break;
				}
				case 1u:
				{
					detail::mpz_raii m1, m2;
					n1.m_int.st.to_mpz(m1.m_mpz);
					n2.m_int.st.to_mpz(m2.m_mpz);
					::mpz_addmul(&m_int.dy,&m1.m_mpz,&m2.m_mpz);
					break;
				}
				case 2u:
				{
					m_int.promote();
					detail::mpz_raii m2;
					n2.m_int.st.to_mpz(m2.m_mpz);
					::mpz_addmul(&m_int.dy,&n1.m_int.dy,&m2.m_mpz);
					break;
				}
				case 3u:
				{
					detail::mpz_raii m2;
					n2.m_int.st.to_mpz(m2.m_mpz);
					::mpz_addmul(&m_int.dy,&n1.m_int.dy,&m2.m_mpz);
					break;
				}
				case 4u:
				{
					m_int.promote();
					detail::mpz_raii m1;
					n1.m_int.st.to_mpz(m1.m_mpz);
					::mpz_addmul(&m_int.dy,&m1.m_mpz,&n2.m_int.dy);
					break;
				}
				case 5u:
				{
					detail::mpz_raii m1;
					n1.m_int.st.to_mpz(m1.m_mpz);
					::mpz_addmul(&m_int.dy,&m1.m_mpz,&n2.m_int.dy);
					break;
				}
				case 6u:
					m_int.promote();
					::mpz_addmul(&m_int.dy,&n1.m_int.dy,&n2.m_int.dy);
					break;
				case 7u:
					::mpz_addmul(&m_int.dy,&n1.m_int.dy,&n2.m_int.dy);
			}
			return *this;
		}
		/// In-place division.
		/**
		 * \note
		 * This operator is enabled only if \p T is an \ref interop "interoperable type" or piranha::mp_integer.
		 * 
		 * Divide by \p x in-place. If \p T is piranha::mp_integer or an integral type, the result will be truncated
		 * (i.e., rounded towards 0). If \p T is a floating-point type, the following
		 * sequence of operations takes place:
		 * 
		 * - \p this is converted to an instance \p f of type \p T via the conversion operator,
		 * - \p f is divided by \p x,
		 * - the result is assigned back to \p this.
		 * 
		 * @param[in] x argument for the division.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 * @throws piranha::zero_division_error if \p T is an integral type and \p x is zero.
		 */
		template <typename T>
		typename std::enable_if<is_interoperable_type<T>::value || std::is_same<mp_integer,T>::value,
			mp_integer &>::type operator/=(const T &x)
		{
			return in_place_div(x);
		}
		/// Generic in-place division with piranha::mp_integer.
		/**
		 * \note
		 * This operator is enabled only if \p T is a non-const \ref interop "interoperable type".
		 * 
		 * Divide by a piranha::mp_integer in-place. This method will first compute <tt>x / n</tt>, cast it back to \p T via \p static_cast and finally assign the result to \p x.
		 * 
		 * @param[in,out] x first argument.
		 * @param[in] n second argument.
		 * 
		 * @return reference to \p x.
		 * 
		 * @throws unspecified any exception thrown by the binary operator or by casting piranha::mp_integer to \p T.
		 */
		template <typename T>
		friend typename std::enable_if<is_interoperable_type<T>::value && !std::is_const<T>::value,T &>::type
			operator/=(T &x, const mp_integer &n)
		{
			x = static_cast<T>(x / n);
			return x;
		}
		/// Generic binary division involving piranha::mp_integer.
		/**
		 * \note
		 * This template operator is enabled only if either:
		 * - \p T is piranha::mp_integer and \p U is an \ref interop "interoperable type",
		 * - \p U is piranha::mp_integer and \p T is an \ref interop "interoperable type",
		 * - both \p T and \p U are piranha::mp_integer.
		 * 
		 * If one of the arguments is a floating-point value \p f of type \p F, the other argument will be converted to an instance of type \p F
		 * and divided by (or used as a dividend for) \p f to generate the return value, which will then be of type \p F.
		 * 
		 * @param[in] x first argument
		 * @param[in] y second argument.
		 * 
		 * @return <tt>x / y</tt>.
		 * 
		 * @throws unspecified any exception resulting from interoperating with floating-point types.
		 * @throws piranha::zero_division_error if both operands are of integral type and a division by zero occurs.
		 */
		template <typename T, typename U>
		friend typename std::enable_if<are_binary_op_types<T,U>::value,typename deduce_binary_op_result_type<T,U>::type>::type
			operator/(const T &x, const U &y)
		{
			return binary_div(x,y);
		}
	private:
		detail::integer_union<NBits> m_int;
};

//using integer = mp_integer<>;

namespace math
{

/// Specialisation of the implementation of piranha::math::multiply_accumulate() for piranha::mp_integer.
template <int NBits>
struct multiply_accumulate_impl<mp_integer<NBits>,mp_integer<NBits>,mp_integer<NBits>,void>
{
	/// Call operator.
	/**
	 * This implementation will use piranha::mp_integer::multiply_accumulate().
	 * 
	 * @param[in,out] x target value for accumulation.
	 * @param[in] y first argument.
	 * @param[in] z second argument.
	 * 
	 * @return <tt>x.multiply_accumulate(y,z)</tt>.
	 *
	 * @throws unspecified any exception thrown by piranha::mp_integer::multiply_accumulate().
	 */
	auto operator()(mp_integer<NBits> &x, const mp_integer<NBits> &y, const mp_integer<NBits> &z) const -> decltype(x.multiply_accumulate(y,z))
	{
		return x.multiply_accumulate(y,z);
	}
};

/// Specialisation of the piranha::math::negate() functor for piranha::mp_integer.
template <int NBits>
struct negate_impl<mp_integer<NBits>,void>
{
	/// Call operator.
	/**
	 * Will use internally piranha::mp_integer::negate().
	 * 
	 * @param[in,out] n piranha::mp_integer to be negated.
	 *
	 * @throws unspecified any exception thrown by piranha::mp_integer::negate().
	 */
	void operator()(mp_integer<NBits> &n) const
	{
		n.negate();
	}
};

/// Specialisation of the piranha::math::is_zero() functor for piranha::mp_integer.
template <int NBits>
struct is_zero_impl<mp_integer<NBits>,void>
{
	/// Call operator.
	/**
	 * Will use internally piranha::mp_integer::sign().
	 * 
	 * @param[in] n piranha::mp_integer to be tested.
	 * 
	 * @return \p true if \p n is zero, \p false otherwise.
	 * 
	 * @throws unspecified any exception thrown by piranha::mp_integer::sign().
	 */
	bool operator()(const mp_integer<NBits> &n) const
	{
		return n.sign() == 0;
	}
};

}

}

#endif
