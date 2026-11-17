#include "Presentation/WeaponVisualRecoilData.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "UObject/UnrealType.h"
#endif

bool FVisualRecoilPoseSettings::IsFinite() const
{
	return !Roll.ContainsNaN() && !Pitch.ContainsNaN() && !Yaw.ContainsNaN()
		&& !KickBack.ContainsNaN() && !KickRight.ContainsNaN() && !KickUp.ContainsNaN() && !PivotOffset.ContainsNaN();
}

bool UWeaponVisualRecoilData::IsUsable() const
{
	const auto Positive = [](float V) { return FMath::IsFinite(V) && V > 0; };
	const auto NonNegative = [](float V) { return FMath::IsFinite(V) && V >= 0; };
	float RotationMin = 0, RotationMax = 0, LocationMin = 0, LocationMax = 0;
	if (IsValid(SingleRotation)) SingleRotation->GetTimeRange(RotationMin, RotationMax);
	if (IsValid(SingleLocation)) SingleLocation->GetTimeRange(LocationMin, LocationMax);
	return IsValid(SingleRotation) && IsValid(SingleLocation) && Hip.IsFinite() && Aim.IsFinite()
		&& FMath::IsFinite(RotationMax) && FMath::IsFinite(LocationMax) && FMath::Max(RotationMax, LocationMax) > UE_SMALL_NUMBER
		&& Positive(PlayRate) && NonNegative(PlaybackOffset) && Positive(SmoothingSpeed)
		&& !AutoRotationScale.ContainsNaN() && !AutoLocationScale.ContainsNaN() && !SpaceRotation.ContainsNaN()
		&& FMath::IsFinite(PushbackAmount) && NonNegative(PushbackDamping) && NonNegative(PushbackInterpSpeed)
		&& (PushbackAmount == 0 || (Positive(PushbackDamping) && Positive(PushbackInterpSpeed)))
		&& !NoiseAmount.ContainsNaN() && !NoiseDamping.ContainsNaN() && !NoiseInterpSpeed.ContainsNaN()
		&& NoiseDamping.X >= 0 && NoiseDamping.Y >= 0 && NoiseInterpSpeed.X >= 0 && NoiseInterpSpeed.Y >= 0
		&& (NoiseAmount.X == 0 || (NoiseDamping.X > 0 && NoiseInterpSpeed.X > 0))
		&& (NoiseAmount.Y == 0 || (NoiseDamping.Y > 0 && NoiseInterpSpeed.Y > 0))
		&& NonNegative(NoiseAimScale) && !PitchSwayRange.ContainsNaN() && !YawSwayRange.ContainsNaN()
		&& Positive(SwayReturnSpeed) && FMath::IsFinite(ProgressPitchPerShot) && FMath::IsFinite(ProgressUpPerShot)
		&& Positive(ProgressReturnSpeed);
}

#if WITH_EDITOR
EDataValidationResult UWeaponVisualRecoilData::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	if (!IsUsable())
	{
		Context.AddError(FText::FromString(TEXT("枪身后坐需要单发位置/旋转曲线、有限幅度和正的播放/回正速度。")));
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}

namespace
{
	FString Key(FString Name)
	{
		FString Result;
		for (TCHAR C : Name) if (FChar::IsAlnum(C)) Result.AppendChar(FChar::ToLower(C));
		return Result;
	}
	FProperty* Field(const UStruct* Type, const TCHAR* Name)
	{
		const FString Wanted = Key(Name);
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			const FString Actual = It->GetName();
			// User-defined BP struct fields append an index/GUID to the authored name.
			if (Key(Actual) == Wanted || Actual.StartsWith(FString(Name) + TEXT("_"), ESearchCase::IgnoreCase)) return *It;
		}
		return nullptr;
	}
	bool Number(const UStruct* Type, const void* Container, const TCHAR* Name, double& Out)
	{
		const FNumericProperty* P = CastField<FNumericProperty>(Field(Type, Name));
		if (!P) return false;
		const void* Address = P->ContainerPtrToValuePtr<void>(Container);
		Out = P->IsFloatingPoint() ? P->GetFloatingPointPropertyValue(Address) : static_cast<double>(P->GetSignedIntPropertyValue(Address));
		return FMath::IsFinite(Out);
	}
	template<class TVector>
	bool Vector(const UStruct* Type, const void* Container, const TCHAR* Name, TVector& Out, int32 Axes)
	{
		const FStructProperty* P = CastField<FStructProperty>(Field(Type, Name));
		if (!P) return false;
		const void* Address = P->ContainerPtrToValuePtr<void>(Container);
		const TCHAR* Names[] = {TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT("W")};
		TVector Value = Out;
		for (int32 I = 0; I < Axes; ++I) { double V = 0; if (!Number(P->Struct, Address, Names[I], V)) return false; Value[I] = V; }
		Out = Value;
		return true;
	}
	struct FScope { const UStruct* Type; const void* Container; };
	FScope Nested(FScope Root, const TCHAR* Name)
	{
		const FStructProperty* P = CastField<FStructProperty>(Field(Root.Type, Name));
		return P ? FScope{P->Struct, P->ContainerPtrToValuePtr<void>(Root.Container)} : FScope{nullptr, nullptr};
	}
	bool Scalar(FScope Source, const TCHAR* Name, float& Out)
	{
		double Value = 0;
		if (!Source.Type || !Number(Source.Type, Source.Container, Name, Value) || FMath::Abs(Value) > MAX_flt) return false;
		Out = static_cast<float>(Value); return true;
	}
	UCurveVector* Curve(FScope Source, const TCHAR* Name)
	{
		if (!Source.Type) return nullptr;
		if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Field(Source.Type, Name)))
			return Cast<UCurveVector>(Soft->GetPropertyValue_InContainer(Source.Container).LoadSynchronous());
		const FObjectPropertyBase* P = CastField<FObjectPropertyBase>(Field(Source.Type, Name));
		return P ? Cast<UCurveVector>(P->GetObjectPropertyValue_InContainer(Source.Container)) : nullptr;
	}
	bool Pose(const UObject* Source, bool bAim, FVisualRecoilPoseSettings& Out)
	{
		const UStruct* Type = Source->GetClass();
		bool Ok = Vector(Type, Source, bAim ? TEXT("RollAim") : TEXT("RollHip"), Out.Roll, 4);
		Ok &= Vector(Type, Source, bAim ? TEXT("PitchAim") : TEXT("PitchHip"), Out.Pitch, 2);
		Ok &= Vector(Type, Source, bAim ? TEXT("YawAim") : TEXT("YawHip"), Out.Yaw, 4);
		Ok &= Vector(Type, Source, bAim ? TEXT("KickAim") : TEXT("KickHip"), Out.KickBack, 2);
		Ok &= Vector(Type, Source, bAim ? TEXT("KickAimR") : TEXT("KickHipR"), Out.KickRight, 2);
		Ok &= Vector(Type, Source, bAim ? TEXT("KickUpAim") : TEXT("KickHipUp"), Out.KickUp, 2);
		Ok &= Vector(Type, Source, bAim ? TEXT("AimPivotOffset") : TEXT("HipPivotOffset"), Out.PivotOffset, 3);
		return Ok;
	}
}
#endif

void UWeaponVisualRecoilData::ImportLegacyVisualRecoil()
{
#if WITH_EDITOR && WITH_EDITORONLY_DATA
	const UObject* Source = LegacyImportSource;
	if (!IsValid(Source) || Source == this) { ImportStatus = TEXT("请选择旧RD资源；尚未修改参数。"); return; }
	UWeaponVisualRecoilData* Draft = NewObject<UWeaponVisualRecoilData>(GetTransientPackage());
	const FScope Root{Source->GetClass(), Source};
	FScope Curves = Nested(Root, TEXT("Curves"));
	if (!Curves.Type) Curves = Nested(Root, TEXT("RecoilCurves"));
	Draft->SingleRotation = Curve(Curves, TEXT("SingleRot"));
	Draft->SingleLocation = Curve(Curves, TEXT("SingleLoc"));
	Draft->AutoRotation = Curve(Curves, TEXT("AutoRot"));
	Draft->AutoLocation = Curve(Curves, TEXT("AutoLoc"));
	if (!Draft->SingleRotation || !Draft->SingleLocation || !Pose(Source, false, Draft->Hip) || !Pose(Source, true, Draft->Aim))
	{
		ImportStatus = TEXT("旧资源缺少单发曲线/腰射或瞄准幅度/支点字段；未覆盖任何参数，请核对RD资源类型。");
		return;
	}
	TArray<FString> Missing;
	const auto Float = [&](const TCHAR* Name, float& Value) { if (!Scalar(Root, Name, Value)) Missing.Add(Name); };
	Float(TEXT("Playrate"), Draft->PlayRate); Float(TEXT("PlaybackOffset"), Draft->PlaybackOffset);
	const FScope Pushback = Nested(Root, TEXT("Pushback"));
	if (!Scalar(Pushback, TEXT("Amount"), Draft->PushbackAmount)) Missing.Add(TEXT("Pushback.Amount"));
	if (!Scalar(Pushback, TEXT("Damping"), Draft->PushbackDamping)) Missing.Add(TEXT("Pushback.Damping"));
	if (!Scalar(Pushback, TEXT("InterpSpeed"), Draft->PushbackInterpSpeed)) Missing.Add(TEXT("Pushback.InterpSpeed"));
	FScope Noise = Nested(Root, TEXT("RecoilNoise"));
	if (!Noise.Type) Noise = Nested(Root, TEXT("Noise"));
	if (!Scalar(Noise, TEXT("AimScalar"), Draft->NoiseAimScale)) Missing.Add(TEXT("Noise.AimScalar"));
	const auto Vec = [&](const TCHAR* Name, FVector2D& Value) { if (!Noise.Type || !Vector(Noise.Type, Noise.Container, Name, Value, 2)) Missing.Add(Name); };
	Vec(TEXT("Target"), Draft->NoiseAmount); Vec(TEXT("Damping"), Draft->NoiseDamping);
	Vec(TEXT("InterpSpeed"), Draft->NoiseInterpSpeed);
	if (!Vector(Source->GetClass(), Source, TEXT("MultiRot"), Draft->AutoRotationScale, 3)) Missing.Add(TEXT("MultiRot"));
	if (!Vector(Source->GetClass(), Source, TEXT("MultiLoc"), Draft->AutoLocationScale, 3)) Missing.Add(TEXT("MultiLoc"));
	if (const FBoolProperty* P = CastField<FBoolProperty>(Field(Source->GetClass(), TEXT("bSmoothRoll"))))
		Draft->bAlternateRoll = P->GetPropertyValue_InContainer(Source);
	if (const FStructProperty* P = CastField<FStructProperty>(Field(Source->GetClass(), TEXT("SpaceRotation"))))
	{
		if (P->Struct == TBaseStructure<FRotator>::Get()) Draft->SpaceRotation = *P->ContainerPtrToValuePtr<FRotator>(Source);
		else Missing.Add(TEXT("SpaceRotation"));
	}
	if (const FStructProperty* P = CastField<FStructProperty>(Field(Source->GetClass(), TEXT("Sway"))))
	{
		const void* Value = P->ContainerPtrToValuePtr<void>(Source);
		if (!Vector(P->Struct, Value, TEXT("PitchSway"), Draft->PitchSwayRange, 2)) Missing.Add(TEXT("Sway.PitchSway"));
		if (!Vector(P->Struct, Value, TEXT("YawSway"), Draft->YawSwayRange, 2)) Missing.Add(TEXT("Sway.YawSway"));
	}
	if (const FStructProperty* P = CastField<FStructProperty>(Field(Source->GetClass(), TEXT("Progress"))))
	{
		const void* Value = P->ContainerPtrToValuePtr<void>(Source); double Back = 0, Up = 0;
		if (Number(P->Struct, Value, TEXT("PitchAmount"), Back)) Draft->ProgressPitchPerShot = static_cast<float>(Back);
		else Missing.Add(TEXT("Progress.PitchAmount"));
		if (Number(P->Struct, Value, TEXT("UpAmount"), Up)) Draft->ProgressUpPerShot = static_cast<float>(Up);
		else Missing.Add(TEXT("Progress.UpAmount"));
	}
	if (!Draft->IsUsable()) { ImportStatus = TEXT("导入数据含无效幅度或非正速度；未覆盖参数。"); return; }
	Modify();
	for (TFieldIterator<FProperty> It(StaticClass()); It; ++It)
	{
		if (It->GetOwnerStruct() == StaticClass() && !It->HasAnyPropertyFlags(CPF_EditorOnly))
			It->CopyCompleteValue_InContainer(this, Draft);
	}
	LegacyImportSource = nullptr;
	ImportStatus = TEXT("已导入曲线及可识别视觉参数，旧资源引用已清空；未保存。新接续/回正算法须PIE对照。默认平滑/摆动回正/累积回正速度保留。");
	if (!Missing.IsEmpty()) ImportStatus += TEXT(" 未匹配可选字段：") + FString::Join(Missing, TEXT(", "));
	MarkPackageDirty();
#endif
}
