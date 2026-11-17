// Copyright Epic Games, Inc. All Rights Reserved.

#include "Outlier.h"
#include "Logging/OutlierLog.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, Outlier, "Outlier" );

DEFINE_LOG_CATEGORY(LogOutlier)
namespace
{
TAutoConsoleVariable<int32> CVarOutlierLogAll(TEXT("outlier.Log.All"), 0,
	TEXT("Enable all general Outlier trace channels. 0=off, 1=on. Feature-specific diagnostics keep their own switches."));
TAutoConsoleVariable<int32> CVarOutlierLogCore(TEXT("outlier.Log.Core"), 0,
	TEXT("Enable Core trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogNetwork(TEXT("outlier.Log.Network"), 0,
	TEXT("Enable Network trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogSave(TEXT("outlier.Log.Save"), 0,
	TEXT("Enable Save trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogShooter(TEXT("outlier.Log.Shooter"), 0,
	TEXT("Enable Shooter trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogAnimation(TEXT("outlier.Log.Animation"), 0,
	TEXT("Enable Animation trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogWeapon(TEXT("outlier.Log.Weapon"), 0,
	TEXT("Enable Weapon trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogEnemy(TEXT("outlier.Log.Enemy"), 0,
	TEXT("Enable Enemy trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogPartner(TEXT("outlier.Log.Partner"), 0,
	TEXT("Enable Partner trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogFlight(TEXT("outlier.Log.Flight"), 0,
	TEXT("Enable Flight trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogAbility(TEXT("outlier.Log.Ability"), 0,
	TEXT("Enable Ability trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogRoom(TEXT("outlier.Log.Room"), 0,
	TEXT("Enable Room trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogInteraction(TEXT("outlier.Log.Interaction"), 0,
	TEXT("Enable Interaction trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogUI(TEXT("outlier.Log.UI"), 0,
	TEXT("Enable UI trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogAudio(TEXT("outlier.Log.Audio"), 0,
	TEXT("Enable Audio trace logs. 0=off, 1=on. Warnings and errors remain visible."));
TAutoConsoleVariable<int32> CVarOutlierLogWorld(TEXT("outlier.Log.World"), 0,
	TEXT("Enable World trace logs. 0=off, 1=on. Warnings and errors remain visible."));
}

bool OutlierLog::IsEnabled(EChannel Channel)
{
	if (CVarOutlierLogAll.GetValueOnAnyThread() != 0) return true;
	switch (Channel)
	{
	case EChannel::Core: return CVarOutlierLogCore.GetValueOnAnyThread() != 0;
	case EChannel::Network: return CVarOutlierLogNetwork.GetValueOnAnyThread() != 0;
	case EChannel::Save: return CVarOutlierLogSave.GetValueOnAnyThread() != 0;
	case EChannel::Shooter: return CVarOutlierLogShooter.GetValueOnAnyThread() != 0;
	case EChannel::Animation: return CVarOutlierLogAnimation.GetValueOnAnyThread() != 0;
	case EChannel::Weapon: return CVarOutlierLogWeapon.GetValueOnAnyThread() != 0;
	case EChannel::Enemy: return CVarOutlierLogEnemy.GetValueOnAnyThread() != 0;
	case EChannel::Partner: return CVarOutlierLogPartner.GetValueOnAnyThread() != 0;
	case EChannel::Flight: return CVarOutlierLogFlight.GetValueOnAnyThread() != 0;
	case EChannel::Ability: return CVarOutlierLogAbility.GetValueOnAnyThread() != 0;
	case EChannel::Room: return CVarOutlierLogRoom.GetValueOnAnyThread() != 0;
	case EChannel::Interaction: return CVarOutlierLogInteraction.GetValueOnAnyThread() != 0;
	case EChannel::UI: return CVarOutlierLogUI.GetValueOnAnyThread() != 0;
	case EChannel::Audio: return CVarOutlierLogAudio.GetValueOnAnyThread() != 0;
	case EChannel::World: return CVarOutlierLogWorld.GetValueOnAnyThread() != 0;
	default: return false;
	}
}
