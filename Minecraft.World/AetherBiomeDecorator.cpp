#include "stdafx.h"
#include "AetherBiomeDecorator.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.level.levelgen.feature.h"
#include "net.minecraft.world.level.biome.h"
#include "QuicksoilShelfFeature.h"

AetherBiomeDecorator::AetherBiomeDecorator(Biome *biome) : BiomeDecorator(biome)
{
	// Aether ores replace holystone instead of stone
	ambrosiumOreFeature = new OreFeature(Tile::ambrosiumOre_Id, 16, Tile::holystone_Id);
	zaniteOreFeature = new OreFeature(Tile::zaniteOre_Id, 8, Tile::holystone_Id);
	gravititeOreFeature = new OreFeature(Tile::gravititeOre_Id, 4, Tile::holystone_Id);

	// Quicksoil shelves on island undersides
	quicksoilShelfFeature = new QuicksoilShelfFeature();

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
