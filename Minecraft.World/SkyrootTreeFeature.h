#pragma once
#include "Feature.h"

class SkyrootTreeFeature : public Feature
{
private:
	const int baseHeight;

public:
	SkyrootTreeFeature(bool doUpdate);
	SkyrootTreeFeature(bool doUpdate, int baseHeight);

	virtual bool place(Level *level, Random *random, int x, int y, int z);
};
