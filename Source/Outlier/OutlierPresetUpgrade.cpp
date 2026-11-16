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
	for (auto* Controller : { ShooterController, PartnerController })
	{
		Controller->ClientPushUILayer(Request);
	}
	return true;
}
