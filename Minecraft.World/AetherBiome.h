#pragma once
#include "Biome.h"

class AetherBiome : public Biome
{
public:
	AetherBiome(int id);

	virtual Feature *getTreeFeature(Random *random);
	virtual Feature *getGrassFeature(Random *random);
	virtual int getGrassColor();
	virtual int getFolageColor();
};
