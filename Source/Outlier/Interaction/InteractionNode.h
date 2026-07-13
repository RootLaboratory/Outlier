#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Interface/InteractableInterface.h"
#include "InteractionNode.generated.h"

class AFirstPersonCharacter;
class APlayerController;
class UInteractableComponent;
class UInteractInfoWidget;
class USceneComponent;
class UWidgetComponent;

UCLASS()
class OUTLIER_API AInteractionNode : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractionNode();

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual void Interact(AFirstPersonCharacter* Interactor) override;

	void InteractInfoWidgetActivate(AFirstPersonCharacter* Interactor);
	void InteractInfoWidgetDeactivate();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

private:
	UWidgetComponent* EnsureInteractInfoWidgetComponent(APlayerController* PlayerController);
	bool GetPrimaryInteractTag(FGameplayTag& OutInteractTag) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractInfoWidget> InteractInfoWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	float InteractInfoWidgetZOffset = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	FVector2D InteractInfoWidgetDrawSize = FVector2D(1500.0f, 400.0f);

	UPROPERTY(Transient)
	TObjectPtr<UInteractInfoWidget> InteractInfoWidget;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> InteractInfoWidgetComponent;
};
