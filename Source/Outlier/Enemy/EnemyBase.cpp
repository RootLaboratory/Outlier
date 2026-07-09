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
#include "GameplayTags/OutlierGameplayTags.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("StateTreeComponent"));
	EnemyCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("EnemyCameraComponent"));
	EnemyCameraComponent->SetupAttachment(GetRootComponent());

	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::Possessable());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Possess());
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
	DOREPLIFETIME(AEnemyBase, RoomTag);
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
	// APartnerCharacter::UnPossessed()와 동일한 이유 — UnPossessed() 시 InputComponent가
	// 컨트롤러 입력 스택에 남아있으면 재빙의 전까지 입력이 중복 발동할 수 있음
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}

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

void AEnemyBase::SetEnemyPossessed(bool bNewIsPossessed)
{
	if (!HasAuthority())
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
	if (!HasAuthority())
	{
		return;
	}

	bPlayerCurrentlyVisible = bNewVisible;
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

	bInCombat = true;
	CombatState = EEnemyCombatState::Combat;
	UpdateLastKnownPlayerLocation(PlayerLocation);

	const int32 PropagationArenaId = ArenaId != INDEX_NONE ? ArenaId : LastKnownArenaId;
	if (bPropagateToRoom && PropagationArenaId != INDEX_NONE && RoomTag.IsValid())
	{
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->NotifyRoomCombat(PropagationArenaId, RoomTag, PlayerLocation, this);
		}
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

	if (CombatState == EEnemyCombatState::Combat)
	{
		UpdateLastKnownPlayerLocation(PlayerLocation);
		return;
	}

	CombatState = EEnemyCombatState::Alert;
	UpdateLastKnownPlayerLocation(PlayerLocation);
}

void AEnemyBase::EnterStun()
{
	if (!HasAuthority())
	{
		return;
	}

	if (CombatState != EEnemyCombatState::Stun)
	{
		PreStunCombatState = CombatState;
	}

	CombatState = EEnemyCombatState::Stun;
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
}

void AEnemyBase::ApplyDamageInternal(float DamageAmount)
{
	if (!HasAuthority() || DamageAmount <= 0.0f || CurrentHealth <= 0.0f)
	{
		return;
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
		MovementComponent->MaxWalkSpeed = FMath::Max(RuntimeStat.MoveSpeed, 0.0f);
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
		bInCombat = true;
		return;
	}

	if (DetectedState == EEnemyCombatState::Alert && PreStunCombatState == EEnemyCombatState::NonCombat)
	{
		PreStunCombatState = EEnemyCombatState::Alert;
	}
}

void AEnemyBase::HandleDeath()
{
	if (bIsPossessed)
	{
		ClearPossessedPlayerState();
	}

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
