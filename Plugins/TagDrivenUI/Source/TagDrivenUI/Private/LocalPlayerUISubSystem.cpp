// Fill out your copyright notice in the Description page of Project Settings.
#include "LocalPlayerUISubSystem.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "AbilityIconUI.h"
#include "MainUIBase.h"
#include "HPBarUI.h"
#include "PartnerCamUI.h"
#include "AmmoUI.h"
#include "DynamicCrossHair.h"
#include "EventDrivenUI.h"
#include "StaticCrossHair.h"
#include "DistanceSlideUI.h"
#include "PartnerHPUI.h"
#include "ShooterCurrentAbilityIcon.h"
#include "ShooterMainWidget.h"
#include "TagDrivenUIGameplayTags.h"
#include "Blueprint/UserWidget.h"

void ULocalPlayerUISubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	//UE_LOG(LogTemp, Warning, TEXT("LocalPlayerUISubSystem Initialized"));
}

void ULocalPlayerUISubSystem::Deinitialize()
{
	Super::Deinitialize();
}

void ULocalPlayerUISubSystem::RegisterMainUI(UMainUIBase* InMainUI)
{
	MainUIInstance = InMainUI;
}

void ULocalPlayerUISubSystem::UnregisterMainUI(UMainUIBase* InMainUI)
{
	if (MainUIInstance == InMainUI)
	{
		InteractionWidgetInstance = nullptr;
		MainUIInstance = nullptr;
	}
}

UMainUIBase* ULocalPlayerUISubSystem::GetMainUI() const
{
	return IsValid(MainUIInstance) ? MainUIInstance.Get() : nullptr;
}

UEventDrivenUI* ULocalPlayerUISubSystem::GetModule(const FGameplayTag& ModuleTag) const
{
	UMainUIBase* MainUI = GetMainUI();
	return MainUI ? MainUI->GetModule(ModuleTag) : nullptr;
}

UEventDrivenUI* ULocalPlayerUISubSystem::GetModuleAny(
	const FGameplayTag& FirstTag,
	const FGameplayTag& SecondTag
) const
{
	if (UEventDrivenUI* FirstModule = GetModule(FirstTag))
	{
		return FirstModule;
	}

	return GetModule(SecondTag);
}

void ULocalPlayerUISubSystem::OnRep_HUDActivate(bool bShouldActivate)
{
	UMainUIBase* MainUI = GetMainUI();
	if (!MainUI)
	{
		return;

	}

	if (bShouldActivate)
	{
		MainUI->ModuleActivate();
	}
	else
	{
		MainUI->ModuleDeActivate();
	}

}

void ULocalPlayerUISubSystem::OnRep_HealthChanged(float InHealth, float MaxHealth)
{

	float Ratio = InHealth / MaxHealth;

	if (!GetMainUI())
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(GetModuleAny(TagDrivenUITags::Shooter::HP(), TagDrivenUITags::Partner::HP())))
	{
		//UE_LOG(LogTemp, Error, TEXT("HP Changed, %f"), Ratio);
		HPBarUI->HealthChanged(Ratio);
	}
}

void ULocalPlayerUISubSystem::OnRep_PartnerShieldChanged(float InPartnerShield, float MaxPartnerShield)
{
	float Ratio = MaxPartnerShield > 0.0f ? InPartnerShield / MaxPartnerShield : 0.0f;

	if (!GetMainUI())
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(GetModuleAny(TagDrivenUITags::Shooter::HP(), TagDrivenUITags::Partner::HP())))
	{
		//UE_LOG(LogTemp, Error, TEXT("PartnerShield Changed, %f"), Ratio);
		HPBarUI->PartnerShieldChanged(Ratio);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShieldChanged( float InCurShield,  float InMaxShield)
{
	float Ratio = InCurShield / InMaxShield;

	if (!GetMainUI())
	{
		return;
	}

	if (UHPBarUI* HPBarUI = Cast<UHPBarUI>(GetModuleAny(TagDrivenUITags::Shooter::HP(), TagDrivenUITags::Partner::HP())))
	{
		HPBarUI->ShieldChanged(Ratio);
	}
}

void ULocalPlayerUISubSystem::OnRep_AmmoCountChanged(int32 InAmmoCount)
{

	if (!GetMainUI())
	{
		return;
	}

	if (UAmmoUI* AmmoUI = Cast<UAmmoUI>(GetModuleAny(TagDrivenUITags::Shooter::Ammo(), TagDrivenUITags::Partner::Ammo())))
	{
		AmmoUI->AmmoCountChanged(InAmmoCount);
	}

}

void ULocalPlayerUISubSystem::OnRep_ShooterConditionRefresh()
{
	if (UPartnerHPUI* PartnerHPUI = Cast<UPartnerHPUI>(GetModule(TagDrivenUITags::Partner::HP())))
	{
		PartnerHPUI->RefreshShooterConditionUI();
	}
}

void ULocalPlayerUISubSystem::OnRep_PlayerStateChanged(EUIPlayerState State)
{

	if (UDynamicCrossHair* CrossHairUI = Cast<UDynamicCrossHair>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		CrossHairUI->SetPlayerState(State);
	}
	else
	{
		//UE_LOG(LogTemp, Error, TEXT("UNVALID CLASS NOT DynamicCrossHairClass"));
	}
}



void ULocalPlayerUISubSystem::PartnerCameraToggle()
{
	if (!GetMainUI())
	{
		UE_LOG(LogTemp, Error, TEXT("GetMainUI"));

		return;
	}

	if (UPartnerCamUI* PartnerCamUI = Cast<UPartnerCamUI>(GetModuleAny(TagDrivenUITags::Shooter::PartnerCam(), TagDrivenUITags::Partner::PartnerCam())))
	{
		PartnerCamUI->TogglePartnerCamera();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PartnerCamUI"));

	}
}

void ULocalPlayerUISubSystem::PartnerDistanceUpdate(const float Distance)
{
	if (UDistanceSlideUI* DistanceSlideUI = Cast<UDistanceSlideUI>(GetModule(TagDrivenUITags::Partner::DistanceLimit())))
	{
		DistanceSlideUI->UpdateDistanceRatio(Distance);
	}
}

void ULocalPlayerUISubSystem::OnCurrentWeaponChanged(EWidgetWeaponType WeaponType)
{
	if (UShooterMainWidget* ShooterMainUI = Cast<UShooterMainWidget>(GetMainUI()))
	{
		ShooterMainUI->OnChangeWeapon(WeaponType);
	}
}

void ULocalPlayerUISubSystem::OnCurrentAbilityChanged(const FGameplayTag& AbilityTag)
{
	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		CurrentAbilityIcon->SetCurrentAbility(AbilityTag);
	}
}

void ULocalPlayerUISubSystem::ResetShooterAbilityState(const FGameplayTag& SelectedAbilityTag)
{
	if (UMainUIBase* MainUI = GetMainUI())
	{
		MainUI->ResetAbilityCooldowns();
	}

	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(
		GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		CurrentAbilityIcon->ResetCooldown();
		CurrentAbilityIcon->SetCurrentAbility(SelectedAbilityTag);
	}
}

bool ULocalPlayerUISubSystem::ApplyCurrentAbilityCooldownIfMatches(const FGameplayTag& AbilityTag, float CoolTime)
{
	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		return CurrentAbilityIcon->ApplyCooldownIfMatches(AbilityTag, CoolTime);
	}

	return false;
}

void ULocalPlayerUISubSystem::OnRep_Aiming()
{
	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		CrossHairBase->OnAiming();
	}
	
}

void ULocalPlayerUISubSystem::OnRep_AimingOff()
{
	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		CrossHairBase->OnAimingOff();
	}
}


void ULocalPlayerUISubSystem::OnRep_AttackSign(EAttackSign InType)
{

	if (UCrossHairBase* CrossHairBase = Cast<UCrossHairBase>(GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair())))
	{
		//UE_LOG(LogTemp, Error, TEXT("CrossHair Instance Class: %s"), *GetNameSafe(CrossHairBase->GetClass()));
		//UE_LOG(LogTemp, Error, TEXT("OnRep_AttackSign %d"), (uint8)InType);
		CrossHairBase->SpawnAttackSign(InType);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShootCrosshairChanged(float InFireRate)
{
	UEventDrivenUI* CrossHairModule = GetModuleAny(TagDrivenUITags::Shooter::CrossHair(), TagDrivenUITags::Partner::CrossHair());
	if (!CrossHairModule)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LocalPlayerUISubSystem] ShootCrosshair skipped: MainUI or CrossHair module is not ready"));
		return;
	}

	if (UDynamicCrossHair* CrossHairBase = Cast<UDynamicCrossHair>(CrossHairModule))
	{
		//UE_LOG(LogTemp, Log, TEXT("OnRep_ShootCrosshairChanged"));
		CrossHairBase->On_RepShoot();
	}
	else if (UStaticCrossHair* Crosshair = Cast<UStaticCrossHair>(CrossHairModule))
	{
		Crosshair->SetCoolTime(InFireRate );
		//UE_LOG(LogTemp, Log, TEXT("InFireRate %f"), InFireRate);

	}
	else
		UE_LOG(LogTemp, Log, TEXT("Type Error"));

}

void ULocalPlayerUISubSystem::OnRep_ShooterHPStateChanged(const FGameplayTag& InShooterConditionTag)
{
	if (UPartnerHPUI* PartnerHPUI = Cast<UPartnerHPUI>(GetModule(TagDrivenUITags::Partner::HP())))
	{
		PartnerHPUI->SetShooterCondition(InShooterConditionTag);
	}
}

void ULocalPlayerUISubSystem::OnRep_ShooterDynamicCrosshairChanged(bool InFlag)
{
	if (UCrossHairBase* Crosshair = Cast<UCrossHairBase>(GetModule(TagDrivenUITags::Shooter::CrossHair())))
	{
		if (InFlag)
		{
			Crosshair->OnAiming();
		}
		else
		{
			Crosshair->OnAimingOff();
		}
	}
}

void ULocalPlayerUISubSystem::OnAbilityDisabledByDistance()
{
	if (UMainUIBase* MainUI = GetMainUI())
	{
		MainUI->On_RepAbilityDisabledByDistance();
	}
}

void ULocalPlayerUISubSystem::OnAbilityEnabledByDistance()
{
	if (UMainUIBase* MainUI = GetMainUI())
	{
		MainUI->On_RepAbilityabledByDistance();
	}
}

void ULocalPlayerUISubSystem::OnAbilityUsed(const FGameplayTag& AbilityTag, float CoolTime)
{
	UMainUIBase* MainUI = GetMainUI();
	if (!MainUI)
	{
		return;
	}

	if (UAbilityIconUI* Icon = MainUI->GetAbilityIcon(AbilityTag))
	{
		Icon->SetCoolTime(CoolTime);
	}

	if (UShooterCurrentAbilityIcon* CurrentAbilityIcon = Cast<UShooterCurrentAbilityIcon>(GetModule(TagDrivenUITags::Shooter::CurrentAbility())))
	{
		CurrentAbilityIcon->ApplyCooldownIfMatches(AbilityTag, CoolTime);
	}
}

void ULocalPlayerUISubSystem::BindInteractionWidget(UUserWidget* InteractionWidget)
{
	BindInteractionWidget(InteractionWidget, FVector2D(0.0f, 160.0f));
}

void ULocalPlayerUISubSystem::BindInteractionWidget(UUserWidget* InteractionWidget, const FVector2D& WidgetPosition)
{
	UMainUIBase* MainUI = GetMainUI();
	if (!MainUI || !MainUI->InteractionLayer || !InteractionWidget)
	{
		return;
	}

	InteractionWidgetInstance = InteractionWidget;

	if (!InteractionWidget->GetParent())
	{
		UCanvasPanelSlot* CanvasSlot = MainUI->InteractionLayer->AddChildToCanvas(InteractionWidget);
		if (CanvasSlot)
		{
			CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CanvasSlot->SetPosition(WidgetPosition);
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetSize(FVector2D(64.0f, 64.0f));
			CanvasSlot->SetZOrder(0);
		}
	}
	else if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(InteractionWidget->Slot))
	{
		CanvasSlot->SetPosition(WidgetPosition);
	}

	InteractionWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void ULocalPlayerUISubSystem::UnbindInteractionWidget(UUserWidget* InteractionWidget)
{
	if (!InteractionWidget || InteractionWidgetInstance != InteractionWidget)
	{
		return;
	}

	InteractionWidget->SetVisibility(ESlateVisibility::Collapsed);
}

void ULocalPlayerUISubSystem::PartnerCameraBind(USceneCaptureComponent2D* InCaptureComponent2D)
{
	//UE_LOG(LogTemp, Error, TEXT("Try PartnerCameraBind"));

	if (!InCaptureComponent2D)
	{
		return;
	}

	if (UPartnerCamUI* PartnerCamUI = Cast<UPartnerCamUI>(GetModuleAny(TagDrivenUITags::Shooter::PartnerCam(), TagDrivenUITags::Partner::PartnerCam())))
	{
		UTextureRenderTarget2D* RenderTarget = InCaptureComponent2D->TextureTarget;
		if (RenderTarget)
		{
			PartnerCamUI->SetPartnerRenderTarget(RenderTarget);
		}
		else
		{
		//	UE_LOG(LogTemp, Error, TEXT("Cant RT"));

		}
	}
	else
	{
		//	UE_LOG(LogTemp, Error, TEXT("Cant PartnerCamUI"));

	}
}
