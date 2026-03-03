#pragma once
#include "Tile.h"

class Player;

// Simple tile for natural Aether terrain blocks (aether dirt, holystone, mossy holystone)
// Handles the player-placed bit and skyroot double-drop mechanic.
class AetherNaturalTile : public Tile
{
	friend class Tile;
public:
	// Bit used to mark a block as player-placed (prevents skyroot double drops)
	// For non-log Aether blocks, bit 3 is free (no data usage)
	static const int PLAYER_PLACED_BIT = 0x8;

	// For log blocks, bit 0 is free (bits 2-3 used for rotation)
	static const int PLAYER_PLACED_BIT_LOG = 0x1;

	// Returns true if the player is currently holding a skyroot tool
	static bool isSkyrootTool(shared_ptr<Player> player);

	// Spawns an extra set of drops for the skyroot double-drop bonus.
	// Call this after the normal playerDestroy has already run.
	static void spawnSkyrootBonusDrops(Tile *tile, Level *level, shared_ptr<Player> player, int x, int y, int z, int data);

protected:
	AetherNaturalTile(int id, Material *material);

public:
	virtual void setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by);
	virtual void playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data);
};
