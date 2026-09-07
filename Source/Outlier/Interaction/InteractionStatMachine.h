#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/HackableInterface.h"
#include "Interface/InteractableInterface.h"
#include "Interface/RoomTagInterface.h"
#include "InteractionStatMachine.generated.h"

class AFirstPersonCharacter;
class AFirstPersonPlayerController;
class AOutlierPlayerState;
class APartnerCharacter;
class AShooterCharacter;
class UHackableComponent;
class UInteractableComponent;
class URoomTagComponent;
class USceneComponent;
class UStatAllocatorWidget;

UCLASS(Blueprintable)
class OUTLIER_API AInteractionStatMachine : public AActor,
	public IInteractableInterface,
	public IHackableInterface,
	public IRoomTagInterface
{
	GENERATED_BODY()

public:
	AInteractionStatMachine();

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;

	virtual FGameplayTag GetCurrentRoomTag() const override;
	virtual FGameplayTag GetDefaultRoomTag() const override;
	virtual URoomTagComponent* GetRoomTagComp() const override;

	UFUNCTION(BlueprintPure, Category = "Stat Machine")
	bool IsInteractionBlocked() const;

protected:
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<URoomTagComponent> RoomTagComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stat Machine|UI")
	TSubclassOf<UStatAllocatorWidget> StatAllocatorWidgetClass;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AOutlierPlayerState> CachedHackingPartnerPlayerState;

	int32 CachedHackingPairId = INDEX_NONE;

	void ResolvePairCharacters(
		AFirstPersonCharacter* Interactor,
		AShooterCharacter*& OutShooterCharacter,
		APartnerCharacter*& OutPartnerCharacter) const;

	void ApplyUnblockEffect(const FHackResultContext& Context);

};
