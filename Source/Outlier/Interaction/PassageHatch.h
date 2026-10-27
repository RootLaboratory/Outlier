#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PassageHatch.generated.h"

class UStaticMeshComponent;

USTRUCT()
struct FPassageHatchMotion
{
	GENERATED_BODY()
	UPROPERTY() bool bClosed = false;
	UPROPERTY() double StartServerTime = 0.0;
	UPROPERTY() float Duration = 0.0f;
};

/** Placed mesh transforms are the open pose. Offsets are local transforms from that pose. */
UCLASS()
class OUTLIER_API APassageHatch : public AActor
{
	GENERATED_BODY()
public:
	APassageHatch();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Passage|Hatch")
	void CloseHatch();
	UFUNCTION(BlueprintPure, Category = "Passage|Hatch")
	bool IsClosed() const { return Motion.bClosed; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage|Hatch")
	TObjectPtr<UStaticMeshComponent> LeftHatchMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage|Hatch")
	TObjectPtr<UStaticMeshComponent> RightHatchMesh;
	/** Unique across all persistent gameplay objects, as with DoorId. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Passage|Hatch")
	FName HatchId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage|Hatch")
	FTransform LeftCloseOffset = FTransform(FVector(150.0f, 0.0f, 0.0f));
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage|Hatch")
	FTransform RightCloseOffset = FTransform(FVector(-150.0f, 0.0f, 0.0f));
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage|Hatch", meta = (ClampMin = "0", Units = "s"))
	float CloseDuration = 0.75f;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UPROPERTY(ReplicatedUsing = OnRep_Motion)
	FPassageHatchMotion Motion;
	UFUNCTION() void OnRep_Motion();
	void ApplyMotion();
	double GetMotionTime() const;
	FTransform OpenLeft;
	FTransform OpenRight;
	bool bInitialized = false;
	bool bProgressIdRegistered = false;
};
