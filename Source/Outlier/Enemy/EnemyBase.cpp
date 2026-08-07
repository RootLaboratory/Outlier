#include "Enemy/EnemyBase.h"
#include "Camera/CameraComponent.h"
#include "Enemy/EnemyStateTreeComponent.h"
#include "Damage/OutlierTaggedDamageEvent.h"
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
#include "Enemy/EnemyRoomSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "InputActionValue.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AIPerceptionSystem.h"
#include "StateTree.h"
#include "Components/StateTreeComponentSchema.h"
#include "Team/OutlierTeamIds.h"
#include "TimerManager.h"
#include "Room/RoomTagComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Outlier.h"

namespace
{
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
}

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

	StateTreeComponent = CreateDefaultSubobject<UEnemyStateTreeComponent>(TEXT("StateTreeComponent"));
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
	DOREPLIFETIME(AEnemyBase, CurrentHealth);
	DOREPLIFETIME(AEnemyBase, bInCombat);
	DOREPLIFETIME(AEnemyBase, LastKnownPlayerLocation);
	DOREPLIFETIME(AEnemyBase, PatternStartPlayerLocation);
	DOREPLIFETIME(AEnemyBase, CombatState);
	DOREPLIFETIME(AEnemyBase, bIsPossessed);
	DOREPLIFETIME(AEnemyBase, bPossessionInProgress);
	DOREPLIFETIME(AEnemyBase, bPossessedImpactInputLocked);
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
	EndPossessedImpactInputLock();
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
	const bool bPossessionDiagnosticEvent = Tag.ToString().StartsWith(
		TEXT("Enemy.Event.Possession"));
	const bool bAttackDiagnosticEvent = Tag.ToString().StartsWith(
		TEXT("Enemy.Event.Attack"));
	if (!HasAuthority() || !Tag.IsValid() || !StateTreeComponent)
	{
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

	bPossessedAttackHeld = false;
	EndPossessedImpactInputLock();
	bIsPossessed = bNewIsPossessed;
	if (bIsPossessed)
	{
		CancelCommittedAction();
		EndImpactReaction();
	}
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
	CancelCommittedAction();
	EndImpactReaction();
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
		EnemyAIController->StopMovement();
		EnemyAIController->SetEnemyPerceptionEnabled(false);
	}

	PartnerCharacter->SetEnemyPossessionProtection(true);
	ForceNetUpdate();

	// Global Sync가 bPossessionInProgress=true를 먼저 읽은 뒤 Pending 상태를 선택하게 한다.
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
	if (!HasAuthority() || !bPossessionInProgress || !HackableComponent)
	{
		return;
	}

	HackableComponent->HackTags.RemoveTag(
		OutlierGameplayTags::State::PossessPending());

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
		// Global Sync가 bPossessionInProgress=false를 반영한 뒤 Pending 상태를 빠져나가게 한다.
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
	CancelCommittedAction();
	EndImpactReaction();
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

void AEnemyBase::ApplyDamageInternal(float DamageAmount)
{
	if (!HasAuthority() || DamageAmount <= 0.0f || CurrentHealth <= 0.0f)
	{
		return;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Max(CurrentHealth - DamageAmount, 0.0f);
	HandleCurrentHealthChanged(PreviousHealth);

	if (CurrentHealth <= 0.0f)
	{
		HandleDeath();
	}
}

float AEnemyBase::TakeDamage(
	float DamageAmount,
	FDamageEvent const& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	if (!HasAuthority() || DamageAmount <= 0.0f || CurrentHealth <= 0.0f)
	{
		return 0.0f;
	}

	float DamageMultiplier = 1.0f;
	bool bCoreWeakPointHit = false;
	if (DamageEvent.IsOfType(FOutlierTaggedDamageEvent::ClassID))
	{
		const FOutlierTaggedDamageEvent& TaggedEvent = static_cast<const FOutlierTaggedDamageEvent&>(DamageEvent);
		if (TaggedEvent.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon()))
		{
			const UPrimitiveComponent* HitComponent = TaggedEvent.HitResult.GetComponent();
			bCoreWeakPointHit = HitComponent == CoreHitboxComponent;
			DamageMultiplier = GetWeakPointDamageMultiplier(HitComponent);
		}
	}

	const float PreviousHealth = CurrentHealth;
	const float FinalDamage = DamageAmount * FMath::Max(DamageMultiplier, 0.0f);
	const float AppliedDamage = Super::TakeDamage(FinalDamage, DamageEvent, EventInstigator, DamageCauser);
	// 공통 TakeDamage 진입점을 기존 Enemy HP 및 사망 처리로 연결한다.
	ApplyDamageInternal(AppliedDamage);
	if (bCoreWeakPointHit)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyWeakPoint] Type=Core Actor=%s Component=%s RawDamage=%.2f Multiplier=%.2f AppliedDamage=%.2f HP=%.2f->%.2f"),
			*GetNameSafe(this),
			*GetNameSafe(CoreHitboxComponent),
			DamageAmount,
			DamageMultiplier,
			AppliedDamage,
			PreviousHealth,
			CurrentHealth);
	}
	return AppliedDamage;
}

float AEnemyBase::GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const
{
	return HitComponent == CoreHitboxComponent ? CoreCriticalMultiplier : 1.0f;
}

void AEnemyBase::ApplyExplosionReaction(
	const FVector& ExplosionOrigin,
	float EnemyImpulseScale,
	float TurretReactionScale,
	float EffectRatio)
{
	if (!HasAuthority() || EffectRatio <= 0.0f || CurrentHealth <= 0.0f)
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

bool AEnemyBase::BeginImpactReaction()
{
	if (!HasAuthority()
		|| CurrentHealth <= 0.0f
		|| bIsPossessed
		|| bPossessionInProgress
		|| CurrentImpactStrength < RuntimeImpactReactionProfile.MinReactionStrength)
	{
		if (IsEnemyImpactReactionDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[EnemyImpactDiag] RecoveryBeginRejected Enemy=%s Authority=%s Health=%.2f Possessed=%s PossessionPending=%s Strength=%.2f MinStrength=%.2f"),
				*GetNameSafe(this),
				HasAuthority() ? TEXT("true") : TEXT("false"),
				CurrentHealth,
				bIsPossessed ? TEXT("true") : TEXT("false"),
				bPossessionInProgress ? TEXT("true") : TEXT("false"),
				CurrentImpactStrength,
				RuntimeImpactReactionProfile.MinReactionStrength);
		}
		return false;
	}

	bImpactReactionActive = true;
	StopCurrentAttack();
	ReleaseSearchRingSlot();

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
	if (!HasAuthority() || !bImpactReactionActive || CurrentHealth <= 0.0f)
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
	if (!HasAuthority() || !bImpactReactionActive)
	{
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
		&& CurrentHealth > 0.0f
		&& CombatState != EEnemyCombatState::Stun
		&& !IsAIControlSuppressed())
	{
		EnemyAIController->SetEnemyPerceptionEnabled(true);
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
		&& CurrentHealth > 0.0f;
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

void AEnemyBase::OnRep_CurrentHealth(float PreviousHealth)
{
	HandleCurrentHealthChanged(PreviousHealth);
}

void AEnemyBase::HandleCurrentHealthChanged(float PreviousHealth)
{
	if (!bIsPossessed || !IsLocallyControlled() || CurrentHealth >= PreviousHealth)
	{
		return;
	}

	UWorld* World = GetWorld();
	const float MaxHealth = RuntimeStat.Health;
	if (!World || MaxHealth <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PPS = World->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		const float HealthRatio = FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f);
		PPS->UpdateDamagedPostProcess(HealthRatio, FVector4(0.0f, 0.0f, 1.0f, 0.0f));
		PPS->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, true);
	}
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
				bPossessionInProgress ? TEXT("true") : TEXT("false"),
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
		|| CurrentHealth <= 0.0f
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
				CurrentHealth,
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
