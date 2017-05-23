/////////////////////////////////////////////////////////////////////////////
// Copyright (c) Electronic Arts Inc. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "EASTLTest.h"

#include <EASTL/tuple_named.h>

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
		//EATEST_VERIFY(get<"someInt"_tn>(intNamed) == get<0>(intNamed));

/*
		tuple_named<
			tuple_named_tag<int, "someInt"_tn>,
			tuple_named_tag<float, "someFloat"_tn>
			>intFloatNamed(2, 2.0f);
		EATEST_VERIFY(get<int>(intFloatNamed) == 2);
		EATEST_VERIFY(get<float>(intFloatNamed) == 2.0f);
		EATEST_VERIFY(get<"someInt"_tn>(intFloatNamed) == 2);
		EATEST_VERIFY(get<"someFloat"_tn>(intFloatNamed) == 2);

		tuple_named<
			tuple_named_tag<int, "someInt"_tn>,
			tuple_named_tag<int, "someOtherInt"_tn>,
			tuple_named_tag<int, "anotherInt"_tn>
		>multipleIntsNamed(1,2,3);
		EATEST_VERIFY(get<"someOtherInt"_tn>(multipleIntsNamed) == 2);
		EATEST_VERIFY(get<"anotherInt"_tn>(multipleIntsNamed) == 3);

*/

	}

	return nErrorCount;
}

#else

int TestTupleNamed() { return 0; }

#endif  // EASTL_TUPLE_ENABLED

