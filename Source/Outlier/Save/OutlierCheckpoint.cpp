// Fill out your copyright notice in the Description page of Project Settings.


#include "Save/OutlierCheckpoint.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

// Sets default values
AOutlierCheckpoint::AOutlierCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetBoxExtent(FVector(120.0f, 120.0f, 120.0f));
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	CheckpointMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CheckpointMesh"));
	CheckpointMesh->SetupAttachment(Trigger);
	CheckpointMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CheckpointMesh->SetGenerateOverlapEvents(false);

	SpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Trigger);
	bActivationConditionSatisfied = bInitiallyActive;
}

// Called when the game starts or when spawned
void AOutlierCheckpoint::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (!bActivationExplicitlySet)
		{
			bActivationConditionSatisfied = bInitiallyActive;
		}
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleTriggerBeginOverlap);
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			bCheckpointIdRegistered = SaveSubsystem->RegisterCheckpointId(CheckpointId, this);
			bCheckpointCommitted = SaveSubsystem->HasCommittedCheckpoint(CheckpointId);
		}
		UE_LOG(LogTemp, Display,
			TEXT("[Checkpoint] Ready Id=%s Active=%d Registered=%d Committed=%d Actor=%s"),
			*CheckpointId.ToString(), bActivationConditionSatisfied ? 1 : 0,
			bCheckpointIdRegistered ? 1 : 0, bCheckpointCommitted ? 1 : 0,
			*GetNameSafe(this));
	}
}

void AOutlierCheckpoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bCheckpointIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterCheckpointId(CheckpointId, this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

FTransform AOutlierCheckpoint::GetSpawnTransform() const
{
	return SpawnPoint ? SpawnPoint->GetComponentTransform() : GetActorTransform();
}

FTransform AOutlierCheckpoint::GetPartnerSpawnTransform() const
{
	FTransform PartnerTransform = GetSpawnTransform();
	PartnerTransform.AddToTranslation(
		PartnerTransform.GetRotation().RotateVector(PartnerSpawnOffset));
	return PartnerTransform;
}

bool AOutlierCheckpoint::SetActivationConditionSatisfied(
	AController* ActivatingController,
	bool bSatisfied)
{
	(void)ActivatingController;
	if (!HasAuthority() || bCheckpointCommitted)
	{
		return false;
	}
	bActivationExplicitlySet = true;

	if (bActivationConditionSatisfied == bSatisfied)
	{
		return true;
	}
	// 이전 접촉 기록은 버린다. 활성화 순간에도 실제로 겹쳐 있는 플레이어만 새 통과로 반영한다.
	bActivationConditionSatisfied = bSatisfied;
	OverlappingPairId = INDEX_NONE;
	bShooterPassed = false;
	bPartnerPassed = false;
	CommitController.Reset();
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[Checkpoint] Activation Id=%s Active=%d Actor=%s"),
		*CheckpointId.ToString(), bActivationConditionSatisfied ? 1 : 0, *GetNameSafe(this));
	if (bActivationConditionSatisfied)
	{
		// 박스 안에 머물러 있으면 BeginOverlap이 다시 오지 않으므로 활성화 시 한 번만 보정한다.
		Trigger->UpdateOverlaps();
		TArray<UPrimitiveComponent*> OverlappingComponents;
		Trigger->GetOverlappingComponents(OverlappingComponents);
		for (UPrimitiveComponent* Component : OverlappingComponents)
		{
			if (IsValid(Component))
			{
				ProcessTriggerOverlap(Component->GetOwner(), Component);
			}
		}
	}
	return true;
}

void AOutlierCheckpoint::SetCombatRoomTag(FGameplayTag InRoomTag)
{
	if (HasAuthority() && !bCheckpointCommitted)
	{
		CombatRoomTag = InRoomTag;
	}
}

AOutlierPlayerState* AOutlierCheckpoint::ResolvePairPlayerState(AActor* Actor) const
{
	const APawn* Pawn = Cast<APawn>(Actor);
	AOutlierPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (!PlayerState || PlayerState->GetPairId() == INDEX_NONE)
	{
		return nullptr;
	}
	if (PlayerState->IsShooterPlayer() && PlayerState->GetShooterCharacter() == Actor)
	{
		return PlayerState;
	}
	return PlayerState->IsPartnerPlayer() && PlayerState->GetPartnerCharacter() == Actor
		? PlayerState : nullptr;
}

void AOutlierCheckpoint::HandleTriggerBeginOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;
	ProcessTriggerOverlap(OtherActor, OtherComp);
}

void AOutlierCheckpoint::ProcessTriggerOverlap(AActor* OtherActor, UPrimitiveComponent* OtherComp)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!HasAuthority() || !Pawn || OtherComp != Pawn->GetRootComponent()
		|| (!Pawn->IsA<AShooterCharacter>() && !Pawn->IsA<APartnerCharacter>()))
	{
		return;
	}
	AOutlierPlayerState* PlayerState = ResolvePairPlayerState(OtherActor);
	UE_LOG(LogTemp, Display,
		TEXT("[Checkpoint] Enter Id=%s Actor=%s Pawn=%s Pair=%d Active=%d Registered=%d Committed=%d ShooterPassed=%d PartnerPassed=%d"),
		*CheckpointId.ToString(), *GetNameSafe(this), *GetNameSafe(OtherActor),
		PlayerState ? PlayerState->GetPairId() : INDEX_NONE,
		bActivationConditionSatisfied ? 1 : 0, bCheckpointIdRegistered ? 1 : 0,
		bCheckpointCommitted ? 1 : 0, bShooterPassed ? 1 : 0, bPartnerPassed ? 1 : 0);
	if (!bActivationConditionSatisfied || bCheckpointCommitted)
	{
		UE_LOG(LogTemp, Display, TEXT("[Checkpoint] Entry ignored Id=%s Reason=%s"),
			*CheckpointId.ToString(), bCheckpointCommitted ? TEXT("AlreadyCommitted") : TEXT("Inactive"));
		return;
	}
	if (!PlayerState || (OverlappingPairId != INDEX_NONE
		&& OverlappingPairId != PlayerState->GetPairId()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Checkpoint] Entry ignored Id=%s Reason=%s Pawn=%s"),
			*CheckpointId.ToString(), PlayerState ? TEXT("DifferentPair") : TEXT("UnlinkedPlayer"),
			*GetNameSafe(OtherActor));
		return;
	}
	OverlappingPairId = PlayerState->GetPairId();
	if (PlayerState->IsShooterPlayer())
	{
		bShooterPassed = true;
		CommitController = Cast<AController>(PlayerState->GetOwner());
	}
	else
	{
		bPartnerPassed = true;
		if (!CommitController.IsValid())
		{
			CommitController = Cast<AController>(PlayerState->GetOwner());
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[Checkpoint] Pass Id=%s Pair=%d ShooterPassed=%d PartnerPassed=%d"),
		*CheckpointId.ToString(), OverlappingPairId,
		bShooterPassed ? 1 : 0, bPartnerPassed ? 1 : 0);
	TryCommit();
}

bool AOutlierCheckpoint::RetryCommit()
{
	return TryCommit();
}

bool AOutlierCheckpoint::TryCommit()
{
	if (!HasAuthority() || !bActivationConditionSatisfied || bCheckpointCommitted
		|| !bShooterPassed || !bPartnerPassed)
	{
		return false;
	}
	if (!bCheckpointIdRegistered)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Checkpoint] Commit blocked Id=%s Reason=UnregisteredId"),
			*CheckpointId.ToString());
		return false;
	}
	AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr;
	if (!GameMode || !CommitController.IsValid()
		|| !GameMode->RegisterCheckpoint(CommitController.Get(), this))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Checkpoint] Commit failed Id=%s Pair=%d GameMode=%d Controller=%d"),
			*CheckpointId.ToString(), OverlappingPairId,
			GameMode ? 1 : 0, CommitController.IsValid() ? 1 : 0);
		return false;
	}

	// GameMode가 디스크 기록을 마친 뒤에만 확정하고, 문 같은 관찰자에게 성공을 통지한다.
	bCheckpointCommitted = true;
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[Checkpoint] Committed Id=%s Pair=%d"),
		*CheckpointId.ToString(), OverlappingPairId);
	OnCheckpointCommitted.Broadcast(this);
	return true;
}

void AOutlierCheckpoint::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOutlierCheckpoint, bActivationConditionSatisfied);
	DOREPLIFETIME(AOutlierCheckpoint, bCheckpointCommitted);
}
