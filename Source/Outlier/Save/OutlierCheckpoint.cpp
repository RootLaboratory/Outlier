// Fill out your copyright notice in the Description page of Project Settings.


#include "Save/OutlierCheckpoint.h"
#include "OutlierGameMode.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

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
}

// Called when the game starts or when spawned
void AOutlierCheckpoint::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
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
	if (!HasAuthority() || !bCheckpointIdRegistered || bCheckpointCommitted
		|| !bSatisfied || !ActivatingController)
	{
		return false;
	}

	// 외부 조건 충족은 저장 요청의 입구다. 실제 저장 가능 여부와 페어 스냅샷 확정은 GameMode가 판단한다.
	bActivationConditionSatisfied = true;
	ForceNetUpdate();
	AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr;
	if (!GameMode || !GameMode->RegisterCheckpoint(ActivatingController, this))
	{
		return false;
	}

	// 저장 성공 뒤에만 재활성화를 막는다. 실패한 요청을 이미 저장된 체크포인트로 표시하지 않는다.
	bCheckpointCommitted = true;
	ForceNetUpdate();
	return true;
}

void AOutlierCheckpoint::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOutlierCheckpoint, bActivationConditionSatisfied);
	DOREPLIFETIME(AOutlierCheckpoint, bCheckpointCommitted);
}
