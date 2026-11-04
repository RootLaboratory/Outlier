#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/UILayerTypes.h"
#include "OutlierCinematicSubsystem.generated.h"

class ALevelSequenceActor;
class AFirstPersonPlayerController;
class ULevelSequencePlayer;

namespace OutlierCinematicTags
{
	OUTLIER_API FGameplayTag ArenaIntro();
	OUTLIER_API FGameplayTag Title();
}

enum class EOutlierCinematicRegistration : uint8
{
	Bound,
	Unregistered,
	Invalid
};

UENUM(BlueprintType)
enum class EOutlierCinematicState : uint8
{
	Idle,
	Preparing,
	Prepared,
	Playing,
	Restoring,
	Ready,
	Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOutlierCinematicFinished, FGameplayTag, CinematicTag);

/** 로컬 재생과 연출을 담당한다. 서버 권한은 GameMode, RPC는 PlayerController에서 관리한다. */
UCLASS()
class OUTLIER_API UOutlierCinematicSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void PrepareInitialIntro(AFirstPersonPlayerController* Controller, const FGuid& IntroId);
	void StartCinematic(const FGuid& IntroId);
	bool HandleCompleteCinematicKey(const FKey& Key);
	void CompleteCinematic(const FGuid& IntroId);
	void ReleaseCinematic(const FGuid& IntroId);
	void ClearLevelCinematics();
	/** 프런트엔드의 로컬 전용 반복 배경이다. 인트로와 동일한 레지스트리 검증을 사용한다. */
	bool PlayTitleCinematic();

	EOutlierCinematicRegistration BindLevelCinematics(FGameplayTag Tag);
	bool ValidateCinematic(FGameplayTag Tag) const;

	UPROPERTY(BlueprintAssignable, Category = "Cinematic")
	FOutlierCinematicFinished OnCinematicFinished;

	UFUNCTION(BlueprintPure, Category = "Cinematic")
	EOutlierCinematicState GetState() const { return State; }

private:
	bool TickCinematic(float DeltaTime);
	bool IsPlayerReady() const;
	bool BeginCinematicInput();
	bool IsCinematicInputReady() const;
	void FailCinematic(const TCHAR* Reason);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	UFUNCTION()
	void HandleSequenceFinished();
	TMap<FGameplayTag, TWeakObjectPtr<ALevelSequenceActor>> Cinematics;
	TWeakObjectPtr<UWorld> BoundWorld;
	TWeakObjectPtr<AFirstPersonPlayerController> OwningController;
	TWeakObjectPtr<ULevelSequencePlayer> ActivePlayer;
	FGuid ActiveIntroId;
	FUILayerHandle InputScope;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle WorldCleanupHandle;
	EOutlierCinematicState State = EOutlierCinematicState::Idle;
	bool bSkipUnregisteredCinematic = false;
	bool bCompletionRequested = false;
	FKey CompleteCinematicKey = EKeys::Enter;
	int32 StableReadyFrames = 0;
	float WaitLogSeconds = 0.0f;
};
