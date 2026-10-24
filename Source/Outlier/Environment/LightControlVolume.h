#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "LightControlVolume.generated.h"

class ULightComponent;

USTRUCT()
struct FLightControlCachedState
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<ULightComponent> Light;

	float OriginalIntensity = 0.0f;
	FLinearColor OriginalColor = FLinearColor::White;
};

// Sub-level에 배치해서 Persistent Level(및 로드된 다른 레벨)의 Light 중 Volume 안에 있는 것을 캐싱하고,
// bLightsTriggered가 켜지면 TargetIntensity / TargetColor를 적용, 꺼지면 원래 값으로 되돌린다.
// Light는 Movable 또는 Stationary여야 런타임 변경이 반영된다. Static Light는 캐싱 시 경고 후 제외한다.
UCLASS(Blueprintable)
class OUTLIER_API ALightControlVolume : public AVolume
{
	GENERATED_BODY()

public:
	ALightControlVolume();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Light Control")
	void SetLightsTriggered(bool bTriggered);

	UFUNCTION(BlueprintPure, Category = "Light Control")
	bool IsLightsTriggered() const { return bLightsTriggered; }

	// 이후에 스트리밍된 레벨의 Light까지 다시 잡고 싶을 때 호출. 기존 Light는 원래 값으로 복원 후 재캐싱한다.
	UFUNCTION(BlueprintCallable, Category = "Light Control")
	void RefreshCachedLights();

	// Target 값을 런타임에 바꾼 뒤 즉시 반영하고 싶을 때 호출.
	UFUNCTION(BlueprintCallable, Category = "Light Control")
	void ApplyLightState();

	UFUNCTION(BlueprintPure, Category = "Light Control")
	TArray<ULightComponent*> GetCachedLights() const;

#if WITH_EDITOR
	// 에디터 미리보기: 볼륨 안 라이트에 Target 값을 임시로 적용한다. 저장/PIE 시작 전에 자동으로 복원된다.
	UFUNCTION(CallInEditor, Category = "Light Control|Preview")
	void PreviewApplyLights();

	UFUNCTION(CallInEditor, Category = "Light Control|Preview")
	void PreviewResetLights();
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void Destroyed() override;
#endif

#if WITH_EDITORONLY_DATA
	// 콘텐츠 브라우저에서 드래그해 Brush가 비어 있을 때 자동으로 만들 Box 크기(전체 길이).
	// 배치 후 크기는 레벨 인스턴스의 Brush Settings 또는 Scale로 조절한다. BP Class Defaults의 Brush Settings는 건드리지 말 것(엔진 크래시).
	UPROPERTY(EditDefaultsOnly, Category = "Light Control|Editor")
	FVector DefaultBrushSize = FVector(400.0f, 400.0f, 300.0f);
#endif

	UFUNCTION(BlueprintImplementableEvent, Category = "Light Control")
	void OnLightsTriggerChanged(bool bTriggered);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Control")
	uint8 bApplyIntensity : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light Control", meta = (ClampMin = "0.0", EditCondition = "bApplyIntensity"))
	float TargetIntensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Control")
	uint8 bApplyColor : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light Control", meta = (EditCondition = "bApplyColor"))
	FLinearColor TargetColor = FLinearColor::Red;

	// false면 Persistent Level의 Light만, true면 현재 로드된 모든 Level의 Light를 검색한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Control")
	uint8 bSearchAllLoadedLevels : 1 = false;

	// Light 위치가 Volume 경계에서 이 거리 안이면 포함한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Control", meta = (ClampMin = "0.0"))
	float BoundsTolerance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_LightsTriggered, Category = "Light Control")
	uint8 bLightsTriggered : 1 = false;

private:
	UFUNCTION()
	void OnRep_LightsTriggered();

	void CacheLights();
	void RestoreOriginalLights();
	void CollectLights(TArray<FLightControlCachedState>& OutLights) const;
	void ApplyTargetToLights(const TArray<FLightControlCachedState>& Lights) const;
	static void RestoreLights(const TArray<FLightControlCachedState>& Lights);
#if WITH_EDITOR
	bool HasBrushGeometry() const;
	void CreateDefaultBoxBrush();
#endif

	UPROPERTY(Transient)
	TArray<FLightControlCachedState> CachedLights;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TArray<FLightControlCachedState> PreviewLights;

	bool bPreviewActive = false;
	FDelegateHandle PreSaveHandle;
	FDelegateHandle PreBeginPIEHandle;
#endif
};
