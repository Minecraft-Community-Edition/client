#include "stdafx.h"
#include "AetherBiomeDecorator.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.level.levelgen.feature.h"
#include "net.minecraft.world.level.biome.h"
#include "QuicksoilShelfFeature.h"
#include "AerCloudFeature.h"

AetherBiomeDecorator::AetherBiomeDecorator(Biome *biome) : BiomeDecorator(biome)
{
	// Aether ores replace holystone instead of stone
	ambrosiumOreFeature = new OreFeature(Tile::ambrosiumOre_Id, 16, Tile::holystone_Id);
	zaniteOreFeature = new OreFeature(Tile::zaniteOre_Id, 8, Tile::holystone_Id);
	gravititeOreFeature = new OreFeature(Tile::gravititeOre_Id, 4, Tile::holystone_Id);

	// Quicksoil shelves on island undersides
	quicksoilShelfFeature = new QuicksoilShelfFeature();

	// AerCloud features
	// Large clouds at island bottoms: radius 6-10, height 2-4, island-bottom mode
	largeAerCloudFeature = new AerCloudFeature(Tile::aercloud_Id, 6, 10, 2, 4, true);
	// Small white sky clouds: radius 3-6, height 1-2, free-floating
	smallAerCloudFeature = new AerCloudFeature(Tile::aercloud_Id, 3, 6, 1, 2, false);
	// Small gold sky clouds: radius 2-4, height 1-2, free-floating
	smallGoldAerCloudFeature = new AerCloudFeature(Tile::goldAercloud_Id, 2, 4, 1, 2, false);
	// Small blue sky clouds: radius 2-4, height 1-2, free-floating
	smallBlueAerCloudFeature = new AerCloudFeature(Tile::blueAercloud_Id, 2, 4, 1, 2, false);

	// Aether decoration counts
	treeCount = 2;
	grassCount = 5;
	flowerCount = 2;

	// Disable overworld-specific features
	sandCount = 0;
	clayCount = 0;
	gravelCount = 0;
	deadBushCount = 0;
	mushroomCount = 0;
	reedsCount = 0;
	cactusCount = 0;
	waterlilyCount = 0;
	hugeMushrooms = 0;
	liquids = false;
}

void AetherBiomeDecorator::decorate()
{
	PIXBeginNamedEvent(0, "Decorate Aether ores");
	decorateAetherOres();
	PIXEndNamedEvent();

	PIXBeginNamedEvent(0, "Decorate Aether forests");
	int forests = treeCount;
	if (random->nextInt(10) == 0) forests += 1;

	for (int i = 0; i < forests; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int z = zo + random->nextInt(16) + 8;
		Feature *tree = biome->getTreeFeature(random);
		tree->init(1, 1, 1);
		tree->place(level, random, x, level->getHeightmap(x, z), z);
		delete tree;
	}
	PIXEndNamedEvent();

	PIXBeginNamedEvent(0, "Decorate Aether flowers/grass");
	for (int i = 0; i < flowerCount; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int y = random->nextInt(Level::genDepth);
		int z = zo + random->nextInt(16) + 8;
		yellowFlowerFeature->place(level, random, x, y, z);
	}

	for (int i = 0; i < grassCount; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int y = random->nextInt(Level::genDepth);
		int z = zo + random->nextInt(16) + 8;
		MemSect(50);
		Feature *grassFeature = biome->getGrassFeature(random);
		MemSect(0);
		grassFeature->place(level, random, x, y, z);
		delete grassFeature;
	}
	PIXEndNamedEvent();

	PIXBeginNamedEvent(0, "Decorate Aether quicksoil shelves");
	// Place quicksoil shelves on the undersides of islands
	for (int i = 0; i < 3; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int z = zo + random->nextInt(16) + 8;
		int y = level->getHeightmap(x, z);
		if (y > 0)
		{
			quicksoilShelfFeature->place(level, random, x, y, z);
		}
	}
	PIXEndNamedEvent();

	PIXBeginNamedEvent(0, "Decorate Aether clouds");

	// Large aercloud formations at island undersides — gives players a safety net when falling (1 in 5 chunks)
	if (random->nextInt(5) == 0)
	{
		int x = xo + random->nextInt(16) + 8;
		int z = zo + random->nextInt(16) + 8;
		int y = level->getHeightmap(x, z);
		if (y > 0)
		{
			largeAerCloudFeature->place(level, random, x, y, z);
		}
	}

	// Sky clouds spawn at y=80 or higher
	const int minCloudY = 80;

	// Small white aerclouds floating in the sky (1 in 10 chunks)
	if (random->nextInt(10) == 0)
	{
		int x = xo + random->nextInt(16) + 8;
		int z = zo + random->nextInt(16) + 8;
		int y = minCloudY + random->nextInt(Level::genDepth - 10 - minCloudY);
		smallAerCloudFeature->place(level, random, x, y, z);
	}

	// Small gold aerclouds — rare (1 in 30 chunks)
	if (random->nextInt(30) == 0)
	{
		int x = xo + random->nextInt(16) + 8;
		int z = zo + random->nextInt(16) + 8;
		int y = minCloudY + random->nextInt(Level::genDepth - 10 - minCloudY);
		smallGoldAerCloudFeature->place(level, random, x, y, z);
	}

	// Small blue aerclouds — rarest (1 in 60 chunks)
	if (random->nextInt(60) == 0)
	{
		int x = xo + random->nextInt(16) + 8;
		int z = zo + random->nextInt(16) + 8;
		int y = minCloudY + random->nextInt(Level::genDepth - 10 - minCloudY);
		smallBlueAerCloudFeature->place(level, random, x, y, z);
	}

	PIXEndNamedEvent();
}

void AetherBiomeDecorator::decorateAetherOres()
{
	level->setInstaTick(true);
	// Ambrosium: common, full height range
	decorateDepthSpan(20, ambrosiumOreFeature, 0, Level::genDepth);
	// Zanite: moderate, lower half
	decorateDepthSpan(10, zaniteOreFeature, 0, Level::genDepth / 2);
	// Gravitite: rare, bottom quarter
	decorateDepthSpan(4, gravititeOreFeature, 0, Level::genDepth / 4);
	level->setInstaTick(false);
}
