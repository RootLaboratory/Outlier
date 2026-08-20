// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Audio/OutlierAudioTypes.h"
#include "OutlierPlayerState.h"
#include "PlayerUIProvider.h"
#include "UI/UILayerTypes.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "FirstPersonPlayerController.generated.h"

class UInputMappingContext;
class UCameraShakeBase;
class UOutlierUpgradeSetData;

namespace FirstPersonInputModeTags
{
	inline FGameplayTag EMP()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Input.Mode.EMP")));
		return Tag;
	}

	inline FGameplayTag Hack()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Input.Mode.Hack")));
		return Tag;
	}

	inline FGameplayTag UI()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Input.Mode.UI")));
		return Tag;
	}
}

/**
 * 
 */
UCLASS()
class OUTLIER_API AFirstPersonPlayerController : public APlayerController ,  public IPlayerUIProvider
{
	GENERATED_BODY()
	
public:
	AFirstPersonPlayerController();

	/** Client-owned transport for relevant AtLocation audio requests. */
	UFUNCTION(Server, Unreliable)
	void ServerRequestRelevantAudioAtLocation(const FOutlierAudioPlayRequest& Request);

	/** Delivers an already resolved Owner/Relevant sound to this client. */
	UFUNCTION(Client, Unreliable)
	void ClientPlayResolvedAudio(const FOutlierResolvedAudioPlay& ResolvedPlay);

	UFUNCTION(Client, Reliable)
	void ClientArenaLoad(int32 ArenaId);

	UFUNCTION(Client, Reliable)
	void ClientPushUILayer(const FUILayerPushRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerTryActivateUpgradeNode(
		AActor* UpgradeOwner,
		FName NodeIdOrRowName,
		UOutlierUpgradeSetData* UpgradeSetData);

	UFUNCTION(Server, Reliable)
	void ServerNotifyArenaReady();

	// 서버에서 계산한 폭발 충격을 소유 클라이언트의 CameraManager에 전달한다.
	UFUNCTION(Client, Unreliable)
	void ClientPlayExplosionCameraShake(
		TSubclassOf<UCameraShakeBase> CameraShakeClass,
		float Scale,
		bool bAllowInactivePawn);

	// 디버그: 요청한 페어의 arena를 서버 권위로 리로드
	UFUNCTION(Server, Reliable)
	void Server_RequestArenaReload();

	// 디버그: 클라 arena 스트리밍 인스턴스를 강제 언로드 후 재로드
	UFUNCTION(Client, Reliable)
	void ClientArenaReload(int32 ArenaId);

	UFUNCTION()
	void HandleArenaShown(int32 ShownArenaId);
	void ControlMainWidget(bool InFlag) const;

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool SetFirstPersonInputMode(FGameplayTag NewInputMode);

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool TryRestoreFirstPersonDefaultInputMode(FGameplayTag ExpectedInputMode);

	UFUNCTION(BlueprintCallable, Category = "Input|Input Mode")
	bool RestoreFirstPersonDefaultInputMode();

	UFUNCTION(BlueprintPure, Category = "Input|Input Mode")
	FGameplayTag GetFirstPersonInputMode()const; 

	UFUNCTION(BlueprintPure, Category = "Input|Input Mode")
	bool IsFirstPersonInputMode(FGameplayTag InputMode) const;
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair")
	EOutlierPlayerRole DefaultPlayerRole = EOutlierPlayerRole::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair")
	int32 DefaultPairId = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Input|Input Mode")
	FGameplayTag CurrentFirstPersonInputMode;

	virtual void BeginPlay() override;

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnRep_PlayerState() override;

	// Input Mapping Context Setup
	virtual void SetupInputComponent() override;

	virtual TSubclassOf<UMainUIBase> GetMainUIClass_Implementation() const;

	virtual void BindMainUI();
	virtual void BindPostProcessSubSystem();

	void InitializeOutlierPlayerState();
	void RegisterCurrentPawnWithPlayerState();
	virtual void RefreshPostProcessState();
	

protected:

	int32 PendingArenaId = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UMainUIBase> ShooterUIInstance;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UMainUIBase> MainUIClass;
};
