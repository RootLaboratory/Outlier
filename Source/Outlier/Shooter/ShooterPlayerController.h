// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/ShooterAbilityUI.h"
#include "ShooterPlayerController.generated.h"



class AShooterCharacter;
class ULocalPlayerUISubSystem;
class UOutlierAbilitySystemComponent;
struct FGameplayTag;
struct FOnAttributeChangeData;
/**
 * Basic player controller class for shooter gameplay.
 * Manages possession and respawn behavior.
 */
UCLASS(abstract)
class OUTLIER_API AShooterPlayerController : public AFirstPersonPlayerController 
{
	GENERATED_BODY()

protected:
	/** Pawn class used when respawning the player. */
	UPROPERTY(EditAnywhere, Category = "Shooter|Respawn")
	TSubclassOf<AShooterCharacter> CharacterClass;

	/** Tag applied to the possessed player pawn. */
	UPROPERTY(EditAnywhere, Category = "Shooter|Player")
	FName PlayerPawnTag = FName("Player");

	/** Gameplay initialization */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;
	virtual void ReceivedPlayer() override;
	virtual void AcknowledgePossession(APawn* P) override;
	void BindShooterCharacterDelegates(AShooterCharacter* ShooterCharacter);
	void UnbindShooterCharacterDelegates();
	void RefreshShooterVitalityUI();
	void HandleShooterHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void HandleShooterShieldAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void HandleShooterPartnerShieldAttributeChanged(const FOnAttributeChangeData& ChangeData);

	//UI
	ULocalPlayerUISubSystem* GetLocalUISubsystem() const;
	void HandleShooterHealthChanged(float CurrentHealth, float MaxHealth);
	void HandleShooterShieldChanged(float CurrentShield, float MaxShield);
	void HandleShooterPartnerShieldChanged(float CurrentPartnerShield, float MaxPartnerShield);
	void HandleShooterConditionChanged(const FGameplayTag& ConditionTag);
	void HandleShooterDynamicCrosshair(bool InFlag);

	void CleanupPossessedShooterWeapons();

	virtual void BindMainUI() override;

	virtual void BindPostProcessSubSystem() override;

	UFUNCTION()
	void HandleMovementStateChanged(EMovementState NewState);

	UFUNCTION()
	void OnWeaponChanged(EWeaponType NewType);

	UFUNCTION()
	void HandleAbilitySelected(FGameplayTag AbilityTag);

	UFUNCTION()
	void  HandleShooterAimingBlur(bool InFlag, int32 WeaponStencilValue);

	// UI 관련
	// 총알
	// 피격 등등

public:
	AShooterPlayerController();

	void SocketDistanceUpdate(float Distance); //테스팅.

	UPROPERTY()
	TObjectPtr<UShooterAbilityUI>  AbilityUIInstance;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> BoundShooterCharacter;

	UPROPERTY()
	TObjectPtr<UOutlierAbilitySystemComponent> BoundShooterAbilitySystem;

	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;
	FDelegateHandle ShieldChangedHandle;
	FDelegateHandle MaxShieldChangedHandle;
	FDelegateHandle PartnerShieldChangedHandle;
	FDelegateHandle MaxPartnerShieldChangedHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability UI")
	TSubclassOf<UShooterAbilityUI> AbilityUIClass;

};
