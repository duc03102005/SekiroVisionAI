# Augmentation and temporal evidence

Apply training augmentation in the `Training/` dataset loader, after selecting a
causal sequence. Keep the source media, PTS map, reviewed annotations and splits
immutable. Color/compression/blur/resolution/crop perturbations must be consistent
throughout a sequence. Camera jitter or synthetic particles are augmentation
metadata, never new verified attack/contact observations.

Do not change speed or discard/duplicate observations without recording their
mapping and rescaling any affected timing labels. Do not train exact TTI from a
no-hit counterfactual interval. Horizontal flips require direction remapping;
an observed flipped response still is not a verified safe-action target. HUD
variation should hide uploader/HUD shortcuts without erasing the boss/weapon
trajectory needed to judge the annotated event.
