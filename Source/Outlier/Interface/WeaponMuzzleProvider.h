#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WeaponMuzzleProvider.generated.h"

class USkeletalMeshComponent;

UINTERFACE(MinimalAPI)
class UWeaponMuzzleProvider : public UInterface
{
	GENERATED_BODY()
};

// 본체 메시 안에 무기 외형이 포함된 캐릭터가 총구 기준점을 무기 로직에 제공한다.
class OUTLIER_API IWeaponMuzzleProvider
{
	GENERATED_BODY()

public:
	virtual USkeletalMeshComponent* GetWeaponMuzzleComponent(bool bFirstPerson) const = 0;
	virtual FName GetWeaponMuzzleSocketName(bool bFirstPerson) const = 0;

	virtual void GetWeaponMuzzleSocketNames(bool bFirstPerson, TArray<FName>& OutSocketNames) const
	{
		const FName SocketName = GetWeaponMuzzleSocketName(bFirstPerson);
		if (!SocketName.IsNone())
		{
			OutSocketNames.Add(SocketName);
		}
	}

	// 기본 무기는 기존처럼 한 번의 판정만 사용한다. 터렛처럼 한 그룹의 총구가 각각 판정할 때만 재정의한다.
	virtual bool UsesIndependentMuzzleShots() const { return false; }
	virtual void ResetWeaponMuzzleSequence() {}
	virtual void AdvanceWeaponMuzzleSequence() {}
};
