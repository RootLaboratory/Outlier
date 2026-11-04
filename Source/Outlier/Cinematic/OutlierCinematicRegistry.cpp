#include "Cinematic/OutlierCinematicRegistry.h"

#include "Components/SceneComponent.h"

AOutlierCinematicRegistry::AOutlierCinematicRegistry()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}
