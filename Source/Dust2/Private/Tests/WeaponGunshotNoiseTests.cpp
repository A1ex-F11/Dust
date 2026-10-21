#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WeaponDefinitionData.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWeaponGunshotNoiseRangeTest,
	"Dust2.Weapon.GunshotNoise.RangeConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponGunshotNoiseRangeTest::RunTest(const FString& Parameter)
{
	UWeaponDefinitionData* Definition = NewObject<UWeaponDefinitionData>();
	if (!TestNotNull(TEXT("weapon definition created"), Definition))
	{
		return false;
	}

	TestEqual(TEXT("default range is 40 m"), Definition->GunshotHearingRangeMeters, 40.0f);
	TestEqual(TEXT("default 40 m -> 4000 cm"),
		Definition->GetGunshotHearingRangeCm(), 4000.0f);

	Definition->GunshotHearingRangeMeters = 12.0f;
	TestEqual(TEXT("12 m -> 1200 cm"),
		Definition->GetGunshotHearingRangeCm(), 1200.0f);

	Definition->GunshotHearingRangeMeters = 0.0f;
	TestEqual(TEXT("0 m reports no noise"), Definition->GetGunshotHearingRangeCm(), 0.0f);

	Definition->GunshotHearingRangeMeters = -8.0f;
	TestEqual(TEXT("negative range reports no noise"),
		Definition->GetGunshotHearingRangeCm(), 0.0f);

	Definition->GunshotHearingRangeMeters = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("NaN range reports no noise"), Definition->GetGunshotHearingRangeCm(), 0.0f);

	Definition->GunshotHearingRangeMeters = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("positive infinity reports no noise"),
		Definition->GetGunshotHearingRangeCm(), 0.0f);

	Definition->GunshotHearingRangeMeters = -std::numeric_limits<float>::infinity();
	TestEqual(TEXT("negative infinity reports no noise"),
		Definition->GetGunshotHearingRangeCm(), 0.0f);

	Definition->GunshotHearingRangeMeters = 100000.0f;
	TestEqual(TEXT("oversized range is capped at 1000 m"),
		Definition->GetGunshotHearingRangeCm(), 100000.0f);

	return true;
}
#endif
