// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerShieldSphere.h"
#include "Drone/Partner/PartnerShieldRecipientComponent.h"
#include "Enemy/AutoTurret.h"
#include "EngineUtils.h"
#include "Team/OutlierTeamIds.h"
#include "Interface/ScannableInterface.h"
#include "Interface/HackableInterface.h"
#include "Drone/Partner/HackableComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Components/ActorComponent.h"
#include "Engine/OverlapResult.h"
#include "GAS/OutlierAbilitySystemComponent.h"

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

EPartnerSkillUseResult UPartnerSupportComponent::TryScan_Server()
{
	UWorld* World = GetWorld();
	if (!World
		|| !IsValid(PartnerCharacter)
		|| !PartnerCharacter->HasAuthority()
		|| !PartnerCharacter->CanAcceptInput())
	{
		NotifySkillResult(EPartnerSkillType::Scan, EPartnerSkillUseResult::InvalidState);
		return EPartnerSkillUseResult::InvalidState;
	}

	const FVector InScanOrigin = PartnerCharacter->GetActorLocation();
	const UOutlierAbilitySystemComponent* AbilitySystem =
		PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (!AbilitySystem || !AbilitySystem->IsPartnerAbilitiesConfigured())
	{
		NotifySkillResult(EPartnerSkillType::Scan, EPartnerSkillUseResult::InvalidState);
		return EPartnerSkillUseResult::InvalidState;
	}
	const FOutlierPartnerAbilityConfig& Config = AbilitySystem->GetPartnerAbilityConfig();
	const float InScanRange = Config.ScanRange;
	const float InScanDuration = Config.ScanDuration;

	// 정찰 해금도 범위처럼 서버에서 정해 보낸다. 클라가 태그 복제보다 RPC를 먼저 받을 수 있어서다.
	const UOutlierUpgradeComponent* UpgradeComponent = PartnerCharacter->GetUpgradeComponent();
	const bool bRevealHackable = UpgradeComponent
		&& UpgradeComponent->HasUpgradeTag(OutlierGameplayTags::Upgrade::Partner::Scan::RevealHackable());
	const bool bRevealNodeFragments = UpgradeComponent
		&& UpgradeComponent->HasUpgradeTag(OutlierGameplayTags::Upgrade::Partner::Scan::RevealNodeFragments());
	PartnerCharacter->bScanning = true;

	// Shooter도 같은 스캔 연출을 보도록 모든 클라이언트에 보낸다.
	MulticastStartScanVisual(
		InScanOrigin,
		InScanRange,
		InScanDuration,
		bRevealHackable,
		bRevealNodeFragments
	);

	World->GetTimerManager().SetTimer(
		ScanServerStateTimerHandle,
		this,
		&UPartnerSupportComponent::EndScan_Server,
		FMath::Max(InScanDuration, KINDA_SMALL_NUMBER),
		false
	);

	NotifySkillResult(EPartnerSkillType::Scan, EPartnerSkillUseResult::Success);
	return EPartnerSkillUseResult::Success;
}

EPartnerSkillUseResult UPartnerSupportComponent::TryShield_Server()
{
	if (!PartnerCharacter
		|| !PartnerCharacter->HasAuthority()
		|| !PartnerCharacter->CanAcceptInput())
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::InvalidState);
		return EPartnerSkillUseResult::InvalidState;
	}

	if (!CanUseShield())
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::OutOfRange);
		return EPartnerSkillUseResult::OutOfRange;
	}

	AShooterCharacter* Shooter = PartnerCharacter->CachedShooterCharacter;

	if (!Shooter)
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::NoTarget);
		return EPartnerSkillUseResult::NoTarget;
	}

	const UOutlierAbilitySystemComponent* AbilitySystem =
		PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (!AbilitySystem || !AbilitySystem->IsPartnerAbilitiesConfigured())
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::InvalidState);
		return EPartnerSkillUseResult::InvalidState;
	}
	const float ShieldAmount = AbilitySystem->GetPartnerAbilityConfig().ShieldAmount;
	const float ShieldDuration = PartnerCharacter->ShieldDuration;
	const float HealthRegenRate = AbilitySystem->GetPartnerAbilityConfig().ShieldHealthRegenRate;

	if (!Shooter->ApplyPartnerShield(ShieldAmount, ShieldDuration, HealthRegenRate))
	{
		NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::InvalidState);
		return EPartnerSkillUseResult::InvalidState;
	}
	SpawnShieldActor_Server(Shooter);

	const UOutlierUpgradeComponent* Upgrade = PartnerCharacter->GetUpgradeComponent();
	const FGameplayTag TurretShieldTag = FGameplayTag::RequestGameplayTag(TEXT("Upgrade.Partner.Shield.HackedTurretShield"));
	if (Upgrade && Upgrade->HasUpgradeTag(TurretShieldTag))
	{
		if (AAutoTurret* Turret = FindNearestShieldTurret())
		{
			if (UPartnerShieldRecipientComponent* Recipient = Turret->FindComponentByClass<UPartnerShieldRecipientComponent>())
			{
				if (Recipient->ApplyShield(ShieldAmount, ShieldDuration, HealthRegenRate)) SpawnShieldActor_Server(Turret);
			}
		}
	}

	PartnerCharacter->bShieldActive = true;

	GetWorld()->GetTimerManager().SetTimer(
		ShieldMonitorTimerHandle,
		this,
		&UPartnerSupportComponent::UpdateShield_Server,
		0.05f,
		true
	);

	NotifySkillResult(EPartnerSkillType::Shield, EPartnerSkillUseResult::Success);
	return EPartnerSkillUseResult::Success;
}

AAutoTurret* UPartnerSupportComponent::FindNearestShieldTurret() const
{
	if (!PartnerCharacter || !GetWorld()) return nullptr;
	AAutoTurret* Nearest = nullptr;
	double NearestDistanceSq = FMath::Square(static_cast<double>(PartnerCharacter->ShieldRange));
	for (TActorIterator<AAutoTurret> It(GetWorld()); It; ++It)
	{
		AAutoTurret* Turret = *It;
		if (!IsValid(Turret) || !Turret->IsHackedToPlayerTeam() || Turret->IsDead()
			|| Turret->GetCurrentHealth() <= 0.0f || Turret->GetGenericTeamId().GetId() != OutlierTeamIds::Player
			|| Turret->GetTurretLifecycleState() != EAutoTurretLifecycleState::Active) continue;
		const double DistanceSq = FVector::DistSquared(PartnerCharacter->GetActorLocation(), Turret->GetActorLocation());
		if (DistanceSq <= NearestDistanceSq)
		{
			Nearest = Turret;
			NearestDistanceSq = DistanceSq;
		}
	}
	return Nearest;
}

void UPartnerSupportComponent::SpawnShieldActor_Server(AActor* Target)
{
	if (!IsValid(Target) || !PartnerCharacter || !PartnerCharacter->HasAuthority()) return;
	if (UPartnerShieldRecipientComponent* Recipient = Target->FindComponentByClass<UPartnerShieldRecipientComponent>())
	{
		ActiveShieldRecipients.AddUnique(Recipient);
		Recipient->ShowShield(ShieldActorClass, PartnerCharacter);
	}
}

void UPartnerSupportComponent::DestroyShieldActor_Server()
{
	for (UPartnerShieldRecipientComponent* Recipient : ActiveShieldRecipients)
		if (IsValid(Recipient)) Recipient->DestroyVisual();
	ActiveShieldRecipients.Reset();
}

void UPartnerSupportComponent::UpdateShield_Server()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		EndShield_Server();
		return;
	}
	for (int32 Index = ActiveShieldRecipients.Num() - 1; Index >= 0; --Index)
	{
		UPartnerShieldRecipientComponent* Recipient = ActiveShieldRecipients[Index];
		if (!IsValid(Recipient) || !Recipient->HasShield())
		{
			if (IsValid(Recipient)) Recipient->DestroyVisual();
			ActiveShieldRecipients.RemoveAtSwap(Index);
		}
	}
	if (ActiveShieldRecipients.IsEmpty()) EndShield_Server();
}

void UPartnerSupportComponent::EndShield_Server()
{
	if (PartnerCharacter) PartnerCharacter->bShieldActive = false;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ShieldMonitorTimerHandle);
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

	const bool bWasScanning = PartnerCharacter->bScanning;
	World->GetTimerManager().ClearTimer(ScanServerStateTimerHandle);
	PartnerCharacter->bScanning = false;
	if (bWasScanning)
	{
		OnScanFinished.Broadcast();
	}
}

void UPartnerSupportComponent::CancelForReboot()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	EndScan_Server();
	// Multicast는 리슨 서버 자신에서도 실행되므로 로컬 정리를 따로 호출하지 않는다.
	MulticastCancelScanForReboot();
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
	bScanRevealHackable = false;
	bScanRevealNodeFragments = false;

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
	const AShooterCharacter* Shooter = PartnerCharacter->CachedShooterCharacter;
	return Distance <= PartnerCharacter->ShieldRange && IsValid(Shooter)
		&& FVector::DistSquared(PartnerCharacter->GetActorLocation(), Shooter->GetActorLocation())
			<= FMath::Square(PartnerCharacter->ShieldRange);
}

void UPartnerSupportComponent::NotifySkillResult(EPartnerSkillType SkillType, EPartnerSkillUseResult Result) const
{
	if (PartnerCharacter)
	{
		PartnerCharacter->ClientNotifySkillUseResult(SkillType, Result);
	}
}

int32 UPartnerSupportComponent::ResolveScanStencilValue(AActor* Actor) const
{
	if (!Actor)
	{
		return 0;
	}

	int32 StencilValue = DefaultScanStencilValue;
	EScanRevealCondition RevealCondition = EScanRevealCondition::Always;

	// Scannable이 우선이라 Hackable 액터도 Scannable을 직접 구현하면 자기 값을 쓴다 (EnemyBase = Enemy).
	if (const IScannableInterface* NativeScannable = Cast<IScannableInterface>(Actor))
	{
		StencilValue = NativeScannable->GetScanStencilValue();
		RevealCondition = NativeScannable->GetScanRevealCondition();
	}
	// Hackable만 구현한 액터는 해킹 대상(Hack.Target 태그)일 때만 Interactable과 같은 색으로 잡는다.
	else if (const IHackableInterface* NativeHackable = Cast<IHackableInterface>(Actor))
	{
		const UHackableComponent* HackableComponent = NativeHackable->GetHackableComponent();
		if (HackableComponent && HackableComponent->IsHackTargetType())
		{
			StencilValue = static_cast<int32>(EScanType::Interaction);
			RevealCondition = EScanRevealCondition::RevealHackable;
		}
	}

	switch (RevealCondition)
	{
	case EScanRevealCondition::RevealHackable:
		return bScanRevealHackable ? StencilValue : DefaultScanStencilValue;
	case EScanRevealCondition::RevealNodeFragments:
		return bScanRevealNodeFragments ? StencilValue : DefaultScanStencilValue;
	default:
		return StencilValue;
	}
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

bool UPartnerSupportComponent::ShouldProcessLocalScanVisual() const
{
	// Partner 조종 여부와 무관하게 화면이 있는 모든 인스턴스에서 연출한다.
	return PartnerCharacter
		&& GetWorld()
		&& GetWorld()->GetNetMode() != NM_DedicatedServer;
}

void UPartnerSupportComponent::MulticastStartScanVisual_Implementation(
	FVector InScanOrigin,
	float InScanRange,
	float InScanDuration,
	bool bInRevealHackable,
	bool bInRevealNodeFragments)
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
	bScanRevealHackable = bInRevealHackable;
	bScanRevealNodeFragments = bInRevealNodeFragments;
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

void UPartnerSupportComponent::MulticastCancelScanForReboot_Implementation()
{
	EndScan_Client();
}
