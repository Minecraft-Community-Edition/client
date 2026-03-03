#pragma once
#include "BiomeDecorator.h"

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

	virtual void decorate();
	void decorateAetherOres();
};
