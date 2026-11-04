#include "FirstPerson/FirstPersonPlayerController.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "AudioDevice.h"
#include "Camera/PlayerCameraManager.h"
#include "Cinematic/OutlierCinematicSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "LocalPlayerUISubSystem.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "UI/ActorBasedWidgetSubsystem.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/OutlierEndingWidget.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerKeyHintWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "VisualEventSubsystem.h"

bool AFirstPersonPlayerController::IsEndingPresentationActive() const
{
	const ULocalPlayer* Local = GetLocalPlayer();
	const auto* LayerSubsystem = Local ? Local->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
	return LayerSubsystem && LayerSubsystem->IsEndingPresentationActive();
}

void AFirstPersonPlayerController::ClientShowEnding_Implementation(TSubclassOf<UOutlierEndingWidget> EndingWidgetClass)
{
	ULocalPlayer* Local = GetLocalPlayer();
	UWorld* World = GetWorld();
	if (!IsLocalController() || !Local || !World || World->bIsTearingDown || !EndingWidgetClass) return;
	auto* LayerSubsystem = Local->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	if (!LayerSubsystem || LayerSubsystem->IsEndingPresentationActive()) return;

	// 기존 화면을 정리하기 전에 두 위젯과 뷰포트를 검증한다.
	TStrongObjectPtr<UOutlierEndingWidget> Ending(CreateWidget<UOutlierEndingWidget>(this, EndingWidgetClass));
	if (!Ending.IsValid() || !Ending->HasEndingButton() || !LayerSubsystem->EnsureLayerRoot()) return;
	TStrongObjectPtr<UUILayerKeyHintWidget> Hint;
	if (const auto HintClass = Ending->GetKeyHintWidgetClass())
	{
		Hint.Reset(CreateWidget<UUILayerKeyHintWidget>(this, HintClass));
		if (!Hint.IsValid()) return;
	}

	LayerSubsystem->BeginEndingPresentation(Ending.Get(), Hint.Get());
	if (auto* UI = Local->GetSubsystem<ULocalPlayerUISubSystem>()) UI->BeginEndingPresentation();
	if (auto* Cinematic = Local->GetSubsystem<UOutlierCinematicSubsystem>()) Cinematic->ClearLevelCinematics();
	if (auto* Widgets = World->GetSubsystem<UActorBasedWidgetSubsystem>()) Widgets->DismissRegisteredWidgets();
	if (auto* Visual = World->GetSubsystem<UVisualEventSubsystem>()) Visual->BeginEndingPresentation();
	if (auto* Material = World->GetSubsystem<UMaterialPostProcessSubsystem>()) Material->BeginEndingPresentation();
	for (TActorIterator<APostProcessVolume> It(World); It; ++It) It->bEnabled = false;
	if (auto* PostProcess = Local->GetSubsystem<ULocalPlayerPostProcessSubsystem>()) PostProcess->BeginEndingPresentation();
	if (PlayerCameraManager) PlayerCameraManager->StopAllCameraShakes(true);

	auto* Audio = GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>();
	if (Audio) Audio->BeginEndingPresentation(Ending->GetEndingAudioTag());
	// 현재 PIE 및 게임 월드에 한해 태그 서브시스템 밖의 기존 오디오와 환경음도 정지한다.
	for (TObjectIterator<UAudioComponent> It; It; ++It)
	{
		if (It->GetWorld() == World) It->Stop();
	}
	if (FAudioDeviceHandle Device = World->GetAudioDevice()) Device->Flush(World);

	const FGameplayTag Mode = FirstPersonInputModeTags::UI();
	if (!LayerSubsystem->PushWidget(UILayerTags::Modal(), Ending.Get(), Mode, this).IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[Ending] Failed to attach validated ending widget on %s"), *GetName());
		RequestLeaveGame(false);
		return;
	}
	if (Hint.IsValid())
	{
		LayerSubsystem->PushWidget(UILayerTags::Modal(), Hint.Get(), Mode, this, EUILayerFocusTarget::None, true, false);
		Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (Audio && Ending->GetEndingAudioTag().IsValid())
	{
		FOutlierAudioPlayRequest Request;
		Request.EventTag = Ending->GetEndingAudioType().IsValid() ? Ending->GetEndingAudioType()
			: FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.BGM"));
		Request.ContextTags.AddTag(Ending->GetEndingAudioTag());
		Audio->PlayLocal2D(Request);
	}
}
