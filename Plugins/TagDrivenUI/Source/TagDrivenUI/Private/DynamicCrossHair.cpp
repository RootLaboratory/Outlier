// Fill out your copyright notice in the Description page of Project Settings.


#include "DynamicCrossHair.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LocalPlayerUISubSystem.h"

void UDynamicCrossHair::NativeConstruct()
{
	Super::NativeConstruct();
	if (ULocalPlayer* LP = GetOwningLocalPlayer())
	{
		CachedUISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>();
	}
}

void UDynamicCrossHair::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{

		Super::NativeTick(MyGeometry, InDeltaTime);

		APlayerController* PC = GetOwningPlayer();
		ACharacter* Character = IsValid(PC) ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		UCharacterMovementComponent* MoveComp = IsValid(Character) ? Character->GetCharacterMovement() : nullptr;

		if (!PC || !Character || !MoveComp)
		{
			UE_LOG(LogTemp, Error, TEXT("PC Null "));
		}
			const float MaxSpeed = MoveComp->GetMaxSpeed();
			const float CurrentSpeed = MoveComp->Velocity.Size2D();
			const float SpeedRatio = (MaxSpeed > KINDA_SMALL_NUMBER) ? (CurrentSpeed / MaxSpeed) : 0.f;
			float ClampedRatio = FMath::Clamp(SpeedRatio, 0.f, 1.f);
		
				
		 Ratio = ClampedRatio;

		OnCrossHairTick(InDeltaTime);

}



void UDynamicCrossHair::SpawnCriticalUI_Implementation()
{
	UE_LOG(LogTemp, Error, TEXT("CriticalHit!"));
}

void UDynamicCrossHair::SpawnHitUI_Implementation()
{
	UE_LOG(LogTemp, Error, TEXT("Hit!"));
}

void UDynamicCrossHair::SpawnAdaptedHitUI_Implementation()
{
	UE_LOG(LogTemp, Error, TEXT("AdaptedHit!"));
}

void UDynamicCrossHair::SpawnKillHitUI_Implementation()
{
	UE_LOG(LogTemp, Error, TEXT("Kill!"));
}
