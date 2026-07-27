// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerShieldSphere.h"
#include "Interface/ScannableInterface.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Components/ActorComponent.h"
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
		World->GetTimerManager().ClearTimer(ScanServerStateTimerHandle);
		World->GetTimerManager().ClearTimer(ScanClientUpdateTimerHandle);
	}

	if (ActiveScanRange > 0.0f || ActiveScanDuration > 0.0f || !ScannedActors.IsEmpty())
	{
		EndScan_Client();
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
	const FVector InScanOrigin = PartnerCharacter->GetActorLocation();
	const float InScanRange = PartnerCharacter->ScanRange;
	const float InScanDuration = PartnerCharacter->ScanDuration;

	//Multicast 수정
	ClientStartScanVisual(
		InScanOrigin,
		InScanRange,
		InScanDuration
	);

	World->GetTimerManager().SetTimer(
		ScanServerStateTimerHandle,
		this,
		&UPartnerSupportComponent::EndScan_Server,
		FMath::Max(InScanDuration, KINDA_SMALL_NUMBER),
		false
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

void UPartnerSupportComponent::UpdateScan_Client()
{
	UWorld* World = GetWorld();
	if (!World || !ShouldProcessLocalScanVisual())
	{
		EndScan_Client();
		return;
	}

	ScanElapsedTime += ScanUpdateInterval;

	const float ScanProgress = ActiveScanDuration > 0.0f
		? FMath::Clamp(ScanElapsedTime / ActiveScanDuration, 0.0f, 1.0f)
		: 1.0f;

	CurrentScanRadius = ActiveScanRange * ScanProgress;

	const float RadiusSq = FMath::Square(CurrentScanRadius);

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->UpdateScanPostProcess(ScanOrigin, CurrentScanRadius);
	}

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

		const FVector ActorLocation = Actor->GetActorLocation();
		const float DistanceSq = FVector::DistSquared(
			ScanOrigin,
			ActorLocation
		);

		if (DistanceSq > RadiusSq)
		{
			continue;
		}

		ScannedActors.Add(Actor);

		ApplyScanEffect(Actor);
	}

	if (ScanProgress >= 1.0f)
	{
		EndScan_Client();
	}
}

void UPartnerSupportComponent::EndScan_Server()
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(PartnerCharacter))
	{
		return;
	}

	World->GetTimerManager().ClearTimer(ScanServerStateTimerHandle);
	PartnerCharacter->bScanning = false;
}

void UPartnerSupportComponent::EndScan_Client()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(ScanClientUpdateTimerHandle);
	}

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
	ActiveScanRange = 0.0f;
	ActiveScanDuration = 0.0f;

	if (World)
	{
		if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PostProcessSubsystem->EndScanPostProcess();
		}
	}
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

	if (const IScannableInterface* NativeScannable = Cast<IScannableInterface>(Actor))
	{
		const int32 StencilValue = NativeScannable->GetScanStencilValue();
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
		if (UWorld* World = GetWorld())
		{
			if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
			{
				PostProcessSubsystem->ApplyScanStencil(Actor, StencilValue);
			}
		}
	}

}

void UPartnerSupportComponent::ClearScanEffect(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PostProcessSubsystem->ClearScanStencil(Actor);
		}
	}
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
	float InScanRange,
	float InScanDuration)
{
	if (!ShouldProcessLocalScanVisual())
	{
		return;
	}

	EndScan_Client();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ScanOrigin = InScanOrigin;
	CurrentScanRadius = 0.0f;
	ScanElapsedTime = 0.0f;
	ActiveScanRange = FMath::Max(0.0f, InScanRange);
	ActiveScanDuration = FMath::Max(0.0f, InScanDuration);
	ScannedActors.Reset();

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->StartScanPostProcess(ScanOrigin, CurrentScanRadius, ActiveScanRange);
	}

	World->GetTimerManager().SetTimer(
		ScanClientUpdateTimerHandle,
		this,
		&UPartnerSupportComponent::UpdateScan_Client,
		ScanUpdateInterval,
		true
	);
}
