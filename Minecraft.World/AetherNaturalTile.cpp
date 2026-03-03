#include "stdafx.h"
#include "AetherNaturalTile.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.item.enchantment.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.level.h"

AetherNaturalTile::AetherNaturalTile(int id, Material *material) : Tile(id, material)
{
}

bool AetherNaturalTile::isSkyrootTool(shared_ptr<Player> player)
{
	shared_ptr<ItemInstance> held = player->inventory->getSelected();
	if (held == NULL) return false;

	int itemId = held->id;
	return itemId == Item::sword_skyroot_Id ||
		   itemId == Item::shovel_skyroot_Id ||
		   itemId == Item::pickAxe_skyroot_Id ||
		   itemId == Item::hatchet_skyroot_Id;
}

void AetherNaturalTile::spawnSkyrootBonusDrops(Tile *tile, Level *level, shared_ptr<Player> player, int x, int y, int z, int data)
{
	// Don't double silk touch drops
	if (EnchantmentHelper::hasSilkTouch(player->inventory))
		return;

	int playerBonusLevel = EnchantmentHelper::getDiggingLootBonus(player->inventory);
	tile->spawnResources(level, x, y, z, data, playerBonusLevel);
}

void AetherNaturalTile::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by)
{
	// Mark as player-placed so skyroot tools won't double-drop
	int data = level->getData(x, y, z);
	level->setData(x, y, z, data | PLAYER_PLACED_BIT);
}

void AetherNaturalTile::playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data)
{
	// Strip the player-placed bit before passing to base class
	int cleanData = data & ~PLAYER_PLACED_BIT;
	bool isPlayerPlaced = (data & PLAYER_PLACED_BIT) != 0;

	// Normal drop logic
	Tile::playerDestroy(level, player, x, y, z, cleanData);

	// Skyroot double-drop bonus on natural blocks only
	if (!isPlayerPlaced && isSkyrootTool(player))
	{
		spawnSkyrootBonusDrops(this, level, player, x, y, z, cleanData);
	}
}
