#include "WeaponDetailsCustomization.h"

#if WITH_EDITOR
#include "WeaponBallisticsData.h"
#include "WeaponDefinitionData.h"
#include "WeaponAccuracyData.h"
#include "IPropertyUtilities.h"
#include "Widgets/Input/SButton.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"

namespace
{
void CustomizeDistanceInMeters(IDetailPropertyRow& Row, const TSharedRef<IPropertyHandle>& Distance)
{
    TSharedPtr<SWidget> NameWidget;
    TSharedPtr<SWidget> ValueWidget;
    Row.GetDefaultWidgets(NameWidget, ValueWidget);
    if (!NameWidget.IsValid()) return;
    // Same property and raw cm value. Only Slate input/output units change to metres.
    // PropertyHandle retains transactions, multi-selection and reset-to-default support.
    const auto Units = MakeShared<TNumericUnitTypeInterface<float>>(EUnit::Centimeters);
    Units->UserDisplayUnits = EUnit::Meters;
    Row.CustomWidget().NameContent()[NameWidget.ToSharedRef()]
        .ValueContent().MinDesiredWidth(160.0f)
        [
            SNew(SNumericEntryBox<float>)
            .AllowSpin(false)
            .MinValue(0.0f)
            .TypeInterface(Units)
            .IsEnabled_Lambda([Distance]() { return Distance->IsEditable(); })
            .Value_Lambda([Distance]() -> TOptional<float>
            {
                float Centimeters = 0.0f;
                return Distance->GetValue(Centimeters) == FPropertyAccess::Success
                    ? TOptional<float>(Centimeters) : TOptional<float>();
            })
            .OnValueCommitted_Lambda([Distance](float Centimeters, ETextCommit::Type CommitType)
            {
                if (CommitType == ETextCommit::OnCleared || !Distance->IsEditable()
                    || !FMath::IsFinite(Centimeters) || Centimeters < 0.0f) return;
                float Current = 0.0f;
                if (Distance->GetValue(Current) != FPropertyAccess::Success || Current != Centimeters)
                    Distance->SetValue(Centimeters);
            })
            .ToolTipText(FText::FromString(TEXT("输入单位为米（m），例如100 m。底层仍保存10000 cm；可撤销或重置。")))
        ];
}

class FWeaponDefinitionDetails final : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FWeaponDefinitionDetails>(); }
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        const FName Categories[] = { TEXT("基础配置"), TEXT("精度与镜头"), TEXT("后坐力") };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Categories); ++Index)
        {
            IDetailCategoryBuilder& Category = Builder.EditCategory(Categories[Index]);
            Category.SetSortOrder(Index);
        }
        // Keep the original object serialized, but expose only its numeric fields.
        const TSharedRef<IPropertyHandle> Ballistics = Builder.GetProperty(TEXT("BallisticsData"));
        if (Ballistics->IsValidHandle())
        {
            Builder.HideProperty(Ballistics);
            for (const FName Name : { FName(TEXT("DamagePerShot")), FName(TEXT("ArmorPenetration")),
                FName(TEXT("HeadDamageMultiplier")), FName(TEXT("TraceDistance")) })
            {
                const TSharedPtr<IPropertyHandle> Child = Ballistics->GetChildHandle(Name);
                if (!Child.IsValid() || !Child->IsValidHandle()) continue;
                IDetailPropertyRow& Row = Builder.EditCategory(TEXT("基础配置")).AddProperty(Child);
                if (Name == FName(TEXT("TraceDistance")))
                    CustomizeDistanceInMeters(Row, Child.ToSharedRef());
            }
        }
        TArray<TWeakObjectPtr<UObject>> Objects;
        Builder.GetObjectsBeingCustomized(Objects);
        if (Objects.Num() == 1)
        {
            const TWeakObjectPtr<UWeaponDefinitionData> Definition = Cast<UWeaponDefinitionData>(Objects[0].Get());
            const TWeakPtr<IPropertyUtilities> Utilities = Builder.GetPropertyUtilities();
            Builder.EditCategory(TEXT("基础配置")).AddCustomRow(FText::FromString(TEXT("生成独立配置资产")))
                .Visibility(TAttribute<EVisibility>::CreateLambda([Definition]()
                {
                    const UWeaponDefinitionData* D = Definition.Get();
                    const bool bNeedsSplit = D && (!D->AccuracyData || !D->RecoilData
                        || D->AccuracyData->GetOutermost() == D->GetOutermost()
                        || D->RecoilData->GetOutermost() == D->GetOutermost());
                    return bNeedsSplit ? EVisibility::Visible : EVisibility::Collapsed;
                }))
                .WholeRowContent()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("生成独立精度与后坐资产")))
                    .ToolTipText(FText::FromString(TEXT("仅旧配置需要：复制当前数值生成独立资产并更新引用；完成后此入口自动隐藏。")))
                    .OnClicked_Lambda([Definition, Utilities]()
                    {
                        if (UWeaponDefinitionData* D = Definition.Get()) D->SplitAccuracyAndRecoilAssets();
                        if (const TSharedPtr<IPropertyUtilities> P = Utilities.Pin()) P->RequestForceRefresh();
                        return FReply::Handled();
                    })
                ];
        }
        if (IDetailPropertyRow* Row = Builder.EditDefaultProperty(Builder.GetProperty(TEXT("RecoilData"))))
            Row->ShouldAutoExpand(false);
    }
};

class FWeaponAccuracyDetails final : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FWeaponAccuracyDetails>(); }
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        // Promote original child handles: one category, no repeated struct title.
        const FName Categories[] = { TEXT("腰射精度"), TEXT("瞄准精度"), TEXT("镜头反馈") };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Categories); ++Index)
        {
            Builder.EditCategory(Categories[Index]).SetSortOrder(Index);
        }
        const TSharedRef<IPropertyHandle> AimEnabled = Builder.GetProperty(TEXT("bUseAimingSpread"));
        const auto AddAccuracy = [&Builder, AimEnabled](const FName RootName, const FName Category, bool bAim)
        {
            const TSharedRef<IPropertyHandle> Root = Builder.GetProperty(RootName);
            Builder.HideProperty(Root);
            const auto Add = [&Builder, AimEnabled, Category, bAim](const TSharedPtr<IPropertyHandle>& Child)
            {
                if (!Child.IsValid() || !Child->IsValidHandle()) return;
                IDetailPropertyRow& Row = Builder.EditCategory(Category).AddProperty(Child);
                if (bAim)
                    Row.Visibility(TAttribute<EVisibility>::CreateLambda([AimEnabled]()
                    {
                        bool bEnabled = false;
                        return AimEnabled->GetValue(bEnabled) != FPropertyAccess::Success || bEnabled
                            ? EVisibility::Visible : EVisibility::Collapsed;
                    }));
            };
            for (const FName Name : { FName(TEXT("BaseSpreadAngle")), FName(TEXT("MaxFiringExtraAngle")),
                FName(TEXT("ShotsToMaximum")), FName(TEXT("FullRecoverySeconds")) })
                Add(Root->GetChildHandle(Name));
            const TSharedPtr<IPropertyHandle> Posture = Root->GetChildHandle(TEXT("PostureOverride"));
            if (Posture.IsValid() && Posture->IsValidHandle())
            {
                Builder.HideProperty(Posture);
                for (const FName Name : { FName(TEXT("MovingExtraAngle")), FName(TEXT("SlowWalkExtraAngle")),
                    FName(TEXT("AirborneExtraAngle")), FName(TEXT("MovementThreshold")) })
                    Add(Posture->GetChildHandle(Name));
            }
        };
        AddAccuracy(TEXT("HipAccuracy"), TEXT("腰射精度"), false);
        AddAccuracy(TEXT("AimAccuracy"), TEXT("瞄准精度"), true);
        const TSharedRef<IPropertyHandle> Camera = Builder.GetProperty(TEXT("CameraFeedbackOverrides"));
        Builder.HideProperty(Camera);
        for (const FName Name : { FName(TEXT("ReturnSpeed")), FName(TEXT("SmoothKickDuration")),
            FName(TEXT("ImmediateKickFraction")) })
        {
            const TSharedPtr<IPropertyHandle> Child = Camera->GetChildHandle(Name);
            if (Child.IsValid() && Child->IsValidHandle())
                Builder.EditCategory(TEXT("镜头反馈")).AddProperty(Child);
        }
    }
};

class FWeaponBallisticsDetails final : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FWeaponBallisticsDetails>(); }
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        const TSharedRef<IPropertyHandle> Distance = Builder.GetProperty(
            GET_MEMBER_NAME_CHECKED(UWeaponBallisticsData, TraceDistance));
        if (!Distance->IsValidHandle()) return;
        IDetailPropertyRow* Row = Builder.EditDefaultProperty(Distance);
        if (!Row) return;
        CustomizeDistanceInMeters(*Row, Distance);
    }
};
}

void DustWeaponDetails::Register()
{
    FPropertyEditorModule& Editor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    Editor.RegisterCustomClassLayout(TEXT("WeaponDefinitionData"), FOnGetDetailCustomizationInstance::CreateStatic(&FWeaponDefinitionDetails::MakeInstance));
    Editor.RegisterCustomClassLayout(TEXT("WeaponAccuracyData"), FOnGetDetailCustomizationInstance::CreateStatic(&FWeaponAccuracyDetails::MakeInstance));
    Editor.RegisterCustomClassLayout(TEXT("WeaponBallisticsData"), FOnGetDetailCustomizationInstance::CreateStatic(&FWeaponBallisticsDetails::MakeInstance));
    Editor.NotifyCustomizationModuleChanged();
}

void DustWeaponDetails::Unregister()
{
    if (FPropertyEditorModule* Editor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
    {
        Editor->UnregisterCustomClassLayout(TEXT("WeaponDefinitionData"));
        Editor->UnregisterCustomClassLayout(TEXT("WeaponAccuracyData"));
        Editor->UnregisterCustomClassLayout(TEXT("WeaponBallisticsData"));
        Editor->NotifyCustomizationModuleChanged();
    }
}
#endif
