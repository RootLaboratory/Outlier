#pragma once

#include "CoreMinimal.h"
#include "VitalityPresentationTypes.generated.h"

class AActor;

UENUM()
enum class EOutlierVitalityChannel : uint8
{
	ShooterHealth,
	ShooterShield,
	PartnerHealth,
	Count UMETA(Hidden)
};

UENUM()
enum class EOutlierVitalityUpdatePolicy : uint8
{
	Animated,
	Immediate
};

// 수치와 표시 정책을 같은 메시지에 담는다. 별도 Attribute OnRep 순서로 원인을 추측하지 않는다.
USTRUCT()
struct TAGDRIVENUI_API FOutlierVitalityPresentationUpdate
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Source = nullptr;
	UPROPERTY()
	EOutlierVitalityChannel Channel = EOutlierVitalityChannel::ShooterHealth;
	UPROPERTY()
	float Current = 0.0f;
	UPROPERTY()
	float Maximum = 0.0f;
	UPROPERTY()
	EOutlierVitalityUpdatePolicy Policy = EOutlierVitalityUpdatePolicy::Animated;
	UPROPERTY()
	uint32 Generation = 0;
	UPROPERTY()
	uint32 Sequence = 0;
	// 구독/대상 교체/초기 재동기화에 사용한다. 새 세대는 Snapshot부터 수신한다.
	UPROPERTY()
	bool bSnapshot = false;
};
