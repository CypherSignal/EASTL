/////////////////////////////////////////////////////////////////////////////
// Copyright (c) Electronic Arts Inc. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "EASTLTest.h"

#include <EASTL/tuple_named.h>
#include <random>
#include <EASTL/sort.h>
#include <EASTL/array.h>

#if EASTL_TUPLE_ENABLED

int TestTupleNamed()
{
	using namespace eastl;

	int nErrorCount = 0;

	{
		static_assert("a"_tn == (unsigned long)'a', "String literal test failed");
		static_assert("ab"_tn == (unsigned long)'a' + 33 * (int)'b', "String literal test failed");
		static_assert("someInt"_tn == 56441375, "String literal test failed");

		tuple_named<
			tuple_named_tag<int, "someInt"_tn>
			> intNamed(1);
		EATEST_VERIFY(get<int>(intNamed) == 1);
		EATEST_VERIFY(get<0>(intNamed) == 1);
		EATEST_VERIFY(get<"someInt"_tn>(intNamed) == 1);
		EATEST_VERIFY(get<"someInt"_tn>(intNamed) == get<0>(intNamed));


		tuple_named<
			tuple_named_tag<int, "someInt"_tn>,
			tuple_named_tag<float, "someFloat"_tn>
		>intFloatNamed(2, 1.0f);
		EATEST_VERIFY(get<int>(intFloatNamed) == 2);
		EATEST_VERIFY(get<float>(intFloatNamed) == 1.0f);
		EATEST_VERIFY(get<"someInt"_tn>(intFloatNamed) == get<0>(intFloatNamed));
		EATEST_VERIFY(get<"someFloat"_tn>(intFloatNamed) == get<1>(intFloatNamed));

		tuple_named<
			tuple_named_tag<int, "someInt"_tn>,
			tuple_named_tag<int, "someOtherInt"_tn>,
			tuple_named_tag<int, "anotherInt"_tn>
		>multipleIntsNamed(1,2,3);
		EATEST_VERIFY(get<"someOtherInt"_tn>(multipleIntsNamed) == get<1>(multipleIntsNamed));
		EATEST_VERIFY(get<"anotherInt"_tn>(multipleIntsNamed) == 3);


		tuple_vector<int> singleElementVec;
		EATEST_VERIFY(singleElementVec.size() == 0);
		EATEST_VERIFY(singleElementVec.capacity() == 0);
		singleElementVec.push_back_uninitialized();
		singleElementVec.push_back(5);
		EATEST_VERIFY(singleElementVec.size() == 2);
		EATEST_VERIFY(singleElementVec.get<0>()[1] == 5);
		EATEST_VERIFY(singleElementVec.get<int>()[1] == 5);

		tuple_named_vector<
			tuple_named_tag<int, "int"_tn>
		> intNamedVec;
		EATEST_VERIFY(intNamedVec.size() == 0);
		EATEST_VERIFY(intNamedVec.capacity() == 0);
		intNamedVec.push_back_uninitialized();
		intNamedVec.push_back(5);
		EATEST_VERIFY(intNamedVec.size() == 2);
		EATEST_VERIFY(intNamedVec.get<0>()[1] == 5);
		EATEST_VERIFY(intNamedVec.get<int>()[1] == 5);
		EATEST_VERIFY(intNamedVec.get<"int"_tn>()[1] == 5);

	}

	// Test tuple_Vector in a ranged for, and other large-scale iterator testing
	{
		tuple_named_vector<
			tuple_named_tag<int, "int"_tn>,
			tuple_named_tag<float, "fl"_tn>,
			tuple_named_tag<int, "int2"_tn>
			> tripleElementVec;
		tripleElementVec.push_back(1, 2.0f, 6);
		tripleElementVec.push_back(2, 3.0f, 7);
		tripleElementVec.push_back(3, 4.0f, 8);
		tripleElementVec.push_back(4, 5.0f, 9);
		tripleElementVec.push_back(5, 6.0f, 10);


		// test copyConstructible, copyAssignable, swappable, prefix inc, !=, reference convertible to value_type (InputIterator!)
		{
			tuple_named_vector<
				tuple_named_tag<int, "int"_tn>,
				tuple_named_tag<float, "fl"_tn>,
				tuple_named_tag<int, "int2"_tn>
			>::iterator iter = tripleElementVec.begin();
			++iter;
			auto copiedIter(iter);
			EATEST_VERIFY(get<2>(*copiedIter) == 7);
			EATEST_VERIFY(copiedIter == iter);

			++iter;
			copiedIter = iter;
			EATEST_VERIFY(get<2>(*copiedIter) == 8);

			++iter;
			swap(iter, copiedIter);
			EATEST_VERIFY(get<2>(*iter) == 8);
			EATEST_VERIFY(get<2>(*copiedIter) == 9);

			EATEST_VERIFY(copiedIter != iter);

			tuple_named_tag<int, "int"_tn> intTag(5);
			tuple_named_tag<int&, "int"_tn> intRefTag(intTag);
			tuple_named_tag<int, "int"_tn> intOthTag(intRefTag);


			//tuple<int&, float&, int&> ref(*iter);
			//tuple_named<
			//	tuple_named_tag<int, "int"_tn>,
			//	tuple_named_tag<float, "fl"_tn>,
			//	tuple_named_tag<int, "int2"_tn>
			//	> value(*iter);
			//EATEST_VERIFY(get<2>(ref) == get<2>(value));
		}

		// test postfix increment, default constructible (ForwardIterator)
		{
			tuple_named_vector<
				tuple_named_tag<int, "int"_tn>,
				tuple_named_tag<float, "fl"_tn>,
				tuple_named_tag<int, "int2"_tn>
			>::iterator iter = tripleElementVec.begin();
			auto prefixIter = ++iter;

			tuple_named_vector<
				tuple_named_tag<int, "int"_tn>,
				tuple_named_tag<float, "fl"_tn>,
				tuple_named_tag<int, "int2"_tn>
			>::iterator postfixIter;
			postfixIter = iter++;
			EATEST_VERIFY(prefixIter == postfixIter);
			EATEST_VERIFY(get<2>(*prefixIter) == 7);
			EATEST_VERIFY(get<2>(*iter) == 8);
		}

		// test prefix decrement and postfix decrement (BidirectionalIterator)
		{
			tuple_named_vector<
				tuple_named_tag<int, "int"_tn>,
				tuple_named_tag<float, "fl"_tn>,
				tuple_named_tag<int, "int2"_tn>
			>::iterator iter = tripleElementVec.end();
			auto prefixIter = --iter;

			tuple_named_vector<
				tuple_named_tag<int, "int"_tn>,
				tuple_named_tag<float, "fl"_tn>,
				tuple_named_tag<int, "int2"_tn>
			>::iterator postfixIter;
			postfixIter = iter--;
			EATEST_VERIFY(prefixIter == postfixIter);
			EATEST_VERIFY(get<2>(*prefixIter) == 10);
			EATEST_VERIFY(get<2>(*iter) == 9);
		}

		// test many arithmetic operations (RandomAccessIterator)
		{
			tuple_named_vector<
				tuple_named_tag<int, "int"_tn>,
				tuple_named_tag<float, "fl"_tn>,
				tuple_named_tag<int, "int2"_tn>
			>::iterator iter = tripleElementVec.begin();
			auto symmetryOne = iter + 2;
			auto symmetryTwo = 2 + iter;
			iter += 2;
			EATEST_VERIFY(symmetryOne == symmetryTwo);
			EATEST_VERIFY(symmetryOne == iter);

			symmetryOne = iter - 2;
			symmetryTwo = 2 - iter;
			iter -= 2;
			EATEST_VERIFY(symmetryOne == symmetryTwo);
			EATEST_VERIFY(symmetryOne == iter);

			iter += 2;
			EATEST_VERIFY(iter - symmetryOne == 2);

			tuple<int&, float&, int&> symmetryRef = symmetryOne[2];
			EATEST_VERIFY(get<2>(symmetryRef) == get<2>(*iter));

			EATEST_VERIFY(symmetryOne < iter);
			EATEST_VERIFY(iter > symmetryOne);
			EATEST_VERIFY(symmetryOne >= symmetryTwo && iter >= symmetryOne);
			EATEST_VERIFY(symmetryOne <= symmetryTwo && symmetryOne <= iter);
		}

		{
			float i = 0;
			int j = 0;
			EATEST_VERIFY(&get<0>(*tripleElementVec.begin()) == tripleElementVec.get<0>());
			EATEST_VERIFY(&get<1>(*tripleElementVec.begin()) == tripleElementVec.get<1>());
			for (auto& iter : tripleElementVec)
			{
				i += get<1>(iter);
				j += get<2>(iter);
			}
			EATEST_VERIFY(i == 20.0f);
			EATEST_VERIFY(j == 40);
		}
	}

	// test sort.h
	{
		// create+populate the two vectors with some junk
		const int ElementCount = 1 * 1024 * 1024;
		const int NumData = 64;
		struct LargeData
		{
			LargeData(float f)
			{
				data.fill(f);
			}
			eastl::array<float, NumData> data;
		};
		tuple_named_vector<
			tuple_named_tag<bool, "isActive"_tn>,
			tuple_named_tag<LargeData, "payload"_tn>, 
			tuple_named_tag<int, "lifetime"_tn>
		> tripleElementVec;
		tripleElementVec.reserve(ElementCount);

		struct TripleElement
		{
			bool a;
			LargeData b;
			int c;
		};
		vector<TripleElement> aosTripleElement;
		aosTripleElement.reserve(ElementCount);

		std::default_random_engine e1(0);
		std::uniform_int_distribution<int> bool_picker(0, 1);
		std::uniform_real_distribution<float> float_picker(0, 32768);
		std::uniform_int_distribution<int> int_picker(0, 32768);

		for (int i = 0; i < ElementCount; ++i)
		{
			bool randomBool = bool_picker(e1) < 1 ? false : true;
			float randomFloat = float_picker(e1);
			int randomInt = int_picker(e1);
			tripleElementVec.push_back(randomBool, { randomFloat }, randomInt);
			aosTripleElement.push_back({ randomBool,{ randomFloat }, randomInt });
		}


		// measure tuplevec in a loop
		volatile int numTupleBools = 0;
		for (auto& iter : tripleElementVec)
		{
			numTupleBools += get<"isActive"_tn>(iter) ? 1 : 0;
		}

		//// measure tuplevec in a sort
		//sort(tripleElementVec.begin(), tripleElementVec.end(),
		//	[](auto& a, auto& b)
		//{
		//	return get<>(a) > get<2>(b);
		//});

		//// measure vector in a loop
		//volatile int numVecBools = 0;
		//for (auto& iter : aosTripleElement)
		//{
		//	numVecBools += iter.a ? 1 : 0;
		//}

		//// measure vector in a sort
		//sort(aosTripleElement.begin(), aosTripleElement.end(), [](const TripleElement& a, const TripleElement& b)
		//{
		//	return a.c > b.c;
		//});

	}

	return nErrorCount;
}

#else

int TestTupleNamed() { return 0; }

#endif  // EASTL_TUPLE_ENABLED

