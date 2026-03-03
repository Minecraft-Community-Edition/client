#include "stdafx.h"
#include "SkyrootLogTile.h"
#include "AetherNaturalTile.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.item.enchantment.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.h"
#include "PistonBaseTile.h"
#include "Facing.h"
#include "IconRegister.h"

SkyrootLogTile::SkyrootLogTile(int id, const wstring &sideTexture, const wstring &topTexture) : Tile(id, Material::wood)
{
	iconSide = NULL;
	iconTop = NULL;
	sideTextureName = sideTexture;
	topTextureName = topTexture;
}

int SkyrootLogTile::getRenderShape()
{
	return Tile::SHAPE_TREE;
}

Icon *SkyrootLogTile::getTexture(int face, int data)
{
	int dir = data & MASK_FACING;
	if (dir == FACING_Y && (face == Facing::UP || face == Facing::DOWN)) return iconTop;
	else if (dir == FACING_X && (face == Facing::EAST || face == Facing::WEST)) return iconTop;
	else if (dir == FACING_Z && (face == Facing::NORTH || face == Facing::SOUTH)) return iconTop;
	return iconSide;
}

void SkyrootLogTile::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by)
{
	int dir = PistonBaseTile::getNewFacing(level, x, y, z, dynamic_pointer_cast<Player>(by));
	int facing = 0;
	switch (dir)
	{
		case Facing::NORTH: case Facing::SOUTH: facing = FACING_Z; break;
		case Facing::EAST:  case Facing::WEST:  facing = FACING_X; break;
		case Facing::UP:    case Facing::DOWN:   facing = FACING_Y; break;
	}
	// Also mark as player-placed for skyroot double-drop prevention
	level->setData(x, y, z, facing | PLAYER_PLACED_BIT);
}

void SkyrootLogTile::playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data)
{
	int cleanData = data & ~PLAYER_PLACED_BIT;
	bool isPlayerPlaced = (data & PLAYER_PLACED_BIT) != 0;

	Tile::playerDestroy(level, player, x, y, z, cleanData);

	if (!isPlayerPlaced && AetherNaturalTile::isSkyrootTool(player))
	{
		AetherNaturalTile::spawnSkyrootBonusDrops(this, level, player, x, y, z, cleanData);
	}
}

void SkyrootLogTile::registerIcons(IconRegister *iconRegister)
{
	iconSide = iconRegister->registerIcon(sideTextureName.c_str());
	iconTop = iconRegister->registerIcon(topTextureName.c_str());
}

int SkyrootLogTile::getSpawnResourcesAuxValue(int data)
{
	return 0;
}
