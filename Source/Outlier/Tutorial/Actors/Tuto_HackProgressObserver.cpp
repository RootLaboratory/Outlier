#include "Tutorial/Actors/Tuto_HackProgressObserver.h"

#include "Components/BillboardComponent.h"
#include "Engine/World.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Tutorial/Trigger/Tuto_ProgressSourceInterface.h"

ATuto_HackProgressObserver::ATuto_HackProgressObserver()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
#if WITH_EDITORONLY_DATA
	// 배치용 마커일 뿐이며, 게임 중에는 아무것도 렌더링하지 않는다.
	if (UBillboardComponent* Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite")))
	{
		Sprite->SetupAttachment(GetRootComponent());
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
	// 관찰과 이벤트 확정은 서버 전용이다.
	bNetLoadOnClient = false;
}

void ATuto_HackProgressObserver::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() == NM_Client) return;
	if (AreAllSlotsFired())
	{
		UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s: every slot already fired in this game flow; destroyed on BeginPlay."), *GetName());
		Destroy();
		return;
	}
	for (AActor* Source : Sources)
	{
		// 같은 액터가 Sources에 중복 지정돼도 완료 수와 delegate는 한 번만 센다.
		if (!IsValid(Source) || ObservedSources.Contains(Source)) continue;
		ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(Source);
		if (!Progress)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s: source '%s' is not a Tuto progress source; ignored."),
				*GetName(), *GetNameSafe(Source));
			continue;
		}
		Progress->OnProgressCompleted.AddUObject(this, &ThisClass::HandleSourceCompleted);
		ObservedSources.Add(Source);
	}
	// 복원된 완료 Source는 여기서 세기만 한다. 수신 Pair를 정할 플레이어가 없으므로 다음 완료 때 도달한 slot이 함께 나간다.
	UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s observing %d sources (%d already completed)."),
		*GetName(), ObservedSources.Num(), GetCompletedSourceCount());
}

void ATuto_HackProgressObserver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopObserving();
	Super::EndPlay(EndPlayReason);
}

int32 ATuto_HackProgressObserver::GetCompletedSourceCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& Source : ObservedSources)
		if (const ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(Source.Get()))
			if (Progress->IsProgressCompleted()) ++Count;
	return Count;
}

int32 ATuto_HackProgressObserver::GetRequiredCount(int32 Slot) const
{
	const int32 Required = CountTriggers.IsValidIndex(Slot) ? CountTriggers[Slot].RequiredCount : 0;
	return Required > 0 ? Required : ObservedSources.Num();
}

bool ATuto_HackProgressObserver::IsSlotFired(int32 Slot) const
{
	const FGameplayTag Tag = GetEventTag(Slot);
	const UOutlierSaveSubSystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	return IsEventSubmitted(Slot) || (Tag.IsValid() && Saves && Saves->HasFiredTutorialEvent(Tag));
}

bool ATuto_HackProgressObserver::AreAllSlotsFired() const
{
	bool bAnyConfigured = false;
	for (int32 Slot = 0; Slot < CountTriggers.Num(); ++Slot)
	{
		if (!GetEventTag(Slot).IsValid()) continue;
		bAnyConfigured = true;
		if (!IsSlotFired(Slot)) return false;
	}
	return bAnyConfigured;
}

void ATuto_HackProgressObserver::HandleSourceCompleted(AActor* Source, AOutlierPlayerState* SourcePlayerState)
{
	const int32 Completed = GetCompletedSourceCount();
	UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s: '%s' completed (%d/%d)."),
		*GetName(), *GetNameSafe(Source), Completed, ObservedSources.Num());
	for (int32 Slot = 0; Slot < CountTriggers.Num(); ++Slot)
		if (GetEventTag(Slot).IsValid() && !IsSlotFired(Slot) && Completed >= GetRequiredCount(Slot))
			TriggerEvent(Slot, SourcePlayerState);
	if (AreAllSlotsFired())
	{
		StopObserving();
		Destroy();
	}
}

void ATuto_HackProgressObserver::StopObserving()
{
	for (const TWeakObjectPtr<AActor>& Source : ObservedSources)
		if (ITuto_ProgressSourceInterface* Progress = Cast<ITuto_ProgressSourceInterface>(Source.Get()))
			Progress->OnProgressCompleted.RemoveAll(this);
	ObservedSources.Reset();
}
