#pragma once
#include "Feature.h"

class AerCloudFeature : public Feature
{
public:
	// tileId: which aercloud tile to place (aercloud, gold, blue)
	// minRadius/maxRadius: horizontal radius range for the cloud blob
	// minHeight/maxHeight: vertical thickness range
	// islandBottom: if true, generates large clouds at island undersides; if false, generates floating sky clouds
	AerCloudFeature(int tileId, int minRadius, int maxRadius, int minHeight, int maxHeight, bool islandBottom);

	virtual bool place(Level *level, Random *random, int x, int y, int z);

private:
	int tileId;
	int minRadius;
	int maxRadius;
	int minHeight;
	int maxHeight;
	bool islandBottom;

	// Generate an ellipsoidal cloud blob centered at (cx, cy, cz)
	void generateCloudBlob(Level *level, Random *random, int cx, int cy, int cz, int rx, int rz, int ry);

	// Find the bottom of an island by scanning down from y
	int findIslandBottom(Level *level, int x, int startY, int z);
};
