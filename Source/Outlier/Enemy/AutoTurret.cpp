#include "Enemy/AutoTurret.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameInstance.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"
#include "OutlierArenaSettings.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Team/OutlierTeamIds.h"
#include "TimerManager.h"
#include "Weapon/RangedWeaponBase.h"
#include "GAS/OutlierAbilitySystemComponent.h"

#if WITH_EDITOR
#include "EngineUtils.h"
#include "Misc/DataValidation.h"
#endif

namespace
{
	TAutoConsoleVariable<int32> CVarTurretDiagnostics(
		TEXT("outlier.Enemy.TurretDiagnostics"),
		0,
		TEXT("Logs AutoTurret task failures, rejected damage, and applied impact reactions. 0: off, 1: on"),
		ECVF_Cheat);
}

AAutoTurret::AAutoTurret()
{
	GetOutlierAbilitySystemComponent()->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	bUseCoreWeakPoint = false;
	ApplyClassStatOverrides();

	// 데디 서버에서도 전개 애니메이션의 본 위치를 갱신해야 Physics Asset이 열린 해치 위치를 따라간다.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	TurretHeadPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretHeadPivot"));
	TurretHeadPivot->SetupAttachment(GetRootComponent());
	TurretHeadPivot->SetIsReplicated(true);

	TurretHeadPitchPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretHeadPitchPivot"));
	TurretHeadPitchPivot->SetupAttachment(TurretHeadPivot);
	TurretHeadPitchPivot->SetIsReplicated(true);

	TurretHeadMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TurretHeadMesh"));
	TurretHeadMesh->SetupAttachment(TurretHeadPitchPivot);
	TurretHeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurretHeadMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	TurretHeadMesh->SetIsReplicated(true);

	TurretBlockingBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TurretBlockingBox"));
	TurretBlockingBox->SetupAttachment(GetRootComponent());
	TurretBlockingBox->InitBoxExtent(FVector(50.0f));
	TurretBlockingBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurretBlockingBox->SetCollisionObjectType(ECC_WorldDynamic);
	TurretBlockingBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TurretBlockingBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	// 이동 차단용 Box가 총알을 먼저 가로채지 않도록 피해 Trace는 실제 Body/Head Physics Asset까지 통과시킨다.
	TurretBlockingBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	TurretBlockingBox->SetGenerateOverlapEvents(false);

	// ACharacter의 Root Capsule은 계층 유지를 위해 남기되 터렛 Collision에는 사용하지 않는다.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
		MovementComponent->MaxFlySpeed = 0.0f;
	}

	ConfigureTurretHackPolicy();
}

bool AAutoTurret::IsTurretDiagnosticsEnabled()
{
	return CVarTurretDiagnostics.GetValueOnGameThread() != 0;
}

void AAutoTurret::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureHeadPivotAttachment();
}

void AAutoTurret::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 단일 Actor가 해치 표현과 터렛을 모두 가지므로, EnemyBase BeginPlay보다 먼저
	// 대기 상태를 고정해야 일반 배치 Enemy 등록과 StateTree 시작이 발생하지 않는다.
	PrepareForRoomWaveActivation();
}

void AAutoTurret::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UOutlierSaveSubSystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr;
	if (!SaveSubsystem
		|| !SaveSubsystem->RegisterPersistentTurretId(PersistentTurretId, this))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomCombat] Wave turret Stable ID registration failed. Turret=%s Id=%s"),
			*GetNameSafe(this), *PersistentTurretId.ToString());
		return;
	}
	bPersistentTurretIdRegistered = true;

	URoomCombatSubsystem* CombatSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
		: nullptr;
	const FGameplayTag RoomTag = GetDefaultRoomTag();
	if (CombatSubsystem
		&& CombatSubsystem->RegisterWaveTurret(
			this, RoomTag, CombatPhaseIndex, WaveIndex))
	{
		bWaveTurretRegistered = true;
		return;
	}

	// 두 등록은 하나의 배치 설정 계약이다. Room 등록이 실패하면 Stable ID 슬롯도 되돌린다.
	SaveSubsystem->UnregisterPersistentTurretId(PersistentTurretId, this);
	bPersistentTurretIdRegistered = false;
}

#if WITH_DEV_AUTOMATION_TESTS
void AAutoTurret::ConfigureWaveRegistrationForTesting(
	FGameplayTag InRoomTag,
	int32 InCombatPhaseIndex,
	int32 InWaveIndex,
	FName InPersistentTurretId)
{
	if (URoomTagComponent* RoomTagComp = GetRoomTagComp())
	{
		RoomTagComp->AssignDefaultRoomTag(InRoomTag);
	}
	CombatPhaseIndex = InCombatPhaseIndex;
	WaveIndex = InWaveIndex;
	PersistentTurretId = InPersistentTurretId;
}
#endif

void AAutoTurret::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAutoTurret, TurretLifecycleState);
	DOREPLIFETIME(AAutoTurret, bHackedToPlayerTeam);
}

float AAutoTurret::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	const UPrimitiveComponent* HitComponent = Request.HitResult.GetComponent();
	const FName HitBoneName = Request.HitResult.BoneName;
	const FString DamageTagString = Request.DamageTag.ToString();

	if (!IsCombatActive())
	{
		if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[TurretDiag][Damage] Rejected Reason=%s Turret=%s Authority=%s Damage=%.2f HP=%.2f Component=%s Bone=%s Tag=%s Causer=%s"),
				IsWaitingForRoomWaveActivation() ? TEXT("WaitingForRoomWave") : TEXT("NotActive"),
				*GetNameSafe(this),
				HasAuthority() ? TEXT("true") : TEXT("false"),
				Request.DamageAmount,
				GetCurrentHealth(),
				*GetNameSafe(HitComponent),
				*HitBoneName.ToString(),
				*DamageTagString,
				*GetNameSafe(Request.DamageCauser));
		}
		return 0.0f;
	}

	if (Request.DamageTag.IsValid())
	{
		if (Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon())
			&& HitComponent == GetMesh()
			&& IsNoDamageBone(HitBoneName))
		{
			if (IsTurretDiagnosticsEnabled())
			{
				UE_LOG(
					LogOutlier,
					Warning,
					TEXT("[TurretDiag][Damage] Rejected Reason=NoDamageBone Turret=%s Authority=%s Damage=%.2f HP=%.2f Component=%s Bone=%s Tag=%s Causer=%s"),
					*GetNameSafe(this),
					HasAuthority() ? TEXT("true") : TEXT("false"),
					Request.DamageAmount,
					GetCurrentHealth(),
					*GetNameSafe(HitComponent),
					*HitBoneName.ToString(),
					*DamageTagString,
					*GetNameSafe(Request.DamageCauser));
			}
			return 0.0f;
		}
	}

	return Super::ReceiveOutlierDamage(Request);
}

FGenericTeamId AAutoTurret::GetGenericTeamId() const
{
	return FGenericTeamId(bHackedToPlayerTeam ? OutlierTeamIds::Player : OutlierTeamIds::Enemy);
}

FVector AAutoTurret::GetPawnViewLocation() const
{
	if (TurretHeadMesh)
	{
		FVector MuzzleLocationSum = FVector::ZeroVector;
		for (const FName SocketName : AimOriginMuzzleSockets)
		{
			MuzzleLocationSum += TurretHeadMesh->GetSocketLocation(SocketName);
		}

		if (!AimOriginMuzzleSockets.IsEmpty())
		{
			// 교대 총구 전체의 중심을 사용해 발사 그룹이 바뀌어도 조준 Pitch가 흔들리지 않게 한다.
			return MuzzleLocationSum / static_cast<float>(AimOriginMuzzleSockets.Num());
		}

		// 총구 그룹을 아직 설정하지 않은 터렛도 낮은 Base Pivot이 아니라 Head 중심에서 조준한다.
		if (TurretHeadMesh->GetSkeletalMeshAsset())
		{
			return TurretHeadMesh->Bounds.Origin;
		}
	}

	return TurretHeadPivot ? TurretHeadPivot->GetComponentLocation() : Super::GetPawnViewLocation();
}

FVector AAutoTurret::GetCombatAimPoint(const AActor* TargetActor) const
{
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		// ACharacter의 ActorLocation은 보통 Capsule 중심이지만, 상대 위치가 조정된 BP도 정확히 처리하도록
		// 실제 Collision Bounds 중심을 사용한다. HalfHeight를 다시 더하면 머리 위를 조준하게 된다.
		if (const UCapsuleComponent* TargetCapsule = TargetCharacter->GetCapsuleComponent())
		{
			return TargetCapsule->Bounds.Origin;
		}
	}

	return Super::GetCombatAimPoint(TargetActor);
}

FRotator AAutoTurret::GetViewRotation() const
{
	if (!TurretHeadPitchPivot)
	{
		return Super::GetViewRotation();
	}

	FRotator LocalAimRotation = CurrentAimOffset + ImpactRotationOffset;
	LocalAimRotation.Roll = 0.0f;
	return (GetActorQuat() * LocalAimRotation.Quaternion()).Rotator();
}

bool AAutoTurret::CanUseEnemyPerception() const
{
	return IsCombatActive()
		&& !IsAIControlSuppressed()
		&& GetCurrentHealth() > 0.0f;
}

bool AAutoTurret::CanUseRoomTargetSharing() const
{
	return IsCombatActive()
		&& !bHackedToPlayerTeam
		&& !IsAIControlSuppressed();
}

USkeletalMeshComponent* AAutoTurret::GetWeaponMuzzleComponent(bool bFirstPerson) const
{
	(void)bFirstPerson;
	return TurretHeadMesh;
}

FName AAutoTurret::GetWeaponMuzzleSocketName(bool bFirstPerson) const
{
	(void)bFirstPerson;
	return MuzzleGroupPrefixes.IsValidIndex(CurrentMuzzleGroupIndex)
		? MuzzleGroupPrefixes[CurrentMuzzleGroupIndex]
		: NAME_None;
}

void AAutoTurret::GetWeaponMuzzleSocketNames(
	bool bFirstPerson, TArray<FName>& OutSocketNames) const
{
	USkeletalMeshComponent* MuzzleMesh = GetWeaponMuzzleComponent(bFirstPerson);
	const FName GroupPrefix = GetWeaponMuzzleSocketName(bFirstPerson);
	if (!MuzzleMesh || GroupPrefix.IsNone())
	{
		return;
	}

	const FString PrefixString = GroupPrefix.ToString();
	for (const FName SocketName : MuzzleMesh->GetAllSocketNames())
	{
		const FString SocketString = SocketName.ToString();
		// L_/R_처럼 방향 접두사가 붙은 총구도 Muzzle_Up 그룹 하나로 함께 수집한다.
		if (SocketName == GroupPrefix
			|| SocketString.StartsWith(PrefixString)
			|| SocketString.EndsWith(PrefixString))
		{
			OutSocketNames.Add(SocketName);
		}
	}

	OutSocketNames.Sort(
		[](const FName& Left, const FName& Right)
		{
			return Left.LexicalLess(Right);
		});
}

bool AAutoTurret::UsesIndependentMuzzleShots() const
{
	return MuzzleGroupPrefixes.Num() > 0;
}

void AAutoTurret::ResetWeaponMuzzleSequence()
{
	CurrentMuzzleGroupIndex = 0;
}

void AAutoTurret::AdvanceWeaponMuzzleSequence()
{
	if (!MuzzleGroupPrefixes.IsEmpty())
	{
		CurrentMuzzleGroupIndex = (CurrentMuzzleGroupIndex + 1) % MuzzleGroupPrefixes.Num();
	}
}

void AAutoTurret::PrepareForStateTreeStart()
{
	// BP에 저장된 부모 기본값이 C++ 생성자 값을 덮어써도 터렛은 빙의 대신 팀 전환만 수행한다.
	ConfigureTurretHackPolicy();
	if (const FAutoTurretBehaviorRow* Behavior =
		TurretBehaviorRow.GetRow<FAutoTurretBehaviorRow>(TEXT("AutoTurretBehaviorRow")))
	{
		RuntimeTurretBehavior = *Behavior;
	}
	ConfigureHeadPivotAttachment();
	ApplyTurretLifecycleState();
}

bool AAutoTurret::ShouldActivateAsPreplacedEnemy() const
{
	return IsCombatActive();
}

void AAutoTurret::ConfigureHeadPivotAttachment()
{
	CacheAimOriginMuzzleSockets();

	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!TurretHeadPivot || !TurretHeadPitchPivot || !TurretHeadMesh || !BodyMesh
		|| HeadPivotSocketName.IsNone()
		|| !BodyMesh->DoesSocketExist(HeadPivotSocketName))
	{
		return;
	}

	TurretHeadPivot->AttachToComponent(
		BodyMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		HeadPivotSocketName);
	// 소켓 회전에는 FBX의 축 차이를 보정해 헤드를 수평으로 눕히는 중립 자세가 포함되어 있다.
	// 이 값은 장착 기준으로만 보존하고 조준 회전은 Actor 축에서 별도로 계산한다.
	HeadMountBasisRotation = GetActorQuat().Inverse() * TurretHeadPivot->GetComponentQuat();
	TurretHeadPivot->SetWorldRotation(GetActorRotation());
	TurretHeadPitchPivot->AttachToComponent(
		TurretHeadPivot,
		FAttachmentTransformRules::KeepRelativeTransform);
	TurretHeadPitchPivot->SetRelativeLocationAndRotation(
		FVector::ZeroVector,
		FRotator::ZeroRotator);

	// 기존 BP에서 조정한 Head Mesh의 상대 Transform은 보존한 채 Pitch 피벗 아래로 옮긴다.
	if (TurretHeadMesh->GetAttachParent() != TurretHeadPitchPivot)
	{
		TurretHeadMesh->AttachToComponent(
			TurretHeadPitchPivot,
			FAttachmentTransformRules::KeepRelativeTransform);
	}

}

void AAutoTurret::CacheAimOriginMuzzleSockets()
{
	AimOriginMuzzleSockets.Reset();
	if (!TurretHeadMesh)
	{
		return;
	}

	for (const FName SocketName : TurretHeadMesh->GetAllSocketNames())
	{
		const FString SocketString = SocketName.ToString();
		for (const FName GroupPrefix : MuzzleGroupPrefixes)
		{
			const FString PrefixString = GroupPrefix.ToString();
			if (SocketName == GroupPrefix
				|| SocketString.StartsWith(PrefixString)
				|| SocketString.EndsWith(PrefixString))
			{
				AimOriginMuzzleSockets.Add(SocketName);
				break;
			}
		}
	}
}

void AAutoTurret::ConfigureTurretHackPolicy()
{
	if (!HackableComponent)
	{
		return;
	}

	HackableComponent->HackTags.RemoveTag(HackGameplayTags::Target::Possessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->SuccessEffectTags.RemoveTag(HackGameplayTags::Effect::Possess());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::ChangeTeam());
}

bool AAutoTurret::BeginRoomWaveDeployment()
{
	// 배치 Wave 터렛은 AnimNotify가 실제 전개 완료를 증명해야 한다.
	// 시간 기반 자동 완료는 Wave 기준값을 연출보다 먼저 확정할 수 있으므로 사용하지 않는다.
	if (!IsWaitingForRoomWaveActivation()
		|| bRoomWaveDeploymentRequested)
	{
		return false;
	}

	bRoomWaveDeploymentRequested = true;
	if (!BeginTurretDeploymentInternal())
	{
		bRoomWaveDeploymentRequested = false;
		return false;
	}
	return true;
}

bool AAutoTurret::BeginTurretDeploymentInternal()
{
	if (!HasAuthority()
		|| TurretLifecycleState == EAutoTurretLifecycleState::DeadPresentation
		|| TurretLifecycleState == EAutoTurretLifecycleState::DeadPersistent)
	{
		return false;
	}
	if (TurretLifecycleState == EAutoTurretLifecycleState::Deploying
		|| TurretLifecycleState == EAutoTurretLifecycleState::Active)
	{
		return true;
	}

	// 현재 Room Wave에 해당하는지는 호출자가 판정하고, 여기서는 승인된 전개의 상태와 연출만 시작한다.
	SetTurretLifecycleState(EAutoTurretLifecycleState::Deploying);
	MulticastBeginTurretDeployment();
	ForceNetUpdate();
	return true;
}

bool AAutoTurret::PrepareForRoomWaveActivation()
{
	// 배치 참조는 Server와 Client의 Level Actor에 모두 존재한다. Client는 BeginPlay 이전
	// 닫힘 표현만 먼저 맞추고, 런타임 상태 변경과 Subsystem 정리는 Server만 수행한다.
	if (!HasAuthority() && HasActorBegunPlay())
	{
		return false;
	}

	SetTurretLifecycleState(EAutoTurretLifecycleState::WaitingForWave);
	bRoomWaveDeploymentRequested = false;
	return true;
}

void AAutoTurret::PlayFireMontage()
{
	if (HasAuthority())
	{
		MulticastPlayFireMontage();
	}
}

void AAutoTurret::StopFireMontage()
{
	if (HasAuthority())
	{
		MulticastStopFireMontage();
	}
}

void AAutoTurret::NotifyDeploySequenceFinished()
{
	if (HasAuthority() && IsDeploying())
	{
		// Room Wave가 소유한 전개는 Subsystem이 현재 Pending 수명인지 확인한 뒤 완료한다.
		if (bRoomWaveDeploymentRequested)
		{
			if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
				: nullptr)
			{
				CombatSubsystem->NotifyWaveTurretDeploymentFinished(this);
			}
			return;
		}

		// 배치 Wave가 소유하지 않은 전개 완료는 수명 계약 위반이므로 활성화하지 않는다.
		if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[TurretDiag][Deploy] CompletionRejected Turret=%s Reason=MissingRoomWaveOwnership"),
				*GetNameSafe(this));
		}
	}
}

void AAutoTurret::CompleteTurretDeployment()
{
	if (!HasAuthority() || !IsDeploying())
	{
		return;
	}
	SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
	OnTurretDeploymentCompleted();
}

bool AAutoTurret::CompleteRoomWaveDeployment()
{
	if (!HasAuthority() || !IsDeploying())
	{
		return false;
	}

	CompleteTurretDeployment();
	bRoomWaveDeploymentRequested = false;
	SpawnDefaultController();
	RefreshPerceptionConfigForCurrentState();
	if (StateTreeComponent)
	{
		StateTreeComponent->StartLogic();
	}
	if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
	{
		AdaptationSubsystem->RegisterEnemy(this);
	}
	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		// StateTree가 준비된 뒤 등록해야 현재 Room의 전투 상태와 공유 타깃 이벤트를 즉시 받을 수 있다.
		RoomSubsystem->RegisterEnemy(this);
	}
	ForceNetUpdate();
	return true;
}

bool AAutoTurret::IsCombatActive() const
{
	return TurretLifecycleState == EAutoTurretLifecycleState::Active;
}

void AAutoTurret::SetTurretLifecycleState(EAutoTurretLifecycleState NewState)
{
	TurretLifecycleState = NewState;
	if (NewState == EAutoTurretLifecycleState::WaitingForWave)
	{
		ApplyWaitingForWaveState();
	}
	else
	{
		ApplyTurretLifecycleState();
	}

	if (HasAuthority() && HasActorBegunPlay())
	{
		ForceNetUpdate();
	}
}

void AAutoTurret::ApplyTurretLifecycleState()
{
	const bool bUsesBodyQueryCollision = IsDeploying() || IsCombatActive();
	SetActorEnableCollision(bUsesBodyQueryCollision);
	SetCanBeDamaged(IsCombatActive());
	ApplyTurretCollisionState();

	if (HackableComponent)
	{
		if (IsCombatActive())
		{
			HackableComponent->HackTags.RemoveTag(OutlierGameplayTags::State::Locked());
		}
		else
		{
			HackableComponent->HackTags.AddTag(OutlierGameplayTags::State::Locked());
		}
	}

	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->SetEnemyPerceptionEnabled(CanUseEnemyPerception());
	}
}

void AAutoTurret::ApplyWaitingForWaveState()
{
	if (!IsWaitingForRoomWaveActivation())
	{
		ApplyTurretLifecycleState();
		return;
	}

	bInCombat = false;
	CombatState = EEnemyCombatState::NonCombat;
	bPlayerCurrentlyVisible = false;
	bHasSharedTargetContact = false;
	StopMontageOnMesh(GetMesh(), DeployMontage);
	StopMontageOnMesh(TurretHeadMesh, FireMontage);

	// 초기화 순서상 전투 참여 흔적이 먼저 생겼더라도 대기 진입 경로에서 모두 되돌린다.
	if (HasActorBegunPlay())
	{
		StopCurrentAttack();
		RemoveRoomTargetObserver();
		ClearSharedTargetContact();
		ReleaseSearchRingSlot();
		if (StateTreeComponent)
		{
			StateTreeComponent->StopLogic(TEXT("Turret waiting for Room Wave activation"));
		}

		if (HasAuthority())
		{
			if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
			{
				AdaptationSubsystem->UnregisterEnemy(this);
			}
			if (URoomCombatSubsystem* CombatSubsystem =
				GetWorld()->GetSubsystem<URoomCombatSubsystem>())
			{
				CombatSubsystem->UnregisterEnemy(this);
			}
			if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
			{
				RoomSubsystem->UnregisterEnemy(this);
			}
		}
	}

	ApplyTurretLifecycleState();
}

void AAutoTurret::ApplyTurretCollisionState()
{
	const bool bUsesBodyQueryCollision = IsDeploying() || IsCombatActive();
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (BodyMesh)
	{
		BodyMesh->SetCollisionEnabled(bUsesBodyQueryCollision
			? ECollisionEnabled::QueryOnly
			: ECollisionEnabled::NoCollision);
		BodyMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);

		const ECollisionEnabled::Type HiddenBodyCollision = bUsesBodyQueryCollision
			? ECollisionEnabled::QueryOnly
			: ECollisionEnabled::NoCollision;
		for (const FName BoneRoot : HiddenCollisionBoneRoots)
		{
			if (BoneRoot.IsNone())
			{
				continue;
			}

			BodyMesh->ForEachBodyBelow(
				BoneRoot,
				true,
				false,
				[HiddenBodyCollision](FBodyInstance* BodyInstance)
				{
					if (BodyInstance)
					{
						BodyInstance->SetCollisionEnabled(HiddenBodyCollision);
					}
				});
		}
	}

	// BP에서 TurretHeadPivot 아래에 추가한 Head Skeletal Mesh도 같은 단계로 전환한다.
	TArray<USceneComponent*> HeadChildren;
	if (TurretHeadPivot)
	{
		TurretHeadPivot->GetChildrenComponents(true, HeadChildren);
	}
	const ECollisionEnabled::Type HeadCollision = bUsesBodyQueryCollision
		? ECollisionEnabled::QueryOnly
		: ECollisionEnabled::NoCollision;
	for (USceneComponent* Child : HeadChildren)
	{
		if (USkeletalMeshComponent* HeadMesh = Cast<USkeletalMeshComponent>(Child))
		{
			HeadMesh->SetCollisionEnabled(HeadCollision);
			HeadMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);

			// Physics Asset에서 개별 Body가 NoCollision로 저장되면 컴포넌트만 QueryOnly로 바꿔도 총알에 맞지 않는다.
			// 터렛 Head는 모든 Physics Body를 피해 판정용 Query Collision으로 통일한다.
			for (FBodyInstance* BodyInstance : HeadMesh->Bodies)
			{
				if (BodyInstance)
				{
					BodyInstance->SetCollisionEnabled(HeadCollision);
				}
			}
		}
	}

	// 상속 Capsule은 고정형 터렛의 형상과 맞지 않으므로 모든 상태에서 Collision을 사용하지 않는다.
	if (UCapsuleComponent* BlockingCapsule = GetCapsuleComponent())
	{
		BlockingCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 전개 애니메이션 중에는 플레이어를 밀지 않고 완료된 뒤에만 Box로 이동을 차단한다.
	if (TurretBlockingBox)
	{
		// Box는 전개 후 Pawn 이동만 막고, Shooter Hitscan은 Body/Head의 Physics Asset이 받는다.
		TurretBlockingBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
		TurretBlockingBox->SetCollisionEnabled(
			IsCombatActive()
				? ECollisionEnabled::QueryAndPhysics
				: ECollisionEnabled::NoCollision);
	}
}

bool AAutoTurret::IsNoDamageBone(FName BoneName) const
{
	if (BoneName.IsNone())
	{
		return false;
	}

	const USkeletalMeshComponent* BodyMesh = GetMesh();
	for (const FName BoneRoot : NoDamageBoneRoots)
	{
		if (BoneName == BoneRoot
			|| (BodyMesh && !BoneRoot.IsNone() && BodyMesh->BoneIsChildOf(BoneName, BoneRoot)))
		{
			return true;
		}
	}
	return false;
}

void AAutoTurret::OnRep_TurretLifecycleState()
{
	if (IsWaitingForRoomWaveActivation())
	{
		ApplyWaitingForWaveState();
	}
	else
	{
		ApplyTurretLifecycleState();
	}

	if (TurretLifecycleState == EAutoTurretLifecycleState::Active)
	{
		OnTurretDeploymentCompleted();
	}
}

void AAutoTurret::MulticastBeginTurretDeployment_Implementation()
{
	PlayMontageOnMesh(GetMesh(), DeployMontage);
	OnTurretDeploymentStarted();
}

void AAutoTurret::MulticastPlayFireMontage_Implementation()
{
	PlayMontageOnMesh(TurretHeadMesh, FireMontage);
}

void AAutoTurret::MulticastStopFireMontage_Implementation()
{
	StopMontageOnMesh(TurretHeadMesh, FireMontage);
}

void AAutoTurret::MulticastPlayDeathMontage_Implementation()
{
	StopMontageOnMesh(TurretHeadMesh);
	PlayMontageOnMesh(TurretHeadMesh, DeathMontage);
}

void AAutoTurret::PlayMontageOnMesh(USkeletalMeshComponent* TargetMesh, UAnimMontage* Montage)
{
	if (!TargetMesh || !Montage)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = TargetMesh->GetAnimInstance())
	{
		AnimInstance->Montage_Play(Montage);
	}
}

void AAutoTurret::StopMontageOnMesh(USkeletalMeshComponent* TargetMesh, UAnimMontage* Montage)
{
	if (UAnimInstance* AnimInstance = TargetMesh ? TargetMesh->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(0.0f, Montage);
	}
}

bool AAutoTurret::UpdateTurretAimAtActor(AActor* TargetActor, float DeltaTime, bool bAttackRotation)
{
	return IsValid(TargetActor)
		&& UpdateTurretAimAtLocation(GetCombatAimPoint(TargetActor), DeltaTime, bAttackRotation);
}

bool AAutoTurret::UpdateTurretAimAtLocation(
	const FVector& TargetLocation, float DeltaTime, bool bAttackRotation, bool bUseSearchPitch)
{
	if (!HasAuthority() || !IsCombatActive() || !TurretHeadPivot)
	{
		return false;
	}

	const FVector Direction = TargetLocation - GetPawnViewLocation();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	CurrentAimLocation = TargetLocation;
	const FRotator DesiredWorldRotation = Direction.Rotation();
	// 소켓은 위치만 따라가고 회전은 터렛 Actor의 로컬 X Forward/Z Up 축을 기준으로 계산한다.
	const FTransform AimParentTransform = GetActorTransform();
	FRotator DesiredOffset = FRotator::ZeroRotator;
	if (bUseSearchPitch)
	{
		// 탐색 좌표의 높이는 버리고 소켓 로컬 평면의 X/Y로 Yaw를 직접 계산한다.
		// 월드 회전을 Rotator로 변환한 뒤 Pitch만 덮으면 뒤쪽 목표에서 Euler 축이 뒤집힐 수 있다.
		const FVector LocalDirection = AimParentTransform.InverseTransformVectorNoScale(Direction);
		if (FVector2D(LocalDirection.X, LocalDirection.Y).IsNearlyZero())
		{
			return false;
		}

		const float DesiredRelativeYaw = FMath::RadiansToDegrees(
			FMath::Atan2(LocalDirection.Y, LocalDirection.X));
		DesiredOffset.Yaw = FMath::UnwindDegrees(DesiredRelativeYaw);
		const float SearchMinPitch = FMath::Min(RuntimeTurretBehavior.SearchMinPitchDegrees,
			RuntimeTurretBehavior.SearchMaxPitchDegrees);
		const float SearchMaxPitch = FMath::Max(RuntimeTurretBehavior.SearchMinPitchDegrees,
			RuntimeTurretBehavior.SearchMaxPitchDegrees);
		DesiredOffset.Pitch = FMath::Clamp(RuntimeTurretBehavior.SearchNeutralPitchDegrees,
			SearchMinPitch, SearchMaxPitch);
	}
	else
	{
		const FRotator DesiredRelativeRotation =
			AimParentTransform.InverseTransformRotation(DesiredWorldRotation.Quaternion()).Rotator();
		DesiredOffset = DesiredRelativeRotation.GetNormalized();
		DesiredOffset.Pitch = FMath::Clamp(DesiredOffset.Pitch,
			-RuntimeTurretBehavior.MaxPitchDegrees, RuntimeTurretBehavior.MaxPitchDegrees);
	}
	const float ConfiguredMaxYawDegrees = bAttackRotation
		? RuntimeTurretBehavior.AttackMaxYawDegrees
		: RuntimeTurretBehavior.MaxYawDegrees;
	const float MaxYawDegrees = FMath::Clamp(ConfiguredMaxYawDegrees, 0.0f, 180.0f);
	DesiredOffset.Yaw = FMath::Clamp(DesiredOffset.Yaw, -MaxYawDegrees, MaxYawDegrees);
	DesiredOffset.Roll = 0.0f;

	const float RotationSpeed = bAttackRotation
		? RuntimeTurretBehavior.AttackRotationSpeedDegrees
		: RuntimeTurretBehavior.DefaultRotationSpeedDegrees;
	CurrentAimOffset = FMath::RInterpConstantTo(CurrentAimOffset, DesiredOffset,
		FMath::Max(DeltaTime, 0.0f), FMath::Max(RotationSpeed, 0.0f));

	ApplyHeadRotation();

	return FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentAimOffset.Yaw, DesiredOffset.Yaw))
		<= RuntimeTurretBehavior.AimToleranceDegrees
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentAimOffset.Pitch, DesiredOffset.Pitch))
		<= RuntimeTurretBehavior.AimToleranceDegrees;
}

bool AAutoTurret::UpdateAttackLocation(const FVector& TargetLocation)
{
	if (!HasAuthority() || !IsCombatActive() || IsAIControlSuppressed()
		|| !IsValid(CurrentWeapon) || !TurretHeadPivot)
	{
		return false;
	}

	CurrentAimLocation = TargetLocation;
	// 공격 요청이 들어와도 목표 좌표로 즉시 회전하지 않고, Task가 계산한 실제 머리 방향으로 발사한다.
	if (AController* ActiveController = GetController())
	{
		ActiveController->SetControlRotation(GetViewRotation());
		return true;
	}
	return false;
}

void AAutoTurret::ApplyExplosionReaction(const FVector& ExplosionOrigin, float EnemyImpulseScale,
	float TurretReactionScale, float EffectRatio)
{
	(void)EnemyImpulseScale;
	if (!HasAuthority() || !IsCombatActive()
		|| EffectRatio <= 0.0f || GetCurrentHealth() <= 0.0f)
	{
		return;
	}

	const FVector Direction = (GetActorLocation() - ExplosionOrigin).GetSafeNormal();
	const float ReactionAlpha = FMath::Clamp(FMath::Max(TurretReactionScale, 0.0f) * EffectRatio, 0.0f, 1.0f);
	const float Side = FVector::DotProduct(Direction, GetActorRightVector());
	const float Forward = FVector::DotProduct(Direction, GetActorForwardVector());
	const float Vertical = FVector::DotProduct(Direction, GetActorUpVector());
	// 정면과 후면의 폭발도 눈에 보이는 상하 반동이 되도록 변환한다.
	// 높이 차이가 더 크면 수직 충격을 우선하여 서로 상쇄되는 현상을 막는다.
	const float PitchDirection = FMath::Abs(Vertical) > FMath::Abs(Forward)
		? Vertical
		: -Forward;
	const FRotator PreviousImpactOffset = ImpactRotationOffset;
	ImpactRotationOffset.Yaw = FMath::Clamp(
		ImpactRotationOffset.Yaw + Side * RuntimeTurretBehavior.MaxImpactYawDegrees * ReactionAlpha,
		-RuntimeTurretBehavior.MaxImpactYawDegrees, RuntimeTurretBehavior.MaxImpactYawDegrees);
	ImpactRotationOffset.Pitch = FMath::Clamp(
		ImpactRotationOffset.Pitch + PitchDirection * RuntimeTurretBehavior.MaxImpactPitchDegrees * ReactionAlpha,
		-RuntimeTurretBehavior.MaxImpactPitchDegrees, RuntimeTurretBehavior.MaxImpactPitchDegrees);
	const FRotator AppliedImpactDelta = (ImpactRotationOffset - PreviousImpactOffset).GetNormalized();
	if (!AppliedImpactDelta.IsNearlyZero(0.01f) && IsTurretDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[TurretDiag][Impact] ReactionApplied Turret=%s Strength=%.3f Delta=%s Offset=%s Mode=%s"),
			*GetNameSafe(this),
			ReactionAlpha,
			*AppliedImpactDelta.ToString(),
			*ImpactRotationOffset.ToString(),
			ImpactReactionMode == EAutoTurretImpactReactionMode::StateTreeInterrupt
				? TEXT("StateTreeInterrupt") : TEXT("ConcurrentRecovery"));
	}

	ApplyHeadRotation();
	if (ImpactReactionMode == EAutoTurretImpactReactionMode::StateTreeInterrupt)
	{
		StopImpactRecovery();
		SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Status.ImpactStarted")));
	}
	else
	{
		StartImpactRecovery();
	}
	MulticastExplosionReaction(Direction, ReactionAlpha);
}

void AAutoTurret::StartImpactRecovery()
{
	if (!HasAuthority())
	{
		return;
	}

	// 연속 폭발을 받으면 마지막 충격 방향에서 유지 시간을 다시 시작한다.
	ImpactRecoveryHoldRemaining = FMath::Max(RuntimeTurretBehavior.ImpactHoldDuration, 0.0f);
	LastImpactRecoveryUpdateTimeSeconds = GetWorld()->GetTimeSeconds();
	if (GetWorldTimerManager().IsTimerActive(ImpactRecoveryTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		ImpactRecoveryTimerHandle,
		this,
		&AAutoTurret::TickImpactRecovery,
		1.0f / 60.0f,
		true);
}

void AAutoTurret::TickImpactRecovery()
{
	if (!HasAuthority() || GetCurrentHealth() <= 0.0f)
	{
		StopImpactRecovery();
		return;
	}

	const double CurrentTimeSeconds = GetWorld()->GetTimeSeconds();
	const float DeltaTime = static_cast<float>(FMath::Max(
		CurrentTimeSeconds - LastImpactRecoveryUpdateTimeSeconds,
		0.0));
	LastImpactRecoveryUpdateTimeSeconds = CurrentTimeSeconds;
	if (ImpactRecoveryHoldRemaining > 0.0f)
	{
		// Hold 중에도 ApplyHeadRotation이 ControlRotation을 갱신하므로 실제 탄환이 휘청인 방향으로 발사된다.
		ImpactRecoveryHoldRemaining = FMath::Max(ImpactRecoveryHoldRemaining - DeltaTime, 0.0f);
		ApplyHeadRotation();
		return;
	}
	if (UpdateTurretImpactRecovery(DeltaTime))
	{
		StopImpactRecovery();
	}
}

void AAutoTurret::StopImpactRecovery()
{
	GetWorldTimerManager().ClearTimer(ImpactRecoveryTimerHandle);
	LastImpactRecoveryUpdateTimeSeconds = 0.0;
	ImpactRecoveryHoldRemaining = 0.0f;
}

bool AAutoTurret::UpdateTurretImpactRecovery(float DeltaTime)
{
	const float RecoverySpeed = FMath::Max(RuntimeTurretBehavior.ImpactRecoverySpeedDegrees, 0.0f);
	if (RecoverySpeed <= KINDA_SMALL_NUMBER)
	{
		ResetTurretImpactRecovery();
		return true;
	}

	ImpactRotationOffset.Pitch = FMath::FInterpConstantTo(ImpactRotationOffset.Pitch, 0.0f, DeltaTime, RecoverySpeed);
	ImpactRotationOffset.Yaw = FMath::FInterpConstantTo(ImpactRotationOffset.Yaw, 0.0f, DeltaTime, RecoverySpeed);
	ImpactRotationOffset.Roll = 0.0f;
	if (ImpactRotationOffset.IsNearlyZero(0.01f))
	{
		ResetTurretImpactRecovery();
		return true;
	}
	ApplyHeadRotation();
	return false;
}

void AAutoTurret::ResetTurretImpactRecovery()
{
	ImpactRotationOffset = FRotator::ZeroRotator;
	ApplyHeadRotation();
}

void AAutoTurret::ApplyHeadRotation()
{
	if (!TurretHeadPivot || !TurretHeadPitchPivot)
	{
		return;
	}

	FRotator LocalAimRotation = CurrentAimOffset + ImpactRotationOffset;
	LocalAimRotation.Roll = 0.0f;
	// Actor 회전에 로컬 조준 회전을 Quaternion으로 합성해 Yaw/Pitch가 월드 Roll로 뒤집히지 않게 한다.
	const FQuat YawRotation = FRotator(0.0f, LocalAimRotation.Yaw, 0.0f).Quaternion();
	TurretHeadPivot->SetWorldRotation(GetActorQuat() * YawRotation);
	// Mesh의 X 90도는 FBX 축 보정값으로 유지하고, 상하 조준은 전용 Pivot에 Pitch로만 적용한다.
	const float VisualPitch = LocalAimRotation.Pitch * HeadVisualPitchDirection;
	TurretHeadPitchPivot->SetRelativeRotation(FRotator(VisualPitch, 0.0f, 0.0f));
	if (AController* ActiveController = GetController())
	{
		// 실제 Hitscan 방향도 머리의 반동과 복구를 그대로 따라가도록 ControlRotation을 맞춘다.
		ActiveController->SetControlRotation(GetViewRotation());
	}
}

void AAutoTurret::HandleHackStarted(const FHackQueryContext& Context)
{
	(void)Context;
	if (!HasAuthority() || !IsCombatActive() || bHackedToPlayerTeam)
	{
		return;
	}
	if (CurrentWeapon && GetAttackPhase() == EEnemyAttackPhase::Firing)
	{
		CurrentWeapon->ForcePostBurstCooldown();
	}
	StopCurrentAttack();
	EnterStun();
}

void AAutoTurret::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	if (!HasAuthority() || EffectTag != HackGameplayTags::Effect::ChangeTeam()
		|| Context.Result != EHackResult::Success || !IsCombatActive())
	{
		return;
	}

	bHackedToPlayerTeam = true;
	if (HackableComponent)
	{
		HackableComponent->MarkAsHackedOnce();
	}
	ApplyHackedTeamState();
}

void AAutoTurret::HandleHackCompleted(const FHackResultContext& Context)
{
	(void)Context;
	if (!HasAuthority() || !IsCombatActive())
	{
		return;
	}
	if (GetCombatState() == EEnemyCombatState::Stun)
	{
		RestoreStateAfterStun();
	}
}

void AAutoTurret::ApplyHackedTeamState()
{
	StopCurrentAttack();
	SetPlayerCurrentlyVisible(false);
	RemoveRoomTargetObserver();
	ClearSharedTargetContact();
	ReleaseSearchRingSlot();
	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->RefreshTeamAndPerceptionFromPawn();
	}

	if (HasAuthority())
	{
		// 팀이 바뀐 액터를 기존 우호 대상으로 기억한 AI도 즉시 적대 대상으로 다시 평가한다.
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->RefreshDetectionTarget(this);
		}
	}
	ForceNetUpdate();
}

void AAutoTurret::OnRep_HackedTeam()
{
	ApplyHackedTeamState();
}

void AAutoTurret::HandleEMPStarted(FGameplayTag EffectTag)
{
	if (IsCombatActive())
	{
		Super::HandleEMPStarted(EffectTag);
	}
}

void AAutoTurret::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (bWaveTurretRegistered)
		{
			if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
				? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
				: nullptr)
			{
				CombatSubsystem->UnregisterWaveTurret(this);
			}
			bWaveTurretRegistered = false;
		}

		if (bPersistentTurretIdRegistered)
		{
			UGameInstance* GameInstance = GetGameInstance();
			if (UOutlierSaveSubSystem* SaveSubsystem = GameInstance
				? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
				: nullptr)
			{
				SaveSubsystem->UnregisterPersistentTurretId(PersistentTurretId, this);
			}
			bPersistentTurretIdRegistered = false;
		}
	}

	StopImpactRecovery();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult AAutoTurret::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (IsTemplate())
	{
		return Result == EDataValidationResult::NotValidated
			? EDataValidationResult::Valid
			: Result;
	}

	auto AddValidationError = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};

	const FGameplayTag RoomTag = GetDefaultRoomTag();
	if (!RoomTag.IsValid())
	{
		AddValidationError(TEXT("A wave AutoTurret requires a valid DefaultRoomTag."));
	}
	if (CombatPhaseIndex < 0 || WaveIndex < 0)
	{
		AddValidationError(TEXT("A wave AutoTurret requires non-negative phase and Wave indices."));
	}
	if (PersistentTurretId.IsNone())
	{
		AddValidationError(TEXT("A wave AutoTurret requires a PersistentTurretId."));
	}
	else if (UWorld* World = GetWorld())
	{
		// 에디터 검증에서 현재 로드된 WP Actor끼리의 중복을 먼저 보여준다.
		// 언로드 순서가 겹치는 런타임 중복은 SaveSubsystem의 전역 등록부가 다시 막는다.
		for (TActorIterator<AAutoTurret> It(World); It; ++It)
		{
			const AAutoTurret* OtherTurret = *It;
			if (OtherTurret != this
				&& OtherTurret->PersistentTurretId == PersistentTurretId)
			{
				AddValidationError(FString::Printf(
					TEXT("A wave AutoTurret has a duplicate PersistentTurretId '%s' with %s."),
					*PersistentTurretId.ToString(), *GetNameSafe(OtherTurret)));
				break;
			}
		}
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const URoomCombatDefinition* Definition = Settings
		? Settings->RoomCombatDefinition.LoadSynchronous()
		: nullptr;
	const FRoomCombatRoomDefinition* RoomDefinition = Definition && RoomTag.IsValid()
		? Definition->FindRoomDefinition(RoomTag)
		: nullptr;
	const FRoomCombatWaveDefinition* Wave = RoomDefinition
		? RoomDefinition->FindWave(CombatPhaseIndex, WaveIndex)
		: nullptr;
	if (!Definition)
	{
		AddValidationError(TEXT("A wave AutoTurret requires the integrated RoomCombatDefinition setting."));
	}
	else if (!RoomDefinition)
	{
		AddValidationError(FString::Printf(
			TEXT("A wave AutoTurret has no RoomCombatDefinition entry for %s."),
			*RoomTag.ToString()));
	}
	else if (!Wave)
	{
		AddValidationError(TEXT("A wave AutoTurret points to an invalid combat phase or Wave."));
	}
	else if (!Wave->IsSpawnFromObjects() || !Wave->HasWaveTurrets())
	{
		AddValidationError(TEXT("A wave AutoTurret must target a SpawnFromObjects Wave with ExpectedWaveTurretCount."));
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif

void AAutoTurret::HandleDeath()
{
	StopImpactRecovery();
	SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPresentation);
	bRoomWaveDeploymentRequested = false;
	MulticastPlayDeathMontage();
	Super::HandleDeath();
}

float AAutoTurret::GetDeathDestroyDelay() const
{
	return DeathMontage ? DeathMontage->GetPlayLength() : 0.0f;
}

void AAutoTurret::ApplyClassStatOverrides()
{
	SetDefaultEnemyType(EEnemyType::Turret);
	RuntimeStat.MoveSpeed = 0.0f;
}

void AAutoTurret::ApplyMovementFromRuntimeStat()
{
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
		MovementComponent->MaxFlySpeed = 0.0f;
	}
}
