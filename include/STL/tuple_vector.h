///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Electronic Arts Inc. All rights reserved.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// tuple_vector is a data container that is designed to abstract and simplify
// the handling of a "structure of arrays" layout of data in memory. In
// particular, it mimics the interface of vector, including functionality to do
// inserts, erases, push_backs, and random-access. It also provides a
// RandomAccessIterator and corresponding functionality, making it compatible
// with most STL (and STL-esque) algorithms such as ranged-for loops, find_if,
// remove_if, or sort.

// When used or applied properly, this container can improve performance of
// some algorithms through cache-coherent data accesses or allowing for
// sensible SIMD programming, while keeping the structure of a single
// container, to permit a developer to continue to use existing algorithms in
// STL and the like.
//
// Consult doc/Bonus/tuple_vector_readme.md for more information.
//
// This is a variant of tuple_vector designed to be relatively divorced from
// EASTL so that it can be glued into STL cpp programs easily
//
///////////////////////////////////////////////////////////////////////////////

#ifndef STL_TUPLEVECTOR_H
#define STL_TUPLEVECTOR_H

#pragma once // Some compilers (e.g. VC++) benefit significantly from using this. We've measured 3-4% build speed improvements in apps as a result.

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std_tuple_vector
{

namespace TupleVecInternal
{

	/// iterator_status_flag
	/// 
	/// Defines the validity status of an iterator. This is primarily used for 
	/// iterator validation in debug builds. These are implemented as OR-able 
	/// flags (as opposed to mutually exclusive values) in order to deal with 
	/// the nature of iterator status. In particular, an iterator may be valid
	/// but not dereferencable, as in the case with an iterator to container end().
	/// An iterator may be valid but also dereferencable, as in the case with an
	/// iterator to container begin().
	///
	enum iterator_status_flag
	{
		isf_none = 0x00, /// This is called none and not called invalid because it is not strictly the opposite of invalid.
		isf_valid = 0x01, /// The iterator is valid, which means it is in the range of [begin, end].
		isf_current = 0x02, /// The iterator is valid and points to the same element it did when created. For example, if an iterator points to vector::begin() but an element is inserted at the front, the iterator is valid but not current. Modification of elements in place do not make iterators non-current.
		isf_can_dereference = 0x04  /// The iterator is dereferencable, which means it is in the range of [begin, end). It may or may not be current.
	};

// forward declarations
template <std::size_t I, typename... Ts>
struct tuplevec_element;

template <std::size_t I, typename... Ts>
using tuplevec_element_t = typename tuplevec_element<I, Ts...>::type;

template <typename... Ts>
struct TupleTypes {};

template <typename Allocator, typename Indices, typename... Ts>
class TupleVecImpl;

template <typename... Ts>
struct TupleRecurser;

template <std::size_t I, typename... Ts>
struct TupleIndexRecurser;

template <std::size_t I, typename T>
struct TupleVecLeaf;

template <typename Indices, typename... Ts>
struct TupleVecIter;

// tuplevec_element helper to be able to isolate a type given an index
template <std::size_t I>
struct tuplevec_element<I>
{
	static_assert(I != I, "tuplevec_element index out of range");
};

template <typename T, typename... Ts>
struct tuplevec_element<0, T, Ts...>
{
	tuplevec_element() = delete; // tuplevec_element should only be used for compile-time assistance, and never be instantiated
	typedef T type;
};

template <std::size_t I, typename T, typename... Ts>
struct tuplevec_element<I, T, Ts...>
{
	typedef tuplevec_element_t<I - 1, Ts...> type;
};

// attempt to isolate index given a type
template <typename T, typename TupleVector>
struct tuplevec_index
{
};

template <typename T>
struct tuplevec_index<T, TupleTypes<>>
{
	typedef void DuplicateTypeCheck;
	tuplevec_index() = delete; // tuplevec_index should only be used for compile-time assistance, and never be instantiated
	static const std::size_t index = 0;
};

template <typename T, typename... TsRest>
struct tuplevec_index<T, TupleTypes<T, TsRest...>>
{
	typedef int DuplicateTypeCheck;
	static_assert(std::is_void<typename tuplevec_index<T, TupleTypes<TsRest...>>::DuplicateTypeCheck>::value, "duplicate type T in tuple_vector::get<T>(); unique types must be provided in declaration, or only use get<std::size_t>()");

	static const std::size_t index = 0;
};

template <typename T, typename Ts, typename... TsRest>
struct tuplevec_index<T, TupleTypes<Ts, TsRest...>>
{
	typedef typename tuplevec_index<T, TupleTypes<TsRest...>>::DuplicateTypeCheck DuplicateTypeCheck;
	static const std::size_t index = tuplevec_index<T, TupleTypes<TsRest...>>::index + 1;
};

template <typename Allocator, typename T, typename Indices, typename... Ts>
struct tuplevec_index<T, TupleVecImpl<Allocator, Indices, Ts...>> : public tuplevec_index<T, TupleTypes<Ts...>>
{
};


// helper to calculate the layout of the allocations for the tuple of types (esp. to take alignment into account)
template <>
struct TupleRecurser<>
{
	typedef std::size_t size_type;

	// This class should never be instantiated. This is just a helper for working with static functions when anonymous functions don't work
	// and provide some other utilities
	TupleRecurser() = delete;
		
	static constexpr size_type GetTotalAlignment()
	{
		return 0;
	}

	static constexpr size_type GetTotalAllocationSize(size_type capacity, size_type offset)
	{
		return offset;
	}

	template<typename Allocator, size_type I, typename Indices, typename... VecTypes>
	static std::pair<void*, size_type> DoAllocate(TupleVecImpl<Allocator, Indices, VecTypes...> &vec, void** ppNewLeaf, size_type capacity, size_type offset)
	{
		// If n is zero, then we allocate no memory and just return NULL. 
		// This is fine, as our default ctor initializes with NULL pointers. 
		size_type alignment = TupleRecurser<VecTypes...>::GetTotalAlignment();
		void* ptr = capacity ? vec.internalAllocator().allocate(offset) : nullptr;
		return std::make_pair(ptr, offset);
	}

	template<typename TupleVecImplType, size_type I>
	static void SetNewData(TupleVecImplType &vec, void* pData, size_type capacity, size_type offset) 
	{ }
};

template <typename T, typename... Ts>
struct TupleRecurser<T, Ts...> : TupleRecurser<Ts...>
{
	typedef std::size_t size_type;

	static constexpr size_type GetTotalAlignment()
	{
		return std::max(alignof(T), TupleRecurser<Ts...>::GetTotalAlignment());
	}

	static constexpr size_type GetTotalAllocationSize(size_type capacity, size_type offset)
	{
		return TupleRecurser<Ts...>::GetTotalAllocationSize(capacity, CalculateAllocationSize(offset, capacity));
	}

	template<typename Allocator, size_type I, typename Indices, typename... VecTypes>
	static std::pair<void*, size_type> DoAllocate(TupleVecImpl<Allocator, Indices, VecTypes...> &vec, void** ppNewLeaf, size_type capacity, size_type offset)
	{
		std::size_t allocationOffset = CalculatAllocationOffset(offset);
		std::size_t allocationSize = CalculateAllocationSize(offset, capacity);
		std::pair<void*, size_type> allocation = TupleRecurser<Ts...>::template DoAllocate<Allocator, I + 1, Indices, VecTypes...>(
			vec, ppNewLeaf, capacity, allocationSize);
		ppNewLeaf[I] = (void*)((uintptr_t)(allocation.first) + allocationOffset);
		return allocation;
	}

	template<typename TupleVecImplType, size_type I>
	static void SetNewData(TupleVecImplType &vec, void* pData, size_type capacity, size_type offset)
	{
		std::size_t allocationOffset = CalculatAllocationOffset(offset);
		std::size_t allocationSize = CalculateAllocationSize(offset, capacity);
		vec.TupleVecLeaf<I, T>::mpData = (T*)((uintptr_t)pData + allocationOffset);
		TupleRecurser<Ts...>::template SetNewData<TupleVecImplType, I + 1>(vec, pData, capacity, allocationSize);
	}

	struct alignas(GetTotalAlignment()) AlignedData
	{
		char data;
	};

private:
	static constexpr size_type CalculateAllocationSize(size_type offset, size_type capacity)
	{
		return CalculatAllocationOffset(offset) + sizeof(T) * capacity;
	}

	static constexpr std::size_t CalculatAllocationOffset(std::size_t offset) { return (offset + alignof(T) - 1) & (~alignof(T) + 1); }
};

template <std::size_t I, typename T>
struct TupleVecLeaf
{
	typedef std::size_t size_type;

	void DoUninitializedMoveAndDestruct(const size_type begin, const size_type end, T* pDest)
	{
		T* pBegin = mpData + begin;
		T* pEnd = mpData + end;
		std::uninitialized_move(pBegin, pEnd, pDest);
		std::destroy(pBegin, pEnd);
	}

	void DoInsertAndFill(size_type pos, size_type n, size_type numElements, const T& arg)
	{
		T* pDest = mpData + pos;
		T* pDataEnd = mpData + numElements;
		const T temp = arg;
		const size_type nExtra = (numElements - pos);
		if (n < nExtra) // If the inserted values are entirely within initialized memory (i.e. are before mpEnd)...
		{
			std::uninitialized_move(pDataEnd - n, pDataEnd, pDataEnd);
			std::move_backward(pDest, pDataEnd - n, pDataEnd); // We need move_backward because of potential overlap issues.
			std::fill(pDest, pDest + n, temp);
		}
		else
		{
			std::uninitialized_fill_n(pDataEnd, n - nExtra, temp);
			std::uninitialized_move(pDest, pDataEnd, pDataEnd + n - nExtra);
			std::fill(pDest, pDataEnd, temp);
		}
	}

	void DoInsertRange(T* pSrcBegin, T* pSrcEnd, T* pDestBegin, size_type numDataElements)
	{
		size_type pos = pDestBegin - mpData;
		size_type n = pSrcEnd - pSrcBegin;
		T* pDataEnd = mpData + numDataElements;
		const size_type nExtra = numDataElements - pos;
		if (n < nExtra) // If the inserted values are entirely within initialized memory (i.e. are before mpEnd)...
		{
			std::uninitialized_move(pDataEnd - n, pDataEnd, pDataEnd);
			std::move_backward(pDestBegin, pDataEnd - n, pDataEnd); // We need move_backward because of potential overlap issues.
			std::copy(pSrcBegin, pSrcEnd, pDestBegin);
		}
		else
		{
			std::uninitialized_copy(pSrcEnd - (n - nExtra), pSrcEnd, pDataEnd);
			std::uninitialized_move(pDestBegin, pDataEnd, pDataEnd + n - nExtra);
			std::copy(pSrcBegin, pSrcEnd - (n - nExtra), pDestBegin);
		}
	}

	void DoInsertValue(size_type pos, size_type numElements, T&& arg)
	{
		T* pDest = mpData + pos;
		T* pDataEnd = mpData + numElements;

		std::uninitialized_move(pDataEnd - 1, pDataEnd, pDataEnd);
		std::move_backward(pDest, pDataEnd - 1, pDataEnd); // We need move_backward because of potential overlap issues.
		std::destroy_at(pDest);
		::new (pDest) T(std::forward<T>(arg));
	}

	T* mpData = nullptr;
};

// swallow allows for parameter pack expansion of arguments as means of expanding operations performed
// if a void function is used for operation expansion, it should be wrapped in (..., 0) so that the compiler
// thinks it has a parameter to pass into the function
template <typename... Ts>
void swallow(Ts&&...) { }

inline bool variadicAnd(bool cond) { return cond; }

inline bool variadicAnd(bool cond, bool conds...) { return cond && variadicAnd(conds); }

// Helper struct to check for strict compatibility between two iterators, whilst still allowing for
// conversion between TupleVecImpl<Ts...>::iterator and TupleVecImpl<Ts...>::const_iterator. 
template <bool IsSameSize, typename From, typename To>
struct TupleVecIterCompatibleImpl : public std::false_type { };
	
template<>
struct TupleVecIterCompatibleImpl<true, TupleTypes<>, TupleTypes<>> : public std::true_type { };

template <typename From, typename... FromRest, typename To, typename... ToRest>
struct TupleVecIterCompatibleImpl<true, TupleTypes<From, FromRest...>, TupleTypes<To, ToRest...>> : public std::integral_constant<bool,
		TupleVecIterCompatibleImpl<true, TupleTypes<FromRest...>, TupleTypes<ToRest...>>::value &&
		std::is_same<typename std::remove_const<From>::type, typename std::remove_const<To>::type>::value >
{ };

template <typename From, typename To>
struct TupleVecIterCompatible;

template<typename... Us, typename... Ts>
struct TupleVecIterCompatible<TupleTypes<Us...>, TupleTypes<Ts...>> :
	public TupleVecIterCompatibleImpl<sizeof...(Us) == sizeof...(Ts), TupleTypes<Us...>, TupleTypes<Ts...>>
{ };

// The Iterator operates by storing a persistent index internally,
// and resolving the tuple of pointers to the various parts of the original tupleVec when dereferenced.
// While resolving the tuple is a non-zero operation, it consistently generated better code than the alternative of
// storing - and harmoniously updating on each modification - a full tuple of pointers to the tupleVec's data
template <std::size_t... Indices, typename... Ts>
struct TupleVecIter<std::index_sequence<Indices...>, Ts...>
{
private:
	typedef TupleVecIter<std::index_sequence<Indices...>, Ts...> this_type;
	typedef std::size_t size_type;

	template<typename U, typename... Us> 
	friend struct TupleVecIter;

	template<typename U, typename V, typename... Us>
	friend class TupleVecImpl;

	template<typename U>
	friend class std::move_iterator;
public:
	typedef std::random_access_iterator_tag iterator_category;
	typedef std::tuple<Ts...> value_type;
	typedef std::size_t difference_type;
	typedef std::tuple<Ts*...> pointer;
	typedef std::tuple<Ts&...> reference;

	TupleVecIter() = default;

	template<typename VecImplType>
	TupleVecIter(VecImplType* tupleVec, size_type index)
		: mIndex(index)
		, mpData{(void*)tupleVec->TupleVecLeaf<Indices, Ts>::mpData...}
	{ }

	template <typename OtherIndicesType, typename... Us,
			  typename = typename std::enable_if<TupleVecIterCompatible<TupleTypes<Us...>, TupleTypes<Ts...>>::value, bool>::type>
	TupleVecIter(const TupleVecIter<OtherIndicesType, Us...>& other)
		: mIndex(other.mIndex)
		, mpData{other.mpData[Indices]...}
	{
	}

	bool operator==(const TupleVecIter& other) const { return mIndex == other.mIndex && mpData[0] == other.mpData[0]; }
	bool operator!=(const TupleVecIter& other) const { return mIndex != other.mIndex || mpData[0] != other.mpData[0]; }
	reference operator*() const { return MakeReference(); }

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
	this_type operator+(difference_type n) const
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
	this_type operator-(difference_type n) const
	{
		this_type temp = *this;
		return temp -= n;
	}
	friend this_type operator-(difference_type n, const this_type& rhs)
	{
		this_type temp = rhs;
		return temp -= n;
	}

	difference_type operator-(const this_type& rhs) const { return mIndex - rhs.mIndex; }
	bool operator<(const this_type& rhs) const { return mIndex < rhs.mIndex; }
	bool operator>(const this_type& rhs) const { return mIndex > rhs.mIndex; }
	bool operator>=(const this_type& rhs) const { return mIndex >= rhs.mIndex; }
	bool operator<=(const this_type& rhs) const { return mIndex <= rhs.mIndex; }

	reference operator[](const size_type n) const
	{
		return *(*this + n);
	}

private:

	value_type MakeValue() const
	{
		return value_type(((Ts*)mpData[Indices])[mIndex]...);
	}

	reference MakeReference() const
	{
		return reference(((Ts*)mpData[Indices])[mIndex]...);
	}

	pointer MakePointer() const
	{
		return pointer(&((Ts*)mpData[Indices])[mIndex]...);
	}

	size_type mIndex = 0;
	const void* mpData[sizeof...(Ts)];
};

// TupleVecImpl
template <typename Allocator, std::size_t... Indices, typename... Ts>
class TupleVecImpl<Allocator, std::index_sequence<Indices...>, Ts...> : public TupleVecLeaf<Indices, Ts>...
{
	typedef Allocator	allocator_type;
	typedef std::index_sequence<Indices...> index_sequence_type;
	typedef TupleVecImpl<Allocator, index_sequence_type, Ts...> this_type;
	typedef TupleVecImpl<Allocator, index_sequence_type, const Ts...> const_this_type;

public:
	typedef TupleVecInternal::TupleVecIter<index_sequence_type, Ts...> iterator;
	typedef TupleVecInternal::TupleVecIter<index_sequence_type, const Ts...> const_iterator;
	typedef std::reverse_iterator<iterator> reverse_iterator;
	typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
	typedef std::size_t size_type;
	typedef std::tuple<Ts...> value_tuple;
	typedef std::tuple<Ts&...> reference_tuple;
	typedef std::tuple<const Ts&...> const_reference_tuple;
	typedef std::tuple<Ts*...> ptr_tuple;
	typedef std::tuple<const Ts*...> const_ptr_tuple;
	typedef std::tuple<Ts&&...> rvalue_tuple;

	TupleVecImpl()
		: mDataSizeAndAllocator(0, allocator_type())
	{}

	TupleVecImpl(const allocator_type& allocator)
		: mDataSizeAndAllocator(0, allocator)
	{}

	TupleVecImpl(this_type&& x)
		: mDataSizeAndAllocator(0, std::move(x.internalAllocator()))
	{
		swap(x);
	}

	TupleVecImpl(this_type&& x, const Allocator& allocator) 
		: mDataSizeAndAllocator(0, allocator)
	{
		if (internalAllocator() == x.internalAllocator()) // If allocators are equivalent, then we can safely swap member-by-member
		{
			swap(x);
		}
		else
		{
			this_type temp(std::move(*this));
			temp.swap(x);
		}
	}

	TupleVecImpl(const this_type& x) 
		: mDataSizeAndAllocator(0, x.internalAllocator())
	{
		DoInitFromIterator(x.begin(), x.end());
	}

	template<typename OtherAllocator>
	TupleVecImpl(const TupleVecImpl<OtherAllocator, index_sequence_type, Ts...>& x, const Allocator& allocator)  
		: mDataSizeAndAllocator(0, allocator)
	{
		DoInitFromIterator(x.begin(), x.end());
	}

	template<typename MoveIterBase>
	TupleVecImpl(std::move_iterator<MoveIterBase> begin, std::move_iterator<MoveIterBase> end, const allocator_type& allocator = allocator_type())
		: mDataSizeAndAllocator(0, allocator)
	{
		DoInitFromIterator(begin, end);
	}

	TupleVecImpl(const_iterator begin, const_iterator end, const allocator_type& allocator = allocator_type())
		: mDataSizeAndAllocator(0, allocator )
	{
		DoInitFromIterator(begin, end);
	}

	TupleVecImpl(size_type n, const allocator_type& allocator = allocator_type())
		: mDataSizeAndAllocator(0, allocator)
	{
		DoInitDefaultFill(n);
	}

	TupleVecImpl(size_type n, const Ts&... args) 
		: mDataSizeAndAllocator(0, allocator_type())
	{
		DoInitFillArgs(n, args...);
	}

	TupleVecImpl(size_type n, const Ts&... args, const allocator_type& allocator) 
		: mDataSizeAndAllocator(0, allocator)
	{
		DoInitFillArgs(n, args...);
	}

	TupleVecImpl(size_type n, const_reference_tuple tup, const allocator_type& allocator = allocator_type())
		: mDataSizeAndAllocator(0, allocator)
	{
		DoInitFillTuple(n, tup);
	}

	TupleVecImpl(const value_tuple* first, const value_tuple* last, const allocator_type& allocator = allocator_type())
		: mDataSizeAndAllocator(0, allocator)
	{
		DoInitFromTupleArray(first, last);
	}

	TupleVecImpl(std::initializer_list<value_tuple> iList, const allocator_type& allocator = allocator_type())
		: mDataSizeAndAllocator(0, allocator)
	{
		DoInitFromTupleArray(iList.begin(), iList.end());
	}

protected:
	// ctor to provide a pre-allocated field of data that the container will own, specifically for fixed_tuple_vector
	TupleVecImpl(const allocator_type& allocator, void* pData, size_type capacity, size_type dataSize)
		: mpData(pData), mNumCapacity(capacity), mDataSizeAndAllocator(dataSize, allocator)
	{
		TupleRecurser<Ts...>::template SetNewData<this_type, 0>(*this, mpData, mNumCapacity, 0);
	}

public:
	~TupleVecImpl()
	{ 
		swallow((std::destroy(TupleVecLeaf<Indices, Ts>::mpData, TupleVecLeaf<Indices, Ts>::mpData + mNumElements), 0)...);
		if (mpData)
			internalAllocator().deallocate((typename TupleRecurser<Ts...>::AlignedData*)mpData, internalDataSize()); 
	}

	void assign(size_type n, const Ts&... args)
	{
		if (n > mNumCapacity)
		{
			this_type temp(n, args..., internalAllocator()); // We have little choice but to reallocate with new memory.
			swap(temp);
		}
		else if (n > mNumElements) // If n > mNumElements ...
		{
			size_type oldNumElements = mNumElements;
			swallow((std::fill(TupleVecLeaf<Indices, Ts>::mpData, TupleVecLeaf<Indices, Ts>::mpData + oldNumElements, args), 0)...);
			swallow((std::uninitialized_fill(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements,
					                       TupleVecLeaf<Indices, Ts>::mpData + n, args), 0)...);
			mNumElements = n;
		}
		else // else 0 <= n <= mNumElements
		{
			swallow((std::fill(TupleVecLeaf<Indices, Ts>::mpData, TupleVecLeaf<Indices, Ts>::mpData + n, args), 0)...);
			erase(begin() + n, end());
		}
	}

	void assign(const_iterator first, const_iterator last)
	{
		assert(validate_iterator_pair(first, last));
		size_type newNumElements = last - first;
		if (newNumElements > mNumCapacity)
		{
			this_type temp(first, last, internalAllocator());
			swap(temp);
		}
		else
		{
			const void* ppOtherData[sizeof...(Ts)] = {first.mpData[Indices]...};
			size_type firstIdx = first.mIndex;
			size_type lastIdx = last.mIndex;
			if (newNumElements > mNumElements) // If n > mNumElements ...
			{
				size_type oldNumElements = mNumElements;
				swallow((std::copy((Ts*)(ppOtherData[Indices]) + firstIdx,
						       (Ts*)(ppOtherData[Indices]) + firstIdx + oldNumElements,
						       TupleVecLeaf<Indices, Ts>::mpData), 0)...);
				swallow((std::uninitialized_copy((Ts*)(ppOtherData[Indices]) + firstIdx + oldNumElements,
						                       (Ts*)(ppOtherData[Indices]) + lastIdx,
						                       TupleVecLeaf<Indices, Ts>::mpData + oldNumElements), 0)...);
				mNumElements = newNumElements;
			}
			else // else 0 <= n <= mNumElements
			{
				swallow((std::copy((Ts*)(ppOtherData[Indices]) + firstIdx, (Ts*)(ppOtherData[Indices]) + lastIdx,
						       TupleVecLeaf<Indices, Ts>::mpData), 0)...);
				erase(begin() + newNumElements, end());
			}
		}
	}

	void assign(const value_tuple* first, const value_tuple* last)
	{
		assert(first <= last && first != nullptr && last != nullptr);
		size_type newNumElements = last - first;
		if (newNumElements > mNumCapacity)
		{
			this_type temp(first, last, internalAllocator());
			swap(temp);
		}
		else
		{
			if (newNumElements > mNumElements) // If n > mNumElements ...
			{
				size_type oldNumElements = mNumElements;
				
				DoCopyFromTupleArray(begin(), begin() + oldNumElements, first);
				DoUninitializedCopyFromTupleArray(begin() + oldNumElements, begin() + newNumElements, first);
				mNumElements = newNumElements;
			}
			else // else 0 <= n <= mNumElements
			{
				DoCopyFromTupleArray(begin(), begin() + newNumElements, first);
				erase(begin() + newNumElements, end());
			}
		}
	}

	reference_tuple push_back()
	{
		size_type oldNumElements = mNumElements;
		size_type newNumElements = oldNumElements + 1;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		DoGrow(oldNumElements, oldNumCapacity, newNumElements);
		swallow(::new(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements) Ts()...);
		return back();
	}

	void push_back(const Ts&... args)
	{
		size_type oldNumElements = mNumElements;
		size_type newNumElements = oldNumElements + 1;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		DoGrow(oldNumElements, oldNumCapacity, newNumElements);
		swallow(::new(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements) Ts(args)...);
	}

	void push_back_uninitialized()
	{
		size_type oldNumElements = mNumElements;
		size_type newNumElements = oldNumElements + 1;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		DoGrow(oldNumElements, oldNumCapacity, newNumElements);
	}
	
	reference_tuple emplace_back(Ts&&... args)
	{
		size_type oldNumElements = mNumElements;
		size_type newNumElements = oldNumElements + 1;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		DoGrow(oldNumElements, oldNumCapacity, newNumElements);
		swallow(::new(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements) Ts(std::forward<Ts>(args))...);
		return back();
	}

	iterator emplace(const_iterator pos, Ts&&... args)
	{
		assert(validate_iterator(pos) != isf_none);
		size_type firstIdx = pos - cbegin();
		size_type oldNumElements = mNumElements;
		size_type newNumElements = mNumElements + 1;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		if (newNumElements > oldNumCapacity || firstIdx != oldNumElements)
		{
			if (newNumElements > oldNumCapacity)
			{
				const size_type newCapacity = std::max(GetNewCapacity(oldNumCapacity), newNumElements);

				void* ppNewLeaf[sizeof...(Ts)];
				std::pair<void*, size_type> allocation = TupleRecurser<Ts...>::template DoAllocate<allocator_type, 0, index_sequence_type, Ts...>(
					*this, ppNewLeaf, newCapacity, 0);

				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					0, firstIdx, (Ts*)ppNewLeaf[Indices]), 0)...);
				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					firstIdx, oldNumElements, (Ts*)ppNewLeaf[Indices] + firstIdx + 1), 0)...);
				swallow(::new ((Ts*)ppNewLeaf[Indices] + firstIdx) Ts(std::forward<Ts>(args))...);
				swallow(TupleVecLeaf<Indices, Ts>::mpData = (Ts*)ppNewLeaf[Indices]...);

				internalAllocator().deallocate((typename TupleRecurser<Ts...>::AlignedData*)mpData, internalDataSize());
				mpData = allocation.first;
				mNumCapacity = newCapacity;
				internalDataSize() = allocation.second;
			}
			else
			{
				swallow((TupleVecLeaf<Indices, Ts>::DoInsertValue(firstIdx, oldNumElements, std::forward<Ts>(args)), 0)...);
			}
		}
		else
		{
			swallow(::new (TupleVecLeaf<Indices, Ts>::mpData + oldNumElements) Ts(std::forward<Ts>(args))...);
		}
		return begin() + firstIdx;
	}

	iterator insert(const_iterator pos, size_type n, const Ts&... args)
	{
		assert(validate_iterator(pos) != isf_none);
		size_type firstIdx = pos - cbegin();
		size_type lastIdx = firstIdx + n;
		size_type oldNumElements = mNumElements;
		size_type newNumElements = mNumElements + n;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		if (newNumElements > oldNumCapacity || firstIdx != oldNumElements)
		{
			if (newNumElements > oldNumCapacity)
			{
				const size_type newCapacity = std::max(GetNewCapacity(oldNumCapacity), newNumElements);

				void* ppNewLeaf[sizeof...(Ts)];
				std::pair<void*, size_type> allocation = TupleRecurser<Ts...>::template DoAllocate<allocator_type, 0, index_sequence_type, Ts...>(
						*this, ppNewLeaf, newCapacity, 0);

				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					0, firstIdx, (Ts*)ppNewLeaf[Indices]), 0)...);
				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					firstIdx, oldNumElements, (Ts*)ppNewLeaf[Indices] + lastIdx), 0)...);
				swallow((std::uninitialized_fill((Ts*)ppNewLeaf[Indices] + firstIdx, (Ts*)ppNewLeaf[Indices] + lastIdx, args), 0)...);
				swallow(TupleVecLeaf<Indices, Ts>::mpData = (Ts*)ppNewLeaf[Indices]...);
		
				internalAllocator().deallocate((typename TupleRecurser<Ts...>::AlignedData*)mpData, internalDataSize());
				mpData = allocation.first;
				mNumCapacity = newCapacity;
				internalDataSize() = allocation.second;
			}
			else
			{
				swallow((TupleVecLeaf<Indices, Ts>::DoInsertAndFill(firstIdx, n, oldNumElements, args), 0)...);
			}
		}
		else
		{
			swallow((std::uninitialized_fill(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements,
					                       TupleVecLeaf<Indices, Ts>::mpData + newNumElements, args), 0)...);
		}
		return begin() + firstIdx;
	}

	iterator insert(const_iterator pos, const_iterator first, const_iterator last)
	{
		assert(validate_iterator(pos) != isf_none);
		assert(validate_iterator_pair(first, last));
		size_type posIdx = pos - cbegin();
		size_type firstIdx = first.mIndex;
		size_type lastIdx = last.mIndex;
		size_type numToInsert = last - first;
		size_type oldNumElements = mNumElements;
		size_type newNumElements = oldNumElements + numToInsert;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		const void* ppOtherData[sizeof...(Ts)] = {first.mpData[Indices]...};
		if (newNumElements > oldNumCapacity || posIdx != oldNumElements)
		{
			if (newNumElements > oldNumCapacity)
			{
				const size_type newCapacity = std::max(GetNewCapacity(oldNumCapacity), newNumElements);

				void* ppNewLeaf[sizeof...(Ts)];
				std::pair<void*, size_type> allocation = TupleRecurser<Ts...>::template DoAllocate<allocator_type, 0, index_sequence_type, Ts...>(
						*this, ppNewLeaf, newCapacity, 0);

				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					0, posIdx, (Ts*)ppNewLeaf[Indices]), 0)...);
				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					posIdx, oldNumElements, (Ts*)ppNewLeaf[Indices] + posIdx + numToInsert), 0)...);
				swallow((std::uninitialized_copy((Ts*)(ppOtherData[Indices]) + firstIdx,
						                       (Ts*)(ppOtherData[Indices]) + lastIdx,
						                       (Ts*)ppNewLeaf[Indices] + posIdx), 0)...);
				swallow(TupleVecLeaf<Indices, Ts>::mpData = (Ts*)ppNewLeaf[Indices]...);
				
				internalAllocator().deallocate((typename TupleRecurser<Ts...>::AlignedData*)mpData, internalDataSize());
				mpData = allocation.first;
				mNumCapacity = newCapacity;
				internalDataSize() = allocation.second;
			}
			else
			{
				swallow((TupleVecLeaf<Indices, Ts>::DoInsertRange(
					(Ts*)(ppOtherData[Indices]) + firstIdx, (Ts*)(ppOtherData[Indices]) + lastIdx,
					TupleVecLeaf<Indices, Ts>::mpData + posIdx, oldNumElements), 0)...);
			}
		}
		else
		{
			swallow((std::uninitialized_copy((Ts*)(ppOtherData[Indices]) + firstIdx,
					                       (Ts*)(ppOtherData[Indices]) + lastIdx,
					                       TupleVecLeaf<Indices, Ts>::mpData + posIdx), 0)...);
		}
		return begin() + posIdx;
	}

	iterator insert(const_iterator pos, const value_tuple* first, const value_tuple* last)
	{
		assert(validate_iterator(pos) != isf_none);
		assert(first <= last && first != nullptr && last != nullptr);
		size_type posIdx = pos - cbegin();
		size_type numToInsert = last - first;
		size_type oldNumElements = mNumElements;
		size_type newNumElements = oldNumElements + numToInsert;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = newNumElements;
		if (newNumElements > oldNumCapacity || posIdx != oldNumElements)
		{
			if (newNumElements > oldNumCapacity)
			{
				const size_type newCapacity = std::max(GetNewCapacity(oldNumCapacity), newNumElements);

				void* ppNewLeaf[sizeof...(Ts)];
				std::pair<void*, size_type> allocation = TupleRecurser<Ts...>::template DoAllocate<allocator_type, 0, index_sequence_type, Ts...>(
					*this, ppNewLeaf, newCapacity, 0);

				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					0, posIdx, (Ts*)ppNewLeaf[Indices]), 0)...);
				swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(
					posIdx, oldNumElements, (Ts*)ppNewLeaf[Indices] + posIdx + numToInsert), 0)...);
				
				swallow(TupleVecLeaf<Indices, Ts>::mpData = (Ts*)ppNewLeaf[Indices]...);

				// Do this after mpData is updated so that we can use new iterators
				DoUninitializedCopyFromTupleArray(begin() + posIdx, begin() + posIdx + numToInsert, first);

				internalAllocator().deallocate((typename TupleRecurser<Ts...>::AlignedData*)mpData, internalDataSize());
				mpData = allocation.first;
				mNumCapacity = newCapacity;
				internalDataSize() = allocation.second;
			}
			else
			{
				const size_type nExtra = oldNumElements - posIdx;
				void* ppDataEnd[sizeof...(Ts)] = { (void*)(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements)... };
				void* ppDataBegin[sizeof...(Ts)] = { (void*)(TupleVecLeaf<Indices, Ts>::mpData + posIdx)... };
				if (numToInsert < nExtra) // If the inserted values are entirely within initialized memory (i.e. are before mpEnd)...
				{
					swallow((std::uninitialized_move((Ts*)ppDataEnd[Indices] - numToInsert,
						(Ts*)ppDataEnd[Indices], (Ts*)ppDataEnd[Indices]), 0)...);
					// We need move_backward because of potential overlap issues.
					swallow((std::move_backward((Ts*)ppDataBegin[Indices],
						(Ts*)ppDataEnd[Indices] - numToInsert, (Ts*)ppDataEnd[Indices]), 0)...); 
					
					DoCopyFromTupleArray(pos, pos + numToInsert, first);
				}
				else
				{
					size_type numToInitialize = numToInsert - nExtra;
					swallow((std::uninitialized_move((Ts*)ppDataBegin[Indices],
						(Ts*)ppDataEnd[Indices], (Ts*)ppDataEnd[Indices] + numToInitialize), 0)...);
					
					DoCopyFromTupleArray(pos, begin() + oldNumElements, first);
					DoUninitializedCopyFromTupleArray(begin() + oldNumElements, pos + numToInsert, first + nExtra);
				}
			}
		}
		else
		{
			DoUninitializedCopyFromTupleArray(pos, pos + numToInsert, first);
		}
		return begin() + posIdx;
	}

	iterator erase(const_iterator first, const_iterator last)
	{
		assert(validate_iterator(first) != isf_none && validate_iterator(last) != isf_none);
		assert(validate_iterator_pair(first, last));
		if (first != last)
		{
			size_type firstIdx = first - cbegin();
			size_type lastIdx = last - cbegin();
			size_type oldNumElements = mNumElements;
			size_type newNumElements = oldNumElements - (lastIdx - firstIdx);
			mNumElements = newNumElements;
			swallow((std::move(TupleVecLeaf<Indices, Ts>::mpData + lastIdx,
					       TupleVecLeaf<Indices, Ts>::mpData + oldNumElements,
					       TupleVecLeaf<Indices, Ts>::mpData + firstIdx), 0)...);
			swallow((std::destroy(TupleVecLeaf<Indices, Ts>::mpData + newNumElements,
					           TupleVecLeaf<Indices, Ts>::mpData + oldNumElements), 0)...);
		}
		return begin() + first.mIndex;
	}
	
	iterator erase_unsorted(const_iterator pos)
	{
		assert(validate_iterator(pos) != isf_none);
		size_type oldNumElements = mNumElements;
		size_type newNumElements = oldNumElements - 1;
		mNumElements = newNumElements;
		swallow((std::move(TupleVecLeaf<Indices, Ts>::mpData + newNumElements,
				       TupleVecLeaf<Indices, Ts>::mpData + oldNumElements,
				       TupleVecLeaf<Indices, Ts>::mpData + (pos - begin())), 0)...);
		swallow((std::destroy(TupleVecLeaf<Indices, Ts>::mpData + newNumElements,
				           TupleVecLeaf<Indices, Ts>::mpData + oldNumElements), 0)...);
		return begin() + pos.mIndex;
	}

	void resize(size_type n)
	{
		size_type oldNumElements = mNumElements;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = n;
		if (n > oldNumElements)
		{
			if (n > oldNumCapacity)
			{
				DoReallocate(oldNumElements, std::max<size_type>(GetNewCapacity(oldNumCapacity), n));
			}
			swallow((std::uninitialized_default_construct_n(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements, n - oldNumElements), 0)...);
		}
		else
		{
			swallow((std::destroy(TupleVecLeaf<Indices, Ts>::mpData + n,
					           TupleVecLeaf<Indices, Ts>::mpData + oldNumElements), 0)...);
		}
	}

	void resize(size_type n, const Ts&... args)
	{
		size_type oldNumElements = mNumElements;
		size_type oldNumCapacity = mNumCapacity;
		mNumElements = n;
		if (n > oldNumElements)
		{
			if (n > oldNumCapacity)
			{
				DoReallocate(oldNumElements, std::max<size_type>(GetNewCapacity(oldNumCapacity), n));
			} 
			swallow((std::uninitialized_fill(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements,
					                       TupleVecLeaf<Indices, Ts>::mpData + n, args), 0)...);
		}
		else
		{
			swallow((std::destroy(TupleVecLeaf<Indices, Ts>::mpData + n,
					           TupleVecLeaf<Indices, Ts>::mpData + oldNumElements), 0)...);
		}
	}

	void reserve(size_type n)
	{
		DoConditionalReallocate(mNumElements, mNumCapacity, n);
	}

	void shrink_to_fit()
	{
		this_type temp(std::move_iterator<iterator>(begin()),std::move_iterator<iterator>(end()), internalAllocator());
		swap(temp);
	}

	void clear() noexcept
	{
		size_type oldNumElements = mNumElements;
		mNumElements = 0;
		swallow((std::destroy(TupleVecLeaf<Indices, Ts>::mpData, TupleVecLeaf<Indices, Ts>::mpData + oldNumElements), 0)...);
	}

	void pop_back()
	{
		assert(mNumElements > 0);
		size_type oldNumElements = mNumElements--;
		swallow((std::destroy(TupleVecLeaf<Indices, Ts>::mpData + oldNumElements - 1,
				           TupleVecLeaf<Indices, Ts>::mpData + oldNumElements), 0)...);
	}

	void swap(this_type& x)
	{
		swallow((std::swap(TupleVecLeaf<Indices, Ts>::mpData, x.TupleVecLeaf<Indices, Ts>::mpData), 0)...);
		std::swap(mpData, x.mpData);
		std::swap(mNumElements, x.mNumElements);
		std::swap(mNumCapacity, x.mNumCapacity);
		std::swap(internalAllocator(), x.internalAllocator());
		std::swap(internalDataSize(), x.internalDataSize());
	}

	void assign(size_type n, const_reference_tuple tup) { assign(n, std::get<Indices>(tup)...); }
	void assign(std::initializer_list<value_tuple> iList) { assign(iList.begin(), iList.end()); }

	void push_back(Ts&&... args) { emplace_back(std::forward<Ts>(args)...); }
	void push_back(const_reference_tuple tup) { push_back(std::get<Indices>(tup)...); }
	void push_back(rvalue_tuple tup) { emplace_back(std::forward<Ts>(std::get<Indices>(tup))...); }

	void emplace_back(rvalue_tuple tup) { emplace_back(std::forward<Ts>(std::get<Indices>(tup))...); }
	void emplace(const_iterator pos, rvalue_tuple tup) { emplace(pos, std::forward<Ts>(std::get<Indices>(tup))...); }

	iterator insert(const_iterator pos, const Ts&... args) { return insert(pos, 1, args...); }
	iterator insert(const_iterator pos, Ts&&... args) { return emplace(pos, std::forward<Ts>(args)...); }
	iterator insert(const_iterator pos, rvalue_tuple tup) { return emplace(pos, std::forward<Ts>(std::get<Indices>(tup))...); }
	iterator insert(const_iterator pos, const_reference_tuple tup) { return insert(pos, std::get<Indices>(tup)...); }
	iterator insert(const_iterator pos, size_type n, const_reference_tuple tup) { return insert(pos, n, std::get<Indices>(tup)...); }
	iterator insert(const_iterator pos, std::initializer_list<value_tuple> iList) { return insert(pos, iList.begin(), iList.end()); }

	iterator erase(const_iterator pos) { return erase(pos, pos + 1); }
	reverse_iterator erase(const_reverse_iterator pos) { return reverse_iterator(erase((pos + 1).base(), (pos).base())); }
	reverse_iterator erase(const_reverse_iterator first, const_reverse_iterator last) { return reverse_iterator(erase((last).base(), (first).base())); }
	reverse_iterator erase_unsorted(const_reverse_iterator pos) { return reverse_iterator(erase_unsorted((pos + 1).base())); }

	void resize(size_type n, const_reference_tuple tup) { resize(n, std::get<Indices>(tup)...); }

	bool empty() const noexcept { return mNumElements == 0; }
	size_type size() const noexcept { return mNumElements; }
	size_type capacity() const noexcept { return mNumCapacity; }

	iterator begin() noexcept { return iterator(this, 0); }
	const_iterator begin() const noexcept { return const_iterator((const_this_type*)(this), 0); }
	const_iterator cbegin() const noexcept { return const_iterator((const_this_type*)(this), 0); }

	iterator end() noexcept { return iterator(this, size()); }
	const_iterator end() const noexcept { return const_iterator((const_this_type*)(this), size()); }
	const_iterator cend() const noexcept { return const_iterator((const_this_type*)(this), size()); }

	reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
	const_reverse_iterator rbegin() const  noexcept { return const_reverse_iterator(end()); }
	const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
	
	reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
	const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
	const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

	ptr_tuple data() noexcept { return ptr_tuple(TupleVecLeaf<Indices, Ts>::mpData...); }
	const_ptr_tuple data() const noexcept { return const_ptr_tuple(TupleVecLeaf<Indices, Ts>::mpData...); }

	reference_tuple at(size_type n) 
	{ 
		assert(n < mNumElements);
		return reference_tuple(*(TupleVecLeaf<Indices, Ts>::mpData + n)...); 
	}

	const_reference_tuple at(size_type n) const
	{
		assert(n < mNumElements);
		return const_reference_tuple(*(TupleVecLeaf<Indices, Ts>::mpData + n)...); 
	}
	
	reference_tuple operator[](size_type n) { return at(n); }
	const_reference_tuple operator[](size_type n) const { return at(n); }
	
	reference_tuple front() 
	{
		assert(mNumElements > 0);
		return at(0); 
	}

	const_reference_tuple front() const
	{
		assert(mNumElements > 0);
		return at(0); 
	}
	
	reference_tuple back() 
	{
		assert(mNumElements > 0);
		return at(size() - 1); 
	}

	const_reference_tuple back() const 
	{
		assert(mNumElements > 0);
		return at(size() - 1); 
	}

	template <std::size_t I>
	tuplevec_element_t<I, Ts...>* get() 
	{
		typedef tuplevec_element_t<I, Ts...> Element;
		return TupleVecLeaf<I, Element>::mpData;
	}
	template <std::size_t I>
	const tuplevec_element_t<I, Ts...>* get() const
	{
		typedef tuplevec_element_t<I, Ts...> Element;
		return TupleVecLeaf<I, Element>::mpData;
	}

	template <typename T>
	T* get() 
	{ 
		typedef tuplevec_index<T, TupleTypes<Ts...>> Index;
		return TupleVecLeaf<Index::index, T>::mpData;
	}
	template <typename T>
	const T* get() const
	{
		typedef tuplevec_index<T, TupleTypes<Ts...>> Index;
		return TupleVecLeaf<Index::index, T>::mpData;
	}

	this_type& operator=(const this_type& other)
	{
		if (this != &other)
		{
			clear();
			assign(other.begin(), other.end());
		}
		return *this;
	}

	this_type& operator=(this_type&& other)
	{
		if (this != &other)
		{
			swap(other);
		}
		return *this;
	}

	this_type& operator=(std::initializer_list<value_tuple> iList) 
	{
		assign(iList.begin(), iList.end());
		return *this; 
	}

	bool validate() const noexcept
	{
		if (mNumElements > mNumCapacity)
			return false;
		if (!(variadicAnd(mpData <= TupleVecLeaf<Indices, Ts>::mpData...)))
			return false;
		void* pDataEnd = (void*)((uintptr_t)mpData + internalDataSize());
		if (!(variadicAnd(pDataEnd >= TupleVecLeaf<Indices, Ts>::mpData...)))
			return false;
		return true;
	}

	int validate_iterator(const_iterator iter) const noexcept
	{
		if (!(variadicAnd(iter.mpData[Indices] == TupleVecLeaf<Indices, Ts>::mpData...)))
			return isf_none;
		if (iter.mIndex < mNumElements)
			return (isf_valid | isf_current | isf_can_dereference);
		if (iter.mIndex <= mNumElements)
			return (isf_valid | isf_current);
		return isf_none;
	}

	static bool validate_iterator_pair(const_iterator first, const_iterator last) noexcept
	{
		return (first.mIndex <= last.mIndex) && variadicAnd(first.mpData[Indices] == last.mpData[Indices]...);
	}

protected:

	void* mpData = nullptr;
	size_type mNumElements = 0;
	size_type mNumCapacity = 0;

	std::pair<size_type, allocator_type> mDataSizeAndAllocator;

	size_type& internalDataSize() noexcept { return mDataSizeAndAllocator.first; }
	size_type const& internalDataSize() const noexcept { return mDataSizeAndAllocator.first; }
	allocator_type& internalAllocator() noexcept { return mDataSizeAndAllocator.second; }
	const allocator_type& internalAllocator() const noexcept { return mDataSizeAndAllocator.second; }

	friend struct TupleRecurser<>;
	template<typename... Us>
	friend struct TupleRecurser;

	template <typename MoveIterBase>
	void DoInitFromIterator(std::move_iterator<MoveIterBase> begin, std::move_iterator<MoveIterBase> end)
	{
		assert(validate_iterator_pair(begin.base(), end.base()));
		size_type newNumElements = (size_type)(end - begin);
		const void* ppOtherData[sizeof...(Ts)] = { begin.base().mpData[Indices]... };
		size_type beginIdx = begin.base().mIndex;
		size_type endIdx = end.base().mIndex;
		DoConditionalReallocate(0, mNumCapacity, newNumElements);
		mNumElements = newNumElements;
		swallow((std::uninitialized_move(std::move_iterator<Ts*>((Ts*)(ppOtherData[Indices]) + beginIdx),
				                       std::move_iterator<Ts*>((Ts*)(ppOtherData[Indices]) + endIdx),
				                       TupleVecLeaf<Indices, Ts>::mpData), 0)...);
	}

	void DoInitFromIterator(const_iterator begin, const_iterator end)
	{
		assert(validate_iterator_pair(begin, end));
		size_type newNumElements = (size_type)(end - begin);
		const void* ppOtherData[sizeof...(Ts)] = { begin.mpData[Indices]... };
		size_type beginIdx = begin.mIndex;
		size_type endIdx = end.mIndex;
		DoConditionalReallocate(0, mNumCapacity, newNumElements);
		mNumElements = newNumElements;
		swallow((std::uninitialized_copy((Ts*)(ppOtherData[Indices]) + beginIdx,
				                       (Ts*)(ppOtherData[Indices]) + endIdx,
				                       TupleVecLeaf<Indices, Ts>::mpData), 0)...);
	}

	void DoInitFillTuple(size_type n, const_reference_tuple tup) { DoInitFillArgs(n, std::get<Indices>(tup)...); }

	void DoInitFillArgs(size_type n, const Ts&... args)
	{
		DoConditionalReallocate(0, mNumCapacity, n);
		mNumElements = n;
		swallow((std::uninitialized_fill(TupleVecLeaf<Indices, Ts>::mpData, TupleVecLeaf<Indices, Ts>::mpData + n, args), 0)...);
	}

	void DoInitDefaultFill(size_type n)
	{
		DoConditionalReallocate(0, mNumCapacity, n);
		mNumElements = n;
		swallow((std::uninitialized_default_construct_n(TupleVecLeaf<Indices, Ts>::mpData, n), 0)...);
	}

	void DoInitFromTupleArray(const value_tuple* first, const value_tuple* last)
	{
		assert(first <= last && first != nullptr && last != nullptr);
		size_type newNumElements = last - first;
		DoConditionalReallocate(0, mNumCapacity, newNumElements);
		mNumElements = newNumElements;
		DoUninitializedCopyFromTupleArray(begin(), end(), first);
	}

	void DoCopyFromTupleArray(iterator destPos, iterator destEnd, const value_tuple* srcTuple)
	{
		// assign to constructed region
		while (destPos < destEnd)
		{
			*destPos = *srcTuple;
			++destPos;
			++srcTuple;
		}
	}

	void DoUninitializedCopyFromTupleArray(iterator destPos, iterator destEnd, const value_tuple* srcTuple)
	{
		// placement-new/copy-ctor to unconstructed regions
		while (destPos < destEnd)
		{
			swallow(::new(std::get<Indices>(destPos.MakePointer())) Ts(std::get<Indices>(*srcTuple))...);
			++destPos;
			++srcTuple;
		}
	}

	// Try to grow the size of the container "naturally" given the number of elements being used
	void DoGrow(size_type oldNumElements, size_type oldNumCapacity, size_type requiredCapacity)
	{
		if (requiredCapacity > oldNumCapacity)
			DoReallocate(oldNumElements, GetNewCapacity(requiredCapacity));
	}

	// Reallocate to the newCapacity (IFF it's actually larger, though)
	void DoConditionalReallocate(size_type oldNumElements, size_type oldNumCapacity, size_type requiredCapacity)
	{
		if (requiredCapacity > oldNumCapacity)
			DoReallocate(oldNumElements, requiredCapacity);
	}

	void DoReallocate(size_type oldNumElements, size_type requiredCapacity)
	{
		void* ppNewLeaf[sizeof...(Ts)];
		std::pair<void*, size_type> allocation = TupleRecurser<Ts...>::template DoAllocate<allocator_type, 0, index_sequence_type, Ts...>(
			*this, ppNewLeaf, requiredCapacity, 0);
		swallow((TupleVecLeaf<Indices, Ts>::DoUninitializedMoveAndDestruct(0, oldNumElements, (Ts*)ppNewLeaf[Indices]), 0)...);
		swallow(TupleVecLeaf<Indices, Ts>::mpData = (Ts*)ppNewLeaf[Indices]...);

		internalAllocator().deallocate((typename TupleRecurser<Ts...>::AlignedData*)mpData, internalDataSize());
		mpData = allocation.first;
		mNumCapacity = requiredCapacity;
		internalDataSize() = allocation.second;
	}

	size_type GetNewCapacity(size_type oldNumCapacity)
	{
		return (oldNumCapacity > 0) ? (2 * oldNumCapacity) : 1;
	}
};

}  // namespace TupleVecInternal

template <typename AllocatorA, typename AllocatorB, typename Indices, typename... Ts>
inline bool operator==(const TupleVecInternal::TupleVecImpl<AllocatorA, Indices, Ts...>& a,
					   const TupleVecInternal::TupleVecImpl<AllocatorB, Indices, Ts...>& b)
{
	return ((a.size() == b.size()) && equal(a.begin(), a.end(), b.begin()));
}

template <typename AllocatorA, typename AllocatorB, typename Indices, typename... Ts>
inline bool operator!=(const TupleVecInternal::TupleVecImpl<AllocatorA, Indices, Ts...>& a,
					   const TupleVecInternal::TupleVecImpl<AllocatorB, Indices, Ts...>& b)
{
	return ((a.size() != b.size()) || !equal(a.begin(), a.end(), b.begin()));
}

template <typename AllocatorA, typename AllocatorB, typename Indices, typename... Ts>
inline bool operator<(const TupleVecInternal::TupleVecImpl<AllocatorA, Indices, Ts...>& a,
					  const TupleVecInternal::TupleVecImpl<AllocatorB, Indices, Ts...>& b)
{
	return lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

template <typename AllocatorA, typename AllocatorB, typename Indices, typename... Ts>
inline bool operator>(const TupleVecInternal::TupleVecImpl<AllocatorA, Indices, Ts...>& a,
					  const TupleVecInternal::TupleVecImpl<AllocatorB, Indices, Ts...>& b)
{
	return b < a;
}

template <typename AllocatorA, typename AllocatorB,typename Indices, typename... Ts>
inline bool operator<=(const TupleVecInternal::TupleVecImpl<AllocatorA, Indices, Ts...>& a,
					   const TupleVecInternal::TupleVecImpl<AllocatorB, Indices, Ts...>& b)
{
	return !(b < a);
}

template <typename AllocatorA, typename AllocatorB,typename Indices, typename... Ts>
inline bool operator>=(const TupleVecInternal::TupleVecImpl<AllocatorA, Indices, Ts...>& a,
					   const TupleVecInternal::TupleVecImpl<AllocatorB, Indices, Ts...>& b)
{
	return !(a < b);
}

template <typename AllocatorA, typename AllocatorB,typename Indices, typename... Ts>
inline void swap(TupleVecInternal::TupleVecImpl<AllocatorA, Indices, Ts...>& a,
				TupleVecInternal::TupleVecImpl<AllocatorB, Indices, Ts...>& b)
{
	a.swap(b);
}

// External interface of tuple_vector
template <typename... Ts>
class tuple_vector : public TupleVecInternal::TupleVecImpl<
	std::allocator<typename TupleVecInternal::TupleRecurser<Ts...>::AlignedData>,
	std::make_index_sequence<sizeof...(Ts)>, Ts...>
{
	typedef tuple_vector<Ts...> this_type;
	typedef TupleVecInternal::TupleVecImpl<
		std::allocator<typename TupleVecInternal::TupleRecurser<Ts...>::AlignedData>,
		std::make_index_sequence<sizeof...(Ts)>, Ts...> base_type;
	using base_type::base_type;

public:

	this_type& operator=(std::initializer_list<typename base_type::value_tuple> iList) 
	{
		base_type::operator=(iList);
		return *this;
	}
};

// Variant of tuple_vector that allows a user-defined allocator type (can't mix default template params with variadics)
template <typename AllocatorType, typename... Ts>
class tuple_vector_alloc
	: public TupleVecInternal::TupleVecImpl<AllocatorType, std::make_index_sequence<sizeof...(Ts)>, Ts...>
{
	typedef tuple_vector_alloc<AllocatorType, Ts...> this_type;
	typedef TupleVecInternal::TupleVecImpl<AllocatorType, std::make_index_sequence<sizeof...(Ts)>, Ts...> base_type;
	using base_type::base_type;

public:

	this_type& operator=(std::initializer_list<typename base_type::value_tuple> iList)
	{
		base_type::operator=(iList);
		return *this;
	}
};

}  // namespace stl_tuple_vector

namespace std
{
	using namespace std_tuple_vector;

	// Move_iterator specialization for TupleVecIter.
	// An rvalue reference of a move_iterator would normaly be "tuple<Ts...> &&" whereas
	// what we actually want is "tuple<Ts&&...>". This specialization gives us that.
	template <std::size_t... Indices, typename... Ts>
	class move_iterator<TupleVecInternal::TupleVecIter<std::index_sequence<Indices...>, Ts...>>
	{
	public:
		typedef TupleVecInternal::TupleVecIter<std::integer_sequence<std::size_t, Indices...>, Ts...> iterator_type;
		typedef iterator_type wrapped_iterator_type; // This is not in the C++ Standard; it's used by use to identify it as
													 // a wrapping iterator type.
		typedef std::iterator_traits<iterator_type> traits_type;
		typedef typename traits_type::iterator_category iterator_category;
		typedef typename traits_type::value_type value_type;
		typedef typename traits_type::difference_type difference_type;
		typedef typename traits_type::pointer pointer;
		typedef std::tuple<Ts&&...> reference;
		typedef move_iterator<iterator_type> this_type;

	protected:
		iterator_type mIterator;

	public:
		move_iterator() : mIterator() {}
		explicit move_iterator(iterator_type mi) : mIterator(mi) {}

		template <typename U>
		move_iterator(const move_iterator<U>& mi) : mIterator(mi.base()) {}

		iterator_type base() const { return mIterator; }
		reference operator*() const { return std::move(MakeReference()); }
		pointer operator->() const { return mIterator; }

		this_type& operator++() { ++mIterator; return *this; }
		this_type operator++(int) {
			this_type tempMoveIterator = *this;
			++mIterator;
			return tempMoveIterator;
		}

		this_type& operator--() { --mIterator; return *this; }
		this_type operator--(int)
		{
			this_type tempMoveIterator = *this;
			--mIterator;
			return tempMoveIterator;
		}

		this_type operator+(difference_type n) const { return move_iterator(mIterator + n); }
		this_type& operator+=(difference_type n)
		{
			mIterator += n;
			return *this;
		}

		this_type operator-(difference_type n) const { return move_iterator(mIterator - n); }
		this_type& operator-=(difference_type n)
		{
			mIterator -= n;
			return *this;
		}

		difference_type operator-(const this_type& rhs) const { return mIterator.mIndex - rhs.mIterator.mIndex; }
		bool operator<(const this_type& rhs) const { return mIterator.mIndex < rhs.mIterator.mIndex; }
		bool operator>(const this_type& rhs) const { return mIterator.mIndex > rhs.mIterator.mIndex; }
		bool operator>=(const this_type& rhs) const { return mIterator.mIndex >= rhs.mIterator.mIndex; }
		bool operator<=(const this_type& rhs) const { return mIterator.mIndex <= rhs.mIterator.mIndex; }

		reference operator[](difference_type n) const { return *(*this + n); }

	private:
		reference MakeReference() const
		{
			return reference(std::move(((Ts*)mIterator.mpData[Indices])[mIterator.mIndex])...);
		}
	};

	// A customization of swap is made for r-values of tuples-of-references - 
	// normally, swapping rvalues doesn't make sense, but in this case, we do want to 
	// swap the contents of what the tuple-of-references are referring to
	//
	// This is required due to TupleVecIter returning a value-type for its dereferencing,
	// as opposed to an actual real reference of some sort
	template<typename... Ts>
	typename std::enable_if<std::conjunction<std::is_swappable<Ts>...>::value>::type
		swap(std::tuple<Ts&...>&& a, std::tuple<Ts&...>&& b)
	{
		a.swap(b);
	}

	template<typename... Ts>
	typename std::enable_if<!std::conjunction<std::is_swappable<Ts>...>::value>::type
		swap(std::tuple<Ts&...>&& a, std::tuple<Ts&...>&& b) = delete;
}

#endif  // STL_TUPLEVECTOR_H
