// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerDistanceComponent.h"
#include "Audio/OutlierAudioFeedback.h"
#include "LocalPlayerUISubSystem.h"
#include "OutlierPlayerState.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Shooter/ShooterCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"

UPartnerDistanceComponent::UPartnerDistanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPartnerDistanceComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshActivationFromPlayerState();
}

void UPartnerDistanceComponent::RefreshActivationFromPlayerState()
{
	RefreshCharacterRefsFromPlayerState();
	const AOutlierPlayerState* PS = PartnerCharacter
		? PartnerCharacter->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const bool bShouldTick = HasBegunPlay() && IsValid(PartnerCharacter) && IsValid(ShooterCharacter)
		&& PS && PS->IsPairSuitAcquired()
		&& (PartnerCharacter->HasAuthority() || PartnerCharacter->IsLocallyControlled());
	SetComponentTickEnabled(bShouldTick);

	if (bShouldTick)
	{
		CacheUISubsystem();
	}
	else
	{
		CachedSubsystem = nullptr;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ConnectionAudioCooldownTimer);
			World->GetTimerManager().ClearTimer(DisconnectionAudioCooldownTimer);
		}
		// Losing the suit or pair must not leave the Shooter disabled by a stale boundary.
		if (PartnerCharacter && PartnerCharacter->HasAuthority())
		{
			PartnerCharacter->SetBoundaryOutside(false);
		}
	}
}

void UPartnerDistanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ConnectionAudioCooldownTimer);
		World->GetTimerManager().ClearTimer(DisconnectionAudioCooldownTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UPartnerDistanceComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	
	if (!PartnerCharacter)
	{
		return;
	}

	if (!PartnerCharacter->HasAuthority() && !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}



	if (!ShooterCharacter)
	{
		RefreshCharacterRefsFromPlayerState();
		if (!ShooterCharacter)
		{
			return;
		}
	}

	if (!CachedSubsystem)
	{
		CacheUISubsystem();
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float Distance = CalculateDistance();
	const float BoundaryRadius = PartnerCharacter->SuitDisableBoundaryRadius; 
	const float DistanceRatio = BoundaryRadius > 0.0f ? Distance / BoundaryRadius : 0.0f;

	if (CachedSubsystem)
	{
		CachedSubsystem->PartnerDistanceUpdate(DistanceRatio);
	}

	if (PartnerCharacter->HasAuthority())
	{
		UpdateBoundaryState();
	}
}

void UPartnerDistanceComponent::UpdateBoundaryState()
{
	if (!IsComponentTickEnabled() || !PartnerCharacter || !ShooterCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	const float Distance = CalculateDistance();
	const bool bOutside = Distance > PartnerCharacter->SuitDisableBoundaryRadius;
	const bool bStateChanged = (PartnerCharacter->GetBoundaryOutside() == EPartnerBoundaryState::Outside) != bOutside;
	PartnerCharacter->SetBoundaryOutside(bOutside);
	if (bStateChanged)
	{
		PlayBoundaryTransitionAudio(bOutside);
	}
}

void UPartnerDistanceComponent::PlayBoundaryTransitionAudio(bool bOutside)
{
	UWorld* World = GetWorld();
	if (!World || !PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	FTimerHandle& CooldownTimer = bOutside ? DisconnectionAudioCooldownTimer : ConnectionAudioCooldownTimer;
	if (Timers.IsTimerActive(CooldownTimer))
	{
		return;
	}

	// Follow the same PlayerState link used to apply the Shooter's suit boundary state.
	AOutlierPlayerState* PS = PartnerCharacter->GetPlayerState<AOutlierPlayerState>();
	AShooterCharacter* Shooter = PS ? PS->GetShooterCharacter() : nullptr;
	if (!IsValid(Shooter))
	{
		return;
	}

	OutlierAudioFeedback::Owner(Shooter, bOutside
		? TEXT("Audio.Context.Player.01.State.Connection.Lost")
		: TEXT("Audio.Context.Player.01.State.Connection.On"));
	OutlierAudioFeedback::Owner(PartnerCharacter, bOutside
		? TEXT("Audio.Context.Player.02.State.Connection.Lost")
		: TEXT("Audio.Context.Player.02.State.Connection.On"));

	const float Cooldown = bOutside ? DisconnectionAudioCooldown : ConnectionAudioCooldown;
	if (Cooldown > 0.0f)
	{
		// Expiration only rearms this transition; it must not play a deferred sound.
		Timers.SetTimer(CooldownTimer, FTimerDelegate::CreateWeakLambda(this, [] {}), Cooldown, false);
	}
}

float UPartnerDistanceComponent::CalculateDistance() const
{
	if (!PartnerCharacter || !ShooterCharacter)
	{
		return 0.0f;
	}

	return FVector::Dist(
		PartnerCharacter->GetActorLocation(),
		ShooterCharacter->GetActorLocation()
	);
}

void UPartnerDistanceComponent::CacheUISubsystem()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		CachedSubsystem = nullptr;
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PC)
	{
		CachedSubsystem = nullptr;
		return;
	}

	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	CachedSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr;
}
