#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractableSwitch.h"
#include "Interface/HackableInterface.h"
#include "InteractableLevelSwitch.generated.h"

class UHackableComponent;
class UMaterialInstanceDynamic;

/**
 * InteractableSwitch that stays locked until it is hacked (same gating pattern as
 * AInteractionStatMachine: a State.Locked tag on InteractableComponent that a
 * successful Hack.Effect.Unblock removes). Door-toggle flow itself is inherited as-is.
 * Additionally drives a Color / Hacked(float) parameter on the switch mesh's material.
 */
UCLASS()
class OUTLIER_API AInteractableLevelSwitch : public AInteractableSwitch, public IHackableInterface
{
	GENERATED_BODY()

public:
	AInteractableLevelSwitch();

	virtual bool Interact(AFirstPersonCharacter* Interactor) override;

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;

	UFUNCTION(BlueprintPure, Category = "Level Switch")
	bool IsInteractionBlocked() const;

	UFUNCTION(BlueprintPure, Category = "Level Switch")
	bool IsHacked() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UHackableComponent> HackableComponent;

	/** Material slot on SwitchMesh that the Color / Hacked parameters live on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Switch|Material")
	int32 SwitchMaterialSlot = 0;

	/** Scalar (float) parameter that flags whether the switch has been hacked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Switch|Material")
	FName HackedScalarParamName = TEXT("Hacked");

	/** Vector/color parameter on the switch mesh's material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Switch|Material")
	FName SwitchColorParamName = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Switch|Material")
	FLinearColor LockedColor = FLinearColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Switch|Material")
	FLinearColor HackedColor = FLinearColor::Green;

	/**
	 * Fires whenever the hacked visual state is (re)applied - BeginPlay sync and on hack
	 * success - on every machine. Left as an extra hook in case material parameter access
	 * ends up moving to BP instead of the C++ MID push below.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Level Switch|Material")
	void OnHackedStateChanged(bool bHacked);

private:
	UFUNCTION()
	void HandleCheckpointHackStateRestored(bool bHacked);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SwitchMID;

	void CacheSwitchMaterial();
	void ApplyMaterialState(bool bHacked);
};
