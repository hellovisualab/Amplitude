// Minimal self-registering test harness for the engine-agnostic Amplitude core.
#pragma once

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace AmpTest
{
	using FTestFunction = void (*)();

	struct FTestCase
	{
		const char* Name;
		FTestFunction Function;
	};

	std::vector<FTestCase>& GetRegistry();
	void ReportFailure(const char* File, int Line, const std::string& Message);
	void CountCheck();

	struct FRegistrar
	{
		FRegistrar(const char* Name, FTestFunction Function) { GetRegistry().push_back({Name, Function}); }
	};

	template <typename T>
	std::string ToString(const T& Value)
	{
		std::ostringstream Stream;
		Stream << Value;
		return Stream.str();
	}
}

#define AMP_TEST(Name)                                                   \
	static void Name();                                                  \
	static AmpTest::FRegistrar Name##Registrar(#Name, &Name);            \
	static void Name()

#define EXPECT_TRUE(Condition)                                                     \
	do                                                                             \
	{                                                                              \
		AmpTest::CountCheck();                                                     \
		if (!(Condition))                                                          \
		{                                                                          \
			AmpTest::ReportFailure(__FILE__, __LINE__, "expected true: " #Condition); \
		}                                                                          \
	} while (0)

#define EXPECT_FALSE(Condition) EXPECT_TRUE(!(Condition))

#define EXPECT_EQ(Actual, Expected)                                                                                   \
	do                                                                                                                \
	{                                                                                                                 \
		AmpTest::CountCheck();                                                                                        \
		const auto AmpActual = (Actual);                                                                              \
		const auto AmpExpected = (Expected);                                                                          \
		if (!(AmpActual == AmpExpected))                                                                              \
		{                                                                                                             \
			AmpTest::ReportFailure(__FILE__, __LINE__, std::string(#Actual " == " #Expected " (got ") +              \
				AmpTest::ToString(AmpActual) + ", expected " + AmpTest::ToString(AmpExpected) + ")");                 \
		}                                                                                                             \
	} while (0)

#define EXPECT_NEAR(Actual, Expected, Tolerance)                                                                      \
	do                                                                                                                \
	{                                                                                                                 \
		AmpTest::CountCheck();                                                                                        \
		const double AmpActual = static_cast<double>(Actual);                                                         \
		const double AmpExpected = static_cast<double>(Expected);                                                     \
		if (!(std::abs(AmpActual - AmpExpected) <= (Tolerance)))                                                      \
		{                                                                                                             \
			AmpTest::ReportFailure(__FILE__, __LINE__, std::string(#Actual " ~= " #Expected " (got ") +              \
				AmpTest::ToString(AmpActual) + ", expected " + AmpTest::ToString(AmpExpected) + ")");                 \
		}                                                                                                             \
	} while (0)
