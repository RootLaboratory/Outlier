#include "OutlierGameMode.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/UpgradeMachineWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "Upgrade/OutlierPresetStageIds.h"

EDeveloperConvenienceRequestResult AOutlierGameMode::RequestDeveloperConveniencePreset(
	AFirstPersonPlayerController* Requester, const FDeveloperConveniencePresetRequest& Request)
{
	const auto Validation = ValidateDeveloperConvenienceAction(Requester, Request.RequestId,
		Request.MatchId, Request.SessionId, Request.GameplayGeneration);
	if (Validation != EDeveloperConvenienceRequestResult::Accepted)
	{
		return Validation;
	}
	if (!OutlierPresetStageIds::All().Contains(Request.StageId))
	{
		return EDeveloperConvenienceRequestResult::InvalidStage;
	}
	if (!PresetUpgradeWidgetClass || PresetUpgradeWidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return EDeveloperConvenienceRequestResult::UIUnavailable;
	}
	// 스폰/로드 사전 검증이 끝나기 전에는 메뉴와 정지를 유지한다.
	const bool bAccepted = RequestPresetRespawn(Requester, Request.StageId, [this]()
	{
		DeveloperConvenienceState.bTransitioning = true;
		PublishDeveloperConvenienceState();
		return true;
	});
	return bAccepted ? EDeveloperConvenienceRequestResult::Accepted
		: EDeveloperConvenienceRequestResult::TargetUnavailable;
}

bool AOutlierGameMode::OpenPairUpgradeUI(AOutlierPlayerState* ShooterState, AOutlierPlayerState* PartnerState,
	TSubclassOf<UUpgradeMachineWidget> WidgetClass, AActor* RequestOwner, const FGuid& PresetRequestId,
	TFunction<void()> BeforeOpen)
{
	auto* Shooter = IsValid(ShooterState) ? ShooterState->GetShooterCharacter() : nullptr;
	auto* Partner = IsValid(PartnerState) ? PartnerState->GetPartnerCharacter() : nullptr;
	auto* ShooterController = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(ShooterState));
	auto* PartnerController = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(PartnerState));
	if (!HasAuthority() || !IsValid(Shooter) || !IsValid(Partner)
		|| !ShooterController || !PartnerController || ShooterController == PartnerController
		|| ShooterState->GetWorld() != GetWorld() || PartnerState->GetWorld() != GetWorld()
		|| !ShooterState->IsShooterPlayer() || !PartnerState->IsPartnerPlayer()
		|| ShooterState->GetPairId() == INDEX_NONE || ShooterState->GetPairId() != PartnerState->GetPairId()
		|| !IsValid(RequestOwner) || RequestOwner->GetWorld() != GetWorld()
		|| !WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	ShooterState->BeginStatAllocatorSession(PartnerState);
	FUILayerPushRequest Request;
	Request.WidgetClass = WidgetClass;
	Request.LayerTag = UILayerTags::GameMenu();
	Request.InputModeTag = FirstPersonInputModeTags::UI();
	Request.RequestOwner = RequestOwner;
	Request.ContextActors = { Shooter, Partner };
	Request.FocusTarget = EUILayerFocusTarget::Widget;
	Request.bShowCursor = true;
	if (BeforeOpen)
	{
		BeforeOpen();
	}
	const auto* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	for (auto* Controller : { ShooterController, PartnerController })
	{
		if (PresetRequestId.IsValid())
		{
			Controller->ClientOpenPresetUpgradeUI(PresetRequestId, Arena->GetGameplayGeneration(), Request);
		}
		else
		{
			Controller->ClientPushUILayer(Request);
		}
	}
	return true;
}

void AOutlierGameMode::ArmPresetUpgradeUI(int32 PairId, uint32 Generation)
{
	PendingPresetUpgradeRequestId = FGuid::NewGuid();
	PendingPresetUpgradePairId = PairId;
	PendingPresetUpgradeGeneration = Generation;
}

void AOutlierGameMode::ClearPendingPresetUpgradeUI()
{
	const FGuid RequestId = PendingPresetUpgradeRequestId;
	const auto Controllers = PresetUpgradePreparedControllers;
	// Listen의 동기 RPC 콜백도 이미 종료된 요청을 재사용하지 못하도록 먼저 비운다.
	PendingPresetUpgradeRequestId.Invalidate();
	PendingPresetUpgradePairId = INDEX_NONE;
	PendingPresetUpgradeGeneration = 0;
	PresetUpgradePreparedControllers.Reset();
	PresetUpgradeReadyControllers.Reset();
	for (const auto& Controller : Controllers)
	{
		if (Controller.IsValid())
		{
			Controller->ClientCancelPresetUpgradeUI(RequestId);
		}
	}
}

bool AOutlierGameMode::IsPresetUpgradeContextCurrent() const
{
	const auto* Arena = GetWorld() ? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	return HasAuthority() && PendingPresetUpgradeRequestId.IsValid() && Arena
		&& PendingPresetUpgradeGeneration != 0 && PendingPresetUpgradeGeneration == Arena->GetGameplayGeneration()
		&& PendingPresetUpgradePairId != INDEX_NONE && PendingPresetUpgradePairId == ActiveMatchPairId;
}

void AOutlierGameMode::TryPreparePresetUpgradeUI()
{
	if (!IsPresetUpgradeContextCurrent() || !IsPairGameplayReady())
	{
		return;
	}
	if (!PresetUpgradeWidgetClass || PresetUpgradeWidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetUpgrade] Assign PresetUpgradeWidgetClass to the existing upgrade WBP."));
		ClearPendingPresetUpgradeUI();
		return;
	}
	const FGuid RequestId = PendingPresetUpgradeRequestId;
	for (EOutlierPlayerRole PlayerRole : { EOutlierPlayerRole::Shooter, EOutlierPlayerRole::Partner })
	{
		auto* Controller = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(
			FindPairPlayerState(PendingPresetUpgradePairId, PlayerRole)));
		if (Controller && !PresetUpgradePreparedControllers.Contains(Controller))
		{
			PresetUpgradePreparedControllers.Add(Controller);
			Controller->ClientPreparePresetUpgradeUI(RequestId, PendingPresetUpgradeGeneration,
				PendingPresetUpgradePairId, PresetUpgradeWidgetClass);
			if (PendingPresetUpgradeRequestId != RequestId)
			{
				return;
			}
		}
	}
}

void AOutlierGameMode::OnClientPresetUpgradeUIReady(AFirstPersonPlayerController* Controller,
	const FGuid& RequestId, uint32 Generation, bool bAvailable)
{
	if (!IsPresetUpgradeContextCurrent() || RequestId != PendingPresetUpgradeRequestId
		|| Generation != PendingPresetUpgradeGeneration || !PresetUpgradePreparedControllers.Contains(Controller))
	{
		return;
	}
	auto* ShooterState = FindPairPlayerState(PendingPresetUpgradePairId, EOutlierPlayerRole::Shooter);
	auto* PartnerState = FindPairPlayerState(PendingPresetUpgradePairId, EOutlierPlayerRole::Partner);
	auto* ShooterController = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(ShooterState));
	auto* PartnerController = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(PartnerState));
	if (Controller != ShooterController && Controller != PartnerController)
	{
		return;
	}
	if (!bAvailable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetUpgrade] Client unavailable PC=%s Gen=%u"), *GetNameSafe(Controller), Generation);
		ClearPendingPresetUpgradeUI();
		return;
	}
	PresetUpgradeReadyControllers.Add(Controller);
	if (!IsPairGameplayReady() || !ShooterController || !PartnerController
		|| !PresetUpgradeReadyControllers.Contains(ShooterController)
		|| !PresetUpgradeReadyControllers.Contains(PartnerController))
	{
		return;
	}
	// 두 현재 소유자의 ACK가 모인 뒤 예약을 소비한다. 중복 ACK는 다음 UI를 열 수 없다.
	// 성공 경로에서는 Cancel RPC를 보내지 않아 클라이언트가 Open 문맥을 검증할 수 있다.
	PendingPresetUpgradeRequestId.Invalidate();
	PendingPresetUpgradePairId = INDEX_NONE;
	PendingPresetUpgradeGeneration = 0;
	PresetUpgradePreparedControllers.Reset();
	PresetUpgradeReadyControllers.Reset();
	if (!OpenPairUpgradeUI(ShooterState, PartnerState, PresetUpgradeWidgetClass, ShooterState, RequestId))
	{
		ShooterController->ClientCancelPresetUpgradeUI(RequestId);
		PartnerController->ClientCancelPresetUpgradeUI(RequestId);
		UE_LOG(LogTemp, Warning, TEXT("[PresetUpgrade] Pair context changed before opening Gen=%u"), Generation);
	}
}
