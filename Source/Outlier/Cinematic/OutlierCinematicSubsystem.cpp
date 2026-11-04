#include "Cinematic/OutlierCinematicSubsystem.h"

#include "Cinematic/OutlierCinematicRegistry.h"
#include "Cinematic/OutlierCinematicTypes.h"
#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Input/ControllerInputConfig.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "NativeGameplayTags.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ArenaIntro, "Cinematic.Arena.Intro");

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Title, "Cinematic.Title");

FGameplayTag OutlierCinematicTags::ArenaIntro()
{
	return TAG_ArenaIntro;
}

FGameplayTag OutlierCinematicTags::Title()
{
	return TAG_Title;
}

void UOutlierCinematicSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<ULocalPlayerUILayerSubsystem>();
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this, &UOutlierCinematicSubsystem::HandleWorldCleanup);
}

void UOutlierCinematicSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	ClearLevelCinematics();
	Super::Deinitialize();
}

void UOutlierCinematicSubsystem::HandleWorldCleanup(UWorld* World, bool, bool)
{
	if (BoundWorld.Get() == World) ClearLevelCinematics();
}

EOutlierCinematicRegistration UOutlierCinematicSubsystem::BindLevelCinematics(FGameplayTag Tag)
{
	Cinematics.Reset();
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown) return EOutlierCinematicRegistration::Invalid;
	// 아레나가 타이틀 월드로 스트리밍되므로 각 레벨에 레지스트리가 있을 수 있다.
	// 요청한 시네마틱이 등록된 레지스트리를 선택한다.
	AOutlierCinematicRegistry* Registry = nullptr;
	for (TActorIterator<AOutlierCinematicRegistry> It(World); It; ++It)
	{
		if (!IsValid(*It) || !It->Cinematics.Contains(Tag)) continue;
		if (Registry)
		{
			UE_LOG(LogTemp, Error, TEXT("[Cinematic] Multiple registries register %s in %s"), *Tag.ToString(), *World->GetName());
			return EOutlierCinematicRegistration::Invalid;
		}
		Registry = *It;
	}
	if (!Registry)
	{
		UE_LOG(LogTemp, Error, TEXT("[Cinematic] No registry registers %s in %s; skipping it"), *Tag.ToString(), *World->GetName());
		return EOutlierCinematicRegistration::Unregistered;
	}
	for (const auto& Entry : Registry->Cinematics)
	{
		// 액터가 미할당된 항목은 미등록 시네마틱으로 처리한다.
		if (Entry.Key.IsValid() && Entry.Value == nullptr) continue;
		if (!Entry.Key.IsValid() || !IsValid(Entry.Value.Get()) || Entry.Value->GetWorld() != World)
		{
			UE_LOG(LogTemp, Error, TEXT("[Cinematic] Invalid registry entry %s"), *Entry.Key.ToString());
			Cinematics.Reset();
			return EOutlierCinematicRegistration::Invalid;
		}
		Cinematics.Add(Entry.Key, Entry.Value.Get());
	}
	BoundWorld = World;
	CompleteCinematicKey = Registry->CompleteCinematicKey;
	return EOutlierCinematicRegistration::Bound;
}

bool UOutlierCinematicSubsystem::ValidateCinematic(FGameplayTag Tag) const
{
	const auto* Entry = Cinematics.Find(Tag);
	ALevelSequenceActor* Actor = Entry ? Entry->Get() : nullptr;
	return IsValid(Actor) && Actor->GetWorld() == GetWorld() && Actor->GetSequence()
		&& IsValid(Actor->GetSequencePlayer()) && !Actor->bReplicatePlayback
		&& !Actor->PlaybackSettings.bAutoPlay && !Actor->PlaybackSettings.bPauseAtEnd
		&& Actor->PlaybackSettings.LoopCount.Value == 0
		&& !Actor->GetSequencePlayer()->IsPlaying();
}

bool UOutlierCinematicSubsystem::IsPlayerReady() const
{
	const AFirstPersonPlayerController* PC = OwningController.Get();
	const AOutlierPlayerState* PS = PC ? PC->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UOutlierArenaSubsystem* Arena = GetWorld() ? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	return PC && PC->IsLocalController() && PC->GetLocalPlayer() == GetLocalPlayer()
		&& PC->GetWorld() == GetWorld() && IsValid(PS) && PS->GetPairId() != INDEX_NONE
		&& PS->GetPlayerRole() != EOutlierPlayerRole::None && IsValid(Pawn)
		&& Pawn->GetController() == PC && Pawn->GetWorld() == GetWorld()
		&& IsValid(PC->PlayerCameraManager) && Arena && Arena->IsArenaContentReady();
}

bool UOutlierCinematicSubsystem::BeginCinematicInput()
{
	ULocalPlayerUILayerSubsystem* Layers = GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	if (!Layers) return false;
	if (!InputScope.IsValid())
	{
		InputScope = Layers->PushInputScope(UILayerTags::Gameplay(), FirstPersonInputModeTags::UI(), this);
	}
	return IsCinematicInputReady();
}

bool UOutlierCinematicSubsystem::IsCinematicInputReady() const
{
	const auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	const auto* Layers = GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	const auto* Config = Layers ? Layers->GetWidgetInputConfig() : nullptr;
	const FGameplayTag Mode = FirstPersonInputModeTags::UI();
	return InputScope.IsValid() && Input && Layers && Input->GetInputMode().Num() == 1
		&& Input->GetInputMode().HasTagExact(Mode) && Layers->GetActiveInputModeTag() == Mode
		&& Config && Config->WidgetEscapeAction
		&& !Input->QueryKeysMappedToAction(Config->WidgetEscapeAction).IsEmpty()
		&& !GetDefault<UEnhancedInputDeveloperSettings>()->DefaultInputMode.IsEmpty();
}

void UOutlierCinematicSubsystem::PrepareInitialIntro(
	AFirstPersonPlayerController* Controller, const FGuid& IntroId)
{
	if (!Controller || !Controller->IsLocalController() || !IntroId.IsValid()) return;
	if (ActiveIntroId == IntroId) return;
	ClearLevelCinematics();
	OwningController = Controller;
	BoundWorld = Controller->GetWorld();
	ActiveIntroId = IntroId;
	State = EOutlierCinematicState::Preparing;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UOutlierCinematicSubsystem::TickCinematic));
}

bool UOutlierCinematicSubsystem::TickCinematic(float DeltaTime)
{
	if (State == EOutlierCinematicState::Preparing)
	{
		if (IsPlayerReady())
		{
			const EOutlierCinematicRegistration Registration = BindLevelCinematics(OutlierCinematicTags::ArenaIntro());
			if (Registration == EOutlierCinematicRegistration::Invalid)
			{
				FailCinematic(TEXT("Invalid cinematic registry"));
			}
			else if (Registration == EOutlierCinematicRegistration::Unregistered
				|| !Cinematics.Contains(OutlierCinematicTags::ArenaIntro()))
			{
				if (Registration == EOutlierCinematicRegistration::Bound)
					UE_LOG(LogTemp, Error, TEXT("[Cinematic] Cinematic.Arena.Intro has no registered Sequence Actor; skipping the initial Intro"));
				// 입력과 카메라를 변경하지 않고 양쪽 클라이언트의 준비 및 완료 동기화에 참여한다.
				bSkipUnregisteredCinematic = true;
				State = EOutlierCinematicState::Prepared;
				OwningController->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::Prepared});
			}
			else if (!BeginCinematicInput())
			{
				FailCinematic(TEXT("Widget-only input scope or Pause/Escape mapping is unavailable"));
			}
			else if (!ValidateCinematic(OutlierCinematicTags::ArenaIntro()))
			{
				FailCinematic(TEXT("Invalid Intro registration/player/settings (disable Auto Play, Replicate Playback, looping and Pause at End)"));
			}
			else
			{
				State = EOutlierCinematicState::Prepared;
				OwningController->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::Prepared});
			}
		}
	}
	else if (State == EOutlierCinematicState::Playing
		&& (!ActivePlayer.IsValid() || (!ActivePlayer->IsPlaying() && !ActivePlayer->IsPaused())))
	{
		// UE 5.7에서는 자연 종료 시에도 OnFinished 전에 OnStop이 호출된다.
		// 다음 틱에서 검사해 실제 재생 중단만 실패로 처리한다.
		FailCinematic(TEXT("Sequence Player was removed or stopped without finishing"));
	}
	else if (State == EOutlierCinematicState::Restoring && IsPlayerReady())
	{
		AFirstPersonPlayerController* PC = OwningController.Get();
		// 시퀀스 평가와 상태 복원이 끝난 뒤 현재 Pawn을 기준으로 실행한다.
		PC->SetViewTarget(PC->GetPawn());
		if (PC->GetViewTarget() == PC->GetPawn() && IsCinematicInputReady())
		{
			if (++StableReadyFrames >= 2)
			{
				State = EOutlierCinematicState::Ready;
				OwningController->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::Ready});
			}
		}
		else StableReadyFrames = 0;
	}
	else if (State == EOutlierCinematicState::Restoring)
	{
		StableReadyFrames = 0;
	}

	if (State == EOutlierCinematicState::Preparing || State == EOutlierCinematicState::Restoring)
	{
		WaitLogSeconds += DeltaTime;
		if (WaitLogSeconds >= 10.0f)
		{
			WaitLogSeconds = 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("[Cinematic] Waiting for local player/camera readiness Intro=%s State=%d"),
				*ActiveIntroId.ToString(), static_cast<int32>(State));
		}
	}
	return true;
}

void UOutlierCinematicSubsystem::StartCinematic(const FGuid& IntroId)
{
	if (IntroId != ActiveIntroId || State != EOutlierCinematicState::Prepared) return;
	if (bSkipUnregisteredCinematic)
	{
		if (!IsPlayerReady())
		{
			FailCinematic(TEXT("Player readiness changed before skipping the unregistered Intro"));
			return;
		}
		State = EOutlierCinematicState::Ready;
		OwningController->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::Ready});
		return;
	}
	if (!IsPlayerReady() || !IsCinematicInputReady() || !ValidateCinematic(OutlierCinematicTags::ArenaIntro()))
	{
		FailCinematic(TEXT("Intro preconditions changed before playback"));
		return;
	}
	ULevelSequencePlayer* Player = Cinematics.FindChecked(OutlierCinematicTags::ArenaIntro())->GetSequencePlayer();
	ActivePlayer = Player;
	Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(Player->GetStartTime().Time, EUpdatePositionMethod::Jump));
	Player->OnFinished.AddUniqueDynamic(this, &UOutlierCinematicSubsystem::HandleSequenceFinished);
	State = EOutlierCinematicState::Playing;
	Player->Play();
}

bool UOutlierCinematicSubsystem::HandleCompleteCinematicKey(const FKey& Key)
{
	if (!CompleteCinematicKey.IsValid() || Key != CompleteCinematicKey
		|| (State != EOutlierCinematicState::Playing && State != EOutlierCinematicState::Restoring
			&& State != EOutlierCinematicState::Ready)) return false;
	// 서버 요청이나 카메라 복원을 기다리는 동안 반복 입력을 소비한다.
	if (State == EOutlierCinematicState::Playing && !bCompletionRequested)
	{
		AFirstPersonPlayerController* PC = OwningController.Get();
		if (!PC || !PC->IsLocalController() || !ActiveIntroId.IsValid()) return false;
		bCompletionRequested = true;
		PC->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::CompleteRequested});
	}
	return true;
}

void UOutlierCinematicSubsystem::CompleteCinematic(const FGuid& IntroId)
{
	if (IntroId != ActiveIntroId || State != EOutlierCinematicState::Playing) return;
	ULevelSequencePlayer* Player = ActivePlayer.Get();
	if (!Player)
	{
		FailCinematic(TEXT("Cannot complete a removed Sequence Player"));
		return;
	}
	// 마지막 프레임을 평가하고 시퀀스 상태를 복원한다. GoToEndAndStop은 OnFinished를 호출하지 않는다.
	Player->GoToEndAndStop();
	HandleSequenceFinished();
}

void UOutlierCinematicSubsystem::HandleSequenceFinished()
{
	if (State != EOutlierCinematicState::Playing) return;
	State = EOutlierCinematicState::Restoring;
	StableReadyFrames = 0;
	WaitLogSeconds = 0.0f;
	OnCinematicFinished.Broadcast(OutlierCinematicTags::ArenaIntro());
}

void UOutlierCinematicSubsystem::FailCinematic(const TCHAR* Reason)
{
	if (State == EOutlierCinematicState::Failed) return;
	State = EOutlierCinematicState::Failed;
	UE_LOG(LogTemp, Error, TEXT("[Cinematic] Intro=%s failed: %s"), *ActiveIntroId.ToString(), Reason);
	if (AFirstPersonPlayerController* PC = OwningController.Get())
		PC->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::Failed});
}

void UOutlierCinematicSubsystem::ReleaseCinematic(const FGuid& IntroId)
{
	if (IntroId != ActiveIntroId || State != EOutlierCinematicState::Ready) return;
	ClearLevelCinematics();
	const auto* Layers = GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	const auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	const FGameplayTag ActiveMode = Layers ? Layers->GetActiveInputModeTag() : FGameplayTag();
	const FGameplayTagContainer& DefaultMode = GetDefault<UEnhancedInputDeveloperSettings>()->DefaultInputMode;
	const bool bInputRestored = Input && (ActiveMode.IsValid()
		? Input->GetInputMode().Num() == 1 && Input->GetInputMode().HasTagExact(ActiveMode)
		: Input->GetInputMode() == DefaultMode);
	if (!bInputRestored)
		UE_LOG(LogTemp, Error, TEXT("[Cinematic] Input restoration failed after server released Intro=%s"), *IntroId.ToString());
}

bool UOutlierCinematicSubsystem::PlayTitleCinematic()
{
	ClearLevelCinematics();
	const FGameplayTag Tag = OutlierCinematicTags::Title();
	if (BindLevelCinematics(Tag) != EOutlierCinematicRegistration::Bound || !Cinematics.Contains(Tag))
	{
		UE_LOG(LogTemp, Error, TEXT("[Cinematic] %s has no registered Sequence Actor"), *Tag.ToString());
		ClearLevelCinematics();
		return false;
	}
	// 인트로와 같은 설정 규칙을 사용한다. 반복 재생은 액터의 Playback Settings 대신 여기서 제어한다.
	if (!ValidateCinematic(Tag))
	{
		UE_LOG(LogTemp, Error, TEXT("[Cinematic] Invalid %s registration/player/settings (disable Auto Play, Replicate Playback, looping and Pause at End)"),
			*Tag.ToString());
		ClearLevelCinematics();
		return false;
	}
	ULevelSequencePlayer* Player = Cinematics.FindChecked(Tag)->GetSequencePlayer();
	ActivePlayer = Player;
	Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(Player->GetStartTime().Time, EUpdatePositionMethod::Jump));
	Player->PlayLooping();
	UE_LOG(LogTemp, Display, TEXT("[Cinematic] Playing %s Actor=%s"), *Tag.ToString(), *GetNameSafe(Cinematics.FindChecked(Tag).Get()));
	return true;
}

void UOutlierCinematicSubsystem::ClearLevelCinematics()
{
	State = EOutlierCinematicState::Idle;
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (ULevelSequencePlayer* Player = ActivePlayer.Get())
	{
		Player->OnFinished.RemoveDynamic(this, &UOutlierCinematicSubsystem::HandleSequenceFinished);
		if (Player->IsPlaying() || Player->IsPaused()) Player->Stop();
	}
	ActivePlayer.Reset();
	if (InputScope.IsValid())
	{
		if (auto* Layers = GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>())
			Layers->PopLayer(InputScope);
		InputScope.Reset();
	}
	Cinematics.Reset();
	BoundWorld.Reset();
	OwningController.Reset();
	ActiveIntroId.Invalidate();
	bSkipUnregisteredCinematic = false;
	bCompletionRequested = false;
	CompleteCinematicKey = EKeys::Enter;
	StableReadyFrames = 0;
	WaitLogSeconds = 0.0f;
}
