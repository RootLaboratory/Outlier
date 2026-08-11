// Fill out your copyright notice in the Description page of Project Settings.


#include "DynamicCrossHair.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Image.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LocalPlayerUISubSystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"

DEFINE_LOG_CATEGORY_STATIC(LogDynamicCrossHair, Log, All);

namespace
{
	FString GetAttackSignName(EAttackSign InAttackSign)
	{
		const UEnum* AttackSignEnum = StaticEnum<EAttackSign>();
		return AttackSignEnum
			? AttackSignEnum->GetNameStringByValue(static_cast<int64>(InAttackSign))
			: FString::FromInt(static_cast<int32>(InAttackSign));
	}
}

void UDynamicCrossHair::NativeConstruct()
{
	Super::NativeConstruct();
	if (ULocalPlayer* LP = GetOwningLocalPlayer())
	{
		CachedUISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>();
	}

	InitializeAttackSignImages();
}

void UDynamicCrossHair::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateMoveSpread();
	UpdateMoveSpreadRecovery(InDeltaTime);

	UpdateShootSpread(InDeltaTime);

	CurrentStateSpread = CalculateStateSpread();
	UpdateFinalSpread();
	UpdateAttackSign(InDeltaTime);

	/*if (GEngine)
	{
		const FString DebugText = FString::Printf(
			TEXT("MoveSpread: %.2f | ShootSpread: %.2f | StateSpread: %.2f | FinalSpread: %.2f"),
			CurrentMoveSpread,
			CurrentShootSpread,
			CurrentStateSpread,
			FinalSpread
		);

		GEngine->AddOnScreenDebugMessage(
			12345,
			0.f,
			FColor::Green,
			DebugText
		);
	}*/

	OnCrossHairTick(InDeltaTime);
}

void UDynamicCrossHair::OnAiming()
{
	bAiming = true;
	//SetVisibility(ESlateVisibility::Hidden);

	CrossHairCollapsed();
}

void UDynamicCrossHair::OnAimingOff()
{
	bAiming = false;
	//SetVisibility(ESlateVisibility::Visible);

	CrossHairVisible();
}

void UDynamicCrossHair::SpawnAttackSign(EAttackSign InAttackSign)
{
	if (InAttackSign == EAttackSign::None)
	{
		UE_LOG(LogDynamicCrossHair, Log, TEXT("[AttackSign] Stop requested Widget=%s"), *GetName());
		StopAttackSign();
		BP_SpawnAttackSign(InAttackSign);
		return;
	}

	if (CanReuseAttackSignMIDs(InAttackSign))
	{
		UE_LOG(LogDynamicCrossHair, Log,
			TEXT("[AttackSign] Reuse cached MIDs Widget=%s Type=%s"),
			*GetName(),
			*GetAttackSignName(InAttackSign));
	}
	else
	{
		UMaterialInterface* AttackSignInstance = ResolveAttackSignMaterial(InAttackSign);
		UE_LOG(LogDynamicCrossHair, Log,
			TEXT("[AttackSign] Apply material Widget=%s Type=%s Material=%s"),
			*GetName(),
			*GetAttackSignName(InAttackSign),
			*GetNameSafe(AttackSignInstance));

		if (!ApplyAttackSignMaterial(AttackSignInstance))
		{
			UE_LOG(LogDynamicCrossHair, Warning,
				TEXT("[AttackSign] Failed to create MIDs Widget=%s Type=%s Material=%s"),
				*GetName(),
				*GetAttackSignName(InAttackSign),
				*GetNameSafe(AttackSignInstance));
			StopAttackSign();
			BP_SpawnAttackSign(InAttackSign);
			return;
		}

		CachedAttackSignType = InAttackSign;
	}

	AttackSignElapsedTime = 0.f;
	bAttackSignActive = true;
	SetAttackSignTime(0.f);
	SetAttackSignVisibility(ESlateVisibility::HitTestInvisible);

	/*UE_LOG(LogDynamicCrossHair, Log,
		TEXT("[AttackSign] Started Widget=%s Type=%s Time=0.000"),
		*GetName(),
		*GetAttackSignName(InAttackSign));*/

	BP_SpawnAttackSign(InAttackSign);
}

void UDynamicCrossHair::On_RepShoot()
{
	AddShootSpread();
}

void UDynamicCrossHair::SetPlayerState(EUIPlayerState InState)
{
	CurrentState = InState;
	CurrentStateSpread = CalculateStateSpread();
}

void UDynamicCrossHair::AddShootSpread()
{
	CurrentShootSpread = FMath::Clamp(CurrentShootSpread + ShootSpreadStep, 0.f, MaxSpread);
}

float UDynamicCrossHair::CalculateStateSpread() const
{
	switch (CurrentState)
	{
	case EUIPlayerState::Jump:
	case EUIPlayerState::Slide:
		return MaxSpread;

	case EUIPlayerState::Idle:
	case EUIPlayerState::Move: //캐릭터의 walk Run 다 있음. 
	default:
		return 0.f;
	}
}

void UDynamicCrossHair::UpdateMoveSpread()
{
	APlayerController* PC = GetOwningPlayer();
	ACharacter* Character = IsValid(PC) ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	UCharacterMovementComponent* MoveComp = IsValid(Character) ? Character->GetCharacterMovement() : nullptr;

	if (!PC || !Character || !MoveComp)
	{
		CurrentMoveSpread = 0.f;
		Ratio = 0.f;
		return;
	}

	const float MaxSpeed = MoveSpreadReferenceSpeed;
	const float CurrentSpeed = MoveComp->Velocity.Size2D();
	const float SpeedRatio = (MaxSpeed > KINDA_SMALL_NUMBER) ? (CurrentSpeed / MaxSpeed) : 0.f;

	Ratio = FMath::Clamp(SpeedRatio, 0.f, 1.f);

	if (CurrentState == EUIPlayerState::Move)
	{
		CurrentMoveSpread = MaxMoveSpread * Ratio;
	}

}

//상태 전환 되었을 때, Move Jump Slide -> Idle or Move
void UDynamicCrossHair::UpdateShootSpread(float InDeltaTime)
{
	if (CurrentState == EUIPlayerState::Idle || CurrentState == EUIPlayerState::Move)
	{
		CurrentShootSpread = FMath::FInterpTo(CurrentShootSpread, 0.f, InDeltaTime, ShootRecoverSpeed);
		CurrentShootSpread = FMath::Max(CurrentShootSpread, 0.f);
	}
}

void UDynamicCrossHair::UpdateMoveSpreadRecovery(float InDeltaTime)
{
	if (CurrentState == EUIPlayerState::Idle)
	{
		CurrentMoveSpread = FMath::FInterpTo(CurrentMoveSpread, 0.f, InDeltaTime, MoveRecoverSpeed);
		CurrentMoveSpread = FMath::Max(CurrentMoveSpread, 0.f);
	}
}

void UDynamicCrossHair::UpdateFinalSpread()
{
	FinalSpread = CurrentMoveSpread + CurrentStateSpread + CurrentShootSpread;
	FinalSpread = FMath::Clamp(FinalSpread, 0, MaxSpread);
}

void UDynamicCrossHair::InitializeAttackSignImages()
{
	UImage* AttackSignImages[] =
	{
		AttackSign_LeftTop,
		AttackSign_LeftDown,
		AttackSign_RightTop,
		AttackSign_RightDown
	};

	AttackSignTextures.SetNum(UE_ARRAY_COUNT(AttackSignImages));
	AttackSignMIDs.Reset();
	CachedAttackSignType = EAttackSign::None;
	AttackSignElapsedTime = 0.f;
	bAttackSignActive = false;

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AttackSignImages); ++Index)
	{
		UImage* AttackSignImage = AttackSignImages[Index];
		if (!AttackSignImage)
		{
			UE_LOG(LogDynamicCrossHair, Warning,
				TEXT("[AttackSign] Missing BindWidget image Widget=%s Index=%d"),
				*GetName(),
				Index);
			continue;
		}

		if (!AttackSignTextures[Index])
		{
			AttackSignTextures[Index] = Cast<UTexture>(AttackSignImage->GetBrush().GetResourceObject());
		}

		if (!AttackSignTextures[Index])
		{
			UE_LOG(LogDynamicCrossHair, Warning,
				TEXT("[AttackSign] Image has no source texture Widget=%s Image=%s Index=%d"),
				*GetName(),
				*GetNameSafe(AttackSignImage),
				Index);
		}

		AttackSignImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	/*UE_LOG(LogDynamicCrossHair, Log,
		TEXT("[AttackSign] Initialized Widget=%s ImageCount=%d"),
		*GetName(),
		UE_ARRAY_COUNT(AttackSignImages));*/
}

UMaterialInterface* UDynamicCrossHair::ResolveAttackSignMaterial(EAttackSign InAttackSign) const
{
	switch (InAttackSign)
	{
	case EAttackSign::Adjusted:
		return Adjusted ? Adjusted.Get() : AttackSignMaterial.Get();

	case EAttackSign::Critical:
		return Critical ? Critical.Get() : AttackSignMaterial.Get();

	case EAttackSign::Kill:
		return Kill ? Kill.Get() : AttackSignMaterial.Get();

	case EAttackSign::Default:
		return AttackSignMaterial.Get();

	case EAttackSign::None:
	default:
		return nullptr;
	}
}

bool UDynamicCrossHair::ApplyAttackSignMaterial(UMaterialInterface* InMaterial)
{
	if (!InMaterial)
	{
		UE_LOG(LogDynamicCrossHair, Warning,
			TEXT("[AttackSign] Material is null Widget=%s"),
			*GetName());
		return false;
	}

	UImage* AttackSignImages[] =
	{
		AttackSign_LeftTop,
		AttackSign_LeftDown,
		AttackSign_RightTop,
		AttackSign_RightDown
	};
	AttackSignMIDs.Reset();
	AttackSignMIDs.SetNum(UE_ARRAY_COUNT(AttackSignImages));
	bool bAllMIDsCreated = true;

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AttackSignImages); ++Index)
	{
		UImage* AttackSignImage = AttackSignImages[Index];
		if (!AttackSignImage)
		{
			UE_LOG(LogDynamicCrossHair, Warning,
				TEXT("[AttackSign] Cannot apply material: image is null Widget=%s Index=%d"),
				*GetName(),
				Index);
			bAllMIDsCreated = false;
			continue;
		}

		AttackSignImage->SetBrushFromMaterial(InMaterial);

		UMaterialInstanceDynamic* AttackSignMID = AttackSignImage->GetDynamicMaterial();
		AttackSignMIDs[Index] = AttackSignMID;
		UTexture* AttackSignTexture = AttackSignTextures.IsValidIndex(Index)
			? AttackSignTextures[Index].Get()
			: nullptr;

		if (!AttackSignMID)
		{
			UE_LOG(LogDynamicCrossHair, Warning,
				TEXT("[AttackSign] Dynamic material creation failed Widget=%s Image=%s Index=%d Material=%s"),
				*GetName(),
				*GetNameSafe(AttackSignImage),
				Index,
				*GetNameSafe(InMaterial));
			bAllMIDsCreated = false;
			continue;
		}

		if (AttackSignTexture)
		{
			AttackSignMID->SetTextureParameterValue(
				AttackSignTextureParameterName,
				AttackSignTexture);
		}
	}

	/*UE_LOG(LogDynamicCrossHair, Log,
		TEXT("[AttackSign] Material applied Widget=%s Material=%s MIDCount=%d Success=%s"),
		*GetName(),
		*GetNameSafe(InMaterial),
		AttackSignMIDs.Num(),
		bAllMIDsCreated ? TEXT("true") : TEXT("false"));*/

	return bAllMIDsCreated;
}

bool UDynamicCrossHair::CanReuseAttackSignMIDs(EAttackSign InAttackSign) const
{
	if (CachedAttackSignType != InAttackSign || AttackSignMIDs.Num() != 4)
	{
		return false;
	}

	for (const UMaterialInstanceDynamic* AttackSignMID : AttackSignMIDs)
	{
		if (!AttackSignMID)
		{
			return false;
		}
	}

	return true;
}

void UDynamicCrossHair::UpdateAttackSign(float InDeltaTime)
{
	if (!bAttackSignActive)
	{
		return;
	}

	AttackSignElapsedTime += FMath::Max(InDeltaTime, 0.f);
	const float SafeDuration = FMath::Max(AttackSignDuration, KINDA_SMALL_NUMBER);
	const float NormalizedTime = FMath::Clamp(AttackSignElapsedTime / SafeDuration, 0.f, 1.f);
	SetAttackSignTime(NormalizedTime);

	if (NormalizedTime >= 1.f)
	{
		UE_LOG(LogDynamicCrossHair, Log,
			TEXT("[AttackSign] Completed Widget=%s Type=%s Duration=%.3f Time=1.000"),
			*GetName(),
			*GetAttackSignName(CachedAttackSignType),
			AttackSignDuration);
		StopAttackSign();
	}
}

void UDynamicCrossHair::SetAttackSignTime(float InNormalizedTime)
{
	const float ClampedTime = FMath::Clamp(InNormalizedTime, 0.f, 1.f);
	/*UE_LOG(LogDynamicCrossHair, VeryVerbose,
		TEXT("[AttackSign] Update time Widget=%s Type=%s Time=%.3f"),
		*GetName(),
		*GetAttackSignName(CachedAttackSignType),
		ClampedTime);*/

	for (UMaterialInstanceDynamic* AttackSignMID : AttackSignMIDs)
	{
		if (AttackSignMID)
		{
			AttackSignMID->SetScalarParameterValue(AttackSignTimeParameterName, ClampedTime);
		}
	}
}

void UDynamicCrossHair::StopAttackSign()
{
	bAttackSignActive = false;
	AttackSignElapsedTime = 0.f;
	SetAttackSignVisibility(ESlateVisibility::Collapsed);
}

void UDynamicCrossHair::SetAttackSignVisibility(ESlateVisibility InVisibility)
{
	UImage* AttackSignImages[] =
	{
		AttackSign_LeftTop,
		AttackSign_LeftDown,
		AttackSign_RightTop,
		AttackSign_RightDown
	};

	for (UImage* AttackSignImage : AttackSignImages)
	{
		if (AttackSignImage)
		{
			AttackSignImage->SetVisibility(InVisibility);
		}
	}
}
