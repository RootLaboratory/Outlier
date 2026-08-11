#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "InteractionNode.generated.h"

class AFirstPersonCharacter;
class UInteractableComponent;
class USceneComponent;

UCLASS()
class OUTLIER_API AInteractionNode : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractionNode();

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

private:
	bool AddNodeServer(AFirstPersonCharacter* Interactor);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 NodeRewardAmount = 4;
};
