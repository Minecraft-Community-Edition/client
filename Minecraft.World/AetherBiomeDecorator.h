#pragma once
#include "BiomeDecorator.h"

class AerCloudFeature;

class AetherBiomeDecorator : public BiomeDecorator
{
public:
	AetherBiomeDecorator(Biome *biome);

protected:
	// Aether ore features (replace holystone instead of stone)
	Feature *ambrosiumOreFeature;
	Feature *zaniteOreFeature;
	Feature *gravititeOreFeature;

	// Quicksoil shelf feature for island undersides
	Feature *quicksoilShelfFeature;

	// AerCloud features
	Feature *largeAerCloudFeature;      // Big cloud clumps at island undersides
	Feature *smallAerCloudFeature;      // Small white clouds in the sky
	Feature *smallGoldAerCloudFeature;  // Small gold clouds (rare)
	Feature *smallBlueAerCloudFeature;  // Small blue clouds (rarest)

	virtual void decorate();
	void decorateAetherOres();
};
