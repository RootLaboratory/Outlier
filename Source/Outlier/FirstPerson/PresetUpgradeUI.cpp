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
