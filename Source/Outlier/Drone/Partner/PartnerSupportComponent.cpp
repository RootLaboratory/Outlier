// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "Engine/OverlapResult.h"

// Sets default values for this component's properties
UPartnerSupportComponent::UPartnerSupportComponent()
{

	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void UPartnerSupportComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}

void UPartnerSupportComponent::TryHack_Server(AActor* TargetActor)
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		NotifySkillResult(EPartnerSkillType::Hack, EPartnerSkillUseResult::InvalidState);
		return;
	}

	if (!CanUseSkill_Server(TEXT("Hack"), PartnerCharacter->HackCooldown))
	{
		NotifySkillResult(EPartnerSkillType::Hack, EPartnerSkillUseResult::Cooldown);
		return;
	}

	AActor* HackTarget = TargetActor ? TargetActor : FindTarget(PartnerCharacter->HackRange);
	if (!HackTarget)
	{
		NotifySkillResult(EPartnerSkillType::Hack, EPartnerSkillUseResult::NoTarget);
		return;
	}

	const float Distance = FVector::Dist(
		PartnerCharacter->GetActorLocation(),
		HackTarget->GetActorLocation()
	);

	if (Distance > PartnerCharacter->HackRange)
	{
		NotifySkillResult(EPartnerSkillType::Hack, EPartnerSkillUseResult::OutOfRange);
		return;
	}

	if (PartnerCharacter->bRequireLineOfSight && !HasLineOfSight(HackTarget))
	{
		NotifySkillResult(EPartnerSkillType::Hack, EPartnerSkillUseResult::NoLineOfSight);
		return;
	}

	MarkSkillUsed(TEXT("Hack"));
	PartnerCharacter->LastHackServerTime = GetWorld()->GetTimeSeconds();
	NotifySkillResult(EPartnerSkillType::Hack, EPartnerSkillUseResult::Success);

	// 해킹 로직
}

void UPartnerSupportComponent::TryAreaOfEffect_Server()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		NotifySkillResult(EPartnerSkillType::AreaOfEffect, EPartnerSkillUseResult::InvalidState);
		return;
	}

	if (!CanUseSkill_Server(TEXT("AreaOfEffect"), PartnerCharacter->AreaOfEffectCooldown))
	{
		NotifySkillResult(EPartnerSkillType::AreaOfEffect, EPartnerSkillUseResult::Cooldown);
		return;
	}

	MarkSkillUsed(TEXT("AreaOfEffect"));

	TArray<FOverlapResult> Results;


	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PartnerCharacter);

	GetWorld()->OverlapMultiByObjectType(
		Results,
		PartnerCharacter->GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(PartnerCharacter->AreaOfEffectRange),
		QueryParams
	);

	for (const FOverlapResult& Result : Results)
	{
		AActor* Actor = Result.GetActor();
		if (!Actor || Actor == PartnerCharacter)
		{
			continue;
		}

		// 조건은 나중에 켜기
		// if (!IsInsideView(Actor)) continue;
		// if (!HasLineOfSight(Actor)) continue;

		ApplyAreaOfEffect(Actor);
	}

	NotifySkillResult(EPartnerSkillType::AreaOfEffect, EPartnerSkillUseResult::Success);
}

void UPartnerSupportComponent::TryScan_Server()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		NotifySkillResult(EPartnerSkillType::Scan, EPartnerSkillUseResult::InvalidState);
		return;
	}

	if (!CanUseSkill_Server(TEXT("Scan"), PartnerCharacter->ScanCooldown))
	{
		NotifySkillResult(EPartnerSkillType::Scan, EPartnerSkillUseResult::Cooldown);
		return;
	}

	MarkSkillUsed(TEXT("Scan"));

	PartnerCharacter->bScanning = true;
	CurrentScanRadius = 0.0f;

	PendingScanActors.Reset();
	ScannedActors.Reset();

	TArray<FOverlapResult> Results;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PartnerCharacter);

	GetWorld()->OverlapMultiByObjectType(
		Results,
		PartnerCharacter->GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(PartnerCharacter->ScanRange),
		QueryParams
	);

	for (const FOverlapResult& Result : Results)
	{
		if (AActor* Actor = Result.GetActor())
		{
			PendingScanActors.Add(Actor);
		}
	}

	GetWorld()->GetTimerManager().SetTimer(
		ScanTimerHandle,
		this,
		&UPartnerSupportComponent::UpdateScan_Server,
		0.03f,
		true
	);

	NotifySkillResult(EPartnerSkillType::Scan, EPartnerSkillUseResult::Success);
}

void UPartnerSupportComponent::TryShield_Server()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::InvalidState);
		return;
	}

	if (!CanUseSkill_Server(TEXT("Shield"), PartnerCharacter->ShieldCooldown))
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::Cooldown);
		return;
	}

	if (!CanUseShield())
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::OutOfRange);
		return;
	}

	AShooterCharacter* Shooter = PartnerCharacter->CachedShooterCharacter;
	if (!Shooter)
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::NoTarget);
		return;
	}

	MarkSkillUsed(TEXT("Shield"));

	const float ShieldAmount = PartnerCharacter->ShieldAmount;
	const float ShieldDuration = PartnerCharacter->ShieldDuration;

	Shooter->ApplyPartnerShield(ShieldAmount, ShieldDuration);

	PartnerCharacter->bShieldActive = true;
	ShieldElapsedTime = 0.0f;

	GetWorld()->GetTimerManager().SetTimer(
		ShieldTimerHandle,
		this,
		&UPartnerSupportComponent::EndShield_Server,
		ShieldDuration,
		false
	);

	NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::Success);
}

bool UPartnerSupportComponent::CanUseSkill_Server(FName SkillName, float CoolDown) const
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return false;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (const float* LastTime = LastUseTimes.Find(SkillName))
	{
		return Now - *LastTime >= CoolDown;
	}

	return true;
}

void UPartnerSupportComponent::MarkSkillUsed(FName SkillName)
{
	LastUseTimes.FindOrAdd(SkillName) = GetWorld()->GetTimeSeconds();
}

AActor* UPartnerSupportComponent::FindTarget(float Range) const
{
	if (!PartnerCharacter || !PartnerCharacter->GetController())
	{
		return nullptr;
	}

	FVector  Start;
	FRotator Rotation;

	PartnerCharacter->GetController()->GetPlayerViewPoint(Start, Rotation);

	const FVector End = Start + Rotation.Vector() * Range;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(PartnerCharacter);

	bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		Params
	);

	return bHit ? Hit.GetActor() : nullptr;
}

void UPartnerSupportComponent::EndShield_Server()
{
	if (!PartnerCharacter)
	{
		return;
	}

	PartnerCharacter->bShieldActive = false;
	GetWorld()->GetTimerManager().ClearTimer(ShieldTimerHandle);
}

void UPartnerSupportComponent::UpdateScan_Server()
{
	if (!PartnerCharacter)
	{
		EndScan_Server();
		return;
	}

	const float TickInterval = 0.03f;

	CurrentScanRadius += PartnerCharacter->ScanExpandSpeed * TickInterval;

	const FVector ScanOrigin = PartnerCharacter->GetActorLocation();
	const float RadiusSq = FMath::Square(CurrentScanRadius);

	for (int32 Index = PendingScanActors.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = PendingScanActors[Index].Get();

		if (!Actor)
		{
			PendingScanActors.RemoveAtSwap(Index);
			continue;
		}

		const float DistanceSq = FVector::DistSquared(
			ScanOrigin,
			Actor->GetActorLocation()
		);

		if (DistanceSq > RadiusSq)
		{
			continue;
		}

		ScannedActors.Add(Actor);
		PendingScanActors.RemoveAtSwap(Index);

		ApplyScanEffect(Actor);
	}

	if (CurrentScanRadius >= PartnerCharacter->ScanRange)
	{
		EndScan_Server();
	}
}

void UPartnerSupportComponent::EndScan_Server()
{
	if (!PartnerCharacter)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ScanTimerHandle);

	PartnerCharacter->bScanning = false;

	for (const TWeakObjectPtr<AActor>& WeakActor : ScannedActors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			ClearScanEffect(Actor);
		}
	}

	ScannedActors.Reset();
	PendingScanActors.Reset();
	CurrentScanRadius = 0.0f;
}

bool UPartnerSupportComponent::CanUseShield() const
{
	if (!PartnerCharacter)
	{
		return false;
	}

	AOutlierPlayerState* PS = PartnerCharacter->GetPlayerState<AOutlierPlayerState>();

	if (!PS)
	{
		return false;
	}
	
	const float Distance = PS->GetPartnerDistance();

	return Distance <= PartnerCharacter->ShieldRange;
}

void UPartnerSupportComponent::NotifySkillResult(EPartnerSkillType SkillType, EPartnerSkillUseResult Result) const
{
	if (PartnerCharacter)
	{
		PartnerCharacter->ClientNotifySkillUseResult(SkillType, Result);
	}
}

bool UPartnerSupportComponent::IsInsideView(AActor* Actor) const
{
	return false;
}

bool UPartnerSupportComponent::HasLineOfSight(AActor* Actor) const
{
	if (!PartnerCharacter || !Actor || !GetWorld())
	{
		return false;
	}

	FVector Start;
	FRotator Rotation;
	if (AController* Controller = PartnerCharacter->GetController())
	{
		Controller->GetPlayerViewPoint(Start, Rotation);
	}
	else
	{
		Start = PartnerCharacter->GetActorLocation();
	}

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PartnerLineOfSight), false);
	Params.AddIgnoredActor(PartnerCharacter);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		Actor->GetActorLocation(),
		ECC_Visibility,
		Params
	);

	return !bHit || Hit.GetActor() == Actor;
}

void UPartnerSupportComponent::ApplyScanEffect(AActor* Actor)
{
}

void UPartnerSupportComponent::ClearScanEffect(AActor* Actor)
{
}

void UPartnerSupportComponent::ApplyAreaOfEffect(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	// 임시: 로그만
	UE_LOG(LogTemp, Log, TEXT("AOE Target: %s"), *GetNameSafe(Actor));
}
