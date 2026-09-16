// Fill out your copyright notice in the Description page of Project Settings.

#include "Save/PresetPlayerStart.h"

TArray<FName> APresetPlayerStart::GetPresetIdOptions()
{
	return OutlierPresetStageIds::All();
}
