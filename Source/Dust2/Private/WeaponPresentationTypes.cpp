#include "WeaponPresentationTypes.h"

#include "Serialization/Archive.h"
#include "Serialization/CustomVersion.h"

namespace DustWeaponPresentationVersion
{
	const FGuid Guid(0x7D0EA4B1, 0x93F04A65, 0xA127B96D, 0x405C812F);
	constexpr int32 ExplicitCountReloadSwitch = 1;
	const FCustomVersionRegistration Registration(Guid, ExplicitCountReloadSwitch, TEXT("DustWeaponPresentation"));
}

bool FWeaponPresentationProfile::Serialize(FArchive& Ar)
{
	// Same pattern as the engine's FPostProcessSettings: retain tagged property compatibility.
	Ar.UsingCustomVersion(DustWeaponPresentationVersion::Guid);
	return false;
}

void FWeaponPresentationProfile::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(DustWeaponPresentationVersion::Guid)
		< DustWeaponPresentationVersion::ExplicitCountReloadSwitch)
	{
		// Existing R08 profiles selected variants by array length; preserve those assets on load.
		bUseCountBasedReload = !ReloadVariants.IsEmpty();
	}
}
