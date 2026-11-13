#include "Enemy/EnemyBase.h"

#include "Event/OutlierEventSubsystem.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/VECDrone.h"
#include "Camera/CameraComponent.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "PostProcess/OutlierStencilSubsystem.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/EMPGameplayTags.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "EnhancedInputComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyAdaptationShieldComponent.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Enemy/EnemyTargetRules.h"
#include "Enemy/Death/OutlierDeathDebrisActor.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "UObject/StrongObjectPtr.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "InputActionValue.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "NativeGameplayTags.h"
#include "Perception/AIPerceptionSystem.h"
#include "StateTree.h"
#include "Components/StateTreeComponentSchema.h"
#include "Team/OutlierTeamIds.h"
#include "TimerManager.h"
#include "Misc/ScopeExit.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "Drone/FlightPathfinding.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/LaserWeapon.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterPlayerController.h"
#include "Outlier.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FOutlierSightBoneCache& AEnemyBase::ResolveSightBoneRegistration() const
{
	SightBoneCache.Resolve(GetMesh() ? GetMesh()->GetSkeletalMeshAsset() : nullptr, SightBones);
	return SightBoneCache;
}

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EnemyTrackingChanged, "Enemy.Event.Perception.TrackingChanged");
	TAutoConsoleVariable<int32> CVarEnemyImpactReactionDiagnostics(
		TEXT("outlier.Enemy.ImpactReactionDiagnostics"),
		0,
		TEXT("폭발 충격과 반동 회복 사이클 진단 로그를 출력합니다. 0: 끔, 1: 켬"),
		ECVF_Cheat);

	bool IsEnemyImpactReactionDiagnosticsEnabled()
	{
		return CVarEnemyImpactReactionDiagnostics.GetValueOnGameThread() != 0;
	}

	TAutoConsoleVariable<int32> CVarEnemyPossessedStateTreeDiagnostics(
		TEXT("outlier.Enemy.PossessedStateTreeDiagnostics"),
		0,
		TEXT("빙의 공격 StateTree Override와 Schema 호환성 진단 로그를 출력합니다. 0: 끔, 1: 켬"),
		ECVF_Cheat);

	bool IsEnemyPossessedStateTreeDiagnosticsEnabled()
	{
		return CVarEnemyPossessedStateTreeDiagnostics.GetValueOnGameThread() != 0;
	}

	TAutoConsoleVariable<int32> CVarEnemyPossessedAttackDiagnostics(
		TEXT("outlier.Enemy.PossessedAttackDiagnostics"),
		0,
		TEXT("빙의 공격 입력부터 StateTree Task 실행까지의 진단 로그를 출력합니다. 0: 끔, 1: 켬"),
		ECVF_Cheat);

	EEnemyFinalKillCategory ResolveFinalKillCategory(
		EOutlierAdaptationDamageCategory DamageCategory)
	{
		switch (DamageCategory)
		{
		case EOutlierAdaptationDamageCategory::Gun:
			return EEnemyFinalKillCategory::Gun;
		case EOutlierAdaptationDamageCategory::NonGun:
		case EOutlierAdaptationDamageCategory::Pistol:
			return EEnemyFinalKillCategory::NonGun;
		case EOutlierAdaptationDamageCategory::Ignore:
		default:
			return EEnemyFinalKillCategory::Ignore;
		}
	}

	// 공격한 플레이어에게만 명중/처치 확인음을 2D로 들려준다. 빙의 드론 공격도 Partner 컨트롤러로 판정된다.
	void PlayAttackerConfirmAudio(const FOutlierDamageRequest& Request, bool bKilled)
	{
		AController* Attacker = Request.EventInstigator;
		if (!Attacker && Request.DamageCauser)
		{
			Attacker = Request.DamageCauser->GetInstigatorController();
		}

		if (Cast<AShooterPlayerController>(Attacker))
		{
			OutlierAudioFeedback::Owner(Attacker, bKilled
				? TEXT("Audio.Context.Player.01.State.KillConfirm")
				: TEXT("Audio.Context.Player.01.State.HitConfirm"));
		}
		else if (Cast<APartnerPlayerController>(Attacker))
		{
			OutlierAudioFeedback::Owner(Attacker, bKilled
				? TEXT("Audio.Context.Player.02.State.KillConfirm")
				: TEXT("Audio.Context.Player.02.State.HitConfirm"));
		}
	}

	// 위젯 컴포넌트도 UMeshComponent 라서 실제 메시 종류만 등장 디졸브 대상으로 넘긴다.
	void AppendSpawnPresentationMeshes(const AActor* Actor, TArray<UMeshComponent*>& OutMeshes)
	{
		TArray<UMeshComponent*> MeshComponents;
		Actor->GetComponents<UMeshComponent>(MeshComponents);
		for (UMeshComponent* MeshComponent : MeshComponents)
		{
			if (Cast<USkinnedMeshComponent>(MeshComponent) || Cast<UStaticMeshComponent>(MeshComponent))
			{
				OutMeshes.Add(MeshComponent);
			}
		}
	}
}

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	EnemyTraits.AddTag(OutlierGameplayTags::Enemy::Adaptation::Target());
	StunEventTag = FGameplayTag::RequestGameplayTag(TEXT("Event.013_Drone_Stun"));

	OutlierAbilitySystemComponent = CreateDefaultSubobject<UOutlierAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	OutlierAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	VitalAttributeSet = CreateDefaultSubobject<UOutlierVitalAttributeSet>(TEXT("VitalAttributeSet"));

	// 캡슐이 PhysicsBody 채널을 기본(Block)으로 막고 있으면, 무기 트레이스가 캡슐에 먼저 걸려서
	// 캡슐 안쪽에 있는 몸통/코어 등 Physics Asset 본 바디까지 도달하지 못함 (팔다리처럼 캡슐 밖으로
	// 튀어나온 부위만 본 단위로 정상 검출됨) — 그래서 이동/충돌용 콜리전은 그대로 두고 이 채널만 무시
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);

	StateTreeComponent = CreateDefaultSubobject<UEnemyStateTreeComponent>(TEXT("StateTreeComponent"));
	// Enemy StateTree의 행동 Task는 서버 권한과 AIController를 전제로 한다.
	// 컴포넌트 자동 시작을 끄고 BeginPlay 초기화가 끝난 뒤 서버에서만 시작한다.
	StateTreeComponent->SetStartLogicAutomatically(false);
	EnemyCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("EnemyCameraComponent"));
	EnemyCameraComponent->SetupAttachment(GetRootComponent());

	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
	// 목표점은 루트 원점에서 시작하며, 파생 BP에서 메시와 독립적으로 배치한다.
	// 터렛도 이 부착을 유지해 머리의 조준 회전에 목표점이 따라 돌지 않게 한다.
	HackTargetPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HackTargetPoint"));
	HackTargetPoint->SetupAttachment(GetRootComponent());
	HackTargetPoint->bEditableWhenInherited = true;
	HackableComponent->HackTargetPointReference.ComponentProperty = GET_MEMBER_NAME_CHECKED(AEnemyBase, HackTargetPoint);
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::Possessable());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Possess());

	RoomTagComponent = CreateDefaultSubobject<URoomTagComponent>(TEXT("RoomTagComponent"));

	EmpableComponent = CreateDefaultSubobject<UEMPableComponent>(TEXT("EmpableComponent"));
	EmpableComponent->AddEMPTag(EMPGameplayTags::Target::EMPable());

	// 코어 크리티컬 판정용 전용 콜리전 — Physics Asset 바디로 하면 BodyBone 안쪽에 겹친 CoreBone이
	// 같은 컴포넌트 안에서 가려져서 Multi 트레이스로도 검출이 안 됐음 (Body 콜리전을 꺼야만 잡힘,
	// 즉 겹친 바디 중 더 가까운 것에 가려지면 그 뒤는 아예 검사가 안 되는 것으로 확인됨). 그래서
	// SkeletalMeshComponent와 무관한 별도 컴포넌트로 분리해서 독립적으로 트레이스에 잡히게 함
	CoreHitboxComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CoreHitboxComponent"));
	CoreHitboxComponent->SetupAttachment(GetMesh(), CoreBoneName);
	CoreHitboxComponent->InitSphereRadius(20.0f);
	CoreHitboxComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CoreHitboxComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	CoreHitboxComponent->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	CoreHitboxComponent->SetGenerateOverlapEvents(false);

	// 메시의 위치/회전을 따라가는 편집용 기준점이다. 조준에는 위치만 사용하며 충돌과 Tick은 없다.
	AutoAimBodyAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("AutoAimBodyAnchor"));
	AutoAimBodyAnchor->SetupAttachment(GetMesh());
	AutoAimBodyAnchor->PrimaryComponentTick.bCanEverTick = false;

	AdaptationShieldComponent = CreateDefaultSubobject<UEnemyAdaptationShieldComponent>(
		TEXT("AdaptationShieldComponent"));
	AdaptationShieldComponent->SetupAttachment(GetRootComponent());
}

void AEnemyBase::ResetPatrolPoints()
{
	PatrolPointA = GetActorLocation();
	PatrolPointB = GetActorTransform().TransformPositionNoScale(PatrolPointBLocalOffset);
}

void AEnemyBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (HasAuthority())
	{
		// 등록 시점마다 갱신하면 이전 Actor의 늦은 등록이 새 세대로 둔갑한다.
		// Wave 대기 터렛도 여기서 고정하고, 풀 Actor는 별도의 대여 세대를 사용한다.
		if (const UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>())
		{
			PreplacedGameplayGeneration = static_cast<int32>(Arena->GetGameplayGeneration());
		}
	}
	ApplyCoreWeakPointRuntimeState();

	ResetPatrolPoints();

	if (!StateTreeComponent)
	{
		return;
	}

	// BP 기본값이 확정된 뒤, StateTreeComponent가 시작되기 전에 유형별 Linked Asset Override를 등록한다.
	if (BattleStateTreeReference.IsValid())
	{
		StateTreeComponent->AddLinkedStateTreeOverrides(
			FGameplayTag::RequestGameplayTag(TEXT("Enemy.StateTree.Battle")),
			BattleStateTreeReference);
	}

	if (PossessedAttackStateTreeReference.IsValid())
	{
		const FGameplayTag PossessedAttackTag = FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.StateTree.PossessedAttack"));
		StateTreeComponent->AddLinkedStateTreeOverrides(
			PossessedAttackTag,
			PossessedAttackStateTreeReference);

		if (IsEnemyPossessedStateTreeDiagnosticsEnabled())
		{
			const UEnemyStateTreeComponent* EnemyStateTreeComponent =
				Cast<UEnemyStateTreeComponent>(StateTreeComponent);
			const UStateTree* MainTree = EnemyStateTreeComponent
				? EnemyStateTreeComponent->GetConfiguredStateTreeReference().GetStateTree()
				: nullptr;
			const UStateTree* PossessedTree = PossessedAttackStateTreeReference.GetStateTree();
			const UStateTreeSchema* MainSchema = MainTree ? MainTree->GetSchema() : nullptr;
			const UStateTreeSchema* PossessedSchema = PossessedTree ? PossessedTree->GetSchema() : nullptr;
			const UStateTreeComponentSchema* MainComponentSchema = Cast<UStateTreeComponentSchema>(MainSchema);
			const UStateTreeComponentSchema* PossessedComponentSchema = Cast<UStateTreeComponentSchema>(PossessedSchema);
			const UClass* MainContextClass = MainComponentSchema ? MainComponentSchema->GetContextActorClass() : nullptr;
			const UClass* PossessedContextClass = PossessedComponentSchema
				? PossessedComponentSchema->GetContextActorClass()
				: nullptr;
			const bool bSchemaCompatible = MainSchema && PossessedSchema
				&& PossessedSchema->GetClass()->IsChildOf(MainSchema->GetClass());
			const bool bMainContextCompatible = MainContextClass && IsA(MainContextClass);
			const bool bPossessedContextCompatible = PossessedContextClass && IsA(PossessedContextClass);
			const FStateTreeReference* RegisteredOverride = EnemyStateTreeComponent
				? EnemyStateTreeComponent->FindLinkedStateTreeOverride(PossessedAttackTag)
				: nullptr;
			const bool bOverrideRegistered = RegisteredOverride
				&& RegisteredOverride->GetStateTree() == PossessedTree;

			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyPossessedSchema] Enemy=%s MainTree=%s MainSchema=%s MainContext=%s "
					"PossessedTree=%s PossessedSchema=%s PossessedContext=%s "
					"SchemaCompatible=%s MainContextCompatible=%s PossessedContextCompatible=%s "
					"OverrideTag=%s OverrideRegistered=%s"),
				*GetNameSafe(this),
				*GetNameSafe(MainTree),
				*GetNameSafe(MainSchema ? MainSchema->GetClass() : nullptr),
				*GetNameSafe(MainContextClass),
				*GetNameSafe(PossessedTree),
				*GetNameSafe(PossessedSchema ? PossessedSchema->GetClass() : nullptr),
				*GetNameSafe(PossessedContextClass),
				bSchemaCompatible ? TEXT("true") : TEXT("false"),
				bMainContextCompatible ? TEXT("true") : TEXT("false"),
				bPossessedContextCompatible ? TEXT("true") : TEXT("false"),
				*PossessedAttackTag.ToString(),
				bOverrideRegistered ? TEXT("true") : TEXT("false"));

			if (!bSchemaCompatible || !bMainContextCompatible
				|| !bPossessedContextCompatible || !bOverrideRegistered)
			{
				UE_LOG(
					LogOutlier,
					Error,
					TEXT("[EnemyPossessedSchema] Invalid possessed StateTree setup. Enemy=%s "
						"공용 트리와 빙의 트리의 Schema/Context Actor Class 및 Override 등록 결과를 확인하세요."),
					*GetNameSafe(this));
			}
		}
	}
	else if (HackableComponent
		&& HackableComponent->HackTags.HasTagExact(HackGameplayTags::Target::Possessable()))
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttack] Possessed attack StateTree is not configured. Enemy=%s"),
			*GetNameSafe(this));
	}
}

void AEnemyBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEnemyBase, RuntimeStat);
	DOREPLIFETIME(AEnemyBase, bInCombat);
	DOREPLIFETIME(AEnemyBase, LastKnownPlayerLocation);
	DOREPLIFETIME(AEnemyBase, PatternStartPlayerLocation);
	DOREPLIFETIME(AEnemyBase, CombatState);
	DOREPLIFETIME(AEnemyBase, bIsPossessed);
	DOREPLIFETIME(AEnemyBase, bPossessedImpactInputLocked);
	DOREPLIFETIME(AEnemyBase, bPlayerCurrentlyVisible);
	DOREPLIFETIME(AEnemyBase, bHasSharedTargetContact);
	DOREPLIFETIME(AEnemyBase, SharedTargetLocation);
	DOREPLIFETIME(AEnemyBase, AttackPhase);
	DOREPLIFETIME(AEnemyBase, bCombatAimPresentationOwned);
	DOREPLIFETIME(AEnemyBase, bAimTargetIsShooter);
	DOREPLIFETIME(AEnemyBase, bNoiseDirectionFacing);
	DOREPLIFETIME(AEnemyBase, CurrentWeapon);
	DOREPLIFETIME(AEnemyBase, PoolState);
	DOREPLIFETIME(AEnemyBase, AdaptationState);
	DOREPLIFETIME(AEnemyBase, PoolGameplayGeneration);
	DOREPLIFETIME(AEnemyBase, PoolLeaseSerial);
	DOREPLIFETIME(AEnemyBase, PoolCombatPhaseIndex);
	DOREPLIFETIME(AEnemyBase, PoolWaveIndex);
	DOREPLIFETIME(AEnemyBase, HackEffectState);
}

float AEnemyBase::GetHackEffectRemainingTime() const
{
	const UWorld* World = GetWorld();
	if (!HackEffectState.bActive || !World)
	{
		return 0.0f;
	}

	double ServerTime = World->GetTimeSeconds();
	if (!HasAuthority())
	{
		const AGameStateBase* GameState = World->GetGameState();
		if (!GameState)
		{
			// 서버 시계가 아직 없으면 60초에서 대기한다. 로컬 시계로 조기 만료시키지 않는다.
			return GetHackEffectDuration();
		}
		ServerTime = GameState->GetServerWorldTimeSeconds();
	}
	return static_cast<float>(FMath::Clamp(HackEffectState.EndServerTime - ServerTime,
		0.0, static_cast<double>(GetHackEffectDuration())));
}

FEnemyHackEffectTime AEnemyBase::GetHackEffectRemainingMinutesAndSeconds() const
{
	return FEnemyHackEffectTime::FromRemainingTime(GetHackEffectRemainingTime());
}

bool AEnemyBase::BeginHackEffect()
{
	if (!HasAuthority() || !GetWorld() || IsActorBeingDestroyed() || IsDead() || bDeathCleanupPerformed
		|| HackEffectState.bActive || HackEffectState.EndServerTime > 0.0
		|| (IsPoolManaged() && PoolState != EEnemyPoolState::CombatActive))
	{
		return false;
	}

	const FEnemyHackEffectState PreviousState = HackEffectState;
	++HackEffectState.Revision;
	HackEffectState.bActive = true;
	HackEffectState.EndServerTime = GetWorld()->GetTimeSeconds() + GetHackEffectDuration();
	GetWorldTimerManager().SetTimer(HackEffectExpiryTimer,
		FTimerDelegate::CreateUObject(this, &ThisClass::HandleHackEffectExpired,
			HackEffectState.Revision, PoolGameplayGeneration, PoolLeaseSerial),
		GetHackEffectDuration(), false);
	FlushNetDormancy();
	ForceNetUpdate();
	BroadcastHackEffectStateChange(PreviousState);
	return true;
}

bool AEnemyBase::EndHackEffect(EEnemyHackEffectEndReason EndReason)
{
	if (!HasAuthority() || !HackEffectState.bActive)
	{
		return false;
	}

	// 먼저 종료를 확정한다. 아래 알림에서 사망/복귀가 재진입해도 다시 종료하지 않는다.
	GetWorldTimerManager().ClearTimer(HackEffectExpiryTimer);
	const FEnemyHackEffectState PreviousState = HackEffectState;
	HackEffectState.bActive = false;
	HackEffectState.EndReason = EndReason;
	++HackEffectState.Revision;
	FlushNetDormancy();
	ForceNetUpdate();
	MulticastHackEffectEnded(HackEffectState);
	BroadcastHackEffectStateChange(PreviousState);
	return true;
}

bool AEnemyBase::HasHackEffectEndedWithDeath() const
{
	return !HackEffectState.bActive && HackEffectState.EndServerTime > 0.0
		&& (HackEffectState.EndReason == EEnemyHackEffectEndReason::Expired
			|| HackEffectState.EndReason == EEnemyHackEffectEndReason::ManualRelease);
}

bool AEnemyBase::EndHackEffectAndDie(EEnemyHackEffectEndReason EndReason)
{
	if (EndReason != EEnemyHackEffectEndReason::Expired
		&& EndReason != EEnemyHackEffectEndReason::ManualRelease)
	{
		return false;
	}
	// 종료 알림이 복귀/사망을 다시 호출해도 같은 효과를 두 번 처치하지 않는다.
	// 사망은 기존 GAS 경로가 맡아 Room 집계와 터렛 수명주기를 그대로 거친다.
	const uint32 EndedRevision = HackEffectState.Revision + 1;
	if (!EndHackEffect(EndReason))
	{
		return false;
	}
	// 종료 구독자가 Actor를 풀에 반환/재대여한 경우 새 수명은 처치하지 않는다.
	if (IsValid(this) && !IsActorBeingDestroyed() && HackEffectState.Revision == EndedRevision)
	{
		Die();
	}
	return true;
}

void AEnemyBase::HandleHackEffectExpired(uint32 ExpectedRevision, int32 GameplayGeneration, int32 LeaseSerial)
{
	if (!HasAuthority() || !HackEffectState.bActive || HackEffectState.Revision != ExpectedRevision
		|| (IsPoolManaged() && !MatchesPoolLease(GameplayGeneration, LeaseSerial)))
	{
		return;
	}

	// TimerManager의 프레임 경계보다 저장된 서버 만료 시각을 우선한다.
	const float RemainingTime = GetHackEffectRemainingTime();
	if (RemainingTime > 0.0f)
	{
		GetWorldTimerManager().SetTimer(HackEffectExpiryTimer,
			FTimerDelegate::CreateUObject(this, &ThisClass::HandleHackEffectExpired,
				ExpectedRevision, GameplayGeneration, LeaseSerial), RemainingTime, false);
		return;
	}
	EndHackEffectAndDie(EEnemyHackEffectEndReason::Expired);
}

void AEnemyBase::OnRep_HackEffectState(const FEnemyHackEffectState& PreviousState)
{
	// 종료 RPC와 상태 복제는 같은 변경을 전달한다. 이미 처리한 상태로 되돌아가지 않는다.
	if (HackEffectState.Revision <= PreviousState.Revision)
	{
		HackEffectState = PreviousState;
		return;
	}
	BroadcastHackEffectStateChange(PreviousState);
}

void AEnemyBase::MulticastHackEffectEnded_Implementation(const FEnemyHackEffectState& EndedState)
{
	if (HasAuthority() || EndedState.Revision <= HackEffectState.Revision)
	{
		return;
	}
	const FEnemyHackEffectState PreviousState = HackEffectState;
	HackEffectState = EndedState;
	BroadcastHackEffectStateChange(PreviousState);
}

void AEnemyBase::BroadcastHackEffectStateChange(const FEnemyHackEffectState& PreviousState)
{
	if (HackEffectState.bActive)
	{
		OnHackEffectStarted.Broadcast(this, GetHackEffectDuration());
	}
	else if (PreviousState.bActive || HackEffectState.EndServerTime > 0.0)
	{
		OnHackEffectEnded.Broadcast(this, HackEffectState.EndReason);
	}
}

void AEnemyBase::ResetHackEffectForPool()
{
	if (!HasAuthority())
	{
		return;
	}
	EndHackEffect(EEnemyHackEffectEndReason::Cleanup);
	GetWorldTimerManager().ClearTimer(HackEffectExpiryTimer);
	// Revision은 초기화하지 않아 이전 대여의 콜백과 종료 알림을 새 대여에서 거부한다.
	const uint32 NextRevision = HackEffectState.Revision + 1;
	HackEffectState = FEnemyHackEffectState();
	HackEffectState.Revision = NextRevision;
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();
	IOutlierReadabilityTarget::Execute_InitializeReadabilityStencil(this, false);
	RefreshAbilitySystemActorInfo();
	BindGasVitalityObservers();

	if (RoomTagComponent)
	{
		RoomTagComponent->OnCurrentRoomTagChanged.AddUObject(
			this,
			&AEnemyBase::HandleCurrentRoomTagChanged);
	}

	const bool bActivateAsPreplacedEnemy = !IsPoolManaged()
		&& ShouldActivateAsPreplacedEnemy();
	if (HasAuthority())
	{
		if (bActivateAsPreplacedEnemy)
		{
			// 완료 Room 복원은 처치가 아니다. 전투/내성 등록과 AI 시작 전에 배치 적을 정리한다.
			if (URoomCombatSubsystem* CombatSubsystem = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
			{
				CombatSubsystem->RegisterPreplacedEnemy(this);
			}
			if (IsActorBeingDestroyed())
			{
				return;
			}
			if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
			{
				// RoomTag가 없는 배치 적도 아레나 전역의 현재 전투 필드 후보로 관리한다.
				AdaptationSubsystem->RegisterEnemy(this);
			}
			if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
			{
				RoomSubsystem->RegisterEnemy(this);
			}
		}
		if (IsActorBeingDestroyed())
		{
			return;
		}
	}

	InitializeFromEnemyStatRow();
	RefreshEnemyAudioState();
	EquipDefaultWeapon();
	PrepareForStateTreeStart();
	if (IsPoolManaged())
	{
		ApplyPoolState(EEnemyPoolState::Unmanaged);
		return;
	}

	if (HasAuthority() && bActivateAsPreplacedEnemy && StateTreeComponent)
	{
		// Perception이 BeginPlay 전에 상태를 바꿨어도 Global Sync가 현재 값을 읽어
		// 올바른 초기 State를 선택할 수 있도록 모든 Enemy 초기화 뒤에 시작한다.
		StateTreeComponent->StartLogic();
	}
}

#if WITH_EDITOR
EDataValidationResult AEnemyBase::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// Pool Actor는 Arena World 수명을 따르고, 맵에 배치된 Enemy만 Gameplay 레벨과 함께 리로드된다.
	if (!IsPoolManaged() && ShouldActivateAsPreplacedEnemy()
		&& !UOutlierArenaSubsystem::ValidateGameplayActorPlacement(this, Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid : Result;
}
#endif

void AEnemyBase::PrepareForPoolSpawn(UEnemyPoolSubsystem* PoolSubsystem)
{
	if (!HasAuthority() || !PoolSubsystem || HasActorBegunPlay())
	{
		return;
	}

	OwningPoolSubsystem = PoolSubsystem;
	PoolState = EEnemyPoolState::Idle;
	PoolGameplayGeneration = 0;
	PoolLeaseSerial = 0;
	PoolCombatPhaseIndex = INDEX_NONE;
	PoolWaveIndex = INDEX_NONE;
}

bool AEnemyBase::BeginPoolLease(
	const FEnemyPoolLeaseContext& Context,
	const FTransform& SpawnTransform,
	int32 LeaseSerial)
{
	if (!HasAuthority() || PoolState != EEnemyPoolState::Idle
		|| !OwningPoolSubsystem.IsValid() || LeaseSerial == 0)
	{
		return false;
	}

	// 재사용은 BeginPlay를 다시 호출하지 않는다. 복제를 깨운 뒤 이전 대여의 예약 작업부터 끊는다.
	SetNetDormancy(DORM_Awake);
	FlushNetDormancy();
	SetLifeSpan(0.0f);
	DestroyPoolAIController();
	ResetReusableCombatRuntime(TEXT("Enemy pool lease reset"));

	if (RoomTagComponent)
	{
		RoomTagComponent->ClearRuntimeRoomAssignment();
		RoomTagComponent->AssignDefaultRoomTag(Context.RoomTag);
	}
	// 이전 전투 상태를 지운 뒤 새 Room/차수/Wave 수명을 부여한다. 연출 중에는 아직 AI/피해가 비활성이다.
	PoolGameplayGeneration = Context.GameplayGeneration;
	PoolLeaseSerial = LeaseSerial;
	PoolCombatPhaseIndex = Context.CombatPhaseIndex;
	PoolWaveIndex = Context.WaveIndex;
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	// 풀 재사용은 PostInitializeComponents를 다시 호출하지 않는다. 새 스폰 기준으로 순찰도 옮긴다.
	ResetPatrolPoints();
	InitializeFromEnemyStatRow();
	ResetPoolRuntimeState();

	SetPoolState(EEnemyPoolState::SpawnPresentation);
	ForceNetUpdate();

	UE_LOG(LogTemp, Display,
		TEXT("[Reinforcement] Enemy=%s Stage=Lease Generation=%d Lease=%d Room=%s Phase=%d Wave=%d"),
		*GetNameSafe(this),
		PoolGameplayGeneration,
		PoolLeaseSerial,
		*Context.RoomTag.ToString(),
		PoolCombatPhaseIndex,
		PoolWaveIndex);

	OnPoolSpawnPresentationStarted(PoolGameplayGeneration, PoolLeaseSerial);
	return true;
}

void AEnemyBase::ResetReusableCombatRuntime(const TCHAR* StateTreeStopReason)
{
	ResetReturnHomeRecovery();
	ResetCombatTacticalEpisode();
	ClearAlertClue();
	ClearHitClues();
	ClearHeardNoise();
	ResetTargetAttackContinuity();
	ClearTargetTracking();
	PossessionDamageMultiplier = 1.0f;
	ReleasePatrolDestination();
	GetWorldTimerManager().ClearAllTimersForObject(this);
	CancelPossessionProcess();
	EndPossessedImpactInputLock();
	EndImpactReaction();
	ResetPossessedAttackInput();
	StopCurrentAttack();
	RemoveRoomTargetObserver();
	ReleaseSearchDestinations();
	ClearPossessedPlayerState();

	if (StateTreeComponent)
	{
		StateTreeComponent->StopLogic(StateTreeStopReason);
	}
	if (OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->CancelAllAbilities();
		OutlierAbilitySystemComponent->RemoveActiveEffects(FGameplayEffectQuery());
	}
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->ResetForEnemyPoolLease();
	}

	bDeathCleanupPerformed = false;
	bInCombat = false;
	bIsPossessed = false;
	bPlayerCurrentlyVisible = false;
	bHasSharedTargetContact = false;
	bPossessedImpactInputLocked = false;
	bCombatDecisionRefreshPending = false;
	bPossessedAttackHeld = false;
	bPossessedAttackQueued = false;
	CombatState = EEnemyCombatState::NonCombat;
	PreStunCombatState = EEnemyCombatState::NonCombat;
	CombatHeightPreference = EEnemyFlightHeightPreference::Mid;
	AttackPhase = EEnemyAttackPhase::Idle;
	LastKnownPlayerLocation = FVector::ZeroVector;
	PatternStartPlayerLocation = FVector::ZeroVector;
	SharedTargetLocation = FVector::ZeroVector;
	PossessionInstigatorPartner.Reset();
	PossessionPendingEffectHandle.Invalidate();
	RefreshCombatStatePresentation();
}

bool AEnemyBase::MatchesPoolLease(int32 GameplayGeneration, int32 LeaseSerial) const
{
	// Generation은 맵 리로드를, Serial은 같은 맵 안에서 같은 Actor의 반복 대여를 구분한다.
	return IsPoolManaged()
		&& GameplayGeneration == PoolGameplayGeneration
		&& LeaseSerial != 0
		&& LeaseSerial == PoolLeaseSerial;
}

void AEnemyBase::CompletePoolSpawnPresentation(
	int32 GameplayGeneration,
	int32 LeaseSerial)
{
	if (!HasAuthority() || PoolState != EEnemyPoolState::SpawnPresentation
		|| !MatchesPoolLease(GameplayGeneration, LeaseSerial))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Reinforcement] Enemy=%s Stage=PresentationRejected CurrentGeneration=%d CurrentLease=%d CallbackGeneration=%d CallbackLease=%d State=%s Authority=%d"),
			*GetNameSafe(this),
			PoolGameplayGeneration,
			PoolLeaseSerial,
			GameplayGeneration,
			LeaseSerial,
			*UEnum::GetValueAsString(PoolState), HasAuthority());
		return;
	}

	// 연출 완료가 승인된 뒤 방에 등록하고, 전투 중인 방의 표적 정보를 먼저 받는다.
	// StateTree를 시작하기 전에 전투 상태를 준비해 공유만으로도 추적/수색에 합류한다.
	// 이 시점까지 AI와 피해 판정은 비활성이고, 아래 CombatActive 전환에서 활성화한다.
	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->RegisterEnemy(this);
	}

	// 서버가 현재 대여의 연출 완료를 승인한 뒤에만 충돌/피해/StateTree를 활성화한다.
	SetPoolState(EEnemyPoolState::CombatActive);
	UE_LOG(LogTemp, Display,
		TEXT("[Reinforcement] Enemy=%s Stage=PresentationCompleted Generation=%d Lease=%d Room=%s Controller=%s StateTree=%s"),
		*GetNameSafe(this), PoolGameplayGeneration, PoolLeaseSerial,
		*GetDefaultRoomTag().ToString(), *GetNameSafe(GetController()),
		StateTreeComponent
			? *UEnum::GetValueAsString(StateTreeComponent->GetStateTreeRunStatus()) : TEXT("Missing"));
	if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
	{
		AdaptationSubsystem->RegisterEnemy(this);
	}
	ForceNetUpdate();
}

void AEnemyBase::CompletePoolDeathPresentation(
	int32 GameplayGeneration,
	int32 LeaseSerial)
{
	if (!HasAuthority() || PoolState != EEnemyPoolState::DeathPresentation
		|| !MatchesPoolLease(GameplayGeneration, LeaseSerial))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EnemyPool] Ignored death presentation callback Enemy=%s CurrentGeneration=%d CurrentLease=%d CallbackGeneration=%d CallbackLease=%d State=%s"),
			*GetNameSafe(this),
			PoolGameplayGeneration,
			PoolLeaseSerial,
			GameplayGeneration,
			LeaseSerial,
			*UEnum::GetValueAsString(PoolState));
		return;
	}

	ReturnToOwningPool();
}

void AEnemyBase::FinishPoolReturn(int32 GameplayGeneration, int32 LeaseSerial)
{
	if (!HasAuthority() || !MatchesPoolLease(GameplayGeneration, LeaseSerial))
	{
		return;
	}

	ClearAlertClue();
	ClearHitClues();
	ResetHackEffectForPool();

	// 생존 상태의 전투 이탈도 원래 위치에 파편을 남긴다. 사망 경로의 요청은 중복하지 않는다.
	if (PoolState == EEnemyPoolState::CombatActive && !bDeathCleanupPerformed)
	{
		UE_LOG(LogOutlier, Display, TEXT("[DeathDebris] PoolReturn Enemy=%s Collection=%s Generation=%d Lease=%d"),
			*GetName(), *GetNameSafe(DeathDebrisCollection), GameplayGeneration, LeaseSerial);
		if (DeathDebrisCollection)
		{
			FlushNetDormancy();
			MulticastPlayDeathDebris(uint32(GameplayGeneration), LeaseSerial,
				GetMesh() ? GetMesh()->GetComponentTransform() : GetActorTransform(), GetVelocity());
		}
	}

	// 전투 집계와 AI 공유 등록을 먼저 끊고 Idle로 옮긴다. 반환 자체는 처치 이벤트가 아니다.
	if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
	{
		AdaptationSubsystem->UnregisterEnemy(this);
	}
	if (URoomCombatSubsystem* CombatSubsystem = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
	{
		CombatSubsystem->UnregisterEnemy(this);
	}
	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->UnregisterEnemy(this);
	}
	if (RoomTagComponent)
	{
		RoomTagComponent->ClearRuntimeRoomAssignment();
	}

	GetWorldTimerManager().ClearAllTimersForObject(this);
	DestroyPoolAIController();
	StopCurrentAttack();
	PrepareForPoolIdle();
	SetPoolState(EEnemyPoolState::Idle);
	SetActorLocation(FVector(0.0, 0.0, -1000000.0), false, nullptr, ETeleportType::TeleportPhysics);
	PoolGameplayGeneration = 0;
	PoolLeaseSerial = 0;
	PoolCombatPhaseIndex = INDEX_NONE;
	PoolWaveIndex = INDEX_NONE;
	ForceNetUpdate();
	SetNetDormancy(DORM_DormantAll);
}

void AEnemyBase::InvalidatePoolLease()
{
	ClearAlertClue();
	ClearHitClues();
	ResetHackEffectForPool();
	PoolGameplayGeneration = 0;
	PoolLeaseSerial = 0;
	OwningPoolSubsystem.Reset();
}

void AEnemyBase::OnPoolSpawnPresentationStarted_Implementation(
	int32 GameplayGeneration,
	int32 LeaseSerial)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!bPoolPresentationAutoCompleteForTesting)
	{
		return;
	}
#endif
	// 등장 연출 시간과 디졸브는 MaterialPostProcess 쪽이 책임진다. Enemy는 요청하고 끝났다는 통보만 받는다.
	// 볼륨이 없거나 연출 시간이 0이면 통보가 즉시 와서 기존처럼 바로 활성화된다.
	// BP가 Override하면 반드시 같은 토큰으로 완료 API를 호출해야 한다.
	UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>()
		: nullptr;
	if (!PostProcessSubsystem)
	{
		CompletePoolSpawnPresentation(GameplayGeneration, LeaseSerial);
		return;
	}

	PostProcessSubsystem->StartEnemySpawnPresentation(this, FSimpleDelegate::CreateWeakLambda(
		this,
		[this, GameplayGeneration, LeaseSerial]()
		{
			CompletePoolSpawnPresentation(GameplayGeneration, LeaseSerial);
		}));
}

void AEnemyBase::InitializeReadabilityStencil_Implementation(bool bPreserveExistingStencil)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Includes derived actors' body parts, such as AutoTurret's separate head mesh.
	TArray<UMeshComponent*> Meshes;
	GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* MeshComponent : Meshes)
	{
		ApplyReadabilityStencil(MeshComponent, ReadabilityStencilValue, bPreserveExistingStencil);
	}
}

void AEnemyBase::CollectSpawnPresentationMeshes(TArray<UMeshComponent*>& OutMeshes) const
{
	AppendSpawnPresentationMeshes(this, OutMeshes);
	if (IsValid(CurrentWeapon))
	{
		AppendSpawnPresentationMeshes(CurrentWeapon, OutMeshes);
	}
}

void AEnemyBase::OnPoolDeathPresentationStarted_Implementation(
	int32 GameplayGeneration,
	int32 LeaseSerial)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!bPoolPresentationAutoCompleteForTesting)
	{
		return;
	}
#endif
	const float PresentationDuration = FMath::Max(GetDeathDestroyDelay(), 0.0f);
	if (PresentationDuration <= KINDA_SMALL_NUMBER)
	{
		CompletePoolDeathPresentation(GameplayGeneration, LeaseSerial);
		return;
	}

	TWeakObjectPtr<AEnemyBase> WeakThis(this);
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
		this,
		[WeakThis, GameplayGeneration, LeaseSerial, PresentationDuration]()
		{
			AEnemyBase* Enemy = WeakThis.Get();
			if (!Enemy || !Enemy->MatchesPoolLease(GameplayGeneration, LeaseSerial))
			{
				return;
			}

			FTimerHandle DeathPresentationTimer;
			Enemy->GetWorldTimerManager().SetTimer(
				DeathPresentationTimer,
				FTimerDelegate::CreateWeakLambda(
					Enemy,
					[WeakThis, GameplayGeneration, LeaseSerial]()
					{
						if (AEnemyBase* ValidEnemy = WeakThis.Get())
						{
							ValidEnemy->CompletePoolDeathPresentation(
								GameplayGeneration,
								LeaseSerial);
						}
					}),
				PresentationDuration,
				false);
		}));
}

void AEnemyBase::OnRep_PoolState(EEnemyPoolState PreviousState)
{
	ApplyPoolState(PreviousState);
}

void AEnemyBase::ApplyAdaptationState(EEnemyAdaptationState NewState)
{
	if (!HasAuthority() || AdaptationState == NewState)
	{
		return;
	}

	const EEnemyAdaptationState PreviousState = AdaptationState;
	AdaptationState = NewState;
	if (GetNetMode() != NM_DedicatedServer)
	{
		RefreshAdaptationShieldPresentation();
		OnAdaptationStateChanged(PreviousState, AdaptationState);
	}
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Presentation applied Enemy=%s PreviousState=%s CurrentState=%s PoolState=%s Generation=%d Lease=%d"),
		*GetNameSafe(this),
		*UEnum::GetValueAsString(PreviousState),
		*UEnum::GetValueAsString(AdaptationState),
		*UEnum::GetValueAsString(PoolState),
		PoolGameplayGeneration,
		PoolLeaseSerial);
	ForceNetUpdate();
}

FEnemyAdaptationBreakApplicationResult AEnemyBase::ApplyAdaptationBreakEffects(
	float DamageAmount,
	float StunDurationSeconds,
	UObject* EffectSource)
{
	FEnemyAdaptationBreakApplicationResult Result;
	if (!HasAuthority() || IsDead() || !OutlierAbilitySystemComponent)
	{
		return Result;
	}

	if (DamageAmount > 0.0f)
	{
		const float HealthBeforeDamage = GetCurrentHealth();
		FOutlierDamageRequest DamageRequest;
		DamageRequest.DamageAmount = DamageAmount;
		DamageRequest.DamageTag = OutlierGameplayTags::Damage::AdaptationBreak();
		DamageRequest.AdaptationDamageCategory = EOutlierAdaptationDamageCategory::Ignore;
		Result.AppliedDamage = ReceiveOutlierDamage(DamageRequest);
		// 필드 일괄 적용 중에는 GAS의 Health 변경 통지가 이 함수 반환 뒤 확정될 수 있다.
		// 피해 전 체력과 실제 적용량으로 치명을 판정해 기존 사망 정리 경로를 보장한다.
		const bool bLethalDamage = Result.AppliedDamage > 0.0f
			&& Result.AppliedDamage >= HealthBeforeDamage;
		if (bLethalDamage && !IsDead())
		{
			Die();
		}
		Result.bKilled = bLethalDamage || IsDead();
	}

	if (!Result.bKilled && StunDurationSeconds > 0.0f)
	{
		Result.bStunApplied = OutlierAbilitySystemComponent
			->ApplyStunStateToSelf(StunDurationSeconds, EffectSource)
			.IsValid();
	}

	return Result;
}

void AEnemyBase::OnRep_AdaptationState(EEnemyAdaptationState PreviousState)
{
	RefreshAdaptationShieldPresentation();
	OnAdaptationStateChanged(PreviousState, AdaptationState);
	UE_LOG(
		LogOutlier,
		Display,
		TEXT("[EnemyAdaptation] Presentation replicated Enemy=%s PreviousState=%s CurrentState=%s PoolState=%s Generation=%d Lease=%d"),
		*GetNameSafe(this),
		*UEnum::GetValueAsString(PreviousState),
		*UEnum::GetValueAsString(AdaptationState),
		*UEnum::GetValueAsString(PoolState),
		PoolGameplayGeneration,
		PoolLeaseSerial);
}

void AEnemyBase::SetPoolState(EEnemyPoolState NewState)
{
	if (PoolState == NewState)
	{
		return;
	}

	const EEnemyPoolState PreviousState = PoolState;
	PoolState = NewState;
	ApplyPoolState(PreviousState);
}

void AEnemyBase::ApplyPoolState(EEnemyPoolState PreviousState)
{
	if (!IsPoolManaged())
	{
		return;
	}
	if (!HasAuthority() && (PoolState == EEnemyPoolState::SpawnPresentation
		|| PoolState == EEnemyPoolState::CombatActive))
	{
		// 빠른 연출 완료로 SpawnPresentation 복제가 생략되어도 사망 연출 상태를 복원한다.
		ResetPoolPresentationState();
	}

	const bool bIdle = PoolState == EEnemyPoolState::Idle;
	const bool bCombatActive = PoolState == EEnemyPoolState::CombatActive;
	RefreshAdaptationShieldPresentation();
	if (bIdle || PoolState == EEnemyPoolState::DeathPresentation)
	{
		if (auto* Stencils = GetWorld()->GetSubsystem<UOutlierStencilSubsystem>())
		{
			Stencils->ClearActorEffects(this);
		}
	}
	if (bIdle)
	{
		NextFlightPathRequestTime = 0.0;
		NextEnvQueryRequestTime = 0.0;
	}
	SetActorHiddenInGame(bIdle);
	SetActorEnableCollision(bCombatActive);
	SetCanBeDamaged(bCombatActive);

	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->SetActorHiddenInGame(bIdle);
	}

	// 서버는 OnPoolSpawnPresentationStarted에서 완료 통보와 함께 등장 연출을 시작한다.
	// 클라이언트는 표시만 따라 하고, 연출 상태를 벗어나면 어느 쪽이든 원본 머티리얼로 되돌린다.
	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>()
		: nullptr)
	{
		if (PoolState != EEnemyPoolState::SpawnPresentation)
		{
			PostProcessSubsystem->StopEnemySpawnPresentation(this);
		}
		else if (!HasAuthority())
		{
			PostProcessSubsystem->StartEnemySpawnPresentation(this, FSimpleDelegate());
		}
	}

	if (HasAuthority())
	{
		if (bCombatActive)
		{
			SpawnDefaultController();
			RefreshPerceptionConfigForCurrentState();
			if (StateTreeComponent)
			{
				StateTreeComponent->StartLogic();
			}
			UE_LOG(LogTemp, Display,
				TEXT("[Reinforcement] Enemy=%s Stage=AIStartRequested Generation=%d Lease=%d Controller=%s CombatState=%s Shared=%d StateTree=%s"),
				*GetNameSafe(this), PoolGameplayGeneration, PoolLeaseSerial,
				*GetNameSafe(GetController()), *UEnum::GetValueAsString(CombatState),
				bHasSharedTargetContact, StateTreeComponent
					? *UEnum::GetValueAsString(StateTreeComponent->GetStateTreeRunStatus()) : TEXT("Missing"));
#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
			// 시작 직후와 전투 중의 활성 State를 비교해 Running 상태에서 멈춘 지점을 찾는다.
			LastReinforcementStateTreeSnapshot.Reset();
			LastReinforcementStateTreeLogTime = 0.0f;
			GetWorldTimerManager().SetTimer(ReinforcementStateTreeDiagnosticTimerHandle,
				this, &AEnemyBase::LogReinforcementStateTreeSnapshot, 2.0f, true, 0.25f);
#endif
		}
		else
		{
#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
			GetWorldTimerManager().ClearTimer(ReinforcementStateTreeDiagnosticTimerHandle);
#endif
			if (StateTreeComponent)
			{
				StateTreeComponent->StopLogic(TEXT("Enemy pool inactive"));
			}
			DestroyPoolAIController();
		}
	}

	RefreshEnemyAudioState();
	OnPoolStateApplied(PreviousState);
	OnEnemyPoolStateChanged(PreviousState, PoolState);
}

#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
void AEnemyBase::LogReinforcementStateTreeSnapshot()
{
	if (!HasAuthority() || !IsPoolManaged() || PoolState != EEnemyPoolState::CombatActive)
	{
		GetWorldTimerManager().ClearTimer(ReinforcementStateTreeDiagnosticTimerHandle);
		return;
	}

	const AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController());
	const FString ActiveStates = StateTreeComponent
		? FString::JoinBy(StateTreeComponent->GetActiveStateNames(), TEXT(" > "),
			[](const FName& Name) { return Name.ToString(); })
		: TEXT("MissingComponent");
	const FString StateKey = FString::Printf(
		TEXT("Run=%s States=%s Controller=%s Combat=%s Target=%s Visible=%d Shared=%d"),
		StateTreeComponent
			? *UEnum::GetValueAsString(StateTreeComponent->GetStateTreeRunStatus()) : TEXT("Missing"),
		ActiveStates.IsEmpty() ? TEXT("None") : *ActiveStates,
		*GetNameSafe(GetController()), *UEnum::GetValueAsString(CombatState),
		*GetNameSafe(EnemyController ? EnemyController->GetPreferredVisibleTarget() : nullptr),
		bPlayerCurrentlyVisible, bHasSharedTargetContact);
	const float Now = GetWorld()->GetTimeSeconds();
	if (StateKey == LastReinforcementStateTreeSnapshot
		&& Now - LastReinforcementStateTreeLogTime < 10.0f)
	{
		return;
	}
	const FString Snapshot = FString::Printf(
		TEXT("%s LKP=%s"), *StateKey,
		*LastKnownPlayerLocation.ToCompactString());
	UE_LOG(LogTemp, Display,
		TEXT("[Reinforcement] Enemy=%s Stage=StateTreeSnapshot Generation=%d Lease=%d Phase=%d Wave=%d %s"),
		*GetNameSafe(this), PoolGameplayGeneration, PoolLeaseSerial,
		PoolCombatPhaseIndex, PoolWaveIndex, *Snapshot);
	LastReinforcementStateTreeSnapshot = StateKey;
	LastReinforcementStateTreeLogTime = Now;
}
#endif

void AEnemyBase::DestroyPoolAIController()
{
	AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetController());
	if (!EnemyAIController)
	{
		EnemyAIController = Cast<AEnemyAIController>(CachedAIController.Get());
	}
	if (IsValid(EnemyAIController))
	{
		EnemyAIController->SetEnemyPerceptionEnabled(false);
		if (EnemyAIController->GetPawn() == this)
		{
			EnemyAIController->UnPossess();
		}
		EnemyAIController->Destroy();
	}
	CachedAIController.Reset();
}

void AEnemyBase::ReturnToOwningPool()
{
	if (UEnemyPoolSubsystem* PoolSubsystem = OwningPoolSubsystem.Get())
	{
		PoolSubsystem->ReturnEnemy(this, PoolGameplayGeneration, PoolLeaseSerial);
	}
}

void AEnemyBase::ResetPoolRuntimeState()
{
	BoneSight.Shutdown();
	ClearAlertClue();
	ClearHitClues();
	ClearHeardNoise();
	ResetTargetAttackContinuity();
	ClearTargetTracking();
	ResetHackEffectForPool();
	EndSearchEpisode();
	SearchEpisode = FEnemySearchEpisode();
	SharedTargetRevision = 0;
	ConsumedSharedTargetRevision = 0;
	bHasLastKnownPlayerLocation = false;
	LastAcceptedAdaptationDamageCategory = EOutlierAdaptationDamageCategory::Ignore;
	LastAcceptedStealthSession.Reset();
	LastAcceptedDirectShooter.Reset();
	ResetPoolPresentationState();
}

void AEnemyBase::ResetPoolPresentationState()
{
	// 이전 대여의 지연 파편은 재생하되, 다시 대여된 원본 메시를 감추지 않는다.
	++DeathDebrisPresentationSerial;
	DeathDebrisTimerHandle.Invalidate();
	for (const FSourceMeshVisibility& SavedState : SavedSourceMeshVisibility)
	{
		if (USceneComponent* Component = SavedState.Component.Get())
		{
			Component->SetVisibility(SavedState.bVisible, false);
			Component->SetHiddenInGame(SavedState.bHiddenInGame, false);
		}
	}
	SavedSourceMeshVisibility.Reset();
	for (const auto& SavedState : SavedDeathCollision)
	{
		if (UPrimitiveComponent* Component = SavedState.Key.Get())
		{
			Component->SetCollisionEnabled(SavedState.Value);
		}
	}
	SavedDeathCollision.Reset();

	if (USkeletalMeshComponent* CharacterMesh = ShouldResetPoolMontages() ? GetMesh() : nullptr)
	{
		if (UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.0f);
		}
	}
}

void AEnemyBase::PrepareForPoolIdle()
{
}

void AEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetReturnHomeRecovery();
	ResetCombatTacticalEpisode();
	BoneSight.Shutdown();
	ClearAlertClue();
	ClearHitClues();
	ClearHeardNoise();
	ResetTargetAttackContinuity();
	ClearTargetTracking();
	EndHackEffect(EEnemyHackEffectEndReason::Cleanup);
	OutlierAudioFeedback::StopLoops(this);
	ReleasePatrolDestination();
	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>()
		: nullptr)
	{
		PostProcessSubsystem->StopEnemySpawnPresentation(this);
	}
	CancelPossessionProcess();
	ResetPossessedAttackInput();
	EndPossessedImpactInputLock();
	StopCurrentAttack();
	RemoveRoomTargetObserver();
	ReleaseSearchDestinations();

	if (HasAuthority())
	{
		if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
		{
			AdaptationSubsystem->UnregisterEnemy(this);
		}
		if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
			: nullptr)
		{
			CombatSubsystem->UnregisterEnemy(this);
		}
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->UnregisterEnemy(this);
		}
	}

	if (RoomTagComponent)
	{
		RoomTagComponent->OnCurrentRoomTagChanged.RemoveAll(this);
	}

	if (HasAuthority() && IsValid(CurrentWeapon))
	{
		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}

	if (OutlierAbilitySystemComponent)
	{
		UnbindGasVitalityObservers();
		OutlierAbilitySystemComponent->ClearForPawn(this);
	}
	CachedEnemyAdaptationSubsystem.Reset();

	Super::EndPlay(EndPlayReason);
}

void AEnemyBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshAbilitySystemActorInfo();

	if (NewController && !NewController->IsPlayerController())
	{
		CachedAIController = NewController;
		SetEnemyPossessed(false);
	}
	else
	{
		SetEnemyPossessed(true);
	}
}

void AEnemyBase::UnPossessed()
{
	ClearAlertClue();
	ClearHitClues();
	ClearHeardNoise();
	ResetTargetAttackContinuity();
	StopCurrentAttack();
	Super::UnPossessed();
	RefreshAbilitySystemActorInfo();

	SetEnemyPossessed(false);
}

void AEnemyBase::OnRep_Controller()
{
	Super::OnRep_Controller();
	RefreshAbilitySystemActorInfo();
}

UAbilitySystemComponent* AEnemyBase::GetAbilitySystemComponent() const
{
	return OutlierAbilitySystemComponent;
}

float AEnemyBase::GetCurrentHealth() const
{
	return VitalAttributeSet ? VitalAttributeSet->GetHealth() : 0.0f;
}

bool AEnemyBase::IsDead() const
{
	return OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead());
}

bool AEnemyBase::IsPossessionInProgress() const
{
	return OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->HasMatchingGameplayTag(
			OutlierGameplayTags::State::PossessPending());
}

void AEnemyBase::RefreshAbilitySystemActorInfo()
{
	if (OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->InitializeForPawn(this);
	}
}

void AEnemyBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent || !ReleasePossessionAction)
	{
		return;
	}

	EnhancedInputComponent->BindAction(
		ReleasePossessionAction,
		ETriggerEvent::Started,
		this,
		&AEnemyBase::HandleReleasePossessionInput
	);
}

void AEnemyBase::SendEnemyStateTreeEvent(FGameplayTag Tag)
{
	static const FName CombatEnteredEventName(TEXT("Enemy.Event.Combat.Entered"));
	const bool bReinforcementCombatEvent = IsPoolManaged()
		&& Tag.GetTagName() == CombatEnteredEventName;
	const bool bPossessionDiagnosticEvent = Tag.ToString().StartsWith(
		TEXT("Enemy.Event.Possession"));
	const bool bAttackDiagnosticEvent = Tag.ToString().StartsWith(
		TEXT("Enemy.Event.Attack"));
	if (!HasAuthority() || !Tag.IsValid() || !StateTreeComponent
		|| StateTreeComponent->GetStateTreeRunStatus() != EStateTreeRunStatus::Running
		|| (IsPoolManaged() && PoolState != EEnemyPoolState::CombatActive))
	{
		if (bReinforcementCombatEvent)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Reinforcement] Enemy=%s Stage=CombatEventRejected Generation=%d Lease=%d Authority=%d StateTree=%d PoolState=%s"),
				*GetNameSafe(this), PoolGameplayGeneration, PoolLeaseSerial,
				HasAuthority(), StateTreeComponent != nullptr,
				*UEnum::GetValueAsString(PoolState));
		}
		if (IsPossessedAttackDiagnosticsEnabled()
			&& (bAttackDiagnosticEvent || bPossessionDiagnosticEvent))
		{
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[EnemyPossessedAttackDiag] EventRejected Enemy=%s Tag=%s Authority=%s "
					"TagValid=%s StateTreeComponent=%s"),
				*GetNameSafe(this),
				*Tag.ToString(),
				HasAuthority() ? TEXT("true") : TEXT("false"),
				Tag.IsValid() ? TEXT("true") : TEXT("false"),
				*GetNameSafe(StateTreeComponent));
		}
		return;
	}

	StateTreeComponent->SendStateTreeEvent(Tag);
	if (bReinforcementCombatEvent)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[Reinforcement] Enemy=%s Stage=CombatEventSent Generation=%d Lease=%d StateTree=%s Controller=%s"),
			*GetNameSafe(this), PoolGameplayGeneration, PoolLeaseSerial,
			*UEnum::GetValueAsString(StateTreeComponent->GetStateTreeRunStatus()),
			*GetNameSafe(GetController()));
	}

	if (IsPossessedAttackDiagnosticsEnabled()
		&& (bAttackDiagnosticEvent || bPossessionDiagnosticEvent))
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] EventSent Enemy=%s Tag=%s RunStatus=%s "
				"Possessed=%s Held=%s Queued=%s Committed=%s"),
			*GetNameSafe(this),
			*Tag.ToString(),
			*UEnum::GetValueAsString(StateTreeComponent->GetStateTreeRunStatus()),
			bIsPossessed ? TEXT("true") : TEXT("false"),
			bPossessedAttackHeld ? TEXT("true") : TEXT("false"),
			bPossessedAttackQueued ? TEXT("true") : TEXT("false"),
			IsPossessedActionCommitted() ? TEXT("true") : TEXT("false"));

#if WITH_GAMEPLAY_DEBUGGER
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(
				this,
				[this, Tag]()
				{
					if (!StateTreeComponent || !IsPossessedAttackDiagnosticsEnabled())
					{
						return;
					}

					FString ActiveStates;
					for (const FName StateName : StateTreeComponent->GetActiveStateNames())
					{
						if (!ActiveStates.IsEmpty())
						{
							ActiveStates += TEXT(" > ");
						}
						ActiveStates += StateName.ToString();
					}

					UE_LOG(
						LogOutlier,
						Warning,
						TEXT("[EnemyPossessedAttackDiag] ActiveStatesAfterEvent Enemy=%s Tag=%s States=%s"),
						*GetNameSafe(this),
						*Tag.ToString(),
						ActiveStates.IsEmpty() ? TEXT("<None>") : *ActiveStates);
				}));
#endif
	}
}

bool AEnemyBase::IsPossessedAttackDiagnosticsEnabled()
{
	return CVarEnemyPossessedAttackDiagnostics.GetValueOnGameThread() != 0;
}

void AEnemyBase::SendEnemyStateTreeEventNextTick(FGameplayTag Tag)
{
	if (!HasAuthority() || !Tag.IsValid())
	{
		return;
	}


	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(
			this,
			[this, Tag]()
			{
				SendEnemyStateTreeEvent(Tag);
			}));
}

void AEnemyBase::RequestCombatDecisionRefresh()
{
	if (!HasAuthority()
		|| CombatState != EEnemyCombatState::Combat
		|| IsAIControlSuppressed()
		|| bCombatDecisionRefreshPending || HasTrackableTarget())
	{
		return;
	}

	bCombatDecisionRefreshPending = true;
	StopCurrentAttack();

	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(
			this,
			[this]()
			{
				bCombatDecisionRefreshPending = false;

				if (CombatState != EEnemyCombatState::Combat
					|| IsAIControlSuppressed()
					|| bPlayerCurrentlyVisible || HasTrackableTarget())
				{
					return;
				}

				const FGameplayTag DecisionEvent = FGameplayTag::RequestGameplayTag(
					bHasSharedTargetContact
						? TEXT("Enemy.Event.Combat.TargetShared")
						: TEXT("Enemy.Event.Combat.SharedTargetLost"));

				SendEnemyStateTreeEvent(DecisionEvent);
			}));
}

FGenericTeamId AEnemyBase::GetGenericTeamId() const
{
	return FGenericTeamId(bIsPossessed ? OutlierTeamIds::Player : OutlierTeamIds::Enemy);
}

FVector AEnemyBase::GetCombatShotOrigin() const
{
	if (const AController* ActiveController = GetController())
	{
		FVector Location;
		FRotator Rotation;
		ActiveController->GetPlayerViewPoint(Location, Rotation);
		return Location;
	}
	return GetPawnViewLocation();
}

FVector AEnemyBase::GetCombatAimPoint(const AActor* TargetActor) const
{
	// 관측이 끊겨도 마지막 관측점을 유지한다. 숨은 대상의 현재 캡슐 중심으로 되돌리지 않는다.
	// 발사 허가는 별도로 최신 관측과 실제 무기 사선을 모두 확인한다.
	if (!bIsPossessed && !Cast<ALaserWeapon>(CurrentWeapon)
		&& SelectedCombatAimTarget.Get() == TargetActor && CombatAimObservationEpoch != 0
		&& SelectedCombatAimIndex != INDEX_NONE)
	{
		return SelectedCombatAimPoint;
	}
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (UsesCombatNavigationObservation() && (!AI || !bPlayerCurrentlyVisible || !AI->IsTargetDirectlyVisible(TargetActor)))
	{
		// Partner를 포함해 숨은 대상의 캡슐/본 좌표를 조준 갱신 목적으로 조회하지 않는다.
		return GetCombatNavigationTargetLocation(TargetActor);
	}
	if (const AAutoTurret* TargetTurret = Cast<AAutoTurret>(TargetActor))
	{
		FVector ObservedPoint;
		// 터렛은 Actor 원점/캡슐 대신 이 관측자가 실제로 본 표본을 사용한다.
		// 관측이 사라진 뒤에는 숨은 Head의 새 위치를 읽지 않고 마지막 좌표를 유지한다.
		return TargetTurret->TryGetObservedSightPoint(this, ObservedPoint) ? ObservedPoint : GetLastKnownPlayerLocation();
	}
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		// 시점 높이 대신 실제 캡슐 중심을 겨냥해 작은 Partner도 몸체 안쪽으로 조준한다.
		if (const UCapsuleComponent* TargetCapsule = TargetCharacter->GetCapsuleComponent())
		{
			return TargetCapsule->Bounds.Origin;
		}
	}

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetPawnViewLocation();
	}

	return IsValid(TargetActor) ? TargetActor->GetActorLocation() : FVector::ZeroVector;
}

void AEnemyBase::SetCombatAimPresentationOwned(bool bOwned)
{
	if (!HasAuthority() || bCombatAimPresentationOwned == bOwned)
	{
		return;
	}
	bCombatAimPresentationOwned = bOwned;
	ForceNetUpdate();
}

namespace
{
	// 배열 순서는 모델의 명시적 역할 순서다. 좌표가 비슷하다는 이유로 머리 역할을 추측하지 않는다.
	void AppendCombatAimCandidates(const FOutlierSightObservation& Observation,
		TArray<FVector, TInlineAllocator<16>>& Points, TArray<int32, TInlineAllocator<16>>& Ranks,
		TArray<int32, TInlineAllocator<16>>& Indices)
	{
		auto Append = [&](const auto& Source, const auto& BoneIndices, int32 Rank)
		{
			for (int32 Index = 0; Index < Source.Num(); ++Index)
			{
				Points.Add(Source[Index]);
				Ranks.Add(Rank);
				Indices.Add(BoneIndices[Index]);
			}
		};
		Append(Observation.BodyPoints, Observation.BodyBoneIndices, 1);
		Append(Observation.HeadPoints, Observation.HeadBoneIndices, 2);
		Append(Observation.OtherPoints, Observation.OtherBoneIndices, 3);
		// 최초 조준은 인지 위치로 반환한 TP 본에서 시작한다. 나머지는 사선이 막혔을 때의 대안이다.
		const int32 Seen = Indices.IndexOfByKey(Observation.SeenBoneIndex);
		if (Seen > 0)
		{
			Points.Swap(0, Seen); Ranks.Swap(0, Seen); Indices.Swap(0, Seen);
		}
	}
}

void AEnemyBase::RefreshCombatAimPoint()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	const AActor* Target = GetTrackedTarget();
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Target);
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bSameTarget = SelectedCombatAimTarget.Get() == Target;
	if (bSameTarget && Now < NextCombatAimRefreshAt)
	{
		return;
	}
	SelectedCombatAimTarget = Target;
	NextCombatAimRefreshAt = Now + 0.1;
	if (!bSameTarget)
	{
		SelectedCombatAimRank = 0;
		SelectedCombatAimIndex = INDEX_NONE;
		CombatAimObservationEpoch = 0;
	}
	// 최신 사용자 범위: 새 노출 조준은 Shooter만 적용한다. 레이저/빙의/전용 공격은 기존 좌표 계약이다.
	FOutlierSightObservation Observation;
	if (!Shooter || bIsPossessed || Cast<ALaserWeapon>(CurrentWeapon) || !IsValid(CurrentWeapon)
		|| IsAIControlSuppressed() || IsDead())
	{
		SelectedCombatAimRank = 0;
		CombatAimObservationEpoch = 0;
		return;
	}
	if (!Shooter->GetObservedAimPoints(this, Observation))
	{
		SelectedCombatAimRank = 0;
		return;
	}
	if (CombatAimObservationEpoch != Observation.Epoch)
	{
		SelectedCombatAimIndex = INDEX_NONE;
		CombatAimObservationEpoch = Observation.Epoch;
	}
	SelectedCombatAimRank = 0;
	TArray<FVector, TInlineAllocator<16>> Points;
	TArray<int32, TInlineAllocator<16>> Ranks, Indices;
	AppendCombatAimCandidates(Observation, Points, Ranks, Indices);
	if (Points.IsEmpty())
	{
		return;
	}
	const int32 Current = Indices.IndexOfByKey(SelectedCombatAimIndex);
	// 같은 부위가 계속 관측되면 우선 유지한다. 중심이 다시 열려도 조준을 그쪽으로 튕기지 않는다.
	if (Current > 0)
	{
		Points.Swap(0, Current); Ranks.Swap(0, Current); Indices.Swap(0, Current);
	}
	// 사격 불가 시에도 실제 관측점을 바라본다. 재선정은 기존 0.1초 주기와 16 Trace 상한을 지킨다.
	SelectedCombatAimPoint = Points[0];
	SelectedCombatAimIndex = Indices[0];
	int32 Checks = 16;
	const AAutoTurret* Turret = Cast<AAutoTurret>(this);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		// 후보 선택과 실제 발사에 같은 근접 예외를 적용하되, 관측 부위와 총구 사선은 유지한다.
		if ((Turret && Turret->UsesNormalTurretAttackPolicy()
			&& !Turret->CanFireWithoutTurretAimAlignment(Points[Index]) && !Turret->IsTurretAimLocationReachable(Points[Index]))
			|| !CurrentWeapon->HasClearTargetAttackPath(Target, Points[Index], Checks))
		{
			continue;
		}
		SelectedCombatAimRank = Ranks[Index];
		SelectedCombatAimIndex = Indices[Index];
		SelectedCombatAimPoint = Points[Index];
		break;
	}
}

bool AEnemyBase::HasCombatAttackPathFrom(const AActor* Target, const FVector& CandidateLocation, int32& RemainingChecks) const
{
	if (!HasAuthority() || !IsValid(CurrentWeapon) || Target != GetTrackedTarget())
	{
		return false;
	}
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (UsesCombatNavigationObservation() && (!AI || !bPlayerCurrentlyVisible || !AI->IsTargetDirectlyVisible(Target)))
	{
		return false;
	}
	TArray<FVector, TInlineAllocator<16>> Points;
	TArray<int32, TInlineAllocator<16>> Ranks, Indices;
	// 후보 위치에는 현재 자리의 성공을 복사하지 않는다. 같은 관측 후보를 그 위치의 발사 원점에서 검사한다.
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Target);
	FOutlierSightObservation Observation;
	if (Shooter && !bIsPossessed && !Cast<ALaserWeapon>(CurrentWeapon))
	{
		if (!Shooter->GetObservedAimPoints(this, Observation))
		{
			// 직접 관측이 만료되면 캡슐 좌표를 대입해 후보를 통과시키지 않는다.
			if (SelectedCombatAimTarget.Get() == Target && CombatAimObservationEpoch != 0)
			{
				return false;
			}
			Points.Add(GetCombatAimPoint(Target));
		}
		else
		{
			AppendCombatAimCandidates(Observation, Points, Ranks, Indices);
		}
	}
	else
	{
		Points.Add(GetCombatAimPoint(Target));
	}
	const FVector Origin = CandidateLocation + GetCombatShotOrigin() - GetActorLocation();
	for (const FVector& Point : Points)
	{
		if (RemainingChecks < 2)
		{
			return false;
		}
		if (CurrentWeapon->HasClearTargetAttackPathFrom(Target, Point, Origin, RemainingChecks))
		{
			return true;
		}
	}
	return false;
}

FGameplayTag AEnemyBase::GetCurrentRoomTag() const
{
	return RoomTagComponent ? RoomTagComponent->GetCurrentRoomTag() : FGameplayTag();
}

FGameplayTag AEnemyBase::GetDefaultRoomTag() const
{
	return RoomTagComponent ? RoomTagComponent->GetDefaultRoomTag() : FGameplayTag();
}

URoomTagComponent* AEnemyBase::GetRoomTagComp() const
{
	return RoomTagComponent;
}

void AEnemyBase::SetEnemyPossessed(bool bNewIsPossessed)
{
	if (HasAuthority() && !bNewIsPossessed)
	{
		PossessionDamageMultiplier = 1.0f;
	}
	if (!HasAuthority() || bIsPossessed == bNewIsPossessed)
	{
		return;
	}

	ResetReturnHomeRecovery();
	ClearAlertClue();
	ClearHitClues();
	InterruptDamagePresentation(0.05f);
	bPossessedAttackHeld = false;
	EndPossessedImpactInputLock();
	bIsPossessed = bNewIsPossessed;
	SetCombatAimPresentationOwned(false);
	// 서버 로컬 변경에는 PostNetReceive가 없으므로 같은 표현 경로를 직접 갱신한다.
	// 해킹 종료 처치가 시작된 경우에는 드론 쪽에서 소등하여 원래 상태 색이 잠깐 켜지는 것을 막는다.
	RefreshCombatStatePresentation();
	if (bIsPossessed)
	{
		ReleasePatrolDestination();
		CancelCommittedAction();
		EndImpactReaction();
	}
	ResetPossessedAttackInput();

	if (bIsPossessed)
	{
		PossessionInstigatorPartner.Reset();
		RemovePossessionPendingState();

		StopCurrentAttack();
		RemoveRoomTargetObserver();
		ClearSharedTargetContact();
		ReleaseSearchDestinations();
	}

	if (!bIsPossessed)
	{
		ClearPossessedPlayerState();
	}

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController()))
	{
		EnemyAIController->SetEnemyPerceptionEnabled(CanUseEnemyPerception());
	}

	RefreshPerceptionTeamRegistration();
	ForceNetUpdate();

	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] PossessionStateChanged Enemy=%s Possessed=%s Controller=%s"),
			*GetNameSafe(this),
			bIsPossessed ? TEXT("true") : TEXT("false"),
			*GetNameSafe(GetController()));
	}

	// 해킹 성공 확인 시점이 아니라 실제 Controller 소유권이 바뀐 뒤 전환해야
	// Global Sync의 bIsPossessed 조건과 Possession.Started 이벤트가 같은 프레임에 일치한다.
	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			bIsPossessed
			? TEXT("Enemy.Event.Possession.Started")
			: TEXT("Enemy.Event.Possession.Ended")));
}

void AEnemyBase::SetPossessionDamageMultiplier(float Multiplier)
{
	if (!HasAuthority() || !bIsPossessed || IsDead())
	{
		return;
	}
	PossessionDamageMultiplier = FMath::IsFinite(Multiplier) ? FMath::Max(0.0f, Multiplier) : 1.0f;
}

void AEnemyBase::GetWeaponDamageModifiers(FWeaponDamageModifiers& OutModifiers) const
{
	OutModifiers = FWeaponDamageModifiers{};
	OutModifiers.DamageMultiplier = PossessionDamageMultiplier;
}

bool AEnemyBase::BeginPossessionProcess(APartnerCharacter* PartnerCharacter)
{
	if (!HasAuthority()
		|| !IsValid(PartnerCharacter)
		|| bIsPossessed
		|| IsPossessionInProgress()
		|| IsPossessedActionCommitted()
		|| !HackableComponent)
	{
		return false;
	}

	CancelCommittedAction();
	EndImpactReaction();
	PossessionInstigatorPartner = PartnerCharacter;
	ResetPossessedAttackInput();
	if (!ApplyPossessionPendingState())
	{
		PossessionInstigatorPartner.Reset();
		return false;
	}

	InterruptDamagePresentation(0.05f);
	StopCurrentAttack();
	RemoveRoomTargetObserver();
	ClearSharedTargetContact();
	ReleaseSearchDestinations();
	ReleasePatrolDestination();
	SetPlayerCurrentlyVisible(false);

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController()))
	{
		EnemyAIController->StopMovement();
		EnemyAIController->SetEnemyPerceptionEnabled(false);
	}

	PartnerCharacter->SetEnemyPossessionProtection(true);
	ConsumeMovementInputVector();
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	ForceNetUpdate();

	// Global Sync가 ASC PossessPending 태그를 먼저 읽은 뒤 Pending 상태를 선택하게 한다.
	SendEnemyStateTreeEventNextTick(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Possession.Pending")));

	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] PossessionPendingStarted Enemy=%s Partner=%s EventDeferred=true"),
			*GetNameSafe(this),
			*GetNameSafe(PartnerCharacter));
	}

	return true;
}

void AEnemyBase::ConfirmPossessionProcess()
{
	if (!HasAuthority() || !IsPossessionInProgress() || !HackableComponent)
	{
		return;
	}

	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] PossessionConfirmed Enemy=%s AwaitingControllerPossession=true"),
			*GetNameSafe(this));
	}
}

void AEnemyBase::CancelPossessionProcess()
{
	if (!HasAuthority())
	{
		return;
	}

	const bool bHadPossessionProcess = IsPossessionInProgress();
	if (!bHadPossessionProcess)
	{
		return;
	}

	RemovePossessionPendingState();

	if (APartnerCharacter* PartnerCharacter = PossessionInstigatorPartner.Get())
	{
		PartnerCharacter->SetEnemyPossessionProtection(false);
	}
	PossessionInstigatorPartner.Reset();

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController()))
	{
		EnemyAIController->SetEnemyPerceptionEnabled(CanUseEnemyPerception());
	}

	ForceNetUpdate();

	if (!bIsPossessed)
	{
		// Global Sync가 ASC PossessPending 제거를 반영한 뒤 Pending 상태를 빠져나가게 한다.
		SendEnemyStateTreeEventNextTick(
			FGameplayTag::RequestGameplayTag(
				TEXT("Enemy.Event.Possession.Cancelled")));
	}
}

void AEnemyBase::RefreshPerceptionTeamRegistration()
{
	if (UAIPerceptionSystem* PerceptionSystem = UAIPerceptionSystem::GetCurrent(GetWorld()))
	{
		PerceptionSystem->UnregisterSource(*this);
		PerceptionSystem->RegisterSource(*this);
	}
}

void AEnemyBase::ClearPossessedPlayerState()
{
	if (!HasAuthority())
	{
		return;
	}

	SetPlayerState(nullptr);
}

void AEnemyBase::InitializeFromEnemyStatRow()
{
	if (!HasAuthority())
	{
		return;
	}

	if (const FEnemyStat* StatRow = EnemyStatRow.GetRow<FEnemyStat>(TEXT("EnemyStatRow")))
	{
		RuntimeStat = *StatRow;
	}
	if (const FEnemyImpactReactionProfileRow* ImpactProfile =
		ImpactReactionProfileRow.GetRow<FEnemyImpactReactionProfileRow>(TEXT("ImpactReactionProfileRow")))
	{
		// 런타임 충격마다 RowHandle을 다시 조회하지 않도록 서버 초기화 시 한 번만 복사한다.
		RuntimeImpactReactionProfile = *ImpactProfile;
	}

	ApplyClassStatOverrides();
	// 배치 인스턴스의 감지 거리만 DataTable 결과 위에 덮어쓴다. -1은 원본 값을 유지한다.
	if (SightRadiusOverride >= 0.0f)
	{
		RuntimeStat.SightRadius = SightRadiusOverride;
	}
	if (LoseSightRadiusOverride >= 0.0f)
	{
		RuntimeStat.LoseSightRadius = LoseSightRadiusOverride;
	}
	if (HearingRangeOverride >= 0.0f)
	{
		RuntimeStat.HearingRange = HearingRangeOverride;
	}
	if (BattleHearingRangeOverride >= 0.0f)
	{
		RuntimeStat.BattleHearingRange = BattleHearingRangeOverride;
	}
	ApplyMovementFromRuntimeStat();
	InitializeGasVitality();

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyAIController->RefreshPerceptionConfigFromPawn();
	}
}

void AEnemyBase::UpdateLastKnownPlayerLocation(const FVector& NewLocation)
{
	if (!HasAuthority())
	{
		return;
	}

	LastKnownPlayerLocation = NewLocation;
	bHasLastKnownPlayerLocation = !NewLocation.ContainsNaN();
}

void AEnemyBase::SetPatternStartPlayerLocation(const FVector& NewLocation)
{
	if (!HasAuthority())
	{
		return;
	}

	PatternStartPlayerLocation = NewLocation;
}

void AEnemyBase::SetPlayerCurrentlyVisible(bool bNewVisible)
{
	if (!HasAuthority() || bPlayerCurrentlyVisible == bNewVisible)
	{
		return;
	}
	bPlayerCurrentlyVisible = bNewVisible;
	if (bNewVisible && DetectAudioContext.IsValid())
	{
		OutlierAudioFeedback::World(this, *DetectAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
	}
	RefreshTargetTracking();
}

AActor* AEnemyBase::GetTrackedTarget() const
{
	if (!HasAuthority() || !CanUseEnemyPerception() || IsDead() || CombatState == EEnemyCombatState::Stun)
	{
		return nullptr;
	}
	const AEnemyAIController* TargetController = TrackingController.Get();
	AActor* Target = TrackedTarget.Get();
	if (!TargetController || GetController() != TargetController || TargetController->GetPawn() != this
		|| !TargetController->IsValidTrackingTarget(Target))
	{
		return nullptr;
	}
	if (bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(Target))
	{
		return Target;
	}
	if (CombatState != EEnemyCombatState::Combat)
	{
		return nullptr;
	}
	if (bTrackingFromRoom || (TrackingRooms.IsValid() && CanUseRoomTargetSharing()
		&& TrackingRooms->IsRoomInCombat(GetDefaultRoomTag())))
	{
		FEnemyRoomTargetObservationSnapshot Observation;
		return TrackingRooms.IsValid() && (!bTrackingFromRoom || TrackingRoomTag == GetDefaultRoomTag())
			&& TrackingRooms->TryGetRoomTargetObservation(const_cast<AEnemyBase*>(this), Target, Observation)
			? Target : nullptr;
	}
	return GetWorld() && GetWorld()->GetTimeSeconds() < TargetTrackingExpiresAt ? Target : nullptr;
}

uint64 AEnemyBase::EnsureCombatTacticalEpisode(AActor* Target, const FVector& Goal, float ChangeDistance)
{
	if (!HasAuthority() || !GetWorld() || !IsA<AVECDrone>() || RuntimeStat.Type != EEnemyType::Gun
		|| IsEnemyPossessed() || IsAIControlSuppressed() || Target != GetTrackedTarget())
	{
		return 0;
	}
	const uint64 PreviousSerial = CombatTacticalEpisode.GetSerial();
	const uint64 Serial = CombatTacticalEpisode.Ensure(Target, GetController(), PoolGameplayGeneration,
		PoolLeaseSerial, Goal, GetActorLocation(), ChangeDistance, RuntimeStat);
	if (Serial != PreviousSerial)
	{
		// 첫 회차 생성은 기존 예약을 교체하지 않는다. 방금 수신한 같은 목표의 양보 요청도 유지한다.
		if (PreviousSerial != 0 || CombatReservationRequest != 0)
		{
			ReleaseCombatReservation();
		}
	}
	if (Serial != 0 && !TacticalObstacleHandle.IsValid())
	{
		TacticalFailureRooms = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
		if (URoomCombatSubsystem* Rooms = TacticalFailureRooms.Get())
		{
			TacticalObstacleHandle = Rooms->OnMovementObstacleOpened.AddUObject(this, &AEnemyBase::HandleCombatTacticalObstacleOpened);
		}
	}
	return Serial;
}

bool AEnemyBase::IsCombatTacticalEpisodeCurrent(uint64 Serial) const
{
	return HasAuthority() && GetWorld() && !IsAIControlSuppressed() && !IsEnemyPossessed()
		&& GetCombatState() == EEnemyCombatState::Combat && HasTrackableTarget()
		&& CombatTacticalEpisode.IsCurrent(Serial, GetWorld()->GetTimeSeconds());
}

bool AEnemyBase::TryBeginCombatTacticalRequest()
{
	if (!HasAuthority() || !GetWorld())
	{
		return false;
	}
	const bool bWasObserving = CombatTacticalEpisode.IsObserving();
	const bool bAllowed = CombatTacticalEpisode.TryBeginRequest(GetWorld()->GetTimeSeconds());
	if (!bWasObserving && CombatTacticalEpisode.IsObserving())
	{
		EnterCombatObservationWait();
	}
	return bAllowed;
}

bool AEnemyBase::CanContinueCombatTacticalRequest()
{
	if (!HasAuthority() || !GetWorld() || CombatTacticalEpisode.GetSerial() == 0)
	{
		return false;
	}
	if (!CombatTacticalEpisode.IsObserving() && CombatTacticalEpisode.HasExpired(GetWorld()->GetTimeSeconds()))
	{
		EnterCombatObservationWait(true);
	}
	return !CombatTacticalEpisode.IsObserving();
}

void AEnemyBase::EnterCombatObservationWait(bool bBudgetShortage)
{
	if (!HasAuthority())
	{
		return;
	}
	CombatTacticalEpisode.EnterObservation(bBudgetShortage);
	ReleaseCombatReservation();
	ClearCombatFlightHandoff();
	// 실제 캡슐 충돌 점유는 그대로 둔다. 미래 목적지 예약만 반환하며 사격/인지 시계는 건드리지 않는다.
	ReleasePatrolDestination();
	ReleaseSearchDestinations();
}

void AEnemyBase::NoteCombatTacticalBudgetShortage()
{
	if (HasAuthority())
	{
		CombatTacticalEpisode.NoteBudgetShortage();
	}
}

bool AEnemyBase::TryResumeCombatTacticalBudget(bool bActualExecutionGranted)
{
	return HasAuthority() && GetWorld()
		&& CombatTacticalEpisode.TryResumeBudget(bActualExecutionGranted, GetWorld()->GetTimeSeconds());
}

void AEnemyBase::CompleteCombatTacticalSelectionSuccess(bool bInvalidatePendingRequests)
{
	if (HasAuthority())
	{
		CombatTacticalEpisode.CompleteSelectionSuccess(bInvalidatePendingRequests);
	}
}

void AEnemyBase::BeginCombatTacticalMovement(bool bSightReposition)
{
	// 검증된 정상 이동은 재탐색 대기가 아니다. 이동 실패로 돌아오면 원래 시계와 횟수를 이어 쓴다.
	if (HasAuthority())
	{
		CombatTacticalEpisode.BeginMovement(bSightReposition);
	}
}

void AEnemyBase::EndCombatTacticalMovement()
{
	if (HasAuthority())
	{
		CombatTacticalEpisode.EndMovement();
	}
}

uint64 AEnemyBase::BeginCombatReservationRequest()
{
	if (!HasAuthority() || !GetWorld())
	{
		return 0;
	}
	if (!TrackingRooms.IsValid())
	{
		TrackingRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	}
	return TrackingRooms.IsValid()
		? TrackingRooms->BeginCombatReservationRequest(this, GetCombatTacticalEpisodeSerial()) : 0;
}

bool AEnemyBase::IsCombatReservationRequestCurrent(uint64 Request) const
{
	return TrackingRooms.IsValid() && TrackingRooms->IsCombatReservationRequestCurrent(this, Request);
}

bool AEnemyBase::AdoptCombatReservation(uint64 Request)
{
	if (!TrackingRooms.IsValid() || !TrackingRooms->IsCombatReservationCurrent(this, Request))
	{
		return false;
	}
	CombatReservationRequest = Request;
	return true;
}

bool AEnemyBase::RefreshCombatReservation(uint64 Request)
{
	return Request != 0 && CombatReservationRequest == Request && TrackingRooms.IsValid()
		&& TrackingRooms->RefreshCombatReservation(this, Request);
}

bool AEnemyBase::IsCombatSegmentReserved(uint64 Request, const FVector& Start, const FVector& End) const
{
	return Request != 0 && CombatReservationRequest == Request && TrackingRooms.IsValid()
		&& TrackingRooms->IsCombatSegmentReserved(this, Request, Start, End);
}

void AEnemyBase::ReleaseCombatReservation(uint64 Request)
{
	if (!HasAuthority())
	{
		return;
	}
	if (TrackingRooms.IsValid())
	{
		TrackingRooms->ReleaseCombatReservation(this, Request);
	}
	if (Request == 0 || CombatReservationRequest == Request)
	{
		if (CombatYieldMovementSerial != 0 && TrackingRooms.IsValid())
		{
			TrackingRooms->FinishCombatYield(this, CombatYieldMovementSerial);
		}
		CombatYieldMovementReservation = 0;
		CombatYieldMovementSerial = 0;
		CombatYieldController.Reset();
		CombatYieldTarget.Reset();
		CombatReservationRequest = 0;
	}
}

bool AEnemyBase::IsReadyForCombatYieldPriority() const
{
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	AActor* Target = GetTrackedTarget();
	if (!HasAuthority() || !GetWorld() || IsDead() || IsAIControlSuppressed() || bIsPossessed
		|| CombatState != EEnemyCombatState::Combat || IsLocationInvestigationActive()
		|| !AI || !Target || !bPlayerCurrentlyVisible || !AI->IsTargetDirectlyVisible(Target)
		|| !IsValid(CurrentWeapon) || Cast<ALaserWeapon>(CurrentWeapon) || IsTargetAttemptActive())
	{
		return false;
	}
	// 예약 후보 비교는 같은 평가에서 여러 번 호출된다. 정상 사격 준비만 제한된 주기로 재검사한다.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextCombatYieldPriorityCheckAt)
	{
		const float Interval = RuntimeStat.CombatYieldCheckInterval;
		NextCombatYieldPriorityCheckAt = Now + (FMath::IsFinite(Interval) ? FMath::Max(0.01f, Interval) : 0.5f);
		bCombatYieldPriorityReady = CurrentWeapon->CanAttack() && CanAttackTrackedTarget(Target)
			&& CurrentWeapon->HasClearTargetAttackPath(Target, GetCombatAimPoint(Target));
	}
	return bCombatYieldPriorityReady;
}

bool AEnemyBase::GetCombatYieldContext(FVector& Start, FVector& End, uint64& Serial) const
{
	Start = End = FVector::ZeroVector;
	Serial = 0;
	if (!HasAuthority() || !UsesCombatNavigationObservation() || IsDead() || IsAIControlSuppressed()
		|| CombatState != EEnemyCombatState::Combat || IsLocationInvestigationActive() || !GetTrackedTarget()
		|| !TrackingRooms.IsValid())
	{
		return false;
	}
	// 확정한 양보 경로는 통신용 요청의 1초 수명과 분리한다. 실제 이동의 예약/중단 상한은 그대로 지킨다.
	if (CombatYieldMovementSerial != 0 && CombatYieldMovementReservation == CombatReservationRequest
		&& CombatYieldController.Get() == GetController() && CombatYieldTarget.Get() == GetTrackedTarget()
		&& CombatYieldGeneration == PoolGameplayGeneration && CombatYieldLease == PoolLeaseSerial
		&& IsCombatReservationRequestCurrent(CombatYieldMovementReservation)
		&& TrackingRooms->IsCombatReservationCurrent(this, CombatYieldMovementReservation))
	{
		Start = CombatYieldProtectedStart;
		End = CombatYieldProtectedEnd;
		Serial = CombatYieldMovementSerial;
		return true;
	}
	FEnemyCombatYieldContext Context;
	// 이미 승인된 일반 이동은 끝까지 보장한다. 새 양보 요청은 다음 안전한 선택 경계에서 소비한다.
	if (CombatReservationRequest != 0 && TrackingRooms->IsCombatReservationCurrent(this, CombatReservationRequest))
	{
		return false;
	}
	if (!TrackingRooms->GetCombatYieldContext(this, Context))
	{
		return false;
	}
	Start = Context.Start;
	End = Context.End;
	Serial = Context.Serial;
	return true;
}

void AEnemyBase::BeginCombatYieldMovement(uint64 Reservation, uint64 YieldSerial)
{
	FEnemyCombatYieldContext Context;
	// 새 양보 경로의 예약 직후이므로 일반 이동 보호용 getter를 거치지 않고 같은 요청을 인계한다.
	if (!HasAuthority() || !TrackingRooms.IsValid() || !TrackingRooms->GetCombatYieldContext(this, Context)
		|| Context.Serial != YieldSerial
		|| Reservation == 0 || Reservation != CombatReservationRequest || !IsCombatReservationRequestCurrent(Reservation))
	{
		return;
	}
	CombatYieldMovementReservation = Reservation;
	CombatYieldMovementSerial = YieldSerial;
	CombatYieldProtectedStart = Context.Start;
	CombatYieldProtectedEnd = Context.End;
	CombatYieldController = GetController();
	CombatYieldTarget = GetTrackedTarget();
	CombatYieldGeneration = PoolGameplayGeneration;
	CombatYieldLease = PoolLeaseSerial;
}

bool AEnemyBase::IsCombatTacticalCandidateFailure(FName Intent, const FVector& Candidate) const
{
	return HasAuthority() && GetWorld()
		&& CombatTacticalEpisode.IsFailure(Intent, Candidate, GetWorld()->GetTimeSeconds());
}

void AEnemyBase::RecordCombatTacticalCandidateFailure(FName Intent, const FVector& Candidate, const FBox& Bounds)
{
	if (HasAuthority() && GetWorld())
	{
		CombatTacticalEpisode.RecordFailure(Intent, Candidate, Bounds, GetWorld()->GetTimeSeconds());
	}
}

bool AEnemyBase::WasCombatTacticalCandidateChecked(FName Intent, const FVector& Candidate) const
{
	return CombatTacticalEpisode.WasChecked(Intent, Candidate);
}

void AEnemyBase::MarkCombatTacticalCandidateChecked(FName Intent, const FVector& Candidate)
{
	if (HasAuthority())
	{
		CombatTacticalEpisode.MarkChecked(Intent, Candidate);
	}
}

void AEnemyBase::ResetCombatTacticalEpisode(bool bClearFailureCache)
{
	if (!HasAuthority())
	{
		return;
	}
	ReleaseCombatReservation();
	ClearCombatFlightHandoff();
	CombatTacticalEpisode.Reset(bClearFailureCache);
	TacticalSightTarget.Reset();
	TacticalSightBlockedAt = -1.0;
	if (bClearFailureCache)
	{
		if (URoomCombatSubsystem* Rooms = TacticalFailureRooms.Get())
		{
			Rooms->OnMovementObstacleOpened.Remove(TacticalObstacleHandle);
		}
		TacticalObstacleHandle.Reset();
		TacticalFailureRooms.Reset();
	}
}

void AEnemyBase::HandleCombatTacticalObstacleOpened(const FBox& Bounds)
{
	if (HasAuthority())
	{
		CombatTacticalEpisode.InvalidateSpace(Bounds);
		for (int32 Index = 0; Index < TargetAttackAttemptRecords.Num(); ++Index)
		{
			FTargetAttackAttemptRecord& Entry = TargetAttackAttemptRecords[Index];
			// 전체 방 변경이 아니라 저장한 사선과 교차하는 실제 장애물 변경만 사용 기록을 해제한다.
			if (Bounds.IsValid && Entry.bUsed && Entry.FailureBounds.IsValid && Entry.FailureBounds.Intersect(Bounds))
			{
				Entry.bUsed = false;
				Entry.bAlternativesExhausted = false;
				if (ActiveAttemptIndex == Index)
				{
					FinishTargetAttackAttempt();
				}
			}
		}
	}
}

void AEnemyBase::UpdateCombatTacticalSightBlock()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	CanContinueCombatTacticalRequest();
	FVector ProtectedStart;
	FVector ProtectedEnd;
	uint64 YieldSerial = 0;
	if (GetCombatYieldContext(ProtectedStart, ProtectedEnd, YieldSerial))
	{
		// 자기 사선이 열렸다는 이유로 동료를 위한 양보 선택/경로를 완료 처리하지 않는다.
		return;
	}
	AActor* Target = GetTrackedTarget();
	const bool bEligible = IsA<AVECDrone>() && RuntimeStat.Type == EEnemyType::Gun
		&& CombatState == EEnemyCombatState::Combat && !bIsPossessed && !IsAIControlSuppressed()
		&& !IsLocationInvestigationActive() && !IsDead() && IsValid(Target) && IsValid(CurrentWeapon)
		&& !IsTargetOutsideWeaponRange(Target);
	if (!bEligible)
	{
		TacticalSightTarget.Reset();
		TacticalSightBlockedAt = -1.0;
		return;
	}
	if (TacticalSightTarget.Get() != Target)
	{
		TacticalSightTarget = Target;
		TacticalSightBlockedAt = -1.0;
	}
	// 전조/회복/회전은 사선 차단 사유가 아니다. 같은 노출 후보를 실제 현재 발사 원점에서만 검사한다.
	const bool bWasRepositionRequired = NeedsCombatTacticalReposition();
	int32 RemainingChecks = 32;
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	const bool bDirect = AI && bPlayerCurrentlyVisible && AI->IsTargetDirectlyVisible(Target);
	if ((!UsesCombatNavigationObservation() || bDirect) && HasCombatAttackPathFrom(Target, GetActorLocation(), RemainingChecks))
	{
		TacticalSightBlockedAt = -1.0;
		// 현재 자리에서 사선이 복구됐으면 재배치 목적도 달성했다. 종료된 실패 회차는 되살리지 않는다.
		// 번호도 교체해 다시 가려지는 동안 옛 요청이 늦게 도착해도 적용하지 못하게 한다.
		if (bWasRepositionRequired)
		{
			CompleteCombatTacticalSelectionSuccess();
		}
	}
	else if (TacticalSightBlockedAt < 0.0)
	{
		TacticalSightBlockedAt = GetWorld()->GetTimeSeconds();
	}
	if (bDirect && TacticalSightBlockedAt >= 0.0 && GetWorld()->GetTimeSeconds() >= NextCombatAllyBlockCheckAt)
	{
		const float Interval = RuntimeStat.CombatYieldCheckInterval;
		NextCombatAllyBlockCheckAt = GetWorld()->GetTimeSeconds()
			+ (FMath::IsFinite(Interval) ? FMath::Max(0.01f, Interval) : 0.5f);
		int32 AllyChecks = 16;
		FVector ShotOrigin;
		if (AEnemyBase* Blocker = CurrentWeapon->FindTargetAttackBlockingAlly(Target, GetCombatAimPoint(Target), AllyChecks, ShotOrigin))
		{
			if (TrackingRooms.IsValid())
			{
				TrackingRooms->RequestCombatYield(this, Blocker, ShotOrigin, GetCombatAimPoint(Target));
			}
		}
	}
}

bool AEnemyBase::NeedsCombatTacticalReposition() const
{
	FVector ProtectedStart;
	FVector ProtectedEnd;
	uint64 YieldSerial = 0;
	if (GetCombatYieldContext(ProtectedStart, ProtectedEnd, YieldSerial))
	{
		return true;
	}
	const float Duration = FMath::IsFinite(RuntimeStat.TacticalSightBlockDuration)
		? FMath::Max(0.0f, RuntimeStat.TacticalSightBlockDuration) : 0.3f;
	return HasAuthority() && GetWorld() && TacticalSightBlockedAt >= 0.0 && TacticalSightTarget.Get() == GetTrackedTarget()
		&& GetWorld()->GetTimeSeconds() - TacticalSightBlockedAt >= Duration;
}

bool AEnemyBase::UsesCombatNavigationObservation() const
{
	return IsA<AVECDrone>() && RuntimeStat.Type == EEnemyType::Gun
		&& !bIsPossessed && !Cast<ALaserWeapon>(CurrentWeapon);
}

FVector AEnemyBase::GetCombatNavigationTargetLocation(const AActor* Target) const
{
	if (!UsesCombatNavigationObservation())
	{
		return IsValid(Target) ? Target->GetActorLocation() : LastKnownPlayerLocation;
	}
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (IsValid(Target) && AI && bPlayerCurrentlyVisible && AI->IsTargetDirectlyVisible(Target))
	{
		return Target->GetActorLocation();
	}
	// 타깃 교체 시 다른 대상의 직접 관측점을 전용하지 않는다. 공유 입력은 별도 관측 좌표만 사용한다.
	return CombatNavigationObservedTarget.Get() == Target
		? CombatNavigationObservedLocation : LastKnownPlayerLocation;
}

void AEnemyBase::RememberCombatNavigationPath(uint64 Reservation, const FVector& Destination, const TArray<FVector>& Waypoints, bool bProgressOnly)
{
	if (!HasAuthority() || Reservation == 0 || Reservation != GetCombatReservationRequest()
		|| !IsCombatReservationRequestCurrent(Reservation) || Waypoints.Num() < 2)
	{
		return;
	}
	CombatNavigationPathReservation = Reservation;
	CombatNavigationPathSerial = GetCombatTacticalEpisodeSerial();
	CombatNavigationPathDestination = Destination;
	CombatNavigationPath = Waypoints;
	bCombatNavigationProgressOnly = bProgressOnly;
	CombatNavigationPathTarget = GetTrackedTarget();
	CombatNavigationPathController = GetController();
	CombatNavigationPathGeneration = GetPoolGameplayGeneration();
	CombatNavigationPathLease = GetPoolLeaseSerial();
	bCombatNavigationRetargetPending = false;
}

bool AEnemyBase::GetCombatNavigationPath(uint64 Reservation, const FVector& Destination, TArray<FVector>& OutWaypoints, bool* OutProgressOnly) const
{
	if (!HasAuthority() || Reservation == 0 || Reservation != CombatNavigationPathReservation
		|| Reservation != GetCombatReservationRequest()
		|| !IsCombatReservationRequestCurrent(Reservation) || !IsCombatTacticalEpisodeCurrent(CombatNavigationPathSerial)
		|| !Destination.Equals(CombatNavigationPathDestination, 1.0f) || CombatNavigationPath.Num() < 2)
	{
		return false;
	}
	OutWaypoints = CombatNavigationPath;
	if (OutProgressOnly)
	{
		*OutProgressOnly = bCombatNavigationProgressOnly;
	}
	return true;
}

void AEnemyBase::ObserveTrackedTarget(AActor* Target)
{
	if (!HasAuthority() || !IsValid(Target))
	{
		return;
	}
	AEnemyAIController* TargetController = Cast<AEnemyAIController>(GetController());
	if (!TargetController || !CanUseEnemyPerception() || !TargetController->IsValidTrackingTarget(Target))
	{
		return;
	}
	const bool bChangedTarget = TrackedTarget.Get() != Target;
	TArray<FVector> PreviousRoute;
	FVector PreviousDestination = FVector::ZeroVector;
	bool bPreviousProgressOnly = false;
	const FVector PreviousVelocity = GetVelocity();
	const bool bCanOfferPreviousRoute = bChangedTarget && UsesCombatNavigationObservation()
		&& TrackingController.Get() == TargetController
		&& GetCombatNavigationHandoff(PreviousRoute, PreviousDestination, &bPreviousProgressOnly, true);
	const bool bPreparationContractChanged = (PreparedAttackTarget.IsValid() || PreparedAttackWeapon.IsValid() || PreparedAttackController.IsValid())
		&& (PreparedAttackWeapon.Get() != CurrentWeapon || PreparedAttackController.Get() != TargetController);
	if (bChangedTarget || bPreparationContractChanged)
	{
		// 제거된 대상의 weak 참조가 비어 있어도 옛 공격을 닫는다. 대상 교체는 무기 공통 기한을 보존한다.
		ResetTargetAttackContinuity(bPreparationContractChanged);
	}
	if (bChangedTarget)
	{
		ClearTargetTracking();
	}
	TrackedTarget = Target;
	bAimTargetIsShooter = Target->IsA<AShooterCharacter>();
	TrackingController = TargetController;
	bTrackingExpiredForSearch = false;
	if (bChangedTarget)
	{
		RestoreTargetAttackPreparation();
	}
	if (bCanOfferPreviousRoute)
	{
		// 형상만 새 후보로 넘긴다. 이전 예약/요청/사격 승인은 정리했고 새 대상 기준으로 다시 검증한다.
		CombatNavigationPath = MoveTemp(PreviousRoute);
		CombatNavigationPathDestination = PreviousDestination;
		CombatNavigationPathTarget = Target;
		CombatNavigationPathController = TargetController;
		CombatNavigationPathGeneration = GetPoolGameplayGeneration();
		CombatNavigationPathLease = GetPoolLeaseSerial();
		bCombatNavigationProgressOnly = bPreviousProgressOnly;
		bCombatNavigationRetargetPending = true;
		CombatNavigationRetargetVelocity = PreviousVelocity;
		CombatNavigationRetargetVelocityDestination = PreviousDestination;
		CombatNavigationRetargetVelocityExpiresAt = GetWorld()->GetTimeSeconds() + 2.0;
	}
	if (!UsesCombatNavigationObservation() || (bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(Target)))
	{
		const FVector ObservedLocation = Target->GetActorLocation();
		if (UsesCombatNavigationObservation())
		{
			CombatNavigationObservedTarget = Target;
			CombatNavigationObservedLocation = ObservedLocation;
		}
		UpdateLastKnownPlayerLocation(ObservedLocation);
	}
}

void AEnemyBase::RefreshTargetTracking()
{
	if (!HasAuthority())
	{
		return;
	}
	ON_SCOPE_EXIT { RefreshPerceptionGoal(); };
	if (!CanUseEnemyPerception() || IsDead() || CombatState == EEnemyCombatState::Stun)
	{
		// 추적 중단과 실제 Sight 상실은 다르다. 기절 중인 정상 감지 플래그를 지우지 않는다.
		ResetTargetAttackContinuity();
		ClearTargetTracking();
		return;
	}
	AEnemyAIController* TargetController = Cast<AEnemyAIController>(GetController());
	if (!TargetController || TargetController->GetPawn() != this)
	{
		ClearTargetTracking();
		return;
	}
	if (!TrackingRooms.IsValid())
	{
		TrackingRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	}
	UEnemyRoomSubsystem* Rooms = TrackingRooms.Get();
	const bool bCanShare = CombatState == EEnemyCombatState::Combat && CanUseRoomTargetSharing()
		&& Rooms && Rooms->IsRoomInCombat(GetDefaultRoomTag());
	AActor* PreviousTarget = TrackedTarget.Get();
	const bool bWasAnnounced = bTrackingAnnounced;
	const bool bWasDirect = bTrackingWasDirectlyVisible;
	if (bWasDirect && !bTrackingFromRoom && !bCanShare && CombatState == EEnemyCombatState::Combat
		&& !(bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(PreviousTarget)))
	{
		const float Grace = FMath::IsFinite(RuntimeStat.TargetTrackingGraceSeconds)
			? FMath::Max(0.0f, RuntimeStat.TargetTrackingGraceSeconds) : 0.0f;
		// 다른 플레이어가 계속 보여도 현재 대상의 시야 상실은 별도로 유예를 시작한다.
		// Room에 보고된 대상은 아래의 실제 관측 시각을 사용해 이 로컬 기한을 대체한다.
		TargetTrackingExpiresAt = GetWorld()->GetTimeSeconds() + Grace;
	}
	FEnemyRoomTargetObservationSnapshot Observation;
	bool bHasRoomObservation = bCanShare
		&& Rooms->TryGetRoomTargetObservation(this, PreviousTarget, Observation)
		&& TargetController->IsValidTrackingTarget(PreviousTarget);
	AActor* Target = GetTrackedTarget();
	if (!Target && bHasRoomObservation)
	{
		Target = PreviousTarget;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const float Interval = FMath::IsFinite(RuntimeStat.TargetReevaluationInterval)
		? FMath::Max(0.01f, RuntimeStat.TargetReevaluationInterval) : 0.5f;
	const AAutoTurret* Turret = Cast<AAutoTurret>(this);
	const bool bNormalTurret = Turret && Turret->UsesNormalTurretAttackPolicy();
	const bool bTargetDirectlyVisible = bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(Target);
	const bool bTurretSelectionBoundary = !Target || !bTargetDirectlyVisible
		|| bTargetSelectionBoundaryPending || Now >= NextTargetReevaluationAt;
	// 기존 재평가 간격 안에서만 후보 사선을 검사한다. 매 프레임 감지 갱신이 후보 Trace를 반복하지 않는다.
	AActor* OpenTurretCandidate = bNormalTurret && bPlayerCurrentlyVisible && bTurretSelectionBoundary
		&& !HasHitResponse() && !IsLocationInvestigationActive()
		? TargetController->GetPreferredAttackableTurretTarget(Target) : nullptr;
	AActor* VisibleCandidate = bPlayerCurrentlyVisible ? TargetController->GetPreferredKnownVisibleTarget() : nullptr;
	if (bNormalTurret && Target && bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(Target))
	{
		// 모두 막히면 현재 관측을 추적한다. 다른 대상이 열렸을 때만 공격 가능성으로 인계한다.
		VisibleCandidate = OpenTurretCandidate ? OpenTurretCandidate : Target;
	}
	else if (OpenTurretCandidate)
	{
		VisibleCandidate = OpenTurretCandidate;
	}
	const bool bCommittedAttackProtected = IsHitResponseCommitted();
	const bool bDedicatedAttackProtected = bCommittedAttackProtected
		|| (Turret && !bNormalTurret && AttackPhase != EEnemyAttackPhase::Idle);
	// 시야 집합이 먼저다. 숨은 대상의 유예/버스트/유지 기한은 보이는 대상 인계를 막지 않는다.
	// 커밋된 전용 공격만 기존 종료 경계까지 대상을 유지한다.
	const bool bVisibleHandoff = VisibleCandidate && !bTargetDirectlyVisible && !bCommittedAttackProtected;
	const bool bBurstProtected = TargetSelectionBurstTarget.Get() == Target && Target
		&& (TargetSelectionBlockedAt < 0.0 || Now - TargetSelectionBlockedAt < Interval);
	const bool bReevaluate = bVisibleHandoff || (Target && CombatState == EEnemyCombatState::Combat
		&& !IsLocationInvestigationActive() && !HasHitResponse() && (!bBurstProtected || bNormalTurret) && !bDedicatedAttackProtected
		&& (bTargetSelectionBoundaryPending || Now >= NextTargetReevaluationAt));
	if (!Target || bReevaluate)
	{
		// 직접 보이는 후보가 없을 때만 공유 후보를 사용한다. 종류 순위로 시야 집합을 역전하지 않는다.
		NextTargetReevaluationAt = Now + Interval;
		bTargetSelectionBoundaryPending = false;
		AActor* Candidate = VisibleCandidate;
		FEnemyRoomTargetObservationSnapshot CandidateObservation;
		const bool bHasSharedCandidate = !Candidate && bCanShare && Rooms->TrySelectRoomTarget(this, CandidateObservation);
		if (bHasSharedCandidate)
		{
			Candidate = CandidateObservation.TargetActor.Get();
		}
		if (Candidate && Candidate != Target)
		{
			const auto CurrentPriority = OutlierEnemyTargetRules::GetPriority(Target);
			const auto CandidatePriority = OutlierEnemyTargetRules::GetPriority(Candidate);
			const float Hold = FMath::IsFinite(RuntimeStat.TargetMinimumHoldSeconds)
				? FMath::Max(0.0f, RuntimeStat.TargetMinimumHoldSeconds) : 1.0f;
			const float Gain = FMath::IsFinite(RuntimeStat.TargetSwitchDistanceGain)
				? FMath::Max(0.0f, RuntimeStat.TargetSwitchDistanceGain) : 200.0f;
			const FVector CandidateLocation = bHasSharedCandidate && Candidate == CandidateObservation.TargetActor.Get()
				&& !TargetController->IsTargetDirectlyVisible(Candidate)
				? CandidateObservation.LastObservedLocation : Candidate->GetActorLocation();
			const FVector CurrentLocation = bHasRoomObservation && !TargetController->IsTargetDirectlyVisible(Target)
				? Observation.LastObservedLocation : Target ? GetCombatNavigationTargetLocation(Target) : GetActorLocation();
			const double CurrentDistance = FVector::Distance(GetActorLocation(), CurrentLocation);
			const double CandidateDistance = FVector::Distance(GetActorLocation(), CandidateLocation);
			const bool bSamePriorityImprovement = CandidatePriority == CurrentPriority && Now - TargetSelectedAt >= Hold
				&& CandidateDistance < CurrentDistance && CurrentDistance - CandidateDistance >= Gain;
			const bool bTurretAttackableHandoff = bNormalTurret && OpenTurretCandidate == Candidate;
			if (!Target || bVisibleHandoff || bTurretAttackableHandoff || CandidatePriority < CurrentPriority || bSamePriorityImprovement)
			{
				Target = Candidate;
				bHasRoomObservation = bCanShare && Rooms->TryGetRoomTargetObservation(this, Target, Observation);
			}
		}
	}
	if (!Target)
	{
		if (bPlayerCurrentlyVisible && !TargetController->IsValidTrackingTarget(TargetController->GetCachedVisibleTarget()))
		{
			bPlayerCurrentlyVisible = false;
		}
		if (TrackingController.IsValid())
		{
			FinishTargetTracking();
		}
		return;
	}
	const bool bDirect = bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(Target);
	if (bDirect)
	{
		LastPerceptionObservedAt = GetWorld()->GetTimeSeconds();
	}
	else if (bHasRoomObservation)
	{
		LastPerceptionObservedAt = FMath::Max(LastPerceptionObservedAt, Observation.LastObservedAt);
	}
	ObserveTrackedTarget(Target);
	if (!bDirect && bHasRoomObservation && UsesCombatNavigationObservation()
		&& CombatNavigationObservedTarget.Get() != Target)
	{
		// 직접 본 적 없는 공유 대상도 보고된 위치까지만 접근하며 실시간 위치로 승격하지 않는다.
		UpdateLastKnownPlayerLocation(Observation.LastObservedLocation);
	}
	bTrackingFromRoom = bHasRoomObservation;
	TrackingRoomTag = bHasRoomObservation ? GetDefaultRoomTag() : FGameplayTag();
	const double Deadline = bHasRoomObservation ? Observation.TrackingExpiresAt : TargetTrackingExpiresAt;
	const bool bNeedsExpiryTimer = !bDirect && !(bHasRoomObservation && Observation.bDirectlyObserved);
	if (!bNeedsExpiryTimer)
	{
		GetWorldTimerManager().ClearTimer(TargetTrackingTimer);
	}
	else if (Deadline != TargetTrackingExpiresAt || !GetWorldTimerManager().TimerExists(TargetTrackingTimer))
	{
		// 만료 콜백도 먼저 대체 관측/대상을 확인한다. 공유를 받았다는 이유로 Now + 3초를 만들지 않는다.
		GetWorldTimerManager().SetTimer(TargetTrackingTimer, this, &AEnemyBase::RefreshTargetTracking,
			static_cast<float>(FMath::Max(Deadline - GetWorld()->GetTimeSeconds(), 0.001)), false);
	}
	TargetTrackingExpiresAt = Deadline;
	bTrackingWasDirectlyVisible = bDirect;
	const bool bNewTarget = !bWasAnnounced || PreviousTarget != Target;
	if (bNewTarget)
	{
		static const IConsoleVariable* Diagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.PerceptionDiagnostics"));
		if (Diagnostics && Diagnostics->GetInt() != 0)
		{
			UE_LOG(LogOutlier, Display, TEXT("[EnemyTargetSelection] Enemy=%s Previous=%s Selected=%s Direct=%d Time=%.3f RecoveryRemaining=%.3f"),
				*GetName(), *GetNameSafe(PreviousTarget), *GetNameSafe(Target), bDirect, Now, GetTargetAttackRecoveryRemaining());
		}
		TargetSelectedAt = Now;
		NextTargetReevaluationAt = Now + Interval;
		const bool bSamePreparedTarget = PreparedAttackTarget.Get() == Target
			&& PreparedAttackWeapon.Get() == CurrentWeapon && PreparedAttackController.Get() == TargetController;
		bResumeTargetAttackPreparation = bSamePreparedTarget;
		// ObserveTrackedTarget에서 저장/복원과 이전 권한 정리를 끝냈다. 여기서 다시 초기화하지 않는다.
		ConsumedSharedTargetRevision = SharedTargetRevision;
		if (!IsLocationInvestigationActive())
		{
			EndSearchEpisode();
		}
	}
	// 관찰 대기도 실제 목표가 달라지면 새 회차를 얻는다. 동일 위치 보고/새 EQS 점은 이 기준을 움직이지 않는다.
	if (CombatState == EEnemyCombatState::Combat && (bNewTarget || bReevaluate) && !IsLocationInvestigationActive())
	{
		EnsureCombatTacticalEpisode(Target, GetCombatNavigationTargetLocation(Target));
	}
	bTrackingAnnounced = true;
	if (bNewTarget || bWasDirect != bDirect)
	{
		SendEnemyStateTreeEvent(bNewTarget
			? FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Perception.TargetAcquired")) : TAG_EnemyTrackingChanged);
	}
}

void AEnemyBase::RefreshTargetTrackingAtSelectionBoundary()
{
	if (HasAuthority())
	{
		bTargetSelectionBoundaryPending = true;
		RefreshTargetTracking();
	}
}

void AEnemyBase::EndTargetSelectionBurst(AActor* BurstTarget)
{
	if (HasAuthority() && BurstTarget && TargetSelectionBurstTarget.Get() == BurstTarget)
	{
		TargetSelectionBurstTarget.Reset();
		TargetSelectionBlockedAt = -1.0;
		bTargetSelectionBoundaryPending = true;
	}
}

void AEnemyBase::LogNoiseInvestigationDiagnostic(const TCHAR* Stage, const FEnemyHeardNoise& Clue, const TCHAR* Reason) const
{
	// 감지 진단 스위치를 재사용한다. 사건/상태 변경 때만 기록하고 추가 Trace나 평가를 수행하지 않는다.
	static const IConsoleVariable* Diagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.PerceptionDiagnostics"));
	if (!Diagnostics || Diagnostics->GetInt() == 0 || !GetWorld())
	{
		return;
	}
	const double NoiseHoldEndsAt = LastNoiseDestinationChangedAt < 0.0 ? -1.0
		: LastNoiseDestinationChangedAt + (FMath::IsFinite(NoiseDestinationUpdateInterval)
			? FMath::Max(0.0f, NoiseDestinationUpdateInterval) : 1.0f);
	const double VisualHoldEndsAt = VisualInvestigation.AdoptedAt < 0.0 ? -1.0
		: VisualInvestigation.AdoptedAt + (FMath::IsFinite(VisualInvestigationHoldDuration)
			? FMath::Max(0.0f, VisualInvestigationHoldDuration) : 1.0f);
	UE_LOG(LogOutlier, Log,
		TEXT("[EnemyHearingDiag] Stage=%s Enemy=%s Source=%s Clue=%llu Occurred=%.3f Now=%.3f Expires=%.3f State=%s Goal=%s Investigation=%llu Search=%llu Reason=%s NoiseHoldUntil=%.3f VisualHoldUntil=%.3f Location=%s Received=%.3f ReceiveLag=%.3f ReceivedDistance=%.1f Near=%d Facing=%llu StartDelay=%.3f FacingElapsed=%.3f Lease=%d Generation=%d"),
		Stage, *GetName(), *GetNameSafe(Clue.Source.Get()), static_cast<unsigned long long>(Clue.Serial),
		Clue.OccurredAt, GetWorld()->GetTimeSeconds(), Clue.OccurredAt + HeardNoiseLifetime,
		*UEnum::GetValueAsString(CombatState), *UEnum::GetValueAsString(PerceptionGoal.Type),
		static_cast<unsigned long long>(NoiseInvestigationSerial), static_cast<unsigned long long>(SearchEpisode.Serial),
		Reason, NoiseHoldEndsAt, VisualHoldEndsAt, *Clue.Location.ToCompactString(),
		Clue.ReceivedAt, Clue.ReceivedAt >= 0.0 ? Clue.ReceivedAt - Clue.OccurredAt : -1.0,
		Clue.ReceivedDistance, Clue.bNearDirectionReaction ? 1 : 0,
		static_cast<unsigned long long>(NoiseDirectionConfirmation.Serial),
		NoiseDirectionConfirmation.Clue.Serial == Clue.Serial && Clue.ReceivedAt >= 0.0
			? NoiseDirectionConfirmation.StartedAt - Clue.ReceivedAt : -1.0,
		NoiseDirectionConfirmation.Clue.Serial == Clue.Serial && NoiseDirectionConfirmation.StartedAt >= 0.0
			? GetWorld()->GetTimeSeconds() - NoiseDirectionConfirmation.StartedAt : -1.0,
		GetPoolLeaseSerial(), GetPoolGameplayGeneration());
}

bool AEnemyBase::RecordHeardNoise(AActor* Source, const FVector& Location, double OccurredAt, bool* bValidStimulus)
{
	if (bValidStimulus)
	{
		*bValidStimulus = false;
	}
	const UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !CanUseEnemyPerception() || !UsesHearingPerception()
		|| IsDead() || !IsValid(Source) || Source == this || Source->IsActorBeingDestroyed()
		|| Source->GetWorld() != World || Location.ContainsNaN() || !FMath::IsFinite(OccurredAt)
		|| !FMath::IsFinite(HeardNoiseLifetime) || HeardNoiseLifetime <= 0.0f)
	{
		return false;
	}

	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	const auto Priority = OutlierEnemyTargetRules::GetPriority(Source);
	if (!AI || !AI->IsValidTrackingTarget(Source)
		|| Priority == OutlierEnemyTargetRules::ETargetPriority::EnemyUnit
		|| Priority == OutlierEnemyTargetRules::ETargetPriority::Invalid)
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	// 수신 시각이 아니라 자극의 발생 시각으로 만료를 계산한다.
	// 중복·역순 여부는 아래에서 출처와 근접 후보별로 판단한다.
	if (OccurredAt < 0.0 || OccurredAt > Now || Now - OccurredAt >= HeardNoiseLifetime
		|| OccurredAt <= ConsumedNoiseAt)
	{
		return false;
	}
	// 인지 시간은 조사 후보의 보관 여부와 별개다. 중복/역순은 인지 상태에서도 출처별로 걸러낸다.
	if (bValidStimulus)
	{
		*bValidStimulus = true;
	}
	HeardNoiseCandidates.RemoveAll([this](const FEnemyHeardNoise& Candidate) { return !IsHeardNoiseValid(Candidate); });
	FEnemyHeardNoise Incoming;
	Incoming.Source = Source;
	Incoming.Location = Location;
	Incoming.OccurredAt = OccurredAt;
	Incoming.ReceivedAt = Now;
	Incoming.ReceivedDistance = FVector::Distance(GetActorLocation(), Location);
	Incoming.bNearDirectionReaction = FMath::IsFinite(RuntimeStat.AwarenessNearDistance)
		&& RuntimeStat.AwarenessNearDistance >= 0.0f
		&& Incoming.ReceivedDistance <= RuntimeStat.AwarenessNearDistance;
	// 근접한 동일 출처 입력만 병합한다. 다른 출처의 오래된 유효 소리는 거리 비교에 남긴다.
	FEnemyHeardNoise* Nearby = HeardNoiseCandidates.FindByPredicate([&](const FEnemyHeardNoise& Candidate)
	{
		return Candidate.Source.Get() == Source && FVector::DistSquared(Candidate.Location, Location) <= FMath::Square(100.0);
	});
	if (Nearby)
	{
		if (OccurredAt <= Nearby->OccurredAt)
		{
			return false;
		}
		Incoming.Serial = ++NextHeardNoiseSerial;
		*Nearby = Incoming;
	}
	else
	{
		if (HeardNoiseCandidates.Num() == MaxHeardNoiseCandidates)
		{
			// 용량이 찼을 때 과거 관측·채택 이전 후보가 새 조사 근거를 모두 밀어내지 않게 한다.
			// 같은 관측 구간 안에서는 가까운 후보, 동거리에서는 최신 후보를 남긴다.
			const double EvidenceAt = bNoiseInvestigationActive
				? FMath::Max(LastPerceptionObservedAt, NoiseInvestigationClue.OccurredAt) : LastPerceptionObservedAt;
			int32 Worst = 0;
			for (int32 Index = 1; Index < HeardNoiseCandidates.Num(); ++Index)
			{
				const bool bCandidateAfterObservation = HeardNoiseCandidates[Index].OccurredAt > EvidenceAt;
				const bool bWorstAfterObservation = HeardNoiseCandidates[Worst].OccurredAt > EvidenceAt;
				const double Distance = FVector::DistSquared(GetActorLocation(), HeardNoiseCandidates[Index].Location);
				const double WorstDistance = FVector::DistSquared(GetActorLocation(), HeardNoiseCandidates[Worst].Location);
				const bool bSameEvidenceGroup = bCandidateAfterObservation == bWorstAfterObservation;
				const bool bFartherOrOlder = Distance > WorstDistance || (Distance == WorstDistance
					&& HeardNoiseCandidates[Index].OccurredAt < HeardNoiseCandidates[Worst].OccurredAt);
				if ((!bCandidateAfterObservation && bWorstAfterObservation)
					|| (bSameEvidenceGroup && bFartherOrOlder))
				{
					Worst = Index;
				}
			}
			const double IncomingDistance = FVector::DistSquared(GetActorLocation(), Location);
			const double WorstDistance = FVector::DistSquared(GetActorLocation(), HeardNoiseCandidates[Worst].Location);
			const bool bIncomingAfterObservation = OccurredAt > EvidenceAt;
			const bool bWorstAfterObservation = HeardNoiseCandidates[Worst].OccurredAt > EvidenceAt;
			const bool bSameEvidenceGroup = bIncomingAfterObservation == bWorstAfterObservation;
			const bool bFartherOrDuplicate = IncomingDistance > WorstDistance
				|| (IncomingDistance == WorstDistance && OccurredAt <= HeardNoiseCandidates[Worst].OccurredAt);
			if ((!bIncomingAfterObservation && bWorstAfterObservation)
				|| (bSameEvidenceGroup && bFartherOrDuplicate))
			{
				return false;
			}
			HeardNoiseCandidates.RemoveAtSwap(Worst);
		}
		Incoming.Serial = ++NextHeardNoiseSerial;
		HeardNoiseCandidates.Add(Incoming);
	}
	bNoiseDirectionEvaluationPending = true;
	MergePendingNoiseDirections();
	RefreshPerceptionGoal();
	const TCHAR* Reason = PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise
		&& PerceptionGoal.NoiseClueSerial == Incoming.Serial ? TEXT("Selected")
		: HasHitResponse() && !bInvestigatingObservedLocation ? TEXT("HitResponse")
		: IsVisualInvestigationHoldActive() ? TEXT("VisualHold")
		: bNoiseInvestigationActive && Now < LastNoiseDestinationChangedAt + NoiseDestinationUpdateInterval ? TEXT("NoiseHold")
		: TEXT("PriorityOrDestinationEligibility");
	LogNoiseInvestigationDiagnostic(TEXT("CandidateReceived"), Incoming, Reason);
	if (Incoming.bNearDirectionReaction && IsNoiseDirectionMerged(Incoming))
	{
		LogNoiseInvestigationDiagnostic(TEXT("FacingMerged"), Incoming, TEXT("SameDirectionWithinConfirmationWindow"));
	}
	else if (Incoming.bNearDirectionReaction && IsNoiseDirectionConfirmationActive())
	{
		LogNoiseInvestigationDiagnostic(TEXT("FacingBlocked"), Incoming, TEXT("QueuedUntilActiveConfirmationEnds"));
	}
	return true;
}

bool AEnemyBase::TryGetHeardNoise(FEnemyHeardNoise& OutNoise) const
{
	OutNoise = FEnemyHeardNoise();
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (!IsHeardNoiseValid(Candidate))
		{
			continue;
		}
		const double Distance = FVector::DistSquared(GetActorLocation(), Candidate.Location);
		const double BestDistance = FVector::DistSquared(GetActorLocation(), OutNoise.Location);
		const bool bKeepSelection = Distance == BestDistance && Candidate.OccurredAt == OutNoise.OccurredAt
			&& Candidate.Location == PerceptionGoal.Location && Candidate.OccurredAt == PerceptionGoal.InformationAt;
		if (OutNoise.OccurredAt < 0.0 || Distance < BestDistance
			|| (Distance == BestDistance && Candidate.OccurredAt > OutNoise.OccurredAt) || bKeepSelection)
		{
			OutNoise = Candidate;
		}
	}
	return OutNoise.OccurredAt >= 0.0;
}

bool AEnemyBase::TryGetDirectionConfirmationNoise(FEnemyHeardNoise& OutNoise) const
{
	OutNoise = FEnemyHeardNoise();
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (Candidate.bUsedForDirectionConfirmation || !IsHeardNoiseValid(Candidate)
			|| Candidate.Serial == NoiseInvestigationClue.Serial
			|| Candidate.Serial == PerceptionGoal.NoiseClueSerial
			|| Candidate.OccurredAt <= LastPerceptionObservedAt
			|| (Candidate.Location - GetActorLocation()).IsNearlyZero()
			|| IsNoiseDirectionMerged(Candidate))
		{
			continue;
		}
		const double Distance = FVector::DistSquared(GetActorLocation(), Candidate.Location);
		const double BestDistance = FVector::DistSquared(GetActorLocation(), OutNoise.Location);
		if (OutNoise.Serial == 0 || Distance < BestDistance
			|| (Distance == BestDistance && Candidate.OccurredAt > OutNoise.OccurredAt))
		{
			OutNoise = Candidate;
		}
	}
	return OutNoise.Serial != 0;
}

bool AEnemyBase::MarkNoiseDirectionUsed(uint64 ClueSerial)
{
	if (!HasAuthority() || ClueSerial == 0)
	{
		return false;
	}
	for (FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (Candidate.Serial != ClueSerial || Candidate.bUsedForDirectionConfirmation || !IsHeardNoiseValid(Candidate))
		{
			continue;
		}
		// 돌아본 사건도 원래 수명 안에서는 이동 단서다. 위치 조사 소비 경계는 변경하지 않는다.
		Candidate.bUsedForDirectionConfirmation = true;
		return true;
	}
	return false;
}

bool AEnemyBase::TryGetNearDirectionConfirmationNoise(FEnemyHeardNoise& OutNoise) const
{
	OutNoise = FEnemyHeardNoise();
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (!Candidate.bNearDirectionReaction || Candidate.bUsedForDirectionConfirmation
			|| !IsHeardNoiseValid(Candidate) || Candidate.OccurredAt <= LastPerceptionObservedAt
			|| (Candidate.Location - GetActorLocation()).IsNearlyZero() || IsNoiseDirectionMerged(Candidate)) continue;
		if (OutNoise.Serial == 0 || Candidate.ReceivedDistance < OutNoise.ReceivedDistance
			|| (Candidate.ReceivedDistance == OutNoise.ReceivedDistance && Candidate.OccurredAt > OutNoise.OccurredAt))
		{
			OutNoise = Candidate;
		}
	}
	return OutNoise.Serial != 0;
}

bool AEnemyBase::CanRunNoiseDirectionConfirmation(bool bNearReaction, const TCHAR** OutReason) const
{
	if (OutReason) *OutReason = nullptr;
	const auto Blocked = [OutReason](const TCHAR* Reason)
	{
		if (OutReason) *OutReason = Reason;
		return false;
	};
	bNearReaction |= IsNoiseDirectionConfirmationActive() && NoiseDirectionConfirmation.Clue.bNearDirectionReaction;
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (!HasAuthority() || !GetWorld() || !AI || !UsesHearingPerception() || !CanUseEnemyPerception()
		|| IsDead() || (CombatState != EEnemyCombatState::Combat && CombatState != EEnemyCombatState::Alert))
	{
		return Blocked(TEXT("StateOrPerceptionUnavailable"));
	}
	if (AI->IsAttackAimActive() || IsHitResponseCommitted()) return Blocked(TEXT("AttackAimOrCommittedAction"));
	if ((HasHitResponse() && (!bInvestigatingObservedLocation || IsHitDirectionConfirmationActive()))
		|| IsHitClueValid(PendingHitClue)) return Blocked(TEXT("HitResponse"));
	if (AI->IsTargetDirectlyVisible(AI->GetCachedVisibleTarget())
		|| AI->IsTargetDirectlyVisible(GetTrackedTarget()))
	{
		return Blocked(TEXT("DirectSight"));
	}
	// 근거리 반응은 이동 목표를 부여하지 않는다. 같은 상위 행동 경계 안에서 회전만 먼저 시작한다.
	if (bNearReaction) return true;
	if (!bNoiseInvestigationActive && VisualInvestigation.AdoptedAt < 0.0) return Blocked(TEXT("AwaitInvestigationAdoption"));
	if (!bNoiseInvestigationActive && PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise)
	{
		return Blocked(TEXT("AwaitInvestigationAdoption"));
	}
	// 이동 의도가 먼저 정해졌으면 별도 돌아보기를 시작하지 않는다. 실제 채택 전에도 옛 확인은 양보한다.
	const bool bEligible = (PerceptionGoal.Type == EEnemyPerceptionGoalType::Search
		|| PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise
		|| (bInvestigatingObservedLocation && PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateHit))
		&& (!bNoiseInvestigationActive || PerceptionGoal.NoiseClueSerial == NoiseInvestigationClue.Serial);
	return bEligible || Blocked(TEXT("HigherPriorityGoal"));
}

bool AEnemyBase::IsNoiseDirectionMerged(const FEnemyHeardNoise& Noise) const
{
	if (!GetWorld() || NoiseDirectionConfirmation.Serial == 0)
	{
		return false;
	}
	const float Interval = FMath::IsFinite(NoiseDirectionConfirmationInterval)
		? FMath::Max(0.0f, NoiseDirectionConfirmationInterval) : 1.0f;
	const double MergeEndsAt = FMath::Max(NoiseDirectionConfirmation.EndedAt,
		NoiseDirectionConfirmation.StartedAt + Interval);
	// 대기 종료 뒤 늦게 전달된 옛 사건도 되살리지 않는다. 종료 후 실제 발생한 사건부터 다시 허용한다.
	if (!IsNoiseDirectionConfirmationActive() && GetWorld()->GetTimeSeconds() >= MergeEndsAt
		&& Noise.OccurredAt > MergeEndsAt)
	{
		return false;
	}
	const FVector Anchor = GetActorLocation();
	const FVector Reference = NoiseDirectionConfirmation.Clue.Location - Anchor;
	const FVector Incoming = Noise.Location - Anchor;
	if (Reference.IsNearlyZero() || Incoming.IsNearlyZero())
	{
		return false;
	}
	const bool bReferenceHasYaw = Reference.SizeSquared2D() > FMath::Square(UE_SMALL_NUMBER);
	const bool bIncomingHasYaw = Incoming.SizeSquared2D() > FMath::Square(UE_SMALL_NUMBER);
	if (bReferenceHasYaw != bIncomingHasYaw)
	{
		return false;
	}
	const FRotator ReferenceRotation = Reference.Rotation();
	const FRotator IncomingRotation = Incoming.Rotation();
	const float PitchLimit = FMath::IsFinite(NoiseDirectionMergePitch)
		? FMath::Clamp(NoiseDirectionMergePitch, 0.0f, 180.0f) : 20.0f;
	// 각도 변환의 부동소수점 오차만 흡수해 정확한 경계값도 병합한다.
	if (FMath::Abs(ReferenceRotation.Pitch - IncomingRotation.Pitch) > PitchLimit + UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	// 두 소리가 모두 수직이면 임의 Yaw를 비교하지 않는다. 위·아래는 항상 별개다.
	if (!bReferenceHasYaw)
	{
		return (Reference.Z > 0.0) == (Incoming.Z > 0.0);
	}
	const float YawLimit = FMath::IsFinite(NoiseDirectionMergeYaw)
		? FMath::Clamp(NoiseDirectionMergeYaw, 0.0f, 180.0f) : 30.0f;
	return FMath::Abs(FMath::FindDeltaAngleDegrees(ReferenceRotation.Yaw, IncomingRotation.Yaw)) <= YawLimit + UE_DOUBLE_SMALL_NUMBER;
}

void AEnemyBase::MergePendingNoiseDirections()
{
	for (FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (!Candidate.bUsedForDirectionConfirmation && IsHeardNoiseValid(Candidate) && IsNoiseDirectionMerged(Candidate))
		{
			Candidate.bUsedForDirectionConfirmation = true;
		}
	}
}

bool AEnemyBase::TryBeginNoiseDirectionConfirmation()
{
	if (!HasAuthority() || !GetWorld())
	{
		return false;
	}
	RefreshPerceptionGoal();
	FEnemyHeardNoise Selected;
	const bool bNearReaction = TryGetNearDirectionConfirmationNoise(Selected);
	const TCHAR* BlockReason = nullptr;
	if (IsNoiseDirectionConfirmationActive() || !CanRunNoiseDirectionConfirmation(bNearReaction, &BlockReason))
	{
		if (bNearReaction && (LastNoiseDirectionBlockedClueSerial != Selected.Serial || LastNoiseDirectionBlockedReason != BlockReason))
		{
			LastNoiseDirectionBlockedClueSerial = Selected.Serial;
			LastNoiseDirectionBlockedReason = BlockReason;
			LogNoiseInvestigationDiagnostic(TEXT("FacingBlocked"), Selected, BlockReason ? BlockReason : TEXT("ActiveConfirmation"));
		}
		return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const float Interval = FMath::IsFinite(NoiseDirectionConfirmationInterval)
		? FMath::Max(0.0f, NoiseDirectionConfirmationInterval) : 1.0f;
	if (!bNearReaction && NoiseDirectionConfirmation.StartedAt >= 0.0 && Now < NoiseDirectionConfirmation.StartedAt + Interval)
	{
		return false;
	}
	MergePendingNoiseDirections();
	const bool bResumeDeferred = !bNearReaction && bDeferredNoiseDirectionReady
		&& DeferredNoiseInvestigationSerial == GetLocationInvestigationSerial() && IsHeardNoiseValid(DeferredNoiseDirectionClue);
	const bool bStationaryContinuation = bResumeDeferred && bDeferredNoiseDirectionStationary;
	if (bNearReaction)
	{
		if (!MarkNoiseDirectionUsed(Selected.Serial)) return false;
	}
	else if (bResumeDeferred)
	{
		Selected = DeferredNoiseDirectionClue;
		DeferredNoiseDirectionClue = FEnemyHeardNoise();
		bDeferredNoiseDirectionReady = false;
	}
	else if (!TryGetDirectionConfirmationNoise(Selected) || !MarkNoiseDirectionUsed(Selected.Serial))
	{
		bNoiseDirectionEvaluationPending = false;
		return false;
	}
	NoiseDirectionConfirmation = FEnemyNoiseDirectionConfirmation();
	NoiseDirectionConfirmation.Phase = EEnemyNoiseConfirmationPhase::Rotating;
	NoiseDirectionConfirmation.Clue = Selected;
	NoiseDirectionConfirmation.Serial = ++NextNoiseDirectionConfirmationSerial;
	NoiseDirectionConfirmation.StartedAt = Now;
	NoiseDirectionConfirmation.bStartedWhileMoving = (bNoiseInvestigationActive && !bNoiseInvestigationSearching)
		|| (bInvestigatingObservedLocation && HitResponsePhase == EHitResponsePhase::Move);
	if (bStationaryContinuation)
	{
		NoiseDirectionConfirmation.bStartedWhileMoving = false;
	}
	const FVector Direction = Selected.Location - GetActorLocation();
	if (!bNearReaction && !bResumeDeferred && NoiseDirectionConfirmation.bStartedWhileMoving && !NoiseInvestigationTravelDirection.IsNearlyZero())
	{
		const double Offset = FMath::Abs(FMath::FindDeltaAngleDegrees(NoiseInvestigationTravelDirection.Rotation().Yaw, Direction.Rotation().Yaw));
		const float Limit = FMath::IsFinite(NoiseDirectionMovingHalfAngle) ? FMath::Clamp(NoiseDirectionMovingHalfAngle, 0.0f, 180.0f) : 60.0f;
		if (Offset > Limit)
		{
			DeferredNoiseDirectionClue = Selected;
			DeferredNoiseInvestigationSerial = GetLocationInvestigationSerial();
			bDeferredNoiseDirectionReady = bDeferredNoiseDirectionStationary = false;
		}
	}
	const double YawDelta = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Direction.Rotation().Yaw);
	NoiseDirectionTurnSign = FMath::Abs(YawDelta) >= 179.9 || FMath::IsNearlyZero(YawDelta) ? 1.0f : FMath::Sign(YawDelta);
	bNoiseDirectionFacing = true;
	ForceNetUpdate();
	const float RotationTime = FMath::IsFinite(NoiseDirectionConfirmationMaxRotationTime)
		? FMath::Max(0.001f, NoiseDirectionConfirmationMaxRotationTime) : 1.0f;
	NoiseDirectionConfirmation.PhaseEndsAt = FMath::Min(Now + RotationTime, Selected.OccurredAt + HeardNoiseLifetime);
	MergePendingNoiseDirections();
	ScheduleNoiseDirectionConfirmationEnd();
	LogNoiseInvestigationDiagnostic(TEXT("FacingStarted"), Selected, bNearReaction ? TEXT("NearImmediate") : TEXT("DirectionConfirmation"));
	return true;
}

void AEnemyBase::ScheduleNoiseDirectionConfirmationEnd()
{
	const uint64 Serial = NoiseDirectionConfirmation.Serial;
	const float Remaining = static_cast<float>(FMath::Max(0.001,
		NoiseDirectionConfirmation.PhaseEndsAt - GetWorld()->GetTimeSeconds()));
	GetWorldTimerManager().SetTimer(NoiseDirectionConfirmationTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Serial] { EndNoiseDirectionConfirmation(Serial); }), Remaining, false);
}

bool AEnemyBase::NotifyNoiseDirectionReached(uint64 Serial)
{
	if (!HasAuthority() || !GetWorld())
	{
		return false;
	}
	RefreshPerceptionGoal();
	if (NoiseDirectionConfirmation.Serial != Serial || NoiseDirectionConfirmation.Phase != EEnemyNoiseConfirmationPhase::Rotating)
	{
		return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// 제한 시간이 지난 도달 알림은 성공으로 처리하지 않고 추가 대기 없이 끝낸다.
	if (Now >= NoiseDirectionConfirmation.PhaseEndsAt)
	{
		EndNoiseDirectionConfirmation(Serial);
		return false;
	}
	const float Hold = FMath::IsFinite(NoiseDirectionConfirmationHold) ? FMath::Max(0.0f, NoiseDirectionConfirmationHold) : 0.3f;
	NoiseDirectionConfirmation.Phase = EEnemyNoiseConfirmationPhase::Holding;
	LogNoiseInvestigationDiagnostic(TEXT("FacingReached"), NoiseDirectionConfirmation.Clue,
		NoiseDirectionConfirmation.bLimitedByMovingAngle ? TEXT("MovingAngleLimit") : TEXT("SourceDirection"));
	if (!IsHeardNoiseValid(NoiseDirectionConfirmation.Clue))
	{
		EndNoiseDirectionConfirmation(Serial);
		return false;
	}
	NoiseDirectionConfirmation.PhaseEndsAt = FMath::Min(Now + Hold, NoiseDirectionConfirmation.Clue.OccurredAt + HeardNoiseLifetime);
	if (Hold <= 0.0f)
	{
		EndNoiseDirectionConfirmation(Serial);
	}
	else
	{
		ScheduleNoiseDirectionConfirmationEnd();
	}
	return true;
}

bool AEnemyBase::EndNoiseDirectionConfirmation(uint64 Serial)
{
	if (HasAuthority() && DeferredNoiseDirectionClue.Serial != 0
		&& (DeferredNoiseInvestigationSerial != GetLocationInvestigationSerial()
			|| !IsHeardNoiseValid(DeferredNoiseDirectionClue) || !CanRunNoiseDirectionConfirmation()))
	{
		DeferredNoiseDirectionClue = FEnemyHeardNoise();
		bDeferredNoiseDirectionReady = false;
	}
	if (!HasAuthority() || !GetWorld() || !IsNoiseDirectionConfirmationActive() || NoiseDirectionConfirmation.Serial != Serial)
	{
		return false;
	}
	NoiseDirectionConfirmation.Phase = EEnemyNoiseConfirmationPhase::None;
	NoiseDirectionConfirmation.EndedAt = GetWorld()->GetTimeSeconds();
	const TCHAR* ActionReason = nullptr;
	const bool bCanContinue = CanRunNoiseDirectionConfirmation(NoiseDirectionConfirmation.Clue.bNearDirectionReaction, &ActionReason);
	const TCHAR* EndReason = !IsHeardNoiseValid(NoiseDirectionConfirmation.Clue) ? TEXT("ClueExpiredOrInvalid")
		: !bCanContinue ? ActionReason
		: NoiseDirectionConfirmation.EndedAt >= NoiseDirectionConfirmation.PhaseEndsAt ? TEXT("PhaseDeadline") : TEXT("Interrupted");
	LogNoiseInvestigationDiagnostic(TEXT("FacingEnded"), NoiseDirectionConfirmation.Clue, EndReason);
	bNoiseDirectionFacing = false;
	bNoiseDirectionEvaluationPending = true;
	ForceNetUpdate();
	GetWorldTimerManager().ClearTimer(NoiseDirectionConfirmationTimer);
	return true;
}

void AEnemyBase::ClearNoiseDirectionConfirmation()
{
	if (IsNoiseDirectionConfirmationActive())
	{
		LogNoiseInvestigationDiagnostic(TEXT("FacingEnded"), NoiseDirectionConfirmation.Clue, TEXT("LifecycleReset"));
	}
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(NoiseDirectionConfirmationTimer);
	}
	NoiseDirectionConfirmation = FEnemyNoiseDirectionConfirmation();
	LastNoiseDirectionBlockedClueSerial = 0;
	LastNoiseDirectionBlockedReason = nullptr;
	bNoiseDirectionFacing = false;
	bNoiseDirectionEvaluationPending = false;
	DeferredNoiseDirectionClue = FEnemyHeardNoise();
	DeferredNoiseInvestigationSerial = 0;
	bDeferredNoiseDirectionReady = bDeferredNoiseDirectionStationary = false;
	NoiseInvestigationTravelDirection = FVector::ZeroVector;
	NoiseDirectionRotationSpeed = 180.0f;
	NoiseInvestigationAcceptanceRadius = 50.0f;
	// 번호는 되돌리지 않는다. 풀/팀/Controller 경계 이전의 지연 알림은 새 확인을 끝낼 수 없다.
}

void AEnemyBase::UpdateNoiseInvestigationTravelDirection(uint64 InvestigationSerial, const FVector& Direction, float RotationSpeed)
{
	if (!HasAuthority() || (!bNoiseInvestigationActive && !bInvestigatingObservedLocation) || IsLocationSearchActive()
		|| InvestigationSerial != NoiseInvestigationSerial || Direction.ContainsNaN())
	{
		return;
	}
	// 경로가 요구하는 방향만 받는다. 돌아본 몸통 방향이나 도착 후 0인 속도를 기준으로 삼지 않는다.
	if (Direction.SizeSquared2D() > UE_SMALL_NUMBER)
	{
		NoiseInvestigationTravelDirection = Direction.GetSafeNormal2D();
	}
	NoiseDirectionRotationSpeed = FMath::IsFinite(RotationSpeed) ? FMath::Max(0.0f, RotationSpeed) : 180.0f;
}

void AEnemyBase::UpdateNoiseInvestigationProgress(uint64 InvestigationSerial, const FVector& PreviousLocation,
	const FVector& CurrentLocation, float AcceptanceRadius)
{
	if (!HasAuthority() || InvestigationSerial != GetLocationInvestigationSerial()
		|| (!bNoiseInvestigationActive && !bInvestigatingObservedLocation) || IsLocationSearchActive()
		|| PreviousLocation.ContainsNaN() || CurrentLocation.ContainsNaN()
		|| !FMath::IsFinite(AcceptanceRadius) || AcceptanceRadius < 0.0f)
	{
		return;
	}
	NoiseInvestigationAcceptanceRadius = AcceptanceRadius;
	if (!IsNoiseDirectionConfirmationActive() || !NoiseDirectionConfirmation.bStartedWhileMoving
		|| NoiseDirectionConfirmation.Clue.bNearDirectionReaction)
	{
		return;
	}
	// 한 프레임에 출처를 지나쳐도 실제 이동 선분과의 거리로 잡는다. 경로 방향 변화만으로 통과를 추정하지 않는다.
	const FVector Point = NoiseDirectionConfirmation.Clue.Location;
	const double Distance = FMath::PointDistToSegment(Point, PreviousLocation, CurrentLocation);
	if (Distance <= AcceptanceRadius + UE_DOUBLE_SMALL_NUMBER)
	{
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	}
}

bool AEnemyBase::IsFailedNoiseLocation(const FVector& Location) const
{
	if (FailedNoiseInvestigationClue.Serial == 0)
	{
		return false;
	}
	const float Margin = FMath::IsFinite(NoiseDestinationChangeDistance) ? FMath::Max(0.0f, NoiseDestinationChangeDistance) : 100.0f;
	return Location.Equals(FailedNoiseInvestigationClue.Location, UE_SMALL_NUMBER)
		|| FVector::DistSquared(Location, FailedNoiseInvestigationClue.Location) < FMath::Square(Margin);
}

bool AEnemyBase::RecordNoiseInvestigationFailure(uint64 InvestigationSerial, const FVector& Destination, const FBox& SearchBounds)
{
	if (!IsNoiseInvestigationCurrent(InvestigationSerial) || Destination.ContainsNaN()
		|| !Destination.Equals(NoiseInvestigationClue.Location, 1.0) || !SearchBounds.IsValid
		|| SearchBounds.Min.ContainsNaN() || SearchBounds.Max.ContainsNaN())
	{
		return false;
	}
	ClearNoiseInvestigationFailure();
	FailedNoiseInvestigationClue = NoiseInvestigationClue;
	FailedNoiseInvestigationBounds = SearchBounds;
	NoiseInvestigationFailedAt = GetWorld()->GetTimeSeconds();
	// 실패 기록은 현재 조사 한 건만 소유한다. 다른 목표 채택/감지 정리 때 구독도 함께 반납한다.
	NoiseFailureRooms = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	if (URoomCombatSubsystem* Rooms = NoiseFailureRooms.Get())
	{
		NoiseFailureObstacleHandle = Rooms->OnMovementObstacleOpened.AddUObject(this, &AEnemyBase::HandleNoiseInvestigationObstacleOpened);
	}
	return true;
}

void AEnemyBase::HandleNoiseInvestigationObstacleOpened(const FBox& Bounds)
{
	if (!HasAuthority() || !CanStorePerceptionClue() || FailedNoiseInvestigationClue.Serial == 0
		|| !FailedNoiseInvestigationBounds.Intersect(Bounds))
	{
		return;
	}
	// 문 열림은 재평가 자격만 준다. 실제 새 사건과 유지/거리 조건을 통과해야 이동으로 인계한다.
	bNoiseFailureRetryAllowed = true;
	RefreshPerceptionGoal();
}

void AEnemyBase::ClearNoiseInvestigationFailure()
{
	if (URoomCombatSubsystem* Rooms = NoiseFailureRooms.Get())
	{
		Rooms->OnMovementObstacleOpened.Remove(NoiseFailureObstacleHandle);
	}
	NoiseFailureObstacleHandle.Reset();
	NoiseFailureRooms.Reset();
	FailedNoiseInvestigationClue = FEnemyHeardNoise();
	FailedNoiseInvestigationBounds = FBox(ForceInit);
	NoiseInvestigationFailedAt = -1.0;
	bNoiseFailureRetryAllowed = false;
}

void AEnemyBase::NotifyNoiseInvestigationNaturalBoundary(uint64 InvestigationSerial, bool bStationary)
{
	if (!HasAuthority() || InvestigationSerial != NoiseInvestigationSerial
		|| DeferredNoiseInvestigationSerial != InvestigationSerial || !IsHeardNoiseValid(DeferredNoiseDirectionClue)
		|| !CanRunNoiseDirectionConfirmation())
	{
		return;
	}
	const FVector Direction = DeferredNoiseDirectionClue.Location - GetActorLocation();
	const float Limit = FMath::IsFinite(NoiseDirectionMovingHalfAngle) ? FMath::Clamp(NoiseDirectionMovingHalfAngle, 0.0f, 180.0f) : 60.0f;
	if (!bStationary && (NoiseInvestigationTravelDirection.IsNearlyZero()
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(NoiseInvestigationTravelDirection.Rotation().Yaw, Direction.Rotation().Yaw)) > Limit))
	{
		return;
	}
	// 경로가 자연스럽게 꺾이거나 이동이 끝난 때만 남은 확인을 한 번 허용한다.
	bDeferredNoiseDirectionReady = true;
	bDeferredNoiseDirectionStationary = bStationary;
	bNoiseDirectionEvaluationPending = true;
}

bool AEnemyBase::UpdateNoiseDirectionFacing(float DeltaTime)
{
	if (!HasAuthority() || !GetWorld() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return false;
	}
	if (DeferredNoiseDirectionClue.Serial != 0 && (DeferredNoiseInvestigationSerial != GetLocationInvestigationSerial()
		|| !IsHeardNoiseValid(DeferredNoiseDirectionClue) || !CanRunNoiseDirectionConfirmation()))
	{
		DeferredNoiseDirectionClue = FEnemyHeardNoise();
		bDeferredNoiseDirectionReady = false;
	}
	if (!IsNoiseDirectionConfirmationActive())
	{
		if (!bNoiseDirectionEvaluationPending)
		{
			return false;
		}
		FEnemyHeardNoise NearCandidate;
		if (!TryGetNearDirectionConfirmationNoise(NearCandidate))
		{
			if (!CanRunNoiseDirectionConfirmation()) return false;
			const float Interval = FMath::IsFinite(NoiseDirectionConfirmationInterval)
				? FMath::Max(0.0f, NoiseDirectionConfirmationInterval) : 1.0f;
			if (NoiseDirectionConfirmation.StartedAt >= 0.0
				&& GetWorld()->GetTimeSeconds() < NoiseDirectionConfirmation.StartedAt + Interval) return false;
			bNoiseDirectionEvaluationPending = false;
		}
		// 근거리 후보는 일반 간격과 채택 gate를 우회한다. 상위 행동 차단 중에는 pending을 유지한다.
		if (!TryBeginNoiseDirectionConfirmation())
		{
			return false;
		}
		bNoiseDirectionEvaluationPending = false;
	}
	if (!CanRunNoiseDirectionConfirmation() || !IsHeardNoiseValid(NoiseDirectionConfirmation.Clue))
	{
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
		return false;
	}
	AEnemyAIController* AI = CastChecked<AEnemyAIController>(GetController());
	const FVector Direction = NoiseDirectionConfirmation.Clue.Location - GetActorLocation();
	if (!NoiseDirectionConfirmation.Clue.bNearDirectionReaction && NoiseDirectionConfirmation.bStartedWhileMoving
		&& Direction.SizeSquared() <= FMath::Square(NoiseInvestigationAcceptanceRadius))
	{
		// Controller가 비행 Task보다 먼저 갱신돼도 이미 가까운 단서를 한 프레임 돌아보지 않는다.
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
		return false;
	}
	if (Direction.IsNearlyZero())
	{
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
		return false;
	}
	const FRotator Current = AI->GetControlRotation().GetNormalized();
	FRotator Desired = Direction.Rotation();
	NoiseDirectionConfirmation.bLimitedByMovingAngle = false;
	Desired.Pitch = FMath::Clamp(Desired.Pitch, -89.0, 89.0);
	Desired.Roll = 0.0;
	if (Direction.SizeSquared2D() <= UE_SMALL_NUMBER)
	{
		Desired.Yaw = Current.Yaw;
	}
	else if (!NoiseDirectionConfirmation.Clue.bNearDirectionReaction
		&& NoiseDirectionConfirmation.bStartedWhileMoving && !NoiseInvestigationTravelDirection.IsNearlyZero())
	{
		const double TravelYaw = NoiseInvestigationTravelDirection.Rotation().Yaw;
		double Delta = FMath::FindDeltaAngleDegrees(TravelYaw, Desired.Yaw);
		if (FMath::Abs(Delta) >= 179.9)
		{
			Delta = 180.0 * NoiseDirectionTurnSign;
		}
		const float Limit = FMath::IsFinite(NoiseDirectionMovingHalfAngle) ? FMath::Clamp(NoiseDirectionMovingHalfAngle, 0.0f, 180.0f) : 60.0f;
		NoiseDirectionConfirmation.bLimitedByMovingAngle = FMath::Abs(Delta) > Limit;
		Desired.Yaw = TravelYaw + FMath::Clamp(Delta, -static_cast<double>(Limit), static_cast<double>(Limit));
	}
	double YawError = FMath::FindDeltaAngleDegrees(Current.Yaw, Desired.Yaw);
	if (FMath::Abs(YawError) >= 179.9)
	{
		YawError = 180.0 * NoiseDirectionTurnSign;
	}
	else if (FMath::Abs(YawError) > 1.0)
	{
		NoiseDirectionTurnSign = FMath::Sign(YawError);
	}
	Desired.Yaw = Current.Yaw + YawError;
	const double Error = FMath::Max(FMath::Abs(YawError), FMath::Abs(Desired.Pitch - Current.Pitch));
	// 기존 수색과 같은 목표 근처 감속이다. 시간 내 도달을 위해 회전 속도를 강제로 높이지 않는다.
	const float Speed = FMath::Min(NoiseDirectionRotationSpeed, static_cast<float>(FMath::Max(2.0, Error * 6.0)));
	FRotator Next = FMath::RInterpConstantTo(Current, Desired, DeltaTime, Speed).GetNormalized();
	Next.Roll = 0.0;
	AI->SetControlRotation(Next);
	SetActorRotation(FRotator(0.0, Next.Yaw, 0.0));
	const double Remaining = FMath::Max(FMath::Abs(FMath::FindDeltaAngleDegrees(Next.Yaw, Desired.Yaw)), FMath::Abs(Next.Pitch - Desired.Pitch));
	if (Remaining <= 1.0 && NoiseDirectionConfirmation.Phase == EEnemyNoiseConfirmationPhase::Rotating)
	{
		NoiseDirectionConfirmation.bReachedSourceDirection = !NoiseDirectionConfirmation.bLimitedByMovingAngle;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(Desired.Yaw, Direction.Rotation().Yaw)) <= 1.0)
		{
			// 실제 출처까지 확인했다면 경유점에서 같은 사건을 다시 실행하지 않는다.
			if (DeferredNoiseDirectionClue.Serial == NoiseDirectionConfirmation.Clue.Serial)
			{
				DeferredNoiseDirectionClue = FEnemyHeardNoise();
				bDeferredNoiseDirectionReady = false;
			}
		}
		NotifyNoiseDirectionReached(NoiseDirectionConfirmation.Serial);
	}
	// 도달 직후 확인이 끝나도 이번 프레임의 회전을 기본 Focus가 덮지 않게 한다.
	return true;
}

bool AEnemyBase::RecordInvestigationSight(AActor* Target, const FVector& Location, double ObservedAt)
{
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (!HasAuthority() || !GetWorld() || !AI || !CanUseEnemyPerception() || IsDead()
		|| CombatState == EEnemyCombatState::Stun || !IsValid(Target) || Target->GetWorld() != GetWorld()
		|| !AI->IsValidTrackingTarget(Target) || Location.ContainsNaN() || !FMath::IsFinite(ObservedAt)
		|| ObservedAt < 0.0 || ObservedAt > GetWorld()->GetTimeSeconds()
		|| ObservedAt < VisualInvestigation.LastObservedAt)
	{
		return false;
	}
	// 일반 전투의 추적 정보를 새 조사로 바꾸지 않는다. 조사 중 채택한 시각 정보만 별도로 보존한다.
	if (!bNoiseInvestigationActive && VisualInvestigation.AdoptedAt < 0.0
		&& !HasHitResponse() && !bObservedInvestigationPending)
	{
		return false;
	}
	EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	const float MergeDistance = FMath::IsFinite(NoiseDestinationChangeDistance)
		? FMath::Max(0.0f, NoiseDestinationChangeDistance) : 100.0f;
	const bool bSameGoal = VisualInvestigation.AdoptedAt >= 0.0 && VisualInvestigation.Target.Get() == Target
		&& (Location.Equals(VisualInvestigation.Location, UE_SMALL_NUMBER)
			|| FVector::DistSquared(Location, VisualInvestigation.Location) < FMath::Square(MergeDistance));
	VisualInvestigation.Target = Target;
	VisualInvestigation.LastObservedAt = ObservedAt;
	LastPerceptionObservedAt = FMath::Max(LastPerceptionObservedAt, ObservedAt);
	if (!bSameGoal)
	{
		++VisualInvestigationRevision;
		VisualInvestigation.Location = Location;
		VisualInvestigation.AdoptedAt = ObservedAt;
	}
	return true;
}

bool AEnemyBase::IsVisualInvestigationHoldActive() const
{
	const float Hold = FMath::IsFinite(VisualInvestigationHoldDuration)
		? FMath::Max(0.0f, VisualInvestigationHoldDuration) : 1.0f;
	return HasAuthority() && GetWorld() && VisualInvestigation.AdoptedAt >= 0.0
		&& GetWorld()->GetTimeSeconds() < VisualInvestigation.AdoptedAt + Hold;
}

bool AEnemyBase::IsHeardNoiseValid(const FEnemyHeardNoise& Noise) const
{
	const UWorld* World = GetWorld();
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (!HasAuthority() || !World || !AI || !CanUseEnemyPerception() || !UsesHearingPerception()
		|| IsDead() || Noise.OccurredAt < 0.0 || Noise.OccurredAt <= ConsumedNoiseAt
		|| !FMath::IsFinite(HeardNoiseLifetime) || HeardNoiseLifetime <= 0.0f
		|| World->GetTimeSeconds() - Noise.OccurredAt >= HeardNoiseLifetime
		|| !AI->IsValidTrackingTarget(Noise.Source.Get()))
	{
		return false;
	}
	return true;
}

bool AEnemyBase::CanReplaceNoiseDestination(const FEnemyHeardNoise& Noise) const
{
	if (!IsHeardNoiseValid(Noise) || IsVisualInvestigationHoldActive())
	{
		return false;
	}
	const bool bFailedLocation = IsFailedNoiseLocation(Noise.Location);
	const bool bRetryFailedLocation = bFailedLocation && bNoiseFailureRetryAllowed
		&& Noise.Serial != FailedNoiseInvestigationClue.Serial && Noise.OccurredAt > NoiseInvestigationFailedAt;
	if (bFailedLocation && !bRetryFailedLocation)
	{
		return false;
	}
	// 이미 그 자리에 서서 수색 중이면 새 이동/즉시 도착 회차를 만들지 않는다. 청각 인지와 방향 후보는 남긴다.
	if (IsLocationSearchActive() && FVector::DistSquared(GetActorLocation(), Noise.Location)
		<= FMath::Square(NoiseInvestigationAcceptanceRadius))
	{
		return false;
	}
	const bool bHasVisualGoal = VisualInvestigation.AdoptedAt >= 0.0;
	if (!bNoiseInvestigationActive && !bHasVisualGoal)
	{
		return true;
	}
	const FVector CurrentLocation = bNoiseInvestigationActive ? NoiseInvestigationLocation : VisualInvestigation.Location;
	const double CurrentInformationAt = bNoiseInvestigationActive ? NoiseInvestigationClue.OccurredAt : VisualInvestigation.LastObservedAt;
	if (Noise.OccurredAt <= CurrentInformationAt)
	{
		return false;
	}
	const float Interval = FMath::IsFinite(NoiseDestinationUpdateInterval) ? FMath::Max(NoiseDestinationUpdateInterval, 0.0f) : 1.0f;
	const float CoordinateMargin = FMath::IsFinite(NoiseDestinationChangeDistance) ? FMath::Max(NoiseDestinationChangeDistance, 0.0f) : 100.0f;
	if ((bNoiseInvestigationActive && GetWorld()->GetTimeSeconds() - LastNoiseDestinationChangedAt < Interval)
		|| (!bRetryFailedLocation && (Noise.Location.Equals(CurrentLocation, UE_SMALL_NUMBER)
			|| FVector::DistSquared(Noise.Location, CurrentLocation) < FMath::Square(CoordinateMargin))))
	{
		return false;
	}
	// 도착 후에는 기존 지점이 가장 가까워진다. 수색 단계까지 거리 이득을 요구하면 새 단서로 떠날 수 없다.
	if (IsNoiseSearchActive() || (bInvestigatingObservedLocation && HitResponsePhase == EHitResponsePhase::Search))
	{
		return true;
	}
	const double CurrentDistance = FVector::Distance(GetActorLocation(), CurrentLocation);
	const double CandidateDistance = FVector::Distance(GetActorLocation(), Noise.Location);
	const float Margin = FMath::IsFinite(NoiseCandidateSwitchDistance) ? FMath::Max(NoiseCandidateSwitchDistance, 0.0f) : 200.0f;
	const float EqualTolerance = FMath::IsFinite(NoiseEqualDistanceTolerance) ? FMath::Max(NoiseEqualDistanceTolerance, 0.0f) : 10.0f;
	return FMath::Abs(CurrentDistance - CandidateDistance) <= EqualTolerance
		|| (CandidateDistance < CurrentDistance && CurrentDistance - CandidateDistance >= Margin);
}

bool AEnemyBase::TryGetNoiseGoalClue(FEnemyHeardNoise& OutClue) const
{
	OutClue = FEnemyHeardNoise();
	if (PerceptionGoal.Type != EEnemyPerceptionGoalType::InvestigateNoise || PerceptionGoal.NoiseClueSerial == 0)
	{
		return false;
	}
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (Candidate.Serial == PerceptionGoal.NoiseClueSerial && IsHeardNoiseValid(Candidate))
		{
			OutClue = Candidate;
			return true;
		}
	}
	return false;
}

bool AEnemyBase::SelectHeardNoise(FEnemyHeardNoise& OutNoise, bool bRequireNewObservation) const
{
	OutNoise = FEnemyHeardNoise();
	const FEnemyHeardNoise* Best = nullptr;
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (!IsHeardNoiseValid(Candidate) || IsVisualInvestigationHoldActive()
			|| (VisualInvestigation.AdoptedAt >= 0.0 && Candidate.OccurredAt <= VisualInvestigation.LastObservedAt)
			|| (bRequireNewObservation && Candidate.OccurredAt <= LastPerceptionObservedAt)
			|| !CanReplaceNoiseDestination(Candidate))
		{
			continue;
		}
		const double Distance = FVector::Distance(GetActorLocation(), Candidate.Location);
		const double BestDistance = Best ? FVector::Distance(GetActorLocation(), Best->Location) : 0.0;
		const bool bSameDistance = Best && FMath::IsNearlyEqual(Distance, BestDistance, UE_SMALL_NUMBER);
		// 거리·시각까지 같으면 기존 선택을 남겨 후보 배열 정리 순서에 영향받지 않는다.
		const bool bKeepSelection = bSameDistance && Candidate.OccurredAt == Best->OccurredAt
			&& Candidate.Location == PerceptionGoal.Location && Candidate.OccurredAt == PerceptionGoal.InformationAt;
		if (!Best || (!bSameDistance && Distance < BestDistance)
			|| (bSameDistance && Candidate.OccurredAt > Best->OccurredAt) || bKeepSelection)
		{
			Best = &Candidate;
		}
	}
	if (!Best)
	{
		return false;
	}
	OutNoise = *Best;
	return true;
}

bool AEnemyBase::RecordAlertClue(double OccurredAt)
{
	if (!CanStorePerceptionClue() || !FMath::IsFinite(OccurredAt) || OccurredAt < 0.0
		|| OccurredAt > GetWorld()->GetTimeSeconds() || OccurredAt <= LastAlertClueAt)
	{
		return false;
	}
	// 수신·재평가 시각이 아니라 원래 발생 시각으로 비교해 중복·역순 입력의 기한 연장을 막는다.
	LastAlertClueAt = OccurredAt;
	return true;
}

bool AEnemyBase::IsAlertHoldActive() const
{
	if (!CanStorePerceptionClue() || !HasAlertClue())
	{
		return false;
	}
	const float Duration = FMath::IsFinite(RuntimeStat.MinAlertHoldAfterClue) && RuntimeStat.MinAlertHoldAfterClue > 0.0f
		? RuntimeStat.MinAlertHoldAfterClue : 4.0f;
	return GetWorld()->GetTimeSeconds() < LastAlertClueAt + Duration;
}

void AEnemyBase::ClearAlertClue()
{
	if (HasAuthority())
	{
		LastAlertClueAt = -1.0;
	}
}

bool AEnemyBase::CanStorePerceptionClue() const
{
	// 스턴·일시적인 감지 억제는 지연 단서를 버릴 이유가 아니다. 생명·제어권·대여 상태만 검사한다.
	return HasAuthority() && GetWorld() && !IsDead() && !bDeathCleanupPerformed && !bIsPossessed
		&& (!IsPoolManaged() || PoolState == EEnemyPoolState::CombatActive);
}

bool AEnemyBase::IsHitClueValid(const FEnemyHitClue& Clue) const
{
	const UWorld* World = GetWorld();
	return CanStorePerceptionClue() && Clue.Receiver.Get() == this && Clue.Serial != 0
		&& !Clue.Location.ContainsNaN() && FMath::IsFinite(Clue.OccurredAt) && FMath::IsFinite(Clue.ExpiresAt)
		&& Clue.OccurredAt >= 0.0 && Clue.OccurredAt <= World->GetTimeSeconds()
		&& Clue.ExpiresAt > Clue.OccurredAt && World->GetTimeSeconds() < Clue.ExpiresAt;
}

bool AEnemyBase::CreateHitClue(const FVector& Location, double OccurredAt, double LifetimeSeconds, FEnemyHitClue& OutClue)
{
	OutClue = FEnemyHitClue();
	if (!CanStorePerceptionClue() || Location.ContainsNaN() || !FMath::IsFinite(OccurredAt)
		|| OccurredAt < 0.0 || OccurredAt > GetWorld()->GetTimeSeconds()
		|| !FMath::IsFinite(LifetimeSeconds) || LifetimeSeconds <= 0.0
		|| NextHitClueSerial == MAX_uint64)
	{
		return false;
	}
	const double ExpiresAt = OccurredAt + LifetimeSeconds;
	if (!FMath::IsFinite(ExpiresAt) || ExpiresAt <= GetWorld()->GetTimeSeconds())
	{
		return false;
	}
	OutClue.Receiver = this;
	OutClue.Location = Location;
	OutClue.OccurredAt = OccurredAt;
	OutClue.ExpiresAt = ExpiresAt;
	OutClue.Serial = ++NextHitClueSerial;
	return true;
}

bool AEnemyBase::RecordHitClue(const FEnemyHitClue& Clue)
{
	if (!IsHitClueValid(Clue) || Clue.Serial > NextHitClueSerial
		|| Clue.Serial <= LastRecordedHitClueSerial || Clue.OccurredAt < LastRecordedHitClueAt)
	{
		return false;
	}
	EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	// 재조회·중복 수신은 수명을 갱신하지 않는다. 같은 프레임의 별개 명중은 사건 번호로 구분한다.
	PendingHitClue = Clue;
	LastRecordedHitClueSerial = Clue.Serial;
	LastRecordedHitClueAt = Clue.OccurredAt;
	RecordAlertClue(Clue.OccurredAt);
	return true;
}

bool AEnemyBase::TryGetPendingHitClue(FEnemyHitClue& OutClue) const
{
	OutClue = FEnemyHitClue();
	if (!IsHitClueValid(PendingHitClue))
	{
		return false;
	}
	OutClue = PendingHitClue;
	return true;
}

bool AEnemyBase::BeginHitInvestigation(uint64 ClueSerial, FEnemyHitClue& OutClue)
{
	OutClue = FEnemyHitClue();
	if (CombatState == EEnemyCombatState::Stun || !CanUseEnemyPerception()
		|| PendingHitClue.Serial != ClueSerial || !IsHitClueValid(PendingHitClue))
	{
		return false;
	}
	ClearNoiseInvestigationFailure();
	// 시작 시 위치를 고정하고 대기 후보에서 제거한다. 이후 만료는 실행 중 조사를 취소하지 않는다.
	// 새 단서로 교체되면 이전 사건 번호의 완료 콜백은 새 조사에 영향을 줄 수 없다.
	ActiveHitClue = PendingHitClue;
	PendingHitClue = FEnemyHitClue();
	OutClue = ActiveHitClue;
	return true;
}

bool AEnemyBase::IsHitInvestigationCurrent(uint64 ClueSerial) const
{
	// 회차 소유 여부만 판정한다. 실제 회전·이동의 스턴/상위 행동 허용 여부는 행동 쪽에서 검사한다.
	return CanStorePerceptionClue() && ClueSerial != 0 && ActiveHitClue.Serial == ClueSerial;
}

bool AEnemyBase::EndHitInvestigation(uint64 ClueSerial)
{
	if (!IsHitInvestigationCurrent(ClueSerial))
	{
		return false;
	}
	if (HasHitResponse())
	{
		FinishHitResponse();
	}
	ActiveHitClue = FEnemyHitClue();
	return true;
}

bool AEnemyBase::ConsumeHitClue(uint64 ClueSerial)
{
	if (!HasAuthority() || ClueSerial == 0 || PendingHitClue.Serial != ClueSerial)
	{
		return false;
	}
	// 직접 시야 전투 등으로 채택하지 않은 사건도 소비한다. 실행 중인 다른 회차는 건드리지 않는다.
	PendingHitClue = FEnemyHitClue();
	return true;
}

void AEnemyBase::ClearHitClues()
{
	if (!HasAuthority())
	{
		return;
	}
	if (HasHitResponse())
	{
		FinishHitResponse();
	}
	HitConfirmationStartedAt = HitResponseLastEndedAt = -1.0;
	bObservedInvestigationPending = bInvestigatingObservedLocation = false;
	ObservedInvestigationAt = -1.0;
	PendingHitClue = FEnemyHitClue();
	ActiveHitClue = FEnemyHitClue();
	// 번호를 되돌리지 않아 생성만 된 옛 단서와 지연 콜백도 새 생명/팀/대여에 재진입하지 못한다.
	LastRecordedHitClueSerial = NextHitClueSerial;
	LastRecordedHitClueAt = -1.0;
}

bool AEnemyBase::IsHitResponseCommitted() const
{
	// 레이저는 Task가 차징~후딜의 AttackPhase를 소유한다. 일반 연사까지 커밋으로 확대하지 않는다.
	const AAutoTurret* Turret = Cast<AAutoTurret>(this);
	return IsPossessedActionCommitted() || (Turret && Cast<ALaserWeapon>(CurrentWeapon)
		&& GetAttackPhase() != EEnemyAttackPhase::Idle);
}

void AEnemyBase::FinishHitResponse()
{
	if (!HasHitResponse())
	{
		return;
	}
	bResumeTargetAttackPreparation = PreparedAttackTarget.IsValid() && PreparedAttackTarget.Get() == GetTrackedTarget()
		&& PreparedAttackWeapon.Get() == CurrentWeapon && PreparedAttackController.Get() == GetController();
	HitResponsePhase = EHitResponsePhase::None;
	ActiveHitClue = FEnemyHitClue();
	bHitReturnDirectionOnly = false;
	bHitMovementAdopted = false;
	bInvestigatingObservedLocation = false;
	HitResponseLastEndedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	++NoiseInvestigationSerial;
	EndSearchEpisode();
	PerceptionGoal = FEnemyPerceptionGoal();
}

void AEnemyBase::OnDirectSightForHitResponse(const FVector& ObservedLocation)
{
	if (!HasAuthority() || ObservedLocation.ContainsNaN())
	{
		return;
	}
	ClearNoiseInvestigationFailure();
	EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	// 같은 시각 조사의 재관측은 정보만 갱신한다. 경로/수색 회차와 채택 시계는 다시 만들지 않는다.
	if (CombatState != EEnemyCombatState::Combat && !bNoiseInvestigationActive && (bInvestigatingObservedLocation || bObservedInvestigationPending)
		&& VisualInvestigationRevision != 0 && VisualInvestigationRevision == ObservedInvestigationRevision)
	{
		PendingHitClue = FEnemyHitClue();
		if (bObservedInvestigationPending)
		{
			ObservedInvestigationAt = GetWorld()->GetTimeSeconds();
		}
		LastRecordedHitClueSerial = NextHitClueSerial;
		LastRecordedHitClueAt = FMath::Max(LastRecordedHitClueAt, GetWorld()->GetTimeSeconds());
		RefreshPerceptionGoal();
		return;
	}
	// 실제 관측이 먼저 소유권을 회수한다. 옛 비행/수색의 완료 콜백은 회차 번호가 달라 거절된다.
	const bool bWasInvestigating = HasHitResponse() || bNoiseInvestigationActive;
	if (bNoiseInvestigationActive)
	{
		EndNoiseInvestigation();
	}
	FinishHitResponse();
	PendingHitClue = FEnemyHitClue();
	// 관측 전에 발급만 된 사건도 뒤늦게 옛 조사로 복원되지 않게 소비 경계를 올린다.
	LastRecordedHitClueSerial = NextHitClueSerial;
	LastRecordedHitClueAt = FMath::Max(LastRecordedHitClueAt, GetWorld()->GetTimeSeconds());
	if ((bWasInvestigating || bObservedInvestigationPending) && CombatState != EEnemyCombatState::Combat)
	{
		// 인지 확정 전의 짧은 관측도 옛 피격 조사를 대체한다. 다시 숨으면 이 관측 위치만 조사한다.
		bObservedInvestigationPending = true;
		ObservedInvestigationRevision = VisualInvestigationRevision;
		ObservedInvestigationLocation = ObservedLocation;
		ObservedInvestigationAt = GetWorld()->GetTimeSeconds();
		UpdateLastKnownPlayerLocation(ObservedLocation);
	}
	else if (CombatState == EEnemyCombatState::Combat)
	{
		bObservedInvestigationPending = false;
	}
	RefreshPerceptionGoal();
}

void AEnemyBase::TickHitResponse(float DeltaTime)
{
	if (!HasAuthority() || !GetWorld() || bProcessingDamageHitResponse)
	{
		return;
	}
	if (PendingHitClue.Serial != 0 && !IsHitClueValid(PendingHitClue))
	{
		PendingHitClue = FEnemyHitClue();
	}
	if (!HasHitResponse() && PendingHitClue.Serial == 0 && !bObservedInvestigationPending)
	{
		return;
	}
	AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (!CanStorePerceptionClue() || !AI)
	{
		return;
	}
	if (CombatState == EEnemyCombatState::Stun || HasActiveStunTag() || !CanUseEnemyPerception())
	{
		// 기절 중에는 대기 단서를 소비하지 않는다. 실제 복귀 시점의 시야로 선택해야 한다.
		FinishHitResponse();
		return;
	}
	AActor* Visible = AI->GetCachedVisibleTarget();
	if (AI->IsValidTrackingTarget(Visible) && AI->IsTargetDirectlyVisible(Visible))
	{
		if (HasHitResponse() || PendingHitClue.Serial != 0 || bObservedInvestigationPending)
		{
			// 연속 직접 관측도 정보 시각만 갱신한다. 시야를 오래 유지한 뒤 놓쳐도 방금 본 위치가 만료되지 않는다.
			RecordInvestigationSight(Visible, Visible->GetActorLocation(), GetWorld()->GetTimeSeconds());
			OnDirectSightForHitResponse(Visible->GetActorLocation());
		}
		return;
	}
	if (IsHitResponseCommitted())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const auto Positive = [](float Value, float Fallback) { return FMath::IsFinite(Value) && Value > 0.0f ? Value : Fallback; };
	const float MinConfirm = Positive(RuntimeStat.HitDirectionMinDuration, 0.4f);
	if (bObservedInvestigationPending)
	{
		bObservedInvestigationPending = false;
		// 관측 완료로 이미 전투 추적이 생겼다면 기존 전투가 이어진다. 부분 관측만 별도 위치 조사로 인계한다.
		if (CombatState != EEnemyCombatState::Combat && !HasHitResponse() && PendingHitClue.Serial == 0)
		{
			FEnemyHitClue Observed;
			if (CreateHitClue(ObservedInvestigationLocation, ObservedInvestigationAt,
				Positive(RuntimeStat.HitClueLifetime, 6.0f), Observed))
			{
				PendingHitClue = Observed;
				bInvestigatingObservedLocation = true;
			}
		}
	}
	FEnemyHitClue Pending;
	if (TryGetPendingHitClue(Pending))
	{
		const bool bReturningCooldown = CombatState == EEnemyCombatState::Returning
			&& HitResponseLastEndedAt >= 0.0 && Now - HitResponseLastEndedAt < Positive(RuntimeStat.HitReturnReactionInterval, 2.0f);
		if (bReturningCooldown)
		{
			ConsumeHitClue(Pending.Serial);
		}
		else if (!HasHitResponse() || Now - HitConfirmationStartedAt >= MinConfirm)
		{
			const FVector OldDirection = (ActiveHitClue.Location - GetActorLocation()).GetSafeNormal();
			const FVector NewDirection = (Pending.Location - GetActorLocation()).GetSafeNormal();
			const float MergeAngle = FMath::Clamp(Positive(RuntimeStat.HitDirectionMergeAngle, 20.0f), 0.1f, 180.0f);
			const bool bSameDirection = HasHitResponse() && !OldDirection.IsNearlyZero() && !NewDirection.IsNearlyZero()
				&& FVector::DotProduct(OldDirection, NewDirection) >= FMath::Cos(FMath::DegreesToRadians(MergeAngle));
			if (bSameDirection)
			{
				ConsumeHitClue(Pending.Serial);
			}
			else
			{
				const bool bObservedLocation = bInvestigatingObservedLocation && !HasHitResponse();
				FinishHitResponse();
				bInvestigatingObservedLocation = bObservedLocation;
				if (bNoiseInvestigationActive)
				{
					EndNoiseInvestigation(false);
				}
				if (BeginHitInvestigation(Pending.Serial, Pending))
				{
					HitResponsePhase = EHitResponsePhase::Confirm;
					HitConfirmationStartedAt = Now;
					bHitReturnDirectionOnly = CombatState == EEnemyCombatState::Returning;
					++NoiseInvestigationSerial;
					PauseTargetAttack();
					if (!bHitReturnDirectionOnly)
					{
						AI->StopMovement();
						if (UCharacterMovementComponent* Movement = GetCharacterMovement())
						{
							Movement->StopMovementImmediately();
						}
						if (CombatState == EEnemyCombatState::NonCombat)
						{
							EnterAlertFromPerception(ActiveHitClue.Location);
						}
					}
					RefreshPerceptionGoal();
					SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
				}
			}
		}
	}
	if (!IsHitDirectionConfirmationActive())
	{
		return;
	}
	bool bReached = false;
	if (AAutoTurret* Turret = Cast<AAutoTurret>(this))
	{
		bReached = Turret->UpdateTurretAimAtLocation(ActiveHitClue.Location, DeltaTime, false);
	}
	else
	{
		const FVector Direction = ActiveHitClue.Location - GetPawnViewLocation();
		FRotator Desired = Direction.IsNearlyZero() ? AI->GetControlRotation() : Direction.Rotation();
		Desired.Roll = 0.0f;
		const FRotator Current = AI->GetControlRotation();
		const float Error = FMath::Max(FMath::Abs(FMath::FindDeltaAngleDegrees(Current.Yaw, Desired.Yaw)),
			FMath::Abs(FMath::FindDeltaAngleDegrees(Current.Pitch, Desired.Pitch)));
		const float Speed = FMath::Min(Positive(RuntimeStat.HitRotationSpeed, 240.0f), FMath::Max(20.0f, Error * 6.0f));
		const FRotator Next = FMath::RInterpConstantTo(Current, Desired, FMath::Max(0.0f, DeltaTime), Speed);
		AI->SetControlRotation(Next);
		SetActorRotation(FRotator(0.0f, Next.Yaw, 0.0f));
		bReached = Error <= 3.0f;
	}
	const double Elapsed = Now - HitConfirmationStartedAt;
	const double MaxConfirm = FMath::Max(MinConfirm, Positive(RuntimeStat.HitDirectionMaxDuration, 2.0f));
	if (Elapsed < MinConfirm || (!bReached && Elapsed < MaxConfirm))
	{
		return;
	}
	if (bHitReturnDirectionOnly || Cast<AAutoTurret>(this))
	{
		FinishHitResponse();
		RefreshPerceptionGoal();
	}
	else
	{
		HitResponsePhase = EHitResponsePhase::Move;
		RefreshPerceptionGoal();
	}
	SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
}

bool AEnemyBase::IsLocationInvestigationGoal() const
{
	return PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise
		|| PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateHit;
}

float AEnemyBase::GetLocationSearchDuration(float DefaultDuration) const
{
	return HasHitResponse() && FMath::IsFinite(RuntimeStat.HitSearchDuration) && RuntimeStat.HitSearchDuration > 0.0f
		? RuntimeStat.HitSearchDuration : DefaultDuration;
}

bool AEnemyBase::IsLocationInvestigationActive() const { return HasHitResponse() || IsNoiseInvestigationActive(); }
bool AEnemyBase::IsLocationSearchActive() const { return HitResponsePhase == EHitResponsePhase::Search || IsNoiseSearchActive(); }

bool AEnemyBase::CanRunHitInvestigationMovement() const
{
	return HasHitResponse() && !bHitReturnDirectionOnly && !Cast<AAutoTurret>(this);
}

bool AEnemyBase::IsLocationInvestigationAdopted() const
{
	return IsNoiseInvestigationActive() || (CanRunHitInvestigationMovement() && bHitMovementAdopted);
}

bool AEnemyBase::BeginLocationInvestigation(FVector& OutDestination)
{
	if (!HasHitResponse())
	{
		return BeginNoiseInvestigation(OutDestination);
	}
	if (!CanRunHitInvestigationMovement() || !IsLocationInvestigationCurrent(NoiseInvestigationSerial))
	{
		return false;
	}
	bHitMovementAdopted = true;
	OutDestination = ActiveHitClue.Location;
	return true;
}

bool AEnemyBase::TryUpdateLocationInvestigationDestination(FVector& OutDestination)
{
	if (!HasHitResponse())
	{
		return TryUpdateNoiseInvestigationDestination(OutDestination);
	}
	OutDestination = ActiveHitClue.Location;
	return IsLocationInvestigationCurrent(NoiseInvestigationSerial);
}

bool AEnemyBase::IsLocationInvestigationCurrent(uint64 Serial) const
{
	if (!HasHitResponse())
	{
		return IsNoiseInvestigationCurrent(Serial);
	}
	return Serial == NoiseInvestigationSerial && CanStorePerceptionClue() && CanUseEnemyPerception()
		&& CombatState != EEnemyCombatState::Stun && !IsHitResponseCommitted()
		&& HitResponsePhase != EHitResponsePhase::Search;
}

bool AEnemyBase::BeginLocationInvestigationSearch(uint64 Serial, const FVector& ReachableCenter)
{
	if (!HasHitResponse())
	{
		return BeginNoiseInvestigationSearch(Serial, ReachableCenter);
	}
	if (!IsLocationInvestigationCurrent(Serial) || IsHitDirectionConfirmationActive()
		|| ReachableCenter.ContainsNaN() || !ReachableCenter.Equals(GetActorLocation(), 1.0))
	{
		return false;
	}
	HitSearchCenter = ReachableCenter;
	HitResponsePhase = EHitResponsePhase::Search;
	RefreshPerceptionGoal();
	if (!BeginSearchEpisode())
	{
		return false;
	}
	NotifyNoiseInvestigationNaturalBoundary(Serial, true);
	return true;
}

void AEnemyBase::EndLocationInvestigation()
{
	if (!HasHitResponse()) { EndNoiseInvestigation(); return; }
	FinishHitResponse();
	RefreshPerceptionGoal();
}

bool AEnemyBase::CompleteLocationInvestigation()
{
	if (!HasHitResponse())
	{
		return CompleteNoiseInvestigation();
	}
	if (!HasAuthority() || !IsLocationSearchActive() || !CanContinueSearchEpisode())
	{
		return false;
	}
	FinishHitResponse();
	RefreshTargetTracking();
	RefreshPerceptionGoal();
	const UEnemyRoomSubsystem* Rooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	if (CombatState == EEnemyCombatState::Combat && Rooms && Rooms->IsRoomInCombat(GetDefaultRoomTag()))
	{
		return true;
	}
	if (PerceptionGoal.Type != EEnemyPerceptionGoalType::Combat && !IsLocationInvestigationGoal())
	{
		CommitToNonCombatAfterSearch();
	}
	return true;
}

void AEnemyBase::ClearHeardNoise()
{
	ClearNoiseInvestigationFailure();
	ClearNoiseDirectionConfirmation();
	if (bNoiseInvestigationActive)
	{
		EndSearchEpisode();
	}
	bNoiseInvestigationActive = false;
	bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	NoiseInvestigationLocation = FVector::ZeroVector;
	NoiseInvestigationClue = FEnemyHeardNoise();
	LastNoiseDestinationChangedAt = ConsumedNoiseAt = -1.0;
	HeardNoiseCandidates.Reset();
	// 재사용·빙의·사망 경계에서는 채택한 조사 의도와 관측 시각도 함께 폐기한다.
	PerceptionGoal = FEnemyPerceptionGoal();
	LastPerceptionObservedAt = -1.0;
	VisualInvestigation = FEnemyVisualInvestigation();
	// 사건 번호는 되돌리지 않는다. 이전 대여/팀의 확인 콜백이 새 후보를 사용 처리하지 못하게 한다.
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(PerceptionGoalTimer);
	}
}

void AEnemyBase::RefreshPerceptionGoal()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	FEnemyPerceptionGoal Next;
	FEnemyHeardNoise Noise;
	bool bFreshNoise = TryGetHeardNoise(Noise);
	const double Now = GetWorld()->GetTimeSeconds();
	AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	const bool bCanDecide = AI && CanUseEnemyPerception() && !IsDead()
		&& CombatState != EEnemyCombatState::Stun;

	// 한 타이머로 후보 만료와 유지 시간 종료를 예약한다. 수신만으로 유지 시간을 다시 시작하지 않는다.
	double NextEvaluationAt = TNumericLimits<double>::Max();
	const float Hold = FMath::IsFinite(NoiseDestinationUpdateInterval) ? FMath::Max(NoiseDestinationUpdateInterval, 0.0f) : 1.0f;
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (!IsHeardNoiseValid(Candidate))
		{
			continue;
		}
		NextEvaluationAt = FMath::Min(NextEvaluationAt, Candidate.OccurredAt + HeardNoiseLifetime);
		if (IsVisualInvestigationHoldActive())
		{
			const float VisualHold = FMath::IsFinite(VisualInvestigationHoldDuration)
				? FMath::Max(0.0f, VisualInvestigationHoldDuration) : 1.0f;
			NextEvaluationAt = FMath::Min(NextEvaluationAt, VisualInvestigation.AdoptedAt + VisualHold);
		}
		// 이동하면서 거리 우위가 달라질 수 있으므로, 지금 먼 후보도 유지 종료 때 다시 비교한다.
		if (bNoiseInvestigationActive && Candidate.OccurredAt > NoiseInvestigationClue.OccurredAt && LastNoiseDestinationChangedAt + Hold > Now)
		{
			NextEvaluationAt = FMath::Min(NextEvaluationAt, LastNoiseDestinationChangedAt + Hold);
		}
	}
	if (bNoiseInvestigationActive && NoiseInvestigationClue.OccurredAt + HeardNoiseLifetime > Now)
	{
		NextEvaluationAt = FMath::Min(NextEvaluationAt, NoiseInvestigationClue.OccurredAt + HeardNoiseLifetime);
	}
	if (bCanDecide && NextEvaluationAt < TNumericLimits<double>::Max())
	{
		const float Remaining = static_cast<float>(FMath::Max(NextEvaluationAt - Now, 0.001));
		if (!GetWorldTimerManager().TimerExists(PerceptionGoalTimer)
			|| !FMath::IsNearlyEqual(GetWorldTimerManager().GetTimerRemaining(PerceptionGoalTimer), Remaining, 0.01f))
		{
			GetWorldTimerManager().SetTimer(PerceptionGoalTimer, this, &AEnemyBase::RefreshPerceptionGoal, Remaining, false);
		}
	}
	else
	{
		GetWorldTimerManager().ClearTimer(PerceptionGoalTimer);
	}

	if (bCanDecide)
	{
		AActor* Tracked = GetTrackedTarget();
		AActor* Visible = bPlayerCurrentlyVisible ? AI->GetCachedVisibleTarget() : nullptr;
		if (bPlayerCurrentlyVisible && AI->IsTargetDirectlyVisible(Tracked))
		{
			Visible = Tracked;
		}
		if (!AI->IsValidTrackingTarget(Visible) || !AI->IsTargetDirectlyVisible(Visible))
		{
			Visible = nullptr;
		}
		// 피격 조사는 추적 지식을 지우지 않고 행동 목표만 선점한다. 청각/공유 후보의 시계는 계속 흐른다.
		if (HasHitResponse() && !bHitReturnDirectionOnly && !Visible && !IsHitResponseCommitted())
		{
			// 부분 관측 위치는 실제 피격 조사와 구분한다. 유지 조건을 통과한 새 소리만 시각 조사를 대체한다.
			FEnemyHeardNoise Replacement;
			if (bInvestigatingObservedLocation && SelectHeardNoise(Replacement, true))
			{
				EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
				FinishHitResponse();
			}
		}
		if (HasHitResponse() && !bHitReturnDirectionOnly && !Visible && !IsHitResponseCommitted())
		{
			if (!bInvestigatingObservedLocation)
			{
				EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
			}
			Next.Type = HitResponsePhase == EHitResponsePhase::Search
				? EEnemyPerceptionGoalType::Search : EEnemyPerceptionGoalType::InvestigateHit;
			Next.Location = HitResponsePhase == EHitResponsePhase::Search ? HitSearchCenter : ActiveHitClue.Location;
			Next.InformationAt = ActiveHitClue.OccurredAt;
			const bool bChanged = Next.Type != PerceptionGoal.Type || Next.Location != PerceptionGoal.Location;
			PerceptionGoal = Next;
			if (bChanged)
			{
				bNoiseDirectionEvaluationPending = true;
			}
			if (bChanged)
			{
				SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
			}
			return;
		}
		if (Visible)
		{
			LastPerceptionObservedAt = Now;
		}

		FEnemyRoomTargetObservationSnapshot Shared;
		bool bLiveShared = false;
		if (!Visible && (bFreshNoise || PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise || IsNoiseSearchActive())
			&& CombatState == EEnemyCombatState::Combat && CanUseRoomTargetSharing())
		{
			if (!TrackingRooms.IsValid())
			{
				TrackingRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
			}
			if (UEnemyRoomSubsystem* Rooms = TrackingRooms.Get())
			{
				bLiveShared = Rooms->TryGetRoomTargetObservation(this, Tracked, Shared) && Shared.bDirectlyObserved;
				if (!bLiveShared)
				{
					bLiveShared = Rooms->TrySelectRoomTarget(this, Shared, true);
				}
				if (bLiveShared)
				{
					LastPerceptionObservedAt = FMath::Max(LastPerceptionObservedAt, Shared.LastObservedAt);
				}
			}
		}

		// 채택 전에는 가까운 후보를, 채택 후에는 전환 조건을 통과한 후보만 목표에 공개한다.
		// 대기 후보가 기존 이동 방향이나 공유와의 비교 위치를 먼저 바꾸지 않게 한다.
		bFreshNoise = SelectHeardNoise(Noise, !bLiveShared);
		if (bNoiseInvestigationActive && !bFreshNoise)
		{
			Noise = NoiseInvestigationClue;
			Noise.Location = NoiseInvestigationLocation;
			bFreshNoise = IsHeardNoiseValid(Noise);
		}
		const bool bInvestigating = PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise || IsNoiseSearchActive();
		bool bChooseNoise = false;
		if (!Visible && !IsPossessedActionCommitted())
		{
			if (bLiveShared && bFreshNoise)
			{
				const float Margin = FMath::IsFinite(NoiseGoalSwitchDistance) ? FMath::Max(0.0f, NoiseGoalSwitchDistance) : 200.0f;
				const double NoiseDistance = FVector::Distance(GetActorLocation(), Noise.Location);
				const double SharedDistance = FVector::Distance(GetActorLocation(), Shared.LastObservedLocation);
				// 양방향 전환에 같은 여유 거리를 적용해 경계에서 의도가 왕복하지 않게 한다.
				// Margin이 0이어도 동거리에서는 현재 의도를 유지한다.
				const bool bNoiseCloser = NoiseDistance < SharedDistance && SharedDistance - NoiseDistance >= Margin;
				const bool bSharedCloser = SharedDistance < NoiseDistance && NoiseDistance - SharedDistance >= Margin;
				bChooseNoise = bInvestigating ? !bSharedCloser : bNoiseCloser;
			}
			else if (!bLiveShared)
			{
				bChooseNoise = (bFreshNoise && Noise.OccurredAt > LastPerceptionObservedAt) || bNoiseInvestigationActive;
			}
		}

		// 전투 대상은 추적 채택 경계 하나가 결정한다. 보호 중에도 의도만 다른 대상을 가리키지 않는다.
		// 비전투 시각 조사는 기존 관측 대상을 사용하며 발견 확정이나 사격 권한을 새로 만들지 않는다.
		AActor* CombatTarget = Visible ? Visible : (bLiveShared ? Shared.TargetActor.Get() : Tracked);
		if (CombatState == EEnemyCombatState::Combat && Tracked)
		{
			CombatTarget = Tracked;
		}
		if (IsPossessedActionCommitted())
		{
			Next.Type = EEnemyPerceptionGoalType::CommittedAction;
		}
		else if (bChooseNoise)
		{
			Next = bInvestigating ? PerceptionGoal : FEnemyPerceptionGoal();
			Next.Type = EEnemyPerceptionGoalType::InvestigateNoise;
			// 유효 시간이 지난 대기 후보는 버리고, 이미 채택해 이동하던 위치만 유지한다.
			if (bNoiseInvestigationActive)
			{
				Next.Location = NoiseInvestigationLocation;
				Next.InformationAt = NoiseInvestigationClue.OccurredAt;
				Next.NoiseClueSerial = NoiseInvestigationClue.Serial;
			}
			if (bFreshNoise && (!bNoiseInvestigationActive || Noise.OccurredAt > Next.InformationAt))
			{
				Next.Location = Noise.Location;
				Next.InformationAt = Noise.OccurredAt;
				Next.NoiseClueSerial = Noise.Serial;
			}
			// 도착한 회차는 고정 중심에서 수색한다. 더 새로운 소리만 이동 단계로 되돌린다.
			if (IsNoiseSearchActive() && !IsNoiseDestinationChangeReady(Next.NoiseClueSerial))
			{
				Next.Type = EEnemyPerceptionGoalType::Search;
				Next.Location = NoiseInvestigationLocation;
				Next.InformationAt = NoiseInvestigationClue.OccurredAt;
				Next.NoiseClueSerial = NoiseInvestigationClue.Serial;
			}
		}
		else if (CombatTarget)
		{
			Next.Type = EEnemyPerceptionGoalType::Combat;
			Next.Target = CombatTarget;
			Next.Location = bLiveShared && Shared.TargetActor.Get() == CombatTarget
				? Shared.LastObservedLocation : GetCombatNavigationTargetLocation(CombatTarget);
			Next.InformationAt = LastPerceptionObservedAt;
		}
		else if (bHasLastKnownPlayerLocation)
		{
			Next.Type = EEnemyPerceptionGoalType::Search;
			Next.Location = LastKnownPlayerLocation;
			Next.InformationAt = LastPerceptionObservedAt;
		}
	}

	if (bNoiseInvestigationActive && (Next.Type == EEnemyPerceptionGoalType::Combat
		|| Next.Type == EEnemyPerceptionGoalType::CommittedAction || Next.Type == EEnemyPerceptionGoalType::None))
	{
		// 재발견은 조사 소유권부터 해제한다. 기존 전투 추적·쿨다운은 그대로 이어간다.
		ConsumedNoiseAt = FMath::Max(ConsumedNoiseAt, LastNoiseDestinationChangedAt);
		bResumeTargetAttackPreparation = Next.Type == EEnemyPerceptionGoalType::Combat
			&& PreparedAttackTarget.Get() == Next.Target.Get() && PreparedAttackWeapon.Get() == CurrentWeapon
			&& PreparedAttackController.Get() == GetController();
		bNoiseInvestigationActive = bNoiseInvestigationSearching = false;
		++NoiseInvestigationSerial;
		EndSearchEpisode();
	}
	const bool bChanged = Next.Type != PerceptionGoal.Type || Next.Target != PerceptionGoal.Target
		|| (Next.Type == EEnemyPerceptionGoalType::InvestigateNoise && Next.NoiseClueSerial != PerceptionGoal.NoiseClueSerial)
		|| ((Next.Type == EEnemyPerceptionGoalType::InvestigateNoise || Next.Type == EEnemyPerceptionGoalType::Search)
			&& Next.Location != PerceptionGoal.Location);
	// 후보 수신과 목표 채택을 구분한다. 선택된 소리의 원래 시각만 반영하며 조회로 연장하지 않는다.
	if (Next.Type == EEnemyPerceptionGoalType::InvestigateNoise)
	{
		RecordAlertClue(Next.InformationAt);
	}
	PerceptionGoal = Next;
	if (bChanged)
	{
		bNoiseDirectionEvaluationPending = true;
	}
	if (bChanged && Next.Type == EEnemyPerceptionGoalType::InvestigateNoise)
	{
		LogNoiseInvestigationDiagnostic(TEXT("GoalSelected"), Noise, TEXT("EligibleCurrentCandidate"));
	}
	if ((IsNoiseDirectionConfirmationActive() || DeferredNoiseDirectionClue.Serial != 0) && !CanRunNoiseDirectionConfirmation())
	{
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	}
	// 의도 변경만 알린다. 기존 전투 대상·이동·공격 상태는 이 단계에서 변경하지 않는다.
	if (bChanged)
	{
		SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
	}
}

bool AEnemyBase::BeginNoiseInvestigation(FVector& OutDestination)
{
	OutDestination = FVector::ZeroVector;
	if (!HasAuthority() || !GetWorld() || !CanUseEnemyPerception() || IsDead()
		|| (CombatState != EEnemyCombatState::Combat && CombatState != EEnemyCombatState::Alert)
		|| IsPossessedActionCommitted())
	{
		return false;
	}
	RefreshPerceptionGoal();
	if (PerceptionGoal.Type != EEnemyPerceptionGoalType::InvestigateNoise)
	{
		return false;
	}
	// 같은 Task의 재진입은 새 채택이 아니다. 만료된 후보라도 이미 시작한 이동 스냅샷은 유지한다.
	if (bNoiseInvestigationActive && !bNoiseInvestigationSearching
		&& PerceptionGoal.NoiseClueSerial == NoiseInvestigationClue.Serial)
	{
		OutDestination = NoiseInvestigationLocation;
		return true;
	}
	FEnemyHeardNoise Selected;
	if (!TryGetNoiseGoalClue(Selected) || !CanReplaceNoiseDestination(Selected))
	{
		return false;
	}
	ClearNoiseInvestigationFailure();
	// 이동으로 채택한 사건을 나중에 옛 돌아보기로 재생하지 않는다. 위치 후보의 수명은 그대로 둔다.
	// 후보 수신/재진입이 아닌 실제 목적지 채택에서만 이전 전투 회차와 예약을 폐기한다.
	ResetCombatTacticalEpisode(false);
	if (!Selected.bNearDirectionReaction) MarkNoiseDirectionUsed(Selected.Serial);
	if (!NoiseDirectionConfirmation.Clue.bNearDirectionReaction)
	{
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	}
	DeferredNoiseDirectionClue = FEnemyHeardNoise();
	bDeferredNoiseDirectionReady = false;
	EndSearchEpisode(true);
	bNoiseInvestigationActive = true;
	bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	NoiseInvestigationLocation = Selected.Location;
	NoiseInvestigationClue = Selected;
	NoiseInvestigationTravelDirection = (Selected.Location - GetActorLocation()).GetSafeNormal2D();
	VisualInvestigation = FEnemyVisualInvestigation();
	LastNoiseDestinationChangedAt = GetWorld()->GetTimeSeconds();
	OutDestination = NoiseInvestigationLocation;
	LogNoiseInvestigationDiagnostic(TEXT("MovementAdopted"), NoiseInvestigationClue, TEXT("NewInvestigation"));
	if (!HasTrackableTarget())
	{
		UpdateLastKnownPlayerLocation(NoiseInvestigationLocation);
	}
	PauseTargetAttack();
	RefreshPerceptionGoal();
	SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
	return true;
}

bool AEnemyBase::IsNoiseInvestigationCurrent(uint64 Serial) const
{
	return HasAuthority() && bNoiseInvestigationActive && NoiseInvestigationSerial == Serial
		&& CanUseEnemyPerception() && !IsDead() && !IsPossessedActionCommitted()
		&& (CombatState == EEnemyCombatState::Combat || CombatState == EEnemyCombatState::Alert)
		&& PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise;
}

bool AEnemyBase::TryUpdateNoiseInvestigationDestination(FVector& OutDestination)
{
	OutDestination = NoiseInvestigationLocation;
	if (!IsNoiseInvestigationCurrent(NoiseInvestigationSerial) || bNoiseInvestigationSearching)
	{
		return false;
	}
	FEnemyHeardNoise Selected;
	if (!TryGetNoiseGoalClue(Selected) || !CanReplaceNoiseDestination(Selected))
	{
		return false;
	}
	ClearNoiseInvestigationFailure();
	ResetCombatTacticalEpisode(false);
	if (!Selected.bNearDirectionReaction) MarkNoiseDirectionUsed(Selected.Serial);
	if (!NoiseDirectionConfirmation.Clue.bNearDirectionReaction)
	{
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	}
	DeferredNoiseDirectionClue = FEnemyHeardNoise();
	bDeferredNoiseDirectionReady = false;
	NoiseInvestigationLocation = Selected.Location;
	NoiseInvestigationClue = Selected;
	NoiseInvestigationTravelDirection = (Selected.Location - GetActorLocation()).GetSafeNormal2D();
	LastNoiseDestinationChangedAt = GetWorld()->GetTimeSeconds();
	OutDestination = NoiseInvestigationLocation;
	LogNoiseInvestigationDiagnostic(TEXT("MovementAdopted"), NoiseInvestigationClue, TEXT("DestinationChanged"));
	if (!HasTrackableTarget())
	{
		UpdateLastKnownPlayerLocation(NoiseInvestigationLocation);
	}
	RefreshPerceptionGoal();
	return true;
}

bool AEnemyBase::IsNoiseDestinationChangeReady(uint64 ClueSerial) const
{
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (Candidate.Serial == ClueSerial)
		{
			return CanReplaceNoiseDestination(Candidate);
		}
	}
	return false;
}

bool AEnemyBase::BeginNoiseInvestigationSearch(uint64 Serial, const FVector& ReachableCenter)
{
	if (!IsNoiseInvestigationCurrent(Serial) || ReachableCenter.ContainsNaN()
		|| !ReachableCenter.Equals(GetActorLocation(), 1.0))
	{
		return false;
	}
	// 이동 실패도 실제 도달한 위치에서만 수색한다. 벽 너머의 원래 좌표를 도달점으로 만들지 않는다.
	NoiseInvestigationLocation = ReachableCenter;
	if (!HasTrackableTarget())
	{
		UpdateLastKnownPlayerLocation(ReachableCenter);
	}
	bNoiseInvestigationSearching = true;
	if (!BeginSearchEpisode())
	{
		bNoiseInvestigationSearching = false;
		return false;
	}
	RefreshPerceptionGoal();
	LogNoiseInvestigationDiagnostic(TEXT("SearchStarted"), NoiseInvestigationClue, TEXT("ReachedCenter"));
	NotifyNoiseInvestigationNaturalBoundary(Serial, true);
	return IsNoiseSearchActive();
}

void AEnemyBase::EndNoiseInvestigation(bool bConsumeNoise)
{
	if (!HasAuthority() || !bNoiseInvestigationActive)
	{
		return;
	}
	LogNoiseInvestigationDiagnostic(TEXT("InvestigationEnded"), NoiseInvestigationClue,
		bConsumeNoise ? TEXT("ConsumeAdoptedEvidence") : TEXT("Handoff"));
	EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	bResumeTargetAttackPreparation = PreparedAttackTarget.IsValid() && PreparedAttackTarget.Get() == GetTrackedTarget()
		&& PreparedAttackWeapon.Get() == CurrentWeapon && PreparedAttackController.Get() == GetController();
	// 목표 채택 때 이미 알려진 사건까지 정리한다. 채택 이후 새로 발생한 단서는 별도로 재평가한다.
	if (bConsumeNoise)
	{
		ConsumedNoiseAt = FMath::Max(ConsumedNoiseAt, LastNoiseDestinationChangedAt);
	}
	bNoiseInvestigationActive = bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	EndSearchEpisode();
	PerceptionGoal = FEnemyPerceptionGoal();
	RefreshPerceptionGoal();
}

bool AEnemyBase::CompleteNoiseInvestigation()
{
	if (!HasAuthority() || !IsNoiseSearchActive() || !CanContinueSearchEpisode())
	{
		return false;
	}
	// 완료 출력 직후 새 시야·소리가 도착할 수 있다. 최신 의도를 확인한 뒤에만 회차를 소비한다.
	RefreshTargetTracking();
	if (!IsNoiseSearchActive() || PerceptionGoal.Type != EEnemyPerceptionGoalType::Search)
	{
		return false;
	}
	EndNoiseInvestigation();
	if (PerceptionGoal.Type == EEnemyPerceptionGoalType::Combat || IsLocationInvestigationGoal())
	{
		return true;
	}
	const UEnemyRoomSubsystem* Rooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	// 개인 조사가 끝나도 진행 중인 Room 전투를 종료하지 않는다. 기존 전투 수색·경계로 돌아간다.
	if (CombatState == EEnemyCombatState::Combat && Rooms && Rooms->IsRoomInCombat(GetDefaultRoomTag()))
	{
		return true;
	}
	CommitToNonCombatAfterSearch();
	return true;
}

void AEnemyBase::ClearTargetTracking()
{
	if (!HasAuthority())
	{
		return;
	}
	StoreTargetAttackPreparation();
	PreparedAttackTarget.Reset();
	PreparedAttackWeapon.Reset();
	PreparedAttackController.Reset();
	bTargetAttackPreparationComplete = false;
	TargetAttackPreparationRemaining = 0.0f;
	ResetCombatTacticalEpisode(false);
	NextTargetReevaluationAt = 0.0;
	TargetSelectedAt = -1.0;
	TargetSelectionBurstTarget.Reset();
	TargetSelectionBlockedAt = -1.0;
	bTargetSelectionBoundaryPending = false;
	SetCombatAimPresentationOwned(false);
	SelectedCombatAimTarget.Reset();
	SelectedCombatAimRank = 0;
	CombatAimObservationEpoch = 0;
	NextCombatAimRefreshAt = -1.0;
	GetWorldTimerManager().ClearTimer(TargetTrackingTimer);
	TrackedTarget.Reset();
	CombatNavigationObservedTarget.Reset();
	CombatNavigationObservedLocation = FVector::ZeroVector;
	CombatNavigationPath.Reset();
	CombatNavigationPathReservation = 0;
	CombatNavigationPathSerial = 0;
	bCombatNavigationRetargetPending = false;
	CombatNavigationRetargetVelocity = FVector::ZeroVector;
	CombatNavigationRetargetVelocityExpiresAt = -1.0;
	bAimTargetIsShooter = false;
	TrackingController.Reset();
	TrackingRoomTag = FGameplayTag();
	TargetTrackingExpiresAt = 0.0;
	bTrackingFromRoom = false;
	bTrackingAnnounced = false;
	bTrackingWasDirectlyVisible = false;
	bTrackingExpiredForSearch = false;
}

void AEnemyBase::FinishTargetTracking()
{
	if (!HasAuthority() || GetTrackedTarget())
	{
		return;
	}
	if (bTargetAttackActive)
	{
		PauseTargetAttack();
	}
	const AEnemyAIController* TargetController = TrackingController.Get();
	// 만료 뒤 실제 좌표를 다시 읽지 않는다. 마지막 유효 갱신 위치가 수색 중심으로 남는다.
	// 정상 만료는 동일 대상의 준비 기록을 보존하고 사망/은신/팀 변경은 폐기한다.
	if (!TargetController || TargetController != GetController() || !TargetController->IsValidTrackingTarget(TrackedTarget.Get()))
	{
		// 대상의 무효화는 준비 권한만 폐기한다. 다음 대상에게도 이미 시작한 무기 회복은 남는다.
		const bool bControllerChanged = !TargetController || TargetController != GetController();
		ResetTargetAttackContinuity(bControllerChanged || !UsesTargetAttackPreparation());
	}
	ClearTargetTracking();
	if (CombatState != EEnemyCombatState::Combat || !CanUseEnemyPerception() || IsDead())
	{
		return;
	}
	bTrackingExpiredForSearch = true;
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!HasTrackableTarget() && bTrackingExpiredForSearch)
		{
			SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Perception.TargetLost")));
		}
	}));
}

void AEnemyBase::ApplySharedTargetContact(
	const FVector& TargetLocation,
	bool bDeferStateTreeEvent,
	uint64 Revision)
{
	if (!HasAuthority()
		|| (CombatState != EEnemyCombatState::Combat
			&& CombatState != EEnemyCombatState::Returning
			&& !(IsPoolManaged() && CombatState == EEnemyCombatState::Alert))
		|| IsAIControlSuppressed()
		|| !CanUseRoomTargetSharing())
	{
		return;
	}

	if (TargetLocation.ContainsNaN())
	{
		return;
	}
	// 기존 호출자는 좌표 변경을 새 정보로 취급한다. 방 공유는 명시적 번호로 재목격을 구분한다.
	const bool bContactChanged = Revision != 0 ? Revision > SharedTargetRevision
		: !bHasSharedTargetContact || !SharedTargetLocation.Equals(TargetLocation, 1.0f);
	if (Revision != 0 && Revision < SharedTargetRevision)
	{
		return;
	}
	if (CombatState == EEnemyCombatState::Returning)
	{
		// 방 전투가 계속된다는 사실이나 같은 단서의 재전달은 귀환을 취소하지 않는다.
		// 기존 수색이 채택할 만큼 달라진 새 관측 위치만 새 전투 조사로 넘긴다.
		const FVector PreviousLocation = SearchEpisode.Serial != 0 ? SearchEpisode.Center : LastKnownPlayerLocation;
		if (!bContactChanged || (Revision != 0 && Revision <= ConsumedSharedTargetRevision)
			|| FVector::DistSquared(PreviousLocation, TargetLocation) <= FMath::Square(200.0f))
		{
			return;
		}
		EnterCombatFromRoom(TargetLocation, false, true);
	}
	if (bContactChanged)
	{
		SharedTargetRevision = Revision != 0 ? Revision : SharedTargetRevision + 1;
	}
	// 직접 시야가 더 정확하다. 이때의 공유 정보는 이미 확인한 것으로 처리한다.
	if (bPlayerCurrentlyVisible || HasTrackableTarget())
	{
		ConsumedSharedTargetRevision = SharedTargetRevision;
	}
	bHasSharedTargetContact = true;
	SharedTargetLocation = TargetLocation;
	if (!bPlayerCurrentlyVisible && !HasTrackableTarget() && !bTrackingAnnounced && !bTrackingExpiredForSearch)
	{
		UpdateLastKnownPlayerLocation(TargetLocation);
	}
	// 같은 위치의 공유 갱신은 현재 수색 경로와 그 공간 예약을 그대로 유지한다.
	if (bContactChanged && !HasHitResponse() && (!SearchEpisode.bActive || HasMeaningfulSearchHint()))
	{
		ReleaseSearchDestinations();
	}
	if (IsLocationInvestigationActive())
	{
		RefreshPerceptionGoal();
	}

	if (bContactChanged)
	{
		const FGameplayTag TargetSharedTag = FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Combat.TargetShared"));
		if (bDeferStateTreeEvent)
		{
			GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, TargetSharedTag]()
			{
				SendEnemyStateTreeEvent(HasTrackableTarget() ? TAG_EnemyTrackingChanged : TargetSharedTag);
			}));
		}
		else
		{
			SendEnemyStateTreeEvent(HasTrackableTarget() ? TAG_EnemyTrackingChanged : TargetSharedTag);
		}
	}
}

void AEnemyBase::ClearSharedTargetContact()
{
	if (!HasAuthority() || !bHasSharedTargetContact)
	{
		return;
	}

	// 직접 시야가 없는 개체만 방의 마지막 공유 좌표를 수색 중심으로 보존한다.
	if (!bPlayerCurrentlyVisible && !HasTrackableTarget() && !bTrackingAnnounced && !bTrackingExpiredForSearch)
	{
		UpdateLastKnownPlayerLocation(SharedTargetLocation);
	}
	bHasSharedTargetContact = false;
	if (IsNoiseInvestigationActive())
	{
		RefreshPerceptionGoal();
	}

	const FGameplayTag SharedTargetLostEvent = FGameplayTag::RequestGameplayTag(
		TEXT("Enemy.Event.Combat.SharedTargetLost"));

	// TimerManager가 StateTreeComponent보다 먼저 갱신될 수 있으므로 한 프레임을
	// 완전히 통과시킨 뒤 이벤트를 보내 Global Task 출력 갱신을 보장한다.
	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(
			this,
			[this, SharedTargetLostEvent]()
			{
				GetWorldTimerManager().SetTimerForNextTick(
					FTimerDelegate::CreateWeakLambda(
						this,
						[this, SharedTargetLostEvent]()
						{
							if (!bHasSharedTargetContact)
							{
								SendEnemyStateTreeEvent(HasTrackableTarget() ? TAG_EnemyTrackingChanged : SharedTargetLostEvent);
							}
						}));
			}));
}

void AEnemyBase::EnterCombat(const FVector& PlayerLocation)
{
	EnterCombatFromRoom(PlayerLocation, true);
}

void AEnemyBase::EnterCombatFromRoom(
	const FVector& PlayerLocation,
	bool bPropagateToRoom,
	bool bDeferStateTreeEvent)
{
	if (!HasAuthority())
	{
		return;
	}

	if (CombatState == EEnemyCombatState::Stun)
	{
		PromotePreStunState(EEnemyCombatState::Combat);
		UpdateLastKnownPlayerLocation(PlayerLocation);
		return;
	}

	const FGameplayTag RoomTag = GetDefaultRoomTag();
	const AAutoTurret* Turret = Cast<AAutoTurret>(this);
	// 아군화된 터렛은 같은 감지 절차를 사용하되 적 Encounter의 시작 승인에는 참여하지 않는다.
	if (bPropagateToRoom && RoomTag.IsValid() && !(Turret && Turret->IsHackedToPlayerTeam()))
	{
		// 합류가 실패했다면 최초 발각 Enemy도 Alert에 남겨 Room 전투보다 먼저 공격하지 않는다.
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			if (!RoomSubsystem->NotifyRoomCombat(RoomTag, PlayerLocation, this))
			{
				return;
			}
		}
	}

	const bool bEnteredCombat = CombatState != EEnemyCombatState::Combat;
	if (bEnteredCombat)
	{
		ResetReturnHomeRecovery();
	}
	ReleasePatrolDestination();
	bInCombat = true;
	CombatState = EEnemyCombatState::Combat;
	RefreshCombatStatePresentation();
	UpdateLastKnownPlayerLocation(PlayerLocation);

	if (bEnteredCombat)
	{
		InitializeCombatMovementPreferences();
		RefreshPerceptionConfigForCurrentState();
	}

	if (bEnteredCombat)
	{
		const FGameplayTag CombatEnteredTag = FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Combat.Entered"));
		if (bDeferStateTreeEvent)
		{
			SendEnemyStateTreeEventNextTick(CombatEnteredTag);
		}
		else
		{
			SendEnemyStateTreeEvent(CombatEnteredTag);
		}
	}
}

void AEnemyBase::EnterAlert(const FVector& PlayerLocation)
{
	EnterAlertFromPerception(PlayerLocation);
}

// Alert Task가 감지 유지/상실 시간을 판정한 뒤 이 함수들로 실제 CombatState를 확정한다.
// 시간 계산은 StateTree가, 권한 상태 변경과 전환 이벤트 발행은 C++가 담당한다.
bool AEnemyBase::CommitAlertToCombat()
{
	if (!HasAuthority() || CombatState != EEnemyCombatState::Alert)
	{
		return false;
	}

	// 이 함수는 Alert StateTree Task의 Tick 안에서 호출된다. 같은 Tick에 이벤트를 보내면
	// Global Sync가 이전 Alert 값을 가진 채 Enter Condition을 검사하므로 다음 Tick으로 넘긴다.
	bool bPropagateToRoom = true;
	if (IsPoolManaged())
	{
		if (const UEnemyRoomSubsystem* RoomSubsystem =
			GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			// 이미 전투 중인 방의 증원은 새 전투를 요청하지 않고 자신만 Combat으로 확정한다.
			bPropagateToRoom = !RoomSubsystem->IsRoomInCombat(GetDefaultRoomTag());
		}
	}
	EnterCombatFromRoom(LastKnownPlayerLocation, bPropagateToRoom, true);
	return CombatState == EEnemyCombatState::Combat;
}

bool AEnemyBase::CommitAlertToCombatFromHearing(AActor* Source, const FVector& HeardLocation, double OccurredAt)
{
	if (!HasAuthority() || CombatState != EEnemyCombatState::Alert || !IsValid(Source)
		|| Source->IsActorBeingDestroyed() || HeardLocation.ContainsNaN()
		|| !FMath::IsFinite(OccurredAt) || OccurredAt < 0.0 || OccurredAt > GetWorld()->GetTimeSeconds())
	{
		return false;
	}
	UpdateLastKnownPlayerLocation(HeardLocation);
	if (!CommitAlertToCombat())
	{
		return false;
	}

	// 완료한 출처의 마지막 소리로 조사 회차를 넘긴다. 이전 부분 시야의 추적 유예나
	// 다른 출처의 조사 좌표가 청각 완료를 실제 대상 위치 추적으로 바꾸지 않게 분리한다.
	ClearTargetTracking();
	EndSearchEpisode();
	bNoiseInvestigationActive = true;
	bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	NoiseInvestigationClue = FEnemyHeardNoise();
	NoiseInvestigationClue.Source = Source;
	NoiseInvestigationClue.Location = HeardLocation;
	NoiseInvestigationLocation = HeardLocation;
	NoiseInvestigationClue.OccurredAt = OccurredAt;
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (Candidate.Source.Get() == Source && Candidate.Location == HeardLocation && Candidate.OccurredAt == OccurredAt)
		{
			NoiseInvestigationClue = Candidate;
		}
	}
	if (NoiseInvestigationClue.Serial == 0)
	{
		NoiseInvestigationClue.Serial = ++NextHeardNoiseSerial;
	}
	VisualInvestigation = FEnemyVisualInvestigation();
	LastNoiseDestinationChangedAt = GetWorld()->GetTimeSeconds();
	UpdateLastKnownPlayerLocation(HeardLocation);
	PauseTargetAttack();
	RefreshPerceptionGoal();
	return true;
}

bool AEnemyBase::CommitAlertToNonCombat()
{
	if (!HasAuthority() || CombatState != EEnemyCombatState::Alert)
	{
		return false;
	}
	return CommitToNonCombatAfterSearch();
}

bool AEnemyBase::CommitToNonCombatAfterSearch()
{
	if (!HasAuthority() || IsDead() || !CanUseEnemyPerception()
		|| (CombatState != EEnemyCombatState::Alert && CombatState != EEnemyCombatState::Combat))
	{
		return false;
	}
	// 최초 경계의 모든 해제 경로가 같은 기한을 따른다. Combat 수색/Returning의 종료 시간은 바꾸지 않는다.
	if (CombatState == EEnemyCombatState::Alert
		&& (IsAlertHoldActive() || IsHitInvestigationCurrent(ActiveHitClue.Serial)))
	{
		return false;
	}
	if (IsPoolManaged())
	{
		if (const UEnemyRoomSubsystem* RoomSubsystem =
			GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			// 방 전투가 끝날 때까지 미발견 증원은 경계를 유지한다.
			if (RoomSubsystem->IsRoomInCombat(GetDefaultRoomTag()))
			{
				return false;
			}
		}
	}

	// 형제 방에서 Alert가 끝나도 곧바로 순찰하지 않는다. 원래 방의 실제
	// 캡슐 위치를 확인하고, 아직 밖이면 StateTree의 귀환 이동으로 넘긴다.
	const URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	const FGameplayTag HomeTag = GetDefaultRoomTag();
	const ARoomVolume* Home = Combat ? Combat->GetRegisteredRoomVolume(HomeTag) : nullptr;
	// 소속 방이 언로드된 동안 CurrentRoomTag가 기본 태그로 돌아와도 도착으로
	// 취급하지 않는다. 등록이 복구될 때까지 귀환 상태에서 대기한다.
	const bool bAwayFromHome = Home ? !IsInsideDefaultRoom()
		: URoomTagComponent::IsValidChildRoomTag(HomeTag);
	ResetReturnHomeRecovery();
	CombatState = bAwayFromHome ? EEnemyCombatState::Returning : EEnemyCombatState::NonCombat;
	ClearAlertClue();
	bInCombat = false;
	RefreshCombatStatePresentation();
	RefreshPerceptionConfigForCurrentState();

	SendEnemyStateTreeEventNextTick(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Combat.AlertCleared")));

	return true;
}

bool AEnemyBase::IsInsideDefaultRoom() const
{
	const UWorld* World = GetWorld();
	const URoomCombatSubsystem* Combat = World ? World->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	ARoomVolume* Home = Combat ? Combat->GetRegisteredRoomVolume(GetDefaultRoomTag()) : nullptr;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!IsValid(Home) || Home->IsActorBeingDestroyed() || !IsValid(Capsule))
	{
		return false;
	}
	FFlightPathRequest Bounds;
	Bounds.bRequireRoom = true;
	Bounds.Room = Home;
	const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetComponentQuat());
	return FlightPathfinding::IsInsideMovementRooms(Bounds, Capsule->GetComponentLocation(), Extent);
}

bool AEnemyBase::GetReturnHomeDestination(FVector& OutDestination) const
{
	const UWorld* World = GetWorld();
	const URoomCombatSubsystem* Combat = World ? World->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	ARoomVolume* Home = Combat ? Combat->GetRegisteredRoomVolume(GetDefaultRoomTag()) : nullptr;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!HasAuthority() || CombatState != EEnemyCombatState::Returning || IsDead()
		|| IsAIControlSuppressed() || !IsValid(Home) || Home->IsActorBeingDestroyed() || !IsValid(Capsule))
	{
		return false;
	}
	const FVector Offset = Capsule->GetComponentLocation() - GetActorLocation();
	const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(
		Capsule->GetScaledCapsuleRadius() + 5.0f, Capsule->GetScaledCapsuleHalfHeight() + 5.0f,
		Capsule->GetComponentQuat());
	FVector Center;
	// 순찰 A는 원래 배치/대여 위치다. 이 좌표가 경계 밖이어도 목적지는 원래
	// 방 안으로만 보정한다. 경로가 막히면 이동 Task가 재시도하고 순간이동하지 않는다.
	const FVector Requested = GetPatrolPointA().ContainsNaN() ? Home->GetActorLocation() : GetPatrolPointA();
	if (!Home->TryClampFlightLocation(Requested + Offset, Extent, Center))
	{
		return false;
	}
	OutDestination = Center - Offset;
	return true;
}

void AEnemyBase::RestartReturnHomeRecovery()
{
	++ReturnHomeRequestSerial;
	bReturnHomeGuarding = false;
	bReturnHomeFallback = false;
	ReturnHomeCandidateChecks = 0;
	SelectedReturnHomeDestination = FVector::ZeroVector;
	bReturnHomeFlightActive = false;
	bReturnHomeRecoveryExhausted = false;
	bReturnHomeRecoverySuspended = false;
	ReturnHomeRequests = 0;
	ReturnHomeWaitingSince = -1.0;
	ReturnHomeRecoveryElapsed = 0.0;
	ReturnHomeNextAttemptAt = 0.0;
}

void AEnemyBase::SuspendReturnHomeRecovery()
{
	if (!bReturnHomeRecoveryActive || bReturnHomeRecoverySuspended)
	{
		return;
	}
	++ReturnHomeRequestSerial;
	bReturnHomeFlightActive = false;
	if (ReturnHomeWaitingSince >= 0.0 && GetWorld())
	{
		ReturnHomeRecoveryElapsed += GetWorld()->GetTimeSeconds() - ReturnHomeWaitingSince;
	}
	ReturnHomeWaitingSince = -1.0;
	bReturnHomeRecoverySuspended = true;
	if (URoomCombatSubsystem* Rooms = ReturnHomeRooms.Get())
	{
		Rooms->OnMovementObstacleOpened.Remove(ReturnHomeObstacleHandle);
		Rooms->OnRoomStartReadinessChanged.Remove(ReturnHomeReadinessHandle);
	}
	ReturnHomeObstacleHandle.Reset();
	ReturnHomeReadinessHandle.Reset();
}

void AEnemyBase::ResetReturnHomeRecovery()
{
	if (URoomCombatSubsystem* Rooms = ReturnHomeRooms.Get())
	{
		Rooms->OnMovementObstacleOpened.Remove(ReturnHomeObstacleHandle);
		Rooms->OnRoomStartReadinessChanged.Remove(ReturnHomeReadinessHandle);
	}
	ReturnHomeObstacleHandle.Reset();
	ReturnHomeReadinessHandle.Reset();
	ReturnHomeRooms.Reset();
	ReturnHomeSnapshot.Reset();
	ReturnHomeSnapshotBounds = FBox(ForceInit);
	ReturnHomeFailureBounds = FBox(ForceInit);
	ReturnHomeRecoveryChanges.Reset();
	ReturnHomeController.Reset();
	bReturnHomeRecoveryActive = false;
	RestartReturnHomeRecovery();
}

bool AEnemyBase::IsReturnHomeRequestCurrent(uint64 Serial) const
{
	return Serial != 0 && Serial == ReturnHomeRequestSerial && bReturnHomeRecoveryActive
		&& bReturnHomeFlightActive && HasAuthority() && !IsDead() && !IsAIControlSuppressed()
		&& CombatState == EEnemyCombatState::Returning && ReturnHomeController.Get() == GetController()
		&& ReturnHomeGeneration == GetPoolGameplayGeneration() && ReturnHomeLease == GetPoolLeaseSerial();
}

uint64 AEnemyBase::GetReturnHomeRequestSerial() const
{
	return IsReturnHomeRequestCurrent(ReturnHomeRequestSerial) ? ReturnHomeRequestSerial : 0;
}

bool AEnemyBase::GetSelectedReturnHomeDestination(FVector& OutDestination) const
{
	if (!IsReturnHomeRequestCurrent(ReturnHomeRequestSerial))
	{
		return false;
	}
	OutDestination = SelectedReturnHomeDestination;
	return true;
}

bool AEnemyBase::TrySelectReturnHomeDestination(FVector& OutDestination)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || IsDead() || IsAIControlSuppressed()
		|| CombatState != EEnemyCombatState::Returning)
	{
		return false;
	}
	if (!bReturnHomeRecoveryActive || ReturnHomeController.Get() != GetController()
		|| ReturnHomeGeneration != GetPoolGameplayGeneration() || ReturnHomeLease != GetPoolLeaseSerial())
	{
		ResetReturnHomeRecovery();
		bReturnHomeRecoveryActive = true;
		ReturnHomeController = GetController();
		ReturnHomeGeneration = GetPoolGameplayGeneration();
		ReturnHomeLease = GetPoolLeaseSerial();
	}
	if (!ReturnHomeReadinessHandle.IsValid())
	{
		URoomCombatSubsystem* Rooms = World->GetSubsystem<URoomCombatSubsystem>();
		ReturnHomeRooms = Rooms;
		if (Rooms)
		{
			ARoomVolume* Home = Rooms->GetRegisteredRoomVolume(GetDefaultRoomTag());
			ReturnHomeSnapshot = Home;
			ReturnHomeSnapshotBounds = Home ? Home->GetComponentsBoundingBox() : FBox(ForceInit);
			ReturnHomeReadinessHandle = Rooms->OnRoomStartReadinessChanged.AddWeakLambda(this,
				[this](FGameplayTag RoomTag)
				{
					if (CombatState != EEnemyCombatState::Returning || RoomTag != GetDefaultRoomTag())
					{
						return;
					}
					URoomCombatSubsystem* CurrentRooms = ReturnHomeRooms.Get();
					ARoomVolume* CurrentHome = CurrentRooms ? CurrentRooms->GetRegisteredRoomVolume(RoomTag) : nullptr;
					const FBox Bounds = CurrentHome ? CurrentHome->GetComponentsBoundingBox() : FBox(ForceInit);
					if (!IsValid(CurrentHome))
					{
						// 방 제거는 복구 근거가 아니다. 진행 중 결과만 폐기하고 기존 예산으로 등록을 기다린다.
						ReturnHomeSnapshot.Reset();
						ReturnHomeSnapshotBounds = FBox(ForceInit);
						if (bReturnHomeFlightActive)
						{
							bReturnHomeFlightActive = false;
							++ReturnHomeRequestSerial;
							ReturnHomeWaitingSince = GetWorld()->GetTimeSeconds();
							const float Interval = FMath::IsFinite(RuntimeStat.ReturnRetryInterval)
								? FMath::Max(0.01f, RuntimeStat.ReturnRetryInterval) : 1.0f;
							ReturnHomeNextAttemptAt = ReturnHomeWaitingSince + Interval;
						}
						return;
					}
					// 같은 readiness 통지는 예산을 되살리지 않는다. 실제 방의 등록/공간 변경만 새 회차다.
					if (CurrentHome != ReturnHomeSnapshot.Get() || Bounds != ReturnHomeSnapshotBounds)
					{
						ReturnHomeSnapshot = CurrentHome;
						ReturnHomeSnapshotBounds = Bounds;
						ReturnHomeRecoveryChanges.Reset();
						RestartReturnHomeRecovery();
					}
				});
			ReturnHomeObstacleHandle = Rooms->OnMovementObstacleOpened.AddWeakLambda(this,
				[this](const FBox& Bounds)
				{
					if (CombatState != EEnemyCombatState::Returning || bReturnHomeFlightActive
						|| !ReturnHomeFailureBounds.IsValid || !Bounds.Intersect(ReturnHomeFailureBounds)
						|| ReturnHomeRecoveryChanges.Contains(Bounds))
					{
						return;
					}
					// 이미 처리한 문 열림은 같은 실패를 반복해도 새 예산으로 취급하지 않는다.
					ReturnHomeRecoveryChanges.Add(Bounds);
					RestartReturnHomeRecovery();
				});
		}
	}
	if (bReturnHomeGuarding)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const double RetryInterval = FMath::IsFinite(RuntimeStat.ReturnRetryInterval)
		? FMath::Max(0.01f, RuntimeStat.ReturnRetryInterval) : 1.0f;
	if (bReturnHomeRecoverySuspended)
	{
		// Stun 동안 예산은 보존하되, 복원 후에도 이전 비동기 이동 결과는 다시 소유하지 않는다.
		bReturnHomeRecoverySuspended = false;
		ReturnHomeWaitingSince = Now;
		ReturnHomeNextAttemptAt = Now + RetryInterval;
	}
	if (bReturnHomeFlightActive)
	{
		// Fly/도착 검증 실패로 재진입한 시점부터 복구 시계를 재개한다. 정상 이동 시간은 제외한다.
		bReturnHomeFlightActive = false;
		++ReturnHomeRequestSerial;
		ReturnHomeWaitingSince = Now;
		ReturnHomeNextAttemptAt = Now + RetryInterval;
	}
	const double MaxSeconds = FMath::IsFinite(RuntimeStat.ReturnMaxSeconds)
		? FMath::Max(0.01f, RuntimeStat.ReturnMaxSeconds) : 3.0f;
	const double Waiting = ReturnHomeWaitingSince >= 0.0 ? Now - ReturnHomeWaitingSince : 0.0;
	if (ReturnHomeRequests >= FMath::Max(1, RuntimeStat.ReturnMaxRequests)
		|| ReturnHomeRecoveryElapsed + Waiting >= MaxSeconds)
	{
		if (!bReturnHomeRecoveryExhausted)
		{
			bReturnHomeRecoveryExhausted = true;
			bReturnHomeGuarding = true;
			++ReturnHomeRequestSerial;
			UE_LOG(LogOutlier, Display, TEXT("[ReturnHome] SafeWait Enemy=%s Requests=%d RecoverySeconds=%.2f Generation=%d Lease=%d"),
				*GetName(), ReturnHomeRequests, ReturnHomeRecoveryElapsed + Waiting, ReturnHomeGeneration, ReturnHomeLease);
		}
		return false;
	}
	if (Now < ReturnHomeNextAttemptAt)
	{
		return false;
	}
	++ReturnHomeRequests;
	++ReturnHomeRequestSerial;
	if (!GetReturnHomeDestination(OutDestination))
	{
		if (ReturnHomeWaitingSince < 0.0)
		{
			ReturnHomeWaitingSince = Now;
		}
		ReturnHomeNextAttemptAt = Now + RetryInterval;
		return false;
	}
	if (ReturnHomeRequests > 1)
	{
		// 대체 후보도 같은 회차의 요청/대기 상한을 사용한다. 실제 경로는 기존 Fly가 검증한다.
		const FVector HomeAnchor = OutDestination;
		UCapsuleComponent* Capsule = GetCapsuleComponent();
		URoomCombatSubsystem* Rooms = ReturnHomeRooms.Get();
		FEnemyMovementRoomContext MovementRooms;
		const bool bHasMovementRooms = Capsule && Rooms && Rooms->BuildEnemyMovementRoomContext(this, MovementRooms);
		bool bSelected = false;
		while (ReturnHomeCandidateChecks < 8 && bHasMovementRooms)
		{
			const float Angle = ReturnHomeCandidateChecks++ * PI / 4.0f;
			FVector Candidate = HomeAnchor + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 200.0f;
			const FVector Offset = Capsule->GetComponentLocation() - GetActorLocation();
			ARoomVolume* Home = ReturnHomeSnapshot.Get();
			const FVector Extent = FlightPathfinding::GetCapsuleWorldExtent(
				Capsule->GetScaledCapsuleRadius() + 5.0f, Capsule->GetScaledCapsuleHalfHeight() + 5.0f,
				Capsule->GetComponentQuat());
			FVector Center;
			if (!Home || !Home->TryClampFlightLocation(Candidate + Offset, Extent, Center))
			{
				continue;
			}
			Candidate = Center - Offset;
			if (Candidate.Equals(HomeAnchor, 1.0f))
			{
				continue;
			}
			FFlightPathRequest Request;
			Request.Start = Capsule->GetComponentLocation();
			Request.Goal = Center;
			Request.SearchBounds = FBox(Request.Start.ComponentMin(Center), Request.Start.ComponentMax(Center)).ExpandBy(1.0f);
			Request.bRequireRoom = true;
			Request.Room = Home;
			Request.AllowedRooms = MovementRooms.Rooms;
			if (FlightPathfinding::CheckDestination(*World, *Capsule, Request) != EFlightPathStatus::Success)
			{
				continue;
			}
			OutDestination = Candidate;
			bSelected = true;
			bReturnHomeFallback = true;
			break;
		}
		if (!bSelected)
		{
			bReturnHomeGuarding = true;
			bReturnHomeRecoveryExhausted = true;
			ReturnHomeWaitingSince = -1.0;
			UE_LOG(LogOutlier, Display, TEXT("[ReturnHome] Guard Reason=NoSafeFallback Enemy=%s Requests=%d CandidateChecks=%d"),
				*GetName(), ReturnHomeRequests, ReturnHomeCandidateChecks);
			return false;
		}
	}
	ReturnHomeRecoveryElapsed += Waiting;
	ReturnHomeWaitingSince = -1.0;
	SelectedReturnHomeDestination = OutDestination;
	bReturnHomeFlightActive = true;
	ReturnHomeFailureBounds = FBox(GetActorLocation(), GetActorLocation());
	ReturnHomeFailureBounds += OutDestination;
	ReturnHomeFailureBounds = ReturnHomeFailureBounds.ExpandBy(200.0f);
	return true;
}

bool AEnemyBase::CompleteReturnHome()
{
	if (bReturnHomeGuarding)
	{
		return false;
	}
	if (bReturnHomeRecoveryActive && (!IsReturnHomeRequestCurrent(ReturnHomeRequestSerial)
		|| FVector::DistSquared(GetActorLocation(), SelectedReturnHomeDestination) > FMath::Square(5.0f)))
	{
		return false;
	}
	if (bReturnHomeFallback)
	{
		const UCapsuleComponent* Capsule = GetCapsuleComponent();
		if (Capsule && FVector::DistSquared(GetActorLocation(), SelectedReturnHomeDestination) <= FMath::Square(5.0f))
		{
			bReturnHomeFlightActive = false;
			bReturnHomeGuarding = true;
			++ReturnHomeRequestSerial;
		}
		return false;
	}
	if (!HasAuthority() || CombatState != EEnemyCombatState::Returning || IsDead()
		|| IsAIControlSuppressed() || !IsInsideDefaultRoom())
	{
		return false;
	}
	ResetReturnHomeRecovery();
	CombatState = EEnemyCombatState::NonCombat;
	bInCombat = false;
	RefreshCombatStatePresentation();
	RefreshPerceptionConfigForCurrentState();
	SendEnemyStateTreeEventNextTick(FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Combat.AlertCleared")));
	return true;
}

void AEnemyBase::EnterAlertFromPerception(const FVector& PlayerLocation)
{
	if (!HasAuthority())
	{
		return;
	}

	if (CombatState == EEnemyCombatState::Stun)
	{
		PromotePreStunState(EEnemyCombatState::Alert);
		UpdateLastKnownPlayerLocation(PlayerLocation);
		return;
	}

	if (CombatState == EEnemyCombatState::Combat ||
		CombatState == EEnemyCombatState::Alert)
	{
		UpdateLastKnownPlayerLocation(PlayerLocation);
		return;
	}

	ResetReturnHomeRecovery();
	CombatState = EEnemyCombatState::Alert;
	RefreshCombatStatePresentation();
	ReleasePatrolDestination();
	UpdateLastKnownPlayerLocation(PlayerLocation);
	RefreshPerceptionConfigForCurrentState();

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Combat.Alerted")));
}

void AEnemyBase::EnterStun()
{
	ResetTargetAttackContinuity();
	if (!HasAuthority())
	{
		return;
	}

	if (CombatState == EEnemyCombatState::Stun)
	{
		return;
	}

	SuspendReturnHomeRecovery();
	InterruptDamagePresentation(0.05f);
	ClearTargetTracking();
	PreStunCombatState = CombatState;
	ReleasePatrolDestination();
	CombatState = EEnemyCombatState::Stun;
	RefreshPerceptionGoal();
	RefreshCombatStatePresentation();
	if (DownAudioContext.IsValid())
	{
		OutlierAudioFeedback::World(this, *DownAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
	}
	CancelCommittedAction();
	EndImpactReaction();
	ResetPossessedAttackInput();
	StopCurrentAttack();
	RemoveRoomTargetObserver();
	ReleaseSearchDestinations();
	RefreshPerceptionConfigForCurrentState();

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Status.StunStarted")));
}

void AEnemyBase::ClearStunForCommittedPossession()
{
	// IsPlayerControlled는 PlayerState 존재 여부도 검사한다.
	// 성공 확정 시점의 소유권은 실제 Controller를 기준으로 확인한다.
	const AController* PossessingController = GetController();
	if (!HasAuthority() || !bIsPossessed || !PossessingController
		|| !PossessingController->IsPlayerController() || IsDead()
		|| !OutlierAbilitySystemComponent)
	{
		return;
	}

	// 성공 시점의 스턴만 수집한다. 제거 콜백에서 새로 적용된 스턴까지 지우지 않는다.
	FGameplayEffectQuery StunQuery;
	StunQuery.CustomMatchDelegate.BindLambda([](const FActiveGameplayEffect& Effect)
	{
		// OwningTags 쿼리는 AssetTags도 포함하므로 실제 스턴을 부여하는 효과만 고른다.
		FGameplayTagContainer GrantedTags;
		Effect.Spec.GetAllGrantedTags(GrantedTags);
		return GrantedTags.HasTag(OutlierGameplayTags::State::Stunned());
	});
	const TArray<FActiveGameplayEffectHandle> StunEffects =
		OutlierAbilitySystemComponent->GetActiveEffects(StunQuery);
	for (const FActiveGameplayEffectHandle& Handle : StunEffects)
	{
		OutlierAbilitySystemComponent->RemoveActiveEffectFromSelf(Handle);
	}

	// 마지막 태그 제거 콜백이 먼저 복구했으면 아무 일도 하지 않는다.
	// 이미 태그가 없고 CombatState만 남은 경우도 같은 복구 경로로 정리한다.
	RestoreStateAfterStun();
}

void AEnemyBase::RestoreStateAfterStun()
{
	if (!HasAuthority())
	{
		return;
	}

	if (CombatState != EEnemyCombatState::Stun || HasActiveStunTag())
	{
		return;
	}

	// 빙의 중에는 스턴 전 AI 전투 상태를 되살리지 않는다.
	CombatState = bIsPossessed ? EEnemyCombatState::NonCombat : PreStunCombatState;
	bInCombat = CombatState == EEnemyCombatState::Combat;
	RefreshCombatStatePresentation();
	RefreshPerceptionConfigForCurrentState();
	// 스턴 진입 시 지운 추적을 현재 감지로 먼저 복구해야 StateTree가 전투 대신 수색을 고르지 않는다.
	RefreshTargetTracking();

	// StunEnded의 AI 복귀 전이 대신 빙의 상태를 다시 선택한다. 빙의 후 신규 스턴 만료도 동일하다.
	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			bIsPossessed
				? TEXT("Enemy.Event.Possession.Started")
				: TEXT("Enemy.Event.Status.StunEnded")));
	// 복귀 상태를 먼저 확정한 뒤 원래 사건을 평가한다. 해제 시각으로 단서/경계 수명을 다시 시작하지 않는다.
	TickHitResponse(0.0f);
}

bool AEnemyBase::ApplyDamageInternal(
	float DamageAmount,
	AController* DamageInstigator,
	AActor* DamageCauser,
	const FGameplayTag& DamageTag)
{
	if (!HasAuthority() || DamageAmount <= 0.0f || IsDead() || !OutlierAbilitySystemComponent)
	{
		return false;
	}

	return OutlierAbilitySystemComponent->ApplyDamageToSelf(
		DamageAmount,
		DamageInstigator,
		DamageCauser,
		DamageTag);
}

FVector AEnemyBase::GetAutoAimBodyLocation() const
{
	// Anchor 기본 Transform은 항등값이므로, 기존 Offset만 설정된 BP도 같은 위치를 사용한다.
	const FTransform& BodyTransform = AutoAimBodyAnchor
		? AutoAimBodyAnchor->GetComponentTransform() : GetMesh()->GetComponentTransform();
	return BodyTransform.TransformPosition(AutoAimBodyLocalOffset);
}

float AEnemyBase::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	if (!HasAuthority() || !CanBeDamaged() || Request.DamageAmount <= 0.0f || IsDead() || GetCurrentHealth() <= 0.0f)
	{
		return 0.0f;
	}
	const double HitOccurredAt = GetWorld()->GetTimeSeconds();
	ON_SCOPE_EXIT { TickHitResponse(0.0f); };
	// 피해·내성 부가 효과는 GAS 콜백을 동기 호출할 수 있다. 전체 처리가 끝나기 전에는 조사를 시작하지 않는다.
	TGuardValue<bool> DamageResponseGuard(bProcessingDamageHitResponse, true);

	float HitMultiplier = 1.0f;
	float AdaptationMultiplier = 1.0f;
	int32 AdaptationStackBeforeDamage = 0;
	bool bCoreWeakPointHit = false;
	// 자동 보정 여부와 관계없이 실제 피격 부위로 약점 배율을 결정한다.
	if (Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon()))
	{
		const UPrimitiveComponent* HitComponent = Request.HitResult.GetComponent();
		bCoreWeakPointHit = bUseCoreWeakPoint && HitComponent == CoreHitboxComponent;
		HitMultiplier = GetWeakPointDamageMultiplier(HitComponent);
	}
	const bool bAdaptationGunDamage =
		Request.AdaptationDamageCategory == EOutlierAdaptationDamageCategory::Gun
		&& HasEnemyTrait(OutlierGameplayTags::Enemy::Adaptation::Target());
	if (bAdaptationGunDamage)
	{
		if (const UEnemyAdaptationSubsystem* AdaptationSubsystem =
			GetEnemyAdaptationSubsystem())
		{
			AdaptationStackBeforeDamage =
				AdaptationSubsystem->GetCurrentGunAdaptationStack();
			AdaptationMultiplier = AdaptationSubsystem->GetCurrentGunDamageMultiplier();
		}
	}

	const float PreviousHealth = GetCurrentHealth();
	const float DamageMultiplier = HitMultiplier * AdaptationMultiplier;
	const float FinalDamage = Request.DamageAmount * FMath::Max(DamageMultiplier, 0.0f);
	const EOutlierAdaptationDamageCategory PreviousDamageCategory =
		LastAcceptedAdaptationDamageCategory;
	const TSharedPtr<FOutlierStealthSession> PreviousStealthSession = LastAcceptedStealthSession;
	const TWeakObjectPtr<AShooterCharacter> PreviousDirectShooter = LastAcceptedDirectShooter;
	const AWeaponBase* DirectWeapon = Cast<AWeaponBase>(Request.DamageCauser);
	AShooterCharacter* DirectShooter = DirectWeapon
		? Cast<AShooterCharacter>(DirectWeapon->GetOwner()) : nullptr;
	const bool bDirectShooterAttack = DirectShooter && !Request.bReflectedDamage && !bIsPossessed
		&& GetGenericTeamId().GetId() == OutlierTeamIds::Enemy
		&& (Request.DamageTag.MatchesTagExact(OutlierGameplayTags::Damage::Ranged())
			|| Request.DamageTag.MatchesTagExact(OutlierGameplayTags::Damage::Melee()));
	const bool bDirectStealthAttack = bDirectShooterAttack && Request.StealthSession
		&& Request.StealthSession->Shooter.Get() == DirectShooter;
	// GAS의 Health 변경 콜백은 ApplyDamageInternal 안에서 즉시 사망 처리를 호출한다.
	// 이번 공격 분류를 먼저 기록해야 사망 경로가 정확한 최종 원인을 소비할 수 있다.
	LastAcceptedAdaptationDamageCategory = Request.AdaptationDamageCategory;
	// 사망 콜백이 동기 실행되므로 막타의 직접 공격 자격도 피해 적용 전에 교체한다.
	// 환경/폭발/Partner 피해는 이전 공격의 자격을 상속하지 않는다.
	LastAcceptedStealthSession = bDirectStealthAttack ? Request.StealthSession : nullptr;
	LastAcceptedDirectShooter = bDirectShooterAttack ? DirectShooter : nullptr;
	const bool bDamageApplied = ApplyDamageInternal(
		FinalDamage,
		Request.EventInstigator,
		Request.DamageCauser,
		Request.DamageTag);
	if (!bDamageApplied)
	{
		LastAcceptedAdaptationDamageCategory = PreviousDamageCategory;
		LastAcceptedStealthSession = PreviousStealthSession;
		LastAcceptedDirectShooter = PreviousDirectShooter;
	}
	else if (!IsDead())
	{
		// 비치명타의 자격이 나중의 스크립트 사망이나 수명 종료에 재사용되지 않게 한다.
		LastAcceptedStealthSession.Reset();
		LastAcceptedDirectShooter.Reset();
	}
	const float AppliedDamage = bDamageApplied ? FinalDamage : 0.0f;
	if (bAdaptationGunDamage)
	{
		UE_LOG(
			LogOutlier,
			Display,
			TEXT("[EnemyAdaptation] Gun damage applied Enemy=%s Stack=%d RawDamage=%.2f HitMultiplier=%.3f AdaptationMultiplier=%.3f FinalDamage=%.2f Applied=%s HP=%.2f->%.2f"),
			*GetNameSafe(this),
			AdaptationStackBeforeDamage,
			Request.DamageAmount,
			HitMultiplier,
			AdaptationMultiplier,
			FinalDamage,
			bDamageApplied ? TEXT("true") : TEXT("false"),
			PreviousHealth,
			GetCurrentHealth());
	}

	if (AppliedDamage > 0.0f
		&& !IsDead()
		&& Request.AdaptationDamageCategory == EOutlierAdaptationDamageCategory::Pistol)
	{
		FEnemyAdaptationUpdateResult AdaptationResult;
		if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem();
			AdaptationSubsystem && AdaptationSubsystem->ReportPistolHit(
				this,
				GetCombatGameplayGeneration(),
				PoolLeaseSerial,
				false,
				AdaptationResult))
		{
			// 이 권총 명중은 이미 내성 파괴로 소비됐다. 이후 별도 사망 원인으로 재사용하지 않는다.
			LastAcceptedAdaptationDamageCategory = EOutlierAdaptationDamageCategory::Ignore;
		}
	}
	float AppliedHitStunDuration = 0.0f;
	if (AppliedDamage > 0.0f
		&& !IsDead()
		&& Request.StunDurationSeconds > 0.0f
		&& Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon()))
	{
		const FActiveGameplayEffectHandle StunHandle = OutlierAbilitySystemComponent->ApplyStunStateToSelf(
			Request.StunDurationSeconds,
			Request.DamageCauser);
		const bool bStunApplied = StunHandle.IsValid();
		// 요청 기본값 대신 이번 명중으로 실제 적용된 효과의 최종 시간을 사용한다.
		// 다른 원인의 기존 스턴/이후 연장은 이 사건의 수명에 합산하지 않는다.
		if (const FActiveGameplayEffect* StunEffect = OutlierAbilitySystemComponent->GetActiveGameplayEffect(StunHandle))
		{
			const float Duration = StunEffect->Spec.GetDuration();
			if (FMath::IsFinite(Duration) && Duration > 0.0f)
			{
				AppliedHitStunDuration = Duration;
			}
		}
		// 위 내성 파괴가 LastAccepted 카테고리를 소비할 수 있으므로 원인은 요청 기준으로 판정한다.
		if (bStunApplied && Request.AdaptationDamageCategory == EOutlierAdaptationDamageCategory::Pistol)
		{
			UOutlierEventSubsystem::EventCallOnce(this, StunEventTag);
		}
	}
	// 권총도 같은 사건 저장소를 사용한다. 스턴 중에는 저장만 하고 복귀 후 한 번 확인한다.
	if (AppliedDamage > 0.0f && !IsDead() && GetCurrentHealth() > 0.0f && Request.bHasDamageOrigin)
	{
		FEnemyHitClue Clue;
		float Lifetime = FMath::IsFinite(RuntimeStat.HitClueLifetime) && RuntimeStat.HitClueLifetime > 0.0f
			? RuntimeStat.HitClueLifetime : 6.0f;
		if (Request.AdaptationDamageCategory == EOutlierAdaptationDamageCategory::Pistol)
		{
			const float Grace = FMath::IsFinite(RuntimeStat.PistolHitClueGraceSeconds) && RuntimeStat.PistolHitClueGraceSeconds > 0.0f
				? RuntimeStat.PistolHitClueGraceSeconds : 1.0f;
			Lifetime = AppliedHitStunDuration + Grace;
		}
		if (CreateHitClue(Request.DamageOrigin, HitOccurredAt, Lifetime, Clue))
		{
			RecordHitClue(Clue);
		}
	}
	if (bCoreWeakPointHit)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyWeakPoint] Type=Core Actor=%s Component=%s RawDamage=%.2f Multiplier=%.2f AppliedDamage=%.2f HP=%.2f->%.2f"),
			*GetNameSafe(this),
			*GetNameSafe(CoreHitboxComponent),
			Request.DamageAmount,
			DamageMultiplier,
			AppliedDamage,
			PreviousHealth,
			GetCurrentHealth());
	}

	if (AppliedDamage > 0.0f)
	{
		PlayAttackerConfirmAudio(Request, IsDead());
		BroadcastDamageCue(Request, AppliedDamage);
	}

	// 피해 판정과 무관한 표현이다. 내성 대상인 총기 피격만 방어막 파문으로 보여준다.
	if (AppliedDamage > 0.0f
		&& bAdaptationGunDamage
		&& !IsDead()
		&& AdaptationShieldComponent
		&& AdaptationShieldComponent->ShouldShowForState(AdaptationState))
	{
		MulticastAdaptationShieldHit(Request.HitResult.bBlockingHit
			? FVector(Request.HitResult.ImpactPoint)
			: (Request.DamageOrigin.IsZero() ? GetActorLocation() : Request.DamageOrigin));
	}

	return AppliedDamage;
}

void AEnemyBase::BroadcastDamageCue(const FOutlierDamageRequest& Request, float AppliedDamage) const
{
	if (!HasAuthority() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	FGameplayCueParameters CueParameters;
	CueParameters.AggregatedTargetTags.AddTag(GetHitAudioContext());
	CueParameters.RawMagnitude = AppliedDamage;
	CueParameters.Instigator = Request.EventInstigator;
	CueParameters.EffectCauser = Request.DamageCauser;

	// The shared hit cue selects melee/ranged/explosion feedback from
	// AggregatedSourceTags; EffectContext supplies the impact location and normal.

	FGameplayEffectContextHandle EffectContext = OutlierAbilitySystemComponent->MakeEffectContext();
	EffectContext.AddInstigator(Request.EventInstigator, Request.DamageCauser);
	if (Request.HitResult.bBlockingHit)
	{
		EffectContext.AddHitResult(Request.HitResult);
		CueParameters.Location = Request.HitResult.ImpactPoint;
		CueParameters.Normal = Request.HitResult.ImpactNormal;
	}
	else
	{
		CueParameters.Location = Request.DamageOrigin.IsZero()
			? GetActorLocation()
			: Request.DamageOrigin;
	}
	CueParameters.EffectContext = EffectContext;

	if (Request.DamageTag.IsValid())
	{
		CueParameters.AggregatedSourceTags.AddTag(Request.DamageTag);
	}

	OutlierAbilitySystemComponent->ExecuteGameplayCue(
		OutlierGameplayTags::Cue::Drone::Hit(),
		CueParameters);
}

void AEnemyBase::BroadcastDeathCue() const
{
	if (!HasAuthority() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	FGameplayCueParameters CueParameters;
	CueParameters.AggregatedTargetTags.AddTag(GetDeathAudioContext());
	CueParameters.Location = GetActorLocation();
	CueParameters.Normal = FVector::UpVector;
	CueParameters.EffectContext = OutlierAbilitySystemComponent->MakeEffectContext();

	OutlierAbilitySystemComponent->ExecuteGameplayCue(
		OutlierGameplayTags::Cue::Drone::Death(),
		CueParameters);
}

float AEnemyBase::GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const
{
	return bUseCoreWeakPoint && HitComponent == CoreHitboxComponent
		? CoreCriticalMultiplier : 1.0f;
}

void AEnemyBase::ApplyCoreWeakPointRuntimeState()
{
	if (!CoreHitboxComponent)
	{
		return;
	}

	CoreHitboxComponent->SetCollisionEnabled(
		bUseCoreWeakPoint ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AEnemyBase::ApplyExplosionReaction(
	const FVector& ExplosionOrigin,
	float EnemyImpulseScale,
	float TurretReactionScale,
	float EffectRatio)
{
	if (!HasAuthority() || EffectRatio <= 0.0f || IsDead())
	{
		return;
	}

	const FVector Direction = (GetActorLocation() - ExplosionOrigin).GetSafeNormal();
	if (RuntimeStat.Type == EEnemyType::Turret)
	{
		// 고정형 Turret은 이동시키지 않고 BP 연출에 필요한 방향과 강도만 전달한다.
		MulticastExplosionReaction(Direction, FMath::Max(TurretReactionScale, 0.0f) * EffectRatio);
		return;
	}

	const float ImpulseStrength = FMath::Max(EnemyImpulseScale, 0.0f) * EffectRatio;
	if (ImpulseStrength <= 0.0f)
	{
		MulticastExplosionReaction(Direction, EffectRatio);
		return;
	}

	const float ResistanceRatio = FMath::Clamp(
		RuntimeImpactReactionProfile.ResistanceRatio,
		0.0f,
		1.0f);
	const FVector ImpactVelocity =
		Direction * ImpulseStrength * (1.0f - ResistanceRatio);
	if (ImpactVelocity.Size() >= RuntimeImpactReactionProfile.MinReactionStrength)
	{
		InterruptDamagePresentation(0.05f);
	}
	if (IsEnemyImpactReactionDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyImpactDiag] ExplosionReceived Enemy=%s Origin=%s EffectRatio=%.3f ImpulseScale=%.2f Resistance=%.3f ImpactVelocity=%s ImpactStrength=%.2f Active=%s Possessed=%s"),
			*GetNameSafe(this),
			*ExplosionOrigin.ToCompactString(),
			EffectRatio,
			EnemyImpulseScale,
			ResistanceRatio,
			*ImpactVelocity.ToCompactString(),
			ImpactVelocity.Size(),
			bImpactReactionActive ? TEXT("true") : TEXT("false"),
			bIsPossessed ? TEXT("true") : TEXT("false"));
	}

	// 자폭 전조가 확정된 드론은 빙의 여부와 무관하게 돌진 방향과 반동 속도를 합성한다.
	if (TryApplyCommittedImpactVelocity(ImpactVelocity))
	{
		if (IsEnemyImpactReactionDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] CommittedImpactApplied Enemy=%s CommonRecoveryState=false Reason=CommittedAction"),
				*GetNameSafe(this));
		}
		MulticastExplosionReaction(Direction, EffectRatio);
		return;
	}

	// 직접 빙의된 V.E.C.는 StateTree를 전환하지 않고 Pawn에서 물리 반동만 회복한다.
	if (bIsPossessed)
	{
		AccumulateImpactVelocity(ImpactVelocity);
		if (CurrentImpactStrength >= RuntimeImpactReactionProfile.MinReactionStrength)
		{
			bImpactReactionActive = true;
			if (UCharacterMovementComponent* Movement = GetCharacterMovement())
			{
				Movement->SetMovementMode(MOVE_Flying);
				Movement->MaxFlySpeed = FMath::Max(
					GetRuntimeStat().MoveSpeed,
					AccumulatedImpactVelocity.Size());
				Movement->Velocity = AccumulatedImpactVelocity;
			}
			BeginPossessedImpactInputLock();
		}
		else
		{
			AccumulatedImpactVelocity = FVector::ZeroVector;
			CurrentImpactStrength = 0.0f;
			CurrentPhysicalKnockbackDuration = 0.0f;
			CurrentControlRecoveryDuration = 0.0f;
		}

		if (IsEnemyImpactReactionDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] PossessedImpactApplied Enemy=%s PawnRecovery=%s Strength=%.2f PhysicalDuration=%.3f"),
				*GetNameSafe(this),
				bImpactReactionActive ? TEXT("true") : TEXT("false"),
				CurrentImpactStrength,
				CurrentPhysicalKnockbackDuration);
		}
		MulticastExplosionReaction(Direction, EffectRatio);
		return;
	}

	AccumulateImpactVelocity(ImpactVelocity);
	if (!bImpactReactionActive
		&& CurrentImpactStrength < RuntimeImpactReactionProfile.MinReactionStrength)
	{
		// 반응 기준 미만의 단발 충격은 연출만 재생하고 다음 피격까지 속도를 남기지 않는다.
		AccumulatedImpactVelocity = FVector::ZeroVector;
		CurrentImpactStrength = 0.0f;
		CurrentPhysicalKnockbackDuration = 0.0f;
		CurrentControlRecoveryDuration = 0.0f;
		if (IsEnemyImpactReactionDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] ImpactIgnored Enemy=%s Reason=BelowThreshold MinStrength=%.2f"),
				*GetNameSafe(this),
				RuntimeImpactReactionProfile.MinReactionStrength);
		}
	}
	else if (!bImpactReactionActive)
	{
		SendEnemyStateTreeEvent(
			FGameplayTag::RequestGameplayTag(
				TEXT("Enemy.Event.Status.ImpactStarted")));
		if (IsEnemyImpactReactionDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] ImpactEventSent Enemy=%s Event=Enemy.Event.Status.ImpactStarted Strength=%.2f"),
				*GetNameSafe(this),
				CurrentImpactStrength);
		}
	}
	MulticastExplosionReaction(Direction, EffectRatio);
}

void AEnemyBase::ApplyMagneticPullVelocity(const FVector& PullVelocity, AActor* SourceActor)
{
	if (!HasAuthority() || IsDead() || PullVelocity.IsNearlyZero())
	{
		return;
	}

	bMagneticPullActive = true;
	MagneticPullSource = SourceActor;
	AccumulatedImpactVelocity = PullVelocity;
	ImpactRecoveryElapsedTime = 0.0f;
	RefreshImpactReactionDuration();

	if (!bImpactReactionActive)
	{
		// 타워가 매 틱 호출하므로, 거부될 게 확실한 동안(빙의 대기 등)은 진입 이벤트를 보내지 않는다.
		// 막던 상태가 풀리면 다음 틱 호출에서 다시 시도된다.
		if (CanBeginImpactReaction())
		{
			SendEnemyStateTreeEvent(
				FGameplayTag::RequestGameplayTag(
					TEXT("Enemy.Event.Status.ImpactStarted")));
		}
		return;
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxFlySpeed = FMath::Max(
			GetRuntimeStat().MoveSpeed,
			AccumulatedImpactVelocity.Size());
		Movement->Velocity = AccumulatedImpactVelocity;
	}
}

void AEnemyBase::EndMagneticPull(AActor* SourceActor)
{
	if (!HasAuthority()
		|| !bMagneticPullActive
		|| (MagneticPullSource.IsValid() && MagneticPullSource.Get() != SourceActor))
	{
		return;
	}

	bMagneticPullActive = false;
	MagneticPullSource.Reset();
	EndImpactReaction();
}

void AEnemyBase::AccumulateImpactVelocity(const FVector& ImpactVelocity)
{
	const float PreviousStrength = AccumulatedImpactVelocity.Size();
	ImpactRecoveryElapsedTime = 0.0f;
	const float MaxStrength = FMath::Max(
		RuntimeImpactReactionProfile.MaxAccumulatedStrength,
		0.0f);
	AccumulatedImpactVelocity = MaxStrength > 0.0f
		? (AccumulatedImpactVelocity + ImpactVelocity).GetClampedToMaxSize(MaxStrength)
		: FVector::ZeroVector;

	if (bImpactReactionActive)
	{
		// 활성 반동 중 추가 충격은 CharacterMovement가 다음 Sweep에서 바로 반영하도록 속도도 갱신한다.
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->MaxFlySpeed = FMath::Max(
				GetRuntimeStat().MoveSpeed,
				AccumulatedImpactVelocity.Size());
			Movement->Velocity = AccumulatedImpactVelocity;
		}
	}

	RefreshImpactReactionDuration();
	if (IsEnemyImpactReactionDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyImpactDiag] ImpactAccumulated Enemy=%s Strength=%.2f->%.2f PhysicalDuration=%.3f ControlDuration=%.3f Active=%s"),
			*GetNameSafe(this),
			PreviousStrength,
			CurrentImpactStrength,
			CurrentPhysicalKnockbackDuration,
			CurrentControlRecoveryDuration,
			bImpactReactionActive ? TEXT("true") : TEXT("false"));
	}
}

void AEnemyBase::RefreshImpactReactionDuration()
{
	CurrentImpactStrength = AccumulatedImpactVelocity.Size();
	const float MinStrength = FMath::Max(
		RuntimeImpactReactionProfile.MinReactionStrength,
		0.0f);
	const float MaxStrength = FMath::Max(
		RuntimeImpactReactionProfile.MaxAccumulatedStrength,
		MinStrength);
	const float StrengthAlpha = MaxStrength > MinStrength
		? FMath::Clamp(
			(CurrentImpactStrength - MinStrength) / (MaxStrength - MinStrength),
			0.0f,
			1.0f)
		: 1.0f;
	const float MinPhysicalDuration = FMath::Max(
		RuntimeImpactReactionProfile.MinPhysicalKnockbackDuration,
		0.0f);
	const float MaxPhysicalDuration = FMath::Max(
		RuntimeImpactReactionProfile.MaxPhysicalKnockbackDuration,
		MinPhysicalDuration);
	CurrentPhysicalKnockbackDuration = FMath::Lerp(
		MinPhysicalDuration,
		MaxPhysicalDuration,
		StrengthAlpha);

	const float MinControlDuration = FMath::Max(
		RuntimeImpactReactionProfile.MinControlRecoveryDuration,
		CurrentPhysicalKnockbackDuration);
	const float MaxControlDuration = FMath::Max(
		RuntimeImpactReactionProfile.MaxControlRecoveryDuration,
		MinControlDuration);
	CurrentControlRecoveryDuration = FMath::Lerp(
		MinControlDuration,
		MaxControlDuration,
		StrengthAlpha);
}

bool AEnemyBase::CanBeginImpactReaction() const
{
	return HasAuthority()
		&& !IsDead()
		&& !bIsPossessed
		&& !IsPossessionInProgress()
		&& CurrentImpactStrength >= RuntimeImpactReactionProfile.MinReactionStrength;
}

bool AEnemyBase::BeginImpactReaction()
{
	if (!CanBeginImpactReaction())
	{
		if (IsEnemyImpactReactionDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] RecoveryBeginRejected Enemy=%s Authority=%s Health=%.2f Possessed=%s PossessionPending=%s Strength=%.2f MinStrength=%.2f"),
				*GetNameSafe(this),
				HasAuthority() ? TEXT("true") : TEXT("false"),
				GetCurrentHealth(),
				bIsPossessed ? TEXT("true") : TEXT("false"),
				IsPossessionInProgress() ? TEXT("true") : TEXT("false"),
				CurrentImpactStrength,
				RuntimeImpactReactionProfile.MinReactionStrength);
		}
		return false;
	}

	InterruptDamagePresentation(0.05f);
	bImpactReactionActive = true;
	ReleasePatrolDestination();
	StopCurrentAttack();
	ReleaseSearchDestinations();

	AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetController());
	if (!EnemyAIController)
	{
		EnemyAIController = Cast<AEnemyAIController>(CachedAIController.Get());
	}
	if (EnemyAIController)
	{
		EnemyAIController->StopMovement();
		EnemyAIController->SetEnemyPerceptionEnabled(false);
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Flying);
		Movement->MaxFlySpeed = FMath::Max(
			GetRuntimeStat().MoveSpeed,
			AccumulatedImpactVelocity.Size());
		Movement->Velocity = AccumulatedImpactVelocity;
	}
	if (IsEnemyImpactReactionDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyImpactDiag] RecoveryStateBegin Enemy=%s Strength=%.2f InertiaHold=%.3f PhysicalDuration=%.3f ControlDuration=%.3f"),
			*GetNameSafe(this),
			CurrentImpactStrength,
			RuntimeImpactReactionProfile.InitialInertiaHoldDuration,
			CurrentPhysicalKnockbackDuration,
			CurrentControlRecoveryDuration);
	}
	return true;
}

bool AEnemyBase::UpdateImpactRecovery(float DeltaTime, float ElapsedTime)
{
	(void)ElapsedTime;
	if (!HasAuthority() || !bImpactReactionActive || IsDead())
	{
		return true;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		if (IsEnemyImpactReactionDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] RecoveryUpdateEnded Enemy=%s Reason=MissingCharacterMovement"),
				*GetNameSafe(this));
		}
		return true;
	}

	if (bMagneticPullActive)
	{
		Movement->MaxFlySpeed = FMath::Max(
			GetRuntimeStat().MoveSpeed,
			AccumulatedImpactVelocity.Size());
		Movement->Velocity = AccumulatedImpactVelocity;
		CurrentImpactStrength = AccumulatedImpactVelocity.Size();
		return false;
	}
	const float PreviousElapsedTime = ImpactRecoveryElapsedTime;
	const float PreviousStrength = CurrentImpactStrength;
	ImpactRecoveryElapsedTime += FMath::Max(DeltaTime, 0.0f);
	const float InertiaHoldDuration = FMath::Max(
		RuntimeImpactReactionProfile.InitialInertiaHoldDuration,
		0.0f);

	// CharacterMovement가 직전 프레임 충돌면을 따라 보정한 속도를 다음 감속의 기준으로 사용한다.
	if (ImpactRecoveryElapsedTime >= InertiaHoldDuration
		&& ImpactRecoveryElapsedTime < CurrentPhysicalKnockbackDuration)
	{
		const float Damping = FMath::Max(RuntimeImpactReactionProfile.KnockbackDamping, 0.0f);
		AccumulatedImpactVelocity = Movement->Velocity
			* FMath::Exp(-Damping * FMath::Max(DeltaTime, 0.0f));
	}
	else if (ImpactRecoveryElapsedTime >= CurrentPhysicalKnockbackDuration)
	{
		AccumulatedImpactVelocity = FVector::ZeroVector;
	}
	Movement->Velocity = AccumulatedImpactVelocity;
	CurrentImpactStrength = AccumulatedImpactVelocity.Size();

	if (CurrentImpactStrength <= FMath::Max(RuntimeImpactReactionProfile.RecoverySpeedThreshold, 0.0f))
	{
		AccumulatedImpactVelocity = FVector::ZeroVector;
		Movement->Velocity = FVector::ZeroVector;
	}

	if (IsEnemyImpactReactionDiagnosticsEnabled())
	{
		if (InertiaHoldDuration > 0.0f
			&& PreviousElapsedTime < InertiaHoldDuration
			&& ImpactRecoveryElapsedTime >= InertiaHoldDuration)
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] InertiaHoldFinished Enemy=%s Elapsed=%.3f Strength=%.2f"),
				*GetNameSafe(this),
				ImpactRecoveryElapsedTime,
				CurrentImpactStrength);
		}

		if (PreviousElapsedTime < CurrentPhysicalKnockbackDuration
			&& ImpactRecoveryElapsedTime >= CurrentPhysicalKnockbackDuration)
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] PhysicalKnockbackFinished Enemy=%s Elapsed=%.3f ControlDuration=%.3f"),
				*GetNameSafe(this),
				ImpactRecoveryElapsedTime,
				CurrentControlRecoveryDuration);
		}

		const float RecoverySpeedThreshold = FMath::Max(
			RuntimeImpactReactionProfile.RecoverySpeedThreshold,
			0.0f);
		if (PreviousStrength > RecoverySpeedThreshold
			&& CurrentImpactStrength <= RecoverySpeedThreshold)
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] RecoverySpeedReached Enemy=%s Elapsed=%.3f Threshold=%.2f"),
				*GetNameSafe(this),
				ImpactRecoveryElapsedTime,
				RecoverySpeedThreshold);
		}
	}

	const bool bRecoveryComplete =
		ImpactRecoveryElapsedTime >= CurrentControlRecoveryDuration;
	if (bRecoveryComplete && IsEnemyImpactReactionDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyImpactDiag] RecoveryComplete Enemy=%s Elapsed=%.3f ControlDuration=%.3f"),
			*GetNameSafe(this),
			ImpactRecoveryElapsedTime,
			CurrentControlRecoveryDuration);
	}
	return bRecoveryComplete;
}

void AEnemyBase::UpdatePossessedImpactRecovery(float DeltaTime)
{
	if (!HasAuthority() || !bIsPossessed || !bImpactReactionActive)
	{
		return;
	}

	UpdateImpactRecovery(DeltaTime, ImpactRecoveryElapsedTime);
	if (ImpactRecoveryElapsedTime < CurrentPhysicalKnockbackDuration)
	{
		return;
	}

	if (IsEnemyImpactReactionDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyImpactDiag] PossessedRecoveryComplete Enemy=%s Elapsed=%.3f PhysicalDuration=%.3f"),
			*GetNameSafe(this),
			ImpactRecoveryElapsedTime,
			CurrentPhysicalKnockbackDuration);
	}
	EndImpactReaction();
}

void AEnemyBase::EndImpactReaction()
{
	if (!HasAuthority())
	{
		return;
	}

	bMagneticPullActive = false;
	MagneticPullSource.Reset();
	if (!bImpactReactionActive)
	{
		AccumulatedImpactVelocity = FVector::ZeroVector;
		CurrentImpactStrength = 0.0f;
		CurrentPhysicalKnockbackDuration = 0.0f;
		CurrentControlRecoveryDuration = 0.0f;
		ImpactRecoveryElapsedTime = 0.0f;
		return;
	}
	if (IsEnemyImpactReactionDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyImpactDiag] RecoveryStateEnd Enemy=%s Elapsed=%.3f ControlDuration=%.3f Strength=%.2f Completed=%s"),
			*GetNameSafe(this),
			ImpactRecoveryElapsedTime,
			CurrentControlRecoveryDuration,
			CurrentImpactStrength,
			ImpactRecoveryElapsedTime >= CurrentControlRecoveryDuration
				? TEXT("true")
				: TEXT("false"));
	}

	bImpactReactionActive = false;
	AccumulatedImpactVelocity = FVector::ZeroVector;
	CurrentImpactStrength = 0.0f;
	CurrentPhysicalKnockbackDuration = 0.0f;
	CurrentControlRecoveryDuration = 0.0f;
	ImpactRecoveryElapsedTime = 0.0f;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxFlySpeed = FMath::Max(GetRuntimeStat().MoveSpeed, 0.0f);
	}

	AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetController());
	if (!EnemyAIController)
	{
		EnemyAIController = Cast<AEnemyAIController>(CachedAIController.Get());
	}
	if (EnemyAIController
		&& !IsDead()
		&& CombatState != EEnemyCombatState::Stun
		&& !IsAIControlSuppressed())
	{
		EnemyAIController->SetEnemyPerceptionEnabled(CanUseEnemyPerception());
		EnemyAIController->RefreshPerceptionConfigFromPawn();
	}
}

bool AEnemyBase::TryApplyCommittedImpactVelocity(const FVector& ImpactVelocity)
{
	(void)ImpactVelocity;
	return false;
}

void AEnemyBase::CancelCommittedAction()
{
}

void AEnemyBase::MulticastExplosionReaction_Implementation(
	FVector_NetQuantizeNormal Direction,
	float ReactionScale)
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		ApplyExplosionReactionPresentation(Direction, ReactionScale);
	}
}

void AEnemyBase::ApplyExplosionReactionPresentation(
	const FVector& Direction,
	float ReactionScale)
{
	OnExplosionReaction(Direction, ReactionScale);
}

void AEnemyBase::BeginPossessedImpactInputLock()
{
	if (!HasAuthority() || !bIsPossessed)
	{
		return;
	}

	bPossessedImpactInputLocked = true;
	bPossessedAttackQueued = false;
	GetWorldTimerManager().ClearTimer(PossessedImpactInputLockTimerHandle);
	GetWorldTimerManager().SetTimer(
		PossessedImpactInputLockTimerHandle,
		this,
		&AEnemyBase::EndPossessedImpactInputLock,
		FMath::Max(RuntimeImpactReactionProfile.InputLockDuration, KINDA_SMALL_NUMBER),
		false);
}

void AEnemyBase::EndPossessedImpactInputLock()
{
	GetWorldTimerManager().ClearTimer(PossessedImpactInputLockTimerHandle);
	const bool bShouldResumeHeldAttack = HasAuthority()
		&& bPossessedImpactInputLocked
		&& bIsPossessed
		&& bPossessedAttackHeld
		&& !IsDead();
	bPossessedImpactInputLocked = false;

	if (bShouldResumeHeldAttack)
	{
		bPossessedAttackQueued = true;
		SendEnemyStateTreeEvent(
			FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Attack.InputPressed")));
	}
}

void AEnemyBase::OnRep_RuntimeStat()
{
	ApplyMovementFromRuntimeStat();
}

void AEnemyBase::InitializeGasVitality()
{
	if (!HasAuthority() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	const float InitialHealth = FMath::Max(RuntimeStat.Health, 0.0f);
	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierVitalAttributeSet::GetMaxHealthAttribute(),
		InitialHealth);
	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierVitalAttributeSet::GetHealthAttribute(),
		InitialHealth);
}

void AEnemyBase::BindGasVitalityObservers()
{
	if (!OutlierAbilitySystemComponent)
	{
		return;
	}

	if (!HealthChangedHandle.IsValid())
	{
		HealthChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).AddUObject(
				this, &AEnemyBase::HandleHealthChanged);
	}

	if (!StunnedTagChangedHandle.IsValid())
	{
		StunnedTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
			OutlierGameplayTags::State::Stunned(),
			EGameplayTagEventType::NewOrRemoved).AddUObject(
				this, &AEnemyBase::HandleStunnedTagChanged);
	}
	if (!DeadTagChangedHandle.IsValid())
	{
		// Health/CombatState와 Dead 태그의 복제 순서는 다를 수 있다.
		// 태그 자체도 관찰해야 고정 조명이 다음 Actor 복제까지 남지 않는다.
		DeadTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
			OutlierGameplayTags::State::Dead(),
			EGameplayTagEventType::NewOrRemoved).AddUObject(
				this, &AEnemyBase::HandleDeadTagChanged);
	}
}

void AEnemyBase::UnbindGasVitalityObservers()
{
	if (!OutlierAbilitySystemComponent)
	{
		return;
	}

	if (HealthChangedHandle.IsValid())
	{
		OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		HealthChangedHandle.Reset();
	}

	if (StunnedTagChangedHandle.IsValid())
	{
		OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
			OutlierGameplayTags::State::Stunned(),
			EGameplayTagEventType::NewOrRemoved).Remove(StunnedTagChangedHandle);
		StunnedTagChangedHandle.Reset();
	}
	if (DeadTagChangedHandle.IsValid())
	{
		OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
			OutlierGameplayTags::State::Dead(),
			EGameplayTagEventType::NewOrRemoved).Remove(DeadTagChangedHandle);
		DeadTagChangedHandle.Reset();
	}
}

void AEnemyBase::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	RefreshCombatStatePresentation();
	if (!HasAuthority() && ChangeData.NewValue <= 0.0f)
	{
		InterruptDamagePresentation(0.0f);
	}

	if (HasAuthority() && ChangeData.OldValue > 0.0f && ChangeData.NewValue <= 0.0f)
	{
		Die();
	}

	if (!bIsPossessed || !IsLocallyControlled() || ChangeData.NewValue >= ChangeData.OldValue)
	{
		return;
	}

	UWorld* World = GetWorld();
	const float MaxHealth = VitalAttributeSet ? VitalAttributeSet->GetMaxHealth() : 0.0f;
	if (!World || MaxHealth <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PPS = World->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		const float HealthRatio = FMath::Clamp(ChangeData.NewValue / MaxHealth, 0.0f, 1.0f);
		PPS->UpdateDamagedPostProcess(HealthRatio, FVector4(0.0f, 0.0f, 1.0f, 0.0f));
		PPS->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, true);
	}
}

void AEnemyBase::HandleStunnedTagChanged(const FGameplayTag StunnedTag, int32 NewCount)
{
	// 태그가 먼저 도착한 클라이언트는 즉시 소등하고, 태그 해제 때도 표현을 재평가한다.
	// 서버의 실제 상태 복원은 아래 기존 경로가 계속 소유한다.
	RefreshCombatStatePresentation();
	// 복제된 스턴도 로컬 피격 연출을 정리한다. 실제 전투 상태 변경은 아래 서버 경로만 담당한다.
	if (!HasAuthority() && StunnedTag == OutlierGameplayTags::State::Stunned() && NewCount > 0)
	{
		InterruptDamagePresentation(0.05f);
	}

	if (!HasAuthority() || StunnedTag != OutlierGameplayTags::State::Stunned())
	{
		return;
	}

	if (NewCount > 0)
	{
		EnterStun();
	}
	else if (!HasActiveStunTag())
	{
		RestoreStateAfterStun();
	}
}

void AEnemyBase::HandleDeadTagChanged(const FGameplayTag DeadTag, int32 NewCount)
{
	RefreshCombatStatePresentation();
}

void AEnemyBase::Die()
{
	if (!HasAuthority() || IsDead() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	if (OutlierAbilitySystemComponent->ApplyDeadStateToSelf())
	{
		// HandleDeath() 는 기본적으로 같은 프레임에 Destroy() 까지 간다( GetDeathDestroyDelay 기본 0 ).
		// 브로드캐스트가 그보다 먼저 나가야 한다.
		BroadcastDeathCue();
		HandleDeath();
	}
}

UHackableComponent* AEnemyBase::GetHackableComponent() const
{
	return HackableComponent;
}

void AEnemyBase::HandleHackCompleted(const FHackResultContext& Context)
{
	if (HasAuthority()
		&& IsPossessionInProgress()
		&& Context.Result != EHackResult::Success)
	{
		CancelPossessionProcess();
	}
}

void AEnemyBase::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	if (!HasAuthority())
	{
		return;
	}

	if (EffectTag != HackGameplayTags::Effect::Possess())
	{
		return;
	}

	if (IsEnemyPossessed() || !IsPossessionInProgress())
	{
		return;
	}

	if (!HackableComponent)
	{
		return;
	}
	if (Context.Result != EHackResult::Success)
	{
		CancelPossessionProcess();
		return;
	}

	APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(Context.InstigatorActor);
	if (!PartnerCharacter || PossessionInstigatorPartner.Get() != PartnerCharacter)
	{
		CancelPossessionProcess();
		return;
	}

	APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(PartnerCharacter->GetController());
	if (!PartnerController)
	{
		CancelPossessionProcess();
		return;
	}

	if (!PartnerController->BeginEnemyPossessionTransition(this, PartnerCharacter))
	{
		CancelPossessionProcess();
		return;
	}

	ConfirmPossessionProcess();
}

void AEnemyBase::HandleHackStarted(const FHackQueryContext& Context)
{
	if (!HasAuthority() || !HackableComponent || IsAIControlSuppressed())
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[EnemyPossessedAttackDiag] HackStartRejected Enemy=%s Authority=%s Hackable=%s "
					"AISuppressed=%s Possessed=%s Pending=%s Impact=%s"),
				*GetNameSafe(this),
				HasAuthority() ? TEXT("true") : TEXT("false"),
				*GetNameSafe(HackableComponent),
				IsAIControlSuppressed() ? TEXT("true") : TEXT("false"),
				bIsPossessed ? TEXT("true") : TEXT("false"),
				IsPossessionInProgress() ? TEXT("true") : TEXT("false"),
				bImpactReactionActive ? TEXT("true") : TEXT("false"));
		}
		return;
	}

	APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(Context.InstigatorActor);
	if (!IsValid(PartnerCharacter))
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[EnemyPossessedAttackDiag] HackStartRejected Enemy=%s Reason=InvalidPartner Instigator=%s"),
				*GetNameSafe(this),
				*GetNameSafe(Context.InstigatorActor));
		}
		return;
	}

	if (!BeginPossessionProcess(PartnerCharacter))
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[EnemyPossessedAttackDiag] HackStartRejected Enemy=%s Reason=BeginPossessionProcessFailed"),
				*GetNameSafe(this));
		}
		return;
	}

	if (HackableComponent)
	{
		HackableComponent->HackTags.RemoveTag(HackGameplayTags::Target::Possessable());
		HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	}
}

UEMPableComponent* AEnemyBase::GetEMPableComponent() const
{
	return EmpableComponent;
}

//Partner EmpComponent Timer Delegate.
void AEnemyBase::HandleEMPStarted(FGameplayTag EffectTag)
{
	if (EffectTag == OutlierGameplayTags::State::Stunned()
		&& HasAuthority()
		&& HasActiveStunTag())
	{
		EnterStun();
	}
}

void AEnemyBase::HandleEMPEnded(FGameplayTag EffectTag)
{
	if (EffectTag == OutlierGameplayTags::State::Stunned()
		&& HasAuthority()
		&& !HasActiveStunTag())
	{
		RestoreStateAfterStun();
	}
}

int32 AEnemyBase::GetScanStencilValue() const
{
	return static_cast<int32>(EScanType::Enemy);
}

bool AEnemyBase::CanShowMeleeTargetIndicator_Implementation(const AActor* /*InstigatorActor*/) const
{
	return !IsDead()
		&& CanBeDamaged()
		&& GetGenericTeamId().GetId() == OutlierTeamIds::Enemy;
}

void AEnemyBase::SetMeleeTargeted_Implementation(AActor* InstigatorActor, bool bTargeted)
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(InstigatorActor);
	AShooterPlayerController* ShooterController = Shooter
		? Cast<AShooterPlayerController>(Shooter->GetController()) : nullptr;
	if (IsValid(ShooterController))
	{
		// 적 액터는 클라이언트 소유가 아니므로 RPC는 Shooter의 Controller를 통해 보낸다.
		ShooterController->ClientNotifyMeleeTargeted(this, Shooter, bTargeted);
	}
}

void AEnemyBase::SetDefaultEnemyType(EEnemyType EnemyType)
{
	RuntimeStat.Type = EnemyType;
}

void AEnemyBase::PrepareForStateTreeStart()
{
}

void AEnemyBase::ApplyClassStatOverrides()
{
}

void AEnemyBase::ApplyMovementFromRuntimeStat()
{
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->MaxFlySpeed = FMath::Max(RuntimeStat.MoveSpeed, 0.0f);
	}
}

bool AEnemyBase::HasActiveStunTag() const
{
	return OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->HasMatchingGameplayTag(
			OutlierGameplayTags::State::Stunned());
}

bool AEnemyBase::ApplyPossessionPendingState()
{
	if (!HasAuthority() || !OutlierAbilitySystemComponent || PossessionPendingEffectHandle.IsValid())
	{
		UE_LOG(
			LogOutlier,
			Error,
			TEXT("[Enemy.Possession] Cannot apply PossessPending. Enemy=%s Authority=%d ASC=%s ExistingHandle=%d"),
			*GetNameSafe(this),
			HasAuthority() ? 1 : 0,
			*GetNameSafe(OutlierAbilitySystemComponent),
			PossessionPendingEffectHandle.IsValid() ? 1 : 0);
		return false;
	}

	PossessionPendingEffectHandle =
		OutlierAbilitySystemComponent->ApplyPossessPendingStateToSelf();
	if (!PossessionPendingEffectHandle.IsValid()
		|| OutlierAbilitySystemComponent->GetGameplayTagCount(
			OutlierGameplayTags::State::PossessPending()) != 1)
	{
		UE_LOG(
			LogOutlier,
			Error,
			TEXT("[Enemy.Possession] PossessPending GameplayEffect was rejected. Enemy=%s Owner=%s Avatar=%s TagCount=%d"),
			*GetNameSafe(this),
			*GetNameSafe(OutlierAbilitySystemComponent->GetOwnerActor()),
			*GetNameSafe(OutlierAbilitySystemComponent->GetAvatarActor()),
			OutlierAbilitySystemComponent->GetGameplayTagCount(
				OutlierGameplayTags::State::PossessPending()));
		if (PossessionPendingEffectHandle.IsValid())
		{
			OutlierAbilitySystemComponent->RemoveActiveEffectFromSelf(
				PossessionPendingEffectHandle);
			PossessionPendingEffectHandle.Invalidate();
		}
		return false;
	}
	return true;
}

bool AEnemyBase::RemovePossessionPendingState()
{
	if (!HasAuthority() || !OutlierAbilitySystemComponent || !PossessionPendingEffectHandle.IsValid())
	{
		PossessionPendingEffectHandle = FActiveGameplayEffectHandle();
		return false;
	}

	const bool bRemoved =
		OutlierAbilitySystemComponent->RemoveActiveEffectFromSelf(PossessionPendingEffectHandle);
	PossessionPendingEffectHandle = FActiveGameplayEffectHandle();
	return bRemoved;
}

void AEnemyBase::InitializeCombatMovementPreferences()
{
	CombatOrbitTarget.Reset();
	CombatOrbitController.Reset();
	// 일반 진입과 스턴 중 승격 모두 같은 성향을 정하고 해당 전투 동안 유지한다.
	bPrefersCombatLeft = FMath::RandBool();
	CombatChaseLane = FMath::RandRange(-1, 1);
	CombatHeightPreference = EEnemyFlightHeightPreference::Mid;
	if (FlightHeightSettings.bEnabled && FlightHeightSettings.IsValid())
	{
		CombatHeightPreference = FlightHeightSettings.bRandomizePreference
			? static_cast<EEnemyFlightHeightPreference>(FMath::RandRange(0, 2))
			: FlightHeightSettings.FixedPreference;
	}
}

bool AEnemyBase::PrefersCombatOrbitLeft() const
{
	// 시작 성향은 최초 선택에만 사용한다. 같은 대상·제어권의 실제 채택 방향이 있으면 이어간다.
	return CombatOrbitTarget.IsValid() && CombatOrbitTarget.Get() == GetTrackedTarget()
		&& CombatOrbitController.IsValid() && CombatOrbitController.Get() == GetController()
		? bLastCombatOrbitLeft : bPrefersCombatLeft;
}

void AEnemyBase::RememberCombatOrbitDirection(AActor* Target, bool bLeft)
{
	if (!HasAuthority() || !Target || Target != GetTrackedTarget() || !GetController())
	{
		return;
	}
	CombatOrbitTarget = Target;
	CombatOrbitController = GetController();
	bLastCombatOrbitLeft = bLeft;
}

void AEnemyBase::PromotePreStunState(EEnemyCombatState DetectedState)
{
	if (CombatState != EEnemyCombatState::Stun)
	{
		return;
	}

	if (DetectedState == EEnemyCombatState::Combat)
	{
		PreStunCombatState = EEnemyCombatState::Combat;
		if (!bInCombat)
		{
			InitializeCombatMovementPreferences();
			bInCombat = true;
			RefreshCombatStatePresentation();
			RefreshPerceptionConfigForCurrentState();
		}
		return;
	}

	if (DetectedState == EEnemyCombatState::Alert
		&& (PreStunCombatState == EEnemyCombatState::NonCombat || PreStunCombatState == EEnemyCombatState::Returning))
	{
		PreStunCombatState = EEnemyCombatState::Alert;
	}
}

void AEnemyBase::RefreshPerceptionConfigForCurrentState()
{
	AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController());
	if (!EnemyAIController)
	{
		EnemyAIController = Cast<AEnemyAIController>(GetController());
	}

	if (EnemyAIController)
	{
		EnemyAIController->RefreshPerceptionConfigFromPawn();
	}
}

void AEnemyBase::EquipDefaultWeapon()
{
	if (!HasAuthority() || !DefaultWeaponClass || IsValid(CurrentWeapon))
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARangedWeaponBase* SpawnedWeapon = GetWorld()->SpawnActor<ARangedWeaponBase>(
		DefaultWeaponClass,
		GetActorTransform(),
		SpawnParameters);
	if (!SpawnedWeapon)
	{
		return;
	}

	CurrentWeapon = SpawnedWeapon;

	// OnEquipped로 WeaponOwner/복제 상태를 먼저 확정한다.
	// Shooter 전용 무기 메시 부착 헬퍼에 의존하지 않도록 Enemy Mesh에는 Actor Root를 직접 부착한다.
	CurrentWeapon->OnEquipped(this);
	CurrentWeapon->AttachToComponent(
		GetMesh(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		WeaponSocketName);
	CurrentWeapon->ShowEquippedPresentation();
	ForceNetUpdate();
}

bool AEnemyBase::StartAttackTarget(AActor* TargetActor)
{
	RefreshCombatAimPoint();
	if (!CanAttackTrackedTarget(TargetActor))
	{
		return false;
	}
	if (bTargetAttackActive && ActiveAttackTarget.Get() == TargetActor && CurrentWeapon->IsAttacking())
	{
		return true;
	}
	// 첫 발도 자동 연사와 같은 대상 검증을 거치도록 먼저 공격 소유권을 설정한다.
	ActiveAttackTarget = TargetActor;
	bTargetAttackActive = true;
	if (!StartAttackLocation(GetCombatAimPoint(TargetActor)))
	{
		ActiveAttackTarget.Reset();
		bTargetAttackActive = false;
		return false;
	}
	RememberTargetAttackPreparation();
	TargetSelectionBlockedAt = -1.0;
	bResumeTargetAttackPreparation = false;
	return true;
}

bool AEnemyBase::CanAttackTrackedTarget(const AActor* TargetActor) const
{
	FVector ProtectedStart;
	FVector ProtectedEnd;
	uint64 YieldSerial = 0;
	if (CombatYieldMovementSerial != 0 && GetCombatYieldContext(ProtectedStart, ProtectedEnd, YieldSerial))
	{
		// 양보 경로 확정 후에는 발사를 보류한다. 이동 완료/중단 후 같은 준비·회복으로 복귀한다.
		return false;
	}
	// Task Tick 사이의 자동 연사도 같은 절대 종료 시각을 따른다. 정상 사선 복구가 기한을 재충전하지 않는다.
	if (HasTargetAttackAttemptExpired())
	{
		return false;
	}
	const bool bTargetValid = HasAuthority() && CombatState == EEnemyCombatState::Combat
		&& !IsAIControlSuppressed() && !IsDead() && !IsLocationInvestigationActive() && TargetActor
		&& TargetActor == GetTrackedTarget() && IsValid(CurrentWeapon);
	uint8 Reason = !bTargetValid ? 1 : 0;
	const AAutoTurret* Turret = Cast<AAutoTurret>(this);
	if (!Reason && (UsesCombatNavigationObservation() || (Turret && Turret->UsesNormalTurretAttackPolicy())))
	{
		const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
		// 유예/공유는 이동 단서다. 버스트 도중에도 재노출 전 새 탄을 승인하지 않는다.
		if (!AI || !bPlayerCurrentlyVisible || !AI->IsTargetDirectlyVisible(TargetActor))
		{
			Reason = 1;
		}
	}
	if (!Reason && UsesTargetAttackPreparation())
	{
		if (!IsTargetAttackPreparationComplete())
		{
			Reason = 2;
		}
		else if (GetTargetAttackRecoveryRemaining() > 0.0f)
		{
			Reason = 3;
		}
		else if (!IsTargetAttackAimReady())
		{
			Reason = 4;
		}
	}
	if (!Reason && Turret && Turret->UsesNormalTurretAttackPolicy())
	{
		if (const AAutoTurret* TargetTurret = Cast<AAutoTurret>(TargetActor))
		{
			FVector ObservedPoint;
			// 자동 연사도 후보 선택 때와 같은 최신 터렛 관측을 요구한다. 마지막 조준점은 발사 허가가 아니다.
			if (!TargetTurret->TryGetObservedSightPoint(this, ObservedPoint, true)) Reason = 5;
		}
	}
	const bool bShooterObservationRequired = Cast<AShooterCharacter>(TargetActor)
		&& (UsesCombatNavigationObservation() || (Turret && Turret->UsesNormalTurretAttackPolicy()));
	if (!Reason && !bIsPossessed && !Cast<ALaserWeapon>(CurrentWeapon)
		&& (bShooterObservationRequired
			|| (SelectedCombatAimTarget.Get() == TargetActor && CombatAimObservationEpoch != 0)))
	{
		FOutlierSightObservation Observation;
		const AShooterCharacter* Shooter = Cast<AShooterCharacter>(TargetActor);
		// 무기 콜백 직전에도 같은 본이 최신 관측에 남아 있어야 한다. 만료/완전 차폐는 발사 보류다.
		// 대상 전환으로 관측 번호가 초기화된 순간도 보류한다. 새 자기 관측 없이 캡슐 중심으로 발사하지 않는다.
		if (SelectedCombatAimTarget.Get() != TargetActor || CombatAimObservationEpoch == 0
			|| (!IsTargetAttemptActive() && SelectedCombatAimRank <= 0) || !GetWorld() || GetWorld()->GetTimeSeconds() > NextCombatAimRefreshAt + 0.15
			|| !Shooter || !Shooter->GetObservedAimPoints(this, Observation)
			|| Observation.Epoch != CombatAimObservationEpoch
			|| !(Observation.BodyBoneIndices.Contains(SelectedCombatAimIndex)
				|| Observation.HeadBoneIndices.Contains(SelectedCombatAimIndex)
				|| Observation.OtherBoneIndices.Contains(SelectedCombatAimIndex)))
		{
			Reason = 5;
		}
	}
	// 승인된 시도만 환경 차단 예외를 사용한다. 일반 사격의 사선 검사는 그대로 유지한다.
	if (!Reason && !(IsTargetAttemptActive()
		? CurrentWeapon->HasTargetAttemptPath(TargetActor, GetCombatAimPoint(TargetActor))
		: CurrentWeapon->HasClearTargetAttackPath(TargetActor, GetCombatAimPoint(TargetActor))))
	{
		Reason = 5;
	}
	// 대기 사유가 바뀔 때만 기록한다. 기존 사선 검사 결과를 재사용하며 진단용 Trace를 추가하지 않는다.
	if (UsesTargetAttackPreparation() && LastTargetAttackWaitReason != Reason)
	{
		LastTargetAttackWaitReason = Reason;
		const TCHAR* Reasons[] = { TEXT("ShotEligible"), TEXT("TargetOrAuthority"), TEXT("Telegraph"), TEXT("Recovery"), TEXT("AimAngle"), TEXT("RangeOrPath") };
		LogTargetAttackReadiness(Reasons[Reason]);
	}
	return Reason == 0;
}

bool AEnemyBase::IsTargetOutsideWeaponRange(const AActor* TargetActor) const
{
	if (!IsValid(TargetActor) || TargetActor != GetTrackedTarget() || !IsValid(CurrentWeapon)
		|| !FMath::IsFinite(CurrentWeapon->GetEffectiveRange()) || CurrentWeapon->GetEffectiveRange() <= 0.0f)
	{
		return false;
	}
	return CurrentWeapon->IsOutsideEffectiveRange(GetCombatAimPoint(TargetActor));
}

bool AEnemyBase::RefreshTargetAttackForShot()
{
	AActor* Target = ActiveAttackTarget.Get();
	return bTargetAttackActive && CanAttackTrackedTarget(Target)
		&& UpdateAttackLocation(GetCombatAimPoint(Target));
}

bool AEnemyBase::IsTargetAttackAttemptContractCurrent() const
{
	return AttemptWeapon.Get() == CurrentWeapon && AttemptController.Get() == GetController()
		&& AttemptGeneration == GetPoolGameplayGeneration() && AttemptLease == GetPoolLeaseSerial();
}

void AEnemyBase::RefreshTargetAttackAttemptContract()
{
	if (!IsTargetAttackAttemptContractCurrent())
	{
		FinishTargetAttackAttempt();
		TargetAttackAttemptRecords.Reset();
		AttemptWeapon = CurrentWeapon;
		AttemptController = GetController();
		AttemptGeneration = GetPoolGameplayGeneration();
		AttemptLease = GetPoolLeaseSerial();
	}
	if (HasAuthority() && GetWorld() && !TacticalObstacleHandle.IsValid())
	{
		TacticalFailureRooms = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
		if (URoomCombatSubsystem* Rooms = TacticalFailureRooms.Get())
		{
			TacticalObstacleHandle = Rooms->OnMovementObstacleOpened.AddUObject(this, &AEnemyBase::HandleCombatTacticalObstacleOpened);
		}
	}
}

AEnemyBase::FTargetAttackAttemptRecord* AEnemyBase::FindTargetAttackAttemptRecord()
{
	AActor* Target = GetTrackedTarget();
	return TargetAttackAttemptRecords.FindByPredicate([Target](const FTargetAttackAttemptRecord& Entry)
	{
		return Target && Entry.Target.Get() == Target;
	});
}

bool AEnemyBase::HasCurrentTargetAttemptObservation() const
{
	const AActor* Target = GetTrackedTarget();
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (!HasAuthority() || !GetWorld() || !UsesTargetAttackPreparation() || !IsValid(CurrentWeapon)
		|| Cast<ALaserWeapon>(CurrentWeapon) || !AI || !AI->IsValidTrackingTarget(Target)
		|| !bPlayerCurrentlyVisible || !AI->IsTargetDirectlyVisible(Target)
		|| IsDead() || IsAIControlSuppressed() || IsLocationInvestigationActive()
		|| GetCombatState() != EEnemyCombatState::Combat)
	{
		return false;
	}
	if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Target))
	{
		FOutlierSightObservation Observation;
		return SelectedCombatAimTarget.Get() == Target && CombatAimObservationEpoch != 0
			&& GetWorld()->GetTimeSeconds() <= NextCombatAimRefreshAt + 0.15
			&& Shooter->GetObservedAimPoints(this, Observation) && Observation.Epoch == CombatAimObservationEpoch
			&& (Observation.BodyBoneIndices.Contains(SelectedCombatAimIndex)
				|| Observation.HeadBoneIndices.Contains(SelectedCombatAimIndex)
				|| Observation.OtherBoneIndices.Contains(SelectedCombatAimIndex));
	}
	return true;
}

void AEnemyBase::NoteTargetAttackAlternativesExhausted()
{
	if (!HasAuthority() || !UsesCombatNavigationObservation() || !GetTrackedTarget())
	{
		return;
	}
	RefreshTargetAttackAttemptContract();
	FTargetAttackAttemptRecord* Entry = FindTargetAttackAttemptRecord();
	if (!Entry && TargetAttackAttemptRecords.Num() < 64)
	{
		Entry = &TargetAttackAttemptRecords.AddDefaulted_GetRef();
		Entry->Target = GetTrackedTarget();
	}
	if (Entry)
	{
		Entry->bAlternativesExhausted = true;
		const FBox& FailedMovement = CombatTacticalEpisode.GetCurrentFailureBounds();
		if (FailedMovement.IsValid)
		{
			Entry->FailureBounds += FailedMovement;
		}
	}
}

void AEnemyBase::ClearTargetAttackAlternativesExhausted()
{
	if (FTargetAttackAttemptRecord* Entry = FindTargetAttackAttemptRecord())
	{
		Entry->bAlternativesExhausted = false;
	}
}

bool AEnemyBase::HasTargetAttackAlternativesExhausted() const
{
	const AActor* Target = GetTrackedTarget();
	return IsTargetAttackAttemptContractCurrent()
		&& TargetAttackAttemptRecords.ContainsByPredicate([Target](const FTargetAttackAttemptRecord& Entry)
		{
			return Target && Entry.Target.Get() == Target && Entry.bAlternativesExhausted;
		});
}

bool AEnemyBase::HasTargetAttackAttemptExpired() const
{
	return bTargetAttackAttemptArmed && GetWorld() && TargetAttackAttemptEndsAt >= 0.0
		&& GetWorld()->GetTimeSeconds() >= TargetAttackAttemptEndsAt;
}

bool AEnemyBase::IsTargetAttemptActive() const
{
	if (!bTargetAttackAttemptArmed || !IsTargetAttackAttemptContractCurrent()
		|| HasTargetAttackAttemptExpired() || !HasCurrentTargetAttemptObservation()
		|| !TargetAttackAttemptRecords.IsValidIndex(ActiveAttemptIndex))
	{
		return false;
	}
	const FTargetAttackAttemptRecord& Entry = TargetAttackAttemptRecords[ActiveAttemptIndex];
	const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(GetTrackedTarget());
	return Entry.Target.Get() == GetTrackedTarget() && (!EnemyTarget
		|| (Entry.TargetGeneration == EnemyTarget->GetPoolGameplayGeneration()
			&& Entry.TargetLease == EnemyTarget->GetPoolLeaseSerial()
			&& Entry.TargetHackRevision == EnemyTarget->HackEffectState.Revision));
}

bool AEnemyBase::CanUseTargetAttemptPathFrom(const AActor* Target, const FVector& Origin) const
{
	if (!IsTargetAttemptActive() || Target != GetTrackedTarget())
	{
		return false;
	}
	int32 RemainingChecks = 16;
	return CurrentWeapon->HasTargetAttemptPathFrom(Target, GetCombatAimPoint(Target), Origin, RemainingChecks);
}

bool AEnemyBase::PrepareTargetAttackAttempt(float RemainingBurstSeconds)
{
	RefreshTargetAttackAttemptContract();
	if (IsTargetAttemptActive())
	{
		if (CurrentWeapon->HasClearTargetAttackPath(GetTrackedTarget(), GetCombatAimPoint(GetTrackedTarget())))
		{
			// 사선 복구는 같은 버스트의 일반 사격으로 연결한다. 사용 기록과 무기 소비량은 반환하지 않는다.
			FinishTargetAttackAttempt();
			return false;
		}
		return true;
	}
	if (HasTargetAttackAttemptExpired() || !FMath::IsFinite(RemainingBurstSeconds)
		|| RemainingBurstSeconds <= 0.0f || !HasCurrentTargetAttemptObservation())
	{
		return false;
	}
	const AActor* Target = GetTrackedTarget();
	const AAutoTurret* Turret = Cast<AAutoTurret>(this);
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	FTargetAttackAttemptRecord* Entry = FindTargetAttackAttemptRecord();
	if (!Turret && (!Entry || !Entry->bAlternativesExhausted))
	{
		return false;
	}
	const AEnemyBase* CurrentEnemyTarget = Cast<AEnemyBase>(Target);
	const bool bSameTargetIdentity = !CurrentEnemyTarget
		|| (Entry && Entry->TargetGeneration == CurrentEnemyTarget->GetPoolGameplayGeneration()
			&& Entry->TargetLease == CurrentEnemyTarget->GetPoolLeaseSerial()
			&& Entry->TargetHackRevision == CurrentEnemyTarget->HackEffectState.Revision);
	if (Entry && Entry->bUsed && bSameTargetIdentity
		&& Entry->Origin.Equals(GetCombatShotOrigin(), 100.0f) && Entry->Aim.Equals(GetCombatAimPoint(Target), 100.0f))
	{
		// 같은 실패로 관찰 중에는 매 Task Tick마다 환경/동료 Trace를 반복하지 않는다.
		return false;
	}
	if (Turret && AI->GetPreferredAttackableTurretTarget(Target))
	{
		// 고정 터렛도 열린 다른 대상이 있으면 먼저 정상 사격 선택으로 인계한다.
		return false;
	}
	int32 NormalChecks = 16;
	if (HasCombatAttackPathFrom(Target, GetActorLocation(), NormalChecks) || NormalChecks <= 0)
	{
		return false;
	}
	if (Turret && !Entry && TargetAttackAttemptRecords.Num() < 64)
	{
		Entry = &TargetAttackAttemptRecords.AddDefaulted_GetRef();
		Entry->Target = const_cast<AActor*>(Target);
	}
	if (!Entry)
	{
		// 수명 동안 사용 기록을 축출해 같은 조건을 재충전하지 않는다. 상한에서는 안전하게 보류한다.
		return false;
	}
	FTargetAttemptPathEvidence Evidence;
	if (!CurrentWeapon->HasTargetAttemptPath(Target, GetCombatAimPoint(Target), &Evidence) || !Evidence.Blocker.IsValid())
	{
		return false;
	}
	const FVector Origin = GetCombatShotOrigin();
	const FVector Aim = GetCombatAimPoint(Target);
	const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(Target);
	const bool bTargetIdentityChanged = EnemyTarget && (Entry->TargetGeneration != EnemyTarget->GetPoolGameplayGeneration()
		|| Entry->TargetLease != EnemyTarget->GetPoolLeaseSerial() || Entry->TargetHackRevision != EnemyTarget->HackEffectState.Revision);
	// 위치만 조금 바뀐 동일 벽 사격은 반복하지 않는다. 충분한 위치/각도 변화와 새 충돌 경로를 함께 요구한다.
	const bool bDifferentLine = (FVector::DistSquared(Origin, Entry->Origin) >= FMath::Square(100.0)
		|| FVector::DistSquared(Aim, Entry->Aim) >= FMath::Square(100.0))
		&& FVector::DotProduct((Aim - Origin).GetSafeNormal(), (Entry->Aim - Entry->Origin).GetSafeNormal())
			< FMath::Cos(FMath::DegreesToRadians(10.0))
		&& (Evidence.Blocker != Entry->Blocker || FVector::DistSquared(Evidence.ImpactPoint, Entry->Impact) >= FMath::Square(100.0));
	if (Entry->bUsed && !bTargetIdentityChanged && !bDifferentLine)
	{
		return false;
	}
	Entry->bUsed = false;
	Entry->Blocker = Evidence.Blocker;
	Entry->Impact = Evidence.ImpactPoint;
	Entry->Origin = Origin;
	Entry->Aim = Aim;
	Entry->FailureBounds += Origin;
	Entry->FailureBounds += Aim;
	Entry->FailureBounds = Entry->FailureBounds.ExpandBy(25.0f);
	Entry->TargetGeneration = EnemyTarget ? EnemyTarget->GetPoolGameplayGeneration() : 0;
	Entry->TargetLease = EnemyTarget ? EnemyTarget->GetPoolLeaseSerial() : 0;
	Entry->TargetHackRevision = EnemyTarget ? EnemyTarget->HackEffectState.Revision : 0;
	ActiveAttemptIndex = int32(Entry - TargetAttackAttemptRecords.GetData());
	TargetAttackAttemptDuration = FMath::Min(0.5f, RemainingBurstSeconds);
	TargetAttackAttemptEndsAt = -1.0;
	bTargetAttackAttemptArmed = true;
	return true;
}

void AEnemyBase::FinishTargetAttackAttempt()
{
	// 무기/Task는 기존 회복을 소유한다. 여기서는 예외 승인만 반환하고 사용 기록은 남긴다.
	bTargetAttackAttemptArmed = false;
	ActiveAttemptIndex = INDEX_NONE;
	TargetAttackAttemptEndsAt = -1.0;
	TargetAttackAttemptDuration = 0.0f;
}

void AEnemyBase::PauseTargetAttack()
{
	if (!HasAuthority())
	{
		return;
	}
	// 짧은 사선/회전 보류는 같은 버스트다. 장기 보류만 비사격 재선정 경계로 풀어 준다.
	if (TargetSelectionBurstTarget.IsValid() && TargetSelectionBlockedAt < 0.0 && GetWorld())
	{
		TargetSelectionBlockedAt = GetWorld()->GetTimeSeconds();
	}
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->PauseTargetAttack();
	}
	ActiveAttackTarget.Reset();
	bTargetAttackActive = false;
	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->SetAttackAimActive(false);
	}
	if (AttackPhase == EEnemyAttackPhase::Firing)
	{
		SetAttackPhase(EEnemyAttackPhase::Idle);
	}
}

void AEnemyBase::PruneTargetAttackPreparationHistory()
{
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	TargetAttackPreparationHistory.RemoveAll([&](const FTargetAttackPreparationHistory& Entry)
	{
		const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(Entry.Target.Get());
		return !AI || Entry.Controller.Get() != AI || Entry.Weapon.Get() != CurrentWeapon
			|| Entry.Generation != GetPoolGameplayGeneration() || Entry.Lease != GetPoolLeaseSerial()
			|| Now >= Entry.ExpiresAt || !AI->IsValidTrackingTarget(Entry.Target.Get())
			|| (EnemyTarget && (Entry.TargetGeneration != EnemyTarget->GetPoolGameplayGeneration()
				|| Entry.TargetLease != EnemyTarget->GetPoolLeaseSerial()
				|| Entry.TargetHackRevision != EnemyTarget->HackEffectState.Revision));
	});
}

void AEnemyBase::StoreTargetAttackPreparation()
{
	PruneTargetAttackPreparationHistory();
	AActor* Target = PreparedAttackTarget.Get();
	AEnemyAIController* AI = PreparedAttackController.Get();
	if (!HasAuthority() || !GetWorld() || !Target || !AI || AI != GetController()
		|| PreparedAttackWeapon.Get() != CurrentWeapon || !AI->IsValidTrackingTarget(Target)
		|| PreparedAttackGeneration != GetPoolGameplayGeneration() || PreparedAttackLease != GetPoolLeaseSerial()
		|| (!bHasTargetAttackPreparationDuration && !bTargetAttackPreparationComplete))
	{
		return;
	}
	const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(Target);
	if (EnemyTarget && (PreparedAttackTargetGeneration != EnemyTarget->GetPoolGameplayGeneration()
		|| PreparedAttackTargetLease != EnemyTarget->GetPoolLeaseSerial()
		|| PreparedAttackTargetHackRevision != EnemyTarget->HackEffectState.Revision))
	{
		return;
	}
	if (TargetAttackPreparationHistory.ContainsByPredicate([Target](const FTargetAttackPreparationHistory& Entry)
		{ return Entry.Target.Get() == Target; }))
	{
		// 중복 정리/입력은 최초 선택 이탈 시각을 연장하지 않는다.
		return;
	}
	FTargetAttackPreparationHistory& Entry = TargetAttackPreparationHistory.AddDefaulted_GetRef();
	Entry.Target = Target;
	Entry.Weapon = CurrentWeapon;
	Entry.Controller = AI;
	Entry.ExpiresAt = GetWorld()->GetTimeSeconds() + 3.0;
	Entry.Remaining = TargetAttackPreparationRemaining;
	Entry.Duration = TargetAttackPreparationDuration;
	Entry.bComplete = bTargetAttackPreparationComplete;
	Entry.Generation = PreparedAttackGeneration;
	Entry.Lease = PreparedAttackLease;
	Entry.TargetGeneration = PreparedAttackTargetGeneration;
	Entry.TargetLease = PreparedAttackTargetLease;
	Entry.TargetHackRevision = PreparedAttackTargetHackRevision;
	while (TargetAttackPreparationHistory.Num() > 4)
	{
		TargetAttackPreparationHistory.RemoveAt(0);
	}
}

bool AEnemyBase::RestoreTargetAttackPreparation()
{
	PruneTargetAttackPreparationHistory();
	// 관측 콜백은 대상 신원을 먼저 채택하고 시야 플래그를 나중에 갱신할 수 있다.
	// 준비 이력은 채택한 신원으로 복원하되, 진행과 발사는 별도의 현재 직접 시야 검사를 유지한다.
	const AActor* Target = TrackedTarget.Get();
	const int32 Index = TargetAttackPreparationHistory.IndexOfByPredicate([Target](const FTargetAttackPreparationHistory& Entry)
		{ return Target && Entry.Target.Get() == Target; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FTargetAttackPreparationHistory Entry = TargetAttackPreparationHistory[Index];
	TargetAttackPreparationHistory.RemoveAt(Index);
	PreparedAttackTarget = Entry.Target;
	PreparedAttackWeapon = Entry.Weapon;
	PreparedAttackController = Entry.Controller;
	PreparedAttackGeneration = Entry.Generation;
	PreparedAttackLease = Entry.Lease;
	PreparedAttackTargetGeneration = Entry.TargetGeneration;
	PreparedAttackTargetLease = Entry.TargetLease;
	PreparedAttackTargetHackRevision = Entry.TargetHackRevision;
	TargetAttackPreparationRemaining = Entry.Remaining;
	TargetAttackPreparationDuration = Entry.Duration;
	bHasTargetAttackPreparationDuration = true;
	bTargetAttackPreparationComplete = Entry.bComplete;
	bResumeTargetAttackPreparation = true;
	// 복원한 것은 전조뿐이다. 현재 관측/회전/총구 사선은 발사 직전에 다시 검사한다.
	bTargetAttackAimWasReady = false;
	bTargetAttackFirstShotConsumed = false;
	return true;
}

void AEnemyBase::RememberTargetAttackPreparation()
{
	if (!HasAuthority() || !GetTrackedTarget() || bIsPossessed)
	{
		return;
	}
	PreparedAttackTarget = GetTrackedTarget();
	PreparedAttackWeapon = CurrentWeapon;
	PreparedAttackController = Cast<AEnemyAIController>(GetController());
	PreparedAttackGeneration = GetPoolGameplayGeneration();
	PreparedAttackLease = GetPoolLeaseSerial();
	const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(GetTrackedTarget());
	PreparedAttackTargetGeneration = EnemyTarget ? EnemyTarget->GetPoolGameplayGeneration() : 0;
	PreparedAttackTargetLease = EnemyTarget ? EnemyTarget->GetPoolLeaseSerial() : 0;
	PreparedAttackTargetHackRevision = EnemyTarget ? EnemyTarget->HackEffectState.Revision : 0;
	bTargetAttackPreparationComplete = true;
	TargetAttackPreparationRemaining = 0.0f;
}

bool AEnemyBase::UsesTargetAttackPreparation() const
{
	const AAutoTurret* Turret = Cast<AAutoTurret>(this);
	return !bIsPossessed && ((RuntimeStat.Type == EEnemyType::Gun && IsA<AVECDrone>())
		|| (Turret && Turret->UsesNormalTurretAttackPolicy()));
}

void AEnemyBase::BeginTargetAttackPreparation(float Duration)
{
	if (!HasAuthority() || !UsesTargetAttackPreparation() || !GetTrackedTarget())
	{
		return;
	}
	if (PreparedAttackTarget.Get() == GetTrackedTarget() && PreparedAttackWeapon.Get() == CurrentWeapon
		&& PreparedAttackController.Get() == GetController()
		&& PreparedAttackGeneration == GetPoolGameplayGeneration() && PreparedAttackLease == GetPoolLeaseSerial())
	{
		const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(GetTrackedTarget());
		if (!EnemyTarget || (PreparedAttackTargetGeneration == EnemyTarget->GetPoolGameplayGeneration()
			&& PreparedAttackTargetLease == EnemyTarget->GetPoolLeaseSerial()
			&& PreparedAttackTargetHackRevision == EnemyTarget->HackEffectState.Revision))
		{
			return;
		}
	}
	StoreTargetAttackPreparation();
	if (RestoreTargetAttackPreparation())
	{
		return;
	}
	// 새 선택도 네 대상 상한에 포함한다. 가장 오래 이탈한 대상부터 반환한다.
	while (TargetAttackPreparationHistory.Num() >= 4)
	{
		TargetAttackPreparationHistory.RemoveAt(0);
	}
	// Task 재진입은 남은 전조를 덮어쓰지 않는다. 대상/무기/제어권이 바뀐 때만 새 준비를 만든다.
	PreparedAttackTarget = GetTrackedTarget();
	PreparedAttackWeapon = CurrentWeapon;
	PreparedAttackController = Cast<AEnemyAIController>(GetController());
	PreparedAttackGeneration = GetPoolGameplayGeneration();
	PreparedAttackLease = GetPoolLeaseSerial();
	const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(GetTrackedTarget());
	PreparedAttackTargetGeneration = EnemyTarget ? EnemyTarget->GetPoolGameplayGeneration() : 0;
	PreparedAttackTargetLease = EnemyTarget ? EnemyTarget->GetPoolLeaseSerial() : 0;
	PreparedAttackTargetHackRevision = EnemyTarget ? EnemyTarget->HackEffectState.Revision : 0;
	TargetAttackPreparationRemaining = FMath::IsFinite(Duration) ? FMath::Max(0.0f, Duration) : 0.25f;
	TargetAttackPreparationDuration = TargetAttackPreparationRemaining;
	bHasTargetAttackPreparationDuration = true;
	bTargetAttackPreparationComplete = TargetAttackPreparationRemaining <= 0.0f;
	bTargetAttackFirstShotConsumed = false;
	bTargetAttackAimWasReady = false;
	SetAttackPhase(EEnemyAttackPhase::Telegraph);
	LogTargetAttackReadiness(TEXT("TelegraphBegin"));
}

bool AEnemyBase::IsTargetAttackPreparationComplete() const
{
	const AEnemyBase* EnemyTarget = Cast<AEnemyBase>(GetTrackedTarget());
	return bTargetAttackPreparationComplete && GetTrackedTarget()
		&& PreparedAttackTarget.Get() == GetTrackedTarget() && PreparedAttackWeapon.Get() == CurrentWeapon
		&& PreparedAttackController.Get() == GetController()
		&& PreparedAttackGeneration == GetPoolGameplayGeneration() && PreparedAttackLease == GetPoolLeaseSerial()
		&& (!EnemyTarget || (PreparedAttackTargetGeneration == EnemyTarget->GetPoolGameplayGeneration()
			&& PreparedAttackTargetLease == EnemyTarget->GetPoolLeaseSerial()
			&& PreparedAttackTargetHackRevision == EnemyTarget->HackEffectState.Revision));
}

bool AEnemyBase::AdvanceTargetAttackPreparation(float DeltaTime)
{
	// 같은 Attack State 안에서 대상만 교체되어도, Tree에서 받은 최초 전조 설정을 새 대상에게 적용한다.
	if (bHasTargetAttackPreparationDuration)
	{
		BeginTargetAttackPreparation(TargetAttackPreparationDuration);
	}
	if (!HasAuthority() || !UsesTargetAttackPreparation() || !GetTrackedTarget()
		|| PreparedAttackTarget.Get() != GetTrackedTarget() || PreparedAttackWeapon.Get() != CurrentWeapon
		|| PreparedAttackController.Get() != GetController())
	{
		return false;
	}
	// 직접 관측만 전조를 진행시킨다. 가려진 시간과 추적 유예는 준비 시간으로 소급하지 않는다.
	const AEnemyAIController* AI = PreparedAttackController.Get();
	if (!bTargetAttackPreparationComplete && bPlayerCurrentlyVisible && AI
		&& AI->IsTargetDirectlyVisible(GetTrackedTarget()) && FMath::IsFinite(DeltaTime))
	{
		TargetAttackPreparationRemaining = FMath::Max(0.0f, TargetAttackPreparationRemaining - FMath::Max(0.0f, DeltaTime));
		bTargetAttackPreparationComplete = TargetAttackPreparationRemaining <= 0.0f;
		if (bTargetAttackPreparationComplete)
		{
			LogTargetAttackReadiness(TEXT("TelegraphComplete"));
		}
	}
	return IsTargetAttackPreparationComplete();
}

bool AEnemyBase::UpdateTargetAttackAim(float DeltaTime)
{
	// 준비 이력은 공유하지만 터렛의 고정 몸체와 헤드 회전은 전용 Task가 소유한다.
	if (IsA<AAutoTurret>())
	{
		return false;
	}
	if (bHasTargetAttackPreparationDuration)
	{
		BeginTargetAttackPreparation(TargetAttackPreparationDuration);
	}
	AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	AActor* Target = GetTrackedTarget();
	if (!HasAuthority() || !UsesTargetAttackPreparation() || !AI || !Target
		|| CombatState != EEnemyCombatState::Combat || IsDead() || IsAIControlSuppressed()
		|| IsLocationInvestigationActive() || PreparedAttackTarget.Get() != Target
		|| PreparedAttackWeapon.Get() != CurrentWeapon || PreparedAttackController.Get() != AI)
	{
		return false;
	}
	RefreshCombatAimPoint();
	// 시야와 실제 발사 경로가 모두 끊기면 전투 조준 점유만 반환한다. 추적 유예/준비 이력은 건드리지 않는다.
	if (!bPlayerCurrentlyVisible && (!IsValid(CurrentWeapon)
		|| !CurrentWeapon->HasClearTargetAttackPath(Target, GetCombatAimPoint(Target))))
	{
		return false;
	}
	const FVector Direction = GetCombatAimPoint(Target) - GetCombatShotOrigin();
	if (Direction.ContainsNaN() || Direction.IsNearlyZero())
	{
		return false;
	}
	const float Speed = FMath::IsFinite(RuntimeStat.AttackRotationSpeed) ? FMath::Max(1.0f, RuntimeStat.AttackRotationSpeed) : 240.0f;
	const float Step = FMath::IsFinite(DeltaTime) ? Speed * FMath::Max(0.0f, DeltaTime) : 0.0f;
	// 기존 컨트롤러 갱신 한 곳에서 구면 회전한다. 수직 목표와 ±180도 경계에서도 총 각속도를 제한한다.
	const FQuat Current = AI->GetControlRotation().Quaternion();
	const FQuat Goal = Direction.Rotation().Quaternion();
	const float Angle = FMath::RadiansToDegrees(Current.AngularDistance(Goal));
	FRotator Rotation = FQuat::Slerp(Current, Goal, Angle > UE_SMALL_NUMBER ? FMath::Min(1.0f, Step / Angle) : 1.0f).Rotator();
	Rotation.Roll = 0.0f;
	AI->SetAttackAimActive(true);
	SetCombatAimPresentationOwned(true);
	AI->SetControlRotation(Rotation);
	SetActorRotation(FRotator(0.0f, Rotation.Yaw, 0.0f));
	const bool bAimReady = IsTargetAttackAimReady();
	if (bAimReady && !bTargetAttackAimWasReady)
	{
		LogTargetAttackReadiness(TEXT("AimReady"));
	}
	bTargetAttackAimWasReady = bAimReady;
	return true;
}

void AEnemyBase::LogTargetAttackReadiness(const TCHAR* Stage) const
{
	static const IConsoleVariable* Diagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.PerceptionDiagnostics"));
	if (!Diagnostics || Diagnostics->GetInt() == 0 || !GetWorld())
	{
		return;
	}
	UE_LOG(LogOutlier, Log, TEXT("[EnemyAttackDiag] Stage=%s Enemy=%s Target=%s Now=%.3f TelegraphRemaining=%.3f RecoveryRemaining=%.3f Phase=%s Lease=%d Generation=%d"),
		Stage, *GetName(), *GetNameSafe(GetTrackedTarget()), GetWorld()->GetTimeSeconds(),
		TargetAttackPreparationRemaining, GetTargetAttackRecoveryRemaining(), *UEnum::GetValueAsString(AttackPhase),
		GetPoolLeaseSerial(), GetPoolGameplayGeneration());
}

void AEnemyBase::NotifyTargetAttackShotConsumed()
{
	// 공격 Task의 진입 횟수가 아니라 실제 총구별 탄 소비를 기록한다.
	// 숨은 대상의 실시간 위치 대신 기존 전투 관측 위치를 사용한다.
	static const IConsoleVariable* Diagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.PerceptionDiagnostics"));
	if (HasAuthority() && GetWorld() && Diagnostics && Diagnostics->GetInt() != 0)
	{
		AActor* Target = GetTrackedTarget();
		const FVector ObservedTarget = IsValid(Target) ? GetCombatNavigationTargetLocation(Target) : GetActorLocation();
		const FVector Away = (GetActorLocation() - ObservedTarget).GetSafeNormal2D();
		UE_LOG(LogOutlier, Log,
			TEXT("[EnemyShotConsumed] Enemy=%s Target=%s Now=%.3f Lease=%d Generation=%d Speed=%.1f AwaySpeed=%.1f Distance2D=%.1f Location=%s ObservedTarget=%s"),
			*GetName(), *GetNameSafe(Target), GetWorld()->GetTimeSeconds(), GetPoolLeaseSerial(), GetPoolGameplayGeneration(),
			GetVelocity().Size2D(), FVector::DotProduct(GetVelocity(), Away),
			IsValid(Target) ? FVector::Dist2D(GetActorLocation(), ObservedTarget) : -1.0,
			*GetActorLocation().ToCompactString(), *ObservedTarget.ToCompactString());
	}
	if (IsTargetAttemptActive() && TargetAttackAttemptRecords.IsValidIndex(ActiveAttemptIndex))
	{
		FTargetAttackAttemptRecord& Entry = TargetAttackAttemptRecords[ActiveAttemptIndex];
		if (!Entry.bUsed && GetWorld())
		{
			// 첫 실제 탄약 소비가 한 번의 시도를 확정한다. 이후 사선/회전 보류로 기한을 연장하지 않는다.
			Entry.bUsed = true;
			TargetAttackAttemptEndsAt = GetWorld()->GetTimeSeconds() + TargetAttackAttemptDuration;
		}
	}
	// 첫 전조의 첫 탄 기록과 별개로, 후속 버스트도 실제 탄을 소비한 순간부터 보호한다.
	if (HasAuthority() && UsesTargetAttackPreparation() && bTargetAttackActive)
	{
		TargetSelectionBurstTarget = ActiveAttackTarget;
		TargetSelectionBlockedAt = -1.0;
	}
	if (!HasAuthority() || !UsesTargetAttackPreparation() || !bTargetAttackActive || bTargetAttackFirstShotConsumed)
	{
		return;
	}
	bTargetAttackFirstShotConsumed = true;
	LogTargetAttackReadiness(TEXT("FirstAmmoConsumed"));
}

bool AEnemyBase::IsTargetAttackAimReady() const
{
	if (const AAutoTurret* Turret = Cast<AAutoTurret>(this); Turret && Turret->UsesNormalTurretAttackPolicy())
	{
		return Turret->IsTurretAttackAimReady(GetTrackedTarget());
	}
	const AController* AimController = GetController();
	const AActor* Target = GetTrackedTarget();
	if (!AimController || !Target)
	{
		return false;
	}
	const FVector Direction = GetCombatAimPoint(Target) - GetCombatShotOrigin();
	if (Direction.ContainsNaN() || Direction.IsNearlyZero())
	{
		return false;
	}
	const float Tolerance = FMath::IsFinite(RuntimeStat.AttackAimTolerance) ? FMath::Clamp(RuntimeStat.AttackAimTolerance, 0.1f, 45.0f) : 5.0f;
	return FVector::DotProduct(AimController->GetControlRotation().Vector(), Direction.GetSafeNormal())
		>= FMath::Cos(FMath::DegreesToRadians(Tolerance));
}

bool AEnemyBase::ConsumeTargetAttackResume()
{
	const bool bResume = bResumeTargetAttackPreparation && GetTrackedTarget()
		&& PreparedAttackTarget.Get() == GetTrackedTarget()
		&& PreparedAttackWeapon.Get() == CurrentWeapon
		&& PreparedAttackController.Get() == GetController();
	bResumeTargetAttackPreparation = false;
	return bResume;
}

void AEnemyBase::RememberTargetAttackRecovery(float Duration)
{
	if (HasAuthority() && GetWorld() && (!UsesTargetAttackPreparation() || GetTargetAttackRecoveryRemaining() <= 0.0f))
	{
		TargetAttackRecoveryEndsAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, Duration);
	}
}

float AEnemyBase::GetTargetAttackRecoveryRemaining() const
{
	return GetWorld() ? FMath::Max(0.0, TargetAttackRecoveryEndsAt - GetWorld()->GetTimeSeconds()) : 0.0f;
}

void AEnemyBase::ResetTargetAttackContinuity(bool bResetRecovery)
{
	NextCombatYieldPriorityCheckAt = -1.0;
	bCombatYieldPriorityReady = false;
	NextCombatAllyBlockCheckAt = -1.0;
	FinishTargetAttackAttempt();
	ClearTargetAttackAlternativesExhausted();
	ReleaseCombatReservation();
	if (!HasAuthority())
	{
		return;
	}
	if (bResetRecovery)
	{
		TargetAttackPreparationHistory.Reset();
		CombatNavigationRetargetVelocity = FVector::ZeroVector;
		CombatNavigationRetargetVelocityExpiresAt = -1.0;
	}
	else
	{
		StoreTargetAttackPreparation();
	}
	ClearCombatFlightHandoff();
	TargetSelectionBurstTarget.Reset();
	TargetSelectionBlockedAt = -1.0;
	CombatOrbitTarget.Reset();
	CombatOrbitController.Reset();
	++CombatAimObservationSerial;
	SetCombatAimPresentationOwned(false);
	SelectedCombatAimTarget.Reset();
	SelectedCombatAimRank = 0;
	CombatAimObservationEpoch = 0;
	NextCombatAimRefreshAt = -1.0;
	PreparedAttackTarget.Reset();
	PreparedAttackWeapon.Reset();
	PreparedAttackController.Reset();
	ActiveAttackTarget.Reset();
	bTargetAttackActive = false;
	bResumeTargetAttackPreparation = false;
	bTargetAttackPreparationComplete = false;
	TargetAttackPreparationRemaining = 0.0f;
	bTargetAttackAimWasReady = false;
	bTargetAttackFirstShotConsumed = false;
	LastTargetAttackWaitReason = 255;
	if (bResetRecovery)
	{
		TargetAttackRecoveryEndsAt = 0.0;
		bHasTargetAttackPreparationDuration = false;
		TargetAttackPreparationDuration = 0.0f;
	}
	if (!bResetRecovery)
	{
		const AAutoTurret* Turret = Cast<AAutoTurret>(this);
		if (Turret && Turret->UsesNormalTurretAttackPolicy() && IsValid(CurrentWeapon)
			&& CurrentWeapon->GetCurrentBurstShotCount() > 0 && !CurrentWeapon->IsOnPostBurstCooldown())
		{
			// 실제 탄을 쓴 터렛의 대상 인계는 이 경계에서 버스트를 닫는다. Task 종료 순서와 무관하게 같은 회복이다.
			CurrentWeapon->ForceTargetAttackPostBurstCooldown();
		}
		// 대상만 바꿀 때는 무기의 다음 발사 시각도 보존한다. StopAttack은 연사 쿨다운까지 지운다.
		PauseTargetAttack();
		SetAttackPhase(EEnemyAttackPhase::Idle);
	}
	else
	{
		StopCurrentAttack();
	}
}

bool AEnemyBase::StartAttackLocation(const FVector& TargetLocation)
{
	if (IsLocationInvestigationActive())
	{
		return false;
	}
	// 수색 중 병렬 공격 Task가 잘못 연결되어도 숨은 플레이어에게 사격하지 않는다.
	if (SearchEpisode.bActive && !bPlayerCurrentlyVisible && !bIsPossessed)
	{
		return false;
	}
	// 방 합류 준비 중에는 StateTree가 Combat으로 전환돼도 실제 발사를 시작하지 않는다.
	if (HasAuthority() && GetWorld())
	{
		if (const URoomCombatSubsystem* RoomCombat = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
			RoomCombat && RoomCombat->IsEnemyAttackBlocked(this))
		{
			return false;
		}
	}

	const bool bHasWeapon = IsValid(CurrentWeapon);
	const bool bCanAttack = bHasWeapon && CurrentWeapon->CanAttack();
	// 쿨다운 중인 요청은 조준 방향을 바꾸지 않는다. 실제로 발사할 수 있을 때만
	// ControlRotation을 갱신해야 실패 재시도가 드론 Pitch를 흔들지 않는다.
	const bool bAimUpdated = bCanAttack && UpdateAttackLocation(TargetLocation);
	if (!bAimUpdated || !bHasWeapon || !bCanAttack)
	{
		return false;
	}

	// 첫 발 전에 소유권을 확보하고, 이후 연사 사이에도 같은 조준을 유지한다.
	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->SetAttackAimActive(true);
	}
	CurrentWeapon->StartAttack();
	SetAttackPhase(EEnemyAttackPhase::Firing);
	return true;
}

bool AEnemyBase::StartPossessedAttackBurst()
{
	if (!HasAuthority()
		|| !bIsPossessed
		|| bPossessedImpactInputLocked
		|| !HasPossessedAttackRequest()
		|| CombatState == EEnemyCombatState::Stun
		|| !IsValid(CurrentWeapon)
		|| !CurrentWeapon->HasFixedBurst()
		|| !CurrentWeapon->CanAttack())
	{
		return false;
	}

	CurrentWeapon->StartAttack();
	if (bPossessedAttackQueued)
	{
		ConsumePossessedAttackRequest();
	}
	SetAttackPhase(EEnemyAttackPhase::Firing);
	return true;
}

bool AEnemyBase::ConsumePossessedAttackRequest()
{
	if (!HasAuthority() || !bIsPossessed || !HasPossessedAttackRequest())
	{
		return false;
	}

	bPossessedAttackQueued = false;
	SendEnemyStateTreeEventNextTick(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Attack.RequestConsumed")));
	return true;
}

bool AEnemyBase::UpdateAttackLocation(const FVector& TargetLocation)
{
	if (SearchEpisode.bActive && !bPlayerCurrentlyVisible && !bIsPossessed)
	{
		return false;
	}
	if (!HasAuthority()
		|| CombatState == EEnemyCombatState::Stun
		|| IsAIControlSuppressed()
		|| !IsValid(CurrentWeapon))
	{
		return false;
	}

	AController* ActiveController = GetController();
	if (!ActiveController)
	{
		return false;
	}

	// 조준과 무기의 실제 발사가 같은 원점을 사용한다. Gun 드론은 높은 시점 대신 몸체 축에서 겨냥한다.
	const FVector Direction = TargetLocation - GetCombatShotOrigin();
	if (Direction.ContainsNaN() || Direction.IsNearlyZero())
	{
		return false;
	}
	// AI Gun은 컨트롤러가 이미 제한 속도로 돌린 방향을 사용한다. Task/연사 콜백은 순간 회전시키지 않는다.
	if (UsesTargetAttackPreparation() && bTargetAttackActive)
	{
		return true;
	}

	FRotator AimRotation = Direction.Rotation();
	AimRotation.Roll = 0.0f;

	// ControlRotation에는 실제 사격 Pitch를 보존한다. 슈터 대상 외형의 하향 보정은 이동 컴포넌트에서 따로 적용한다.
	// Character 본체는 이동/충돌을 위해 Yaw만 회전한다.
	ActiveController->SetControlRotation(AimRotation);
	SetActorRotation(FRotator(0.0f, AimRotation.Yaw, 0.0f));
	return true;
}

void AEnemyBase::StopCurrentWeaponAttack()
{
	if (HasAuthority() && !bIsPossessed && PreparedAttackTarget.IsValid()
		&& PreparedAttackWeapon.Get() == CurrentWeapon && PreparedAttackController.Get() == GetController())
	{
		PauseTargetAttack();
		return;
	}
	ActiveAttackTarget.Reset();
	bTargetAttackActive = false;
	// Task 종료뿐 아니라 Stun/사망/빙의 등의 공통 중단 경로에서도 해제한다.
	if (HasAuthority())
	{
		if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
		{
			EnemyController->SetAttackAimActive(false);
		}
	}
	if (HasAuthority() && IsValid(CurrentWeapon))
	{
		CurrentWeapon->StopAttack();
	}
}

void AEnemyBase::StopCurrentAttack()
{
	StopCurrentWeaponAttack();
	if (HasAuthority())
	{
		SetAttackPhase(EEnemyAttackPhase::Idle);
	}
}

void AEnemyBase::SetAttackPhase(EEnemyAttackPhase NewPhase)
{
	if (!HasAuthority() || AttackPhase == NewPhase)
	{
		return;
	}

	const EEnemyAttackPhase PreviousPhase = AttackPhase;
	AttackPhase = NewPhase;
	OnAttackPhaseChanged(PreviousPhase, AttackPhase);
	ForceNetUpdate();
}

void AEnemyBase::OnRep_AttackPhase(EEnemyAttackPhase PreviousPhase)
{
	OnAttackPhaseChanged(PreviousPhase, AttackPhase);
}

void AEnemyBase::ReleasePatrolDestination()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->ReleasePatrolDestination(this);
	}
}

void AEnemyBase::ReleaseSearchDestinations()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->ReleaseSearchDestinations(this);
	}
}

bool AEnemyBase::CanContinueSearchEpisode() const
{
	const bool bNoiseSearch = IsLocationSearchActive();
	return HasAuthority() && !IsDead() && CanUseEnemyPerception() && !bPlayerCurrentlyVisible
		&& (CombatState == EEnemyCombatState::Combat || (bNoiseSearch && CombatState == EEnemyCombatState::Alert))
		&& (bNoiseSearch ? PerceptionGoal.Type == EEnemyPerceptionGoalType::Search : !HasTrackableTarget());
}

bool AEnemyBase::BeginSearchEpisode()
{
	const bool bNoiseSearch = IsLocationSearchActive();
	if ((!bNoiseSearch && !CanContinueSearchEpisode())
		|| (bNoiseSearch && (!HasAuthority() || IsDead() || !CanUseEnemyPerception() || bPlayerCurrentlyVisible
			|| (CombatState != EEnemyCombatState::Combat && CombatState != EEnemyCombatState::Alert)))
		|| (!bNoiseSearch && !bHasLastKnownPlayerLocation && !HasFreshSharedTargetLocation()))
	{
		return false;
	}
	const FVector Center = HitResponsePhase == EHitResponsePhase::Search ? HitSearchCenter : bNoiseSearch ? NoiseInvestigationLocation
		: (!bTrackingExpiredForSearch && HasFreshSharedTargetLocation() ? SharedTargetLocation : LastKnownPlayerLocation);
	const uint64 InvestigationSerial = bNoiseSearch ? GetLocationInvestigationSerial() : 0;
	const bool bSameEvidence = SearchEpisode.Serial != 0 && SearchEpisode.Controller.Get() == GetController()
		&& SearchEpisode.Generation == PoolGameplayGeneration && SearchEpisode.Lease == PoolLeaseSerial
		&& SearchEpisode.InvestigationSerial == InvestigationSerial && !bTrackingExpiredForSearch
		&& FVector::DistSquared(SearchEpisode.Center, Center) <= FMath::Square(200.0f);
	if (bSameEvidence)
	{
		// 자식/부모 재진입과 같은 위치의 새 보고는 기존 확인 지점과 절대 기한을 유지한다.
		// 종료된 Task의 늦은 결과는 재활성화한 회차 번호를 소유하지 못한다.
		if (!SearchEpisode.bActive)
		{
			SearchEpisode.Serial = ++NextSearchEpisodeSerial;
		}
		SearchEpisode.bActive = true;
		SearchEpisode.SharedRevision = SharedTargetRevision;
		RefreshSearchEpisodeLimit();
		return true;
	}
	EndSearchEpisode();
	SearchEpisode = FEnemySearchEpisode();
	SearchEpisode.bActive = true;
	SearchEpisode.Serial = ++NextSearchEpisodeSerial;
	SearchEpisode.SharedRevision = SharedTargetRevision;
	SearchEpisode.Center = Center;
	SearchEpisode.LookLocation = HitResponsePhase == EHitResponsePhase::Search ? ActiveHitClue.Location
		: bInvestigatingObservedLocation ? VisualInvestigation.Location
		: bNoiseSearch ? NoiseInvestigationClue.Location : Center;
	SearchEpisode.Controller = GetController();
	SearchEpisode.Generation = PoolGameplayGeneration;
	SearchEpisode.Lease = PoolLeaseSerial;
	SearchEpisode.InvestigationSerial = InvestigationSerial;
	SearchEpisode.MaxPoints = bNoiseSearch ? 2 : FMath::Clamp(RuntimeStat.SearchMaxPoints, 1, 4);
	SearchEpisode.StartedAt = GetWorld()->GetTimeSeconds();
	const float Duration = bNoiseSearch ? GetLocationSearchDuration(5.0f)
		: FMath::IsFinite(RuntimeStat.SearchMaxSeconds) ? FMath::Max(0.01f, RuntimeStat.SearchMaxSeconds) : 12.0f;
	SearchEpisode.EndsAt = SearchEpisode.StartedAt + Duration;
	bTrackingExpiredForSearch = false;
	if (bNoiseSearch)
	{
		PauseTargetAttack();
	}
	else
	{
		StopCurrentAttack();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	return true;
}

int32 AEnemyBase::GetSearchPointLimit() const
{
	return SearchEpisode.bActive ? SearchEpisode.MaxPoints : FMath::Clamp(RuntimeStat.SearchMaxPoints, 1, 4);
}

bool AEnemyBase::HasMeaningfulSearchHint() const
{
	return SearchEpisode.bActive && SharedTargetRevision > SearchEpisode.SharedRevision
		&& HasFreshSharedTargetLocation()
		&& FVector::DistSquared(SearchEpisode.Center, SharedTargetLocation) > FMath::Square(200.0f);
}

bool AEnemyBase::RefreshSearchEpisodeLimit()
{
	// 청각/피격의 완료는 기존 부모 전이가 담당한다. 일반 수색의 이동/질의만 유한하게 닫는다.
	if (HasAuthority() && GetWorld() && SearchEpisode.bActive && !SearchEpisode.bLocalGuard
		&& SearchEpisode.InvestigationSerial == 0 && GetWorld()->GetTimeSeconds() >= SearchEpisode.EndsAt)
	{
		EnterSearchLocalGuard();
	}
	return SearchEpisode.bActive && !SearchEpisode.bLocalGuard;
}

bool AEnemyBase::CanCompleteGeneralSearch(uint64 Serial) const
{
	return HasAuthority() && GetWorld() && !IsDead() && CanUseEnemyPerception()
		&& CombatState == EEnemyCombatState::Combat && SearchEpisode.bActive
		&& SearchEpisode.Serial == Serial && SearchEpisode.InvestigationSerial == 0
		&& SearchEpisode.Controller.Get() == GetController()
		&& SearchEpisode.Generation == PoolGameplayGeneration && SearchEpisode.Lease == PoolLeaseSerial
		&& SearchEpisode.bLocalGuard
		&& SearchEpisode.GuardEndsAt >= 0.0 && GetWorld()->GetTimeSeconds() >= SearchEpisode.GuardEndsAt
		&& !IsPlayerCurrentlyVisible() && !HasTrackableTarget()
		&& !IsLocationInvestigationActive() && !IsLocationInvestigationGoal()
		&& !IsHitClueValid(PendingHitClue) && !IsHitResponseCommitted() && !HasMeaningfulSearchHint();
}

bool AEnemyBase::TryCompleteGeneralSearch(uint64 Serial)
{
	if (!HasAuthority() || !GetWorld())
	{
		return false;
	}
	// 전이 조건을 만든 뒤 같은 프레임에 들어온 새 단서를 먼저 채택한다.
	// 과거 수색의 완료가 새 전투/조사의 귀환으로 이어지지 않게 확정 직전에 다시 검사한다.
	RefreshPerceptionGoal();
	if (!CanCompleteGeneralSearch(Serial))
	{
		return false;
	}
	EndSearchEpisode();
	ResetReturnHomeRecovery();
	// 방 전투의 종료와 개별 수색의 종료는 별개다. 방 안에서도 원래 배치 위치로 돌아간다.
	// 최초 Alert/청각 종료의 증원 대기 정책은 기존 공통 종료 경로에 그대로 둔다.
	CombatState = EEnemyCombatState::Returning;
	ClearAlertClue();
	bInCombat = false;
	RefreshCombatStatePresentation();
	RefreshPerceptionConfigForCurrentState();
	const int32 Generation = PoolGameplayGeneration;
	const int32 Lease = PoolLeaseSerial;
	const TWeakObjectPtr<AController> ReturnController = GetController();
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, Serial, Generation, Lease, ReturnController]()
		{
			// 귀환 확정 뒤 새 단서/제어권이 들어왔으면 이전 회차의 전이 이벤트를 보내지 않는다.
			if (CombatState == EEnemyCombatState::Returning && !SearchEpisode.bActive
				&& SearchEpisode.Serial == Serial && PoolGameplayGeneration == Generation
				&& PoolLeaseSerial == Lease && GetController() == ReturnController.Get() && CanUseEnemyPerception())
			{
				SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Combat.AlertCleared")));
			}
		}));
	return true;
}

bool AEnemyBase::GetSearchLookPitch(float& OutPitch) const
{
	OutPitch = 0.0f;
	if (!SearchEpisode.bActive || IsA<AAutoTurret>() || SearchEpisode.LookLocation.ContainsNaN())
	{
		return false;
	}
	const FVector Direction = SearchEpisode.LookLocation - GetCombatShotOrigin();
	const float Limit = FMath::IsFinite(RuntimeStat.SearchPitchLimit)
		? FMath::Clamp(RuntimeStat.SearchPitchLimit, 0.0f, 45.0f) : 45.0f;
	OutPitch = FMath::Clamp(Direction.Rotation().Pitch, -Limit, Limit);
	return true;
}

void AEnemyBase::EndSearchEpisode(bool bPreserveNearNoiseFacing)
{
	if (!HasAuthority())
	{
		return;
	}
	if (SearchEpisode.bActive && bNoiseInvestigationActive)
	{
		LogNoiseInvestigationDiagnostic(TEXT("SearchEnded"), NoiseInvestigationClue, TEXT("ReleaseOwnedEpisode"));
	}
	if (SearchEpisode.bActive && !(bPreserveNearNoiseFacing && NoiseDirectionConfirmation.Clue.bNearDirectionReaction))
	{
		EndNoiseDirectionConfirmation(NoiseDirectionConfirmation.Serial);
	}
	ReleaseSearchDestinations();
	// 행동 소유권은 반환하지만 동일 단서의 지점/기한은 남긴다. 새 실제 단서/lease만 새 예산을 만든다.
	SearchEpisode.bActive = false;
}

void AEnemyBase::CompleteSearchAnchor()
{
	if (!HasAuthority() || !SearchEpisode.bActive)
	{
		return;
	}
	// 도착한 회차의 정보만 소비한다. 이동 중 받은 더 최신 정보는 다음 회차가 확인한다.
	ConsumedSharedTargetRevision = FMath::Max(ConsumedSharedTargetRevision, SearchEpisode.SharedRevision);
}

bool AEnemyBase::SetSearchPoints(const TArray<FVector>& Points)
{
	if (!HasAuthority() || !SearchEpisode.bActive || SearchEpisode.bLocalGuard
		|| SearchEpisode.bPointsSelected || Points.Num() > GetSearchPointLimit())
	{
		return false;
	}
	for (const FVector& Point : Points)
	{
		if (Point.ContainsNaN())
		{
			return false;
		}
	}
	// 경로 검증을 통과한 유한한 지점을 한 번만 확정한다. 이동 중 후보를 보충하지 않는다.
	SearchEpisode.Points = Points;
	SearchEpisode.bPointsSelected = true;
	SearchEpisode.PointIndex = 0;
	if (Points.IsEmpty())
	{
		EnterSearchLocalGuard();
	}
	return true;
}

bool AEnemyBase::CompleteSearchPoint()
{
	if (!HasAuthority() || !SearchEpisode.bActive
		|| !RefreshSearchEpisodeLimit()
		|| !SearchEpisode.Points.IsValidIndex(SearchEpisode.PointIndex))
	{
		return false;
	}
	// 도착 후 LookAround까지 끝낸 지점을 기록하고 다음 지점으로 진행한다.
	SearchEpisode.CheckedPoints.Add(SearchEpisode.Points[SearchEpisode.PointIndex++]);
	if (!SearchEpisode.Points.IsValidIndex(SearchEpisode.PointIndex))
	{
		EnterSearchLocalGuard();
	}
	return true;
}

void AEnemyBase::EnterSearchLocalGuard()
{
	if (!HasAuthority() || !SearchEpisode.bActive || SearchEpisode.bLocalGuard)
	{
		return;
	}
	CompleteSearchAnchor();
	// 경계의 시계는 같은 단서의 State 재진입으로 연장하지 않는다.
	// 청각/피격의 전용 종료 시계는 부모 조사 Task가 계속 소유한다.
	if (SearchEpisode.InvestigationSerial == 0 && SearchEpisode.GuardEndsAt < 0.0)
	{
		const float Duration = FMath::IsFinite(RuntimeStat.SearchGuardSeconds)
			? FMath::Max(0.01f, RuntimeStat.SearchGuardSeconds) : 3.0f;
		SearchEpisode.GuardEndsAt = GetWorld()->GetTimeSeconds() + Duration;
	}
	SearchEpisode.bLocalGuard = true;
	ReleaseSearchDestinations();
	if (IsNoiseSearchActive())
	{
		PauseTargetAttack();
	}
	else
	{
		StopCurrentAttack();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
}

void AEnemyBase::HandleCurrentRoomTagChanged(
	FGameplayTag PreviousRoomTag,
	FGameplayTag NewRoomTag)
{
	(void)PreviousRoomTag;
	RemoveRoomTargetObserver();
	ClearSharedTargetContact();
	ReleaseSearchDestinations();
	// 위치 변경은 기존 방의 탐색 정보를 먼저 정리한다. 추적 진입의 참여 판정만
	// 전투 관리자에 맡기며 DefaultRoomTag와 원래 방의 Enemy 등록은 옮기지 않는다.
	if (HasAuthority())
	{
		if (URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
		{
			Combat->TryStartCombatFromPursuit(this, NewRoomTag);
		}
	}
}

void AEnemyBase::RemoveRoomTargetObserver()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->RemoveRoomTargetObserver(this);
	}
}

void AEnemyBase::HandleDeath()
{
	ResetReturnHomeRecovery();
	UE_LOG(LogOutlier, Display, TEXT("[DeathDebris] Death Enemy=%s Collection=%s Generation=%d Lease=%d"),
		*GetName(), *GetNameSafe(DeathDebrisCollection), GetCombatGameplayGeneration(), PoolLeaseSerial);
	// 파편 컬렉션이 지정된 적만 연출을 탄다. 없으면 기존 사망 동작 그대로다.
	// Super 가 아래에서 SetLifeSpan 으로 파괴를 예약하므로 그 전에 브로드캐스트해야
	// 액터 채널이 닫히기 전에 RPC 가 나간다 ( DeathDestroyDelay 주석 참고 ).
	if (DeathDebrisCollection)
	{
		MulticastPlayDeathDebris(uint32(GetCombatGameplayGeneration()), PoolLeaseSerial,
			GetMesh() ? GetMesh()->GetComponentTransform() : GetActorTransform(), GetVelocity());
	}

	PerformDeathCleanup();
	if (IsPoolManaged())
	{
		SetPoolState(EEnemyPoolState::DeathPresentation);
		ForceNetUpdate();
		OnPoolDeathPresentationStarted(PoolGameplayGeneration, PoolLeaseSerial);
		return;
	}

	const float DestroyDelay = FMath::Max(
		GetDeathDestroyDelay(),
		DeathDebrisCollection ? DeathDebrisDelay + 0.05f : 0.0f);
	if (DestroyDelay > KINDA_SMALL_NUMBER)
	{
		SetLifeSpan(DestroyDelay);
	}
	else
	{
		Destroy();
	}
}

void AEnemyBase::MulticastAdaptationShieldHit_Implementation(FVector_NetQuantize ImpactLocation)
{
	if (AdaptationShieldComponent)
	{
		AdaptationShieldComponent->NotifyShieldHit(ImpactLocation);
	}
}

void AEnemyBase::PlayAdaptationShieldBreak(EEnemyAdaptationState BrokenState, UNiagaraSystem* BreakVFX)
{
	if (HasAuthority()
		&& AdaptationShieldComponent
		&& AdaptationShieldComponent->ShouldShowForState(BrokenState))
	{
		MulticastAdaptationShieldBreak(BreakVFX);
	}
}

void AEnemyBase::MulticastAdaptationShieldBreak_Implementation(UNiagaraSystem* BreakVFX)
{
	if (AdaptationShieldComponent)
	{
		AdaptationShieldComponent->PlayBreak(BreakVFX);
	}
}

void AEnemyBase::MulticastPlayDeathDebris_Implementation(
	uint32 GameplayGeneration, int32 LeaseSerial, FTransform SourceTransform, FVector_NetQuantize100 DeathVelocity)
{
	UOutlierArenaSubsystem* Arena = GetWorld() ? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!Arena || !Arena->IsDeathDebrisGenerationCurrent(GameplayGeneration))
	{
		UE_LOG(LogOutlier, Verbose, TEXT("[DeathDebris] Reject stale request Enemy=%s Generation=%u Current=%u"),
			*GetName(), GameplayGeneration, Arena ? Arena->GetGameplayGeneration() : 0);
		return;
	}
	// 판정 제거는 데디케이티드 서버를 포함한 모든 인스턴스에서 한다.
	// 같은 세대의 이전 lease 파편은 재생하되 새 대여의 원본 상태는 건드리지 않는다.
	const bool bSourceLeaseMatches = !IsPoolManaged()
		|| (uint32(PoolGameplayGeneration) == GameplayGeneration && PoolLeaseSerial == LeaseSerial
			&& PoolState != EEnemyPoolState::Idle);
	if (bSourceLeaseMatches) DisableDeathCollision();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 월드 소유 타이머와 스냅샷으로 풀 반환/재대여/파괴와 독립적으로 재생한다.
	const uint32 PresentationSerial = bSourceLeaseMatches ? ++DeathDebrisPresentationSerial : DeathDebrisPresentationSerial;
	const TWeakObjectPtr<AEnemyBase> WeakSource(this);
	const TWeakObjectPtr<UOutlierArenaSubsystem> WeakArena(Arena);
	const TStrongObjectPtr<UGeometryCollection> Collection(DeathDebrisCollection);
	const FGeometryCollectionDeathProfile Profile = DeathDebrisProfile;
	const auto SpawnDebris = [WeakSource, WeakArena, Collection, Profile, SourceTransform, DeathVelocity,
		PresentationSerial, GameplayGeneration, LeaseSerial, bSourceLeaseMatches]()
	{
		UOutlierArenaSubsystem* CurrentArena = WeakArena.Get();
		if (!CurrentArena || !CurrentArena->IsDeathDebrisGenerationCurrent(GameplayGeneration))
		{
			return;
		}
		const AOutlierDeathDebrisActor* Debris = AOutlierDeathDebrisActor::SpawnLocalDebris(
			CurrentArena->GetWorld(), Collection.Get(), SourceTransform, FVector(DeathVelocity), Profile, GameplayGeneration);
		AEnemyBase* Source = WeakSource.Get();
		// 새 대여의 메시를 이전 파편 연출이 숨기지 않는다. 생성 실패 시에도 원본은 유지한다.
		if (Debris && Source && bSourceLeaseMatches && Source->DeathDebrisPresentationSerial == PresentationSerial
			&& (!Source->IsPoolManaged() || (Source->PoolState != EEnemyPoolState::Idle
				&& uint32(Source->PoolGameplayGeneration) == GameplayGeneration && Source->PoolLeaseSerial == LeaseSerial)))
		{
			Source->HideSourceMeshes();
		}
		UE_LOG(LogOutlier, Display, TEXT("[DeathDebris] Spawn Source=%s Generation=%u Lease=%d Success=%d Collection=%s Location=%s"),
			*GetNameSafe(Source), GameplayGeneration, LeaseSerial, Debris != nullptr,
			*GetNameSafe(Collection.Get()), *SourceTransform.GetLocation().ToCompactString());
	};
	const float Delay = FMath::Max(DeathDebrisDelay, 0.0f);
	UE_LOG(LogOutlier, Display, TEXT("[DeathDebris] Requested Enemy=%s PoolState=%s Generation=%d Lease=%d Delay=%.3f Collection=%s Location=%s"),
		*GetName(), *UEnum::GetValueAsString(PoolState), int32(GameplayGeneration), LeaseSerial, Delay,
		*GetNameSafe(DeathDebrisCollection), *SourceTransform.GetLocation().ToCompactString());
	if (Delay > KINDA_SMALL_NUMBER)
	{
		DeathDebrisTimerHandle = Arena->ScheduleDeathDebris(GameplayGeneration, Delay, FTimerDelegate::CreateLambda(SpawnDebris));
		return;
	}
	SpawnDebris();
}

void AEnemyBase::DisableDeathCollision()
{
	// Actor 충돌 토글은 개별 컴포넌트의 NoCollision 설정을 되돌리지 못한다.
	auto DisableAndRemember = [this](UPrimitiveComponent* Component)
	{
		if (!Component)
		{
			return;
		}
		if (!SavedDeathCollision.ContainsByPredicate(
			[Component](const auto& Entry) { return Entry.Key.Get() == Component; }))
		{
			SavedDeathCollision.Emplace(Component, Component->GetCollisionEnabled());
		}
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	};
	if (UCapsuleComponent* EnemyCapsule = GetCapsuleComponent())
	{
		DisableAndRemember(EnemyCapsule);
	}

	if (CoreHitboxComponent)
	{
		DisableAndRemember(CoreHitboxComponent);
	}

	if (USkeletalMeshComponent* EnemyMesh = GetMesh())
	{
		DisableAndRemember(EnemyMesh);
	}

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->StopMovementImmediately();
		MovementComponent->DisableMovement();
	}
}

void AEnemyBase::HideSourceMeshes()
{
	if (USkeletalMeshComponent* EnemyMesh = GetMesh())
	{
		SaveSourceMeshVisibility(EnemyMesh);
		EnemyMesh->SetVisibility(false, true);
		EnemyMesh->SetHiddenInGame(true, true);
	}
}

void AEnemyBase::SaveSourceMeshVisibility(USceneComponent* SourceMeshRoot)
{
	if (!SourceMeshRoot)
	{
		return;
	}
	TArray<USceneComponent*> Components;
	SourceMeshRoot->GetChildrenComponents(true, Components);
	Components.Add(SourceMeshRoot);
	for (USceneComponent* Component : Components)
	{
		if (Component && !SavedSourceMeshVisibility.ContainsByPredicate(
			[Component](const FSourceMeshVisibility& Entry) { return Entry.Component.Get() == Component; }))
		{
			SavedSourceMeshVisibility.Add({
				Component, Component->IsVisible(), static_cast<bool>(Component->bHiddenInGame)});
		}
	}
}

void AEnemyBase::PerformDeathCleanup()
{
	ClearAlertClue();
	ClearHitClues();
	ClearHeardNoise();
	ResetTargetAttackContinuity();
	ClearTargetTracking();
	PossessionDamageMultiplier = 1.0f;
	ReleasePatrolDestination();
	if (bDeathCleanupPerformed)
	{
		return;
	}
	bDeathCleanupPerformed = true;
	EndHackEffect(EEnemyHackEffectEndReason::TargetDied);
	if (auto* Stencils = GetWorld()->GetSubsystem<UOutlierStencilSubsystem>())
	{
		Stencils->ClearActorEffects(this);
	}
	RefreshCombatStatePresentation();
	InterruptDamagePresentation(0.0f);

	if (HasAuthority())
	{
		ReportFinalAdaptationResult();
		const TSharedPtr<FOutlierStealthSession> KillingSession = MoveTemp(LastAcceptedStealthSession);
		AShooterCharacter* KillingShooter = LastAcceptedDirectShooter.Get();
		LastAcceptedDirectShooter.Reset();
		if (KillingShooter && !bIsPossessed)
		{
			if (UOutlierAbilitySystemComponent* AbilitySystem = KillingShooter->GetOutlierAbilitySystemComponent())
			{
				AbilitySystem->NotifyShooterStealthKill(KillingSession);
			}
		}
		if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
		{
			// 처치 결과를 Stack에 반영한 뒤 즉시 활성 목록에서 제외한다.
			// Pool 반환이나 EndPlay까지 남겨두면 이미 죽은 Enemy가 필드 파열 대상에 포함될 수 있다.
			AdaptationSubsystem->UnregisterEnemy(this);
		}
		if (URoomCombatSubsystem* CombatSubsystem =
			GetWorld()->GetSubsystem<URoomCombatSubsystem>())
		{
			// 일부 파생 적은 HandleDeath 대신 이 정리 함수를 직접 호출하므로 공통 경로에서 통보한다.
			CombatSubsystem->NotifyEnemyDefeated(this);
		}
	}

	EndPossessedImpactInputLock();
	EndImpactReaction();
	ResetPossessedAttackInput();
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->CancelLocalRecoilPresentation();
	}
	StopCurrentAttack();

	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->NotifyTargetActorRemoved(this);
	}

	RemoveRoomTargetObserver();
	ReleaseSearchDestinations();

	if (bIsPossessed)
	{
		if (HasAuthority())
		{
			if (APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(GetController()))
			{
				PartnerController->ReleaseEnemyPossession();
			}
		}
		ClearPossessedPlayerState();
	}

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Died")));

	if (StateTreeComponent)
	{
		StateTreeComponent->StopLogic(TEXT("Enemy died"));
	}

	DestroyPoolAIController();
}

UEnemyAdaptationSubsystem* AEnemyBase::GetEnemyAdaptationSubsystem()
{
	if (!CachedEnemyAdaptationSubsystem.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			CachedEnemyAdaptationSubsystem = World->GetSubsystem<UEnemyAdaptationSubsystem>();
		}
	}
	return CachedEnemyAdaptationSubsystem.Get();
}

void AEnemyBase::ReportFinalAdaptationResult()
{
	UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem();
	if (!AdaptationSubsystem)
	{
		return;
	}

	FEnemyAdaptationUpdateResult Result;
	if (LastAcceptedAdaptationDamageCategory == EOutlierAdaptationDamageCategory::Pistol
		&& AdaptationSubsystem->ReportPistolHit(
			this,
			GetCombatGameplayGeneration(),
			PoolLeaseSerial,
			true,
			Result))
	{
		// Stack 8 이상 권총 치명타는 명중 파괴가 최종 처리이며 NonGun 처치를 중복 적용하지 않는다.
		return;
	}

	AdaptationSubsystem->ReportEnemyDefeat(
		this,
		GetCombatGameplayGeneration(),
		PoolLeaseSerial,
		ResolveFinalKillCategory(LastAcceptedAdaptationDamageCategory),
		Result);
}

void AEnemyBase::HandleStartAttackInput()
{
	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] StartInput Enemy=%s Authority=%s Local=%s "
				"Possessed=%s Controller=%s"),
			*GetNameSafe(this),
			HasAuthority() ? TEXT("true") : TEXT("false"),
			IsLocallyControlled() ? TEXT("true") : TEXT("false"),
			bIsPossessed ? TEXT("true") : TEXT("false"),
			*GetNameSafe(GetController()));
	}

	if (!HasAuthority())
	{
		if (IsLocallyControlled())
		{
			ServerStartWeaponAttack();
		}
		return;
	}

	SetPossessedAttackHeld(true);
}

void AEnemyBase::HandleStopAttackInput()
{
	if (!HasAuthority())
	{
		if (IsLocallyControlled())
		{
			ServerStopWeaponAttack();
		}
		return;
	}

	SetPossessedAttackHeld(false);
}

void AEnemyBase::ServerStartWeaponAttack_Implementation()
{
	// 서버에서 HandleStartAttackInput을 다시 거쳐 bIsPossessed와 CurrentWeapon을 검증한다.
	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] StartRPCReceived Enemy=%s Possessed=%s Controller=%s"),
			*GetNameSafe(this),
			bIsPossessed ? TEXT("true") : TEXT("false"),
			*GetNameSafe(GetController()));
	}
	HandleStartAttackInput();
}

void AEnemyBase::ServerStopWeaponAttack_Implementation()
{
	HandleStopAttackInput();
}

void AEnemyBase::SetPossessedAttackHeld(bool bHeld)
{
	if (!HasAuthority())
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] SetHeldRejected Enemy=%s Reason=NoAuthority"), *GetNameSafe(this));
		}
		return;
	}

	if (!bIsPossessed)
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Error, TEXT("[EnemyPossessedAttackDiag] SetHeldRejected Enemy=%s Reason=NotPossessed"), *GetNameSafe(this));
		}
		ResetPossessedAttackInput();
		return;
	}

	if (!bHeld)
	{
		const bool bWasHeld = bPossessedAttackHeld;

		bPossessedAttackHeld = false;

		if (bWasHeld)
		{
			SendEnemyStateTreeEvent(
				FGameplayTag::RequestGameplayTag(
					TEXT("Enemy.Event.Attack.InputReleased")));
		}
		return;
	}

	if (bPossessedImpactInputLocked)
	{
		bPossessedAttackHeld = true;
		bPossessedAttackQueued = false;
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[EnemyPossessedAttackDiag] SetHeldDeferred Enemy=%s Reason=ImpactInputLocked"), *GetNameSafe(this));
		}
		return;
	}

	if (CombatState == EEnemyCombatState::Stun
		|| IsDead()
		|| !PossessedAttackStateTreeReference.IsValid()
		|| IsPossessedActionCommitted()
		|| bPossessedAttackHeld)
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[EnemyPossessedAttackDiag] SetHeldRejected Enemy=%s Reason=Guard "
					"Stun=%s Health=%.2f PossessedTreeValid=%s Committed=%s AlreadyHeld=%s"),
				*GetNameSafe(this),
				CombatState == EEnemyCombatState::Stun ? TEXT("true") : TEXT("false"),
				GetCurrentHealth(),
				PossessedAttackStateTreeReference.IsValid() ? TEXT("true") : TEXT("false"),
				IsPossessedActionCommitted() ? TEXT("true") : TEXT("false"),
				bPossessedAttackHeld ? TEXT("true") : TEXT("false"));
		}
		return;
	}

	bPossessedAttackHeld = true;
	bPossessedAttackQueued = true;
	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] AttackRequestAccepted Enemy=%s Held=true Queued=true"),
			*GetNameSafe(this));
	}

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Attack.InputPressed")));
}

void AEnemyBase::ResetPossessedAttackInput()
{
	bPossessedAttackHeld = false;
	bPossessedAttackQueued = false;
}

void AEnemyBase::HandleReleasePossessionInput(const FInputActionValue& Value)
{
	(void)Value;
	if (IsPossessedActionCommitted())
	{
		return;
	}

	APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(GetController());
	if (!PartnerController)
	{
		return;
	}

	PartnerController->ReleaseEnemyPossession();
}

void AEnemyBase::PlaySpawnAppearAudio()
{
	if (AppearAudioContext.IsValid())
	{
		OutlierAudioFeedback::World(this, *AppearAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
	}
}

void AEnemyBase::RefreshEnemyAudioState()
{
	const bool bActive = !IsDead() && (!IsPoolManaged() || PoolState == EEnemyPoolState::CombatActive);
	if (HoverAudioContext.IsValid())
	{
		OutlierAudioFeedback::Loop(this, *HoverAudioContext.ToString(), bActive, TEXT("Audio.Type.Enemy"));
	}
	if (CombatLoopAudioContext.IsValid())
	{
		OutlierAudioFeedback::Loop(this, *CombatLoopAudioContext.ToString(), bActive && ShouldPlayCombatLoopAudio(), TEXT("Audio.Type.Enemy"));
	}
}

bool AEnemyBase::ShouldPlayCombatLoopAudio() const
{
	return bInCombat;
}

void AEnemyBase::RefreshAdaptationShieldPresentation()
{
	if (GetNetMode() != NM_DedicatedServer && AdaptationShieldComponent)
	{
		// 중앙 내성 단계는 유지하고, 전투 여부만 표시 조건으로 사용한다.
		// 두 복제 속성 중 어느 쪽이 먼저 도착해도 현재 상태를 다시 투영한다.
		const bool bShowShield = IsInCombat() && !IsDead()
			&& (!IsPoolManaged() || PoolState == EEnemyPoolState::CombatActive);
		AdaptationShieldComponent->ApplyAdaptationState(
			bShowShield ? AdaptationState : EEnemyAdaptationState::Normal);
	}
}

void AEnemyBase::RefreshCombatStatePresentation()
{
	RefreshEnemyAudioState();
	RefreshAdaptationShieldPresentation();
}

void AEnemyBase::OnRep_InCombat()
{
	RefreshCombatStatePresentation();
}
