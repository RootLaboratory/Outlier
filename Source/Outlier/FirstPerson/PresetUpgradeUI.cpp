#include "FirstPerson/FirstPersonPlayerController.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "LocalPlayerUISubSystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "HAL/PlatformTime.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UpgradeMachineWidget.h"

FGuid AFirstPersonPlayerController::RequestDeveloperConveniencePreset(
	FName StageId, const FDeveloperConvenienceSnapshot& DisplayedContext)
{
	if (!IsLocalController())
	{
		return FGuid();
	}
	FDeveloperConveniencePresetRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.MatchId = DisplayedContext.MatchId;
	Request.SessionId = DisplayedContext.SessionId;
	Request.GameplayGeneration = DisplayedContext.GameplayGeneration;
	Request.StageId = StageId;
	ServerRequestDeveloperConveniencePreset(Request);
	return Request.RequestId;
}

void AFirstPersonPlayerController::ServerRequestDeveloperConveniencePreset_Implementation(
	FDeveloperConveniencePresetRequest Request)
{
	FDeveloperConveniencePresetResult Result;
	Result.RequestId = Request.RequestId;
	Result.MatchId = Request.MatchId;
	Result.SessionId = Request.SessionId;
	Result.GameplayGeneration = Request.GameplayGeneration;
	Result.StageId = Request.StageId;
	if (auto* Mode = GetWorld()->GetAuthGameMode<AOutlierGameMode>())
	{
		Result.Result = Mode->RequestDeveloperConveniencePreset(this, Request);
	}
	ClientDeveloperConveniencePresetResult(Result);
}

void AFirstPersonPlayerController::ClientDeveloperConveniencePresetResult_Implementation(
	FDeveloperConveniencePresetResult Result)
{
	OnDeveloperConveniencePresetResult.Broadcast(Result);
}

void AFirstPersonPlayerController::ClearPendingPresetUpgradeUI()
{
	GetWorldTimerManager().ClearTimer(PresetUpgradeUIReadyTimer);
	PendingPresetUpgradeRequestId.Invalidate();
	PendingPresetUpgradeGeneration = 0;
	PendingPresetUpgradePairId = INDEX_NONE;
	bPresetUpgradeUIReadyReported = false;
	PresetUpgradeUIWaitStartedAt = 0.0;
	PendingPresetUpgradeWidgetClass = nullptr;
	PresetUpgradeArena.Reset();
	PresetUpgradeLayers.Reset();
}

void AFirstPersonPlayerController::ClientBeginPresetTransition_Implementation(FGuid RequestId, uint32 GameplayGeneration)
{
	if (!IsLocalController() || !RequestId.IsValid() || bClientArenaExitPrepared
		|| !UOutlierArenaSubsystem::IsGameplayGenerationNewer(GameplayGeneration, PresetTransitionGeneration))
	{
		return;
	}
	// UI 준비 문맥은 리로드 RPC에서 초기화된다. 암전 소유권은 별도로 유지해 그 사이 화면이 열리지 않게 한다.
	PresetTransitionRequestId = RequestId;
	PresetTransitionGeneration = GameplayGeneration;
	AddCameraBlackoutReason(ECameraBlackoutReason::PresetTransition);
	ULocalPlayer* Local = GetLocalPlayer();
	SetViewportTransitionCover(true, PresetTransitionCover, PresetTransitionViewport);
	if (ULocalPlayerUISubSystem* UI = Local ? Local->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr)
	{
		UI->SetTransientWidgetsSuppressed(true);
	}
	if (UOutlierAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>() : nullptr)
	{
		Audio->SetPresetTransitionSuppressed(this, true);
	}
	RefreshHudVisibility();
}

void AFirstPersonPlayerController::EndPresetTransition(const FGuid& RequestId)
{
	if (!RequestId.IsValid() || RequestId != PresetTransitionRequestId)
	{
		return;
	}
	PresetTransitionRequestId.Invalidate();
	// 요청 소유권이 일치할 때만 가림을 걷는다. 이전 요청의 늦은 취소는 새 전환을 복구하지 않는다.
	SetViewportTransitionCover(false, PresetTransitionCover, PresetTransitionViewport);
	if (UOutlierAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>() : nullptr)
	{
		Audio->SetPresetTransitionSuppressed(this, false);
	}
	RemoveCameraBlackoutReason(ECameraBlackoutReason::PresetTransition);
	if (ULocalPlayerUISubSystem* UI = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr)
	{
		UI->SetTransientWidgetsSuppressed(bHudHiddenForDeath);
	}
	RefreshHudVisibility();
}

void AFirstPersonPlayerController::SetViewportTransitionCover(bool bEnabled, TSharedPtr<SWidget>& Cover,
	TWeakObjectPtr<UGameViewportClient>& Viewport)
{
	if (bEnabled)
	{
		ULocalPlayer* Local = GetLocalPlayer();
		if (Local && Local->ViewportClient && !Cover.IsValid())
		{
			// 최상단에서 AddToViewport로 등록된 HUD와 자막까지 덮고 숨은 클릭을 막는다.
			Cover = SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor::Black)
				.Visibility(EVisibility::Visible)
				.Cursor(EMouseCursor::None);
			Viewport = Local->ViewportClient;
			Local->ViewportClient->AddViewportWidgetContent(Cover.ToSharedRef(), 10001);
		}
		return;
	}

	if (Cover.IsValid() && Viewport.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(Cover.ToSharedRef());
	}
	Cover.Reset();
	Viewport.Reset();
}

void AFirstPersonPlayerController::ClientPreparePresetUpgradeUI_Implementation(
	FGuid RequestId, uint32 Generation, int32 PairId, TSubclassOf<UUpgradeMachineWidget> WidgetClass)
{
	if (!IsLocalController() || !RequestId.IsValid() || Generation == 0 || PairId == INDEX_NONE
		|| RequestId == LastOpenedPresetUpgradeRequestId || bClientArenaExitPrepared)
	{
		return;
	}
	auto* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	if (!Arena || Arena->GetGameplayGeneration() != Generation)
	{
		return;
	}
	if (PendingPresetUpgradeRequestId == RequestId)
	{
		return;
	}
	ClearPendingPresetUpgradeUI();
	PendingPresetUpgradeRequestId = RequestId;
	PendingPresetUpgradeGeneration = Generation;
	PendingPresetUpgradePairId = PairId;
	PendingPresetUpgradeWidgetClass = WidgetClass;
	PresetUpgradeArena = Arena;
	PresetUpgradeLayers = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
	PresetUpgradeUIWaitStartedAt = FPlatformTime::Seconds();
	// 콘텐츠 ACK 뒤에도 Pawn/PlayerState/ASC 복제가 남을 수 있다. 이 요청이 살아 있는 동안만 확인한다.
	GetWorldTimerManager().SetTimer(PresetUpgradeUIReadyTimer, this,
		&AFirstPersonPlayerController::TryReportPresetUpgradeUIReady, 0.1f, true);
	TryReportPresetUpgradeUIReady();
}

bool AFirstPersonPlayerController::IsPresetUpgradeUIReady() const
{
	const auto* State = GetPlayerState<AOutlierPlayerState>();
	const auto* Arena = PresetUpgradeArena.Get();
	if (!IsLocalController() || !PendingPresetUpgradeRequestId.IsValid() || !State || !Arena
		|| !PresetUpgradeLayers.IsValid() || Arena->GetGameplayGeneration() != PendingPresetUpgradeGeneration
		|| !Arena->IsGameplayLevelsReady() || State->GetPairId() != PendingPresetUpgradePairId
		|| !PendingPresetUpgradeWidgetClass || PendingPresetUpgradeWidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	auto* Shooter = State->GetShooterCharacter();
	auto* Partner = State->GetPartnerCharacter();
	const auto* ShooterState = IsValid(Shooter) ? Shooter->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const auto* PartnerState = IsValid(Partner) ? Partner->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (!ShooterState || !PartnerState || !ShooterState->IsShooterPlayer() || !PartnerState->IsPartnerPlayer()
		|| ShooterState->GetPairId() != PendingPresetUpgradePairId || PartnerState->GetPairId() != PendingPresetUpgradePairId
		|| ShooterState->GetShooterCharacter() != Shooter || PartnerState->GetShooterCharacter() != Shooter
		|| ShooterState->GetPartnerCharacter() != Partner || PartnerState->GetPartnerCharacter() != Partner
		|| !((State == ShooterState && GetPawn() == Shooter) || (State == PartnerState && GetPawn() == Partner)))
	{
		return false;
	}
	const auto* ShooterASC = Shooter->GetOutlierAbilitySystemComponent();
	const auto* PartnerASC = Partner->GetOutlierAbilitySystemComponent();
	return ShooterASC && PartnerASC && ShooterASC->GetAvatarActor() == Shooter && PartnerASC->GetAvatarActor() == Partner;
}

void AFirstPersonPlayerController::TryReportPresetUpgradeUIReady()
{
	if (!PendingPresetUpgradeRequestId.IsValid() || bPresetUpgradeUIReadyReported)
	{
		return;
	}
	const bool bReady = IsPresetUpgradeUIReady();
	const auto* Arena = PresetUpgradeArena.Get();
	const bool bUnavailable = !Arena || Arena->GetGameplayGeneration() != PendingPresetUpgradeGeneration
		|| !PresetUpgradeLayers.IsValid() || !PendingPresetUpgradeWidgetClass
		|| PendingPresetUpgradeWidgetClass->HasAnyClassFlags(CLASS_Abstract)
		|| FPlatformTime::Seconds() - PresetUpgradeUIWaitStartedAt >= 15.0;
	if (!bReady && !bUnavailable)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PresetUpgradeUIReadyTimer);
	bPresetUpgradeUIReadyReported = true;
	const FGuid RequestId = PendingPresetUpgradeRequestId;
	const uint32 Generation = PendingPresetUpgradeGeneration;
	ServerPresetUpgradeUIReady(RequestId, Generation, bReady);
	if (!bReady && PendingPresetUpgradeRequestId == RequestId)
	{
		ClearPendingPresetUpgradeUI();
		EndPresetTransition(RequestId);
	}
}

void AFirstPersonPlayerController::ServerPresetUpgradeUIReady_Implementation(
	FGuid RequestId, uint32 Generation, bool bAvailable)
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<AOutlierGameMode>())
	{
		Mode->OnClientPresetUpgradeUIReady(this, RequestId, Generation, bAvailable);
	}
}

void AFirstPersonPlayerController::ClientCancelPresetUpgradeUI_Implementation(FGuid RequestId)
{
	// 준비 RPC 이전의 로딩 실패도 정리하고, 이전 요청의 취소는 새 암전에 영향을 주지 않는다.
	EndPresetTransition(RequestId);
	if (RequestId == PendingPresetUpgradeRequestId)
	{
		ClearPendingPresetUpgradeUI();
	}
}

void AFirstPersonPlayerController::ClientOpenPresetUpgradeUI_Implementation(
	FGuid RequestId, uint32 Generation, const FUILayerPushRequest& Request)
{
	if (RequestId != PendingPresetUpgradeRequestId || RequestId == LastOpenedPresetUpgradeRequestId
		|| Generation != PendingPresetUpgradeGeneration)
	{
		return;
	}
	if (!IsPresetUpgradeUIReady() || Request.WidgetClass != PendingPresetUpgradeWidgetClass)
	{
		ClientCancelPresetUpgradeUI_Implementation(RequestId);
		UE_LOG(LogTemp, Warning, TEXT("[PresetUpgrade] Local context changed before opening PC=%s Gen=%u"), *GetNameSafe(this), Generation);
		return;
	}
	const auto* State = GetPlayerState<AOutlierPlayerState>();
	if (Request.ContextActors.Num() != 2 || Request.ContextActors[0] != State->GetShooterCharacter()
		|| Request.ContextActors[1] != State->GetPartnerCharacter()
		|| Request.RequestOwner != State->GetShooterCharacter()->GetPlayerState())
	{
		ClientCancelPresetUpgradeUI_Implementation(RequestId);
		return;
	}
	// 닫기 스냅샷보다 Open RPC가 먼저 와도 이전 메뉴가 새 강화 UI의 입력을 복구하지 않게 먼저 정리한다.
	ClearDeveloperConveniencePresentation();
	// 양쪽 준비 ACK 뒤에도 위젯 생성은 실패할 수 있다. 실제 레이어 등록 결과를 확인한 뒤 화면을 연다.
	const bool bOpened = PresetUpgradeLayers->PushWidget(Request).IsValid();
	if (bOpened)
	{
		LastOpenedPresetUpgradeRequestId = RequestId;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetUpgrade] Failed to open UI PC=%s Gen=%u"), *GetNameSafe(this), Generation);
	}
	ClearPendingPresetUpgradeUI();
	EndPresetTransition(RequestId);
}
