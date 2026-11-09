#include "Enemy/EnemyBase.h"

#include "Event/OutlierEventSubsystem.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Enemy/AutoTurret.h"
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
			if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
			{
				// RoomTag가 없는 배치 적도 아레나 전역의 현재 전투 필드 후보로 관리한다.
				AdaptationSubsystem->RegisterEnemy(this);
			}
			if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
			{
				RoomSubsystem->RegisterEnemy(this);
			}
			if (URoomCombatSubsystem* CombatSubsystem =
				GetWorld()->GetSubsystem<URoomCombatSubsystem>())
			{
				CombatSubsystem->RegisterPreplacedEnemy(this);
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
			MulticastPlayDeathDebris(GetMesh() ? GetMesh()->GetComponentTransform() : GetActorTransform(), GetVelocity());
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
	if (GetNetMode() == NM_DedicatedServer) return;

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
		if (auto* Stencils = GetWorld()->GetSubsystem<UOutlierStencilSubsystem>()) Stencils->ClearActorEffects(this);
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
	SharedTargetRevision = 0;
	ConsumedSharedTargetRevision = 0;
	bHasLastKnownPlayerLocation = false;
	LastAcceptedAdaptationDamageCategory = EOutlierAdaptationDamageCategory::Ignore;
	LastAcceptedStealthSession.Reset();
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

FVector AEnemyBase::GetCombatAimPoint(const AActor* TargetActor) const
{
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

	ClearAlertClue();
	ClearHitClues();
	InterruptDamagePresentation(0.05f);
	bPossessedAttackHeld = false;
	EndPossessedImpactInputLock();
	bIsPossessed = bNewIsPossessed;
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
	if (!HasAuthority() || !bIsPossessed || IsDead()) return;
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
	if (!HasAuthority() || bPlayerCurrentlyVisible == bNewVisible) return;
	bPlayerCurrentlyVisible = bNewVisible;
	if (bNewVisible && DetectAudioContext.IsValid())
		OutlierAudioFeedback::World(this, *DetectAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
	RefreshTargetTracking();
}

AActor* AEnemyBase::GetTrackedTarget() const
{
	if (!HasAuthority() || !CanUseEnemyPerception() || IsDead() || CombatState == EEnemyCombatState::Stun) return nullptr;
	const AEnemyAIController* TargetController = TrackingController.Get();
	AActor* Target = TrackedTarget.Get();
	if (!TargetController || GetController() != TargetController || TargetController->GetPawn() != this
		|| !TargetController->IsValidTrackingTarget(Target)) return nullptr;
	if (bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(Target)) return Target;
	if (CombatState != EEnemyCombatState::Combat) return nullptr;
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

void AEnemyBase::ObserveTrackedTarget(AActor* Target)
{
	if (!HasAuthority() || !IsValid(Target)) return;
	AEnemyAIController* TargetController = Cast<AEnemyAIController>(GetController());
	if (!TargetController || !CanUseEnemyPerception() || !TargetController->IsValidTrackingTarget(Target)) return;
	if (PreparedAttackTarget.IsValid() && (PreparedAttackTarget.Get() != Target
		|| PreparedAttackWeapon.Get() != CurrentWeapon || PreparedAttackController.Get() != TargetController))
		ResetTargetAttackContinuity();
	if (TrackedTarget.Get() != Target) ClearTargetTracking();
	TrackedTarget = Target;
	TrackingController = TargetController;
	bTrackingExpiredForSearch = false;
	UpdateLastKnownPlayerLocation(Target->GetActorLocation());
}

void AEnemyBase::RefreshTargetTracking()
{
	if (!HasAuthority()) return;
	ON_SCOPE_EXIT { RefreshPerceptionGoal(); };
	if (!CanUseEnemyPerception() || IsDead() || CombatState == EEnemyCombatState::Stun)
	{
		// 추적 중단과 실제 Sight 상실은 다르다. 기절 중인 정상 감지 플래그를 지우지 않는다.
		ResetTargetAttackContinuity();
		ClearTargetTracking();
		return;
	}
	AEnemyAIController* TargetController = Cast<AEnemyAIController>(GetController());
	if (!TargetController || TargetController->GetPawn() != this) { ClearTargetTracking(); return; }
	if (!TrackingRooms.IsValid()) TrackingRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
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
	if (!Target && bHasRoomObservation) Target = PreviousTarget;
	if (!Target)
	{
		// 현재 대상이 무효일 때만 새 후보를 비교한다. 직접 시야의 다른 대상이 기존 추적을 빼앗지 않는다.
		AActor* Visible = bPlayerCurrentlyVisible ? TargetController->GetCachedVisibleTarget() : nullptr;
		Target = TargetController->IsValidTrackingTarget(Visible) ? Visible : nullptr;
		bHasRoomObservation = bCanShare && Rooms->TrySelectRoomTarget(this, Observation);
		if (bHasRoomObservation)
		{
			AActor* Shared = Observation.TargetActor.Get();
			const auto SharedPriority = OutlierEnemyTargetRules::GetPriority(Shared);
			const auto VisiblePriority = OutlierEnemyTargetRules::GetPriority(Target);
			if (!Target || SharedPriority < VisiblePriority
				|| (SharedPriority == VisiblePriority && FVector::DistSquared(GetActorLocation(), Shared->GetActorLocation())
					< FVector::DistSquared(GetActorLocation(), Target->GetActorLocation()))) Target = Shared;
			else bHasRoomObservation = Rooms->TryGetRoomTargetObservation(this, Target, Observation);
		}
	}
	if (!Target)
	{
		if (bPlayerCurrentlyVisible && !TargetController->IsValidTrackingTarget(TargetController->GetCachedVisibleTarget()))
			bPlayerCurrentlyVisible = false;
		if (TrackingController.IsValid()) FinishTargetTracking();
		return;
	}
	const bool bDirect = bPlayerCurrentlyVisible && TargetController->IsTargetDirectlyVisible(Target);
	if (bDirect) LastPerceptionObservedAt = GetWorld()->GetTimeSeconds();
	else if (bHasRoomObservation)
		LastPerceptionObservedAt = FMath::Max(LastPerceptionObservedAt, Observation.LastObservedAt);
	ObserveTrackedTarget(Target);
	bTrackingFromRoom = bHasRoomObservation;
	TrackingRoomTag = bHasRoomObservation ? GetDefaultRoomTag() : FGameplayTag();
	const double Deadline = bHasRoomObservation ? Observation.TrackingExpiresAt : TargetTrackingExpiresAt;
	const bool bNeedsExpiryTimer = !bDirect && !(bHasRoomObservation && Observation.bDirectlyObserved);
	if (!bNeedsExpiryTimer) GetWorldTimerManager().ClearTimer(TargetTrackingTimer);
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
		const bool bSamePreparedTarget = PreparedAttackTarget.Get() == Target
			&& PreparedAttackWeapon.Get() == CurrentWeapon && PreparedAttackController.Get() == TargetController;
		bResumeTargetAttackPreparation = bSamePreparedTarget;
		if (!bSamePreparedTarget) ResetTargetAttackContinuity();
		ConsumedSharedTargetRevision = SharedTargetRevision;
		if (!IsLocationInvestigationActive()) EndSearchEpisode();
	}
	bTrackingAnnounced = true;
	if (bNewTarget || bWasDirect != bDirect)
		SendEnemyStateTreeEvent(bNewTarget
			? FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Perception.TargetAcquired")) : TAG_EnemyTrackingChanged);
}

bool AEnemyBase::RecordHeardNoise(AActor* Source, const FVector& Location, double OccurredAt, bool* bValidStimulus)
{
	if (bValidStimulus) *bValidStimulus = false;
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
	if (bValidStimulus) *bValidStimulus = true;
	HeardNoiseCandidates.RemoveAll([this](const FEnemyHeardNoise& Candidate) { return !IsHeardNoiseValid(Candidate); });
	FEnemyHeardNoise Incoming;
	Incoming.Source = Source;
	Incoming.Location = Location;
	Incoming.OccurredAt = OccurredAt;
	// 근접한 동일 출처 입력만 병합한다. 다른 출처의 오래된 유효 소리는 거리 비교에 남긴다.
	FEnemyHeardNoise* Nearby = HeardNoiseCandidates.FindByPredicate([&](const FEnemyHeardNoise& Candidate)
	{
		return Candidate.Source.Get() == Source && FVector::DistSquared(Candidate.Location, Location) <= FMath::Square(100.0);
	});
	if (Nearby)
	{
		if (OccurredAt <= Nearby->OccurredAt) return false;
		*Nearby = Incoming;
	}
	else
	{
		if (HeardNoiseCandidates.Num() == MaxHeardNoiseCandidates)
		{
			// 용량이 찼을 때 과거 관측·채택 이전 후보가 새 조사 근거를 모두 밀어내지 않게 한다.
			// 같은 관측 구간 안에서는 가까운 후보, 동거리에서는 최신 후보를 남긴다.
			const double EvidenceAt = bNoiseInvestigationActive
				? FMath::Max(LastPerceptionObservedAt, NoiseInvestigationAt) : LastPerceptionObservedAt;
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
					|| (bSameEvidenceGroup && bFartherOrOlder)) Worst = Index;
			}
			const double IncomingDistance = FVector::DistSquared(GetActorLocation(), Location);
			const double WorstDistance = FVector::DistSquared(GetActorLocation(), HeardNoiseCandidates[Worst].Location);
			const bool bIncomingAfterObservation = OccurredAt > EvidenceAt;
			const bool bWorstAfterObservation = HeardNoiseCandidates[Worst].OccurredAt > EvidenceAt;
			const bool bSameEvidenceGroup = bIncomingAfterObservation == bWorstAfterObservation;
			const bool bFartherOrDuplicate = IncomingDistance > WorstDistance
				|| (IncomingDistance == WorstDistance && OccurredAt <= HeardNoiseCandidates[Worst].OccurredAt);
			if ((!bIncomingAfterObservation && bWorstAfterObservation)
				|| (bSameEvidenceGroup && bFartherOrDuplicate)) return false;
			HeardNoiseCandidates.RemoveAtSwap(Worst);
		}
		HeardNoiseCandidates.Add(Incoming);
	}
	// 소비 경계에는 선택 결과가 아니라 수신한 전체 후보의 최신 시각을 사용한다.
	LatestHeardNoiseAt = FMath::Max(LatestHeardNoiseAt, OccurredAt);
	RefreshPerceptionGoal();
	return true;
}

bool AEnemyBase::TryGetHeardNoise(FEnemyHeardNoise& OutNoise) const
{
	OutNoise = FEnemyHeardNoise();
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (!IsHeardNoiseValid(Candidate)) continue;
		const double Distance = FVector::DistSquared(GetActorLocation(), Candidate.Location);
		const double BestDistance = FVector::DistSquared(GetActorLocation(), OutNoise.Location);
		const bool bKeepSelection = Distance == BestDistance && Candidate.OccurredAt == OutNoise.OccurredAt
			&& Candidate.Location == PerceptionGoal.Location && Candidate.OccurredAt == PerceptionGoal.InformationAt;
		if (OutNoise.OccurredAt < 0.0 || Distance < BestDistance
			|| (Distance == BestDistance && Candidate.OccurredAt > OutNoise.OccurredAt) || bKeepSelection) OutNoise = Candidate;
	}
	return OutNoise.OccurredAt >= 0.0;
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
	if (!IsHeardNoiseValid(Noise) || Noise.OccurredAt <= NoiseInvestigationAt) return false;
	const float Interval = FMath::IsFinite(NoiseDestinationUpdateInterval) ? FMath::Max(NoiseDestinationUpdateInterval, 0.0f) : 1.0f;
	const float CoordinateMargin = FMath::IsFinite(NoiseDestinationChangeDistance) ? FMath::Max(NoiseDestinationChangeDistance, 0.0f) : 100.0f;
	if (GetWorld()->GetTimeSeconds() - LastNoiseDestinationChangedAt < Interval
		|| Noise.Location.Equals(NoiseInvestigationLocation, UE_SMALL_NUMBER)
		|| FVector::DistSquared(Noise.Location, NoiseInvestigationLocation) < FMath::Square(CoordinateMargin)) return false;
	const double CurrentDistance = FVector::Distance(GetActorLocation(), NoiseInvestigationLocation);
	const double CandidateDistance = FVector::Distance(GetActorLocation(), Noise.Location);
	const float Margin = FMath::IsFinite(NoiseCandidateSwitchDistance) ? FMath::Max(NoiseCandidateSwitchDistance, 0.0f) : 200.0f;
	return FMath::IsNearlyEqual(CurrentDistance, CandidateDistance, UE_SMALL_NUMBER)
		|| (CandidateDistance < CurrentDistance && CurrentDistance - CandidateDistance >= Margin);
}

bool AEnemyBase::SelectHeardNoise(FEnemyHeardNoise& OutNoise, bool bRequireNewObservation) const
{
	OutNoise = FEnemyHeardNoise();
	const FEnemyHeardNoise* Best = nullptr;
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
	{
		if (!IsHeardNoiseValid(Candidate) || (bRequireNewObservation && Candidate.OccurredAt <= LastPerceptionObservedAt)
			|| (bNoiseInvestigationActive && !CanReplaceNoiseDestination(Candidate))) continue;
		const double Distance = FVector::Distance(GetActorLocation(), Candidate.Location);
		const double BestDistance = Best ? FVector::Distance(GetActorLocation(), Best->Location) : 0.0;
		const bool bSameDistance = Best && FMath::IsNearlyEqual(Distance, BestDistance, UE_SMALL_NUMBER);
		// 거리·시각까지 같으면 기존 선택을 남겨 후보 배열 정리 순서에 영향받지 않는다.
		const bool bKeepSelection = bSameDistance && Candidate.OccurredAt == Best->OccurredAt
			&& Candidate.Location == PerceptionGoal.Location && Candidate.OccurredAt == PerceptionGoal.InformationAt;
		if (!Best || (!bSameDistance && Distance < BestDistance)
			|| (bSameDistance && Candidate.OccurredAt > Best->OccurredAt) || bKeepSelection) Best = &Candidate;
	}
	if (!Best) return false;
	OutNoise = *Best;
	return true;
}

bool AEnemyBase::RecordAlertClue(double OccurredAt)
{
	if (!CanStorePerceptionClue() || !FMath::IsFinite(OccurredAt) || OccurredAt < 0.0
		|| OccurredAt > GetWorld()->GetTimeSeconds() || OccurredAt <= LastAlertClueAt) return false;
	// 수신·재평가 시각이 아니라 원래 발생 시각으로 비교해 중복·역순 입력의 기한 연장을 막는다.
	LastAlertClueAt = OccurredAt;
	return true;
}

bool AEnemyBase::IsAlertHoldActive() const
{
	if (!CanStorePerceptionClue() || !HasAlertClue()) return false;
	const float Duration = FMath::IsFinite(RuntimeStat.MinAlertHoldAfterClue) && RuntimeStat.MinAlertHoldAfterClue > 0.0f
		? RuntimeStat.MinAlertHoldAfterClue : 4.0f;
	return GetWorld()->GetTimeSeconds() < LastAlertClueAt + Duration;
}

void AEnemyBase::ClearAlertClue()
{
	if (HasAuthority()) LastAlertClueAt = -1.0;
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
		|| NextHitClueSerial == MAX_uint64) return false;
	const double ExpiresAt = OccurredAt + LifetimeSeconds;
	if (!FMath::IsFinite(ExpiresAt) || ExpiresAt <= GetWorld()->GetTimeSeconds()) return false;
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
		|| Clue.Serial <= LastRecordedHitClueSerial || Clue.OccurredAt < LastRecordedHitClueAt) return false;
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
	if (!IsHitClueValid(PendingHitClue)) return false;
	OutClue = PendingHitClue;
	return true;
}

bool AEnemyBase::BeginHitInvestigation(uint64 ClueSerial, FEnemyHitClue& OutClue)
{
	OutClue = FEnemyHitClue();
	if (CombatState == EEnemyCombatState::Stun || !CanUseEnemyPerception()
		|| PendingHitClue.Serial != ClueSerial || !IsHitClueValid(PendingHitClue)) return false;
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
	if (!IsHitInvestigationCurrent(ClueSerial)) return false;
	if (HasHitResponse()) FinishHitResponse();
	ActiveHitClue = FEnemyHitClue();
	return true;
}

bool AEnemyBase::ConsumeHitClue(uint64 ClueSerial)
{
	if (!HasAuthority() || ClueSerial == 0 || PendingHitClue.Serial != ClueSerial) return false;
	// 직접 시야 전투 등으로 채택하지 않은 사건도 소비한다. 실행 중인 다른 회차는 건드리지 않는다.
	PendingHitClue = FEnemyHitClue();
	return true;
}

void AEnemyBase::ClearHitClues()
{
	if (!HasAuthority()) return;
	if (HasHitResponse()) FinishHitResponse();
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
	if (!HasHitResponse()) return;
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
	if (!HasAuthority() || ObservedLocation.ContainsNaN()) return;
	// 실제 관측이 먼저 소유권을 회수한다. 옛 비행/수색의 완료 콜백은 회차 번호가 달라 거절된다.
	const bool bWasInvestigating = HasHitResponse();
	FinishHitResponse();
	PendingHitClue = FEnemyHitClue();
	// 관측 전에 발급만 된 사건도 뒤늦게 옛 조사로 복원되지 않게 소비 경계를 올린다.
	LastRecordedHitClueSerial = NextHitClueSerial;
	LastRecordedHitClueAt = FMath::Max(LastRecordedHitClueAt, GetWorld()->GetTimeSeconds());
	if (bWasInvestigating || bObservedInvestigationPending)
	{
		// 인지 확정 전의 짧은 관측도 옛 피격 조사를 대체한다. 다시 숨으면 이 관측 위치만 조사한다.
		bObservedInvestigationPending = true;
		ObservedInvestigationLocation = ObservedLocation;
		ObservedInvestigationAt = GetWorld()->GetTimeSeconds();
		UpdateLastKnownPlayerLocation(ObservedLocation);
	}
	RefreshPerceptionGoal();
}

void AEnemyBase::TickHitResponse(float DeltaTime)
{
	if (!HasAuthority() || !GetWorld() || bProcessingDamageHitResponse) return;
	if (PendingHitClue.Serial != 0 && !IsHitClueValid(PendingHitClue)) PendingHitClue = FEnemyHitClue();
	if (!HasHitResponse() && PendingHitClue.Serial == 0 && !bObservedInvestigationPending) return;
	AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (!CanStorePerceptionClue() || !AI) return;
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
			OnDirectSightForHitResponse(Visible->GetActorLocation());
		return;
	}
	if (IsHitResponseCommitted()) return;
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
		if (bReturningCooldown) ConsumeHitClue(Pending.Serial);
		else if (!HasHitResponse() || Now - HitConfirmationStartedAt >= MinConfirm)
		{
			const FVector OldDirection = (ActiveHitClue.Location - GetActorLocation()).GetSafeNormal();
			const FVector NewDirection = (Pending.Location - GetActorLocation()).GetSafeNormal();
			const float MergeAngle = FMath::Clamp(Positive(RuntimeStat.HitDirectionMergeAngle, 20.0f), 0.1f, 180.0f);
			const bool bSameDirection = HasHitResponse() && !OldDirection.IsNearlyZero() && !NewDirection.IsNearlyZero()
				&& FVector::DotProduct(OldDirection, NewDirection) >= FMath::Cos(FMath::DegreesToRadians(MergeAngle));
			if (bSameDirection) ConsumeHitClue(Pending.Serial);
			else
			{
				const bool bObservedLocation = bInvestigatingObservedLocation && !HasHitResponse();
				FinishHitResponse();
				bInvestigatingObservedLocation = bObservedLocation;
				if (bNoiseInvestigationActive) EndNoiseInvestigation(false);
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
						if (UCharacterMovementComponent* Movement = GetCharacterMovement()) Movement->StopMovementImmediately();
						if (CombatState == EEnemyCombatState::NonCombat) EnterAlertFromPerception(ActiveHitClue.Location);
					}
					RefreshPerceptionGoal();
					SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
				}
			}
		}
	}
	if (!IsHitDirectionConfirmationActive()) return;
	bool bReached = false;
	if (AAutoTurret* Turret = Cast<AAutoTurret>(this))
		bReached = Turret->UpdateTurretAimAtLocation(ActiveHitClue.Location, DeltaTime, false);
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
	if (Elapsed < MinConfirm || (!bReached && Elapsed < MaxConfirm)) return;
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
	if (!HasHitResponse()) return BeginNoiseInvestigation(OutDestination);
	if (!CanRunHitInvestigationMovement() || !IsLocationInvestigationCurrent(NoiseInvestigationSerial)) return false;
	bHitMovementAdopted = true;
	OutDestination = ActiveHitClue.Location;
	return true;
}

bool AEnemyBase::TryUpdateLocationInvestigationDestination(FVector& OutDestination)
{
	if (!HasHitResponse()) return TryUpdateNoiseInvestigationDestination(OutDestination);
	OutDestination = ActiveHitClue.Location;
	return IsLocationInvestigationCurrent(NoiseInvestigationSerial);
}

bool AEnemyBase::IsLocationInvestigationCurrent(uint64 Serial) const
{
	if (!HasHitResponse()) return IsNoiseInvestigationCurrent(Serial);
	return Serial == NoiseInvestigationSerial && CanStorePerceptionClue() && CanUseEnemyPerception()
		&& CombatState != EEnemyCombatState::Stun && !IsHitResponseCommitted()
		&& HitResponsePhase != EHitResponsePhase::Search;
}

bool AEnemyBase::BeginLocationInvestigationSearch(uint64 Serial, const FVector& ReachableCenter)
{
	if (!HasHitResponse()) return BeginNoiseInvestigationSearch(Serial, ReachableCenter);
	if (!IsLocationInvestigationCurrent(Serial) || IsHitDirectionConfirmationActive()
		|| ReachableCenter.ContainsNaN() || !ReachableCenter.Equals(GetActorLocation(), 1.0)) return false;
	HitSearchCenter = ReachableCenter;
	HitResponsePhase = EHitResponsePhase::Search;
	RefreshPerceptionGoal();
	return BeginSearchEpisode();
}

void AEnemyBase::EndLocationInvestigation()
{
	if (!HasHitResponse()) { EndNoiseInvestigation(); return; }
	FinishHitResponse();
	RefreshPerceptionGoal();
}

bool AEnemyBase::CompleteLocationInvestigation()
{
	if (!HasHitResponse()) return CompleteNoiseInvestigation();
	if (!HasAuthority() || !IsLocationSearchActive() || !CanContinueSearchEpisode()) return false;
	FinishHitResponse();
	RefreshTargetTracking();
	RefreshPerceptionGoal();
	const UEnemyRoomSubsystem* Rooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	if (CombatState == EEnemyCombatState::Combat && Rooms && Rooms->IsRoomInCombat(GetDefaultRoomTag())) return true;
	if (PerceptionGoal.Type != EEnemyPerceptionGoalType::Combat && !IsLocationInvestigationGoal()) CommitToNonCombatAfterSearch();
	return true;
}

void AEnemyBase::ClearHeardNoise()
{
	if (bNoiseInvestigationActive) EndSearchEpisode();
	bNoiseInvestigationActive = false;
	bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	NoiseInvestigationLocation = FVector::ZeroVector;
	NoiseInvestigationSource.Reset();
	NoiseInvestigationAt = LastNoiseDestinationChangedAt = ConsumedNoiseAt = -1.0;
	LatestHeardNoiseAt = -1.0;
	HeardNoiseCandidates.Reset();
	// 재사용·빙의·사망 경계에서는 채택한 조사 의도와 관측 시각도 함께 폐기한다.
	PerceptionGoal = FEnemyPerceptionGoal();
	LastPerceptionObservedAt = -1.0;
	if (GetWorld()) GetWorldTimerManager().ClearTimer(PerceptionGoalTimer);
}

void AEnemyBase::RefreshPerceptionGoal()
{
	if (!HasAuthority() || !GetWorld()) return;
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
		if (!IsHeardNoiseValid(Candidate)) continue;
		NextEvaluationAt = FMath::Min(NextEvaluationAt, Candidate.OccurredAt + HeardNoiseLifetime);
		// 이동하면서 거리 우위가 달라질 수 있으므로, 지금 먼 후보도 유지 종료 때 다시 비교한다.
		if (bNoiseInvestigationActive && Candidate.OccurredAt > NoiseInvestigationAt && LastNoiseDestinationChangedAt + Hold > Now)
			NextEvaluationAt = FMath::Min(NextEvaluationAt, LastNoiseDestinationChangedAt + Hold);
	}
	if (bNoiseInvestigationActive && NoiseInvestigationAt + HeardNoiseLifetime > Now)
		NextEvaluationAt = FMath::Min(NextEvaluationAt, NoiseInvestigationAt + HeardNoiseLifetime);
	if (bCanDecide && NextEvaluationAt < TNumericLimits<double>::Max())
	{
		const float Remaining = static_cast<float>(FMath::Max(NextEvaluationAt - Now, 0.001));
		if (!GetWorldTimerManager().TimerExists(PerceptionGoalTimer)
			|| !FMath::IsNearlyEqual(GetWorldTimerManager().GetTimerRemaining(PerceptionGoalTimer), Remaining, 0.01f))
			GetWorldTimerManager().SetTimer(PerceptionGoalTimer, this, &AEnemyBase::RefreshPerceptionGoal, Remaining, false);
	}
	else GetWorldTimerManager().ClearTimer(PerceptionGoalTimer);

	if (bCanDecide)
	{
		AActor* Tracked = GetTrackedTarget();
		AActor* Visible = bPlayerCurrentlyVisible ? AI->GetCachedVisibleTarget() : nullptr;
		if (bPlayerCurrentlyVisible && AI->IsTargetDirectlyVisible(Tracked)) Visible = Tracked;
		if (!AI->IsValidTrackingTarget(Visible) || !AI->IsTargetDirectlyVisible(Visible)) Visible = nullptr;
		// 피격 조사는 추적 지식을 지우지 않고 행동 목표만 선점한다. 청각/공유 후보의 시계는 계속 흐른다.
		if (HasHitResponse() && !bHitReturnDirectionOnly && !Visible && !IsHitResponseCommitted())
		{
			Next.Type = HitResponsePhase == EHitResponsePhase::Search
				? EEnemyPerceptionGoalType::Search : EEnemyPerceptionGoalType::InvestigateHit;
			Next.Location = HitResponsePhase == EHitResponsePhase::Search ? HitSearchCenter : ActiveHitClue.Location;
			Next.InformationAt = ActiveHitClue.OccurredAt;
			const bool bChanged = Next.Type != PerceptionGoal.Type || Next.Location != PerceptionGoal.Location;
			PerceptionGoal = Next;
			if (bChanged) SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
			return;
		}
		if (Visible) LastPerceptionObservedAt = Now;

		FEnemyRoomTargetObservationSnapshot Shared;
		bool bLiveShared = false;
		if (!Visible && (bFreshNoise || PerceptionGoal.Type == EEnemyPerceptionGoalType::InvestigateNoise || IsNoiseSearchActive())
			&& CombatState == EEnemyCombatState::Combat && CanUseRoomTargetSharing())
		{
			if (!TrackingRooms.IsValid()) TrackingRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
			if (UEnemyRoomSubsystem* Rooms = TrackingRooms.Get())
			{
				bLiveShared = Rooms->TryGetRoomTargetObservation(this, Tracked, Shared) && Shared.bDirectlyObserved;
				if (!bLiveShared) bLiveShared = Rooms->TrySelectRoomTarget(this, Shared, true);
				if (bLiveShared) LastPerceptionObservedAt = FMath::Max(LastPerceptionObservedAt, Shared.LastObservedAt);
			}
		}

		// 채택 전에는 가까운 후보를, 채택 후에는 전환 조건을 통과한 후보만 목표에 공개한다.
		// 대기 후보가 기존 이동 방향이나 공유와의 비교 위치를 먼저 바꾸지 않게 한다.
		bFreshNoise = SelectHeardNoise(Noise, !bLiveShared);
		if (bNoiseInvestigationActive && !bFreshNoise)
		{
			Noise.Source = NoiseInvestigationSource;
			Noise.Location = NoiseInvestigationLocation;
			Noise.OccurredAt = NoiseInvestigationAt;
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
				bChooseNoise = (bFreshNoise && Noise.OccurredAt > LastPerceptionObservedAt) || bNoiseInvestigationActive;
		}

		if (IsPossessedActionCommitted()) Next.Type = EEnemyPerceptionGoalType::CommittedAction;
		else if (bChooseNoise)
		{
			Next = bInvestigating ? PerceptionGoal : FEnemyPerceptionGoal();
			Next.Type = EEnemyPerceptionGoalType::InvestigateNoise;
			// 유효 시간이 지난 대기 후보는 버리고, 이미 채택해 이동하던 위치만 유지한다.
			if (bNoiseInvestigationActive)
			{
				Next.Location = NoiseInvestigationLocation;
				Next.InformationAt = NoiseInvestigationAt;
			}
			if (bFreshNoise && (!bNoiseInvestigationActive || Noise.OccurredAt > Next.InformationAt))
			{
				Next.Location = Noise.Location;
				Next.InformationAt = Noise.OccurredAt;
			}
			// 도착한 회차는 고정 중심에서 수색한다. 더 새로운 소리만 이동 단계로 되돌린다.
			if (IsNoiseSearchActive() && !IsNoiseDestinationChangeReady(Next.Location, Next.InformationAt))
			{
				Next.Type = EEnemyPerceptionGoalType::Search;
				Next.Location = NoiseInvestigationLocation;
				Next.InformationAt = NoiseInvestigationAt;
			}
		}
		else if (AActor* Target = Visible ? Visible : (bLiveShared ? Shared.TargetActor.Get() : Tracked))
		{
			Next.Type = EEnemyPerceptionGoalType::Combat;
			Next.Target = Target;
			Next.Location = bLiveShared ? Shared.LastObservedLocation : Target->GetActorLocation();
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
		ConsumedNoiseAt = FMath::Max(ConsumedNoiseAt, LatestHeardNoiseAt);
		bResumeTargetAttackPreparation = Next.Type == EEnemyPerceptionGoalType::Combat
			&& PreparedAttackTarget.Get() == Next.Target.Get() && PreparedAttackWeapon.Get() == CurrentWeapon
			&& PreparedAttackController.Get() == GetController();
		bNoiseInvestigationActive = bNoiseInvestigationSearching = false;
		++NoiseInvestigationSerial;
		EndSearchEpisode();
	}
	const bool bChanged = Next.Type != PerceptionGoal.Type || Next.Target != PerceptionGoal.Target
		|| ((Next.Type == EEnemyPerceptionGoalType::InvestigateNoise || Next.Type == EEnemyPerceptionGoalType::Search)
			&& Next.Location != PerceptionGoal.Location);
	// 후보 수신과 목표 채택을 구분한다. 선택된 소리의 원래 시각만 반영하며 조회로 연장하지 않는다.
	if (Next.Type == EEnemyPerceptionGoalType::InvestigateNoise) RecordAlertClue(Next.InformationAt);
	PerceptionGoal = Next;
	// 의도 변경만 알린다. 기존 전투 대상·이동·공격 상태는 이 단계에서 변경하지 않는다.
	if (bChanged) SendEnemyStateTreeEvent(TAG_EnemyTrackingChanged);
}

bool AEnemyBase::BeginNoiseInvestigation(FVector& OutDestination)
{
	OutDestination = FVector::ZeroVector;
	if (!HasAuthority() || !GetWorld() || !CanUseEnemyPerception() || IsDead()
		|| (CombatState != EEnemyCombatState::Combat && CombatState != EEnemyCombatState::Alert)
		|| IsPossessedActionCommitted()) return false;
	RefreshPerceptionGoal();
	if (PerceptionGoal.Type != EEnemyPerceptionGoalType::InvestigateNoise) return false;
	EndSearchEpisode();
	bNoiseInvestigationActive = true;
	bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	NoiseInvestigationLocation = PerceptionGoal.Location;
	NoiseInvestigationAt = PerceptionGoal.InformationAt;
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
		if (Candidate.OccurredAt == NoiseInvestigationAt && Candidate.Location == NoiseInvestigationLocation) NoiseInvestigationSource = Candidate.Source;
	LastNoiseDestinationChangedAt = GetWorld()->GetTimeSeconds();
	OutDestination = NoiseInvestigationLocation;
	if (!HasTrackableTarget()) UpdateLastKnownPlayerLocation(NoiseInvestigationLocation);
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
	if (!IsNoiseInvestigationCurrent(NoiseInvestigationSerial) || bNoiseInvestigationSearching
		|| !IsNoiseDestinationChangeReady(PerceptionGoal.Location, PerceptionGoal.InformationAt)) return false;
	NoiseInvestigationLocation = PerceptionGoal.Location;
	NoiseInvestigationAt = PerceptionGoal.InformationAt;
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
		if (Candidate.OccurredAt == NoiseInvestigationAt && Candidate.Location == NoiseInvestigationLocation) NoiseInvestigationSource = Candidate.Source;
	LastNoiseDestinationChangedAt = GetWorld()->GetTimeSeconds();
	OutDestination = NoiseInvestigationLocation;
	if (!HasTrackableTarget()) UpdateLastKnownPlayerLocation(NoiseInvestigationLocation);
	RefreshPerceptionGoal();
	return true;
}

bool AEnemyBase::IsNoiseDestinationChangeReady(const FVector& Location, double InformationAt) const
{
	for (const FEnemyHeardNoise& Candidate : HeardNoiseCandidates)
		if (Candidate.OccurredAt == InformationAt && Candidate.Location == Location)
			return CanReplaceNoiseDestination(Candidate);
	return false;
}

bool AEnemyBase::BeginNoiseInvestigationSearch(uint64 Serial, const FVector& ReachableCenter)
{
	if (!IsNoiseInvestigationCurrent(Serial) || ReachableCenter.ContainsNaN()
		|| !ReachableCenter.Equals(GetActorLocation(), 1.0)) return false;
	// 이동 실패도 실제 도달한 위치에서만 수색한다. 벽 너머의 원래 좌표를 도달점으로 만들지 않는다.
	NoiseInvestigationLocation = ReachableCenter;
	if (!HasTrackableTarget()) UpdateLastKnownPlayerLocation(ReachableCenter);
	bNoiseInvestigationSearching = true;
	if (!BeginSearchEpisode())
	{
		bNoiseInvestigationSearching = false;
		return false;
	}
	RefreshPerceptionGoal();
	return IsNoiseSearchActive();
}

void AEnemyBase::EndNoiseInvestigation(bool bConsumeNoise)
{
	if (!HasAuthority() || !bNoiseInvestigationActive) return;
	bResumeTargetAttackPreparation = PreparedAttackTarget.IsValid() && PreparedAttackTarget.Get() == GetTrackedTarget()
		&& PreparedAttackWeapon.Get() == CurrentWeapon && PreparedAttackController.Get() == GetController();
	if (bConsumeNoise) ConsumedNoiseAt = FMath::Max(ConsumedNoiseAt, LatestHeardNoiseAt);
	bNoiseInvestigationActive = bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	EndSearchEpisode();
	PerceptionGoal = FEnemyPerceptionGoal();
	RefreshPerceptionGoal();
}

bool AEnemyBase::CompleteNoiseInvestigation()
{
	if (!HasAuthority() || !IsNoiseSearchActive() || !CanContinueSearchEpisode()) return false;
	// 완료 출력 직후 새 시야·소리가 도착할 수 있다. 최신 의도를 확인한 뒤에만 회차를 소비한다.
	RefreshTargetTracking();
	if (!IsNoiseSearchActive() || PerceptionGoal.Type != EEnemyPerceptionGoalType::Search) return false;
	EndNoiseInvestigation();
	if (PerceptionGoal.Type == EEnemyPerceptionGoalType::Combat) return true;
	const UEnemyRoomSubsystem* Rooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	// 개인 조사가 끝나도 진행 중인 Room 전투를 종료하지 않는다. 기존 전투 수색·경계로 돌아간다.
	if (CombatState == EEnemyCombatState::Combat && Rooms && Rooms->IsRoomInCombat(GetDefaultRoomTag())) return true;
	CommitToNonCombatAfterSearch();
	return true;
}

void AEnemyBase::ClearTargetTracking()
{
	if (!HasAuthority()) return;
	GetWorldTimerManager().ClearTimer(TargetTrackingTimer);
	TrackedTarget.Reset();
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
	if (!HasAuthority() || GetTrackedTarget()) return;
	if (bTargetAttackActive) PauseTargetAttack();
	const AEnemyAIController* TargetController = TrackingController.Get();
	// 만료 뒤 실제 좌표를 다시 읽지 않는다. 마지막 유효 갱신 위치가 수색 중심으로 남는다.
	// 정상 만료는 동일 대상의 준비 기록을 보존하고 사망/은신/팀 변경은 폐기한다.
	if (!TargetController || TargetController != GetController() || !TargetController->IsValidTrackingTarget(TrackedTarget.Get()))
		ResetTargetAttackContinuity();
	ClearTargetTracking();
	if (CombatState != EEnemyCombatState::Combat || !CanUseEnemyPerception() || IsDead()) return;
	bTrackingExpiredForSearch = true;
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!HasTrackableTarget() && bTrackingExpiredForSearch)
			SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(TEXT("Enemy.Event.Perception.TargetLost")));
	}));
}

void AEnemyBase::ApplySharedTargetContact(
	const FVector& TargetLocation,
	bool bDeferStateTreeEvent,
	uint64 Revision)
{
	if (!HasAuthority()
		|| (CombatState != EEnemyCombatState::Combat
			&& !(IsPoolManaged() && CombatState == EEnemyCombatState::Alert))
		|| IsAIControlSuppressed()
		|| !CanUseRoomTargetSharing())
	{
		return;
	}

	if (TargetLocation.ContainsNaN()) return;
	// 기존 호출자는 좌표 변경을 새 정보로 취급한다. 방 공유는 명시적 번호로 재목격을 구분한다.
	const bool bContactChanged = Revision != 0 ? Revision > SharedTargetRevision
		: !bHasSharedTargetContact || !SharedTargetLocation.Equals(TargetLocation, 1.0f);
	if (Revision != 0 && Revision < SharedTargetRevision) return;
	if (bContactChanged) SharedTargetRevision = Revision != 0 ? Revision : SharedTargetRevision + 1;
	// 직접 시야가 더 정확하다. 이때의 공유 정보는 이미 확인한 것으로 처리한다.
	if (bPlayerCurrentlyVisible || HasTrackableTarget()) ConsumedSharedTargetRevision = SharedTargetRevision;
	bHasSharedTargetContact = true;
	SharedTargetLocation = TargetLocation;
	if (!bPlayerCurrentlyVisible && !HasTrackableTarget() && !bTrackingAnnounced && !bTrackingExpiredForSearch)
		UpdateLastKnownPlayerLocation(TargetLocation);
	if (bContactChanged && !HasHitResponse()) ReleaseSearchDestinations();
	if (IsLocationInvestigationActive()) RefreshPerceptionGoal();

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
		UpdateLastKnownPlayerLocation(SharedTargetLocation);
	bHasSharedTargetContact = false;
	if (IsNoiseInvestigationActive()) RefreshPerceptionGoal();

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
	if (bPropagateToRoom && RoomTag.IsValid())
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
		|| !FMath::IsFinite(OccurredAt) || OccurredAt < 0.0 || OccurredAt > GetWorld()->GetTimeSeconds()) return false;
	UpdateLastKnownPlayerLocation(HeardLocation);
	if (!CommitAlertToCombat()) return false;

	// 완료한 출처의 마지막 소리로 조사 회차를 넘긴다. 이전 부분 시야의 추적 유예나
	// 다른 출처의 조사 좌표가 청각 완료를 실제 대상 위치 추적으로 바꾸지 않게 분리한다.
	ClearTargetTracking();
	EndSearchEpisode();
	bNoiseInvestigationActive = true;
	bNoiseInvestigationSearching = false;
	++NoiseInvestigationSerial;
	NoiseInvestigationSource = Source;
	NoiseInvestigationLocation = HeardLocation;
	NoiseInvestigationAt = OccurredAt;
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
		|| (CombatState != EEnemyCombatState::Alert && CombatState != EEnemyCombatState::Combat)) return false;
	// 최초 경계의 모든 해제 경로가 같은 기한을 따른다. Combat 수색/Returning의 종료 시간은 바꾸지 않는다.
	if (CombatState == EEnemyCombatState::Alert
		&& (IsAlertHoldActive() || IsHitInvestigationCurrent(ActiveHitClue.Serial))) return false;
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
	if (!IsValid(Home) || Home->IsActorBeingDestroyed() || !IsValid(Capsule)) return false;
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
	if (!Home->TryClampFlightLocation(Requested + Offset, Extent, Center)) return false;
	OutDestination = Center - Offset;
	return true;
}

bool AEnemyBase::CompleteReturnHome()
{
	if (!HasAuthority() || CombatState != EEnemyCombatState::Returning || IsDead()
		|| IsAIControlSuppressed() || !IsInsideDefaultRoom())
	{
		return false;
	}
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

	InterruptDamagePresentation(0.05f);
	ClearTargetTracking();
	PreStunCombatState = CombatState;
	ReleasePatrolDestination();
	CombatState = EEnemyCombatState::Stun;
	RefreshPerceptionGoal();
	RefreshCombatStatePresentation();
	if (DownAudioContext.IsValid())
		OutlierAudioFeedback::World(this, *DownAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
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
	const AWeaponBase* DirectWeapon = Cast<AWeaponBase>(Request.DamageCauser);
	const AShooterCharacter* DirectShooter = DirectWeapon
		? Cast<AShooterCharacter>(DirectWeapon->GetOwner()) : nullptr;
	const bool bDirectStealthAttack = !Request.bReflectedDamage && !bIsPossessed
		&& (Request.DamageTag.MatchesTagExact(OutlierGameplayTags::Damage::Ranged())
			|| Request.DamageTag.MatchesTagExact(OutlierGameplayTags::Damage::Melee()))
		&& Request.StealthSession && DirectShooter
		&& Request.StealthSession->Shooter.Get() == DirectShooter;
	// GAS의 Health 변경 콜백은 ApplyDamageInternal 안에서 즉시 사망 처리를 호출한다.
	// 이번 공격 분류를 먼저 기록해야 사망 경로가 정확한 최종 원인을 소비할 수 있다.
	LastAcceptedAdaptationDamageCategory = Request.AdaptationDamageCategory;
	// 사망 콜백이 동기 실행되므로 막타의 직접 공격 자격도 피해 적용 전에 교체한다.
	// 환경/폭발/Partner 피해는 이전 공격의 자격을 상속하지 않는다.
	LastAcceptedStealthSession = bDirectStealthAttack ? Request.StealthSession : nullptr;
	const bool bDamageApplied = ApplyDamageInternal(
		FinalDamage,
		Request.EventInstigator,
		Request.DamageCauser,
		Request.DamageTag);
	if (!bDamageApplied)
	{
		LastAcceptedAdaptationDamageCategory = PreviousDamageCategory;
		LastAcceptedStealthSession = PreviousStealthSession;
	}
	else if (!IsDead())
	{
		// 비치명타의 자격이 나중의 스크립트 사망이나 수명 종료에 재사용되지 않게 한다.
		LastAcceptedStealthSession.Reset();
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
				PoolGameplayGeneration,
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
			if (FMath::IsFinite(Duration) && Duration > 0.0f) AppliedHitStunDuration = Duration;
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
		if (CreateHitClue(Request.DamageOrigin, HitOccurredAt, Lifetime, Clue)) RecordHitClue(Clue);
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
	if (!CanAttackTrackedTarget(TargetActor)) return false;
	if (bTargetAttackActive && ActiveAttackTarget.Get() == TargetActor && CurrentWeapon->IsAttacking()) return true;
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
	bResumeTargetAttackPreparation = false;
	return true;
}

bool AEnemyBase::CanAttackTrackedTarget(const AActor* TargetActor) const
{
	return HasAuthority() && CombatState == EEnemyCombatState::Combat
		&& !IsAIControlSuppressed() && !IsDead() && !IsLocationInvestigationActive() && TargetActor
		&& TargetActor == GetTrackedTarget() && IsValid(CurrentWeapon)
		&& CurrentWeapon->HasClearTargetAttackPath(TargetActor, GetCombatAimPoint(TargetActor));
}

bool AEnemyBase::RefreshTargetAttackForShot()
{
	AActor* Target = ActiveAttackTarget.Get();
	return bTargetAttackActive && CanAttackTrackedTarget(Target)
		&& UpdateAttackLocation(GetCombatAimPoint(Target));
}

void AEnemyBase::PauseTargetAttack()
{
	if (!HasAuthority()) return;
	if (IsValid(CurrentWeapon)) CurrentWeapon->PauseTargetAttack();
	ActiveAttackTarget.Reset();
	bTargetAttackActive = false;
	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
		EnemyController->SetAttackAimActive(false);
	if (AttackPhase == EEnemyAttackPhase::Firing) SetAttackPhase(EEnemyAttackPhase::Idle);
}

void AEnemyBase::RememberTargetAttackPreparation()
{
	if (!HasAuthority() || !GetTrackedTarget() || bIsPossessed) return;
	PreparedAttackTarget = GetTrackedTarget();
	PreparedAttackWeapon = CurrentWeapon;
	PreparedAttackController = Cast<AEnemyAIController>(GetController());
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
	if (HasAuthority() && GetWorld())
		TargetAttackRecoveryEndsAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, Duration);
}

float AEnemyBase::GetTargetAttackRecoveryRemaining() const
{
	return GetWorld() ? FMath::Max(0.0, TargetAttackRecoveryEndsAt - GetWorld()->GetTimeSeconds()) : 0.0f;
}

void AEnemyBase::ResetTargetAttackContinuity()
{
	if (!HasAuthority()) return;
	PreparedAttackTarget.Reset();
	PreparedAttackWeapon.Reset();
	PreparedAttackController.Reset();
	ActiveAttackTarget.Reset();
	bTargetAttackActive = false;
	bResumeTargetAttackPreparation = false;
	TargetAttackRecoveryEndsAt = 0.0;
	StopCurrentAttack();
}

bool AEnemyBase::StartAttackLocation(const FVector& TargetLocation)
{
	if (IsLocationInvestigationActive()) return false;
	// 수색 중 병렬 공격 Task가 잘못 연결되어도 숨은 플레이어에게 사격하지 않는다.
	if (SearchEpisode.bActive && !bPlayerCurrentlyVisible && !bIsPossessed) return false;
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
	if (SearchEpisode.bActive && !bPlayerCurrentlyVisible && !bIsPossessed) return false;
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

	FVector ViewLocation;
	FRotator ViewRotation;
	ActiveController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Direction = TargetLocation - ViewLocation;
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	FRotator AimRotation = Direction.Rotation();
	AimRotation.Roll = 0.0f;

	// ControlRotation에는 Pitch까지 보존해 사격 방향과 VEC의 3인칭 조준 연출이 같은 값을 사용하게 한다.
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
	if (!HasAuthority() || !GetWorld()) return;
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
		|| (!bNoiseSearch && !bHasLastKnownPlayerLocation && !HasFreshSharedTargetLocation())) return false;
	// 이전 회차의 예약부터 해제하고, 이번 회차가 사용할 중심과 정보 버전을 고정한다.
	// 이후 LKP가 갱신되어도 진행 중인 수색 중심은 바꾸지 않는다.
	EndSearchEpisode();
	SearchEpisode.bActive = true;
	SearchEpisode.Serial = ++NextSearchEpisodeSerial;
	SearchEpisode.SharedRevision = SharedTargetRevision;
	SearchEpisode.Center = HitResponsePhase == EHitResponsePhase::Search ? HitSearchCenter : bNoiseSearch ? NoiseInvestigationLocation
		: (!bTrackingExpiredForSearch && HasFreshSharedTargetLocation() ? SharedTargetLocation : LastKnownPlayerLocation);
	bTrackingExpiredForSearch = false;
	if (bNoiseSearch) PauseTargetAttack();
	else StopCurrentAttack();
	if (UCharacterMovementComponent* Movement = GetCharacterMovement()) Movement->StopMovementImmediately();
	return true;
}

void AEnemyBase::EndSearchEpisode()
{
	if (!HasAuthority()) return;
	ReleaseSearchDestinations();
	SearchEpisode = FEnemySearchEpisode();
}

void AEnemyBase::CompleteSearchAnchor()
{
	if (!HasAuthority() || !SearchEpisode.bActive) return;
	// 도착한 회차의 정보만 소비한다. 이동 중 받은 더 최신 정보는 다음 회차가 확인한다.
	ConsumedSharedTargetRevision = FMath::Max(ConsumedSharedTargetRevision, SearchEpisode.SharedRevision);
}

bool AEnemyBase::SetSearchPoints(const TArray<FVector>& Points)
{
	if (!HasAuthority() || !SearchEpisode.bActive || SearchEpisode.bLocalGuard
		|| SearchEpisode.bPointsSelected || Points.Num() > 2) return false;
	for (const FVector& Point : Points)
	{
		if (Point.ContainsNaN()) return false;
	}
	// 경로 검증을 통과한 최대 두 지점을 한 번만 확정한다. 이동 중 후보를 보충하지 않는다.
	SearchEpisode.Points = Points;
	SearchEpisode.bPointsSelected = true;
	SearchEpisode.PointIndex = 0;
	if (Points.IsEmpty()) EnterSearchLocalGuard();
	return true;
}

bool AEnemyBase::CompleteSearchPoint()
{
	if (!HasAuthority() || !SearchEpisode.bActive
		|| !SearchEpisode.Points.IsValidIndex(SearchEpisode.PointIndex)) return false;
	// 도착 후 LookAround까지 끝낸 지점을 기록하고 다음 지점으로 진행한다.
	SearchEpisode.CheckedPoints.Add(SearchEpisode.Points[SearchEpisode.PointIndex++]);
	if (!SearchEpisode.Points.IsValidIndex(SearchEpisode.PointIndex)) EnterSearchLocalGuard();
	return true;
}

void AEnemyBase::EnterSearchLocalGuard()
{
	if (!HasAuthority() || !SearchEpisode.bActive) return;
	CompleteSearchAnchor();
	// 방 전투와 수색 회차는 유지하되 이동 예약은 반납한다.
	// 현재 위치에서 경계하다가 직접 재감지 또는 새 공유 정보로 다시 행동한다.
	SearchEpisode.bLocalGuard = true;
	ReleaseSearchDestinations();
	if (IsNoiseSearchActive()) PauseTargetAttack();
	else StopCurrentAttack();
	if (UCharacterMovementComponent* Movement = GetCharacterMovement()) Movement->StopMovementImmediately();
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
	UE_LOG(LogOutlier, Display, TEXT("[DeathDebris] Death Enemy=%s Collection=%s Generation=%d Lease=%d"),
		*GetName(), *GetNameSafe(DeathDebrisCollection), PoolGameplayGeneration, PoolLeaseSerial);
	// 파편 컬렉션이 지정된 적만 연출을 탄다. 없으면 기존 사망 동작 그대로다.
	// Super 가 아래에서 SetLifeSpan 으로 파괴를 예약하므로 그 전에 브로드캐스트해야
	// 액터 채널이 닫히기 전에 RPC 가 나간다 ( DeathDestroyDelay 주석 참고 ).
	if (DeathDebrisCollection)
	{
		MulticastPlayDeathDebris(GetMesh() ? GetMesh()->GetComponentTransform() : GetActorTransform(), GetVelocity());
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

void AEnemyBase::MulticastPlayDeathDebris_Implementation(FTransform SourceTransform, FVector_NetQuantize100 DeathVelocity)
{
	// 판정 제거는 데디케이티드 서버를 포함한 모든 인스턴스에서 한다.
	DisableDeathCollision();
	if (GetNetMode() == NM_DedicatedServer) return;

	// 월드 소유 타이머와 스냅샷으로 풀 반환/재대여/파괴와 독립적으로 재생한다.
	const uint32 PresentationSerial = ++DeathDebrisPresentationSerial;
	const TWeakObjectPtr<AEnemyBase> WeakSource(this);
	const TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
	const TStrongObjectPtr<UGeometryCollection> Collection(DeathDebrisCollection);
	const FGeometryCollectionDeathProfile Profile = DeathDebrisProfile;
	const auto SpawnDebris = [WeakSource, WeakWorld, Collection, Profile, SourceTransform, DeathVelocity, PresentationSerial]()
	{
		UWorld* World = WeakWorld.Get();
		if (!World) return;
		const AOutlierDeathDebrisActor* Debris = AOutlierDeathDebrisActor::SpawnLocalDebris(
			World, Collection.Get(), SourceTransform, FVector(DeathVelocity), Profile);
		AEnemyBase* Source = WeakSource.Get();
		// 새 대여의 메시를 이전 파편 연출이 숨기지 않는다. 생성 실패 시에도 원본은 유지한다.
		if (Debris && Source && Source->DeathDebrisPresentationSerial == PresentationSerial
			&& (!Source->IsPoolManaged() || Source->PoolState != EEnemyPoolState::Idle))
		{
			Source->HideSourceMeshes();
		}
		UE_LOG(LogOutlier, Display, TEXT("[DeathDebris] Spawn Source=%s Success=%d Collection=%s Location=%s"),
			*GetNameSafe(Source), Debris != nullptr, *GetNameSafe(Collection.Get()), *SourceTransform.GetLocation().ToCompactString());
	};
	const float Delay = FMath::Max(DeathDebrisDelay, 0.0f);
	UE_LOG(LogOutlier, Display, TEXT("[DeathDebris] Requested Enemy=%s PoolState=%s Generation=%d Lease=%d Delay=%.3f Collection=%s Location=%s"),
		*GetName(), *UEnum::GetValueAsString(PoolState), PoolGameplayGeneration, PoolLeaseSerial, Delay,
		*GetNameSafe(DeathDebrisCollection), *SourceTransform.GetLocation().ToCompactString());
	if (Delay > KINDA_SMALL_NUMBER)
	{
		GetWorldTimerManager().SetTimer(DeathDebrisTimerHandle,
			FTimerDelegate::CreateLambda(SpawnDebris), Delay, false);
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
	if (auto* Stencils = GetWorld()->GetSubsystem<UOutlierStencilSubsystem>()) Stencils->ClearActorEffects(this);
	RefreshCombatStatePresentation();
	InterruptDamagePresentation(0.0f);

	if (HasAuthority())
	{
		ReportFinalAdaptationResult();
		const TSharedPtr<FOutlierStealthSession> KillingSession = MoveTemp(LastAcceptedStealthSession);
		if (KillingSession && !bIsPossessed)
		{
			if (AShooterCharacter* Shooter = KillingSession->Shooter.Get())
			{
				if (UOutlierAbilitySystemComponent* AbilitySystem = Shooter->GetOutlierAbilitySystemComponent())
				{
					AbilitySystem->NotifyShooterStealthKill(KillingSession, !IsA<AAutoTurret>());
				}
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
			PoolGameplayGeneration,
			PoolLeaseSerial,
			true,
			Result))
	{
		// Stack 8 이상 권총 치명타는 명중 파괴가 최종 처리이며 NonGun 처치를 중복 적용하지 않는다.
		return;
	}

	AdaptationSubsystem->ReportEnemyDefeat(
		this,
		PoolGameplayGeneration,
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
		OutlierAudioFeedback::World(this, *AppearAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
}

void AEnemyBase::RefreshEnemyAudioState()
{
	const bool bActive = !IsDead() && (!IsPoolManaged() || PoolState == EEnemyPoolState::CombatActive);
	if (HoverAudioContext.IsValid())
		OutlierAudioFeedback::Loop(this, *HoverAudioContext.ToString(), bActive, TEXT("Audio.Type.Enemy"));
	if (CombatLoopAudioContext.IsValid())
		OutlierAudioFeedback::Loop(this, *CombatLoopAudioContext.ToString(), bActive && ShouldPlayCombatLoopAudio(), TEXT("Audio.Type.Enemy"));
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
