#include "FirstPerson/FirstPersonPlayerController.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
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
	if (RequestId == PendingPresetUpgradeRequestId)
	{
		ClearPendingPresetUpgradeUI();
	}
}

void AFirstPersonPlayerController::ClientOpenPresetUpgradeUI_Implementation(
	FGuid RequestId, uint32 Generation, const FUILayerPushRequest& Request)
{
	if (RequestId != PendingPresetUpgradeRequestId || RequestId == LastOpenedPresetUpgradeRequestId
		|| Generation != PendingPresetUpgradeGeneration || !IsPresetUpgradeUIReady()
		|| Request.WidgetClass != PendingPresetUpgradeWidgetClass)
	{
		return;
	}
	const auto* State = GetPlayerState<AOutlierPlayerState>();
	if (Request.ContextActors.Num() != 2 || Request.ContextActors[0] != State->GetShooterCharacter()
		|| Request.ContextActors[1] != State->GetPartnerCharacter()
		|| Request.RequestOwner != State->GetShooterCharacter()->GetPlayerState())
	{
		return;
	}
	// 닫기 스냅샷보다 Open RPC가 먼저 와도 이전 메뉴가 새 강화 UI의 입력을 복구하지 않게 먼저 정리한다.
	ClearDeveloperConveniencePresentation();
	LastOpenedPresetUpgradeRequestId = RequestId;
	ClearPendingPresetUpgradeUI();
	ClientPushUILayer_Implementation(Request);
}
