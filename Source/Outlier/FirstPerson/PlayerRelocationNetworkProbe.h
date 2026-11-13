#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "PlayerRelocationNetworkProbe.generated.h"

class AShooterCharacter;
class APartnerCharacter;
class UBoxComponent;
class ARoomCombatBarrier;
class ARoomVolume;
class FJsonObject;

UCLASS(Transient, NotBlueprintable)
class APlayerRelocationProbeController : public AFirstPersonPlayerController
{
	GENERATED_BODY()
public:
	UFUNCTION(Server, Reliable) void ServerProbeReady();
	UFUNCTION(Server, Reliable) void ServerProbeObservation(int32 Index, const FString& Json);
	UFUNCTION(Server, Reliable) void ServerProbeTransitionReady();
	bool bProbeReady = false;
};

// 명시적인 개발 검증 URL에서만 선택한다. 일반 플레이의 Controller와 GameMode는 변경하지 않는다.
UCLASS(Transient, NotBlueprintable)
class APlayerRelocationProbeGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	APlayerRelocationProbeGameMode();
	virtual void PostLogin(APlayerController* Player) override;
};

UCLASS(Transient, NotBlueprintable)
class APlayerRelocationNetworkProbe : public AActor
{
	GENERATED_BODY()
public:
	APlayerRelocationNetworkProbe();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
	void ReceiveObservation(APlayerRelocationProbeController* Sender, int32 Index, const FString& Json);
	UPROPERTY(Replicated) TObjectPtr<AShooterCharacter> Shooter;
	UPROPERTY(Replicated) TObjectPtr<APartnerCharacter> Partner;
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> DestinationFloor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> StartFloor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Ceiling;
	UPROPERTY(Replicated) TObjectPtr<ARoomCombatBarrier> Barrier;
	UPROPERTY(Transient) TObjectPtr<ARoomVolume> DestinationRoom;
	UPROPERTY(Transient) TObjectPtr<ARoomVolume> OutsideRoom;
	UPROPERTY(Replicated) int32 CaseIndex = -1;
	UPROPERTY(Replicated) int32 Phase = 0;
	UPROPERTY(Replicated) FVector ExpectedShooter = FVector::ZeroVector;
	UPROPERTY(Replicated) FVector ExpectedPartner = FVector::ZeroVector;
	UPROPERTY(Replicated) bool bExpectedCrouch = false;
	UPROPERTY(Replicated) bool bExpectedBarrier = false;
	UPROPERTY(Replicated) bool bAllPassed = true;
	UPROPERTY(Replicated) int32 ExpectedShooterMode = 0;
	UPROPERTY(Replicated) int32 ExpectedPartnerMode = 0;
	UPROPERTY(Replicated) int32 ExpectedCharges = 0;
	UPROPERTY(Replicated) bool bExpectedSuit = false;
	UPROPERTY(Transient) TObjectPtr<class ASuitInteraction> Suit;
	float PhaseStart = 0;
	float StableSince = -1;
	int32 ObservedCase = -1;
	int32 GeometryCase = -2;
	bool bConnected = false;
	bool bFinished = false;
	bool bActionAccepted = false;
	bool bServerPassed = false;
	FVector BeforeShooter;
	FVector BeforePartner;
	TSet<TWeakObjectPtr<APlayerRelocationProbeController>> Observers;
	void SetupCase();
	void PerformAction();
	void CaptureExpected();
	void ApplyGeometry();
	TSharedRef<FJsonObject> Observe() const;
	void Write(const TSharedRef<FJsonObject>& Row) const;
};
