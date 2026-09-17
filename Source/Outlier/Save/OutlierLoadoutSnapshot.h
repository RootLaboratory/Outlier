// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Weapon/WeaponDataTypes.h"
// TSubclassOf<AWeaponBase> 가 operator UClass* 에서 AWeaponBase::StaticClass() 를 부르므로
// 전방선언으로는 부족하고 완전한 타입이 필요하다.
#include "Weapon/WeaponBase.h"
#include "OutlierLoadoutSnapshot.generated.h"

/**
 * 페어가 지금까지 획득한 무기를, 무기 액터의 수명과 무관하게 남겨두는 기록.
 *
 * 무기 액터는 아레나 리로드(Pawn 파괴 -> Data Layer 재스트리밍 -> 재스폰) 구간에서
 * 반드시 사라진다. 그 액터에 상태를 얹어두면 같이 사라지므로, 살아남는 PlayerState가
 * "무엇을 갖고 있었는가"를 클래스 단위로 기억하고 새 Pawn에서 다시 만들어 준다.
 *
 * 일반 프리셋 리로드에서는 WeaponClass 만 사용하고, 체크포인트에서는 CurrentAmmo 까지
 * 채운다. CurrentAmmo == INDEX_NONE 은 기존 프리셋처럼 새 무기의 기본 탄약을 쓰라는 뜻이다.
 */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierWeaponSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<AWeaponBase> WeaponClass;

	// 탄약이 없는 무기와 기존 프리셋 기록은 INDEX_NONE 으로 구분한다.
	UPROPERTY()
	int32 CurrentAmmo = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierLoadoutSnapshot
{
	GENERATED_BODY()

	// index == EWeaponSlot. UShooterInventoryComponent::WeaponSlots 와 크기/인덱스를 맞춘다.
	// 슬롯을 따로 저장하지 않고 인덱스로 표현하므로 캡처/복원 어느 쪽에서도
	// 타입 -> 슬롯 역산(GetSlotForWeaponType)이 필요 없고, 슬롯 enum이 늘어나면
	// 양쪽이 같은 출처를 보고 자동으로 따라온다.
	UPROPERTY()
	TArray<FOutlierWeaponSnapshot> SlotSnapshots;

	// 복원 직후 손에 들려 있어야 할 슬롯.
	UPROPERTY()
	EWeaponSlot CurrentSlot = EWeaponSlot::Primary;

	// Partner는 InventoryComponent가 없고(슈트 지급이 유일 경로) 무기도 하나뿐이라
	// 별도 PlayerState를 두지 않고 Shooter PlayerState에 같이 싣는다.
	// 슈트 지급 액터(ASuitInteraction)는 소비 직후 스스로 Destroy 하므로
	// 이 클래스를 역산할 수 있는 곳이 런타임에 남지 않는다 — 여기 적어두어야 한다.
	UPROPERTY()
	TSubclassOf<AWeaponBase> PartnerWeaponClass;

	bool IsEmpty() const
	{
		if (PartnerWeaponClass)
		{
			return false;
		}

		for (const FOutlierWeaponSnapshot& SlotSnapshot : SlotSnapshots)
		{
			if (SlotSnapshot.WeaponClass)
			{
				return false;
			}
		}

		return true;
	}

	void Reset()
	{
		SlotSnapshots.Reset();
		CurrentSlot = EWeaponSlot::Primary;
		PartnerWeaponClass = nullptr;
	}
};
