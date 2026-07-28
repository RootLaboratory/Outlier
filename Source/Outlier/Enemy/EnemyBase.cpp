#include "Enemy/EnemyBase.h"
#include "Camera/CameraComponent.h"
#include "Components/StateTreeComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/EMPGameplayTags.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "EnhancedInputComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AIPerceptionSystem.h"
#include "Team/OutlierTeamIds.h"
#include "TimerManager.h"
#include "Room/RoomTagComponent.h"
#include "Weapon/RangedWeaponBase.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// 캡슐이 PhysicsBody 채널을 기본(Block)으로 막고 있으면, 무기 트레이스가 캡슐에 먼저 걸려서
	// 캡슐 안쪽에 있는 몸통/코어 등 Physics Asset 본 바디까지 도달하지 못함 (팔다리처럼 캡슐 밖으로
	// 튀어나온 부위만 본 단위로 정상 검출됨) — 그래서 이동/충돌용 콜리전은 그대로 두고 이 채널만 무시
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);

	StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("StateTreeComponent"));
	// Enemy StateTree의 행동 Task는 서버 권한과 AIController를 전제로 한다.
	// 컴포넌트 자동 시작을 끄고 BeginPlay 초기화가 끝난 뒤 서버에서만 시작한다.
	StateTreeComponent->SetStartLogicAutomatically(false);
	EnemyCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("EnemyCameraComponent"));
	EnemyCameraComponent->SetupAttachment(GetRootComponent());

	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
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
}

void AEnemyBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	PatrolPointA = GetActorLocation();
	PatrolPointB = GetActorTransform().TransformPositionNoScale(PatrolPointBLocalOffset);

	if (!StateTreeComponent || !BattleStateTreeReference.IsValid())
	{
		return;
	}

	// BP 기본값이 확정된 뒤, StateTreeComponent가 BeginPlay에서 자동 시작되기 전에 Override를 등록한다.
	const FGameplayTag BattleStateTag = FGameplayTag::RequestGameplayTag(
		TEXT("Enemy.StateTree.Battle"));
	StateTreeComponent->AddLinkedStateTreeOverrides(
		BattleStateTag,
		BattleStateTreeReference);
}

void AEnemyBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEnemyBase, RuntimeStat);
	DOREPLIFETIME(AEnemyBase, CurrentHealth);
	DOREPLIFETIME(AEnemyBase, bInCombat);
	DOREPLIFETIME(AEnemyBase, LastKnownPlayerLocation);
	DOREPLIFETIME(AEnemyBase, PatternStartPlayerLocation);
	DOREPLIFETIME(AEnemyBase, CombatState);
	DOREPLIFETIME(AEnemyBase, bIsPossessed);
	DOREPLIFETIME(AEnemyBase, bPossessionInProgress);
	DOREPLIFETIME(AEnemyBase, bPlayerCurrentlyVisible);
	DOREPLIFETIME(AEnemyBase, bHasSharedTargetContact);
	DOREPLIFETIME(AEnemyBase, SharedTargetLocation);
	DOREPLIFETIME(AEnemyBase, AttackPhase);
	DOREPLIFETIME(AEnemyBase, CurrentWeapon);
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	if (RoomTagComponent)
	{
		RoomTagComponent->OnCurrentRoomTagChanged.AddUObject(
			this,
			&AEnemyBase::HandleCurrentRoomTagChanged);
	}

	if (HasAuthority())
	{
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->RegisterEnemy(this);
		}
	}

	InitializeFromEnemyStatRow();
	EquipDefaultWeapon();

	if (HasAuthority() && StateTreeComponent)
	{
		// Perception이 BeginPlay 전에 상태를 바꿨어도 Global Sync가 현재 값을 읽어
		// 올바른 초기 State를 선택할 수 있도록 모든 Enemy 초기화 뒤에 시작한다.
		StateTreeComponent->StartLogic();
	}
}

void AEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPossessionProcess();
	ResetPossessedAttackInput();
	StopCurrentAttack();
	RemoveRoomTargetObserver();
	ReleaseSearchRingSlot();

	if (HasAuthority())
	{
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

	Super::EndPlay(EndPlayReason);
}

void AEnemyBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

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
	StopCurrentAttack();
	Super::UnPossessed();

	SetEnemyPossessed(false);
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
	if (!HasAuthority() || !Tag.IsValid() || !StateTreeComponent)
	{
		return;
	}

	StateTreeComponent->SendStateTreeEvent(Tag);
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
		|| bCombatDecisionRefreshPending)
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
					|| bPlayerCurrentlyVisible)
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
	if (!HasAuthority() || bIsPossessed == bNewIsPossessed)
	{
		return;
	}

	const bool bWasPossessionInProgress = bPossessionInProgress;
	bIsPossessed = bNewIsPossessed;
	ResetPossessedAttackInput();

	if (bIsPossessed)
	{
		bPossessionInProgress = false;
		PossessionInstigatorPartner.Reset();
		if (HackableComponent)
		{
			HackableComponent->HackTags.RemoveTag(
				OutlierGameplayTags::State::PossessPending());
		}

		StopCurrentAttack();
		RemoveRoomTargetObserver();
		ClearSharedTargetContact();
		ReleaseSearchRingSlot();
	}

	if (!bIsPossessed)
	{
		ClearPossessedPlayerState();
	}

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController()))
	{
		EnemyAIController->SetEnemyPerceptionEnabled(!IsAIControlSuppressed());
	}

	RefreshPerceptionTeamRegistration();
	ForceNetUpdate();

	if (!bIsPossessed || !bWasPossessionInProgress)
	{
		SendEnemyStateTreeEvent(
			FGameplayTag::RequestGameplayTag(
				bIsPossessed
				? TEXT("Enemy.Event.Possession.Started")
				: TEXT("Enemy.Event.Possession.Ended")));
	}
}

bool AEnemyBase::BeginPossessionProcess(APartnerCharacter* PartnerCharacter)
{
	if (!HasAuthority()
		|| !IsValid(PartnerCharacter)
		|| bIsPossessed
		|| bPossessionInProgress
		|| !HackableComponent)
	{
		return false;
	}

	bPossessionInProgress = true;
	PossessionInstigatorPartner = PartnerCharacter;
	ResetPossessedAttackInput();
	HackableComponent->HackTags.AddTag(
		OutlierGameplayTags::State::PossessPending());

	StopCurrentAttack();
	RemoveRoomTargetObserver();
	ClearSharedTargetContact();
	ReleaseSearchRingSlot();
	SetPlayerCurrentlyVisible(false);

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController()))
	{
		EnemyAIController->SetEnemyPerceptionEnabled(false);
	}

	PartnerCharacter->SetEnemyPossessionProtection(true);
	ForceNetUpdate();

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Possession.Pending")));

	return true;
}

void AEnemyBase::ConfirmPossessionProcess()
{
	if (!HasAuthority() || !bPossessionInProgress || !HackableComponent)
	{
		return;
	}

	HackableComponent->HackTags.RemoveTag(
		OutlierGameplayTags::State::PossessPending());

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Possession.Started")));
}

void AEnemyBase::CancelPossessionProcess()
{
	if (!HasAuthority())
	{
		return;
	}

	const bool bHadPossessionProcess = bPossessionInProgress
		|| (HackableComponent
			&& HackableComponent->HasHackTag(
				OutlierGameplayTags::State::PossessPending()));
	if (!bHadPossessionProcess)
	{
		return;
	}

	bPossessionInProgress = false;
	if (HackableComponent)
	{
		HackableComponent->HackTags.RemoveTag(
			OutlierGameplayTags::State::PossessPending());
	}

	if (APartnerCharacter* PartnerCharacter = PossessionInstigatorPartner.Get())
	{
		PartnerCharacter->SetEnemyPossessionProtection(false);
	}
	PossessionInstigatorPartner.Reset();

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController()))
	{
		EnemyAIController->SetEnemyPerceptionEnabled(!bIsPossessed);
	}

	ForceNetUpdate();

	if (!bIsPossessed)
	{
		SendEnemyStateTreeEvent(
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

	ApplyClassStatOverrides();
	ApplyMovementFromRuntimeStat();
	CurrentHealth = RuntimeStat.Health;

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

	if (bNewVisible)
	{
		StopCurrentAttack();
		ReleaseSearchRingSlot();
	}

	const FGameplayTag PerceptionEvent = FGameplayTag::RequestGameplayTag(
		bNewVisible
			? TEXT("Enemy.Event.Perception.TargetAcquired")
			: TEXT("Enemy.Event.Perception.TargetLost"));

	if (bNewVisible)
	{
		SendEnemyStateTreeEvent(PerceptionEvent);
	}
	else
	{

		// StateTree Global Task가 갱신된 가시성으로 전이 조건을 평가하도록 상실 이벤트를 다음 틱에 보낸다.
		// 다음 틱 전에 재감지되었다면 오래된 TargetLost 이벤트는 폐기한다.
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(
				this,
				[this, PerceptionEvent]()
				{
					if (!bPlayerCurrentlyVisible)
					{
						SendEnemyStateTreeEvent(PerceptionEvent);
					}
				}));
	}
}

void AEnemyBase::ApplySharedTargetContact(const FVector& TargetLocation)
{
	if (!HasAuthority()
		|| CombatState != EEnemyCombatState::Combat
		|| IsAIControlSuppressed())
	{
		return;
	}

	const bool bContactChanged = !bHasSharedTargetContact
		|| !SharedTargetLocation.Equals(TargetLocation, 1.0f);
	bHasSharedTargetContact = true;
	SharedTargetLocation = TargetLocation;
	UpdateLastKnownPlayerLocation(TargetLocation);
	ReleaseSearchRingSlot();

	if (bContactChanged)
	{
		SendEnemyStateTreeEvent(
			FGameplayTag::RequestGameplayTag(
				TEXT("Enemy.Event.Combat.TargetShared")));
	}
}

void AEnemyBase::ClearSharedTargetContact()
{
	if (!HasAuthority() || !bHasSharedTargetContact)
	{
		return;
	}

	// 방에서 마지막으로 공유한 위치를 모든 적의 공통 수색 중심으로 보존한다.
	// 개별 Perception 콜백 순서 때문에 Partner 위치가 LKP를 덮어쓴 경우도 여기서 정규화된다.
	UpdateLastKnownPlayerLocation(SharedTargetLocation);
	bHasSharedTargetContact = false;

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
								SendEnemyStateTreeEvent(SharedTargetLostEvent);
							}
						}));
			}));
}

void AEnemyBase::EnterCombat(const FVector& PlayerLocation)
{
	EnterCombatInArena(PlayerLocation, INDEX_NONE, true);
}

void AEnemyBase::EnterCombatInArena(
	const FVector& PlayerLocation,
	int32 ArenaId,
	bool bPropagateToRoom,
	bool bDeferStateTreeEvent)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ArenaId != INDEX_NONE)
	{
		LastKnownArenaId = ArenaId;
		if (UEnemyRoomSubsystem* RoomSubsystem =
			GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->RefreshEnemyRegistration(this);
		}
	}

	if (CombatState == EEnemyCombatState::Stun)
	{
		PromotePreStunState(EEnemyCombatState::Combat);
		UpdateLastKnownPlayerLocation(PlayerLocation);
		return;
	}

	const bool bEnteredCombat = CombatState != EEnemyCombatState::Combat;
	bInCombat = true;
	CombatState = EEnemyCombatState::Combat;
	UpdateLastKnownPlayerLocation(PlayerLocation);

	if (bEnteredCombat)
	{
		RefreshPerceptionConfigForCurrentState();
	}

	const FGameplayTag RoomTag = GetDefaultRoomTag();

	const int32 PropagationArenaId = ArenaId != INDEX_NONE ? ArenaId : LastKnownArenaId;
	if (bPropagateToRoom && PropagationArenaId != INDEX_NONE && RoomTag.IsValid())
	{
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->NotifyRoomCombat(PropagationArenaId, RoomTag, PlayerLocation, this);
		}
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
	EnterAlertInArena(PlayerLocation, INDEX_NONE);
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
	EnterCombatInArena(LastKnownPlayerLocation, INDEX_NONE, true, true);
	return CombatState == EEnemyCombatState::Combat;
}

bool AEnemyBase::CommitAlertToNonCombat()
{
	if (!HasAuthority() || CombatState != EEnemyCombatState::Alert)
	{
		return false;
	}

	CombatState = EEnemyCombatState::NonCombat;
	bInCombat = false;
	RefreshPerceptionConfigForCurrentState();

	SendEnemyStateTreeEventNextTick(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Combat.AlertCleared")));

	return true;
}

void AEnemyBase::EnterAlertInArena(const FVector& PlayerLocation, int32 ArenaId)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ArenaId != INDEX_NONE)
	{
		LastKnownArenaId = ArenaId;
		if (UEnemyRoomSubsystem* RoomSubsystem =
			GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->RefreshEnemyRegistration(this);
		}
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
	UpdateLastKnownPlayerLocation(PlayerLocation);
	RefreshPerceptionConfigForCurrentState();

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Combat.Alerted")));
}

void AEnemyBase::EnterStun()
{
	if (!HasAuthority())
	{
		return;
	}

	if (CombatState == EEnemyCombatState::Stun)
	{
		return;
	}

	PreStunCombatState = CombatState;
	CombatState = EEnemyCombatState::Stun;
	ResetPossessedAttackInput();
	StopCurrentAttack();
	RemoveRoomTargetObserver();
	ReleaseSearchRingSlot();
	RefreshPerceptionConfigForCurrentState();

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Status.StunStarted")));
}

void AEnemyBase::RestoreStateAfterStun()
{
	if (!HasAuthority())
	{
		return;
	}

	if (CombatState != EEnemyCombatState::Stun)
	{
		return;
	}

	CombatState = PreStunCombatState;
	bInCombat = CombatState == EEnemyCombatState::Combat;
	RefreshPerceptionConfigForCurrentState();

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Status.StunEnded")));
}

void AEnemyBase::ApplyDamageInternal(float DamageAmount, bool bIsCoreHit)
{
	if (!HasAuthority() || DamageAmount <= 0.0f || CurrentHealth <= 0.0f)
	{
		return;
	}

	if (bIsCoreHit)
	{
		DamageAmount *= CoreCriticalMultiplier;
	}

	CurrentHealth = FMath::Max(CurrentHealth - DamageAmount, 0.0f);

	if (CurrentHealth <= 0.0f)
	{
		HandleDeath();
	}
}

void AEnemyBase::OnRep_RuntimeStat()
{
	ApplyMovementFromRuntimeStat();
}

UHackableComponent* AEnemyBase::GetHackableComponent() const
{
	return HackableComponent;
}

void AEnemyBase::HandleHackCompleted(const FHackResultContext& Context)
{
	if (HasAuthority()
		&& bPossessionInProgress
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

	if (IsEnemyPossessed() || !bPossessionInProgress)
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
		return;
	}

	APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(Context.InstigatorActor);
	if (!IsValid(PartnerCharacter))
	{
		return;
	}

	if (BeginPossessionProcess(PartnerCharacter))
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

void AEnemyBase::SetDefaultEnemyType(EEnemyType EnemyType)
{
	RuntimeStat.Type = EnemyType;
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
	//스턴 판정이 일단 두 개라서; 임시로 처리. 
	const FGameplayTag StunnedTag = OutlierGameplayTags::State::Stunned();
	return (HackableComponent && HackableComponent->HasHackTag(StunnedTag))
		|| (EmpableComponent && EmpableComponent->HasEMPTag(StunnedTag));
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
			bInCombat = true;
			RefreshPerceptionConfigForCurrentState();
		}
		return;
	}

	if (DetectedState == EEnemyCombatState::Alert && PreStunCombatState == EEnemyCombatState::NonCombat)
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
	if (!IsValid(TargetActor))
	{
		return false;
	}

	const bool bStarted = StartAttackLocation(TargetActor->GetActorLocation());
	return bStarted;
}

bool AEnemyBase::StartAttackLocation(const FVector& TargetLocation)
{
	const bool bHasWeapon = IsValid(CurrentWeapon);
	const bool bCanAttack = bHasWeapon && CurrentWeapon->CanAttack();
	// 쿨다운 중인 요청은 조준 방향을 바꾸지 않는다. 실제로 발사할 수 있을 때만
	// ControlRotation을 갱신해야 실패 재시도가 드론 Pitch를 흔들지 않는다.
	const bool bAimUpdated = bCanAttack && UpdateAttackLocation(TargetLocation);
	if (!bAimUpdated || !bHasWeapon || !bCanAttack)
	{
		return false;
	}

	CurrentWeapon->StartAttack();
	SetAttackPhase(EEnemyAttackPhase::Firing);
	return true;
}

bool AEnemyBase::StartPossessedAttackBurst()
{
	if (!HasAuthority()
		|| !bIsPossessed
		|| CombatState == EEnemyCombatState::Stun
		|| !IsValid(CurrentWeapon)
		|| !CurrentWeapon->HasFixedBurst()
		|| !CurrentWeapon->CanAttack())
	{
		return false;
	}

	CurrentWeapon->StartAttack();
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

void AEnemyBase::ReleaseSearchRingSlot()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->ReleaseSearchRingSlot(this);
	}
}

void AEnemyBase::HandleCurrentRoomTagChanged(
	FGameplayTag PreviousRoomTag,
	FGameplayTag NewRoomTag)
{
	(void)PreviousRoomTag;
	(void)NewRoomTag;
	RemoveRoomTargetObserver();
	ClearSharedTargetContact();
	ReleaseSearchRingSlot();
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
	ResetPossessedAttackInput();
	StopCurrentAttack();

	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->NotifyTargetActorRemoved(this);
	}

	RemoveRoomTargetObserver();
	ReleaseSearchRingSlot();

	if (bIsPossessed)
	{
		ClearPossessedPlayerState();
	}

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Died")));

	if (StateTreeComponent)
	{
		StateTreeComponent->StopLogic(TEXT("Enemy died"));
	}

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
	Destroy();
}

void AEnemyBase::HandleStartAttackInput()
{
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
		return;
	}

	if (!bIsPossessed)
	{
		ResetPossessedAttackInput();
		return;
	}

	if (bHeld
		&& (CombatState == EEnemyCombatState::Stun
			|| CurrentHealth <= 0.0f
			|| !IsValid(CurrentWeapon)))
	{
		return;
	}

	if (bPossessedAttackHeld == bHeld)
	{
		return;
	}

	bPossessedAttackHeld = bHeld;
	if (bHeld)
	{
		bPossessedAttackQueued = true;
	}

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			bHeld
			? TEXT("Enemy.Event.Attack.InputPressed")
			: TEXT("Enemy.Event.Attack.InputReleased")));
}

void AEnemyBase::ResetPossessedAttackInput()
{
	bPossessedAttackHeld = false;
	bPossessedAttackQueued = false;
}

void AEnemyBase::HandleReleasePossessionInput(const FInputActionValue& Value)
{
	(void)Value;

	APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(GetController());
	if (!PartnerController)
	{
		return;
	}

	PartnerController->ReleaseEnemyPossession();
}
