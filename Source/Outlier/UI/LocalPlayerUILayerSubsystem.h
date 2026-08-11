#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/UILayerTypes.h"
#include "LocalPlayerUILayerSubsystem.generated.h"

class AFirstPersonPlayerController;
class UMainUIBase;
class UPanelWidget;
class UUILayerRootWidget;
class UUserWidget;

struct FUILayerEntry
{
	FUILayerHandle Handle;
	FGameplayTag LayerTag;
	FGameplayTag InputModeTag;
	TWeakObjectPtr<UUserWidget> Widget;
	TWeakObjectPtr<UObject> RequestOwner;
	EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::Widget;
	bool bShowCursor = true;
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
		bool bShowCursor = true);

	FUILayerHandle PushInputScope(
		FGameplayTag LayerTag,
		FGameplayTag InputModeTag,
		UObject* RequestOwner,
		EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::GameViewport,
		bool bShowCursor = false);

	FUILayerHandle ReplaceWidget(
		FUILayerHandle PreviousHandle,
		FGameplayTag LayerTag,
		UUserWidget* Widget,
		FGameplayTag InputModeTag,
		UObject* RequestOwner,
		EUILayerFocusTarget FocusTarget = EUILayerFocusTarget::Widget,
		bool bShowCursor = true);

	bool PopLayer(FUILayerHandle Handle);
	int32 PopLayersByOwner(UObject* RequestOwner);
	void ClearAllLayers();

	bool RefocusLayer(FUILayerHandle Handle, EUILayerFocusTarget FocusTarget);
	bool IsLayerActive(FGameplayTag LayerTag) const;
	UUserWidget* GetTopLayerWidget() const;
	FGameplayTag GetActiveInputModeTag() const;
	bool RouteWidgetEscapeInput();
	bool RouteWidgetConfirmedInput();

private:
	bool AttachWidgetToLayer(FGameplayTag LayerTag, UUserWidget* Widget, int32 ZOrder);
	void RemoveInvalidLayers();
	const FUILayerEntry* FindTopInputLayer() const;
	FUILayerEntry* FindLayer(FUILayerHandle Handle);
	int32 GetLayerPriority(FGameplayTag LayerTag) const;

	void RefreshTopLayerInput();
	void ApplyLayerInput(const FUILayerEntry& Layer);
	void ApplyDefaultInput();

	AFirstPersonPlayerController* GetLocalFirstPersonController() const;

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
};
