#include "Enemy/EnemyBase.h"
#include "Camera/CameraComponent.h"
#include "Components/StateTreeComponent.h"
#include "Drone/Partner/HackableComponent.h"
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
#include "Room/RoomTagComponent.h"

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
	EnemyCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("EnemyCameraComponent"));
	EnemyCameraComponent->SetupAttachment(GetRootComponent());

	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::Possessable());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Possess());

	RoomTagComponent = CreateDefaultSubobject<URoomTagComponent>(TEXT("RoomTagComponent"));

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
	DOREPLIFETIME(AEnemyBase, bPlayerCurrentlyVisible);
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	InitializeFromEnemyStatRow();
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

	bIsPossessed = bNewIsPossessed;

	if (!bIsPossessed)
	{
		ClearPossessedPlayerState();
	}

	if (HackableComponent)
	{
		if (bIsPossessed)
		{
			HackableComponent->HackTags.RemoveTag(HackGameplayTags::Target::Possessable());
			HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
			HackableComponent->HackTags.AddTag(OutlierGameplayTags::State::HackedOnce());
		}
		else
		{
			HackableComponent->HackTags.RemoveTag(HackGameplayTags::Target::NonPossessable());
			HackableComponent->HackTags.RemoveTag(OutlierGameplayTags::State::HackedOnce());
			HackableComponent->HackTags.AddTag(HackGameplayTags::Target::Possessable());
		}
	}

	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetCachedAIController()))
	{
		EnemyAIController->SetEnemyPerceptionEnabled(!bIsPossessed);
	}

	RefreshPerceptionTeamRegistration();

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			bIsPossessed
			? TEXT("Enemy.Event.Possession.Started")
			: TEXT("Enemy.Event.Possession.Ended")));
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

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			bNewVisible
			? TEXT("Enemy.Event.Perception.TargetAcquired")
			: TEXT("Enemy.Event.Perception.TargetLost")
		)
	);
}

void AEnemyBase::EnterCombat(const FVector& PlayerLocation)
{
	EnterCombatInArena(PlayerLocation, INDEX_NONE, true);
}

void AEnemyBase::EnterCombatInArena(const FVector& PlayerLocation, int32 ArenaId, bool bPropagateToRoom)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ArenaId != INDEX_NONE)
	{
		LastKnownArenaId = ArenaId;
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
		SendEnemyStateTreeEvent(
			FGameplayTag::RequestGameplayTag(
				TEXT("Enemy.Event.Combat.Entered")));
	}
}

void AEnemyBase::EnterAlert(const FVector& PlayerLocation)
{
	EnterAlertInArena(PlayerLocation, INDEX_NONE);
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

void AEnemyBase::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	if (!HasAuthority() || Context.Result != EHackResult::Success)
	{
		return;
	}

	if (EffectTag != HackGameplayTags::Effect::Possess())
	{
		return;
	}

	if (IsEnemyPossessed())
	{
		return;
	}

	APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(Context.InstigatorActor);
	if (!PartnerCharacter)
	{
		return;
	}

	APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(PartnerCharacter->GetController());
	if (!PartnerController)
	{
		return;
	}

	PartnerController->CachePartnerCharacterForEnemyPossession(PartnerCharacter);
	PartnerCharacter->SetInvincibleForEnemyPossession(true);
	PartnerController->Possess(this);
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

void AEnemyBase::HandleDeath()
{
	if (bIsPossessed)
	{
		ClearPossessedPlayerState();
	}

	SendEnemyStateTreeEvent(
		FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Died")));

	Destroy();
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
