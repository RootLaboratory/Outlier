#include "UI/WorldWidgetPresentation.h"

#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

void FWorldWidgetPresentation::Apply(UWidgetComponent* Component, const FVector& Anchor) const
{
	if (!Component) return;
	Component->SetWorldLocation(Anchor + WidgetOffset);
	UUserWidget* Widget = Component->GetUserWidgetObject();
	if (!Widget) return;
	const float LowerScale = FMath::Max(0.01f, MinScale);
	const float UpperScale = FMath::Max(LowerScale, MaxScale);
	float Scale = FMath::Clamp(1.0f, LowerScale, UpperScale);
	ULocalPlayer* LocalPlayer = Component->GetOwnerPlayer();
	APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(Component->GetWorld()) : nullptr;
	if (Controller)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const float Distance = FMath::Max(1.0f, FVector::Distance(ViewLocation, Component->GetComponentLocation()));
		Scale = FMath::Clamp(FMath::Max(1.0f, ReferenceDistance) / Distance, LowerScale, UpperScale);
	}
	Widget->SetRenderScale(FVector2D(Scale, Scale));
}
