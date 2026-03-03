#include "stdafx.h"
#include "AetherGrassTile.h"
#include "AetherNaturalTile.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.item.enchantment.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.h"
#include "Facing.h"
#include "IconRegister.h"

AetherGrassTile::AetherGrassTile(int id) : Tile(id, Material::grass)
{
	iconTop = NULL;
	iconSide = NULL;
	setTicking(true);
}

Icon *AetherGrassTile::getTexture(int face, int data)
{
	if (face == Facing::UP) return iconTop;
	if (face == Facing::DOWN) return Tile::aetherDirt->getTexture(face);
	return iconSide;
}

Icon *AetherGrassTile::getTexture(LevelSource *level, int x, int y, int z, int face)
{
	if (face == Facing::UP) return iconTop;
	if (face == Facing::DOWN) return Tile::aetherDirt->getTexture(face);
	return iconSide;
}

void AetherGrassTile::registerIcons(IconRegister *iconRegister)
{
	iconSide = iconRegister->registerIcon(L"AetherGrassSide");
	iconTop = iconRegister->registerIcon(L"AetherGrassTop");
}

void AetherGrassTile::tick(Level *level, int x, int y, int z, Random *random)
{
	if (level->isClientSide) return;

	// Spread to adjacent aether dirt
	if (level->getRawBrightness(x, y + 1, z) >= Level::MAX_BRIGHTNESS - 6)
	{
		for (int i = 0; i < 4; i++)
		{
			int xt = x + random->nextInt(3) - 1;
			int yt = y + random->nextInt(5) - 3;
			int zt = z + random->nextInt(3) - 1;
			int above = level->getTile(xt, yt + 1, zt);
			if (level->getTile(xt, yt, zt) == Tile::aetherDirt_Id && level->getRawBrightness(xt, yt + 1, zt) >= MIN_BRIGHTNESS && Tile::lightBlock[above] <= 2)
			{
				level->setTile(xt, yt, zt, Tile::aetherGrass_Id);
			}
		}
	}
}

int AetherGrassTile::getResource(int data, Random *random, int playerBonusLevel)
{
	return Tile::aetherDirt_Id;
}

bool AetherGrassTile::shouldTileTick(Level *level, int x, int y, int z)
{
	return (level->getRawBrightness(x, y + 1, z) >= Level::MAX_BRIGHTNESS - 6);
}

void AetherGrassTile::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by)
{
	// Mark as player-placed so skyroot tools won't double-drop
	int data = level->getData(x, y, z);
	level->setData(x, y, z, data | AetherNaturalTile::PLAYER_PLACED_BIT);
}

void AetherGrassTile::playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data)
{
	int cleanData = data & ~AetherNaturalTile::PLAYER_PLACED_BIT;
	bool isPlayerPlaced = (data & AetherNaturalTile::PLAYER_PLACED_BIT) != 0;

	Tile::playerDestroy(level, player, x, y, z, cleanData);

	if (!isPlayerPlaced && AetherNaturalTile::isSkyrootTool(player))
	{
		AetherNaturalTile::spawnSkyrootBonusDrops(this, level, player, x, y, z, cleanData);
	}
}
