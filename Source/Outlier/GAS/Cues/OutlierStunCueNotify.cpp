#include "GAS/Cues/OutlierStunCueNotify.h"

#include "GameplayTags/OutlierGameplayTags.h"

AOutlierStunCueNotify::AOutlierStunCueNotify()
{
	DeriveGameplayCueTagFromAssetName();

	// Different stun sources share one visual instance on the affected actor.
	bUniqueInstancePerInstigator = false;
	bUniqueInstancePerSourceObject = false;
	bAllowMultipleOnActiveEvents = false;
	bAllowMultipleWhileActiveEvents = false;
	bAutoAttachToOwner = true;
	bAutoDestroyOnRemove = true;
	DefaultPlacementInfo.AttachPolicy = EGameplayCueNotify_AttachPolicy::AttachToTarget;
}

void AOutlierStunCueNotify::DeriveGameplayCueTagFromAssetName()
{
	// UE's asset-name fallback restores an inherited tag without restoring its
	// registry name. This cue always represents the shared Stun GE, regardless
	// of the Blueprint's name, so keep both values synchronized.
	GameplayCueTag = OutlierGameplayTags::Cue::Status::Stun();
	GameplayCueName = GameplayCueTag.GetTagName();
}
