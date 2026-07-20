#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "InteractionDescActor.generated.h"

class AFirstPersonCharacter;
class AInteractionNode;
class APlayerController;
class UBoxComponent;
class UInteractableComponent;
class UInteractionDescWidget;
class USceneComponent;
class UWidgetComponent;

UCLASS()
class OUTLIER_API AInteractionDescActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractionDescActor();

	virtual void Tick(float DeltaSeconds) override;

	void SetSourceInteractionNode(AInteractionNode* SourceNode);

	UFUNCTION(BlueprintCallable, Category = "Interaction|Desc")
	void ActivateDesc(AFirstPersonCharacter* Interactor);

	void ActivateDescFromSource(AFirstPersonCharacter* Interactor, const UInteractableComponent* SourceInteractableComponent);

	UFUNCTION(BlueprintCallable, Category = "Interaction|Desc")
	void DeactivateDesc();

	UFUNCTION(BlueprintCallable, Category = "Interaction|Desc")
	void SetProgress(float InProgress);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UBoxComponent> TraceCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UWidgetComponent> DescWidgetComponent;

private:
	void BillboardToCamera();
	bool GetPrimaryInteractTag(const UInteractableComponent* SourceInteractableComponent, FGameplayTag& OutInteractTag) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UInteractionDescWidget> InteractionDescWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	FVector2D InteractionDescWidgetDrawSize = FVector2D(150, 450.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	FVector InteractionDescWidgetScale = FVector(0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Trace", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	FVector TraceCollisionExtent = FVector(80.0f, 20.0f, 80.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Trace", meta = (AllowPrivateAccess = "true"))
	bool bDrawTraceCollisionDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float Progress = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UInteractionDescWidget> InteractionDescWidget;

	UPROPERTY(Transient)
	TWeakObjectPtr<AInteractionNode> SourceInteractionNode;
};
