#include "Combat/DustShotDamageReceiptLibrary.h"

#include "GameFramework/Actor.h"
#include "Mission/DustEncounterComponent.h"

namespace
{
    thread_local DustShotDamageReceipt::FScopedCapture* ActiveDustShotCapture = nullptr;
}

DustShotDamageReceipt::FScopedCapture::FScopedCapture(const AActor* InExpectedTarget)
    : ExpectedTarget(InExpectedTarget)
{
    if (IsInGameThread())
    {
        PreviousCapture = ActiveDustShotCapture;
        ActiveDustShotCapture = this;
        bInstalled = true;
    }
}

DustShotDamageReceipt::FScopedCapture::~FScopedCapture()
{
    if (bInstalled) ActiveDustShotCapture = PreviousCapture;
}

void DustShotDamageReceipt::FScopedCapture::Report(const AActor* Target,
    float PreviousHealth, float CurrentHealth, bool bWasAlreadyDead)
{
    if (Result.bConfirmed || Target != ExpectedTarget || !IsValid(Target) || bWasAlreadyDead
        || !FMath::IsFinite(PreviousHealth) || !FMath::IsFinite(CurrentHealth)
        || PreviousHealth <= 0.0f || CurrentHealth < 0.0f || CurrentHealth >= PreviousHealth)
        return;
    const float Applied = PreviousHealth - CurrentHealth;
    if (!FMath::IsFinite(Applied) || Applied <= 0.0f) return;
    Result.bConfirmed = true;
    Result.ActualDamage = Applied;
    Result.bKilled = CurrentHealth <= 0.0f;
}

void DustShotDamageReceipt::FScopedCapture::RecordDeath(
    const AActor* Enemy, UDustEncounterComponent* Encounter)
{
    if (Enemy != ExpectedTarget || DeathSerial != 0) return;
    // Ordered at the death itself, including nested synchronous damage callbacks.
    static uint64 NextDeathSerial = 0;
    DeathEncounter = Encounter;
    DeathSerial = ++NextDeathSerial;
}

void DustShotDamageReceipt::RecordEncounterDeath(
    const AActor* Enemy, UDustEncounterComponent* Encounter)
{
    if (IsInGameThread() && ActiveDustShotCapture)
        ActiveDustShotCapture->RecordDeath(Enemy, Encounter);
}

void UDustShotDamageReceiptLibrary::ReportAppliedShotDamage(AActor* DamagedActor,
    float PreviousHealth, float CurrentHealth, bool bWasAlreadyDead)
{
    // Only the current receiver may acknowledge the pending shot; unrelated damage is ignored.
    if (IsInGameThread() && ActiveDustShotCapture)
        ActiveDustShotCapture->Report(DamagedActor, PreviousHealth, CurrentHealth, bWasAlreadyDead);
}
