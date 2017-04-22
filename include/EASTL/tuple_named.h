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
	class tuple_named_tag
	{
	};

	template<typename... Tags>
	class tuple_named;

	template<size_t... Ns, typename... Ts>
	class tuple_named<tuple_named_tag<Ts, Ns>...> : public tuple<Ts...>
	{
	public:
		tuple_named<tuple_named_tag<Ts, Ns>...>(const Ts&... t)
			:tuple<Ts...>(t...)
		{}
	};
}  // namespace eastl

#endif  // EASTL_TUPLE_ENABLED

#endif  // EASTL_TUPLE_NAMED_H
