#include "UI/HackingMiniGameBase.h"

void UHackingMiniGameBase::InitializeMiniGame(AActor* InTargetActor, UHackableComponent* InHackableComponent)
{
	TargetActor = InTargetActor;
	HackableComponent = InHackableComponent;
}

void UHackingMiniGameBase::StartMiniGame()
{
	if (bMiniGameActive)
	{
		return;
	}

	ElapsedTime = 0.0f;
	bMiniGameActive = true;
	OnMiniGameStarted();
}

void UHackingMiniGameBase::FinishMiniGame(EHackResult Result)
{
	if (!bMiniGameActive)
	{
		return;
	}

	bMiniGameActive = false;
	OnMiniGameFinishedEvent(Result);
	OnMiniGameFinished.Broadcast(Result);//Partner->결과 알리기.
}

void UHackingMiniGameBase::OnMiniGameStarted()
{
}

void UHackingMiniGameBase::OnMiniGameUpdated(float DeltaTime)
{
}

void UHackingMiniGameBase::OnMiniGameFinishedEvent(EHackResult Result)
{
}

void UHackingMiniGameBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bMiniGameActive)
	{
		return;
	}

	ElapsedTime += InDeltaTime;
	OnMiniGameUpdated(InDeltaTime);
}

