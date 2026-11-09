#include "Interaction/PanelMover.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Save/OutlierSaveSubSystem.h"

APanelMover::APanelMover()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("PanelRoot"));

	Supporter = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Supporter"));
	Supporter->SetupAttachment(RootComponent);

	PanelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PanelMesh"));
	PanelMesh->SetupAttachment(Supporter);
}

void APanelMover::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::MovedPanel,
			PanelId,
			this);
		if (bProgressIdRegistered
			&& SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::MovedPanel, PanelId))
		{
			// 복원은 새 이동이 아니므로 보간 없이 도착 위치에 둔다.
			SnapToTarget();
		}
	}
}

void APanelMover::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetMovingAudio(false);

	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::MovedPanel,
				PanelId,
				this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool APanelMover::MoveToTarget()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (bIsMoved)
	{
		return false;
	}

	if (!TargetLocationActor)
	{
		return false;
	}

	bIsMoved = true;
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::MovedPanel,
				PanelId,
				true);
		}
	}

	Multicast_StartMove();
	ForceNetUpdate();
	return true;
}

void APanelMover::Multicast_StartMove_Implementation()
{
	StartMove();
}

void APanelMover::OnRep_IsMoved()
{
	if (bIsMoved)
	{
		StartMove();
	}
}

void APanelMover::StartMove()
{
	// 같은 클라이언트에 Multicast와 RepNotify가 둘 다 와도 한 번만 시작한다.
	if (bMoveStarted || !TargetLocationActor)
	{
		return;
	}

	bMoveStarted = true;
	// 기준점을 BeginPlay가 아니라 시작 시점에 읽어, RepNotify가 BeginPlay보다 먼저 와도 비지 않게 한다.
	StartLocation = GetActorLocation();
	TargetLocation = TargetLocationActor->GetActorLocation();
	MoveElapsed = 0.0f;
	if (MoveDuration <= KINDA_SMALL_NUMBER)
	{
		FinishMove();
		return;
	}
	ApplyMovingMaterials();
	SetMovingAudio(true);
	SetActorTickEnabled(true);
}

void APanelMover::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	MoveElapsed += DeltaTime;
	const float TimeRatio = FMath::Min(MoveElapsed / MoveDuration, 1.0f);
	if (TimeRatio >= 1.0f)
	{
		FinishMove();
		return;
	}

	const FVector DesiredLocation = FMath::Lerp(StartLocation, TargetLocation, EvaluateProgress(TimeRatio));
	SetActorLocation(DesiredLocation);
}

void APanelMover::FinishMove()
{
	// 커브 끝값과 상관없이 마지막은 도착 위치로 맞춘다.
	SetActorLocation(TargetLocation);
	SetActorTickEnabled(false);
	SetMovingAudio(false);
	RestoreOriginalMaterials();
}

void APanelMover::ApplyMovingMaterials()
{
	CachedMaterials.Reset();

	auto Apply = [this](UStaticMeshComponent* Mesh, const FPanelMoverMovingMaterial& Setting)
	{
		if (!Mesh || !Setting.MovingMaterial
			|| Setting.MaterialIndex < 0
			|| Setting.MaterialIndex >= Mesh->GetNumMaterials())
		{
			return;
		}

		FPanelMoverCachedMaterial& Cached = CachedMaterials.AddDefaulted_GetRef();
		Cached.Mesh = Mesh;
		Cached.MaterialIndex = Setting.MaterialIndex;
		Cached.OriginalMaterial = Mesh->GetMaterial(Setting.MaterialIndex);
		Mesh->SetMaterial(Setting.MaterialIndex, Setting.MovingMaterial);
	};

	Apply(Supporter, SupporterMovingMaterial);
	Apply(PanelMesh, LiftPlatformMovingMaterial);
}

void APanelMover::RestoreOriginalMaterials()
{
	// 체크포인트 스냅처럼 이동을 거치지 않은 도착은 캐시가 비어 있어 아무것도 바꾸지 않는다.
	for (const FPanelMoverCachedMaterial& Cached : CachedMaterials)
	{
		if (UStaticMeshComponent* Mesh = Cached.Mesh.Get())
		{
			Mesh->SetMaterial(Cached.MaterialIndex, Cached.OriginalMaterial.Get());
		}
	}
	CachedMaterials.Reset();
}

void APanelMover::SetMovingAudio(bool bEnabled)
{
	if (bMovingAudioActive == bEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	UOutlierAudioSubsystem* Audio = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (!Audio)
	{
		return;
	}

	const FGameplayTag TypeTag = MovingAudioTypeTag.IsValid()
		? MovingAudioTypeTag
		: FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Environment"));
	const FGameplayTag ContextTag = MovingAudioContextTag.IsValid()
		? MovingAudioContextTag
		: FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Ambience.Panel.Moving"));

	bMovingAudioActive = bEnabled;
	Audio->SetLocalEmitterLoop(this, TypeTag, ContextTag, bEnabled);
}

void APanelMover::SnapToTarget()
{
	if (!TargetLocationActor)
	{
		return;
	}

	bIsMoved = true;
	bMoveStarted = true;
	TargetLocation = TargetLocationActor->GetActorLocation();
	FinishMove();
	ForceNetUpdate();
}

float APanelMover::EvaluateProgress(float TimeRatio) const
{
	if (!MoveCurve)
	{
		return TimeRatio;
	}

	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	MoveCurve->GetTimeRange(MinTime, MaxTime);
	if (MaxTime <= MinTime)
	{
		return TimeRatio;
	}
	return MoveCurve->GetFloatValue(FMath::Lerp(MinTime, MaxTime, TimeRatio));
}

void APanelMover::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APanelMover, bIsMoved);
}
