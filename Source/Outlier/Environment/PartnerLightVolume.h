#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "Interface/MeleeIgnoreInterface.h"
#include "PartnerLightVolume.generated.h"

class APartnerCharacter;

// Partner spotlights are visible while the Partner overlaps at least one light volume.
UCLASS(Blueprintable)
class OUTLIER_API APartnerLightVolume : public AVolume, public IMeleeIgnoreInterface
{
	GENERATED_BODY()

public:
	APartnerLightVolume();
	static void RefreshPartnerLights(APartnerCharacter* Partner);
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITORONLY_DATA
	// Initial box size when dragging a Blueprint into the level without a volume factory.
	UPROPERTY(EditDefaultsOnly, Category = "Partner Light|Editor")
	FVector DefaultBrushSize = FVector(400.0f, 400.0f, 300.0f);
#endif

private:
	void RefreshInitialOverlaps();
#if WITH_EDITOR
	bool HasBrushGeometry() const;
	void CreateDefaultBoxBrush();
#endif

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	bool bEndingPlay = false;
};
