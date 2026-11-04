#include "UI/LocalPlayerUILayerSubsystem.h"

#include "Enemy/EnemyBase.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "MainUIBase.h"
#include "UI/EnemyHudWidget.h"
#include "UI/UILayerGameplayTags.h"

void ULocalPlayerUILayerSubsystem::HandleDroneDamagePreview(EDroneDamagePreviewAction Action)
{
	if (Action == EDroneDamagePreviewAction::Stop)
	{
		StopDroneDamagePreview();
		return;
	}
	APlayerController* PC = GetLocalPlayerController();
	UWorld* World = GetWorld();
	if (!PC || !PC->IsLocalController() || !PC->GetPawn() || !World || !World->IsGameWorld()
		|| IsEndingPresentationActive())
	{
		DronePreviewMessage = TEXT("Start PIE and possess a Partner/Shooter pawn to preview.");
		return;
	}
	if (Cast<AEnemyBase>(PC->GetPawn()))
	{
		DronePreviewMessage = TEXT("Return to Partner/Shooter before previewing. Live drone feedback is already available.");
		return;
	}
	if (!DronePreviewHud)
	{
		// 가장 가까운 드론에 설정된 HUD를 사용하며, 없으면 프로젝트의 라이플 HUD를 사용한다.
		TSubclassOf<UEnemyHudWidget> HudClass;
		float ClosestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<AEnemyBase> It(World); It; ++It)
		{
			const float Distance = FVector::DistSquared(It->GetActorLocation(), PC->GetPawn()->GetActorLocation());
			if (It->GetPossessedHudClass() && Distance < ClosestDistance)
			{
				HudClass = It->GetPossessedHudClass();
				ClosestDistance = Distance;
			}
		}
		if (!HudClass) HudClass = LoadClass<UEnemyHudWidget>(nullptr,
			TEXT("/Game/Blueprints/Widget/Enemy/WBP_EnemyHud_Rifle.WBP_EnemyHud_Rifle_C"));
		UEnemyHudWidget* Hud = HudClass ? CreateWidget<UEnemyHudWidget>(PC, HudClass) : nullptr;
		if (!Hud || !Hud->InitializeDamagePreview(FMath::Rand()))
		{
			DronePreviewMessage = TEXT("Could not create Drone HUD or bind damage masks.");
			return;
		}
		const FUILayerHandle Layer = PushWidget(UILayerTags::Gameplay(), Hud, FirstPersonInputModeTags::UI(),
			this, EUILayerFocusTarget::None, false, false);
		if (!Layer.IsValid())
		{
			Hud->ReleaseDamageFeedback();
			DronePreviewMessage = TEXT("Could not attach Drone HUD to the gameplay layer.");
			return;
		}
		DronePreviewHud = Hud;
		DronePreviewLayer = Layer;
		DronePreviewHealthRatio = 1.0f;
		DronePreviewMainUI = RegisteredMainUI;
		if (UMainUIBase* MainUI = DronePreviewMainUI.Get())
		{
			DronePreviewPreviousVisibility = MainUI->GetVisibility();
			MainUI->SetVisibility(ESlateVisibility::Collapsed);
		}
		DronePreviewController = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ULocalPlayerUILayerSubsystem::HandleDronePreviewPawnChanged);
	}
	if (Action == EDroneDamagePreviewAction::Start || Action == EDroneDamagePreviewAction::Reset)
	{
		DronePreviewHud->InitializeDamagePreview(FMath::Rand());
		DronePreviewHealthRatio = 1.0f;
	}
	else if (Action == EDroneDamagePreviewAction::Damage)
	{
		// 체력 20%에서 멈춘다. 사망 연출 없이 네 단계로 모든 모서리를 드러낸다.
		DronePreviewHealthRatio = FMath::Max(0.2f, DronePreviewHealthRatio - 0.2f);
		DronePreviewHud->SetDamagePreviewHealthRatio(DronePreviewHealthRatio);
	}
	DronePreviewMessage = FString::Printf(TEXT("Preview: %s | simulated HP %.0f%% | no gameplay damage"),
		*DronePreviewHud->GetClass()->GetName(), DronePreviewHealthRatio * 100.0f);
}

void ULocalPlayerUILayerSubsystem::StopDroneDamagePreview()
{
	if (APlayerController* PC = DronePreviewController.Get())
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &ULocalPlayerUILayerSubsystem::HandleDronePreviewPawnChanged);
	DronePreviewController.Reset();
	if (DronePreviewHud) DronePreviewHud->ReleaseDamageFeedback();
	if (DronePreviewLayer.IsValid()) PopLayer(DronePreviewLayer);
	DronePreviewLayer.Reset();
	DronePreviewHud = nullptr;
	if (UMainUIBase* MainUI = DronePreviewMainUI.Get()) MainUI->SetVisibility(DronePreviewPreviousVisibility);
	DronePreviewMainUI.Reset();
	DronePreviewHealthRatio = 1.0f;
	DronePreviewMessage = TEXT("Preview stopped. Original HUD restored.");
}

void ULocalPlayerUILayerSubsystem::HandleDronePreviewPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	StopDroneDamagePreview();
}
