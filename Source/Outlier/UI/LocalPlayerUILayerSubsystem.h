#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/UILayerTypes.h"
#include "LocalPlayerUILayerSubsystem.generated.h"

class UMainUIBase;
class UPanelWidget;
class UUILayerRootWidget;
class UUserWidget;
class UWidget;

struct FUILayerEntry
{
	FUILayerHandle Handle;
	FGameplayTag LayerTag;
	FGameplayTag InputModeTag;
	TWeakObjectPtr<UUserWidget> Widget;
	TWeakObjectPtr<UWidget> HitTestBlocker;
	TWeakObjectPtr<UObject> RequestOwner;
	EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::Widget;
	bool bShowCursor = true;
	bool bReceivesInput = true;
};

UCLASS()
class OUTLIER_API ULocalPlayerUILayerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterMainUI(UMainUIBase* MainUI);
	void UnregisterMainUI(UMainUIBase* MainUI);

	bool EnsureLayerRoot();
	void DestroyLayerRoot();

	void RegisterLayerContainer(FGameplayTag LayerTag, UPanelWidget* Container);
	void UnregisterLayerContainer(FGameplayTag LayerTag, UPanelWidget* Container);

	FUILayerHandle PushWidget(const FUILayerPushRequest& Request);

	FUILayerHandle PushWidget(
		FGameplayTag LayerTag,
		UUserWidget* Widget,
		FGameplayTag InputModeTag,
		UObject* RequestOwner,
		EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::Widget,
		bool bShowCursor = true,
		bool bReceivesInput = true);

	FUILayerHandle PushInputScope(
		FGameplayTag LayerTag,
		FGameplayTag InputModeTag,
		UObject* RequestOwner,
		EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::GameViewport,
		bool bShowCursor = false,
		bool bReceivesInput = true);

	FUILayerHandle ReplaceWidget(
		FUILayerHandle PreviousHandle,
		FGameplayTag LayerTag,
		UUserWidget* Widget,
		FGameplayTag InputModeTag,
		UObject* RequestOwner,
		EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::Widget,
		bool bShowCursor = true,
		bool bReceivesInput = true);

	bool PopLayer(FUILayerHandle Handle);
	bool PopWidget(UUserWidget* Widget);
	int32 PopLayersByOwner(UObject* RequestOwner);
	void ClearAllLayers();

	bool RefocusLayer(FUILayerHandle Handle, EUILayerFocusTarget FocusTarget);
	bool IsLayerActive(FGameplayTag LayerTag) const;
	UUserWidget* GetTopLayerWidget() const;
	FGameplayTag GetActiveInputModeTag() const;
	bool RouteWidgetEscapeInput(bool bPopUnhandledInput = true);
	bool RouteWidgetConfirmedInput();
	bool RouteWidgetUpInput();
	bool RouteWidgetDownInput();
	bool RouteWidgetLeftInput();
	bool RouteWidgetRightInput();

private:
	bool AttachWidgetToLayer(
		FGameplayTag LayerTag,
		UUserWidget* Widget,
		EUILayerFocusTarget FocusTarget,
		int32 ZOrder,
		UWidget*& OutHitTestBlocker);
	void RemoveInvalidLayers();
	const FUILayerEntry* FindTopInputLayer() const;
	FUILayerEntry* FindLayer(FUILayerHandle Handle);
	int32 GetLayerPriority(FGameplayTag LayerTag) const;
	bool RouteWidgetInput(
		bool (*InputExecutor)(UObject*),
		bool bPopUnhandledInput);
	void ClearAllLayersInternal(bool bRestoreDefaultInput);

	void RefreshTopLayerInput();
	void ApplyLayerInput(const FUILayerEntry& Layer);
	void ApplyDefaultInput();

	APlayerController* GetLocalPlayerController() const;

	UPROPERTY(Transient)
	TObjectPtr<UUILayerRootWidget> LayerRootWidget;

	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UPanelWidget>> LayerContainers;

	TWeakObjectPtr<UMainUIBase> RegisteredMainUI;
	TArray<FUILayerEntry> LayerEntries;
	FUILayerHandle CachedTopLayerHandle;
	TWeakObjectPtr<UUserWidget> CachedInputWidget;
	FGameplayTag CachedInputModeTag;
	int32 NextLayerId = 1;
	bool bIsDeinitializing = false;
	bool bIsClearingLayers = false;
};
