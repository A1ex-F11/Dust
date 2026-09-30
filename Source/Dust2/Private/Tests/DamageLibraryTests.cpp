#include"Misc/AutomationTest.h"
#include"DamageLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDamageCalculationTest,
	"Dust2.Damage.CalculateFinalDamage",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter
)
bool FDamageCalculationTest::RunTest(const FString& Parameter)
{
	const float Tolerance = 0.001f;

	const float Actual = UDamageLibrary::CalculateFinalDamage(
		36.0f,
		FName(TEXT("spine_04")),
		0.775f,
		4.0f,
		false,
		false
	);
	const float Expected = 36.0f;
	TestTrue(
		TEXT("Body without armor returns 36"),
		FMath::IsNearlyEqual(Actual, Expected, Tolerance)
	);
	const float BodyArmorActual = UDamageLibrary::CalculateFinalDamage(
		36.0f,
		FName(TEXT("spine_04")),
		0.775f,
		4.0f,
		false,
		true
	);
	const float BodyArmorExpected = 27.9f;
	TestTrue(
		TEXT("Body with armor returns 27.9f"),
		FMath::IsNearlyEqual(BodyArmorActual, BodyArmorExpected, Tolerance)
	);
	const float HeadNoHelmetActual = UDamageLibrary::CalculateFinalDamage(
		36.0f,
		FName(TEXT("head")),
		0.775f,
		4.0f,
		false,
		false
	);
	const float HeadNoHelmetExpected = 144.0f;
	TestTrue(
		TEXT("Head without helmet 144.0f"),
		FMath::IsNearlyEqual(HeadNoHelmetActual, HeadNoHelmetExpected, Tolerance)
	);
	const float HeadWithHelmetActual = UDamageLibrary::CalculateFinalDamage(
		36.0f,
		FName(TEXT("head")),
		0.775f,
		4.0f,
		true,
		false
	);
	const float HeadWithHelmetExpected = 111.6f;
	TestTrue(
		TEXT("Head with helmet returns 111.6f"),
		FMath::IsNearlyEqual(HeadWithHelmetActual, HeadWithHelmetExpected, Tolerance)
	);
	const float ZeroDamageActual = UDamageLibrary::CalculateFinalDamage(
		0.0f,
		FName(TEXT("head")),
		0.775f,
		4.0f,
		true,
		false
	);
	const float ZeroDamageExpected = 0.0f;
	TestTrue(
		TEXT("Zero damage returns 0"),
		FMath::IsNearlyEqual(ZeroDamageActual, ZeroDamageExpected, Tolerance)
	);
	const float NegativeDamageActual = UDamageLibrary::CalculateFinalDamage(
		-10.0f,
		FName(TEXT("head")),
		0.775f,
		4.0f,
		true,
		false
	);
	const float NegativeDamageExpected = 0.0f;
	TestTrue(
		TEXT("Negative damage returns 0"),
		FMath::IsNearlyEqual(NegativeDamageActual, NegativeDamageExpected, Tolerance)
	);

	return true;
}