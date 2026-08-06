#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Explosion/ExplosionTypes.h"
#include "ExplosiveProp.generated.h"

class ASelfDestructDrone;
class UCapsuleComponent;
class UExplosionComponent;
class UNiagaraSystem;
class USceneComponent;
class USoundBase;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API AExplosiveProp : public AActor
{
	GENERATED_BODY()

public:
	AExplosiveProp();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(
		float DamageAmount,
		FDamageEvent const& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser) override;

	// 자폭 드론이 Deferred Spawn을 완료하기 전에 1P/3P가 공유할 소켓 이름을 전달한다.
	void InitializeMountedSocket(FName InMountedSocketName);

	// 외부 SaveGame 복구 시스템이 서버에서 호출할 초기 상태 복구 함수다.
	UFUNCTION(BlueprintCallable, Category = "Explosive Prop")
	void ResetToInitialState();

	UFUNCTION(BlueprintPure, Category = "Explosive Prop")
	bool IsExploded() const { return bExploded; }

	UFUNCTION(BlueprintPure, Category = "Explosive Prop")
	float GetCurrentHP() const { return CurrentHP; }

	// 자폭 드론이 Owner로 지정된 경우에는 자체 HP 대신 드론 HP로 무기 피해를 전달한다.
	UFUNCTION(BlueprintPure, Category = "Explosive Prop")
	bool IsMountedOnSelfDestructDrone() const { return CachedOwningDrone.IsValid(); }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive Prop")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive Prop")
	TObjectPtr<UStaticMeshComponent> ExplosiveMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive Prop")
	TObjectPtr<UStaticMeshComponent> FirstPersonExplosiveMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive Prop")
	TObjectPtr<UCapsuleComponent> HitCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive Prop")
	TObjectPtr<UExplosionComponent> ExplosionComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosive Prop|Data")
	FDataTableRowHandle ExplosivePropRow;

	// 에셋 참조는 DataTable 대신 폭발물 BP별 기본값으로 관리한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive Prop|Feedback")
	TObjectPtr<UNiagaraSystem> HitFeedbackVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive Prop|Feedback")
	TObjectPtr<USoundBase> HitFeedbackSFX;

	UFUNCTION(BlueprintImplementableEvent, Category = "Explosive Prop|Feedback")
	void OnHitFeedback(float HitFlashDuration);

private:
	void SetupMountedPresentation();

	// 배치 폭발물의 HP와 피격 연출 값을 DataTable에서 읽는다.
	bool InitializeFromDataTable();

	// 폭발 여부에 맞춰 Mesh 표시와 피격 Collision을 함께 변경한다.
	void ApplyExplodedState();

	// 공통 폭발 처리가 끝났다는 알림을 받아 배치 폭발물을 비활성화한다.
	UFUNCTION()
	void HandleExplosionProcessed();

	// OnRep: 서버의 bExploded 값이 클라이언트에 복제됐을 때 표시 상태를 갱신한다.
	UFUNCTION()
	void OnRep_Exploded();

	// 서버에서 현재 클라이언트들에 피격 VFX, SFX와 BP 점멸 이벤트를 전달한다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayHitFeedback(
		FVector_NetQuantize ImpactPoint,
		UNiagaraSystem* HitVFX,
		USoundBase* HitSFX,
		float HitFlashDuration);

	UPROPERTY(ReplicatedUsing = OnRep_Exploded)
	bool bExploded = false;

	UPROPERTY(Replicated)
	FName MountedSocketName = NAME_None;

	float CurrentHP = 0.0f;
	TOptional<FExplosivePropRow> RuntimePropRow;
	TWeakObjectPtr<ASelfDestructDrone> CachedOwningDrone;
};
