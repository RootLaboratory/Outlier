// Fill out your copyright notice in the Description page of Project Settings.


#include "CrossHairBase.h"

void UCrossHairBase::Activate()
{
	Super::Activate();

	// Activate() 는 SetVisibility 를 무조건 밀어버린다.
	// 조준 중에 모듈이 켜지면(무기 전환 등)
	// ADS 숨김이 그대로 덮여서 크로스헤어가 다시 보인다. 상태를 다시 적용한다.
	if (bAiming)
	{
		OnAiming();
	}
}
