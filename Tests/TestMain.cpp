#include "TestFramework.h"

#include <cstring>

namespace AmpTest
{
	namespace
	{
		int Failures = 0;
		int Checks = 0;
	}

	std::vector<FTestCase>& GetRegistry()
	{
		static std::vector<FTestCase> Registry;
		return Registry;
	}

	void ReportFailure(const char* File, int Line, const std::string& Message)
	{
		++Failures;
		std::printf("  FAILED %s:%d: %s\n", File, Line, Message.c_str());
	}

	void CountCheck()
	{
		++Checks;
	}
}

int main(int ArgC, char** ArgV)
{
	const char* Filter = ArgC > 1 ? ArgV[1] : nullptr;
	int Ran = 0;
	int FailedTests = 0;
	for (const AmpTest::FTestCase& Test : AmpTest::GetRegistry())
	{
		if (Filter != nullptr && std::strstr(Test.Name, Filter) == nullptr)
		{
			continue;
		}
		const int FailuresBefore = AmpTest::Failures;
		Test.Function();
		++Ran;
		const bool bPassed = AmpTest::Failures == FailuresBefore;
		FailedTests += bPassed ? 0 : 1;
		std::printf("[%s] %s\n", bPassed ? " OK " : "FAIL", Test.Name);
	}
	std::printf("\n%d tests, %d checks, %d failed tests, %d failed checks\n", Ran, AmpTest::Checks, FailedTests, AmpTest::Failures);
	return AmpTest::Failures == 0 && Ran > 0 ? 0 : 1;
}
