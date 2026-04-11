// Fill out your copyright notice in the Description page of Project Settings.
#include "MainUIBase.h"

UEventDrivenUI* UMainUIBase::GetModule(EUIModule Key)
{
    if (Module.Contains(Key))
    {
        return Module[Key];
    }

    return nullptr;
}
