#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "GameplayTagContainer.h"
#include "OutlierAmbientVolume.generated.h"

class AController;
class APawn;

/**
 * 로컬 플레이어 Pawn 이 안에 있는 동안 Audio Bank 의 Loop 사운드를 재생하는 영역.
 * 사운드 에셋 / VolumeType / VolumeMultiplier 는 Bank Definition 이 결정하고, 여기선 Context 태그만 지정한다.
 * 어느 Bank 인지는 Context 로 찾는다. BGM Bank 면 서브시스템의 Zone BGM 채널( 한 곡만 유지, 돌아오면 이어서 키움 )로,
 * 그 외 Bank 는 로컬 Emitter 루프로 재생한다. 재생은 로컬 전용( 서버 경유 없음 )이다.
 */
UCLASS(Blueprintable)
class OUTLIER_API AOutlierAmbientVolume : public AVolume
{
	GENERATED_BODY()

public:
	AOutlierAmbientVolume();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;

	// Bank 에서 PlaybackPolicy 가 Loop 인 Variant 를 가리켜야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag ContextTag;

	// BGM Bank 가 아닐 때만 쓰인다. false 면 Volume 위치에서 3D 로 재생한다( 감쇠는 사운드 에셋 설정을 따른다 ).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient Audio")
	bool bTwoD = true;

	// BGM 채널에서만 쓰인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient Audio", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient Audio", meta = (ClampMin = "0.0", Units = "s"))
	float FadeOutDuration = 1.5f;

#if WITH_EDITORONLY_DATA
	// 콘텐츠 브라우저에서 BP 를 드래그해 Brush 가 비어 있을 때 자동으로 만들 Box 크기( 전체 길이 ).
	UPROPERTY(EditDefaultsOnly, Category = "Ambient Audio|Editor")
	FVector DefaultBrushSize = FVector(400.0f, 400.0f, 300.0f);
#endif

private:
	UFUNCTION()
	void HandlePawnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	void TrackPawn(APawn* Pawn);
	void UntrackPawn(APawn* Pawn);
	void RefreshPlayback();
	void SetPlaying(bool bNewPlaying);
	void ApplyPlayback();
#if WITH_EDITOR
	bool HasBrushGeometry() const;
	void CreateDefaultBoxBrush();
#endif

	TArray<TWeakObjectPtr<APawn>> OverlappingPawns;
	bool bPlaying = false;
	// ContextTag 가 속한 Bank 의 TypeTag. 처음 재생할 때 찾아 둔다.
	FGameplayTag ResolvedType;
	FTimerHandle ResolveRetryTimer;
};
