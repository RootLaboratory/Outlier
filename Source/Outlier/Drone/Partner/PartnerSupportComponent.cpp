// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerShieldSphere.h"
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

void UPartnerSupportComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyShieldActor_Server();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShieldMonitorTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
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
	ScanElapsedTime = 0.0f;
	ScanOrigin = PartnerCharacter->GetActorLocation();

	ScannedActors.Reset();

	/*UE_LOG(
		LogTemp,
		Error,
		TEXT("[PartnerScanDebug] ScanStart Origin=%s Range=%.2f"),
		*ScanOrigin.ToString(),
		PartnerCharacter->ScanRange
	);*/

	World->GetTimerManager().SetTimer(
		ScanTimerHandle,
		this,
		&UPartnerSupportComponent::UpdateScan_Server,
		0.03f,
		true
	);

	//Multicast 수정
	ClientStartScanVisual(
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
	SpawnShieldActor_Server(Shooter);

	PartnerCharacter->bShieldActive = true;

	GetWorld()->GetTimerManager().SetTimer(
		ShieldMonitorTimerHandle,
		this,
		&UPartnerSupportComponent::UpdateShield_Server,
		0.05f,
		true
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

void UPartnerSupportComponent::SpawnShieldActor_Server(AShooterCharacter* Shooter)
{
	UWorld* World = GetWorld();
	if (!World || !PartnerCharacter || !PartnerCharacter->HasAuthority() || !Shooter || !ShieldActorClass)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerShieldSpawn] Blocked World=%d Partner=%s Authority=%d Shooter=%s ShieldActorClass=%s"),
			World ? 1 : 0,
			*GetNameSafe(PartnerCharacter),
			PartnerCharacter && PartnerCharacter->HasAuthority() ? 1 : 0,
			*GetNameSafe(Shooter),
			*GetNameSafe(ShieldActorClass)
		);
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PartnerShieldSpawn] SpawnRequest Partner=%s Shooter=%s Class=%s Location=%s Rotation=%s"),
		*GetNameSafe(PartnerCharacter),
		*GetNameSafe(Shooter),
		*GetNameSafe(ShieldActorClass),
		*Shooter->GetActorLocation().ToString(),
		*Shooter->GetActorRotation().ToString()
	);

	DestroyShieldActor_Server();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Shooter;
	SpawnParams.Instigator = PartnerCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ActiveShieldActor = World->SpawnActor<APartnerShieldSphere>(
		ShieldActorClass,
		Shooter->GetActorLocation(),
		Shooter->GetActorRotation(),
		SpawnParams
	);

	if (ActiveShieldActor)
	{
		ActiveShieldActor->InitializeShield(Shooter, PartnerCharacter);

		/*UE_LOG(
			LogTemp,
			Log,
			TEXT("[PartnerShieldSpawn] SpawnSuccess Actor=%s Target=%s Radius=%.1f Offset=%s Location=%s"),
			*GetNameSafe(ActiveShieldActor),
			*GetNameSafe(ActiveShieldActor->GetShieldTarget()),
			ActiveShieldActor->GetShieldRadius(),
			*ActiveShieldActor->GetTargetRelativeLocation().ToString(),
			*ActiveShieldActor->GetActorLocation().ToString()
		);*/
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[PartnerShieldSpawn] SpawnFailed Partner=%s Shooter=%s Class=%s"),
			*GetNameSafe(PartnerCharacter),
			*GetNameSafe(Shooter),
			*GetNameSafe(ShieldActorClass)
		);
	}
}

void UPartnerSupportComponent::DestroyShieldActor_Server()
{
	if (ActiveShieldActor)
	{
		UE_LOG(LogTemp, Log, TEXT("[PartnerShieldSpawn] Destroy Actor=%s"), *GetNameSafe(ActiveShieldActor));
		ActiveShieldActor->EndShield();
		ActiveShieldActor = nullptr;
	}
}

void UPartnerSupportComponent::UpdateShield_Server()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		EndShield_Server();
		return;
	}

	AShooterCharacter* Shooter = PartnerCharacter->CachedShooterCharacter;
	if (!Shooter || Shooter->GetCurPartnerShield() <= 0.0f)
	{
		EndShield_Server();
	}
}

void UPartnerSupportComponent::EndShield_Server()
{
	if (!PartnerCharacter)
	{
		return;
	}

	PartnerCharacter->bShieldActive = false;
	GetWorld()->GetTimerManager().ClearTimer(ShieldMonitorTimerHandle);
	DestroyShieldActor_Server();
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

	ScanElapsedTime += TickInterval;

	const float ScanProgress = PartnerCharacter->ScanDuration > 0.0f
		? FMath::Clamp(ScanElapsedTime / PartnerCharacter->ScanDuration, 0.0f, 1.0f)
		: 1.0f;

	CurrentScanRadius = PartnerCharacter->ScanRange * ScanProgress;

	const float RadiusSq = FMath::Square(CurrentScanRadius);

	ClientUpdateScanVisual(
		ScanOrigin,
		CurrentScanRadius
	);

	TArray<FOverlapResult> Results;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerScan), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	World->OverlapMultiByObjectType(
		Results,
		ScanOrigin,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(CurrentScanRadius),
		QueryParams
	);

	for (const FOverlapResult& Result : Results)
	{
		AActor* Actor = Result.GetActor();

		if (!Actor)
		{
			continue;
		}

		if (ScannedActors.Contains(Actor))
		{
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
			TEXT("[PartnerScanDebug] ActorReached Actor=%s Class=%s Distance=%.2f Radius=%.2f"),
			*GetNameSafe(Actor),
			*GetNameSafe(Actor->GetClass()),
			FMath::Sqrt(DistanceSq),
			CurrentScanRadius
		);*/

		ScannedActors.Add(Actor);

		ApplyScanEffect(Actor);
	}

	if (ScanProgress >= 1.0f)
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
	CurrentScanRadius = 0.0f;
	ScanOrigin = FVector::ZeroVector;
	ScanElapsedTime = 0.0f;
	//UE_LOG(LogTemp, Error, TEXT("[PartnerScanDebug] ScanEnd"));

	ClientEndScanVisual();
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
		const int32 StencilValue = NativeScannable->GetScanStencilValue();
		//UE_LOG(LogTemp, Error, TEXT("NativeScannable Valid, %d"), StencilValue);
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
		ClientApplyScanEffect(Actor, StencilValue);
	}

}

void UPartnerSupportComponent::ClearScanEffect(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	ClientClearScanEffect(Actor);
}

void UPartnerSupportComponent::ApplyAreaOfEffect(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	// 임시: 로그만
	// UE_LOG(LogTemp, Log, TEXT("AOE Target: %s"), *GetNameSafe(Actor));
}

bool UPartnerSupportComponent::ShouldProcessLocalScanVisual() const
{
	return PartnerCharacter
		&& PartnerCharacter->IsLocallyControlled()
		&& GetWorld()
		&& GetWorld()->GetNetMode() != NM_DedicatedServer;
}

void UPartnerSupportComponent::ClientStartScanVisual_Implementation(
	FVector InScanOrigin,
	float InCurrentScanRadius,
	float InScanRange)
{
	if (!ShouldProcessLocalScanVisual())
	{
		return;
	}

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

void UPartnerSupportComponent::ClientUpdateScanVisual_Implementation(
	FVector InScanOrigin,
	float InCurrentScanRadius)
{
	if (!ShouldProcessLocalScanVisual())
	{
		return;
	}

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

void UPartnerSupportComponent::ClientApplyScanEffect_Implementation(AActor* Actor, int32 StencilValue)
{
	if (!ShouldProcessLocalScanVisual())
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		//UE_LOG(LogTemp, Error, TEXT("ClientApplyScanEffect_Implementation Valid, %d"), StencilValue);
		PostProcessSubsystem->ApplyScanStencil(Actor, StencilValue);
	}
}

void UPartnerSupportComponent::ClientClearScanEffect_Implementation(AActor* Actor)
{
	if (!ShouldProcessLocalScanVisual())
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->ClearScanStencil(Actor);
	}
}

void UPartnerSupportComponent::ClientEndScanVisual_Implementation()
{
	if (!ShouldProcessLocalScanVisual())
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->EndScanPostProcess();
	}
}
