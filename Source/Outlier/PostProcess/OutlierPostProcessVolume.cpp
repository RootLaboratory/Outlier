// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/OutlierPostProcessVolume.h"

#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Containers/StaticArray.h"
#include "Curves/CurveFloat.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
// PP_MagneticLens 의 파라미터 이름. 머티리얼 쪽과 반드시 같아야 한다.
const FName MagneticLocationParameterName = TEXT("Location");
const FName MagneticRadiusParameterName = TEXT("Radius");
const FName MagneticStartTimeParameterName = TEXT("StartTime");
const FName MagneticEndTimeParameterName = TEXT("EndTime");

// MF_GlitchDissolve 의 파라미터 이름. 머티리얼 쪽과 반드시 같아야 한다.
const FName SpawnDissolveAmountParameterName = TEXT("Dissolve Amount");
const FName SpawnDissolveDivideParameterName = TEXT("Amount Divide");
const FName SpawnDissolveMultiplyParameterName = TEXT("Amount Multiply");
const FName SpawnDissolveSubtractParameterName = TEXT("Amount Subtract");
const FName SpawnDissolvePowerParameterName = TEXT("Power");
const FName SpawnDissolveTexturePowerParameterName = TEXT("Texture Power");
const FName SpawnDissolveContrastParameterName = TEXT("Contrast");

// 사망 연출 텍스처를 EDeathTransitionTexture 순서로 모은다.
TStaticArray<UTexture2D*, DeathTransitionTextureCount> GetDeathTransitionTextures(const AOutlierPostProcessVolume& Volume)
{
	TStaticArray<UTexture2D*, DeathTransitionTextureCount> Textures;
	Textures[static_cast<int32>(EDeathTransitionTexture::BlackBackground)] = Volume.DeathBlackTargetTexture;
	Textures[static_cast<int32>(EDeathTransitionTexture::BlackNoise)] = Volume.DeathBlackNoiseTexture;
	Textures[static_cast<int32>(EDeathTransitionTexture::FadeVignette)] = Volume.DeathBlackVignetteTexture;
	return Textures;
}
}

void AOutlierPostProcessVolume::BeginPlay()
{
	Super::BeginPlay();

	InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType::Damaged);
	InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType::Magnetic);
	InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType::PartnerOutline);

	if (!HasStealthMeshMaterials())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s stealth mesh materials are not assigned."),
			*GetName()
		);
	}

	if (!HasValidScanPostProcessBindings())
	{
		const TObjectPtr<UMaterialInterface>* ScanPostProcessMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Scan);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s scan post-process bindings are invalid. Material=%s MPC=%s RadiusParam=%s LocationParam=%s"),
			*GetName(),
			*GetNameSafe(ScanPostProcessMaterial ? ScanPostProcessMaterial->Get() : nullptr),
			*GetNameSafe(ScanParameterCollection),
			*ScanRadiusParameterName.ToString(),
			*ScanLocationParameterName.ToString()
		);
	}

	if (UWorld* World = GetWorld())
	{
		if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PostProcessSubsystem->RegisterPostProcessVolume(this);
		}

		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			const TStaticArray<UTexture2D*, DeathTransitionTextureCount> DeathTransitionTextures = GetDeathTransitionTextures(*this);
			for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (!LocalPlayer)
				{
					continue;
				}

				if (ULocalPlayerPostProcessSubsystem* RDGSubsystem = LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
				{
					RDGSubsystem->SetDepthOfFieldVolume(this);

					for (int32 SlotIndex = 0; SlotIndex < DeathTransitionTextureCount; ++SlotIndex)
					{
						if (UTexture2D* Texture = DeathTransitionTextures[SlotIndex])
						{
							RDGSubsystem->SetDeathTransitionTexture(static_cast<EDeathTransitionTexture>(SlotIndex), Texture);
						}
					}

				}
			}
		}
	}
}

void AOutlierPostProcessVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const TStaticArray<UTexture2D*, DeathTransitionTextureCount> DeathTransitionTextures = GetDeathTransitionTextures(*this);
	UWorld* World = GetWorld();
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			ULocalPlayerPostProcessSubsystem* RDGSubsystem = LocalPlayer
				? LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>() : nullptr;
			if (!RDGSubsystem)
			{
				continue;
			}

			// 다른 볼륨이 이미 바꿔 끼운 슬롯은 건드리지 않는다.
			for (int32 SlotIndex = 0; SlotIndex < DeathTransitionTextureCount; ++SlotIndex)
			{
				const EDeathTransitionTexture Slot = static_cast<EDeathTransitionTexture>(SlotIndex);
				UTexture2D* Texture = DeathTransitionTextures[SlotIndex];
				if (Texture && RDGSubsystem->GetDeathTransitionTexture(Slot) == Texture)
				{
					RDGSubsystem->SetDeathTransitionTexture(Slot, nullptr);
				}
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AOutlierPostProcessVolume::InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType MaterialType)
{
	TObjectPtr<UMaterialInterface>* PostProcessMaterial = PostProcessMaterials.Find(MaterialType);
	if (!PostProcessMaterial || !PostProcessMaterial->Get() || Cast<UMaterialInstanceDynamic>(PostProcessMaterial->Get()))
	{
		return;
	}

	UMaterialInterface* SourceMaterial = PostProcessMaterial->Get();
	UMaterialInstanceDynamic* RuntimeMaterial = UMaterialInstanceDynamic::Create(SourceMaterial, this);
	if (!RuntimeMaterial)
	{
		return;
	}

	for (FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
	{
		if (Blendable.Object.Get() == SourceMaterial)
		{
			Blendable.Object = RuntimeMaterial;
		}
	}

	*PostProcessMaterial = RuntimeMaterial;
}

bool AOutlierPostProcessVolume::HasValidScanPostProcessBindings() const
{
	return HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Scan)
		&& ScanParameterCollection
		&& !ScanRadiusParameterName.IsNone()
		&& !ScanProgressParameterName.IsNone()
		&& !ScanLocationParameterName.IsNone()
		&& !ScanFlagParameterName.IsNone();
}

bool AOutlierPostProcessVolume::HasValidPostProcessMaterial(EOutlierPostProcessMaterialType MaterialType) const
{
	const TObjectPtr<UMaterialInterface>* PostProcessMaterial = PostProcessMaterials.Find(MaterialType);
	return PostProcessMaterial && PostProcessMaterial->Get();
}

void AOutlierPostProcessVolume::SetPostProcessEnabled(EOutlierPostProcessMaterialType MaterialType, bool bInEnabled)
{
	const float TargetWeight = bInEnabled ? 1.0f : 0.0f;
	if (!SetBlendableWeight(MaterialType, TargetWeight))
	{
		return;
	}

	switch (MaterialType)
	{
	case EOutlierPostProcessMaterialType::Scan:
		bScanPostProcessEnabled = bInEnabled;
		break;
	case EOutlierPostProcessMaterialType::Stealth:
		bStealthPostProcessEnabled = bInEnabled;
		//UE_LOG(LogTemp, Error, TEXT("[StealthPPDebug] SetEnabled=%d Weight=%.2f"), bStealthPostProcessEnabled ? 1 : 0, TargetWeight);
		break;
	case EOutlierPostProcessMaterialType::Damaged:
		bDamagedPostProcessEnabled = bInEnabled;
		//UE_LOG(LogTemp, Error, TEXT("[DamagedPPDebug] SetEnabled=%d Weight=%.2f"), bDamagedPostProcessEnabled ? 1 : 0, TargetWeight);
		break;
	case EOutlierPostProcessMaterialType::Magnetic:
		bMagneticPostProcessEnabled = bInEnabled;
		break;
	case EOutlierPostProcessMaterialType::PartnerOutline:
		bPartnerOutlinePostProcessEnabled = bInEnabled;
		break;
	default:
		break;
	}
}

bool AOutlierPostProcessVolume::SetBlendableWeight(EOutlierPostProcessMaterialType MaterialType, float Weight)
{
	const TObjectPtr<UMaterialInterface>* PostProcessMaterial = PostProcessMaterials.Find(MaterialType);
	if (!PostProcessMaterial || !PostProcessMaterial->Get())
	{
		return false;
	}

	const float ClampedWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
	TArray<FWeightedBlendable>& Blendables = Settings.WeightedBlendables.Array;
	for (FWeightedBlendable& Blendable : Blendables)
	{
		if (Blendable.Object.Get() == PostProcessMaterial->Get())
		{
			Blendable.Weight = ClampedWeight;
			return true;
		}
	}

	Blendables.Add(FWeightedBlendable(ClampedWeight, PostProcessMaterial->Get()));
	return true;
}

void AOutlierPostProcessVolume::SetDamagedMaterialParameters(float InRatio)
{
}

void AOutlierPostProcessVolume::ResetPostProcessMaterialParameters()
{
	SetScanMaterialParameters(FVector::ZeroVector, 0.0f, 0.0f);
	UpdateDamagedMaterialParameters(1.0f);
	ResetMagneticMaterialParameters();
	ScanRangeRange = 0.0f;
}

void AOutlierPostProcessVolume::SetScanMaterialParameters(FVector ScanLocation, float ScanRadius, float Range)
{
	UWorld* World = GetWorld();
	if (!World || !HasValidScanPostProcessBindings())
	{
		return;
	}

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanRadiusParameterName,
		ScanRadius
	);

	UKismetMaterialLibrary::SetVectorParameterValue(
		World,
		ScanParameterCollection,
		ScanLocationParameterName,
		FLinearColor(ScanLocation.X, ScanLocation.Y, ScanLocation.Z, 0.0f)
	);

	const float Progress = Range > 0.0f ? ScanRadius / Range : 0.0f;

	const float Flag = Range > 0.0f ? 1 : 0.0f;

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanProgressParameterName,
		Progress
	);

	//기존 Range->
	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanFlagParameterName,
		Flag
	);

	ScanRangeRange = Range;

}

void AOutlierPostProcessVolume::UpdateScanMaterialParameters(FVector ScanLocation, float ScanRadius) const
{
	UWorld* World = GetWorld();
	if (!World || !HasValidScanPostProcessBindings())
	{
		return;
	}

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanRadiusParameterName,
		ScanRadius
	);

	UKismetMaterialLibrary::SetVectorParameterValue(
		World,
		ScanParameterCollection,
		ScanLocationParameterName,
		FLinearColor(ScanLocation.X, ScanLocation.Y, ScanLocation.Z, 0.0f)
	);

	const float Progress = ScanRangeRange > 0.0f ? ScanRadius / ScanRangeRange : 0.0f;

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanProgressParameterName,
		Progress
	);
}

void AOutlierPostProcessVolume::SetMagneticMaterialParameters(
	FVector Origin,
	float Radius,
	float InStartTime,
	float InEndTime) const
{
	const TObjectPtr<UMaterialInterface>* MagneticMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Magnetic);
	UMaterialInstanceDynamic* MagneticMID = MagneticMaterial ? Cast<UMaterialInstanceDynamic>(MagneticMaterial->Get()) : nullptr;
	if (!MagneticMID)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[MagneticLens] 3/3 실패. %s 에 Magnetic MID 가 없다. MapEntry=%s SourceMaterial=%s")
			TEXT(" ( 볼륨의 PostProcessMaterials 맵에 Magnetic 키로 머티리얼을 넣었는지 확인 )"),
			*GetName(),
			MagneticMaterial ? TEXT("있음") : TEXT("없음"),
			*GetNameSafe(MagneticMaterial ? MagneticMaterial->Get() : nullptr));
		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[MagneticLens] 3/3 파라미터 세팅. MID=%s Origin=%s Radius=%.1f StartTime=%.2f EndTime=%.2f"),
		*GetNameSafe(MagneticMID),
		*Origin.ToCompactString(),
		Radius,
		InStartTime,
		InEndTime);

	MagneticMID->SetVectorParameterValue(
		MagneticLocationParameterName,
		FLinearColor(Origin.X, Origin.Y, Origin.Z, 0.0f)
	);
	MagneticMID->SetScalarParameterValue(MagneticRadiusParameterName, FMath::Max(Radius, 0.0f));
	MagneticMID->SetScalarParameterValue(MagneticStartTimeParameterName, InStartTime);
	MagneticMID->SetScalarParameterValue(MagneticEndTimeParameterName, InEndTime);
}

void AOutlierPostProcessVolume::BeginMagneticFadeOut(float InEndTime) const
{
	const TObjectPtr<UMaterialInterface>* MagneticMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Magnetic);
	UMaterialInstanceDynamic* MagneticMID = MagneticMaterial ? Cast<UMaterialInstanceDynamic>(MagneticMaterial->Get()) : nullptr;
	if (!MagneticMID)
	{
		return;
	}

	MagneticMID->SetScalarParameterValue(MagneticEndTimeParameterName, InEndTime);
}

void AOutlierPostProcessVolume::ResetMagneticMaterialParameters() const
{
	SetMagneticMaterialParameters(FVector::ZeroVector, 0.0f, 0.0f, 0.0f);
}

float AOutlierPostProcessVolume::EvaluateStealthFade(float InLinearFade) const
{
	const float LinearFade = FMath::Clamp(InLinearFade, 0.0f, 1.0f);

	// 양 끝은 커브를 태우지 않는다. 커브가 0/1 에서 어긋나 있어도 완전 은신 / 완전 복구는 보장돼야 한다.
	if (LinearFade <= 0.0f || LinearFade >= 1.0f || !StealthFadeCurve)
	{
		return LinearFade;
	}

	return FMath::Clamp(StealthFadeCurve->GetFloatValue(LinearFade), 0.0f, 1.0f);
}

bool AOutlierPostProcessVolume::ComputeEnemySpawnDissolveRange(float& OutStartAmount, float& OutEndAmount) const
{
	if (!EnemySpawnDissolveMaterial)
	{
		return false;
	}

	// 못 찾은 파라미터는 MF_GlitchDissolve 의 기본값으로 계산한다.
	bool bAllParametersFound = true;
	auto ReadScalar = [this, &bAllParametersFound](FName ParameterName, float DefaultValue)
	{
		float Value = DefaultValue;
		if (!EnemySpawnDissolveMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterName), Value))
		{
			bAllParametersFound = false;
			return DefaultValue;
		}
		return Value;
	};
	const float Divide = ReadScalar(SpawnDissolveDivideParameterName, 2.0f);
	const float Multiply = ReadScalar(SpawnDissolveMultiplyParameterName, 2.0f);
	const float Subtract = ReadScalar(SpawnDissolveSubtractParameterName, 1.0f);
	const float Power = ReadScalar(SpawnDissolvePowerParameterName, 1.4f);
	const float TexturePower = ReadScalar(SpawnDissolveTexturePowerParameterName, 1.5f);
	const float Contrast = ReadScalar(SpawnDissolveContrastParameterName, 1.359162f);
	if (!bAllParametersFound)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EnemySpawnDissolve] %s 의 일부 파라미터를 찾지 못해 MF_GlitchDissolve 기본값으로 범위를 계산한다."),
			*GetNameSafe(EnemySpawnDissolveMaterial));
	}
	if (FMath::IsNearlyZero(Divide) || FMath::IsNearlyZero(Multiply) || Power <= KINDA_SMALL_NUMBER || Contrast < 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EnemySpawnDissolve] %s 파라미터로 범위를 구할 수 없다. Divide=%.3f Multiply=%.3f Power=%.3f Contrast=%.3f"),
			*GetNameSafe(EnemySpawnDissolveMaterial), Divide, Multiply, Power, Contrast);
		return false;
	}

	// 바운드 기준 높이 h( 0~1 ), 노이즈 N 에 대해 머티리얼은 픽셀마다
	//   s    = pow(1 - h + DA * Multiply / Divide - Subtract, Power) - TexturePower * N
	//   Mask = CheapContrast(saturate(2s - 1), Contrast)
	// 를 계산한다. Mask 가 0 → 1 로 바뀌는 s 구간은 Contrast 로만 정해진다.
	const float ContrastRange = 1.0f + 2.0f * Contrast;
	const float HiddenScore = 0.5f * (1.0f + Contrast / ContrastRange);
	const float RevealedScore = 0.5f * (1.0f + (1.0f + Contrast) / ContrastRange);

	// 시작: 가장 먼저 나타나는 픽셀( 바닥 h=0, 노이즈 0 )도 아직 HiddenScore 이하인 값.
	const float StartOffset = FMath::Pow(HiddenScore, 1.0f / Power) - 1.0f;
	// 끝: 가장 늦게 나타나는 픽셀( 꼭대기 h=1, 노이즈 최대 )까지 RevealedScore 에 닿는 값.
	const float NoiseMax = FMath::Clamp(EnemySpawnDissolveNoiseMax, 0.0f, 1.0f);
	const float EndOffset = FMath::Pow(RevealedScore + TexturePower * NoiseMax, 1.0f / Power);

	// Offset = DA * Multiply / Divide - Subtract 를 DA 로 되돌린다.
	OutStartAmount = (StartOffset + Subtract) * Divide / Multiply;
	OutEndAmount = (EndOffset + Subtract) * Divide / Multiply;
	return true;
}

void AOutlierPostProcessVolume::SetEnemySpawnDissolveAmount(UMaterialInstanceDynamic* DissolveMaterial, float Amount) const
{
	if (DissolveMaterial)
	{
		DissolveMaterial->SetScalarParameterValue(SpawnDissolveAmountParameterName, Amount);
	}
}

void AOutlierPostProcessVolume::UpdateDamagedMaterialParameters(float InPlayerHPRatio)  const
{
	const TObjectPtr<UMaterialInterface>* DamagedMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Damaged);
	UMaterialInstanceDynamic* DamagedMID = DamagedMaterial ? Cast<UMaterialInstanceDynamic>(DamagedMaterial->Get()) : nullptr;

	if (DamagedMID)
	{
		DamagedMID->SetScalarParameterValue(TEXT("HP_Portion"), FMath::Clamp(InPlayerHPRatio, 0.0f, 1.0f));
	}
}

void AOutlierPostProcessVolume::UpdateDamagedMaterialParameters(float InPlayerHPRatio, FVector4 Color) const
{
	const TObjectPtr<UMaterialInterface>* DamagedMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Damaged);
	UMaterialInstanceDynamic* DamagedMID = DamagedMaterial ? Cast<UMaterialInstanceDynamic>(DamagedMaterial->Get()) : nullptr;

	if (DamagedMID)
	{
		DamagedMID->SetScalarParameterValue(TEXT("HP_Portion"), FMath::Clamp(InPlayerHPRatio, 0.0f, 1.0f));
		DamagedMID->SetVectorParameterValue(TEXT("DamagedColor"), Color);

	}
}

void AOutlierPostProcessVolume::DisableAllBlendablesHard()
{
	for (FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
	{
		Blendable.Weight = 0.0f;
	}

	bScanPostProcessEnabled = false;
	bStealthPostProcessEnabled = false;
	bDamagedPostProcessEnabled = false;
	bMagneticPostProcessEnabled = false;
	// 상시 패스지만 여기서는 같이 내린다. 하드 리셋이므로 예외를 두지 않는다.
	// 되살리는 책임은 UMaterialPostProcessSubsystem::ApplyAlwaysOnPostProcess 에 있다.
	bPartnerOutlinePostProcessEnabled = false;
}
