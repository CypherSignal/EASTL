///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Electronic Arts Inc. All rights reserved.
///////////////////////////////////////////////////////////////////////////////

#ifndef EASTL_TUPLE_NAMED_H
#define EASTL_TUPLE_NAMED_H

#include <EASTL/internal/config.h>
#include <EASTL/tuple.h>
#include <EASTL/tuple_vector.h>

#if EASTL_TUPLE_ENABLED

namespace eastl
{
	//////////////////////////////////////////////////////////////////////////
	// helpers for named_tags

	constexpr unsigned long hashCalc(const char* ch, size_t len)
	{
		return len > 0 ? ((unsigned long)(*ch) + hashCalc(ch + 1, len - 1) * 33) % (1 << 26) : 0;
	}
	constexpr unsigned long operator "" _tn(const char* ch, size_t len)
	{
		return hashCalc(ch, len);
	}

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

	// dcrooks-todo trying to make tuple_named_tag convertible between, e.g., T and T&
	template<typename T, size_t N>
	class tuple_named_tag { 
	public:

		tuple_named_tag(const T& t)
			: m_t(t)
		{}

		template <typename OtherT, size_t OtherN>
			tuple_named_tag(tuple_named_tag<OtherT, OtherN>&& t)
			: m_t(forward<OtherT>(t.m_t))
		{}


		T m_t;
	};

	template <size_t val, typename... Tags>
	struct index_lookup;

	template <size_t val, typename... Ts, size_t... Ns>
	struct index_lookup<val, tuple_named_tag<Ts, Ns>...>
	{
		static const int index = val > sizeof...(Ts) ? index_lookup_helper<val, Ns...>::index : val;
	};

	//////////////////////////////////////////////////////////////////////////
	// tuple_named

	template<typename... Tags>
	class tuple_named;

	template<typename... Ts, size_t... Ns>
	class tuple_named<tuple_named_tag<Ts, Ns>...> : public tuple<Ts...>
	{
	public:
		typedef tuple<Ts...> TupleType;
	
		tuple_named<tuple_named_tag<Ts, Ns>...>(const Ts&... t)
			:tuple<Ts...>(t...)
		{}

	};

	template <size_t I, typename... Tags>
	auto get(tuple_named<Tags...>& t)
	{
		return get<index_lookup<I, Tags...>::index>(tuple_named<Tags...>::TupleType(t));
	}
	
	//////////////////////////////////////////////////////////////////////////
	// tuple_named_vector and TupleNamedVecIter

	template<typename... Tags>
	class tuple_named_vector;

	template<typename... Tags>
	struct TupleNamedVecIter;

	template <typename... Ts, size_t... Ns>
	struct TupleNamedVecIter<tuple_named_tag<Ts, Ns>...> : public iterator<random_access_iterator_tag,
		tuple_named<tuple_named_tag<Ts, Ns>...>,
		ptrdiff_t,
		tuple_named<tuple_named_tag<Ts*, Ns>...>,
		tuple_named<tuple_named_tag<Ts&, Ns>...>
	>
	{
	private:
		typedef TupleNamedVecIter<tuple_named_tag<Ts, Ns>...> this_type;

	public:
		TupleNamedVecIter() = default;
		TupleNamedVecIter(tuple_named_vector<tuple_named_tag<Ts, Ns>...>& tupleVec, size_t index)
			: mTupleVec(&tupleVec), mIndex(index) { }

		bool operator==(const TupleNamedVecIter& other) const { return mIndex == other.mIndex && mTupleVec->get<0>() == other.mTupleVec->get<0>(); }
		bool operator!=(const TupleNamedVecIter& other) const { return mIndex != other.mIndex || mTupleVec->get<0>() != other.mTupleVec->get<0>(); }
		reference operator*() { return MakeReference(make_index_sequence<sizeof...(Ts)>()); }

		this_type& operator++() { ++mIndex; return *this; }
		this_type operator++(int)
		{
			this_type temp = *this;
			++mIndex;
			return temp;
		}

		this_type& operator--() { --mIndex; return *this; }
		this_type operator--(int)
		{
			this_type temp = *this;
			--mIndex;
			return temp;
		}

		this_type& operator+=(difference_type n) { mIndex += n; return *this; }
		this_type operator+(difference_type n)
		{
			this_type temp = *this;
			return temp += n;
		}
		friend this_type operator+(difference_type n, const this_type& rhs)
		{
			this_type temp = rhs;
			return temp += n;
		}

		this_type& operator-=(difference_type n) { mIndex -= n; return *this; }
		this_type operator-(difference_type n)
		{
			this_type temp = *this;
			return temp -= n;
		}
		friend this_type operator-(difference_type n, const this_type& rhs)
		{
			this_type temp = rhs;
			return temp -= n;
		}

		difference_type operator-(const this_type& rhs) { return mIndex - rhs.mIndex; }
		bool operator<(const this_type& rhs) { return mIndex < rhs.mIndex; }
		bool operator>(const this_type& rhs) { return mIndex > rhs.mIndex; }
		bool operator>=(const this_type& rhs) { return mIndex >= rhs.mIndex; }
		bool operator<=(const this_type& rhs) { return mIndex <= rhs.mIndex; }

		reference operator[](size_t n)
		{
			return *(*this + n);
		}

	private:

		template <size_t... Indices>
		value_type MakeValue(integer_sequence<size_t, Indices...> indices)
		{
			return value_type(mTupleVec->get<Indices>()[mIndex]...);
		}

		template <size_t... Indices>
		reference MakeReference(integer_sequence<size_t, Indices...> indices)
		{
			return reference(mTupleVec->get<Indices>()[mIndex]...);
		}

		template <size_t... Indices>
		pointer MakePointer(integer_sequence<size_t, Indices...> indices)
		{
			return pointer(&mTupleVec->get<Indices>()[mIndex]...);
		}

		size_t mIndex = 0;
		tuple_named_vector<tuple_named_tag<Ts, Ns>...> *mTupleVec = nullptr;
	};

	template<typename... Ts, size_t... Ns>
	class tuple_named_vector<tuple_named_tag<Ts, Ns>...> : public tuple_vector<Ts...>
	{
	public:
		tuple_named_vector<tuple_named_tag<Ts, Ns>...>()
			: tuple_vector<Ts...>()
		{}

		tuple_named_vector<tuple_named_tag<Ts, Ns>...>(const Ts&... t)
			: tuple_vector<Ts...>(t...)
		{}

		typedef tuple_vector<Ts...> TupleVectorType;

		template<size_t I>
		auto get();

		template<typename T>
		T* get()
		{
			return TupleVectorType::get<T>();
		}

		typedef TupleNamedVecIter<tuple_named_tag<Ts, Ns>...> iterator;
		iterator begin()
		{
			return iterator(*this, 0);
		}
		iterator end()
		{
			return iterator(*this, size());
		}
	};

	template<typename... Ts, size_t... Ns>
	template<size_t I>
	auto tuple_named_vector<tuple_named_tag<Ts, Ns>...>::get()
	{
		return TupleVectorType::get<index_lookup<I, tuple_named_tag<Ts, Ns>...>::index>();
	}

}  // namespace eastl

#endif  // EASTL_TUPLE_ENABLED

#endif  // EASTL_TUPLE_NAMED_H
