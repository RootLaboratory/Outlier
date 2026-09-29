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
	// 활성화 이전의 접촉은 통과로 세지 않는다. 다음 BeginOverlap부터 새 페어를 누적한다.
	bActivationConditionSatisfied = bSatisfied;
	OverlappingPairId = INDEX_NONE;
	bShooterPassed = false;
	bPartnerPassed = false;
	CommitController.Reset();
	ForceNetUpdate();
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
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!HasAuthority() || !bActivationConditionSatisfied || bCheckpointCommitted
		|| !Pawn || OtherComp != Pawn->GetRootComponent())
	{
		return;
	}
	AOutlierPlayerState* PlayerState = ResolvePairPlayerState(OtherActor);
	if (!PlayerState || (OverlappingPairId != INDEX_NONE
		&& OverlappingPairId != PlayerState->GetPairId()))
	{
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
	TryCommit();
}

bool AOutlierCheckpoint::RetryCommit()
{
	return TryCommit();
}

bool AOutlierCheckpoint::TryCommit()
{
	if (!HasAuthority() || !bCheckpointIdRegistered || !bActivationConditionSatisfied
		|| bCheckpointCommitted || !bShooterPassed || !bPartnerPassed)
	{
		return false;
	}
	AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr;
	if (!GameMode || !CommitController.IsValid()
		|| !GameMode->RegisterCheckpoint(CommitController.Get(), this))
	{
		return false;
	}

	// GameMode가 디스크 기록을 마친 뒤에만 확정하고, 문 같은 관찰자에게 성공을 통지한다.
	bCheckpointCommitted = true;
	ForceNetUpdate();
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
