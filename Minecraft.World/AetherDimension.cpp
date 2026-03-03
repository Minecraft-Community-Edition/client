#include "stdafx.h"
#include "AetherDimension.h"
#include "FixedBiomeSource.h"
#include "LevelData.h"
#include "net.minecraft.world.level.levelgen.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.level.biome.h"
#include "AetherLevelSource.h"
#include "..\Minecraft.Client\Minecraft.h"
#include "..\Minecraft.Client\Common\Colours\ColourTable.h"

void AetherDimension::init()
{
	// Use the Aether biome for the sky dimension
	biomeSource = new FixedBiomeSource(Biome::aether, 0.5f, 0.0f);
	id = 2;
	hasCeiling = false;
}

ChunkSource *AetherDimension::createRandomLevelSource() const
{
	// Floating island terrain generation for the Aether
	return new AetherLevelSource(level, level->getSeed());
}

float AetherDimension::getTimeOfDay(__int64 time, float a) const
{
	// Permanent daytime — 0.0 gives maximum brightness (skyDarken = 0)
	return 0.0f;
}

float *AetherDimension::getSunriseColor(float td, float a)
{
	// No sunrise/sunset cycle in the Aether
	return NULL;
}

Vec3 *AetherDimension::getFogColor(float td, float a) const
{
	// Bright sky-blue fog color for the Aether
	float r = 0.62f;
	float g = 0.80f;
	float b = 1.0f;
	return Vec3::newTemp(r, g, b);
}

bool AetherDimension::hasGround()
{
	// The Aether has ground (sky islands with void below, but ground exists)
	return true;
}

bool AetherDimension::mayRespawn() const
{
	// Cannot respawn in the Aether — sent back to Overworld on death
	return false;
}

bool AetherDimension::isNaturalDimension()
{
	// Not a natural dimension (no day/night cycle mob spawning rules)
	return false;
}

float AetherDimension::getCloudHeight()
{
	// Clouds are higher in the Aether
	return (float)Level::genDepth + 32;
}

bool AetherDimension::isValidSpawn(int x, int z) const
{
	int topTile = level->getTopTile(x, z);
	if (topTile == 0) return false;
	return Tile::tiles[topTile]->material->blocksMotion();
}

Pos *AetherDimension::getSpawnPos()
{
	return new Pos(0, 64, 0);
}

bool AetherDimension::isFoggyAt(int x, int z)
{
	return false;
}

int AetherDimension::getSpawnYPosition()
{
	return 64;
}
