#include "Interaction/PassageHatch.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Save/OutlierSaveSubSystem.h"

APassageHatch::APassageHatch()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("HatchRoot")));
	LeftHatchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftHatchMesh"));
	RightHatchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightHatchMesh"));
	LeftHatchMesh->SetupAttachment(GetRootComponent());
	RightHatchMesh->SetupAttachment(GetRootComponent());
	LeftHatchMesh->SetMobility(EComponentMobility::Movable);
	RightHatchMesh->SetMobility(EComponentMobility::Movable);
}

void APassageHatch::BeginPlay()
{
	Super::BeginPlay();
	OpenLeft = LeftHatchMesh->GetRelativeTransform();
	OpenRight = RightHatchMesh->GetRelativeTransform();
	bInitialized = true;
	if (HasAuthority())
	{
		if (UOutlierSaveSubSystem* Save = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr)
		{
			bProgressIdRegistered = Save->RegisterWorldProgressId(EOutlierWorldProgressType::ClosedPassageHatch, HatchId, this);
			if (bProgressIdRegistered && Save->HasWorldProgress(EOutlierWorldProgressType::ClosedPassageHatch, HatchId))
			{
				Motion.bClosed = true;
				Motion.Duration = 0.0f; // Restore immediately, without replaying the closing animation.
				ForceNetUpdate();
			}
		}
	}
	ApplyMotion();
}

void APassageHatch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* Save = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr)
			Save->UnregisterWorldProgressId(EOutlierWorldProgressType::ClosedPassageHatch, HatchId, this);
	}
	Super::EndPlay(EndPlayReason);
}

void APassageHatch::CloseHatch()
{
	if (!HasAuthority() || !bInitialized || Motion.bClosed) return;
	Motion.bClosed = true;
	Motion.StartServerTime = GetMotionTime();
	Motion.Duration = FMath::IsFinite(CloseDuration) ? FMath::Max(0.0f, CloseDuration) : 0.0f;
	// Record the accepted close request, just as doors record their requested open state.
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* Save = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr)
			Save->SetWorldProgressState(EOutlierWorldProgressType::ClosedPassageHatch, HatchId, true);
	}
	ApplyMotion();
	ForceNetUpdate();
}

double APassageHatch::GetMotionTime() const
{
	const AGameStateBase* Clock = GetWorld()->GetGameState();
	return Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void APassageHatch::ApplyMotion()
{
	if (!bInitialized) return;
	const float Alpha = !Motion.bClosed ? 0.0f : Motion.Duration <= 0.0f ? 1.0f
		: FMath::Clamp(static_cast<float>((GetMotionTime() - Motion.StartServerTime) / Motion.Duration), 0.0f, 1.0f);
	FTransform Left, Right;
	Left.Blend(OpenLeft, LeftCloseOffset * OpenLeft, Alpha);
	Right.Blend(OpenRight, RightCloseOffset * OpenRight, Alpha);
	LeftHatchMesh->SetRelativeTransform(Left);
	RightHatchMesh->SetRelativeTransform(Right);
	SetActorTickEnabled(Motion.bClosed && Alpha < 1.0f);
}

void APassageHatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyMotion();
}

void APassageHatch::OnRep_Motion() { ApplyMotion(); }

void APassageHatch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APassageHatch, Motion);
}
