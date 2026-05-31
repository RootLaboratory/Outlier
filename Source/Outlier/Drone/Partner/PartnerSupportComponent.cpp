// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Interface/ScannableInterface.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Components/ActorComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"

// Sets default values for this component's properties
UPartnerSupportComponent::UPartnerSupportComponent()
{

	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

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
	UWorld* World = GetWorld();
	if (!World || !IsValid(PartnerCharacter) || !PartnerCharacter->HasAuthority())
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
	ScanOrigin = PartnerCharacter->GetActorLocation();

	PendingScanActors.Reset();
	ScannedActors.Reset();

	TArray<FOverlapResult> Results;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PartnerCharacter);

	World->OverlapMultiByObjectType(
		Results,
		ScanOrigin,
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

	/*UE_LOG(
		LogTemp,
		Error,
		TEXT("[PartnerScanDebug] ScanStart Origin=%s Range=%.2f PendingCount=%d"),
		*ScanOrigin.ToString(),
		PartnerCharacter->ScanRange,
		PendingScanActors.Num()
	);*/

	World->GetTimerManager().SetTimer(
		ScanTimerHandle,
		this,
		&UPartnerSupportComponent::UpdateScan_Server,
		0.03f,
		true
	);

	MulticastStartScanVisual(
		ScanOrigin,
		CurrentScanRadius,
		PartnerCharacter->ScanRange
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
	const UWorld* World = GetWorld();
	if (!World || !IsValid(PartnerCharacter) || !PartnerCharacter->HasAuthority())
	{
		return false;
	}

	const float Now = World->GetTimeSeconds();

	if (const float* LastTime = LastUseTimes.Find(SkillName))
	{
		return Now - *LastTime >= CoolDown;
	}

	return true;
}

void UPartnerSupportComponent::MarkSkillUsed(FName SkillName)
{
	if (const UWorld* World = GetWorld())
	{
		LastUseTimes.FindOrAdd(SkillName) = World->GetTimeSeconds();
	}
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
	UWorld* World = GetWorld();
	if (!World || !IsValid(PartnerCharacter))
	{
		EndScan_Server();
		return;
	}

	const float TickInterval = 0.03f;

	CurrentScanRadius += PartnerCharacter->ScanExpandSpeed * TickInterval;

	const float RadiusSq = FMath::Square(CurrentScanRadius);

	MulticastUpdateScanVisual(
		ScanOrigin,
		CurrentScanRadius
	);


	for (int32 Index = PendingScanActors.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = PendingScanActors[Index].Get();

		if (!Actor)
		{
			UE_LOG(LogTemp, Error, TEXT("[PartnerScanDebug] PendingRemove NullActor Index=%d"), Index);
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

		/*UE_LOG(
			LogTemp,
			Error,
			TEXT("[PartnerScanDebug] ActorReached Actor=%s Class=%s Distance=%.2f Radius=%.2f PendingBefore=%d"),
			*GetNameSafe(Actor),
			*GetNameSafe(Actor->GetClass()),
			FMath::Sqrt(DistanceSq),
			CurrentScanRadius,
			PendingScanActors.Num()
		);*/

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
	UWorld* World = GetWorld();
	if (!World || !IsValid(PartnerCharacter))
	{
		return;
	}

	World->GetTimerManager().ClearTimer(ScanTimerHandle);

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
	ScanOrigin = FVector::ZeroVector;

	UE_LOG(LogTemp, Error, TEXT("[PartnerScanDebug] ScanEnd"));

	MulticastEndScanVisual();
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

int32 UPartnerSupportComponent::ResolveScanStencilValue(AActor* Actor) const
{
	if (!Actor)
	{
		return 0;
	}

	/*UE_LOG(
		LogTemp,
		Error,
		TEXT("ResolveScanStencilValue Target=%s Class=%s Implements=%d NativeCast=%d"),
		*GetNameSafe(Actor),
		*GetNameSafe(Actor->GetClass()),
		Actor->GetClass()->ImplementsInterface(UScannableInterface::StaticClass()) ? 1 : 0,
		Cast<IScannableInterface>(Actor) ? 1 : 0
	);*/

	if (const IScannableInterface* NativeScannable = Cast<IScannableInterface>(Actor))
	{
		const int32 StencilValue = NativeScannable->Execute_GetScanStencilValue(Actor);
		UE_LOG(LogTemp, Error, TEXT("NativeScannable Valid, %d"), StencilValue);
		return StencilValue;
	}

	if (Actor->GetClass()->ImplementsInterface(UScannableInterface::StaticClass()))
	{
		const int32 StencilValue = IScannableInterface::Execute_GetScanStencilValue(Actor);
		//UE_LOG(LogTemp, Error, TEXT("ImplementsInterface Valid, %d"), StencilValue);
		return StencilValue;
	}


	return DefaultScanStencilValue;
}

void UPartnerSupportComponent::ApplyScanEffect(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (int32 StencilValue = ResolveScanStencilValue(Actor))
	{
		MulticastApplyScanEffect(Actor, StencilValue);
	}

}

void UPartnerSupportComponent::ClearScanEffect(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	MulticastClearScanEffect(Actor);
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

void UPartnerSupportComponent::MulticastStartScanVisual_Implementation(
	FVector InScanOrigin,
	float InCurrentScanRadius,
	float InScanRange)
{
	//DrawDebugSphere(
	//	GetWorld(),
	//	InScanOrigin,
	//	InCurrentScanRadius,
	//	32,
	//	FColor::Cyan,
	//	false,
	//	0.25f,
	//	0,
	//	2.0f
	//);

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->StartScanPostProcess(InScanOrigin, InCurrentScanRadius, InScanRange);
	}
}

void UPartnerSupportComponent::MulticastUpdateScanVisual_Implementation(
	FVector InScanOrigin,
	float InCurrentScanRadius)
{
	/*DrawDebugSphere(
		GetWorld(),
		InScanOrigin,
		InCurrentScanRadius,
		32,
		FColor::Cyan,
		false,
		0.05f,
		0,
		1.0f
	);*/

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->UpdateScanPostProcess(InScanOrigin, InCurrentScanRadius);
	}
}

void UPartnerSupportComponent::MulticastApplyScanEffect_Implementation(AActor* Actor, int32 StencilValue)
{
	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		UE_LOG(LogTemp, Error, TEXT("MulticastApplyScanEffect_Implementation Valid, %d"), StencilValue);
		PostProcessSubsystem->ApplyScanStencil(Actor, StencilValue);
	}
}

void UPartnerSupportComponent::MulticastClearScanEffect_Implementation(AActor* Actor)
{
	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->ClearScanStencil(Actor);
	}
}

void UPartnerSupportComponent::MulticastEndScanVisual_Implementation()
{
	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->EndScanPostProcess();
	}
}
