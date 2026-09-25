#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/HackableInterface.h"
#include "MagneticGeneratorTower.generated.h"

class UHackableComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
class AEnemyBase;

/** A hackable tower that attracts enemy actors inside a runtime sphere volume. */
UCLASS()
class OUTLIER_API AMagneticGeneratorTower : public AActor, public IHackableInterface
{
	GENERATED_BODY()

public:
	AMagneticGeneratorTower();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;

	UFUNCTION(BlueprintCallable, Category = "Magnetic Attraction")
	void SetSphereRadius(float NewRadius);

protected:
	/** The generator tower's physical/root mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RootMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SphereVisual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> SphereCollision;

	/** Duration of one attraction pulse in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction", meta = (ClampMin = "0.0", Units = "s"))
	float Duration = 5.0f;

	/** Radius is in Unreal centimeters (10 m = 1000 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnetic Attraction", meta = (ClampMin = "0.0", Units = "cm"))
	float SphereRadius = 1000.0f;

	/** Attraction impulse strength. Higher values pull targets faster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction", meta = (ClampMin = "0.0"))
	float Power = 1000.0f;

	/** How fast each target's velocity converges to the pull velocity. 0 applies the pull velocity instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction", meta = (ClampMin = "0.0"))
	float PullInterpSpeed = 2.0f;

	/** Shows the sphere range while the magnetic effect is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction|Debug")
	bool bVisualizeSphere = false;

	/** Optional sphere mesh used by the range/debug visualization. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction|Visual")
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction|Visual")
	TObjectPtr<UMaterialInterface> SphereMaterial;

	/** Scalar parameter on the RootMesh material driven by the intensity ramp/pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction|Visual")
	FName IntensityParameterName = TEXT("EmissiveStrength");

	/** Time to ramp intensity from 0 to 1 right after activation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction|Visual", meta = (ClampMin = "0.0", Units = "s"))
	float IntensityRampUpTime = 1.0f;

	/** After the ramp, intensity oscillates between 1 and this value. Final value is clamped to [0, IntensityMax]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction|Visual", meta = (ClampMin = "0.0"))
	float IntensityMax = 5.0f;

	/** Duration of one 1 -> Max -> 1 oscillation cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic Attraction|Visual", meta = (ClampMin = "0.01", Units = "s"))
	float IntensityPulsePeriod = 1.0f;

private:
	void StartAttraction();
	// 활성 상태를 즉시 끝내고 끌던 적을 모두 놓는다. 이 타워가 켠 렌즈도 끈다. Duration 만료와 EndPlay 가 같이 쓴다.
	void StopAttraction();
	void UpdateAttraction(float DeltaSeconds);
	void UpdateVisualization();
	void UpdateRootMeshIntensity(float DeltaSeconds);
	void ApplyRootMeshIntensity(float Intensity);
	void UpdateSphereCollisionTransform();

	// 렌즈 포스트프로세스 시작 / 종료. 로컬에서만 의미가 있다.
	// 해킹 효과가 HackableComponent 의 Reliable Multicast 로 모든 인스턴스에서 실행되므로
	// 별도 RPC 없이 각 인스턴스가 Start/StopAttraction 에서 직접 켜고 끈다.
	void StartLensPostProcess(FVector Origin, float Radius, float InDuration);
	void EndLensPostProcess();

	UFUNCTION()
	void HandleSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SphereMID;

	// RootMesh 머티리얼 슬롯별 MID. EmissiveStrength 램프/왕복을 여기에 건다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RootMeshMIDs;

	float RemainingDuration = 0.0f;
	// 이번 활성화가 시작된 뒤 흐른 시간. Intensity 램프/왕복 계산용.
	float IntensityElapsed = 0.0f;
	TSet<TWeakObjectPtr<AEnemyBase>> OverlappingEnemies;
	TMap<TWeakObjectPtr<AEnemyBase>, FVector> PreviousEnemyLocations;
	TMap<TWeakObjectPtr<AEnemyBase>, FVector> EnemyPullVelocities;
};
