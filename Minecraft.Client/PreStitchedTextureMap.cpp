#include "stdafx.h"
#include "..\Minecraft.World\net.minecraft.world.h"
#include "..\Minecraft.World\net.minecraft.world.level.tile.h"
#include "..\Minecraft.World\net.minecraft.world.item.h"
#include "..\Minecraft.World\ByteBuffer.h"
#include "Minecraft.h"
#include "LevelRenderer.h"
#include "EntityRenderDispatcher.h"
#include "Stitcher.h"
#include "StitchSlot.h"
#include "StitchedTexture.h"
#include "Texture.h"
#include "TextureHolder.h"
#include "TextureManager.h"
#include "TexturePack.h"
#include "TexturePackRepository.h"
#include "PreStitchedTextureMap.h"
#include "SimpleIcon.h"
#include "CompassTexture.h"
#include "ClockTexture.h"

const wstring PreStitchedTextureMap::NAME_MISSING_TEXTURE = L"missingno";

PreStitchedTextureMap::PreStitchedTextureMap(int type, const wstring &name, const wstring &path, BufferedImage *missingTexture, bool mipmap) : iconType(type), name(name), path(path), extension(L".png")
{
	this->missingTexture = missingTexture;

	// 4J Initialisers
	missingPosition = NULL;
	stitchResult = NULL;

	m_mipMap = mipmap;
	missingPosition = (StitchedTexture *)(new SimpleIcon(NAME_MISSING_TEXTURE,0,0,1,1));
}

void PreStitchedTextureMap::stitch()
{
	// Animated StitchedTextures store a vector of textures for each frame of the animation. Free any pre-existing ones here.
	for(AUTO_VAR(it, texturesToAnimate.begin()); it != texturesToAnimate.end(); ++it)
	{
		StitchedTexture *animatedStitchedTexture = (StitchedTexture *)texturesByName[it->first];
		animatedStitchedTexture->freeFrameTextures();
	}

	loadUVs();

	if (iconType == Icon::TYPE_TERRAIN)
	{
		//for (Tile tile : Tile.tiles)
		for(unsigned int i = 0; i < Tile::TILE_NUM_COUNT; ++i)
		{
			if (Tile::tiles[i] != NULL)
			{
				Tile::tiles[i]->registerIcons(this);
			}
		}

		Minecraft::GetInstance()->levelRenderer->registerTextures(this);
		EntityRenderDispatcher::instance->registerTerrainTextures(this);
	}

	//for (Item item : Item.items)
	for(unsigned int i = 0; i < Item::ITEM_NUM_COUNT; ++i)
	{
		Item *item = Item::items[i];
		if (item != NULL && item->getIconType() == iconType)
		{
			item->registerIcons(this);
		}
	}

	// Collection bucket for multiple frames per texture
	unordered_map<TextureHolder *, vector<Texture *> * > textures; // = new HashMap<TextureHolder, List<Texture>>();

	Stitcher *stitcher = TextureManager::getInstance()->createStitcher(name);

	animatedTextures.clear();

	// Create the final image
	wstring filename = name + extension;

	TexturePack *texturePack = Minecraft::GetInstance()->skins->getSelected();
	//try {
	int mode = Texture::TM_DYNAMIC;
	int clamp = Texture::WM_WRAP; // 4J Stu - Don't clamp as it causes issues with how we signal non-mipmmapped textures to the pixel shader //Texture::WM_CLAMP;
	int minFilter = Texture::TFLT_NEAREST;
	int magFilter = Texture::TFLT_NEAREST;

	MemSect(32);
	wstring drive = L"";
	if(texturePack->hasFile(L"res/" + filename,false))
	{
		drive = texturePack->getPath(true);
	}
	else
	{
		drive = Minecraft::GetInstance()->skins->getDefault()->getPath(true);
		texturePack = Minecraft::GetInstance()->skins->getDefault();
	}
	//BufferedImage *image = new BufferedImage(texturePack->getResource(L"/" + filename),false,true,drive); //ImageIO::read(texturePack->getResource(L"/" + filename));
	BufferedImage *image = texturePack->getImageResource(filename, false, true, drive);
	MemSect(0);
	int height = image->getHeight();
	int width = image->getWidth();

	if(stitchResult != NULL)
	{
		TextureManager::getInstance()->unregisterTexture(name, stitchResult);
		delete stitchResult;
	}
	stitchResult = TextureManager::getInstance()->createTexture(name, Texture::TM_DYNAMIC, width, height, Texture::TFMT_RGBA, m_mipMap);
	stitchResult->transferFromImage(image);
	delete image;
	TextureManager::getInstance()->registerName(name, stitchResult);
	//stitchResult = stitcher->constructTexture(m_mipMap);

	for(AUTO_VAR(it, texturesByName.begin()); it != texturesByName.end(); ++it)
	{
		StitchedTexture *preStitched = (StitchedTexture *)it->second;

		int x = preStitched->getU0() * stitchResult->getWidth();
		int y = preStitched->getV0() * stitchResult->getHeight();
		int width = (preStitched->getU1() * stitchResult->getWidth()) - x;
		int height = (preStitched->getV1() * stitchResult->getHeight()) - y;

		preStitched->init(stitchResult, NULL, x, y, width, height, false);
	}

	MemSect(52);
	for(AUTO_VAR(it, texturesToAnimate.begin()); it != texturesToAnimate.end(); ++it)
	{
		wstring textureName = it->first;
		wstring textureFileName = it->second;

		StitchedTexture *preStitched = (StitchedTexture *)texturesByName[textureName];

		if(!preStitched->hasOwnData())
		{
			if(preStitched->getFrames() > 1) animatedTextures.push_back(preStitched);
			continue;
		}

		wstring filename = path + textureFileName + extension;

		// TODO: [EB] Put the frames into a proper object, not this inside out hack
		vector<Texture *> *frames = TextureManager::getInstance()->createTextures(filename, m_mipMap);
		if (frames == NULL || frames->empty())
		{
			continue; // Couldn't load a texture, skip it
		}

		Texture *first = frames->at(0);

#ifndef _CONTENT_PACKAGE
		if(first->getWidth() != preStitched->getWidth() || first->getHeight() != preStitched->getHeight())
		{
			__debugbreak();
		}
#endif

		preStitched->init(stitchResult, frames, preStitched->getX(), preStitched->getY(), first->getWidth(), first->getHeight(), false);

		if (frames->size() > 1)
		{
			animatedTextures.push_back(preStitched);

			wstring animString = texturePack->getAnimationString(textureFileName, path, true);

			preStitched->loadAnimationFrames(animString);
		}
	}
	MemSect(0);
	//missingPosition = (StitchedTexture *)texturesByName.find(NAME_MISSING_TEXTURE)->second;

	stitchResult->writeAsPNG(L"debug.stitched_" + name + L".png");
	stitchResult->updateOnGPU();


#ifdef __PSVITA__
	// AP - alpha cut out is expensive on vita so we mark which icons actually require it
	DWORD *data = (DWORD*) this->getStitchedTexture()->getData()->getBuffer();
	int Width = this->getStitchedTexture()->getWidth();
	int Height = this->getStitchedTexture()->getHeight();
	for(AUTO_VAR(it, texturesByName.begin()); it != texturesByName.end(); ++it)
	{
		StitchedTexture *preStitched = (StitchedTexture *)it->second;

		bool Found = false;
		int u0 = preStitched->getU0() * Width;
		int u1 = preStitched->getU1() * Width;
		int v0 = preStitched->getV0() * Height;
		int v1 = preStitched->getV1() * Height;

		// check all the texels for this icon. If ANY are transparent we mark it as 'cut out'
		for( int v = v0;v < v1; v+= 1 )
		{
			for( int u = u0;u < u1; u+= 1 )
			{
				// is this texel alpha value < 0.1
				if( (data[v * Width + u] & 0xff000000) < 0x20000000 )
				{
					// this texel is transparent. Mark the icon as such and bail
					preStitched->setFlags(Icon::IS_ALPHA_CUT_OUT);
					Found = true;
					break;
				}
			}

			if( Found )
			{
				// move onto the next icon
				break;
			}
		}
	}
#endif
}

StitchedTexture *PreStitchedTextureMap::getTexture(const wstring &name)
{
#ifndef _CONTENT_PACKAGE
	app.DebugPrintf("Not implemented!\n");
	__debugbreak();
#endif
	return NULL;
#if 0
	StitchedTexture *result = texturesByName.find(name)->second;
	if (result == NULL) result = missingPosition;
	return result;
#endif
}

void PreStitchedTextureMap::cycleAnimationFrames()
{
	//for (StitchedTexture texture : animatedTextures)
	for(AUTO_VAR(it, animatedTextures.begin() ); it != animatedTextures.end(); ++it)
	{
		StitchedTexture *texture = *it;
		texture->cycleFrames();
	}
}

Texture *PreStitchedTextureMap::getStitchedTexture()
{
	return stitchResult;
}

// 4J Stu - register is a reserved keyword in C++
Icon *PreStitchedTextureMap::registerIcon(const wstring &name)
{
	Icon *result = NULL;
	if (name.empty())
	{
		app.DebugPrintf("Don't register NULL\n");
#ifndef _CONTENT_PACKAGE
		__debugbreak();
#endif
		result = missingPosition;
		//new RuntimeException("Don't register null!").printStackTrace();
	}

	AUTO_VAR(it, texturesByName.find(name));
	if(it != texturesByName.end()) result = it->second;

	if (result == NULL)
	{
#ifndef _CONTENT_PACKAGE
		wprintf(L"Could not find uv data for icon %ls\n", name.c_str() );
		__debugbreak();
#endif
		result = missingPosition;
	}

	return result;
}

int PreStitchedTextureMap::getIconType()
{
	return iconType;
}

Icon *PreStitchedTextureMap::getMissingIcon()
{
	return missingPosition;
}

void PreStitchedTextureMap::loadUVs()
{
	if(!texturesByName.empty())
	{
		// 4J Stu - We only need to populate this once at the moment as we have hardcoded positions for each texture
		// If we ever load that dynamically, be aware that the Icon objects could currently be being used by the
		// GameRenderer::runUpdate thread
		return;
	}

	for(AUTO_VAR(it, texturesByName.begin()); it != texturesByName.end(); ++it)
	{
		delete it->second;
	}
	texturesByName.clear();
	texturesToAnimate.clear();

	float slotW = 1.0f/16.0f;
	float slotH;
	if(iconType != Icon::TYPE_TERRAIN)
	{
		slotH = 1.0f/24.0f; // items.png is 256x384 = 16x24 grid
		texturesByName.insert(stringIconMap::value_type(L"helmetCloth",new SimpleIcon(L"helmetCloth",slotW*0,slotH*0,slotW*(0+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"helmetChain",new SimpleIcon(L"helmetChain",slotW*1,slotH*0,slotW*(1+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"helmetIron",new SimpleIcon(L"helmetIron",slotW*2,slotH*0,slotW*(2+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"helmetDiamond",new SimpleIcon(L"helmetDiamond",slotW*3,slotH*0,slotW*(3+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"helmetGold",new SimpleIcon(L"helmetGold",slotW*4,slotH*0,slotW*(4+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"flintAndSteel",new SimpleIcon(L"flintAndSteel",slotW*5,slotH*0,slotW*(5+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"flint",new SimpleIcon(L"flint",slotW*6,slotH*0,slotW*(6+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"coal",new SimpleIcon(L"coal",slotW*7,slotH*0,slotW*(7+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"string",new SimpleIcon(L"string",slotW*8,slotH*0,slotW*(8+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"seeds",new SimpleIcon(L"seeds",slotW*9,slotH*0,slotW*(9+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"apple",new SimpleIcon(L"apple",slotW*10,slotH*0,slotW*(10+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"appleGold",new SimpleIcon(L"appleGold",slotW*11,slotH*0,slotW*(11+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"egg",new SimpleIcon(L"egg",slotW*12,slotH*0,slotW*(12+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"sugar",new SimpleIcon(L"sugar",slotW*13,slotH*0,slotW*(13+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"snowball",new SimpleIcon(L"snowball",slotW*14,slotH*0,slotW*(14+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"slot_empty_helmet",new SimpleIcon(L"slot_empty_helmet",slotW*15,slotH*0,slotW*(15+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"chestplateCloth",new SimpleIcon(L"chestplateCloth",slotW*0,slotH*1,slotW*(0+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"chestplateChain",new SimpleIcon(L"chestplateChain",slotW*1,slotH*1,slotW*(1+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"chestplateIron",new SimpleIcon(L"chestplateIron",slotW*2,slotH*1,slotW*(2+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"chestplateDiamond",new SimpleIcon(L"chestplateDiamond",slotW*3,slotH*1,slotW*(3+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"chestplateGold",new SimpleIcon(L"chestplateGold",slotW*4,slotH*1,slotW*(4+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"bow",new SimpleIcon(L"bow",slotW*5,slotH*1,slotW*(5+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"brick",new SimpleIcon(L"brick",slotW*6,slotH*1,slotW*(6+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"ingotIron",new SimpleIcon(L"ingotIron",slotW*7,slotH*1,slotW*(7+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"feather",new SimpleIcon(L"feather",slotW*8,slotH*1,slotW*(8+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"wheat",new SimpleIcon(L"wheat",slotW*9,slotH*1,slotW*(9+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"painting",new SimpleIcon(L"painting",slotW*10,slotH*1,slotW*(10+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"reeds",new SimpleIcon(L"reeds",slotW*11,slotH*1,slotW*(11+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"bone",new SimpleIcon(L"bone",slotW*12,slotH*1,slotW*(12+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"cake",new SimpleIcon(L"cake",slotW*13,slotH*1,slotW*(13+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"slimeball",new SimpleIcon(L"slimeball",slotW*14,slotH*1,slotW*(14+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"slot_empty_chestplate",new SimpleIcon(L"slot_empty_chestplate",slotW*15,slotH*1,slotW*(15+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"leggingsCloth",new SimpleIcon(L"leggingsCloth",slotW*0,slotH*2,slotW*(0+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"leggingsChain",new SimpleIcon(L"leggingsChain",slotW*1,slotH*2,slotW*(1+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"leggingsIron",new SimpleIcon(L"leggingsIron",slotW*2,slotH*2,slotW*(2+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"leggingsDiamond",new SimpleIcon(L"leggingsDiamond",slotW*3,slotH*2,slotW*(3+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"leggingsGold",new SimpleIcon(L"leggingsGold",slotW*4,slotH*2,slotW*(4+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"arrow",new SimpleIcon(L"arrow",slotW*5,slotH*2,slotW*(5+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"quiver",new SimpleIcon(L"quiver",slotW*6,slotH*2,slotW*(6+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"ingotGold",new SimpleIcon(L"ingotGold",slotW*7,slotH*2,slotW*(7+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"sulphur",new SimpleIcon(L"sulphur",slotW*8,slotH*2,slotW*(8+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"bread",new SimpleIcon(L"bread",slotW*9,slotH*2,slotW*(9+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"sign",new SimpleIcon(L"sign",slotW*10,slotH*2,slotW*(10+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"doorWood",new SimpleIcon(L"doorWood",slotW*11,slotH*2,slotW*(11+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"doorIron",new SimpleIcon(L"doorIron",slotW*12,slotH*2,slotW*(12+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"bed",new SimpleIcon(L"bed",slotW*13,slotH*2,slotW*(13+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"fireball",new SimpleIcon(L"fireball",slotW*14,slotH*2,slotW*(14+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"slot_empty_leggings",new SimpleIcon(L"slot_empty_leggings",slotW*15,slotH*2,slotW*(15+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"bootsCloth",new SimpleIcon(L"bootsCloth",slotW*0,slotH*3,slotW*(0+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"bootsChain",new SimpleIcon(L"bootsChain",slotW*1,slotH*3,slotW*(1+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"bootsIron",new SimpleIcon(L"bootsIron",slotW*2,slotH*3,slotW*(2+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"bootsDiamond",new SimpleIcon(L"bootsDiamond",slotW*3,slotH*3,slotW*(3+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"bootsGold",new SimpleIcon(L"bootsGold",slotW*4,slotH*3,slotW*(4+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"stick",new SimpleIcon(L"stick",slotW*5,slotH*3,slotW*(5+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"compass",new SimpleIcon(L"compass",slotW*6,slotH*3,slotW*(6+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"compassP0",new SimpleIcon(L"compassP0",slotW*7,slotH*14,slotW*(7+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"compassP1",new SimpleIcon(L"compassP1",slotW*8,slotH*14,slotW*(8+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"compassP2",new SimpleIcon(L"compassP2",slotW*9,slotH*14,slotW*(9+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"compassP3",new SimpleIcon(L"compassP3",slotW*10,slotH*14,slotW*(10+1),slotH*(14+1))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"compass",L"compass"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"compassP0",L"compass"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"compassP1",L"compass"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"compassP2",L"compass"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"compassP3",L"compass"));
		texturesByName.insert(stringIconMap::value_type(L"diamond",new SimpleIcon(L"diamond",slotW*7,slotH*3,slotW*(7+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"redstone",new SimpleIcon(L"redstone",slotW*8,slotH*3,slotW*(8+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"clay",new SimpleIcon(L"clay",slotW*9,slotH*3,slotW*(9+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"paper",new SimpleIcon(L"paper",slotW*10,slotH*3,slotW*(10+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"book",new SimpleIcon(L"book",slotW*11,slotH*3,slotW*(11+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"map",new SimpleIcon(L"map",slotW*12,slotH*3,slotW*(12+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"seeds_pumpkin",new SimpleIcon(L"seeds_pumpkin",slotW*13,slotH*3,slotW*(13+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"seeds_melon",new SimpleIcon(L"seeds_melon",slotW*14,slotH*3,slotW*(14+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"slot_empty_boots",new SimpleIcon(L"slot_empty_boots",slotW*15,slotH*3,slotW*(15+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordWood",new SimpleIcon(L"swordWood",slotW*0,slotH*4,slotW*(0+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordStone",new SimpleIcon(L"swordStone",slotW*1,slotH*4,slotW*(1+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordIron",new SimpleIcon(L"swordIron",slotW*2,slotH*4,slotW*(2+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordDiamond",new SimpleIcon(L"swordDiamond",slotW*3,slotH*4,slotW*(3+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordGold",new SimpleIcon(L"swordGold",slotW*4,slotH*4,slotW*(4+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"fishingRod",new SimpleIcon(L"fishingRod",slotW*5,slotH*4,slotW*(5+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"clock",new SimpleIcon(L"clock",slotW*6,slotH*4,slotW*(6+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"clockP0",new SimpleIcon(L"clockP0",slotW*11,slotH*14,slotW*(11+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"clockP1",new SimpleIcon(L"clockP1",slotW*12,slotH*14,slotW*(12+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"clockP2",new SimpleIcon(L"clockP2",slotW*13,slotH*14,slotW*(13+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"clockP3",new SimpleIcon(L"clockP3",slotW*14,slotH*14,slotW*(14+1),slotH*(14+1))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"clock",L"clock"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"clockP0",L"clock"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"clockP1",L"clock"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"clockP2",L"clock"));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"clockP3",L"clock"));
		texturesByName.insert(stringIconMap::value_type(L"bowl",new SimpleIcon(L"bowl",slotW*7,slotH*4,slotW*(7+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"mushroomStew",new SimpleIcon(L"mushroomStew",slotW*8,slotH*4,slotW*(8+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"yellowDust",new SimpleIcon(L"yellowDust",slotW*9,slotH*4,slotW*(9+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"bucket",new SimpleIcon(L"bucket",slotW*10,slotH*4,slotW*(10+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"bucketWater",new SimpleIcon(L"bucketWater",slotW*11,slotH*4,slotW*(11+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"bucketLava",new SimpleIcon(L"bucketLava",slotW*12,slotH*4,slotW*(12+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"milk",new SimpleIcon(L"milk",slotW*13,slotH*4,slotW*(13+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_black",new SimpleIcon(L"dyePowder_black",slotW*14,slotH*4,slotW*(14+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_gray",new SimpleIcon(L"dyePowder_gray",slotW*15,slotH*4,slotW*(15+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelWood",new SimpleIcon(L"shovelWood",slotW*0,slotH*5,slotW*(0+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelStone",new SimpleIcon(L"shovelStone",slotW*1,slotH*5,slotW*(1+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelIron",new SimpleIcon(L"shovelIron",slotW*2,slotH*5,slotW*(2+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelDiamond",new SimpleIcon(L"shovelDiamond",slotW*3,slotH*5,slotW*(3+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelGold",new SimpleIcon(L"shovelGold",slotW*4,slotH*5,slotW*(4+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"fishingRod_empty",new SimpleIcon(L"fishingRod_empty",slotW*5,slotH*5,slotW*(5+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"diode",new SimpleIcon(L"diode",slotW*6,slotH*5,slotW*(6+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"porkchopRaw",new SimpleIcon(L"porkchopRaw",slotW*7,slotH*5,slotW*(7+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"porkchopCooked",new SimpleIcon(L"porkchopCooked",slotW*8,slotH*5,slotW*(8+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"fishRaw",new SimpleIcon(L"fishRaw",slotW*9,slotH*5,slotW*(9+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"fishCooked",new SimpleIcon(L"fishCooked",slotW*10,slotH*5,slotW*(10+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"rottenFlesh",new SimpleIcon(L"rottenFlesh",slotW*11,slotH*5,slotW*(11+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"cookie",new SimpleIcon(L"cookie",slotW*12,slotH*5,slotW*(12+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"shears",new SimpleIcon(L"shears",slotW*13,slotH*5,slotW*(13+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_red",new SimpleIcon(L"dyePowder_red",slotW*14,slotH*5,slotW*(14+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_pink",new SimpleIcon(L"dyePowder_pink",slotW*15,slotH*5,slotW*(15+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeWood",new SimpleIcon(L"pickaxeWood",slotW*0,slotH*6,slotW*(0+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeStone",new SimpleIcon(L"pickaxeStone",slotW*1,slotH*6,slotW*(1+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeIron",new SimpleIcon(L"pickaxeIron",slotW*2,slotH*6,slotW*(2+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeDiamond",new SimpleIcon(L"pickaxeDiamond",slotW*3,slotH*6,slotW*(3+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeGold",new SimpleIcon(L"pickaxeGold",slotW*4,slotH*6,slotW*(4+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"bow_pull_0",new SimpleIcon(L"bow_pull_0",slotW*5,slotH*6,slotW*(5+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"carrotOnAStick",new SimpleIcon(L"carrotOnAStick",slotW*6,slotH*6,slotW*(6+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"leather",new SimpleIcon(L"leather",slotW*7,slotH*6,slotW*(7+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"saddle",new SimpleIcon(L"saddle",slotW*8,slotH*6,slotW*(8+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"beefRaw",new SimpleIcon(L"beefRaw",slotW*9,slotH*6,slotW*(9+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"beefCooked",new SimpleIcon(L"beefCooked",slotW*10,slotH*6,slotW*(10+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"enderPearl",new SimpleIcon(L"enderPearl",slotW*11,slotH*6,slotW*(11+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"blazeRod",new SimpleIcon(L"blazeRod",slotW*12,slotH*6,slotW*(12+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"melon",new SimpleIcon(L"melon",slotW*13,slotH*6,slotW*(13+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_green",new SimpleIcon(L"dyePowder_green",slotW*14,slotH*6,slotW*(14+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_lime",new SimpleIcon(L"dyePowder_lime",slotW*15,slotH*6,slotW*(15+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetWood",new SimpleIcon(L"hatchetWood",slotW*0,slotH*7,slotW*(0+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetStone",new SimpleIcon(L"hatchetStone",slotW*1,slotH*7,slotW*(1+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetIron",new SimpleIcon(L"hatchetIron",slotW*2,slotH*7,slotW*(2+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetDiamond",new SimpleIcon(L"hatchetDiamond",slotW*3,slotH*7,slotW*(3+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetGold",new SimpleIcon(L"hatchetGold",slotW*4,slotH*7,slotW*(4+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"bow_pull_1",new SimpleIcon(L"bow_pull_1",slotW*5,slotH*7,slotW*(5+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"potatoBaked",new SimpleIcon(L"potatoBaked",slotW*6,slotH*7,slotW*(6+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"potato",new SimpleIcon(L"potato",slotW*7,slotH*7,slotW*(7+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"carrots",new SimpleIcon(L"carrots",slotW*8,slotH*7,slotW*(8+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"chickenRaw",new SimpleIcon(L"chickenRaw",slotW*9,slotH*7,slotW*(9+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"chickenCooked",new SimpleIcon(L"chickenCooked",slotW*10,slotH*7,slotW*(10+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"ghastTear",new SimpleIcon(L"ghastTear",slotW*11,slotH*7,slotW*(11+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"goldNugget",new SimpleIcon(L"goldNugget",slotW*12,slotH*7,slotW*(12+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherStalkSeeds",new SimpleIcon(L"netherStalkSeeds",slotW*13,slotH*7,slotW*(13+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_brown",new SimpleIcon(L"dyePowder_brown",slotW*14,slotH*7,slotW*(14+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_yellow",new SimpleIcon(L"dyePowder_yellow",slotW*15,slotH*7,slotW*(15+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"hoeWood",new SimpleIcon(L"hoeWood",slotW*0,slotH*8,slotW*(0+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"hoeStone",new SimpleIcon(L"hoeStone",slotW*1,slotH*8,slotW*(1+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"hoeIron",new SimpleIcon(L"hoeIron",slotW*2,slotH*8,slotW*(2+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"hoeDiamond",new SimpleIcon(L"hoeDiamond",slotW*3,slotH*8,slotW*(3+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"hoeGold",new SimpleIcon(L"hoeGold",slotW*4,slotH*8,slotW*(4+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"bow_pull_2",new SimpleIcon(L"bow_pull_2",slotW*5,slotH*8,slotW*(5+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"potatoPoisonous",new SimpleIcon(L"potatoPoisonous",slotW*6,slotH*8,slotW*(6+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"minecart",new SimpleIcon(L"minecart",slotW*7,slotH*8,slotW*(7+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"boat",new SimpleIcon(L"boat",slotW*8,slotH*8,slotW*(8+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"speckledMelon",new SimpleIcon(L"speckledMelon",slotW*9,slotH*8,slotW*(9+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"fermentedSpiderEye",new SimpleIcon(L"fermentedSpiderEye",slotW*10,slotH*8,slotW*(10+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"spiderEye",new SimpleIcon(L"spiderEye",slotW*11,slotH*8,slotW*(11+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"potion",new SimpleIcon(L"potion",slotW*12,slotH*8,slotW*(12+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"glassBottle",new SimpleIcon(L"glassBottle",slotW*12,slotH*8,slotW*(12+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"potion_contents",new SimpleIcon(L"potion_contents",slotW*13,slotH*8,slotW*(13+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_blue",new SimpleIcon(L"dyePowder_blue",slotW*14,slotH*8,slotW*(14+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_lightBlue",new SimpleIcon(L"dyePowder_lightBlue",slotW*15,slotH*8,slotW*(15+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"helmetCloth_overlay",new SimpleIcon(L"helmetCloth_overlay",slotW*0,slotH*9,slotW*(0+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"comparator",new SimpleIcon(L"comparator",slotW*5,slotH*9,slotW*(5+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"carrotGolden",new SimpleIcon(L"carrotGolden",slotW*6,slotH*9,slotW*(6+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"minecartChest",new SimpleIcon(L"minecartChest",slotW*7,slotH*9,slotW*(7+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"pumpkinPie",new SimpleIcon(L"pumpkinPie",slotW*8,slotH*9,slotW*(8+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"monsterPlacer",new SimpleIcon(L"monsterPlacer",slotW*9,slotH*9,slotW*(9+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"potion_splash",new SimpleIcon(L"potion_splash",slotW*10,slotH*9,slotW*(10+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"eyeOfEnder",new SimpleIcon(L"eyeOfEnder",slotW*11,slotH*9,slotW*(11+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"cauldron",new SimpleIcon(L"cauldron",slotW*12,slotH*9,slotW*(12+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"blazePowder",new SimpleIcon(L"blazePowder",slotW*13,slotH*9,slotW*(13+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_purple",new SimpleIcon(L"dyePowder_purple",slotW*14,slotH*9,slotW*(14+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_magenta",new SimpleIcon(L"dyePowder_magenta",slotW*15,slotH*9,slotW*(15+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"chestplateCloth_overlay",new SimpleIcon(L"chestplateCloth_overlay",slotW*0,slotH*10,slotW*(0+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherbrick",new SimpleIcon(L"netherbrick",slotW*5,slotH*10,slotW*(5+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"minecartFurnace",new SimpleIcon(L"minecartFurnace",slotW*7,slotH*10,slotW*(7+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"monsterPlacer_overlay",new SimpleIcon(L"monsterPlacer_overlay",slotW*9,slotH*10,slotW*(9+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"ruby",new SimpleIcon(L"ruby",slotW*10,slotH*10,slotW*(10+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"expBottle",new SimpleIcon(L"expBottle",slotW*11,slotH*10,slotW*(11+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"brewingStand",new SimpleIcon(L"brewingStand",slotW*12,slotH*10,slotW*(12+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"magmaCream",new SimpleIcon(L"magmaCream",slotW*13,slotH*10,slotW*(13+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_cyan",new SimpleIcon(L"dyePowder_cyan",slotW*14,slotH*10,slotW*(14+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_orange",new SimpleIcon(L"dyePowder_orange",slotW*15,slotH*10,slotW*(15+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"leggingsCloth_overlay",new SimpleIcon(L"leggingsCloth_overlay",slotW*0,slotH*11,slotW*(0+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"minecartHopper",new SimpleIcon(L"minecartHopper",slotW*7,slotH*11,slotW*(7+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"hopper",new SimpleIcon(L"hopper",slotW*8,slotH*11,slotW*(8+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherStar",new SimpleIcon(L"netherStar",slotW*9,slotH*11,slotW*(9+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"emerald",new SimpleIcon(L"emerald",slotW*10,slotH*11,slotW*(10+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"writingBook",new SimpleIcon(L"writingBook",slotW*11,slotH*11,slotW*(11+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"writtenBook",new SimpleIcon(L"writtenBook",slotW*12,slotH*11,slotW*(12+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"flowerPot",new SimpleIcon(L"flowerPot",slotW*13,slotH*11,slotW*(13+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_silver",new SimpleIcon(L"dyePowder_silver",slotW*14,slotH*11,slotW*(14+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"dyePowder_white",new SimpleIcon(L"dyePowder_white",slotW*15,slotH*11,slotW*(15+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"bootsCloth_overlay",new SimpleIcon(L"bootsCloth_overlay",slotW*0,slotH*12,slotW*(0+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"minecartTnt",new SimpleIcon(L"minecartTnt",slotW*7,slotH*12,slotW*(7+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"fireworks",new SimpleIcon(L"fireworks",slotW*9,slotH*12,slotW*(9+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"fireworksCharge",new SimpleIcon(L"fireworksCharge",slotW*10,slotH*12,slotW*(10+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"fireworksCharge_overlay",new SimpleIcon(L"fireworksCharge_overlay",slotW*11,slotH*12,slotW*(11+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherquartz",new SimpleIcon(L"netherquartz",slotW*12,slotH*12,slotW*(12+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"emptyMap",new SimpleIcon(L"emptyMap",slotW*13,slotH*12,slotW*(13+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"frame",new SimpleIcon(L"frame",slotW*14,slotH*12,slotW*(14+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"enchantedBook",new SimpleIcon(L"enchantedBook",slotW*15,slotH*12,slotW*(15+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"skull_skeleton",new SimpleIcon(L"skull_skeleton",slotW*0,slotH*14,slotW*(0+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"skull_wither",new SimpleIcon(L"skull_wither",slotW*1,slotH*14,slotW*(1+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"skull_zombie",new SimpleIcon(L"skull_zombie",slotW*2,slotH*14,slotW*(2+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"skull_char",new SimpleIcon(L"skull_char",slotW*3,slotH*14,slotW*(3+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"skull_creeper",new SimpleIcon(L"skull_creeper",slotW*4,slotH*14,slotW*(4+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"dragonFireball",new SimpleIcon(L"dragonFireball",slotW*15,slotH*14,slotW*(15+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_13",new SimpleIcon(L"record_13",slotW*0,slotH*15,slotW*(0+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_cat",new SimpleIcon(L"record_cat",slotW*1,slotH*15,slotW*(1+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_blocks",new SimpleIcon(L"record_blocks",slotW*2,slotH*15,slotW*(2+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_chirp",new SimpleIcon(L"record_chirp",slotW*3,slotH*15,slotW*(3+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_far",new SimpleIcon(L"record_far",slotW*4,slotH*15,slotW*(4+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_mall",new SimpleIcon(L"record_mall",slotW*5,slotH*15,slotW*(5+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_mellohi",new SimpleIcon(L"record_mellohi",slotW*6,slotH*15,slotW*(6+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_stal",new SimpleIcon(L"record_stal",slotW*7,slotH*15,slotW*(7+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_strad",new SimpleIcon(L"record_strad",slotW*8,slotH*15,slotW*(8+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_ward",new SimpleIcon(L"record_ward",slotW*9,slotH*15,slotW*(9+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_11",new SimpleIcon(L"record_11",slotW*10,slotH*15,slotW*(10+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"record_where are we now",new SimpleIcon(L"record_where are we now",slotW*11,slotH*15,slotW*(11+1),slotH*(15+1))));

		// Aether Items - Row 16
		texturesByName.insert(stringIconMap::value_type(L"helmetZanite",new SimpleIcon(L"helmetZanite",slotW*0,slotH*16,slotW*(0+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"helmetGravitite",new SimpleIcon(L"helmetGravitite",slotW*1,slotH*16,slotW*(1+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"zanite",new SimpleIcon(L"zanite",slotW*2,slotH*16,slotW*(2+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"ambrosiumShard",new SimpleIcon(L"ambrosiumShard",slotW*3,slotH*16,slotW*(3+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"aechorPetal",new SimpleIcon(L"aechorPetal",slotW*4,slotH*16,slotW*(4+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"skyrootBucket",new SimpleIcon(L"skyrootBucket",slotW*5,slotH*16,slotW*(5+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"skyrootBucketWater",new SimpleIcon(L"skyrootBucketWater",slotW*6,slotH*16,slotW*(6+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"skyrootBucketMilk",new SimpleIcon(L"skyrootBucketMilk",slotW*7,slotH*16,slotW*(7+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"skyrootBucketPoison",new SimpleIcon(L"skyrootBucketPoison",slotW*8,slotH*16,slotW*(8+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloudParachute",new SimpleIcon(L"cloudParachute",slotW*9,slotH*16,slotW*(9+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloudStaff",new SimpleIcon(L"cloudStaff",slotW*10,slotH*16,slotW*(10+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"dartEnchanted",new SimpleIcon(L"dartEnchanted",slotW*11,slotH*16,slotW*(11+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"dartGolden",new SimpleIcon(L"dartGolden",slotW*12,slotH*16,slotW*(12+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"dartPoison",new SimpleIcon(L"dartPoison",slotW*13,slotH*16,slotW*(13+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"goldenAmber",new SimpleIcon(L"goldenAmber",slotW*14,slotH*16,slotW*(14+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"gummieSwet",new SimpleIcon(L"gummieSwet",slotW*15,slotH*16,slotW*(15+1),slotH*(16+1))));

		// Aether Items - Row 17
		texturesByName.insert(stringIconMap::value_type(L"chestplateZanite",new SimpleIcon(L"chestplateZanite",slotW*0,slotH*17,slotW*(0+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"chestplateGravitite",new SimpleIcon(L"chestplateGravitite",slotW*1,slotH*17,slotW*(1+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"flamingGemstone",new SimpleIcon(L"flamingGemstone",slotW*2,slotH*17,slotW*(2+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"skyrootStick",new SimpleIcon(L"skyrootStick",slotW*3,slotH*17,slotW*(3+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"repulsionShield",new SimpleIcon(L"repulsionShield",slotW*4,slotH*17,slotW*(4+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"regenerationStone",new SimpleIcon(L"regenerationStone",slotW*5,slotH*17,slotW*(5+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"pigSlayer",new SimpleIcon(L"pigSlayer",slotW*6,slotH*17,slotW*(6+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"natureStaff",new SimpleIcon(L"natureStaff",slotW*7,slotH*17,slotW*(7+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"moaEgg",new SimpleIcon(L"moaEgg",slotW*8,slotH*17,slotW*(8+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"lifeShard",new SimpleIcon(L"lifeShard",slotW*9,slotH*17,slotW*(9+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"lance",new SimpleIcon(L"lance",slotW*10,slotH*17,slotW*(10+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"dartShooterEnchanted",new SimpleIcon(L"dartShooterEnchanted",slotW*11,slotH*17,slotW*(11+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"dartShooter",new SimpleIcon(L"dartShooter",slotW*12,slotH*17,slotW*(12+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"dartShooterPoison",new SimpleIcon(L"dartShooterPoison",slotW*13,slotH*17,slotW*(13+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"goldenFeather",new SimpleIcon(L"goldenFeather",slotW*14,slotH*17,slotW*(14+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"aetherKey",new SimpleIcon(L"aetherKey",slotW*15,slotH*17,slotW*(15+1),slotH*(17+1))));

		// Aether Items - Row 18
		texturesByName.insert(stringIconMap::value_type(L"leggingsZanite",new SimpleIcon(L"leggingsZanite",slotW*0,slotH*18,slotW*(0+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"leggingsGravitite",new SimpleIcon(L"leggingsGravitite",slotW*1,slotH*18,slotW*(1+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"gravititePlate",new SimpleIcon(L"gravititePlate",slotW*2,slotH*18,slotW*(2+1),slotH*(18+1))));

		// Aether Items - Row 19
		texturesByName.insert(stringIconMap::value_type(L"bootsZanite",new SimpleIcon(L"bootsZanite",slotW*0,slotH*19,slotW*(0+1),slotH*(19+1))));
		texturesByName.insert(stringIconMap::value_type(L"bootsGravitite",new SimpleIcon(L"bootsGravitite",slotW*1,slotH*19,slotW*(1+1),slotH*(19+1))));

		// Aether Items - Row 20 (Swords)
		texturesByName.insert(stringIconMap::value_type(L"swordZanite",new SimpleIcon(L"swordZanite",slotW*0,slotH*20,slotW*(0+1),slotH*(20+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordGravitite",new SimpleIcon(L"swordGravitite",slotW*1,slotH*20,slotW*(1+1),slotH*(20+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordSkyroot",new SimpleIcon(L"swordSkyroot",slotW*2,slotH*20,slotW*(2+1),slotH*(20+1))));
		texturesByName.insert(stringIconMap::value_type(L"swordHolystone",new SimpleIcon(L"swordHolystone",slotW*3,slotH*20,slotW*(3+1),slotH*(20+1))));
		texturesByName.insert(stringIconMap::value_type(L"vampireBlade",new SimpleIcon(L"vampireBlade",slotW*4,slotH*20,slotW*(4+1),slotH*(20+1))));
		texturesByName.insert(stringIconMap::value_type(L"phoenixSword",new SimpleIcon(L"phoenixSword",slotW*5,slotH*20,slotW*(5+1),slotH*(20+1))));
		texturesByName.insert(stringIconMap::value_type(L"lightningKnife",new SimpleIcon(L"lightningKnife",slotW*6,slotH*20,slotW*(6+1),slotH*(20+1))));

		// Aether Items - Row 21 (Shovels)
		texturesByName.insert(stringIconMap::value_type(L"shovelZanite",new SimpleIcon(L"shovelZanite",slotW*0,slotH*21,slotW*(0+1),slotH*(21+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelGravitite",new SimpleIcon(L"shovelGravitite",slotW*1,slotH*21,slotW*(1+1),slotH*(21+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelSkyroot",new SimpleIcon(L"shovelSkyroot",slotW*2,slotH*21,slotW*(2+1),slotH*(21+1))));
		texturesByName.insert(stringIconMap::value_type(L"shovelHolystone",new SimpleIcon(L"shovelHolystone",slotW*3,slotH*21,slotW*(3+1),slotH*(21+1))));
		texturesByName.insert(stringIconMap::value_type(L"phoenixShovel",new SimpleIcon(L"phoenixShovel",slotW*5,slotH*21,slotW*(5+1),slotH*(21+1))));

		// Aether Items - Row 22 (Pickaxes)
		texturesByName.insert(stringIconMap::value_type(L"pickaxeZanite",new SimpleIcon(L"pickaxeZanite",slotW*0,slotH*22,slotW*(0+1),slotH*(22+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeGravitite",new SimpleIcon(L"pickaxeGravitite",slotW*1,slotH*22,slotW*(1+1),slotH*(22+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeSkyroot",new SimpleIcon(L"pickaxeSkyroot",slotW*2,slotH*22,slotW*(2+1),slotH*(22+1))));
		texturesByName.insert(stringIconMap::value_type(L"pickaxeHolystone",new SimpleIcon(L"pickaxeHolystone",slotW*3,slotH*22,slotW*(3+1),slotH*(22+1))));
		texturesByName.insert(stringIconMap::value_type(L"phoenixPickaxe",new SimpleIcon(L"phoenixPickaxe",slotW*5,slotH*22,slotW*(5+1),slotH*(22+1))));

		// Aether Items - Row 23 (Axes)
		texturesByName.insert(stringIconMap::value_type(L"hatchetZanite",new SimpleIcon(L"hatchetZanite",slotW*0,slotH*23,slotW*(0+1),slotH*(23+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetGravitite",new SimpleIcon(L"hatchetGravitite",slotW*1,slotH*23,slotW*(1+1),slotH*(23+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetSkyroot",new SimpleIcon(L"hatchetSkyroot",slotW*2,slotH*23,slotW*(2+1),slotH*(23+1))));
		texturesByName.insert(stringIconMap::value_type(L"hatchetHolystone",new SimpleIcon(L"hatchetHolystone",slotW*3,slotH*23,slotW*(3+1),slotH*(23+1))));
		texturesByName.insert(stringIconMap::value_type(L"phoenixAxe",new SimpleIcon(L"phoenixAxe",slotW*5,slotH*23,slotW*(5+1),slotH*(23+1))));

		// Special cases
		ClockTexture *dataClock = new ClockTexture();
		Icon *oldClock = texturesByName[L"clock"];
		dataClock->initUVs(oldClock->getU0(), oldClock->getV0(), oldClock->getU1(), oldClock->getV1() );
		delete oldClock;
		texturesByName[L"clock"] = dataClock;

		ClockTexture *clock = new ClockTexture(0, dataClock);
		oldClock = texturesByName[L"clockP0"];
		clock->initUVs(oldClock->getU0(), oldClock->getV0(), oldClock->getU1(), oldClock->getV1() );
		delete oldClock;
		texturesByName[L"clockP0"] = clock;

		clock = new ClockTexture(1, dataClock);
		oldClock = texturesByName[L"clockP1"];
		clock->initUVs(oldClock->getU0(), oldClock->getV0(), oldClock->getU1(), oldClock->getV1() );
		delete oldClock;
		texturesByName[L"clockP1"] = clock;

		clock = new ClockTexture(2, dataClock);
		oldClock = texturesByName[L"clockP2"];
		clock->initUVs(oldClock->getU0(), oldClock->getV0(), oldClock->getU1(), oldClock->getV1() );
		delete oldClock;
		texturesByName[L"clockP2"] = clock;

		clock = new ClockTexture(3, dataClock);
		oldClock = texturesByName[L"clockP3"];
		clock->initUVs(oldClock->getU0(), oldClock->getV0(), oldClock->getU1(), oldClock->getV1() );
		delete oldClock;
		texturesByName[L"clockP3"] = clock;

		CompassTexture *dataCompass = new CompassTexture();
		Icon *oldCompass = texturesByName[L"compass"];
		dataCompass->initUVs(oldCompass->getU0(), oldCompass->getV0(), oldCompass->getU1(), oldCompass->getV1() );
		delete oldCompass;
		texturesByName[L"compass"] = dataCompass;

		CompassTexture *compass = new CompassTexture(0, dataCompass);
		oldCompass = texturesByName[L"compassP0"];
		compass->initUVs(oldCompass->getU0(), oldCompass->getV0(), oldCompass->getU1(), oldCompass->getV1() );
		delete oldCompass;
		texturesByName[L"compassP0"] = compass;

		compass = new CompassTexture(1, dataCompass);
		oldCompass = texturesByName[L"compassP1"];
		compass->initUVs(oldCompass->getU0(), oldCompass->getV0(), oldCompass->getU1(), oldCompass->getV1() );
		delete oldCompass;
		texturesByName[L"compassP1"] = compass;

		compass = new CompassTexture(2, dataCompass);
		oldCompass = texturesByName[L"compassP2"];
		compass->initUVs(oldCompass->getU0(), oldCompass->getV0(), oldCompass->getU1(), oldCompass->getV1() );
		delete oldCompass;
		texturesByName[L"compassP2"] = compass;

		compass = new CompassTexture(3, dataCompass);
		oldCompass = texturesByName[L"compassP3"];
		compass->initUVs(oldCompass->getU0(), oldCompass->getV0(), oldCompass->getU1(), oldCompass->getV1() );
		delete oldCompass;
		texturesByName[L"compassP3"] = compass;
	}
	else
	{
		slotH = 1.0f/19.0f; // terrain.png is 256x304 = 16x19 grid
		texturesByName.insert(stringIconMap::value_type(L"grass_top",new SimpleIcon(L"grass_top",slotW*0,slotH*0,slotW*(0+1),slotH*(0+1))));
		texturesByName[L"grass_top"]->setFlags(Icon::IS_GRASS_TOP);			// 4J added for faster determination of texture type in tesselation
		texturesByName.insert(stringIconMap::value_type(L"stone",new SimpleIcon(L"stone",slotW*1,slotH*0,slotW*(1+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"dirt",new SimpleIcon(L"dirt",slotW*2,slotH*0,slotW*(2+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"grass_side",new SimpleIcon(L"grass_side",slotW*3,slotH*0,slotW*(3+1),slotH*(0+1))));
		texturesByName[L"grass_side"]->setFlags(Icon::IS_GRASS_SIDE);		// 4J added for faster determination of texture type in tesselation
		texturesByName.insert(stringIconMap::value_type(L"wood",new SimpleIcon(L"wood",slotW*4,slotH*0,slotW*(4+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"stoneslab_side",new SimpleIcon(L"stoneslab_side",slotW*5,slotH*0,slotW*(5+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"stoneslab_top",new SimpleIcon(L"stoneslab_top",slotW*6,slotH*0,slotW*(6+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"brick",new SimpleIcon(L"brick",slotW*7,slotH*0,slotW*(7+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"tnt_side",new SimpleIcon(L"tnt_side",slotW*8,slotH*0,slotW*(8+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"tnt_top",new SimpleIcon(L"tnt_top",slotW*9,slotH*0,slotW*(9+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"tnt_bottom",new SimpleIcon(L"tnt_bottom",slotW*10,slotH*0,slotW*(10+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"web",new SimpleIcon(L"web",slotW*11,slotH*0,slotW*(11+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"rose",new SimpleIcon(L"rose",slotW*12,slotH*0,slotW*(12+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"flower",new SimpleIcon(L"flower",slotW*13,slotH*0,slotW*(13+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"portal",new SimpleIcon(L"portal",slotW*14,slotH*0,slotW*(14+1),slotH*(0+1))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"portal",L"portal"));
		texturesByName.insert(stringIconMap::value_type(L"sapling",new SimpleIcon(L"sapling",slotW*15,slotH*0,slotW*(15+1),slotH*(0+1))));
		texturesByName.insert(stringIconMap::value_type(L"stonebrick",new SimpleIcon(L"stonebrick",slotW*0,slotH*1,slotW*(0+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"bedrock",new SimpleIcon(L"bedrock",slotW*1,slotH*1,slotW*(1+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"sand",new SimpleIcon(L"sand",slotW*2,slotH*1,slotW*(2+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"gravel",new SimpleIcon(L"gravel",slotW*3,slotH*1,slotW*(3+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"tree_side",new SimpleIcon(L"tree_side",slotW*4,slotH*1,slotW*(4+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"tree_top",new SimpleIcon(L"tree_top",slotW*5,slotH*1,slotW*(5+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"blockIron",new SimpleIcon(L"blockIron",slotW*6,slotH*1,slotW*(6+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"blockGold",new SimpleIcon(L"blockGold",slotW*7,slotH*1,slotW*(7+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"blockDiamond",new SimpleIcon(L"blockDiamond",slotW*8,slotH*1,slotW*(8+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"blockEmerald",new SimpleIcon(L"blockEmerald",slotW*9,slotH*1,slotW*(9+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"blockRedstone",new SimpleIcon(L"blockRedstone",slotW*10,slotH*1,slotW*(10+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"dropper_front",new SimpleIcon(L"dropper_front",slotW*11,slotH*1,slotW*(11+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"mushroom_red",new SimpleIcon(L"mushroom_red",slotW*12,slotH*1,slotW*(12+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"mushroom_brown",new SimpleIcon(L"mushroom_brown",slotW*13,slotH*1,slotW*(13+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"sapling_jungle",new SimpleIcon(L"sapling_jungle",slotW*14,slotH*1,slotW*(14+1),slotH*(1+1))));
		texturesByName.insert(stringIconMap::value_type(L"fire_0",new SimpleIcon(L"fire_0",slotW*15,slotH*1,slotW*(15+1),slotH*(1+1))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"fire_0",L"fire_0"));
		texturesByName.insert(stringIconMap::value_type(L"oreGold",new SimpleIcon(L"oreGold",slotW*0,slotH*2,slotW*(0+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"oreIron",new SimpleIcon(L"oreIron",slotW*1,slotH*2,slotW*(1+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"oreCoal",new SimpleIcon(L"oreCoal",slotW*2,slotH*2,slotW*(2+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"bookshelf",new SimpleIcon(L"bookshelf",slotW*3,slotH*2,slotW*(3+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"stoneMoss",new SimpleIcon(L"stoneMoss",slotW*4,slotH*2,slotW*(4+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"obsidian",new SimpleIcon(L"obsidian",slotW*5,slotH*2,slotW*(5+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"grass_side_overlay",new SimpleIcon(L"grass_side_overlay",slotW*6,slotH*2,slotW*(6+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"tallgrass",new SimpleIcon(L"tallgrass",slotW*7,slotH*2,slotW*(7+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"dispenser_front_vertical",new SimpleIcon(L"dispenser_front_vertical",slotW*8,slotH*2,slotW*(8+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"beacon",new SimpleIcon(L"beacon",slotW*9,slotH*2,slotW*(9+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"dropper_front_vertical",new SimpleIcon(L"dropper_front_vertical",slotW*10,slotH*2,slotW*(10+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"workbench_top",new SimpleIcon(L"workbench_top",slotW*11,slotH*2,slotW*(11+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"furnace_front",new SimpleIcon(L"furnace_front",slotW*12,slotH*2,slotW*(12+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"furnace_side",new SimpleIcon(L"furnace_side",slotW*13,slotH*2,slotW*(13+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"dispenser_front",new SimpleIcon(L"dispenser_front",slotW*14,slotH*2,slotW*(14+1),slotH*(2+1))));
		texturesByName.insert(stringIconMap::value_type(L"fire_1",new SimpleIcon(L"fire_1",slotW*15,slotH*1,slotW*(15+1),slotH*(1+1))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"fire_1",L"fire_1"));
		texturesByName.insert(stringIconMap::value_type(L"sponge",new SimpleIcon(L"sponge",slotW*0,slotH*3,slotW*(0+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"glass",new SimpleIcon(L"glass",slotW*1,slotH*3,slotW*(1+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"oreDiamond",new SimpleIcon(L"oreDiamond",slotW*2,slotH*3,slotW*(2+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"oreRedstone",new SimpleIcon(L"oreRedstone",slotW*3,slotH*3,slotW*(3+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"leaves",new SimpleIcon(L"leaves",slotW*4,slotH*3,slotW*(4+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"leaves_opaque",new SimpleIcon(L"leaves_opaque",slotW*5,slotH*3,slotW*(5+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"stonebricksmooth",new SimpleIcon(L"stonebricksmooth",slotW*6,slotH*3,slotW*(6+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"deadbush",new SimpleIcon(L"deadbush",slotW*7,slotH*3,slotW*(7+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"fern",new SimpleIcon(L"fern",slotW*8,slotH*3,slotW*(8+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"daylightDetector_top",new SimpleIcon(L"daylightDetector_top",slotW*9,slotH*3,slotW*(9+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"daylightDetector_side",new SimpleIcon(L"daylightDetector_side",slotW*10,slotH*3,slotW*(10+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"workbench_side",new SimpleIcon(L"workbench_side",slotW*11,slotH*3,slotW*(11+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"workbench_front",new SimpleIcon(L"workbench_front",slotW*12,slotH*3,slotW*(12+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"furnace_front_lit",new SimpleIcon(L"furnace_front_lit",slotW*13,slotH*3,slotW*(13+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"furnace_top",new SimpleIcon(L"furnace_top",slotW*14,slotH*3,slotW*(14+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"sapling_spruce",new SimpleIcon(L"sapling_spruce",slotW*15,slotH*3,slotW*(15+1),slotH*(3+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_0",new SimpleIcon(L"cloth_0",slotW*0,slotH*4,slotW*(0+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"mobSpawner",new SimpleIcon(L"mobSpawner",slotW*1,slotH*4,slotW*(1+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"snow",new SimpleIcon(L"snow",slotW*2,slotH*4,slotW*(2+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"ice",new SimpleIcon(L"ice",slotW*3,slotH*4,slotW*(3+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"snow_side",new SimpleIcon(L"snow_side",slotW*4,slotH*4,slotW*(4+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"cactus_top",new SimpleIcon(L"cactus_top",slotW*5,slotH*4,slotW*(5+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"cactus_side",new SimpleIcon(L"cactus_side",slotW*6,slotH*4,slotW*(6+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"cactus_bottom",new SimpleIcon(L"cactus_bottom",slotW*7,slotH*4,slotW*(7+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"clay",new SimpleIcon(L"clay",slotW*8,slotH*4,slotW*(8+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"reeds",new SimpleIcon(L"reeds",slotW*9,slotH*4,slotW*(9+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"musicBlock",new SimpleIcon(L"musicBlock",slotW*10,slotH*4,slotW*(10+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"jukebox_top",new SimpleIcon(L"jukebox_top",slotW*11,slotH*4,slotW*(11+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"waterlily",new SimpleIcon(L"waterlily",slotW*12,slotH*4,slotW*(12+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"mycel_side",new SimpleIcon(L"mycel_side",slotW*13,slotH*4,slotW*(13+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"mycel_top",new SimpleIcon(L"mycel_top",slotW*14,slotH*4,slotW*(14+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"sapling_birch",new SimpleIcon(L"sapling_birch",slotW*15,slotH*4,slotW*(15+1),slotH*(4+1))));
		texturesByName.insert(stringIconMap::value_type(L"torch",new SimpleIcon(L"torch",slotW*0,slotH*5,slotW*(0+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"doorWood_upper",new SimpleIcon(L"doorWood_upper",slotW*1,slotH*5,slotW*(1+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"doorIron_upper",new SimpleIcon(L"doorIron_upper",slotW*2,slotH*5,slotW*(2+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"ladder",new SimpleIcon(L"ladder",slotW*3,slotH*5,slotW*(3+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"trapdoor",new SimpleIcon(L"trapdoor",slotW*4,slotH*5,slotW*(4+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"fenceIron",new SimpleIcon(L"fenceIron",slotW*5,slotH*5,slotW*(5+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"farmland_wet",new SimpleIcon(L"farmland_wet",slotW*6,slotH*5,slotW*(6+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"farmland_dry",new SimpleIcon(L"farmland_dry",slotW*7,slotH*5,slotW*(7+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_0",new SimpleIcon(L"crops_0",slotW*8,slotH*5,slotW*(8+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_1",new SimpleIcon(L"crops_1",slotW*9,slotH*5,slotW*(9+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_2",new SimpleIcon(L"crops_2",slotW*10,slotH*5,slotW*(10+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_3",new SimpleIcon(L"crops_3",slotW*11,slotH*5,slotW*(11+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_4",new SimpleIcon(L"crops_4",slotW*12,slotH*5,slotW*(12+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_5",new SimpleIcon(L"crops_5",slotW*13,slotH*5,slotW*(13+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_6",new SimpleIcon(L"crops_6",slotW*14,slotH*5,slotW*(14+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"crops_7",new SimpleIcon(L"crops_7",slotW*15,slotH*5,slotW*(15+1),slotH*(5+1))));
		texturesByName.insert(stringIconMap::value_type(L"lever",new SimpleIcon(L"lever",slotW*0,slotH*6,slotW*(0+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"doorWood_lower",new SimpleIcon(L"doorWood_lower",slotW*1,slotH*6,slotW*(1+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"doorIron_lower",new SimpleIcon(L"doorIron_lower",slotW*2,slotH*6,slotW*(2+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"redtorch_lit",new SimpleIcon(L"redtorch_lit",slotW*3,slotH*6,slotW*(3+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"stonebricksmooth_mossy",new SimpleIcon(L"stonebricksmooth_mossy",slotW*4,slotH*6,slotW*(4+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"stonebricksmooth_cracked",new SimpleIcon(L"stonebricksmooth_cracked",slotW*5,slotH*6,slotW*(5+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"pumpkin_top",new SimpleIcon(L"pumpkin_top",slotW*6,slotH*6,slotW*(6+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"hellrock",new SimpleIcon(L"hellrock",slotW*7,slotH*6,slotW*(7+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"hellsand",new SimpleIcon(L"hellsand",slotW*8,slotH*6,slotW*(8+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"lightgem",new SimpleIcon(L"lightgem",slotW*9,slotH*6,slotW*(9+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"piston_top_sticky",new SimpleIcon(L"piston_top_sticky",slotW*10,slotH*6,slotW*(10+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"piston_top",new SimpleIcon(L"piston_top",slotW*11,slotH*6,slotW*(11+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"piston_side",new SimpleIcon(L"piston_side",slotW*12,slotH*6,slotW*(12+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"piston_bottom",new SimpleIcon(L"piston_bottom",slotW*13,slotH*6,slotW*(13+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"piston_inner_top",new SimpleIcon(L"piston_inner_top",slotW*14,slotH*6,slotW*(14+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"stem_straight",new SimpleIcon(L"stem_straight",slotW*15,slotH*6,slotW*(15+1),slotH*(6+1))));
		texturesByName.insert(stringIconMap::value_type(L"rail_turn",new SimpleIcon(L"rail_turn",slotW*0,slotH*7,slotW*(0+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_15",new SimpleIcon(L"cloth_15",slotW*1,slotH*7,slotW*(1+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_7",new SimpleIcon(L"cloth_7",slotW*2,slotH*7,slotW*(2+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"redtorch",new SimpleIcon(L"redtorch",slotW*3,slotH*7,slotW*(3+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"tree_spruce",new SimpleIcon(L"tree_spruce",slotW*4,slotH*7,slotW*(4+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"tree_birch",new SimpleIcon(L"tree_birch",slotW*5,slotH*7,slotW*(5+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"pumpkin_side",new SimpleIcon(L"pumpkin_side",slotW*6,slotH*7,slotW*(6+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"pumpkin_face",new SimpleIcon(L"pumpkin_face",slotW*7,slotH*7,slotW*(7+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"pumpkin_jack",new SimpleIcon(L"pumpkin_jack",slotW*8,slotH*7,slotW*(8+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"cake_top",new SimpleIcon(L"cake_top",slotW*9,slotH*7,slotW*(9+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"cake_side",new SimpleIcon(L"cake_side",slotW*10,slotH*7,slotW*(10+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"cake_inner",new SimpleIcon(L"cake_inner",slotW*11,slotH*7,slotW*(11+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"cake_bottom",new SimpleIcon(L"cake_bottom",slotW*12,slotH*7,slotW*(12+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"mushroom_skin_red",new SimpleIcon(L"mushroom_skin_red",slotW*13,slotH*7,slotW*(13+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"mushroom_skin_brown",new SimpleIcon(L"mushroom_skin_brown",slotW*14,slotH*7,slotW*(14+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"stem_bent",new SimpleIcon(L"stem_bent",slotW*15,slotH*7,slotW*(15+1),slotH*(7+1))));
		texturesByName.insert(stringIconMap::value_type(L"rail",new SimpleIcon(L"rail",slotW*0,slotH*8,slotW*(0+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_14",new SimpleIcon(L"cloth_14",slotW*1,slotH*8,slotW*(1+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_6",new SimpleIcon(L"cloth_6",slotW*2,slotH*8,slotW*(2+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"repeater",new SimpleIcon(L"repeater",slotW*3,slotH*8,slotW*(3+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"leaves_spruce",new SimpleIcon(L"leaves_spruce",slotW*4,slotH*8,slotW*(4+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"leaves_spruce_opaque",new SimpleIcon(L"leaves_spruce_opaque",slotW*5,slotH*8,slotW*(5+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"bed_feet_top",new SimpleIcon(L"bed_feet_top",slotW*6,slotH*8,slotW*(6+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"bed_head_top",new SimpleIcon(L"bed_head_top",slotW*7,slotH*8,slotW*(7+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"melon_side",new SimpleIcon(L"melon_side",slotW*8,slotH*8,slotW*(8+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"melon_top",new SimpleIcon(L"melon_top",slotW*9,slotH*8,slotW*(9+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"cauldron_top",new SimpleIcon(L"cauldron_top",slotW*10,slotH*8,slotW*(10+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"cauldron_inner",new SimpleIcon(L"cauldron_inner",slotW*11,slotH*8,slotW*(11+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"mushroom_skin_stem",new SimpleIcon(L"mushroom_skin_stem",slotW*13,slotH*8,slotW*(13+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"mushroom_inside",new SimpleIcon(L"mushroom_inside",slotW*14,slotH*8,slotW*(14+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"vine",new SimpleIcon(L"vine",slotW*15,slotH*8,slotW*(15+1),slotH*(8+1))));
		texturesByName.insert(stringIconMap::value_type(L"blockLapis",new SimpleIcon(L"blockLapis",slotW*0,slotH*9,slotW*(0+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_13",new SimpleIcon(L"cloth_13",slotW*1,slotH*9,slotW*(1+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_5",new SimpleIcon(L"cloth_5",slotW*2,slotH*9,slotW*(2+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"repeater_lit",new SimpleIcon(L"repeater_lit",slotW*3,slotH*9,slotW*(3+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"thinglass_top",new SimpleIcon(L"thinglass_top",slotW*4,slotH*9,slotW*(4+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"bed_feet_end",new SimpleIcon(L"bed_feet_end",slotW*5,slotH*9,slotW*(5+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"bed_feet_side",new SimpleIcon(L"bed_feet_side",slotW*6,slotH*9,slotW*(6+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"bed_head_side",new SimpleIcon(L"bed_head_side",slotW*7,slotH*9,slotW*(7+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"bed_head_end",new SimpleIcon(L"bed_head_end",slotW*8,slotH*9,slotW*(8+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"tree_jungle",new SimpleIcon(L"tree_jungle",slotW*9,slotH*9,slotW*(9+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"cauldron_side",new SimpleIcon(L"cauldron_side",slotW*10,slotH*9,slotW*(10+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"cauldron_bottom",new SimpleIcon(L"cauldron_bottom",slotW*11,slotH*9,slotW*(11+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"brewingStand_base",new SimpleIcon(L"brewingStand_base",slotW*12,slotH*9,slotW*(12+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"brewingStand",new SimpleIcon(L"brewingStand",slotW*13,slotH*9,slotW*(13+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"endframe_top",new SimpleIcon(L"endframe_top",slotW*14,slotH*9,slotW*(14+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"endframe_side",new SimpleIcon(L"endframe_side",slotW*15,slotH*9,slotW*(15+1),slotH*(9+1))));
		texturesByName.insert(stringIconMap::value_type(L"oreLapis",new SimpleIcon(L"oreLapis",slotW*0,slotH*10,slotW*(0+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_12",new SimpleIcon(L"cloth_12",slotW*1,slotH*10,slotW*(1+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_4",new SimpleIcon(L"cloth_4",slotW*2,slotH*10,slotW*(2+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"goldenRail",new SimpleIcon(L"goldenRail",slotW*3,slotH*10,slotW*(3+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"redstoneDust_cross",new SimpleIcon(L"redstoneDust_cross",slotW*4,slotH*10,slotW*(4+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"redstoneDust_line",new SimpleIcon(L"redstoneDust_line",slotW*5,slotH*10,slotW*(5+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"enchantment_top",new SimpleIcon(L"enchantment_top",slotW*6,slotH*10,slotW*(6+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"dragonEgg",new SimpleIcon(L"dragonEgg",slotW*7,slotH*10,slotW*(7+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"cocoa_2",new SimpleIcon(L"cocoa_2",slotW*8,slotH*10,slotW*(8+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"cocoa_1",new SimpleIcon(L"cocoa_1",slotW*9,slotH*10,slotW*(9+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"cocoa_0",new SimpleIcon(L"cocoa_0",slotW*10,slotH*10,slotW*(10+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"oreEmerald",new SimpleIcon(L"oreEmerald",slotW*11,slotH*10,slotW*(11+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"tripWireSource",new SimpleIcon(L"tripWireSource",slotW*12,slotH*10,slotW*(12+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"tripWire",new SimpleIcon(L"tripWire",slotW*13,slotH*10,slotW*(13+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"endframe_eye",new SimpleIcon(L"endframe_eye",slotW*14,slotH*10,slotW*(14+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"whiteStone",new SimpleIcon(L"whiteStone",slotW*15,slotH*10,slotW*(15+1),slotH*(10+1))));
		texturesByName.insert(stringIconMap::value_type(L"sandstone_top",new SimpleIcon(L"sandstone_top",slotW*0,slotH*11,slotW*(0+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_11",new SimpleIcon(L"cloth_11",slotW*1,slotH*11,slotW*(1+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_3",new SimpleIcon(L"cloth_3",slotW*2,slotH*11,slotW*(2+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"goldenRail_powered",new SimpleIcon(L"goldenRail_powered",slotW*3,slotH*11,slotW*(3+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"redstoneDust_cross_overlay",new SimpleIcon(L"redstoneDust_cross_overlay",slotW*4,slotH*11,slotW*(4+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"redstoneDust_line_overlay",new SimpleIcon(L"redstoneDust_line_overlay",slotW*5,slotH*11,slotW*(5+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"enchantment_side",new SimpleIcon(L"enchantment_side",slotW*6,slotH*11,slotW*(6+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"enchantment_bottom",new SimpleIcon(L"enchantment_bottom",slotW*7,slotH*11,slotW*(7+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"commandBlock",new SimpleIcon(L"commandBlock",slotW*8,slotH*11,slotW*(8+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"itemframe_back",new SimpleIcon(L"itemframe_back",slotW*9,slotH*11,slotW*(9+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"flowerPot",new SimpleIcon(L"flowerPot",slotW*10,slotH*11,slotW*(10+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"comparator",new SimpleIcon(L"comparator",slotW*11,slotH*11,slotW*(11+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"comparator_lit",new SimpleIcon(L"comparator_lit",slotW*12,slotH*11,slotW*(12+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"activatorRail",new SimpleIcon(L"activatorRail",slotW*13,slotH*11,slotW*(13+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"activatorRail_powered",new SimpleIcon(L"activatorRail_powered",slotW*14,slotH*11,slotW*(14+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherquartz",new SimpleIcon(L"netherquartz",slotW*15,slotH*11,slotW*(15+1),slotH*(11+1))));
		texturesByName.insert(stringIconMap::value_type(L"sandstone_side",new SimpleIcon(L"sandstone_side",slotW*0,slotH*12,slotW*(0+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_10",new SimpleIcon(L"cloth_10",slotW*1,slotH*12,slotW*(1+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_2",new SimpleIcon(L"cloth_2",slotW*2,slotH*12,slotW*(2+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"detectorRail",new SimpleIcon(L"detectorRail",slotW*3,slotH*12,slotW*(3+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"leaves_jungle",new SimpleIcon(L"leaves_jungle",slotW*4,slotH*12,slotW*(4+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"leaves_jungle_opaque",new SimpleIcon(L"leaves_jungle_opaque",slotW*5,slotH*12,slotW*(5+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"wood_spruce",new SimpleIcon(L"wood_spruce",slotW*6,slotH*12,slotW*(6+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"wood_jungle",new SimpleIcon(L"wood_jungle",slotW*7,slotH*12,slotW*(7+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"carrots_0",new SimpleIcon(L"carrots_0",slotW*8,slotH*12,slotW*(8+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"carrots_1",new SimpleIcon(L"carrots_1",slotW*9,slotH*12,slotW*(9+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"carrots_2",new SimpleIcon(L"carrots_2",slotW*10,slotH*12,slotW*(10+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"carrots_3",new SimpleIcon(L"carrots_3",slotW*11,slotH*12,slotW*(11+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"potatoes_0",new SimpleIcon(L"potatoes_0",slotW*8,slotH*12,slotW*(8+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"potatoes_1",new SimpleIcon(L"potatoes_1",slotW*9,slotH*12,slotW*(9+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"potatoes_2",new SimpleIcon(L"potatoes_2",slotW*10,slotH*12,slotW*(10+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"potatoes_3",new SimpleIcon(L"potatoes_3",slotW*12,slotH*12,slotW*(12+1),slotH*(12+1))));
		texturesByName.insert(stringIconMap::value_type(L"water",new SimpleIcon(L"water",slotW*13,slotH*12,slotW*(13+1),slotH*(12+1))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"water",L"water"));
		texturesByName.insert(stringIconMap::value_type(L"water_flow",new SimpleIcon(L"water_flow",slotW*14,slotH*12,slotW*(14+2),slotH*(12+2))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"water_flow",L"water_flow"));
		texturesByName.insert(stringIconMap::value_type(L"sandstone_bottom",new SimpleIcon(L"sandstone_bottom",slotW*0,slotH*13,slotW*(0+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_9",new SimpleIcon(L"cloth_9",slotW*1,slotH*13,slotW*(1+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_1",new SimpleIcon(L"cloth_1",slotW*2,slotH*13,slotW*(2+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"redstoneLight",new SimpleIcon(L"redstoneLight",slotW*3,slotH*13,slotW*(3+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"redstoneLight_lit",new SimpleIcon(L"redstoneLight_lit",slotW*4,slotH*13,slotW*(4+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"stonebricksmooth_carved",new SimpleIcon(L"stonebricksmooth_carved",slotW*5,slotH*13,slotW*(5+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"wood_birch",new SimpleIcon(L"wood_birch",slotW*6,slotH*13,slotW*(6+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"anvil_base",new SimpleIcon(L"anvil_base",slotW*7,slotH*13,slotW*(7+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"anvil_top_damaged_1",new SimpleIcon(L"anvil_top_damaged_1",slotW*8,slotH*13,slotW*(8+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"quartzblock_chiseled_top",new SimpleIcon(L"quartzblock_chiseled_top",slotW*9,slotH*13,slotW*(9+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"quartzblock_lines_top",new SimpleIcon(L"quartzblock_lines_top",slotW*10,slotH*13,slotW*(10+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"quartzblock_top",new SimpleIcon(L"quartzblock_top",slotW*11,slotH*13,slotW*(11+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"hopper",new SimpleIcon(L"hopper",slotW*12,slotH*13,slotW*(12+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"detectorRail_on",new SimpleIcon(L"detectorRail_on",slotW*13,slotH*13,slotW*(13+1),slotH*(13+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherBrick",new SimpleIcon(L"netherBrick",slotW*0,slotH*14,slotW*(0+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"cloth_8",new SimpleIcon(L"cloth_8",slotW*1,slotH*14,slotW*(1+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherStalk_0",new SimpleIcon(L"netherStalk_0",slotW*2,slotH*14,slotW*(2+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherStalk_1",new SimpleIcon(L"netherStalk_1",slotW*3,slotH*14,slotW*(3+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"netherStalk_2",new SimpleIcon(L"netherStalk_2",slotW*4,slotH*14,slotW*(4+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"sandstone_carved",new SimpleIcon(L"sandstone_carved",slotW*5,slotH*14,slotW*(5+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"sandstone_smooth",new SimpleIcon(L"sandstone_smooth",slotW*6,slotH*14,slotW*(6+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"anvil_top",new SimpleIcon(L"anvil_top",slotW*7,slotH*14,slotW*(7+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"anvil_top_damaged_2",new SimpleIcon(L"anvil_top_damaged_2",slotW*8,slotH*14,slotW*(8+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"quartzblock_chiseled",new SimpleIcon(L"quartzblock_chiseled",slotW*9,slotH*14,slotW*(9+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"quartzblock_lines",new SimpleIcon(L"quartzblock_lines",slotW*10,slotH*14,slotW*(10+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"quartzblock_side",new SimpleIcon(L"quartzblock_side",slotW*11,slotH*14,slotW*(11+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"hopper_inside",new SimpleIcon(L"hopper_inside",slotW*12,slotH*14,slotW*(12+1),slotH*(14+1))));
		texturesByName.insert(stringIconMap::value_type(L"lava",new SimpleIcon(L"lava",slotW*13,slotH*14,slotW*(13+1),slotH*(14+1))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"lava",L"lava"));
		texturesByName.insert(stringIconMap::value_type(L"lava_flow",new SimpleIcon(L"lava_flow",slotW*14,slotH*14,slotW*(14+2),slotH*(14+2))));
		texturesToAnimate.push_back(pair<wstring, wstring>(L"lava_flow",L"lava_flow"));
		texturesByName.insert(stringIconMap::value_type(L"destroy_0",new SimpleIcon(L"destroy_0",slotW*0,slotH*15,slotW*(0+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_1",new SimpleIcon(L"destroy_1",slotW*1,slotH*15,slotW*(1+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_2",new SimpleIcon(L"destroy_2",slotW*2,slotH*15,slotW*(2+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_3",new SimpleIcon(L"destroy_3",slotW*3,slotH*15,slotW*(3+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_4",new SimpleIcon(L"destroy_4",slotW*4,slotH*15,slotW*(4+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_5",new SimpleIcon(L"destroy_5",slotW*5,slotH*15,slotW*(5+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_6",new SimpleIcon(L"destroy_6",slotW*6,slotH*15,slotW*(6+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_7",new SimpleIcon(L"destroy_7",slotW*7,slotH*15,slotW*(7+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_8",new SimpleIcon(L"destroy_8",slotW*8,slotH*15,slotW*(8+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"destroy_9",new SimpleIcon(L"destroy_9",slotW*9,slotH*15,slotW*(9+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"quartzblock_bottom",new SimpleIcon(L"quartzblock_bottom",slotW*11,slotH*15,slotW*(11+1),slotH*(15+1))));
		texturesByName.insert(stringIconMap::value_type(L"hopper_top",new SimpleIcon(L"hopper_top",slotW*12,slotH*15,slotW*(12+1),slotH*(15+1))));

		// ---- Aether Block Textures (rows 16-18) ----
		// Row 16
		texturesByName.insert(stringIconMap::value_type(L"Aercloud",new SimpleIcon(L"Aercloud",slotW*0,slotH*16,slotW*(0+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"Aerogel",new SimpleIcon(L"Aerogel",slotW*1,slotH*16,slotW*(1+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"AetherDirt",new SimpleIcon(L"AetherDirt",slotW*2,slotH*16,slotW*(2+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"AetherGrassSide",new SimpleIcon(L"AetherGrassSide",slotW*3,slotH*16,slotW*(3+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"AetherGrassTop",new SimpleIcon(L"AetherGrassTop",slotW*4,slotH*16,slotW*(4+1),slotH*(16+1))));
		texturesByName[L"AetherGrassTop"]->setFlags(Icon::IS_GRASS_TOP);
		texturesByName.insert(stringIconMap::value_type(L"AmbrosiumOre",new SimpleIcon(L"AmbrosiumOre",slotW*5,slotH*16,slotW*(5+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"AmbrosiumTorch",new SimpleIcon(L"AmbrosiumTorch",slotW*6,slotH*16,slotW*(6+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"AngelicStone",new SimpleIcon(L"AngelicStone",slotW*7,slotH*16,slotW*(7+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"CarvedStone",new SimpleIcon(L"CarvedStone",slotW*8,slotH*16,slotW*(8+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"EnchanterSide",new SimpleIcon(L"EnchanterSide",slotW*9,slotH*16,slotW*(9+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"EnchanterTop",new SimpleIcon(L"EnchanterTop",slotW*10,slotH*16,slotW*(10+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"FreezerSide",new SimpleIcon(L"FreezerSide",slotW*11,slotH*16,slotW*(11+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"FreezerTop",new SimpleIcon(L"FreezerTop",slotW*12,slotH*16,slotW*(12+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"GoldenOak",new SimpleIcon(L"GoldenOak",slotW*13,slotH*16,slotW*(13+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"GoldenOakLeaves",new SimpleIcon(L"GoldenOakLeaves",slotW*14,slotH*16,slotW*(14+1),slotH*(16+1))));
		texturesByName.insert(stringIconMap::value_type(L"GoldenOakSapling",new SimpleIcon(L"GoldenOakSapling",slotW*15,slotH*16,slotW*(15+1),slotH*(16+1))));
		// Row 17
		texturesByName.insert(stringIconMap::value_type(L"GravititeOre",new SimpleIcon(L"GravititeOre",slotW*0,slotH*17,slotW*(0+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"HellfireStone",new SimpleIcon(L"HellfireStone",slotW*1,slotH*17,slotW*(1+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"Holystone",new SimpleIcon(L"Holystone",slotW*2,slotH*17,slotW*(2+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"Icestone",new SimpleIcon(L"Icestone",slotW*3,slotH*17,slotW*(3+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"IncubatorSide",new SimpleIcon(L"IncubatorSide",slotW*4,slotH*17,slotW*(4+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"IncubatorTop",new SimpleIcon(L"IncubatorTop",slotW*5,slotH*17,slotW*(5+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"LibraryLoreSide",new SimpleIcon(L"LibraryLoreSide",slotW*6,slotH*17,slotW*(6+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"LibraryLoreTop",new SimpleIcon(L"LibraryLoreTop",slotW*7,slotH*17,slotW*(7+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"LightAngelicStone",new SimpleIcon(L"LightAngelicStone",slotW*8,slotH*17,slotW*(8+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"LightCarvedStone",new SimpleIcon(L"LightCarvedStone",slotW*9,slotH*17,slotW*(9+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"LightHellfireStone",new SimpleIcon(L"LightHellfireStone",slotW*10,slotH*17,slotW*(10+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"MossyHolystone",new SimpleIcon(L"MossyHolystone",slotW*13,slotH*17,slotW*(13+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"PillarCarved",new SimpleIcon(L"PillarCarved",slotW*14,slotH*17,slotW*(14+1),slotH*(17+1))));
		texturesByName.insert(stringIconMap::value_type(L"PillarSide",new SimpleIcon(L"PillarSide",slotW*15,slotH*17,slotW*(15+1),slotH*(17+1))));
		// Row 18
		texturesByName.insert(stringIconMap::value_type(L"PillarTop",new SimpleIcon(L"PillarTop",slotW*0,slotH*18,slotW*(0+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"Plank",new SimpleIcon(L"Plank",slotW*1,slotH*18,slotW*(1+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"PurpleFlower",new SimpleIcon(L"PurpleFlower",slotW*2,slotH*18,slotW*(2+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"Quicksoil",new SimpleIcon(L"Quicksoil",slotW*3,slotH*18,slotW*(3+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"QuicksoilGlass",new SimpleIcon(L"QuicksoilGlass",slotW*4,slotH*18,slotW*(4+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"SkyrootLeaves",new SimpleIcon(L"SkyrootLeaves",slotW*5,slotH*18,slotW*(5+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"SkyrootLogSide",new SimpleIcon(L"SkyrootLogSide",slotW*6,slotH*18,slotW*(6+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"SkyrootLogTop",new SimpleIcon(L"SkyrootLogTop",slotW*7,slotH*18,slotW*(7+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"SkyrootSapling",new SimpleIcon(L"SkyrootSapling",slotW*8,slotH*18,slotW*(8+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"WhiteFlower",new SimpleIcon(L"WhiteFlower",slotW*9,slotH*18,slotW*(9+1),slotH*(18+1))));
		texturesByName.insert(stringIconMap::value_type(L"ZaniteOre",new SimpleIcon(L"ZaniteOre",slotW*10,slotH*18,slotW*(10+1),slotH*(18+1))));
	}
}
