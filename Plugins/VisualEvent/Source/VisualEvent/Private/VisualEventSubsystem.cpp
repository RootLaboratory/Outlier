// Fill out your copyright notice in the Description page of Project Settings.


#include "VisualEventSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectionMarkDefinition.h"

void UVisualEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Error, TEXT("WORLDSUBSYSTEM Initalize"));
}

void UVisualEventSubsystem::Deinitialize()
{
}

void UVisualEventSubsystem::SpawnMarkAtLocation(UProjectionMarkDefinition* Def, FVector Location, FRotator Rotation)
{
    if (!Def || !Def->DecalMaterial)
    {
        UE_LOG(LogTemp, Error, TEXT("NO DECAL "));

        return;
    }

    UE_LOG(LogTemp, Error, TEXT(" DECAL WORKS "));


    UGameplayStatics::SpawnDecalAtLocation(
        GetWorld(),
        Def->DecalMaterial,
        Def->DecalSize,
        Location,
        Rotation + Def->RotationOffset,
        Def->LifeSpan
    );
}

void UVisualEventSubsystem::SpawnMarkFromHit(UProjectionMarkDefinition* Definition, const FHitResult& HitResult)
{
}

void UVisualEventSubsystem::SpawnFXAtLocation(UProjectionMarkDefinition* Definition, FVector Location, FRotator Rotation)
{
}

void UVisualEventSubsystem::PlaySoundAtLocation(UProjectionMarkDefinition* Definition, FVector Location)
{
}
