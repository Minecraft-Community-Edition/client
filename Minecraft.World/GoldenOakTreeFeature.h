#pragma once
#include "Feature.h"

class GoldenOakTreeFeature : public Feature
{
public:
	GoldenOakTreeFeature(bool doUpdate);

	virtual bool place(Level *level, Random *random, int x, int y, int z);
};
