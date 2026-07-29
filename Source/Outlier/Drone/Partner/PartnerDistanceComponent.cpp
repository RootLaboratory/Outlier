// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerDistanceComponent.h"
#include "LocalPlayerUISubSystem.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Shooter/ShooterCharacter.h"

UPartnerDistanceComponent::UPartnerDistanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPartnerDistanceComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheUISubsystem();
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
		const bool bOutside = Distance > BoundaryRadius;
		PartnerCharacter->SetBoundaryOutside(bOutside);
	}
}

void UPartnerDistanceComponent::UpdateBoundaryState()
{
	if (!PartnerCharacter || !ShooterCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	const float Distance = CalculateDistance();
	const bool bOutside = Distance > PartnerCharacter->SuitDisableBoundaryRadius;
	PartnerCharacter->SetBoundaryOutside(bOutside);
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
