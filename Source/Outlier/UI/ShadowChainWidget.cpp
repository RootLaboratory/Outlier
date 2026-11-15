#include "UI/ShadowChainWidget.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Outlier.h"
#include "Shooter/ShooterCharacter.h"

void FShadowChainDisplayState::Reset()
{
	*this = FShadowChainDisplayState();
}

void FShadowChainDisplayState::Update(const FOutlierShadowChainPresentationState& State, float Remaining,
	float DeltaSeconds, float TransitionSeconds, bool bImmediate)
{
	if (!State.bInitialized || !State.bUnlocked)
	{
		Reset();
		return;
	}
	const bool bNewPassive = State.bPassiveActive && Remaining > 0.0f && State.DurationSeconds > 0.0f;
	const float TimeRatio = bNewPassive ? FMath::Clamp(Remaining / State.DurationSeconds, 0.0f, 1.0f) : 0.0f;
	const float StackRatio = FMath::Clamp(static_cast<float>(State.CurrentStacks) / FMath::Max(1, State.RequiredKills), 0.0f, 1.0f);
	if (bImmediate || !bHasSnapshot)
	{
		Progress = bNewPassive ? TimeRatio : StackRatio;
		Target = Progress;
		bTransitioning = bRefilling = false;
		ReconcileOffset = 0.0f;
	}
	else if ((bNewPassive && (!bPassive || State.ActivationSerial != ActivationSerial))
		|| (!bNewPassive && (bPassive || !FMath::IsNearlyEqual(Target, StackRatio))))
	{
		From = Progress;
		Target = bNewPassive ? 1.0f : StackRatio;
		Elapsed = 0.0f;
		bTransitioning = true;
		bRefilling = bNewPassive;
		ReconcileOffset = 0.0f;
	}
	bVisible = bHasSnapshot = true;
	bPassive = bNewPassive;
	ActivationSerial = State.ActivationSerial;
	const float SafeDelta = FMath::Max(0.0f, DeltaSeconds);
	if (bTransitioning)
	{
		Elapsed += SafeDelta;
		const float Alpha = TransitionSeconds > 0.0f ? FMath::Clamp(Elapsed / TransitionSeconds, 0.0f, 1.0f) : 1.0f;
		Progress = FMath::InterpEaseInOut(From, Target, Alpha, 2.0f);
		if (Alpha >= 1.0f)
		{
			bTransitioning = false;
			if (bRefilling && bPassive)
			{
				ReconcileOffset = 1.0f - TimeRatio;
				ReconcileElapsed = 0.0f;
			}
			bRefilling = false;
		}
	}
	else if (bPassive)
	{
		// Briefly remove the visual refill offset; the gameplay deadline never moves.
		ReconcileElapsed += SafeDelta;
		const float Alpha = FMath::Clamp(ReconcileElapsed / 0.1f, 0.0f, 1.0f);
		Progress = FMath::Clamp(TimeRatio + ReconcileOffset * (1.0f - FMath::SmoothStep(0.0f, 1.0f, Alpha)), 0.0f, 1.0f);
	}
	else
	{
		Progress = StackRatio;
	}
}

void UShadowChainWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsDesignTime()) return;
	bConstructed = true;
	if (bAutoObserveOwningShooter) ObservedShooter = Cast<AShooterCharacter>(GetOwningPlayerPawn());
	EnsureMaterial();
	BindObservedShooter();
	RefreshDisplay(0.0f, true);
}

void UShadowChainWidget::NativeDestruct()
{
	bConstructed = false;
	UnbindObservedShooter();
	Display.Reset();
	Super::NativeDestruct();
}

void UShadowChainWidget::BeginDestroy()
{
	StopUpdates();
	Super::BeginDestroy();
}

void UShadowChainWidget::SetObservedShooter(AShooterCharacter* Shooter)
{
	UnbindObservedShooter();
	bAutoObserveOwningShooter = false;
	ObservedShooter = Shooter;
	Display.Reset();
	if (bConstructed)
	{
		BindObservedShooter();
		RefreshDisplay(0.0f, true);
	}
}

void UShadowChainWidget::ClearObservedShooter()
{
	UnbindObservedShooter();
	bAutoObserveOwningShooter = false;
	ObservedShooter.Reset();
	Display.Reset();
	ApplyDisplay();
}

bool UShadowChainWidget::IsValidLocalShooter() const
{
	const AShooterCharacter* Shooter = ObservedShooter.Get();
	const APlayerController* Controller = GetOwningPlayer();
	return IsValid(Shooter) && Controller && Controller->IsLocalController()
		&& Controller->GetPawn() == Shooter && Shooter->IsLocallyControlled();
}

void UShadowChainWidget::BindObservedShooter()
{
	UnbindObservedShooter();
	// Pawn replication can follow HUD creation. Keep the local controller notification
	// even when its requested Shooter is not possessed yet.
	if (APlayerController* Controller = GetOwningPlayer(); Controller && Controller->IsLocalController())
	{
		ObservedController = Controller;
		Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::HandlePawnChanged);
	}
	if (!IsValidLocalShooter()) return;
	AShooterCharacter* Shooter = ObservedShooter.Get();
	UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
	if (!ASC) return;
	ObservedASC = ASC;
	PresentationHandle = ASC->OnShadowChainPresentationChanged.AddUObject(this, &ThisClass::HandlePresentationChanged);
	DeathHandle = ASC->RegisterGameplayTagEvent(OutlierGameplayTags::State::Dead(), EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleDeathChanged);
	Shooter->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleObservedEndPlay);
}

void UShadowChainWidget::UnbindObservedShooter()
{
	StopUpdates();
	if (UOutlierAbilitySystemComponent* ASC = ObservedASC.Get())
	{
		ASC->OnShadowChainPresentationChanged.Remove(PresentationHandle);
		ASC->RegisterGameplayTagEvent(OutlierGameplayTags::State::Dead(), EGameplayTagEventType::NewOrRemoved).Remove(DeathHandle);
	}
	if (AShooterCharacter* Shooter = ObservedShooter.Get()) Shooter->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleObservedEndPlay);
	if (APlayerController* Controller = ObservedController.Get()) Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePawnChanged);
	PresentationHandle.Reset();
	DeathHandle.Reset();
	ObservedASC.Reset();
	ObservedController.Reset();
}

void UShadowChainWidget::HandleObservedEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	if (!bAutoObserveOwningShooter)
	{
		ClearObservedShooter();
		return;
	}
	// Keep listening to the local controller after the old Pawn has ended.
	UnbindObservedShooter();
	ObservedShooter.Reset();
	Display.Reset();
	BindObservedShooter();
	RefreshDisplay(0.0f, true);
}

void UShadowChainWidget::HandlePawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (bAutoObserveOwningShooter)
	{
		UnbindObservedShooter();
		ObservedShooter = Cast<AShooterCharacter>(NewPawn);
		Display.Reset();
		BindObservedShooter();
		RefreshDisplay(0.0f, true);
		return;
	}
	if (NewPawn == ObservedShooter.Get())
	{
		BindObservedShooter();
		RefreshDisplay(0.0f, true);
	}
	else
	{
		ClearObservedShooter();
	}
}

void UShadowChainWidget::HandleDeathChanged(const FGameplayTag Tag, int32 Count)
{
	HandlePresentationChanged();
}

void UShadowChainWidget::HandlePresentationChanged()
{
	RefreshDisplay(0.0f, false);
}

void UShadowChainWidget::RefreshDisplay(float DeltaSeconds, bool bImmediate)
{
	UOutlierAbilitySystemComponent* ASC = ObservedASC.Get();
	if (!bConstructed || !IsValidLocalShooter() || !ASC || ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		Display.Reset();
		StopUpdates();
		ApplyDisplay();
		return;
	}
	const FOutlierShadowChainPresentationState& State = ASC->GetShadowChainPresentationState();
	// A valid passive snapshot can precede GameState replication. Retry without inventing a clock.
	if (State.bInitialized && State.bUnlocked && State.bPassiveActive && GetWorld() && !GetWorld()->GetGameState())
	{
		Display.Reset();
		ApplyDisplay();
		StartUpdates();
		return;
	}
	Display.Update(State, ASC->GetShadowChainStealthRemaining(), DeltaSeconds, FillTransitionSeconds, bImmediate);
	ApplyDisplay();
	if (Display.NeedsUpdate()) StartUpdates();
	else StopUpdates();
}

void UShadowChainWidget::EnsureMaterial()
{
	if (!ShadowChainImage)
	{
		if (!bWarnedMissingMaterial)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[ShadowChain.UI] Missing ShadowChainImage binding: %s"), *GetName());
			bWarnedMissingMaterial = true;
		}
		return;
	}
	UMaterialInterface* Source = FillMaterial;
	if (!Source) Source = Cast<UMaterialInterface>(ShadowChainImage->GetBrush().GetResourceObject());
	if (Source == DynamicMaterial.Get()) Source = MaterialSource;
	if (!Source)
	{
		if (!bWarnedMissingMaterial)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[ShadowChain.UI] Assign FillMaterial or a material brush on ShadowChainImage: %s"), *GetName());
			bWarnedMissingMaterial = true;
		}
		return;
	}
	if (!DynamicMaterial || MaterialSource != Source)
	{
		UMaterialInstanceDynamic* SourceMID = Cast<UMaterialInstanceDynamic>(Source);
		DynamicMaterial = UMaterialInstanceDynamic::Create(SourceMID ? SourceMID->Parent.Get() : Source, this);
		if (DynamicMaterial && SourceMID) DynamicMaterial->CopyParameterOverrides(SourceMID);
		MaterialSource = Source;
	}
	if (DynamicMaterial)
	{
		if (ShadowChainImage->GetBrush().GetResourceObject() != DynamicMaterial.Get()) ShadowChainImage->SetBrushFromMaterial(DynamicMaterial);
		if (IconTexture) DynamicMaterial->SetTextureParameterValue(TEXT("NodeTexture"), IconTexture);
		DynamicMaterial->SetScalarParameterValue(TEXT("UseActivatedTexture"), 0.0f);
	}
}

void UShadowChainWidget::ApplyDisplay()
{
	SetVisibility(Display.bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	EnsureMaterial();
	if (DynamicMaterial)
	{
		DynamicMaterial->SetScalarParameterValue(TEXT("Progress"), Display.Progress);
		DynamicMaterial->SetVectorParameterValue(TEXT("UnlockedColor"), DimColor);
		DynamicMaterial->SetVectorParameterValue(TEXT("ActivatedColor"), Display.bPassive ? StealthColor : StackColor);
	}
}

void UShadowChainWidget::StartUpdates()
{
	if (UpdateHandle.IsValid()) return;
	LastWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UpdateHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickDisplay));
}

void UShadowChainWidget::StopUpdates()
{
	FTSTicker::GetCoreTicker().RemoveTicker(UpdateHandle);
	UpdateHandle.Reset();
}

bool UShadowChainWidget::TickDisplay(float DeltaSeconds)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : LastWorldTime;
	const float Elapsed = static_cast<float>(FMath::Max(0.0, Now - LastWorldTime));
	LastWorldTime = Now;
	RefreshDisplay(Elapsed, false);
	return UpdateHandle.IsValid();
}
