///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Electronic Arts Inc. All rights reserved.
///////////////////////////////////////////////////////////////////////////////

#ifndef EASTL_TUPLE_NAMED_H
#define EASTL_TUPLE_NAMED_H

#include <EASTL/internal/config.h>
#include <EASTL/tuple.h>

#if EASTL_TUPLE_ENABLED

namespace eastl
{
	constexpr unsigned long hashCalc(const char* ch, size_t len)
	{
		return len > 0 ? ((unsigned long)(*ch) + hashCalc(ch + 1, len - 1) * 33) % (1 << 26) : 0;
	}
	constexpr unsigned long operator "" _tn(const char* ch, size_t len)
	{
		return hashCalc(ch, len);
	}

	template<typename T, size_t N>
	class tuple_named_tag { };

	// attempt to isolate index given a type
	template <size_t val, size_t... Ns>
	struct index_lookup_helper;

	template <size_t val>
	struct index_lookup_helper<val>
	{
		typedef void DuplicateHashCheck;
		index_lookup_helper() = delete; // index_lookup should only be used for compile-time assistance, and never be instantiated
		static const size_t index = 0;
	};

	template <size_t val, size_t... Ns>
	struct index_lookup_helper<val, val, Ns...>
	{
		typedef int DuplicateHashCheck;
		static_assert(is_void<typename index_lookup_helper<val, Ns...>::DuplicateHashCheck>::value, "duplicate tag found in named_tag; all named tag values must be unique.");

		static const size_t index = 0;
	};

	template <size_t val, size_t N, size_t... Ns>
	struct index_lookup_helper<val, N, Ns...>
	{
		typedef typename index_lookup_helper<val, Ns...>::DuplicateHashCheck DuplicateHashCheck;
		static const size_t index = index_lookup_helper<val, Ns...>::index + 1;
	};

	template <size_t val, typename... Tags>
	struct index_lookup;

	template <size_t val, typename... Ts, size_t... Ns>
	struct index_lookup<val, tuple_named_tag<Ts, Ns>...>
	{
		static const int index = val > sizeof...(Ts) ? index_lookup_helper<val, Ns...>::index : val;
	};

	template<typename... Tags>
	class tuple_named;

	template<typename... Ts, size_t... Ns>
	class tuple_named<tuple_named_tag<Ts, Ns>...> : public tuple<Ts...>
	{
	public:
		tuple_named<tuple_named_tag<Ts, Ns>...>(const Ts&... t)
			:tuple<Ts...>(t...)
		{}

		typedef tuple<Ts...> TupleType;
	};
	
	template <size_t I, typename... Tags>
	auto get(tuple_named<Tags...>& t)
	{
		return I;
		//return get<index_lookup<I, Tags...>::index>(tuple_named<Tags...>::TupleType(t));
	}

}  // namespace eastl

#endif  // EASTL_TUPLE_ENABLED

#endif  // EASTL_TUPLE_NAMED_H
