// Fill out your copyright notice in the Description page of Project Settings.

#include "ShooterAbilityUI.h"
#include "UI/ShooterAbilitySectionUI.h"

void UShooterAbilityUI::NativeConstruct()
{
	Super::NativeConstruct();

	AbilitySections.Reset();

	AbilitySections.SetNum(static_cast<int32>(EShooterAbility::None));

	AbilitySections[static_cast<int32>(EShooterAbility::None)] = Circle;
	Circle->SetAbility(EShooterAbility::None);

	AbilitySections[static_cast<int32>(EShooterAbility::Teleport)] = Buttom;
	Buttom->SetAbility(EShooterAbility::Teleport);

	AbilitySections[static_cast<int32>(EShooterAbility::Shield)] = TOP;
	TOP->SetAbility(EShooterAbility::Shield);

	AbilitySections[static_cast<int32>(EShooterAbility::Stealth)] = Left;
	Left->SetAbility(EShooterAbility::Stealth);

	AbilitySections[static_cast<int32>(EShooterAbility::Stimpack)] = Right;
	Right->SetAbility(EShooterAbility::Stimpack);

}

bool UShooterAbilityUI::TryGetHoveredAbility(EShooterAbility& OutAbility)
{
	//size 받아서 연산하기.
	return false;
}

