#include "Dust2.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "WeaponDetailsCustomization.h"
#endif

class FDust2Module : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
#if WITH_EDITOR
        if (!IsRunningCommandlet()) DustWeaponDetails::Register();
#endif
    }
    virtual void ShutdownModule() override
    {
#if WITH_EDITOR
        DustWeaponDetails::Unregister();
#endif
        FDefaultGameModuleImpl::ShutdownModule();
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDust2Module, Dust2, "Dust2");
