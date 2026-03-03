#pragma once
#include "Feature.h"

class QuicksoilShelfFeature : public Feature
{
public:
	QuicksoilShelfFeature();

	virtual bool place(Level *level, Random *random, int x, int y, int z);
};
