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
class USceneComponent;
class UCinematicHintkey;

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
	/** 스킵 키(스페이스 고정) 입력을 받아 홀드 시간을 시작/초기화한다. 소비했으면 true. */
	bool HandleCompleteCinematicKey(const FKey& Key, EInputEvent Event);
	/** Clear a pending skip hold when the controller flushes input (including viewport focus loss). */
	void ResetCompleteHold();
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
	void PushCinematicHint();
	void PopCinematicHint();
	/** 스킵 홀드를 진행시키고 시간이 차면 완료를 요청한다. */
	void TickCompleteHold(float DeltaTime);
	UPROPERTY(Transient)
	TSubclassOf<UCinematicHintkey> CinematicHintkeyClass;
	UPROPERTY(Transient)
	TObjectPtr<UCinematicHintkey> CinematicHint;
	FUILayerHandle CinematicHintLayer;
	bool TickCinematic(float DeltaTime);
	bool IsPlayerReady() const;
	bool BeginCinematicInput();
	bool IsCinematicInputReady() const;
	void FailCinematic(const TCHAR* Reason);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	/** 같은 페어의 Pawn/부착 액터와 월드 VFX를 로컬에서만 숨긴다. */
	void HidePairPawnVisuals();
	void HideActorVisuals(const AActor* Actor);
	void HideVisualComponentTree(USceneComponent* Component);
	void RestorePairPawnVisuals();
	/** 인게임 일시정지(월드 정지)에 맞춰 인트로 시퀀스를 멈추고 재개한다. */
	void SyncWorldPause();

	UFUNCTION()
	void HandleSequenceFinished();
	TMap<FGameplayTag, TWeakObjectPtr<ALevelSequenceActor>> Cinematics;
	TWeakObjectPtr<UWorld> BoundWorld;
	TWeakObjectPtr<AFirstPersonPlayerController> OwningController;
	TWeakObjectPtr<ULevelSequencePlayer> ActivePlayer;
	/** 이 서브시스템이 숨긴 시각 컴포넌트만 기록해 원래 보이던 것만 복구한다. */
	TArray<TWeakObjectPtr<USceneComponent>> HiddenPawnVisuals;
	FGuid ActiveIntroId;
	FUILayerHandle InputScope;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle WorldCleanupHandle;
	EOutlierCinematicState State = EOutlierCinematicState::Idle;
	bool bSkipUnregisteredCinematic = false;
	bool bCompletionRequested = false;
	/** 월드 정지로 이 서브시스템이 직접 멈춘 경우에만 재개한다. */
	bool bPausedByWorld = false;
	/** 스킵 키는 스페이스로 고정이다. 홀드 시간은 힌트 위젯(WBP)의 HoldSeconds를 사용한다. */
	float CompleteHoldElapsed = 0.0f;
	bool bCompleteKeyHeld = false;
	int32 StableReadyFrames = 0;
	float WaitLogSeconds = 0.0f;
};
