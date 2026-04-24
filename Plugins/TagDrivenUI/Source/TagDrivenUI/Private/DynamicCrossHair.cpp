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

	UpdateMoveSpread();
	UpdateMoveSpreadRecovery(InDeltaTime);

	UpdateShootSpread(InDeltaTime);

	CurrentStateSpread = CalculateStateSpread();
	UpdateFinalSpread();

	if (GEngine)
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
	}

	OnCrossHairTick(InDeltaTime);
}

void UDynamicCrossHair::OnAiming()
{

	UE_LOG(LogTemp, Error, TEXT("OnAiming"));

	bAiming = true;

	CrossHairCollapsed();
}

void UDynamicCrossHair::OnAimingOff()
{
	bAiming = false;

	CrossHairVisible();
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

	//Max 다른 곳.
	const float MaxSpeed = MoveSpreadReferenceSpeed;
	const float CurrentSpeed = MoveComp->Velocity.Size2D();
	const float SpeedRatio = (MaxSpeed > KINDA_SMALL_NUMBER) ? (CurrentSpeed / MaxSpeed) : 0.f;

	UE_LOG(LogTemp, Error, TEXT("MaxSpeed %f") , MaxSpeed);
	UE_LOG(LogTemp, Error, TEXT("CurrentSpeed %f"), CurrentSpeed);


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





