#include "Tutorial/Actors/Tuto_LaserControlSwitch.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "OutlierPlayerState.h"
#include "Environment/LaserKillFloor.h"
#include "Outlier.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_LaserControlSwitch::ATuto_LaserControlSwitch()
{
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

bool ATuto_LaserControlSwitch::Interact(AFirstPersonCharacter* Interactor)
{
	ActivatingPlayer = IsValid(Interactor) ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const bool bAccepted = Super::Interact(Interactor);
	ActivatingPlayer.Reset();
	return bAccepted;
}

void ATuto_LaserControlSwitch::BeginPlay()
{
	if (HasAuthority())
	{
		bValidTargets = !TargetLaserFloors.IsEmpty();
		for (ALaserKillFloor* Laser : TargetLaserFloors)
		{
			ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(Laser);
			if (!IsValid(Laser) || !Progress)
			{
				bValidTargets = false;
				UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s requires Tuto progress-source laser targets."), *GetName());
				continue;
			}
			if (RequiredLasers.Contains(FObjectKey(Laser))) continue;
			RequiredLasers.Add(FObjectKey(Laser));
			BoundLasers.Add(Laser);
			Progress->OnProgressCompleted.AddUObject(this, &ThisClass::HandleLaserCompleted);
			if (Progress->IsProgressCompleted()) CompletedLasers.Add(FObjectKey(Laser));
		}
	}
	// 부모의 체크포인트 복원도 대상의 실제 비활성화 경로를 사용한다.
	Super::BeginPlay();
	if (HasAuthority()) TryCompleteEffects(nullptr);
}

void ATuto_LaserControlSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const auto& Laser : BoundLasers)
		if (ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(Laser.Get()))
			Progress->OnProgressCompleted.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

bool ATuto_LaserControlSwitch::ActivateTarget()
{
	return bValidTargets && Super::ActivateTarget();
}

bool ATuto_LaserControlSwitch::DeactivateLaserFloor(ALaserKillFloor* LaserFloor)
{
	ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(LaserFloor);
	if (!Progress || !bValidTargets) return false;
	Progress->SetProgressInstigator(ActivatingPlayer.Get());
	const bool bWasEnabled = LaserFloor->IsHazardEnabled();
	LaserFloor->SetHazardEnabled(false);
	// 이미 꺼진 대상은 재처리하지 않되 복원 상태 집계에는 포함한다.
	if (Progress->IsProgressCompleted()) HandleLaserCompleted(LaserFloor, ActivatingPlayer.Get());
	return bWasEnabled && !LaserFloor->IsHazardEnabled();
}

void ATuto_LaserControlSwitch::HandleLaserCompleted(AActor* Source, AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || !Source || !RequiredLasers.Contains(FObjectKey(Source))) return;
	const ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(Source);
	if (!Progress || !Progress->IsProgressCompleted()) return;
	CompletedLasers.Add(FObjectKey(Source));
	TryCompleteEffects(SourcePlayerState);
}

void ATuto_LaserControlSwitch::TryCompleteEffects(AOutlierPlayerState* SourcePlayerState)
{
	if (!HasAuthority() || bEffectCompleted || !bValidTargets || RequiredLasers.IsEmpty()
		|| CompletedLasers.Num() != RequiredLasers.Num()) return;
	bEffectCompleted = true;
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	CompleteProgress(SourcePlayerState);
}
