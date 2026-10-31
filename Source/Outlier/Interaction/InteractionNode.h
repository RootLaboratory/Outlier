#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "Engine/EngineTypes.h"
#include "Curves/CurveFloat.h"
#include "InteractionNode.generated.h"

class AFirstPersonCharacter;
class UInteractableComponent;
class USceneComponent;
class UMaterialInstanceDynamic;
class UMeshComponent;
class AGameStateBase;

USTRUCT()
struct FInteractionNodeHoldState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Revision = 0;
	UPROPERTY()
	int32 AttemptId = 0;
	UPROPERTY()
	TObjectPtr<AFirstPersonCharacter> Interactor;
	UPROPERTY()
	bool bHolding = false;
	UPROPERTY()
	double ServerStartTime = 0.0;
	UPROPERTY()
	float Duration = 0.0f;

	float GetProgress(double ServerTime) const;
};

UCLASS()
class OUTLIER_API AInteractionNode : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractionNode();

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Node")
	void OnCollectedStateChanged(bool bCollectedState);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

private:
	friend class UInteractableComponent;
	friend class FInteractionNodePresentationTest;

	void BeginHoldPresentation(AFirstPersonCharacter* Interactor, int32 AttemptId, float Duration);
	void EndHoldPresentation(bool bCanceled);
	float GetHoldProgress() const;
	float GetEmissiveStrength(float Progress) const;
	bool NeedsPresentationUpdate() const;
	void UpdateHoldPresentation();
	void InitializeEmissiveMaterial();
	double GetPresentationTime() const;
	void PublishHoldState(bool bHolding, int32 AttemptId = 0, float Duration = 0.0f,
		AFirstPersonCharacter* Interactor = nullptr);

	UFUNCTION()
	void OnRep_HoldState();

	UPROPERTY(ReplicatedUsing = OnRep_HoldState)
	FInteractionNodeHoldState HoldState;

	// 미지정 시 초기화 때 지정 슬롯에 발광 파라미터가 있는 유일한 메시를 찾는다.
	UPROPERTY(EditAnywhere, Category = "Node|Emissive", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.MeshComponent"))
	FComponentReference EmissiveMesh;
	UPROPERTY(EditAnywhere, Category = "Node|Emissive", meta = (ClampMin = "0"))
	int32 EmissiveMaterialIndex = 0;
	UPROPERTY(EditAnywhere, Category = "Node|Emissive")
	FName EmissiveParameterName = TEXT("EmissiveStrength");
	UPROPERTY(EditAnywhere, Category = "Node|Emissive")
	bool bUseMaterialIdleStrength = true;
	UPROPERTY(EditAnywhere, Category = "Node|Emissive", meta = (ClampMin = "0.0", EditCondition = "!bUseMaterialIdleStrength"))
	float IdleEmissiveStrength = 0.0f;
	// X는 진행률(0~1), Y는 기본 밝기에 곱할 비율(0~1)이다. 양 끝은 대기 1 / 완료 0으로 고정한다.
	UPROPERTY(EditAnywhere, Category = "Node|Emissive")
	FRuntimeFloatCurve EmissiveFadeCurve;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EmissiveMID;
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedEmissiveMesh;
	UPROPERTY(Transient)
	TObjectPtr<AGameStateBase> PresentationClock;

	int32 LocalAttemptId = 0;
	int32 CanceledAttemptId = 0;
	TWeakObjectPtr<AFirstPersonCharacter> LocalInteractor;
	TWeakObjectPtr<AFirstPersonCharacter> CanceledInteractor;
	float ResolvedIdleStrength = 0.0f;
	bool bPresentationInitialized = false;
	bool bPresentationUpdating = false;

	bool AddNodeServer(AFirstPersonCharacter* Interactor);
	bool bProgressIdRegistered = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Node", meta = (AllowPrivateAccess = "true"))
	FName PickupId = NAME_None;

	UPROPERTY(ReplicatedUsing = OnRep_Collected, VisibleInstanceOnly, BlueprintReadOnly, Category = "Node", meta = (AllowPrivateAccess = "true"))
	bool bCollected = false;

	UFUNCTION()
	void OnRep_Collected();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 NodeRewardAmount = 4;
};
