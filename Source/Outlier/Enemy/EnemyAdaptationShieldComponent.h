#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Enemy/EnemyAdaptationTypes.h"
#include "EnemyAdaptationShieldComponent.generated.h"

class UMaterialInstanceDynamic;
class UNiagaraSystem;

// Enemy 내성 단계를 보여주는 표현 전용 방어막이다. 충돌/피해 판정은 하지 않는다.
// 서버가 복제한 AdaptationState로 표시 여부를 정하고, 피격 위치를 머티리얼 Hit 슬롯 2개에 번갈아 기록한다.
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UEnemyAdaptationShieldComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UEnemyAdaptationShieldComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void ApplyAdaptationState(EEnemyAdaptationState NewState);
	void NotifyShieldHit(const FVector& WorldImpactLocation);

	// 내성 파괴 연출. 보이던 방어막 자리에서 BreakVFX를 재생하고 와이프 없이 즉시 숨긴다.
	void PlayBreak(UNiagaraSystem* BreakVFX);

	UFUNCTION(BlueprintPure, Category = "Enemy|Adaptation|Shield")
	bool ShouldShowForState(EEnemyAdaptationState State) const;

	UFUNCTION(BlueprintPure, Category = "Enemy|Adaptation|Shield")
	bool IsShieldActive() const { return bShieldActive; }

	// StateColors에 없는 단계는 FallbackColor를 쓴다.
	UFUNCTION(BlueprintPure, Category = "Enemy|Adaptation|Shield")
	FLinearColor GetColorForState(EEnemyAdaptationState State) const;

	// 몸체 안쪽 피격점은 구형 마스크에 닿지 않으므로 방어막 표면으로 옮긴다.
	static FVector ProjectToShieldSurface(const FVector& Center, float Radius, const FVector& Point);
	static float ComputeHitFade(float Elapsed, float Duration);
	// 머티리얼 와이프는 OneMinus(Clamp(LocalZ / 64 + Opacity))다. Alpha 0이면 메시 전체가 사라지고
	// 1이면 전체가 보이도록 메시 로컬 높이 범위에서 Opacity 값을 고른다.
	static float ComputeWipeOpacity(float LocalMinZ, float LocalMaxZ, float Alpha);

	static constexpr int32 HitSlotCount = 2;

protected:
	virtual void OnRegister() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield")
	TObjectPtr<UMaterialInterface> ShieldMaterial;

	// 이 단계 이상일 때만 방어막을 보인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield")
	EEnemyAdaptationState MinVisibleState = EEnemyAdaptationState::ShieldPreview;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield", meta = (ClampMin = "1.0", Units = "cm"))
	float ShieldRadius = 120.0f;

	// 내성 단계별 방어막 색(머티리얼 Base_Color). 값이 클수록 밝게 빛난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Color")
	TMap<EEnemyAdaptationState, FLinearColor> StateColors = {
		{ EEnemyAdaptationState::ShieldPreview, FLinearColor(1.0f, 3.0f, 15.0f, 1.0f) },
		{ EEnemyAdaptationState::ResistanceLevel1, FLinearColor(1.0f, 3.0f, 15.0f, 1.0f) },
		{ EEnemyAdaptationState::ResistanceLevel2, FLinearColor(15.0f, 10.0f, 1.0f, 1.0f) },
		{ EEnemyAdaptationState::ResistanceMax, FLinearColor(15.0f, 1.5f, 1.0f, 1.0f) },
	};

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Color")
	FLinearColor FallbackColor = FLinearColor(1.0f, 3.0f, 15.0f, 1.0f);

	// 보이는 중에 단계가 바뀌면 새 색이 아래에서 위로 차오르는 시간이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Color", meta = (ClampMin = "0.0", Units = "s"))
	float ColorFillDuration = 2.0f;

	// 머티리얼 Color_Fill_Softness. 경계선 두께(메시 높이 대비 비율)다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Color", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float ColorFillSoftness = 0.08f;

	// 머티리얼 Strength_Fill_Edge. 차오르는 경계선이 추가로 밝아지는 정도다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Color", meta = (ClampMin = "0.0"))
	float ColorFillEdgeStrength = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Look", meta = (ClampMin = "0.0"))
	float EdgeStrength = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Look", meta = (ClampMin = "0.0"))
	float VertexNoiseStrength = 16.0f;

	// 머티리얼 Strength_Emissive. 피격 글로우에만 곱해진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Hit", meta = (ClampMin = "0.0"))
	float HitEmissiveStrength = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Hit", meta = (ClampMin = "0.0", Units = "cm"))
	float HitEmissiveRadius = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Hit", meta = (ClampMin = "0.0"))
	float HitNoiseStrength = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Hit", meta = (ClampMin = "0.0", Units = "cm"))
	float HitNoiseRadius = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Hit", meta = (ClampMin = "0.01", Units = "s"))
	float HitFadeDuration = 0.6f;

	// 방어막이 바닥부터 꼭대기까지 스며들듯 나타나는 시간이다. 사라질 때는 같은 시간으로 위부터 걷힌다.
	// 머티리얼 Opacity 와이프 범위는 메시 로컬 바운드에서 계산하므로 메시 크기와 무관하게 전체 시간을 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Adaptation|Shield|Appear", meta = (ClampMin = "0.0", Units = "s"))
	float AppearDuration = 2.0f;

private:
	struct FHitSlot
	{
		FVector LocalLocation = FVector::ZeroVector;
		float Elapsed = 0.0f;
		bool bActive = false;
	};

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShieldMID;

	TArray<FHitSlot> HitSlots;
	int32 NextHitSlot = 0;
	// 0은 완전 소멸, 1은 완전 표시다.
	float AppearAlpha = 0.0f;
	bool bShieldActive = false;

	// 0이면 전부 PreviousColor, 1이면 전부 CurrentColor다.
	float ColorFill = 1.0f;
	FLinearColor PreviousColor = FLinearColor::Black;
	FLinearColor CurrentColor = FLinearColor::Black;
	EEnemyAdaptationState DisplayedState = EEnemyAdaptationState::Normal;

	bool IsPresentationEnabled() const;
	void EnsureDynamicMaterial();
	void ApplyShieldRadius();
	void ApplyStaticParameters();
	void ApplyAppearParameter();
	void ApplyColorParameters();
	void ApplyHitSlotParameters(int32 SlotIndex);
	void ClearHitSlots();
	void RefreshTickState();
};
