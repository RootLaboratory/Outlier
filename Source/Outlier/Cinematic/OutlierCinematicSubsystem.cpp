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
#include "Components/MeshComponent.h"
#include "Components/LightComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/GameStateBase.h"
#include "Input/ControllerInputConfig.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "NativeGameplayTags.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/CinematicHintkey.h"
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
	CinematicHintkeyClass = Registry->CinematicHintkeyClass;
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
	else if (State == EOutlierCinematicState::Playing)
	{
		SyncWorldPause();
		TickCompleteHold(DeltaTime);
		// 늦게 생성된 상대 Pawn/무기/VFX나 재생 중 다시 켜진 시각 요소도 계속 숨긴다.
		HidePairPawnVisuals();
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
		OwningController->RemoveCameraBlackoutReason(ECameraBlackoutReason::LevelLoad);
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
	bCompletionRequested = false;
	ResetCompleteHold();
	PushCinematicHint();
	bPausedByWorld = false;
	HidePairPawnVisuals();
	// 시퀀스 Fade 트랙이 암전 상태를 사전 상태로 캡처하지 않도록 재생 전에 같은 프레임에서 해제한다.
	OwningController->RemoveCameraBlackoutReason(ECameraBlackoutReason::LevelLoad);
	Player->Play();
	// 시작 RPC와 정지 복제가 같은 프레임에 도착해도 첫 프레임부터 멈춰 있게 한다.
	SyncWorldPause();
}

bool UOutlierCinematicSubsystem::HandleCompleteCinematicKey(const FKey& Key, EInputEvent Event)
{
	if (Key != EKeys::SpaceBar) return false;
	// 떼면 언제든 홀드를 초기화한다. 상태와 무관하게 먼저 처리해 홀드가 남지 않게 한다.
	if (Event == IE_Released) ResetCompleteHold();
	if (State != EOutlierCinematicState::Playing && State != EOutlierCinematicState::Restoring
		&& State != EOutlierCinematicState::Ready) return false;
	// 일시정지 중에는 완료 키를 소비하지 않아 일시정지 메뉴가 입력을 받게 한다.
	if (State == EOutlierCinematicState::Playing && GetWorld() && GetWorld()->IsPaused())
	{
		ResetCompleteHold();
		return false;
	}
	if (Event == IE_Pressed && State == EOutlierCinematicState::Playing && !bCompletionRequested)
	{
		bCompleteKeyHeld = true;
		CompleteHoldElapsed = 0.0f;
		// 홀드 시간이 0이면 즉시 완료한다.
		TickCompleteHold(0.0f);
	}
	// 홀드 중 반복/해제 입력과 서버 요청·카메라 복원 대기 중 입력도 소비한다.
	return true;
}

void UOutlierCinematicSubsystem::TickCompleteHold(float DeltaTime)
{
	if (!bCompleteKeyHeld || bCompletionRequested || State != EOutlierCinematicState::Playing) return;
	if (GetWorld() && GetWorld()->IsPaused())
	{
		ResetCompleteHold();
		return;
	}
	CompleteHoldElapsed += DeltaTime;
	// 힌트 위젯이 없으면 클래스 기본값(WBP에서 지정한 값), 그것도 없으면 2초를 쓴다.
	const UCinematicHintkey* HintDefaults = CinematicHint ? CinematicHint.Get()
		: (CinematicHintkeyClass ? CinematicHintkeyClass->GetDefaultObject<UCinematicHintkey>() : nullptr);
	const float HoldSeconds = HintDefaults ? HintDefaults->GetHoldSeconds() : 2.0f;
	const float Progress = HoldSeconds > 0.0f
		? FMath::Clamp(CompleteHoldElapsed / HoldSeconds, 0.0f, 1.0f) : 1.0f;
	if (CinematicHint) CinematicHint->SetHoldProgress(Progress);
	if (Progress < 1.0f) return;

	AFirstPersonPlayerController* PC = OwningController.Get();
	if (!PC || !PC->IsLocalController() || !ActiveIntroId.IsValid())
	{
		ResetCompleteHold();
		return;
	}
	bCompleteKeyHeld = false;
	bCompletionRequested = true;
	PC->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::CompleteRequested});
}

void UOutlierCinematicSubsystem::ResetCompleteHold()
{
	bCompleteKeyHeld = false;
	CompleteHoldElapsed = 0.0f;
	// 완료 요청 이후에는 꽉 찬 상태를 유지한다.
	if (CinematicHint && !bCompletionRequested) CinematicHint->SetHoldProgress(0.0f);
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
	PopCinematicHint();
	RestorePairPawnVisuals();
	StableReadyFrames = 0;
	WaitLogSeconds = 0.0f;
	OnCinematicFinished.Broadcast(OutlierCinematicTags::ArenaIntro());
}

void UOutlierCinematicSubsystem::FailCinematic(const TCHAR* Reason)
{
	if (State == EOutlierCinematicState::Failed) return;
	State = EOutlierCinematicState::Failed;
	PopCinematicHint();
	RestorePairPawnVisuals();
	UE_LOG(LogTemp, Error, TEXT("[Cinematic] Intro=%s failed: %s"), *ActiveIntroId.ToString(), Reason);
	if (AFirstPersonPlayerController* PC = OwningController.Get())
	{
		// 실패 시 검은 화면에 갇히지 않도록 레벨 로드 암전을 푼다.
		PC->RemoveCameraBlackoutReason(ECameraBlackoutReason::LevelLoad);
		PC->ServerNotifyCinematic({ActiveIntroId, EOutlierCinematicClientEvent::Failed});
	}
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
	PopCinematicHint();
	CinematicHintkeyClass = nullptr;
	State = EOutlierCinematicState::Idle;
	RestorePairPawnVisuals();
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
	bPausedByWorld = false;
	CompleteHoldElapsed = 0.0f;
	bCompleteKeyHeld = false;
	StableReadyFrames = 0;
	WaitLogSeconds = 0.0f;
}

void UOutlierCinematicSubsystem::PushCinematicHint()
{
	PopCinematicHint();
	AFirstPersonPlayerController* PC = OwningController.Get();
	ULocalPlayerUILayerSubsystem* Layers = GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	if (!PC || !Layers || !CinematicHintkeyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cinematic] Skip hint unavailable: assign CinematicHintkeyClass on the Arena cinematic registry."));
		return;
	}
	CinematicHint = CreateWidget<UCinematicHintkey>(PC, CinematicHintkeyClass);
	if (!CinematicHint) return;
	CinematicHint->SetHoldProgress(0.0f);
	// 표시 전용: 기존 시네마틱 입력 범위와 일시정지 메뉴의 포커스를 바꾸지 않는다.
	CinematicHintLayer = Layers->PushWidget(UILayerTags::Gameplay(), CinematicHint,
		FirstPersonInputModeTags::UI(), this, EUILayerFocusTarget::None, false, false);
	if (!CinematicHintLayer.IsValid())
	{
		CinematicHint = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("[Cinematic] Failed to push skip hint."));
	}
}

void UOutlierCinematicSubsystem::PopCinematicHint()
{
	if (CinematicHintLayer.IsValid())
	{
		if (ULocalPlayerUILayerSubsystem* Layers = GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>())
			Layers->PopLayer(CinematicHintLayer);
		CinematicHintLayer.Reset();
	}
	CinematicHint = nullptr;
}

void UOutlierCinematicSubsystem::HidePairPawnVisuals()
{
	const AFirstPersonPlayerController* PC = OwningController.Get();
	const AOutlierPlayerState* LocalPS = PC ? PC->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!LocalPS || LocalPS->GetPairId() == INDEX_NONE || !GameState) return;

	TArray<AActor*> Actors;
	for (const APlayerState* Entry : GameState->PlayerArray)
	{
		const AOutlierPlayerState* PS = Cast<AOutlierPlayerState>(Entry);
		APawn* Pawn = PS && PS->GetPairId() == LocalPS->GetPairId() ? PS->GetPawn() : nullptr;
		if (!IsValid(Pawn)) continue;
		Actors.Add(Pawn);
		// 무기 등 Pawn에 부착된 액터도 함께 숨긴다.
		Pawn->GetAttachedActors(Actors, false, true);
	}

	for (const AActor* Actor : Actors)
	{
		HideActorVisuals(Actor);
	}
}

void UOutlierCinematicSubsystem::HideActorVisuals(const AActor* Actor)
{
	if (!IsValid(Actor)) return;

	TArray<USceneComponent*> Components;
	Actor->GetComponents<USceneComponent>(Components, true);
	if (const AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(Actor))
	{
		Character->CollectDetachedCinematicVisuals(Components);
	}
	for (USceneComponent* Component : Components)
	{
		HideVisualComponentTree(Component);
	}
}

void UOutlierCinematicSubsystem::HideVisualComponentTree(USceneComponent* Component)
{
	if (!IsValid(Component)) return;

	// SpawnSystemAttached로 붙은 VFX는 Pawn이 소유하지 않을 수 있어 부착 트리도 검사한다.
	TArray<USceneComponent*> Components;
	Component->GetChildrenComponents(true, Components);
	Components.Add(Component);
	for (USceneComponent* Visual : Components)
	{
		if (!IsValid(Visual) || !Visual->IsVisible()) continue;
		if (!Visual->IsA<UMeshComponent>() && !Visual->IsA<ULightComponent>()
			&& !Visual->IsA<UFXSystemComponent>()) continue;

		// 자식 전체 전파/Deactivate 없이 렌더링만 끈다. 충돌/게임플레이와 VFX 재생 상태는 유지한다.
		// SetVisibility(false)는 ShadowMesh의 Hidden Shadow까지 끈다.
		Visual->SetVisibility(false);
		HiddenPawnVisuals.AddUnique(Visual);
	}
}

void UOutlierCinematicSubsystem::SyncWorldPause()
{
	ULevelSequencePlayer* Player = ActivePlayer.Get();
	const UWorld* World = GetWorld();
	if (!Player || !World) return;
	// 서버 SetPause가 WorldSettings로 복제되므로 각 클라이언트가 로컬 시퀀스를 같은 시점에 멈춘다.
	if (World->IsPaused())
	{
		if (!bPausedByWorld && Player->IsPlaying())
		{
			Player->Pause();
			bPausedByWorld = true;
		}
	}
	else if (bPausedByWorld)
	{
		bPausedByWorld = false;
		if (Player->IsPaused()) Player->Play();
	}
}

void UOutlierCinematicSubsystem::RestorePairPawnVisuals()
{
	for (const TWeakObjectPtr<USceneComponent>& Visual : HiddenPawnVisuals)
	{
		if (Visual.IsValid()) Visual->SetVisibility(true);
	}
	HiddenPawnVisuals.Reset();
}
