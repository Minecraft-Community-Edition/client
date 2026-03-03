#include "stdafx.h"
#include "AetherOreTile.h"
#include "AetherNaturalTile.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.item.enchantment.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.level.h"

AetherOreTile::AetherOreTile(int id) : Tile(id, Material::stone)
{
}

int AetherOreTile::getResource(int data, Random *random, int playerBonusLevel)
{
	// Ambrosium ore drops ambrosium shard (item), zanite ore drops zanite gemstone (item)
	// Gravitite ore drops itself (needs smelting)
	if (id == Tile::ambrosiumOre_Id) return Item::ambrosiumShard_Id;
	if (id == Tile::zaniteOre_Id) return Item::zaniteGemstone_Id;
	return id; // gravitite drops itself
}

int AetherOreTile::getResourceCount(Random *random)
{
	return 1;
}

void AetherOreTile::spawnResources(Level *level, int x, int y, int z, int data, float odds, int playerBonusLevel)
{
	Tile::spawnResources(level, x, y, z, data, odds, playerBonusLevel);

	if (getResource(data, level->random, playerBonusLevel) != id)
	{
		int xpCount = 0;
		if (id == Tile::ambrosiumOre_Id)
		{
			xpCount = Mth::nextInt(level->random, 0, 2);
		}
		else if (id == Tile::zaniteOre_Id)
		{
			xpCount = Mth::nextInt(level->random, 2, 5);
		}
		popExperience(level, x, y, z, xpCount);
	}
}

void AetherOreTile::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by)
{
	// Mark as player-placed so skyroot tools won't double-drop
	int data = level->getData(x, y, z);
	level->setData(x, y, z, data | AetherNaturalTile::PLAYER_PLACED_BIT);
}

void AetherOreTile::playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data)
{
	int cleanData = data & ~AetherNaturalTile::PLAYER_PLACED_BIT;
	bool isPlayerPlaced = (data & AetherNaturalTile::PLAYER_PLACED_BIT) != 0;

	Tile::playerDestroy(level, player, x, y, z, cleanData);

	if (!isPlayerPlaced && AetherNaturalTile::isSkyrootTool(player))
	{
		AetherNaturalTile::spawnSkyrootBonusDrops(this, level, player, x, y, z, cleanData);
	}
}
